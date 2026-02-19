#pragma once

#include <coroutine>
#include <cstdint>
#include <exception>

namespace Limitless
{
    struct WaitForSeconds final
    {
        float DurationSeconds = 0.0f;

        explicit WaitForSeconds(float durationSeconds = 0.0f)
            : DurationSeconds(durationSeconds)
        {
        }
    };

    struct WaitForFrames final
    {
        uint32_t FrameCount = 1u;

        explicit WaitForFrames(uint32_t frameCount = 1u)
            : FrameCount(frameCount)
        {
        }
    };

    inline WaitForFrames WaitForNextFrame(uint32_t frameCount = 1u)
    {
        return WaitForFrames(frameCount);
    }

    class CoroutineRoutine final
    {
    public:
        enum class YieldKind : uint8_t
        {
            WaitForFrames = 0,
            WaitForSeconds
        };

        struct promise_type final
        {
            float WaitDurationSeconds = 0.0f;
            uint32_t WaitFrameCount = 1u;
            YieldKind LastYieldKind = YieldKind::WaitForFrames;
            std::exception_ptr UnhandledException = nullptr;

            CoroutineRoutine get_return_object() noexcept
            {
                return CoroutineRoutine(std::coroutine_handle<promise_type>::from_promise(*this));
            }

            std::suspend_always initial_suspend() noexcept { return {}; }
            std::suspend_always final_suspend() noexcept { return {}; }
            void return_void() noexcept {}

            void unhandled_exception() noexcept
            {
                UnhandledException = std::current_exception();
            }

            std::suspend_always yield_value(const WaitForSeconds& waitInstruction) noexcept
            {
                LastYieldKind = YieldKind::WaitForSeconds;
                WaitDurationSeconds = waitInstruction.DurationSeconds;
                return {};
            }

            std::suspend_always yield_value(const WaitForFrames& waitInstruction) noexcept
            {
                LastYieldKind = YieldKind::WaitForFrames;
                WaitFrameCount = waitInstruction.FrameCount;
                return {};
            }

            std::suspend_always yield_value(float waitSeconds) noexcept
            {
                LastYieldKind = YieldKind::WaitForSeconds;
                WaitDurationSeconds = waitSeconds;
                return {};
            }

            std::suspend_always yield_value(uint32_t waitFrames) noexcept
            {
                LastYieldKind = YieldKind::WaitForFrames;
                WaitFrameCount = waitFrames;
                return {};
            }

            std::suspend_always yield_value(std::nullptr_t) noexcept
            {
                LastYieldKind = YieldKind::WaitForFrames;
                WaitFrameCount = 1u;
                return {};
            }
        };

        CoroutineRoutine() = default;

        explicit CoroutineRoutine(std::coroutine_handle<promise_type> handle)
            : m_Handle(handle)
        {
        }

        ~CoroutineRoutine()
        {
            if (m_Handle)
                m_Handle.destroy();
        }

        CoroutineRoutine(const CoroutineRoutine&) = delete;
        CoroutineRoutine& operator=(const CoroutineRoutine&) = delete;

        CoroutineRoutine(CoroutineRoutine&& other) noexcept
            : m_Handle(other.m_Handle)
        {
            other.m_Handle = {};
        }

        CoroutineRoutine& operator=(CoroutineRoutine&& other) noexcept
        {
            if (this == &other)
                return *this;

            if (m_Handle)
                m_Handle.destroy();

            m_Handle = other.m_Handle;
            other.m_Handle = {};
            return *this;
        }

        bool IsValid() const noexcept
        {
            return static_cast<bool>(m_Handle);
        }

        bool IsCompleted() const noexcept
        {
            return !m_Handle || m_Handle.done();
        }

        bool Resume()
        {
            if (!m_Handle || m_Handle.done())
                return false;

            m_Handle.resume();
            return !m_Handle.done();
        }

        const promise_type* GetPromise() const noexcept
        {
            return m_Handle ? &m_Handle.promise() : nullptr;
        }

    private:
        std::coroutine_handle<promise_type> m_Handle{};
    };

    struct CoroutineHandle final
    {
        uint64_t Identifier = 0;

        bool IsValid() const noexcept { return Identifier != 0; }
        explicit operator bool() const noexcept { return IsValid(); }
        bool operator==(const CoroutineHandle&) const = default;
    };
}
