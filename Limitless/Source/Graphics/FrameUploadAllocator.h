#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

namespace Limitless
{
    // -----------------------------------------------------------------------------
    // FrameUploadAllocator
    // A small, renderer-owned, per-frame ring allocator for CPU->GPU upload staging.
    //
    // Goals:
    // - Avoid per-upload heap allocations (e.g. std::vector growth in render commands)
    // - Keep upload bytes alive until the render thread consumes the command
    //
    // Model:
    // - We keep N frame buffers (triple-buffered by default).
    // - `BeginFrame(frameId)` selects and resets the buffer for that frame slot.
    // - `Allocate()` returns a pointer into the current frame buffer.
    //
    // IMPORTANT:
    // The engine currently waits for frame completion in `Renderer::SwapBuffers()` when the
    // render thread is enabled, so there is effectively 1 frame in flight. The triple buffer
    // here is defensive and future-proof (and safe for the single-thread fallback path).
    // -----------------------------------------------------------------------------
    class FrameUploadAllocator final
    {
    public:
        FrameUploadAllocator() = default;
        ~FrameUploadAllocator() = default;

        FrameUploadAllocator(const FrameUploadAllocator&) = delete;
        FrameUploadAllocator& operator=(const FrameUploadAllocator&) = delete;

        void Initialize(size_t bytesPerFrame = 16u * 1024u * 1024u, uint32_t bufferedFrames = 3);
        void Shutdown();

        void BeginFrame(uint64_t frameId);

        // Allocate `sizeBytes` aligned to `alignment` (must be power-of-two).
        // Returns nullptr on failure.
        void* Allocate(size_t sizeBytes, size_t alignment);

    private:
        struct FrameBuffer
        {
            std::unique_ptr<uint8_t[]> Data{};
            size_t CapacityBytes = 0;
            size_t HeadBytes = 0;
            uint64_t FrameId = 0;
        };

        static size_t AlignUp(size_t value, size_t alignment);
        void EnsureFrameCapacity(FrameBuffer& buffer, size_t requiredBytes);

        std::unique_ptr<FrameBuffer[]> m_Buffers{};
        uint32_t m_BufferCount = 0;
        uint32_t m_CurrentIndex = 0;
        size_t m_DefaultBytesPerFrame = 0;
        bool m_Initialized = false;
    };
}

