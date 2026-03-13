#pragma once

#include "Graphics/RenderCommand.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace Limitless
{
    // -----------------------------------------------------------------------------
    // FrameCommandArena
    // Frame-local page-based arena allocator for RenderCommand objects.
    //
    // - Allocates command objects out of fixed-size pages (per buffered frame slot).
    // - When a page is full, a new page is added. Old pages are **never freed**
    //   mid-frame, so pointers returned by Allocate remain valid until BeginFrame.
    // - Returns `UniqueRenderCommand` with a deleter that only runs the destructor (no free).
    //
    // This removes per-command heap allocations in hot render submission paths.
    // -----------------------------------------------------------------------------
    class FrameCommandArena final
    {
    public:
        FrameCommandArena() = default;
        ~FrameCommandArena() = default;

        FrameCommandArena(const FrameCommandArena&) = delete;
        FrameCommandArena& operator=(const FrameCommandArena&) = delete;

        void Initialize(size_t bytesPerPage = 2u * 1024u * 1024u, uint32_t bufferedFrames = 3);
        void Shutdown();

        void BeginFrame(uint64_t frameId);

        void* Allocate(size_t sizeBytes, size_t alignment);

        template<typename TCommand, typename... Args>
        UniqueRenderCommand Make(Args&&... args)
        {
            static_assert(std::is_base_of_v<RenderCommand, TCommand>, "TCommand must derive from RenderCommand");

            void* mem = Allocate(sizeof(TCommand), alignof(TCommand));
            if (!mem)
            {
                // Allocation failure is rare but possible if a frame submits an extreme number of commands.
                // Fallback to heap allocation so we never drop rendering work due to arena size.
                return UniqueRenderCommand(new TCommand(std::forward<Args>(args)...), RenderCommandDeleter{ &DeleteHeap<TCommand> });
            }

            TCommand* cmd = new (mem) TCommand(std::forward<Args>(args)...);
            return UniqueRenderCommand(cmd, RenderCommandDeleter{ &DestroyInArena<TCommand> });
        }

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
            uint32_t ActivePageIndex = 0;
            uint64_t FrameId = 0;
        };

        static size_t AlignUp(size_t value, size_t alignment);
        Page& GetOrAddPage(FrameSlot& slot, size_t minCapacity);

        template<typename TCommand>
        static void DestroyInArena(RenderCommand* base)
        {
            static_cast<TCommand*>(base)->~TCommand();
        }

        template<typename TCommand>
        static void DeleteHeap(RenderCommand* base)
        {
            delete static_cast<TCommand*>(base);
        }

        std::unique_ptr<FrameSlot[]> m_Slots{};
        uint32_t m_SlotCount = 0;
        uint32_t m_CurrentSlotIndex = 0;
        size_t m_DefaultPageSize = 0;
        bool m_Initialized = false;
    };
}

