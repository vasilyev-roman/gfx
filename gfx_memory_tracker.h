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
#if !defined(GFX_INCLUDE_MEMORY_TRACKER_H) && defined(GFX_ENABLE_MEMORY_TRACKER)
#define GFX_INCLUDE_MEMORY_TRACKER_H

#include "gfx_core.h"

// Call this before any allocation happens, so that the memory tracking starts.
GfxResult gfxMemoryTrackerInitialize(char const *name = nullptr);
GfxResult gfxMemoryTrackerInsertTag(char const *tag);
GfxResult gfxMemoryTrackerPushNamespace(char const *name);
GfxResult gfxMemoryTrackerPopNamespace();

#endif //! GFX_INCLUDE_MEMORY_TRACKER_H

// We need to hide the *Record* function declarations from gfx users,
// So we do some define magic. It's a bit complicated when it comes down to a single-TU build
#if defined(GFX_IMPLEMENTATION_DEFINE) && !defined(GFX_MEMORY_TRACKER_INTERNAL)
#define GFX_MEMORY_TRACKER_INTERNAL
#endif

#if !defined(GFX_INCLUDE_MEMORY_TRACKER_INTERNAL_H) && defined(GFX_ENABLE_MEMORY_TRACKER) && defined(GFX_MEMORY_TRACKER_INTERNAL)
#define GFX_INCLUDE_MEMORY_TRACKER_INTERNAL_H

class GfxTexture;

enum class GfxMemoryTrackerResourceType : std::uint8_t
{
    Buffer,
    Texture2D,
    Texture2DArray,
    Texture3D,
    TextureCube
};

GfxResult gfxMemoryTrackerRecordAllocation(uint64_t handle, GfxMemoryTrackerResourceType type, uint64_t size, char const* name = nullptr);
GfxResult gfxMemoryTrackerRecordRelease(uint64_t handle, GfxMemoryTrackerResourceType type);
GfxResult gfxMemoryTrackerRecordResize(uint64_t handle, GfxMemoryTrackerResourceType type, uint64_t new_size);
GfxMemoryTrackerResourceType gfxMemoryTrackerGetTextureType(GfxTexture const& texture);
bool gfxMemoryTrackerIsInitialized();

#endif //! GFX_INCLUDE_MEMORY_TRACKER_INTERNAL_H

#if defined(GFX_IMPLEMENTATION_DEFINE) && defined(GFX_ENABLE_MEMORY_TRACKER)

#pragma once
#include "gfx_memory_tracker.cpp"

#endif //! GFX_IMPLEMENTATION_DEFINE
