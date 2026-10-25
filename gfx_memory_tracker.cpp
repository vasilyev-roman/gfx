/****************************************************************************
MIT License

Copyright (c) 2026 Guillaume Boissé

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
****************************************************************************/

#define GFX_MEMORY_TRACKER_INTERNAL
#include "gfx_memory_tracker.h"
#include "gfx.h"

#include <chrono>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace
{
char const *GetGfxMemoryTrackerResourceTypeName(GfxMemoryTrackerResourceType type)
{
    switch(type)
    {
    case GfxMemoryTrackerResourceType::Buffer:          return "Buffer";
    case GfxMemoryTrackerResourceType::Texture2D:       return "Texture2D";
    case GfxMemoryTrackerResourceType::Texture2DArray:  return "Texture2DArray";
    case GfxMemoryTrackerResourceType::Texture3D:       return "Texture3D";
    case GfxMemoryTrackerResourceType::TextureCube:     return "TextureCube";
    default:
        break;
    }
    return "Unknown";
}

void WriteYamlString(FILE *file, char const *str)
{
    fputc('"', file);
    for(char const *c = str; *c != '\0'; ++c)
    {
        unsigned char const ch = (unsigned char)*c;
        if(ch == '"' || ch == '\\')
        {
            fputc('\\', file);
            fputc(ch, file);
        }
        else if(ch < 0x20)
            fprintf(file, "\\x%02x", ch);   // control characters are not allowed in YAML strings
        else
            fputc(ch, file);
    }
    fputc('"', file);
}

bool IsNameValid(char const *name)
{
    return ((name != nullptr) && (*name != '\0'));
}

struct TrackedAllocation
{
    std::string name;   // namespaced name at allocation time
    uint64_t size;
};

class GfxMemoryTrackerInternal
{
    GFX_NON_COPYABLE(GfxMemoryTrackerInternal);

    std::string filename_;
    FILE* file_ = nullptr;
    std::map<std::pair<GfxMemoryTrackerResourceType, uint64_t>, TrackedAllocation> allocations_;   // live allocations, by (type, handle)
    std::vector<std::string> namespaces_;  // names of the open namespaces, innermost last
    std::chrono::steady_clock::time_point start_time_;

public:
    explicit GfxMemoryTrackerInternal(char const *name)
    {
        start_time_ = std::chrono::steady_clock::now();

        if(!IsNameValid(name))
        {
            GFX_PRINT_WARNING("Memory tracker will use a default name for log file");
            filename_ = "Memory_log";
            return;
        }

        filename_ = name;
    }

    ~GfxMemoryTrackerInternal()
    {
        for(auto const& [key, allocation] : allocations_)
            writeEvent("leak", allocation.name.c_str(), &key.first, allocation.size);
        closeFile();
    }

    // Write a generalized event
    void writeEvent(char const* event, char const* name, GfxMemoryTrackerResourceType const* type = nullptr, uint64_t size = 0)
    {
        if(openFile() != kGfxResult_NoError)
            return;
        double const time_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_time_).count();
        fprintf(file_, "- event: %s\n", event);
        fprintf(file_, "  time_ms: %.3f\n", time_ms);
        fprintf(file_, "  name: ");
        if(IsNameValid(name))
            WriteYamlString(file_, name);
        else
            fprintf(file_, "null");
        fprintf(file_, "\n");
        if(type != nullptr)
        {
            fprintf(file_, "  type: %s\n", GetGfxMemoryTrackerResourceTypeName(*type));
            fprintf(file_, "  size: %llu\n", (unsigned long long)size);
        }
        fflush(file_);  // keep the file up to date in case of a crash or device removal
    }

    std::string getNamespacedName(char const* name)
    {
        std::string namespaced_name;
        for(std::string const& namespace_name : namespaces_)
            namespaced_name += namespace_name + '/';
        if(name != nullptr)
            namespaced_name += name;
        return namespaced_name;
    }

    GfxResult recordAllocation(uint64_t handle, GfxMemoryTrackerResourceType type, uint64_t size, char const* name)
    {
        std::string const namespaced_name = getNamespacedName(IsNameValid(name) ? name : "unnamed");
        if(!allocations_.try_emplace({ type, handle }, TrackedAllocation{ namespaced_name, size }).second)
            return GFX_SET_ERROR(kGfxResult_InvalidOperation, "Memory tracker: allocation recorded twice for the same handle");
        writeEvent("allocation", namespaced_name.c_str(), &type, size);
        return kGfxResult_NoError;
    }

    GfxResult recordRelease(uint64_t handle, GfxMemoryTrackerResourceType type)
    {
        auto const it = allocations_.find({ type, handle });
        if(it == allocations_.end())
            return GFX_SET_ERROR(kGfxResult_InvalidOperation, "Memory tracker: released an allocation that was never recorded (was it created before gfxMemoryTrackerInitialize?)");
        writeEvent("release", it->second.name.c_str(), &type, it->second.size);
        allocations_.erase(it);
        return kGfxResult_NoError;
    }

    GfxResult recordResize(uint64_t handle, GfxMemoryTrackerResourceType type, uint64_t new_size)
    {
        auto const it = allocations_.find({ type, handle });
        if(it == allocations_.end())
            return GFX_SET_ERROR(kGfxResult_InvalidOperation, "Memory tracker: resized an allocation that was never recorded (was it created before gfxMemoryTrackerInitialize?)");
        writeEvent("release", it->second.name.c_str(), &type, it->second.size);
        it->second.size = new_size;
        writeEvent("allocation", it->second.name.c_str(), &type, new_size);
        return kGfxResult_NoError;
    }

    void writeTag(char const *tag)
    {
        auto namespaced_tag = getNamespacedName(tag);
        writeEvent("tag", namespaced_tag.c_str());
    }

    void pushNamespace(char const *name)
    {
        std::string const namespaced_name = getNamespacedName(name);
        namespaces_.push_back(name);
        writeEvent("namespace_push", namespaced_name.c_str());
    }

    GfxResult popNamespace()
    {
        if(namespaces_.empty())
        {
            GFX_PRINT_WARNING("Memory tracker namespace popped without a matching push");
            return kGfxResult_InvalidOperation;
        }

        std::string const name = std::move(namespaces_.back());
        namespaces_.pop_back();
        std::string const namespaced_name = getNamespacedName(name.c_str());
        writeEvent("namespace_pop", namespaced_name.c_str());
        return kGfxResult_NoError;
    }

private:
    GfxResult openFile()
    {
        if(file_ != nullptr)
            return kGfxResult_NoError;
        int64_t const now_us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        std::string filename = filename_ + '_' + std::to_string(now_us) + ".yaml";
        if(fopen_s(&file_, filename.c_str(), "w") != 0 || file_ == nullptr)
        {
            file_ = nullptr;
            GFX_PRINT_ERROR(kGfxResult_InternalError, "Unable to create memory tracker file `%s'", filename.c_str());
            return kGfxResult_InternalError;
        }
        return kGfxResult_NoError;
    }

    void closeFile()
    {
        if(file_ == nullptr)
            return;
        fclose(file_);
        file_ = nullptr;
    }
};

std::unique_ptr<GfxMemoryTrackerInternal> memory_tracker_instance;
}   // namespace


GfxResult gfxMemoryTrackerInitialize(char const *name)
{
    if(memory_tracker_instance != nullptr)
        return kGfxResult_NoError;   // already initialized; later calls do nothing

    memory_tracker_instance = std::make_unique<GfxMemoryTrackerInternal>(name);

    return kGfxResult_NoError;
}

bool gfxMemoryTrackerIsInitialized()
{
    return memory_tracker_instance != nullptr;
}

GfxResult gfxMemoryTrackerRecordAllocation(uint64_t handle, GfxMemoryTrackerResourceType type, uint64_t size, char const* name)
{
    GFX_ASSERT(memory_tracker_instance != nullptr);
    if(handle == 0)
        return GFX_SET_ERROR(kGfxResult_InvalidParameter, "Cannot record an allocation with an invalid handle");
    return memory_tracker_instance->recordAllocation(handle, type, size, name);
}

GfxResult gfxMemoryTrackerRecordRelease(uint64_t handle, GfxMemoryTrackerResourceType type)
{
    GFX_ASSERT(memory_tracker_instance != nullptr);
    if(handle == 0)
        return GFX_SET_ERROR(kGfxResult_InvalidParameter, "Cannot release an allocation with an invalid handle");
    return memory_tracker_instance->recordRelease(handle, type);
}

GfxResult gfxMemoryTrackerRecordResize(uint64_t handle, GfxMemoryTrackerResourceType type, uint64_t new_size)
{
    GFX_ASSERT(memory_tracker_instance != nullptr);
    if(handle == 0)
        return GFX_SET_ERROR(kGfxResult_InvalidParameter, "Cannot resize an allocation with an invalid handle");
    return memory_tracker_instance->recordResize(handle, type, new_size);
}

GfxMemoryTrackerResourceType gfxMemoryTrackerGetTextureType(GfxTexture const& texture)
{
    if(texture.is2DArray()) return GfxMemoryTrackerResourceType::Texture2DArray;
    if(texture.is3D())      return GfxMemoryTrackerResourceType::Texture3D;
    if(texture.isCube())    return GfxMemoryTrackerResourceType::TextureCube;
    return GfxMemoryTrackerResourceType::Texture2D;
}

GfxResult gfxMemoryTrackerInsertTag(char const* tag)
{
    if(memory_tracker_instance == nullptr)
        return GFX_SET_ERROR(kGfxResult_InvalidOperation, "Memory tracker is not initialized");

    if(!IsNameValid(tag))
    {
        GFX_PRINT_ERROR(kGfxResult_InvalidParameter, "An empty tag can not be logged");
        return kGfxResult_InvalidParameter;
    }

    memory_tracker_instance->writeTag(tag);
    return kGfxResult_NoError;
}

GfxResult gfxMemoryTrackerPushNamespace(char const* name)
{
    if(memory_tracker_instance == nullptr)
        return GFX_SET_ERROR(kGfxResult_InvalidOperation, "Memory tracker is not initialized");

    if(!IsNameValid(name))
    {
        GFX_PRINT_ERROR(kGfxResult_InvalidParameter, "An empty name can not be logged");
        return kGfxResult_InvalidParameter;
    }

    memory_tracker_instance->pushNamespace(name);

    return kGfxResult_NoError;
}

GfxResult gfxMemoryTrackerPopNamespace()
{
    if(memory_tracker_instance == nullptr)
        return GFX_SET_ERROR(kGfxResult_InvalidOperation, "Memory tracker is not initialized");

    auto result = memory_tracker_instance->popNamespace();
    return result;
}
