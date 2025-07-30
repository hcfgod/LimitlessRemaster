#pragma once

#include <atomic>
#include <memory>
#include <array>
#include <cstddef>
#include <type_traits>
#include <optional>

namespace Limitless
{
    namespace Concurrency
    {
        // Lock-free single-producer single-consumer queue
        template<typename T, size_t Size>
        class LockFreeSPSCQueue
        {
            static_assert(Size > 0 && ((Size & (Size - 1)) == 0), "Size must be a power of 2");
            static_assert(std::is_nothrow_move_constructible_v<T>, "T must be nothrow move constructible");
            static_assert(std::is_nothrow_move_assignable_v<T>, "T must be nothrow move assignable");

        public:
            LockFreeSPSCQueue() : m_Head(0), m_Tail(0) {}

            // Try to push an item to the queue (thread-safe)
            bool TryPush(T&& item) noexcept
            {
                size_t currentTail = m_Tail.load(std::memory_order_relaxed);
                size_t nextTail = (currentTail + 1) & (Size - 1);
                
                if (nextTail == m_Head.load(std::memory_order_acquire))
                    return false; // Queue is full
                
                m_Buffer[currentTail] = std::move(item);
                m_Tail.store(nextTail, std::memory_order_release);
                return true;
            }

            // Try to pop an item from the queue (thread-safe)
            std::optional<T> TryPop() noexcept
            {
                size_t currentHead = m_Head.load(std::memory_order_relaxed);
                
                if (currentHead == m_Tail.load(std::memory_order_acquire))
                    return std::nullopt; // Queue is empty
                
                T item = std::move(m_Buffer[currentHead]);
                m_Head.store((currentHead + 1) & (Size - 1), std::memory_order_release);
                return std::move(item);
            }

            // Check if queue is empty
            bool IsEmpty() const noexcept
            {
                return m_Head.load(std::memory_order_acquire) == m_Tail.load(std::memory_order_acquire);
            }

            // Check if queue is full
            bool IsFull() const noexcept
            {
                size_t nextTail = (m_Tail.load(std::memory_order_acquire) + 1) & (Size - 1);
                return nextTail == m_Head.load(std::memory_order_acquire);
            }

            // Get approximate size (not exact due to concurrent access)
            size_t GetSize() const noexcept
            {
                size_t head = m_Head.load(std::memory_order_acquire);
                size_t tail = m_Tail.load(std::memory_order_acquire);
                return (tail - head) & (Size - 1);
            }

            // Clear the queue (not thread-safe, use with caution)
            void Clear() noexcept
            {
                m_Head.store(0, std::memory_order_relaxed);
                m_Tail.store(0, std::memory_order_relaxed);
            }

        private:
            std::array<T, Size> m_Buffer;
            std::atomic<size_t> m_Head;
            std::atomic<size_t> m_Tail;
        };

        // Lock-free multi-producer multi-consumer queue using CAS operations
        template<typename T, size_t Size>
        class LockFreeMPMCQueue
        {
            static_assert(Size > 0 && ((Size & (Size - 1)) == 0), "Size must be a power of 2");
            static_assert(std::is_nothrow_move_constructible_v<T>, "T must be nothrow move constructible");
            static_assert(std::is_nothrow_move_assignable_v<T>, "T must be nothrow move assignable");

        public:
            LockFreeMPMCQueue() : m_Head(0), m_Tail(0) {}

            // Try to push an item to the queue (thread-safe, multiple producers)
            bool TryPush(T&& item) noexcept
            {
                size_t currentTail = m_Tail.load(std::memory_order_relaxed);
                size_t nextTail = (currentTail + 1) & (Size - 1);
                
                // Check if queue is full
                if (nextTail == m_Head.load(std::memory_order_acquire))
                    return false;
                
                // Try to reserve the slot
                if (!m_Tail.compare_exchange_weak(currentTail, nextTail, 
                                                 std::memory_order_release, 
                                                 std::memory_order_relaxed))
                    return false;
                
                // Store the item
                m_Buffer[currentTail] = std::move(item);
                return true;
            }

            // Try to pop an item from the queue (thread-safe, multiple consumers)
            std::optional<T> TryPop() noexcept
            {
                size_t currentHead = m_Head.load(std::memory_order_relaxed);
                
                // Check if queue is empty
                if (currentHead == m_Tail.load(std::memory_order_acquire))
                    return std::nullopt;
                
                // Try to reserve the slot
                size_t nextHead = (currentHead + 1) & (Size - 1);
                if (!m_Head.compare_exchange_weak(currentHead, nextHead,
                                                 std::memory_order_release,
                                                 std::memory_order_relaxed))
                    return std::nullopt;
                
                // Load the item
                T item = std::move(m_Buffer[currentHead]);
                return std::move(item);
            }

            // Check if queue is empty
            bool IsEmpty() const noexcept
            {
                return m_Head.load(std::memory_order_acquire) == m_Tail.load(std::memory_order_acquire);
            }

            // Check if queue is full
            bool IsFull() const noexcept
            {
                size_t nextTail = (m_Tail.load(std::memory_order_acquire) + 1) & (Size - 1);
                return nextTail == m_Head.load(std::memory_order_acquire);
            }

            // Get approximate size (not exact due to concurrent access)
            size_t GetSize() const noexcept
            {
                size_t head = m_Head.load(std::memory_order_acquire);
                size_t tail = m_Tail.load(std::memory_order_acquire);
                return (tail - head) & (Size - 1);
            }

            // Clear the queue (not thread-safe, use with caution)
            void Clear() noexcept
            {
                m_Head.store(0, std::memory_order_relaxed);
                m_Tail.store(0, std::memory_order_relaxed);
            }

        private:
            alignas(64) std::array<T, Size> m_Buffer; // Cache line aligned
            alignas(64) std::atomic<size_t> m_Head;   // Cache line aligned
            alignas(64) std::atomic<size_t> m_Tail;   // Cache line aligned
        };

        // Thread-safe object pool for frequently allocated objects
        template<typename T, size_t PoolSize = 64>
        class ObjectPool
        {
        public:
            ObjectPool() = default;
            ~ObjectPool() = default;

            // Get an object from the pool or create a new one
            std::unique_ptr<T> Acquire() noexcept
            {
                std::unique_ptr<T> obj = TryPopFromPool();
                if (!obj)
                {
                    obj = std::make_unique<T>();
                }
                return obj;
            }

            // Return an object to the pool
            void Release(std::unique_ptr<T> obj) noexcept
            {
                if (obj && TryPushToPool(std::move(obj)))
                {
                    // Successfully returned to pool
                }
                // If pool is full, object will be automatically destroyed
            }

            // Clear the pool
            void Clear() noexcept
            {
                m_Pool.Clear();
            }

        private:
            bool TryPushToPool(std::unique_ptr<T> obj) noexcept
            {
                return m_Pool.TryPush(std::move(obj));
            }

            std::unique_ptr<T> TryPopFromPool() noexcept
            {
                auto result = m_Pool.TryPop();
                return result ? std::move(*result) : nullptr;
            }

            LockFreeSPSCQueue<std::unique_ptr<T>, PoolSize> m_Pool;
        };

        // Thread-safe work stealing queue for task scheduling
        template<typename T, size_t Size = 1024>
        class WorkStealingQueue
        {
        public:
            WorkStealingQueue() : m_Bottom(0), m_Top(0) {}

            // Push item to the bottom (owner thread only)
            void Push(T&& item) noexcept
            {
                size_t bottom = m_Bottom.load(std::memory_order_relaxed);
                m_Buffer[bottom & (Size - 1)] = std::move(item);
                m_Bottom.store(bottom + 1, std::memory_order_release);
            }

            // Pop item from the bottom (owner thread only)
            std::optional<T> Pop() noexcept
            {
                size_t bottom = m_Bottom.load(std::memory_order_relaxed) - 1;
                m_Bottom.store(bottom, std::memory_order_relaxed);
                
                size_t top = m_Top.load(std::memory_order_acquire);
                if (top > bottom)
                {
                    m_Bottom.store(bottom + 1, std::memory_order_relaxed);
                    return std::nullopt;
                }
                
                T item = std::move(m_Buffer[bottom & (Size - 1)]);
                
                if (top == bottom)
                {
                    if (!m_Top.compare_exchange_strong(top, top + 1,
                                                      std::memory_order_seq_cst,
                                                      std::memory_order_relaxed))
                    {
                        item = T{}; // Failed to steal
                    }
                    m_Bottom.store(bottom + 1, std::memory_order_relaxed);
                }
                
                return std::move(item);
            }

            // Steal item from the top (other threads)
            std::optional<T> Steal() noexcept
            {
                size_t top = m_Top.load(std::memory_order_acquire);
                size_t bottom = m_Bottom.load(std::memory_order_acquire);
                
                if (top >= bottom)
                    return std::nullopt;
                
                T item = std::move(m_Buffer[top & (Size - 1)]);
                
                if (!m_Top.compare_exchange_strong(top, top + 1,
                                                  std::memory_order_seq_cst,
                                                  std::memory_order_relaxed))
                {
                    return std::nullopt;
                }
                
                return std::move(item);
            }

            // Check if queue is empty
            bool IsEmpty() const noexcept
            {
                size_t top = m_Top.load(std::memory_order_acquire);
                size_t bottom = m_Bottom.load(std::memory_order_acquire);
                return top >= bottom;
            }

            // Get approximate size
            size_t GetSize() const noexcept
            {
                size_t top = m_Top.load(std::memory_order_acquire);
                size_t bottom = m_Bottom.load(std::memory_order_acquire);
                return bottom > top ? bottom - top : 0;
            }

        private:
            alignas(64) std::array<T, Size> m_Buffer;
            alignas(64) std::atomic<size_t> m_Bottom;
            alignas(64) std::atomic<size_t> m_Top;
        };
    }
} 