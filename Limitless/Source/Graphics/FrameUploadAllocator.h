#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace Limitless
{
    // -----------------------------------------------------------------------------
    // FrameUploadAllocator
    // A small, renderer-owned, per-frame page allocator for CPU->GPU upload staging.
    //
    // Goals:
    // - Avoid per-upload heap allocations (e.g. std::vector growth in render commands)
    // - Keep upload bytes alive until the render thread consumes the command
    // - **Never invalidate** previously returned pointers within the same frame
    //
    // Model:
    // - We keep N frame slots (triple-buffered by default).
    // - Each slot holds a list of fixed-size pages.
    // - `BeginFrame(frameId)` selects the slot and resets page heads (pages are reused).
    // - `Allocate()` returns a pointer into the current page, or appends a new page.
    // - Old pages are kept alive until the next `BeginFrame` for the same slot,
    //   so pointers remain valid throughout the frame.
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

        void Initialize(size_t bytesPerPage = 16u * 1024u * 1024u, uint32_t bufferedFrames = 3);
        void Shutdown();

        void BeginFrame(uint64_t frameId);

        // Allocate `sizeBytes` aligned to `alignment` (must be power-of-two).
        // Returns nullptr on failure.
        void* Allocate(size_t sizeBytes, size_t alignment);

    private:
        struct Page
        {
            std::unique_ptr<uint8_t[]> Data{};
            size_t CapacityBytes = 0;
            size_t HeadBytes = 0;
        };

        struct FrameSlot
        {
            std::vector<Page> Pages;
            uint32_t ActivePageIndex = 0;   // index of the page currently being allocated from
            uint64_t FrameId = 0;
        };

        static size_t AlignUp(size_t value, size_t alignment);
        Page& GetOrAddPage(FrameSlot& slot, size_t minCapacity);

        std::unique_ptr<FrameSlot[]> m_Slots{};
        uint32_t m_SlotCount = 0;
        uint32_t m_CurrentSlotIndex = 0;
        size_t m_DefaultPageSize = 0;
        bool m_Initialized = false;
    };
}

