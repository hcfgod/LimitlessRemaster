#include "Graphics/FrameUploadAllocator.h"

#include "Core/Debug/Log.h"

#include <algorithm>
#include <cstring>

namespace Limitless
{
    size_t FrameUploadAllocator::AlignUp(size_t value, size_t alignment)
    {
        return (value + (alignment - 1)) & ~(alignment - 1);
    }

    void FrameUploadAllocator::Initialize(size_t bytesPerPage, uint32_t bufferedFrames)
    {
        if (m_Initialized)
        {
            return;
        }

        if (bufferedFrames == 0)
        {
            bufferedFrames = 1;
        }

        m_DefaultPageSize = std::max<size_t>(bytesPerPage, 1024u);
        m_SlotCount = bufferedFrames;
        m_Slots = std::make_unique<FrameSlot[]>(m_SlotCount);
        m_CurrentSlotIndex = 0;

        // Pre-allocate one page per slot.
        for (uint32_t i = 0; i < m_SlotCount; ++i)
        {
            m_Slots[i].ActivePageIndex = 0;
            m_Slots[i].FrameId = 0;
            m_Slots[i].Pages.resize(1);
            m_Slots[i].Pages[0].CapacityBytes = m_DefaultPageSize;
            m_Slots[i].Pages[0].HeadBytes = 0;
            m_Slots[i].Pages[0].Data = std::make_unique<uint8_t[]>(m_DefaultPageSize);
        }

        m_Initialized = true;
        LT_CORE_INFO("FrameUploadAllocator initialized (BytesPerPage={}, BufferedFrames={})", m_DefaultPageSize, m_SlotCount);
    }

    void FrameUploadAllocator::Shutdown()
    {
        m_Slots.reset();
        m_SlotCount = 0;
        m_CurrentSlotIndex = 0;
        m_DefaultPageSize = 0;
        m_Initialized = false;
    }

    void FrameUploadAllocator::BeginFrame(uint64_t frameId)
    {
        if (!m_Initialized || m_SlotCount == 0)
        {
            return;
        }

        m_CurrentSlotIndex = static_cast<uint32_t>(frameId % static_cast<uint64_t>(m_SlotCount));
        FrameSlot& slot = m_Slots[m_CurrentSlotIndex];

        // Reset all page heads but keep the memory alive for reuse.
        for (auto& page : slot.Pages)
        {
            page.HeadBytes = 0;
        }
        slot.ActivePageIndex = 0;
        slot.FrameId = frameId;
    }

    FrameUploadAllocator::Page& FrameUploadAllocator::GetOrAddPage(FrameSlot& slot, size_t minCapacity)
    {
        // Advance past the current page.
        slot.ActivePageIndex++;

        // Try to reuse an existing page that is large enough.
        if (slot.ActivePageIndex < static_cast<uint32_t>(slot.Pages.size()))
        {
            Page& existing = slot.Pages[slot.ActivePageIndex];
            if (existing.CapacityBytes >= minCapacity)
            {
                return existing;
            }
            // Existing page is too small; replace its buffer with a larger one.
            existing.CapacityBytes = minCapacity;
            existing.Data = std::make_unique<uint8_t[]>(minCapacity);
            existing.HeadBytes = 0;
            return existing;
        }

        // Need a brand-new page.
        const size_t newPageSize = std::max(m_DefaultPageSize, minCapacity);
        Page newPage;
        newPage.CapacityBytes = newPageSize;
        newPage.HeadBytes = 0;
        newPage.Data = std::make_unique<uint8_t[]>(newPageSize);
        slot.Pages.push_back(std::move(newPage));
        return slot.Pages.back();
    }

    void* FrameUploadAllocator::Allocate(size_t sizeBytes, size_t alignment)
    {
        if (!m_Initialized || !m_Slots || m_SlotCount == 0)
        {
            return nullptr;
        }

        if (sizeBytes == 0)
        {
            return nullptr;
        }

        if (alignment == 0 || (alignment & (alignment - 1)) != 0)
        {
            LT_CORE_WARN("FrameUploadAllocator::Allocate: alignment must be power-of-two (got {})", alignment);
            return nullptr;
        }

        FrameSlot& slot = m_Slots[m_CurrentSlotIndex];
        Page& activePage = slot.Pages[slot.ActivePageIndex];

        const size_t alignedHead = AlignUp(activePage.HeadBytes, alignment);
        const size_t required = alignedHead + sizeBytes;

        // Fast path: allocation fits in the current page.
        if (required <= activePage.CapacityBytes)
        {
            void* out = activePage.Data.get() + alignedHead;
            activePage.HeadBytes = required;
            return out;
        }

        // Slow path: spill to the next page. The current page stays alive so
        // previously returned pointers remain valid for the rest of the frame.
        Page& newPage = GetOrAddPage(slot, sizeBytes + alignment);
        const size_t newAlignedHead = AlignUp(newPage.HeadBytes, alignment);
        const size_t newRequired = newAlignedHead + sizeBytes;

        if (newRequired > newPage.CapacityBytes)
        {
            LT_CORE_ERROR("FrameUploadAllocator: new page too small ({} < {})", newPage.CapacityBytes, newRequired);
            return nullptr;
        }

        void* out = newPage.Data.get() + newAlignedHead;
        newPage.HeadBytes = newRequired;
        return out;
    }
}

