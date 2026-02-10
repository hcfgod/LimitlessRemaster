#include "Graphics/FrameCommandArena.h"

#include "Core/Debug/Log.h"

#include <algorithm>
#include <cstring>

namespace Limitless
{
    size_t FrameCommandArena::AlignUp(size_t value, size_t alignment)
    {
        return (value + (alignment - 1)) & ~(alignment - 1);
    }

    void FrameCommandArena::Initialize(size_t bytesPerFrame, uint32_t bufferedFrames)
    {
        if (m_Initialized)
        {
            return;
        }

        if (bufferedFrames == 0)
        {
            bufferedFrames = 1;
        }

        m_DefaultBytesPerFrame = std::max<size_t>(bytesPerFrame, 1024u);
        m_BufferCount = bufferedFrames;
        m_Buffers = std::make_unique<FrameBuffer[]>(m_BufferCount);
        m_CurrentIndex = 0;

        for (uint32_t i = 0; i < m_BufferCount; ++i)
        {
            m_Buffers[i].CapacityBytes = m_DefaultBytesPerFrame;
            m_Buffers[i].HeadBytes = 0;
            m_Buffers[i].FrameId = 0;
            m_Buffers[i].Data = std::make_unique<uint8_t[]>(m_Buffers[i].CapacityBytes);
        }

        m_Initialized = true;
        LT_CORE_INFO("FrameCommandArena initialized (BytesPerFrame={}, BufferedFrames={})", m_DefaultBytesPerFrame, m_BufferCount);
    }

    void FrameCommandArena::Shutdown()
    {
        m_Buffers.reset();
        m_BufferCount = 0;
        m_CurrentIndex = 0;
        m_DefaultBytesPerFrame = 0;
        m_Initialized = false;
    }

    void FrameCommandArena::BeginFrame(uint64_t frameId)
    {
        if (!m_Initialized || m_BufferCount == 0)
        {
            return;
        }

        m_CurrentIndex = static_cast<uint32_t>(frameId % static_cast<uint64_t>(m_BufferCount));
        FrameBuffer& buffer = m_Buffers[m_CurrentIndex];

        buffer.HeadBytes = 0;
        buffer.FrameId = frameId;
    }

    void FrameCommandArena::EnsureFrameCapacity(FrameBuffer& buffer, size_t requiredBytes)
    {
        if (requiredBytes <= buffer.CapacityBytes)
        {
            return;
        }

        size_t newCapacity = buffer.CapacityBytes;
        while (newCapacity < requiredBytes)
        {
            newCapacity *= 2;
        }

        auto newData = std::make_unique<uint8_t[]>(newCapacity);
        if (buffer.Data && buffer.HeadBytes > 0)
        {
            std::memcpy(newData.get(), buffer.Data.get(), buffer.HeadBytes);
        }

        buffer.Data = std::move(newData);
        buffer.CapacityBytes = newCapacity;
    }

    void* FrameCommandArena::Allocate(size_t sizeBytes, size_t alignment)
    {
        if (!m_Initialized || !m_Buffers || m_BufferCount == 0)
        {
            return nullptr;
        }

        if (sizeBytes == 0)
        {
            return nullptr;
        }

        if (alignment == 0 || (alignment & (alignment - 1)) != 0)
        {
            LT_CORE_WARN("FrameCommandArena::Allocate: alignment must be power-of-two (got {})", alignment);
            return nullptr;
        }

        FrameBuffer& buffer = m_Buffers[m_CurrentIndex];
        const size_t alignedHead = AlignUp(buffer.HeadBytes, alignment);
        const size_t required = alignedHead + sizeBytes;

        EnsureFrameCapacity(buffer, required);
        if (!buffer.Data || required > buffer.CapacityBytes)
        {
            return nullptr;
        }

        void* out = buffer.Data.get() + alignedHead;
        buffer.HeadBytes = required;
        return out;
    }
}

