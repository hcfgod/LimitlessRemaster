#include "EventSystem.h"
#include "Debug/Log.h"
#include <algorithm>
#include <chrono>
#include <sstream>

namespace Limitless
{
    // NOTE: This type is forward-declared in `EventSystem.h` and friended by `EventSystem`.
    // It must live in the `Limitless` namespace (not an anonymous namespace) so friendship applies.
    class EventSystemOperationGuard final
    {
    public:
        explicit EventSystemOperationGuard(EventSystem& system) noexcept
            : m_System(system)
            , m_Active(m_System.BeginOperation())
        {
        }

        ~EventSystemOperationGuard()
        {
            if (m_Active)
            {
                m_System.EndOperation();
            }
        }

        EventSystemOperationGuard(const EventSystemOperationGuard&) = delete;
        EventSystemOperationGuard& operator=(const EventSystemOperationGuard&) = delete;

        bool IsActive() const noexcept { return m_Active; }

    private:
        EventSystem& m_System;
        bool m_Active = false;
    };

    // Event base class implementation
    Event::Event(EventType type, EventPriority priority)
        : m_Type(type), m_Priority(priority), m_Timestamp(std::chrono::system_clock::now())
    {
    }

    std::string Event::ToString() const
    {
        std::stringstream ss;
        ss << GetCategory() << "::" << GetName() << " (Priority: " << static_cast<int>(m_Priority) << ")";
        return ss.str();
    }

    // EventDispatcher implementation
    void EventDispatcher::Dispatch(Event& event)
    {
        // Copy dispatch state under lock so we don't hold the mutex while invoking user code.
        // This avoids deadlocks if callbacks/listeners register or remove handlers during dispatch.
        std::function<bool(const Event&)> eventFilter;
        std::vector<ListenerEntry> listeners;
        std::vector<CallbackEntry> callbacks;
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            eventFilter = m_EventFilter;
            listeners = m_Listeners;

            auto it = m_Callbacks.find(event.GetType());
            if (it != m_Callbacks.end())
            {
                callbacks = it->second;
            }
        }

        // Check if event filter exists and if it should filter this event
        if (eventFilter && !eventFilter(event))
        {
            m_EventsFiltered.fetch_add(1, std::memory_order_relaxed);
            LT_CORE_DEBUG("Event filtered out: {}", event.ToString());
            return; // Event is filtered out, don't dispatch
        }

        m_TotalEventsDispatched.fetch_add(1, std::memory_order_relaxed);
        auto startTime = std::chrono::high_resolution_clock::now();

        // Dispatch to callbacks (priority order is maintained at registration time).
        for (auto& callbackEntry : callbacks)
        {
            if (event.IsHandled())
                break;

            try
            {
                if (callbackEntry.Callback)
                {
                    callbackEntry.Callback(event);
                    m_EventsHandled.fetch_add(1, std::memory_order_relaxed);
                }
            }
            catch (const std::exception& e)
            {
                LT_CORE_ERROR("Exception in event callback: {}", e.what());
            }
        }

        // Dispatch to listeners (sorted by priority on insertion).
        for (auto& listenerEntry : listeners)
        {
            if (event.IsHandled())
                break;

            if (!listenerEntry.listener)
                continue;

            try
            {
                if (listenerEntry.listener->ShouldReceiveEvent(event))
                {
                    listenerEntry.listener->OnEvent(event);
                    m_EventsHandled.fetch_add(1, std::memory_order_relaxed);
                }
            }
            catch (const std::exception& e)
            {
                LT_CORE_ERROR("Exception in event listener: {}", e.what());
            }
        }

        auto endTime = std::chrono::high_resolution_clock::now();
        const auto dispatchTime = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime).count();
        m_TotalDispatchTimeMicroseconds.fetch_add(dispatchTime, std::memory_order_relaxed);
    }

    void EventDispatcher::DispatchImmediate(Event& event)
    {
        Dispatch(event);
    }

    EventCallbackToken EventDispatcher::AddCallback(EventType type, EventCallback callback, EventPriority priority)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);

        CallbackEntry entry;
        entry.Token = m_NextCallbackToken.fetch_add(1, std::memory_order_relaxed);
        entry.Callback = std::move(callback);
        entry.Priority = priority;

        m_Callbacks[type].push_back(std::move(entry));
        
        // Sort callbacks by priority (higher priority first).
        // Determinism contract: callbacks of the same priority execute in registration order.
        auto& callbacks = m_Callbacks[type];
        std::stable_sort(callbacks.begin(), callbacks.end(),
            [](const CallbackEntry& a, const CallbackEntry& b) {
                return static_cast<int>(a.Priority) < static_cast<int>(b.Priority);
            });

        return entry.Token;
    }

    bool EventDispatcher::RemoveCallback(EventType type, EventCallbackToken token)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        auto it = m_Callbacks.find(type);
        if (it != m_Callbacks.end())
        {
            auto& callbacks = it->second;
            const size_t oldSize = callbacks.size();
            callbacks.erase(
                std::remove_if(callbacks.begin(), callbacks.end(),
                    [token](const CallbackEntry& entry) {
                        return entry.Token == token;
                    }),
                callbacks.end()
            );
            return callbacks.size() != oldSize;
        }
        return false;
    }

    void EventDispatcher::AddListener(std::shared_ptr<EventListener> listener)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        ListenerEntry entry;
        entry.listener = std::move(listener);
        entry.priority = entry.listener->GetPriority();
        m_Listeners.push_back(std::move(entry));
        // Determinism contract: listeners of the same priority execute in registration order.
        std::stable_sort(m_Listeners.begin(), m_Listeners.end());
    }

    void EventDispatcher::AddListenerNonOwned(EventListener* listener)
    {
        if (!listener)
            return;
        AddListener(std::shared_ptr<EventListener>(listener, [](EventListener*) {}));
    }

    void EventDispatcher::RemoveListener(std::shared_ptr<EventListener> listener)
    {
        RemoveListener(listener.get());
    }

    void EventDispatcher::RemoveListener(const EventListener* listener)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Listeners.erase(
            std::remove_if(m_Listeners.begin(), m_Listeners.end(),
                [listener](const ListenerEntry& entry) {
                    return entry.listener.get() == listener;
                }),
            m_Listeners.end()
        );
    }

    void EventDispatcher::SetEventFilter(std::function<bool(const Event&)> filter)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_EventFilter = std::move(filter);
    }

    void EventDispatcher::ClearEventFilter()
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_EventFilter = nullptr;
    }

    EventDispatcher::DispatchStats EventDispatcher::GetStats() const
    {
        DispatchStats stats;
        stats.totalEventsDispatched = m_TotalEventsDispatched.load(std::memory_order_relaxed);
        stats.eventsHandled = m_EventsHandled.load(std::memory_order_relaxed);
        stats.eventsFiltered = m_EventsFiltered.load(std::memory_order_relaxed);
        stats.totalDispatchTime = std::chrono::microseconds{ m_TotalDispatchTimeMicroseconds.load(std::memory_order_relaxed) };
        stats.averageDispatchTime = stats.totalEventsDispatched > 0 
            ? static_cast<double>(stats.totalDispatchTime.count()) / static_cast<double>(stats.totalEventsDispatched) 
            : 0.0;
        return stats;
    }

    void EventDispatcher::ResetStats()
    {
        m_TotalEventsDispatched.store(0, std::memory_order_relaxed);
        m_EventsHandled.store(0, std::memory_order_relaxed);
        m_EventsFiltered.store(0, std::memory_order_relaxed);
        m_TotalDispatchTimeMicroseconds.store(0, std::memory_order_relaxed);
    }

    size_t EventDispatcher::GetListenerCount() const
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_Listeners.size();
    }

    size_t EventDispatcher::GetCallbackCount() const
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        size_t count = 0;
        for (const auto& [_, vec] : m_Callbacks)
        {
            count += vec.size();
        }
        return count;
    }

    // EventQueue implementation with lock-free queue
    EventQueue::EventQueue(size_t maxSize)
    {
        // Note: the underlying queue has a fixed capacity (kQueueCapacity). The max size is an
        // additional contract-level limit enforced via an atomic counter.
        if (maxSize == 0 || maxSize > kQueueCapacity)
        {
            m_MaxSize = kQueueCapacity;
        }
        else
        {
            m_MaxSize = maxSize;
        }
    }

    void EventQueue::Enqueue(std::unique_ptr<Event> event)
    {
        if (!event)
        {
            return;
        }

        // Enforce the configured max size (bounded queue contract).
        size_t size = m_ApproxSize.load(std::memory_order_relaxed);
        for (;;)
        {
            if (size >= m_MaxSize)
            {
                m_TotalDropped.fetch_add(1, std::memory_order_relaxed);
                LT_CORE_WARN("Event queue is full (maxSize={}): dropping event", m_MaxSize);
                return;
            }

            if (m_ApproxSize.compare_exchange_weak(
                    size, size + 1,
                    std::memory_order_acq_rel,
                    std::memory_order_relaxed))
            {
                break;
            }
        }

        QueuedEvent queuedEvent(std::move(event));
        
        if (!m_Queue.TryPush(std::move(queuedEvent)))
        {
            m_ApproxSize.fetch_sub(1, std::memory_order_relaxed);
            m_TotalDropped.fetch_add(1, std::memory_order_relaxed);
            LT_CORE_WARN("Event queue is full, dropping event");
            return;
        }
        
        m_TotalEnqueued.fetch_add(1, std::memory_order_relaxed);
    }

    std::unique_ptr<Event> EventQueue::Dequeue()
    {
        auto result = m_Queue.TryPop();
        if (!result)
            return nullptr;
        
        m_ApproxSize.fetch_sub(1, std::memory_order_relaxed);
        m_TotalDequeued.fetch_add(1, std::memory_order_relaxed);
        return std::move(result->event);
    }

    void EventQueue::Clear()
    {
        m_Queue.Clear();
        m_ApproxSize.store(0, std::memory_order_relaxed);
    }

    bool EventQueue::IsEmpty() const
    {
        return m_Queue.IsEmpty();
    }

    bool EventQueue::IsFull() const
    {
        return m_Queue.IsFull();
    }

    size_t EventQueue::GetSize() const
    {
        return m_ApproxSize.load(std::memory_order_relaxed);
    }

    void EventQueue::ProcessAll(EventDispatcher& dispatcher)
    {
        while (!m_Queue.IsEmpty())
        {
            auto result = m_Queue.TryPop();
            if (result && result->event)
            {
                m_ApproxSize.fetch_sub(1, std::memory_order_relaxed);
                m_TotalDequeued.fetch_add(1, std::memory_order_relaxed);
                dispatcher.Dispatch(*result->event);
            }
        }
    }

    void EventQueue::ProcessBatch(EventDispatcher& dispatcher, size_t maxEvents)
    {
        size_t processed = 0;
        while (!m_Queue.IsEmpty() && processed < maxEvents)
        {
            auto result = m_Queue.TryPop();
            if (result && result->event)
            {
                m_ApproxSize.fetch_sub(1, std::memory_order_relaxed);
                m_TotalDequeued.fetch_add(1, std::memory_order_relaxed);
                dispatcher.Dispatch(*result->event);
                processed++;
            }
        }
    }

    EventQueue::QueueStats EventQueue::GetStats() const
    {
        QueueStats stats;
        stats.currentSize = m_ApproxSize.load(std::memory_order_relaxed);
        stats.maxSize = m_MaxSize;
        stats.totalEnqueued = m_TotalEnqueued.load(std::memory_order_relaxed);
        stats.totalDequeued = m_TotalDequeued.load(std::memory_order_relaxed);
        stats.totalDropped = m_TotalDropped.load(std::memory_order_relaxed);
        
        // Calculate average queue time if we have processed events
        auto totalTime = m_TotalQueueTime.load(std::memory_order_relaxed);
        auto totalProcessed = stats.totalDequeued;
        stats.averageQueueTime = totalProcessed > 0 ? 
            std::chrono::duration<double, std::micro>(totalTime).count() / totalProcessed : 0.0;
        
        return stats;
    }

    // EventSystem implementation
    EventSystem& EventSystem::GetInstance()
    {
        static EventSystem instance;
        return instance;
    }

    void EventSystem::Initialize()
    {
        if (m_Initialized.load(std::memory_order_acquire))
        {
            LT_CORE_WARN("EventSystem already initialized, skipping...");
            return;
        }

        LT_CORE_INFO("Initializing EventSystem");
        m_ShuttingDown.store(false, std::memory_order_release);
        m_Dispatcher = std::make_shared<EventDispatcher>();
        m_Queue = std::make_shared<EventQueue>(1000);
        m_Initialized.store(true, std::memory_order_release);
        LT_CORE_INFO("EventSystem initialized successfully with dispatcher and queue ready");
    }

    bool EventSystem::BeginOperation() noexcept
    {
        // Gate new operations during shutdown, without holding a lock during user code execution.
        std::lock_guard<std::mutex> gateLock(m_ShutdownGateMutex);
        if (!m_Initialized.load(std::memory_order_acquire) ||
            m_ShuttingDown.load(std::memory_order_acquire))
        {
            return false;
        }

        m_InFlightOperations.fetch_add(1, std::memory_order_acq_rel);
        return true;
    }

    void EventSystem::EndOperation() noexcept
    {
        const uint32_t remaining = m_InFlightOperations.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (remaining == 0)
        {
            std::lock_guard<std::mutex> lock(m_ShutdownMutex);
            m_ShutdownCondition.notify_all();
        }
    }

    void EventSystem::Shutdown()
    {
        if (!m_Initialized.load(std::memory_order_acquire))
        {
            LT_CORE_DEBUG("EventSystem already shutdown, skipping...");
            return;
        }

        // Prevent new work from starting and wait until in-flight operations drain.
        {
            std::lock_guard<std::mutex> gateLock(m_ShutdownGateMutex);
            m_ShuttingDown.store(true, std::memory_order_release);
        }
        {
            std::unique_lock<std::mutex> lock(m_ShutdownMutex);
            m_ShutdownCondition.wait(lock, [&]() {
                return m_InFlightOperations.load(std::memory_order_acquire) == 0;
            });
        }

        LT_CORE_INFO("Shutting down EventSystem...");
        
        // Get stats before shutdown
        if (m_Dispatcher)
        {
            auto stats = m_Dispatcher->GetStats();
            LT_CORE_INFO("EventSystem shutdown stats - Events dispatched: {}, Events handled: {}, Events filtered: {}", 
                        stats.totalEventsDispatched, stats.eventsHandled, stats.eventsFiltered);
        }
        
        // Clear all callbacks and listeners
        LT_CORE_INFO("Clearing EventDispatcher and EventQueue...");
        m_Dispatcher.reset();
        m_Queue.reset();
        m_Initialized.store(false, std::memory_order_release);
        
        LT_CORE_INFO("EventSystem shutdown complete");
    }

    void EventSystem::Dispatch(Event& event)
    {
        EventSystemOperationGuard op(*this);
        if (!op.IsActive())
            return;

        auto dispatcher = m_Dispatcher;
        if (!dispatcher)
            return;

        LT_CORE_DEBUG("Dispatching event: {} (Type: {})", event.GetName(), static_cast<int>(event.GetType()));
        dispatcher->Dispatch(event);
    }

    void EventSystem::DispatchImmediate(Event& event)
    {
        Dispatch(event);
    }

    void EventSystem::DispatchDeferred(std::unique_ptr<Event> event)
    {
        EventSystemOperationGuard op(*this);
        if (!op.IsActive())
            return;

        if (!event)
            return;

        auto queue = m_Queue;
        if (!queue)
            return;

        LT_CORE_DEBUG("Enqueuing deferred event: {} (Type: {})", event->GetName(), static_cast<int>(event->GetType()));
        queue->Enqueue(std::move(event));
    }

    void EventSystem::ProcessEvents()
    {
        EventSystemOperationGuard op(*this);
        if (!op.IsActive())
            return;

        auto queue = m_Queue;
        auto dispatcher = m_Dispatcher;
        if (!queue || !dispatcher)
            return;

        queue->ProcessAll(*dispatcher);
    }

    void EventSystem::ProcessEvents(size_t maxEvents)
    {
        EventSystemOperationGuard op(*this);
        if (!op.IsActive())
            return;

        auto queue = m_Queue;
        auto dispatcher = m_Dispatcher;
        if (!queue || !dispatcher)
            return;

        queue->ProcessBatch(*dispatcher, maxEvents);
    }

    void EventSystem::AddListener(std::shared_ptr<EventListener> listener)
    {
        EventSystemOperationGuard op(*this);
        if (!op.IsActive())
            return;

        auto dispatcher = m_Dispatcher;
        if (!dispatcher)
            return;

        dispatcher->AddListener(std::move(listener));
    }

    void EventSystem::AddListenerNonOwned(EventListener* listener)
    {
        EventSystemOperationGuard op(*this);
        if (!op.IsActive())
            return;

        auto dispatcher = m_Dispatcher;
        if (!dispatcher)
            return;

        dispatcher->AddListenerNonOwned(listener);
    }

    void EventSystem::RemoveListener(std::shared_ptr<EventListener> listener)
    {
        EventSystemOperationGuard op(*this);
        if (!op.IsActive())
            return;

        auto dispatcher = m_Dispatcher;
        if (!dispatcher)
            return;

        dispatcher->RemoveListener(std::move(listener));
    }

    void EventSystem::RemoveListener(const EventListener* listener)
    {
        EventSystemOperationGuard op(*this);
        if (!op.IsActive())
            return;

        auto dispatcher = m_Dispatcher;
        if (!dispatcher)
            return;

        dispatcher->RemoveListener(listener);
    }

    EventCallbackToken EventSystem::AddCallback(EventType type, EventCallback callback, EventPriority priority)
    {
        EventSystemOperationGuard op(*this);
        if (!op.IsActive())
            return 0;

        auto dispatcher = m_Dispatcher;
        if (!dispatcher)
            return 0;

        return dispatcher->AddCallback(type, std::move(callback), priority);
    }

    bool EventSystem::RemoveCallback(EventType type, EventCallbackToken token)
    {
        EventSystemOperationGuard op(*this);
        if (!op.IsActive())
            return false;

        auto dispatcher = m_Dispatcher;
        if (!dispatcher)
            return false;

        return dispatcher->RemoveCallback(type, token);
    }

    void EventSystem::SetEventFilter(std::function<bool(const Event&)> filter)
    {
        EventSystemOperationGuard op(*this);
        if (!op.IsActive())
            return;

        auto dispatcher = m_Dispatcher;
        if (!dispatcher)
            return;

        dispatcher->SetEventFilter(std::move(filter));
    }

    void EventSystem::ClearEventFilter()
    {
        EventSystemOperationGuard op(*this);
        if (!op.IsActive())
            return;

        auto dispatcher = m_Dispatcher;
        if (!dispatcher)
            return;

        dispatcher->ClearEventFilter();
    }

    void EventSystem::SetMaxQueueSize(size_t maxSize)
    {
        if (m_Queue)
        {
            // Note: This would require recreating the queue with new size
            // For now, we'll just update the setting
        }
    }

    void EventSystem::EnableAsyncProcessing(bool enable)
    {
        m_AsyncProcessingEnabled = enable;
    }

    EventSystem::EventSystemStats EventSystem::GetStats() const
    {
        EventSystemStats stats;
        auto dispatcher = m_Dispatcher;
        auto queue = m_Queue;
        if (dispatcher)
        {
            stats.dispatchStats = dispatcher->GetStats();
        }
        if (queue)
        {
            stats.queueStats = queue->GetStats();
        }
        stats.totalListeners = dispatcher ? dispatcher->GetListenerCount() : 0;
        stats.totalCallbacks = dispatcher ? dispatcher->GetCallbackCount() : 0;
        return stats;
    }

    void EventSystem::ResetStats()
    {
        auto dispatcher = m_Dispatcher;
        if (dispatcher)
        {
            dispatcher->ResetStats();
        }
        // Queue stats would need to be reset separately
    }

    // Events namespace implementations
    namespace Events
    {
        // Window events
        WindowResizeEvent::WindowResizeEvent(uint32_t width, uint32_t height)
            : Event(EventType::WindowResize), m_Width(width), m_Height(height)
        {
        }

        std::unique_ptr<Event> WindowResizeEvent::Clone() const
        {
            return std::make_unique<WindowResizeEvent>(m_Width, m_Height);
        }

        WindowCloseEvent::WindowCloseEvent()
            : Event(EventType::WindowClose)
        {
        }

        std::unique_ptr<Event> WindowCloseEvent::Clone() const
        {
            return std::make_unique<WindowCloseEvent>();
        }

        // Input events
        KeyPressedEvent::KeyPressedEvent(int keyCode, bool isRepeat)
            : Event(EventType::KeyPressed), m_KeyCode(keyCode), m_IsRepeat(isRepeat)
        {
        }

        std::unique_ptr<Event> KeyPressedEvent::Clone() const
        {
            return std::make_unique<KeyPressedEvent>(m_KeyCode, m_IsRepeat);
        }

        KeyReleasedEvent::KeyReleasedEvent(int keyCode)
            : Event(EventType::KeyReleased), m_KeyCode(keyCode)
        {
        }

        std::unique_ptr<Event> KeyReleasedEvent::Clone() const
        {
            return std::make_unique<KeyReleasedEvent>(m_KeyCode);
        }

        MouseMovedEvent::MouseMovedEvent(float x, float y)
            : Event(EventType::MouseMoved), m_X(x), m_Y(y)
        {
        }

        std::unique_ptr<Event> MouseMovedEvent::Clone() const
        {
            return std::make_unique<MouseMovedEvent>(m_X, m_Y);
        }

        MouseButtonPressedEvent::MouseButtonPressedEvent(int button)
            : Event(EventType::MouseButtonPressed), m_Button(button)
        {
        }

        std::unique_ptr<Event> MouseButtonPressedEvent::Clone() const
        {
            return std::make_unique<MouseButtonPressedEvent>(m_Button);
        }

        MouseButtonReleasedEvent::MouseButtonReleasedEvent(int button)
            : Event(EventType::MouseButtonReleased), m_Button(button)
        {
        }

        std::unique_ptr<Event> MouseButtonReleasedEvent::Clone() const
        {
            return std::make_unique<MouseButtonReleasedEvent>(m_Button);
        }

        MouseScrolledEvent::MouseScrolledEvent(float xOffset, float yOffset)
            : Event(EventType::MouseScrolled), m_XOffset(xOffset), m_YOffset(yOffset)
        {
        }

        std::unique_ptr<Event> MouseScrolledEvent::Clone() const
        {
            return std::make_unique<MouseScrolledEvent>(m_XOffset, m_YOffset);
        }

        // Application events
        AppTickEvent::AppTickEvent(float deltaTime)
            : Event(EventType::AppTick), m_DeltaTime(deltaTime)
        {
        }

        std::unique_ptr<Event> AppTickEvent::Clone() const
        {
            return std::make_unique<AppTickEvent>(m_DeltaTime);
        }

        AppUpdateEvent::AppUpdateEvent(float deltaTime)
            : Event(EventType::AppUpdate), m_DeltaTime(deltaTime)
        {
        }

        std::unique_ptr<Event> AppUpdateEvent::Clone() const
        {
            return std::make_unique<AppUpdateEvent>(m_DeltaTime);
        }

        AppRenderEvent::AppRenderEvent()
            : Event(EventType::AppRender)
        {
        }

        std::unique_ptr<Event> AppRenderEvent::Clone() const
        {
            return std::make_unique<AppRenderEvent>();
        }

        // Hot reload events
        ConfigReloadedEvent::ConfigReloadedEvent(const std::string& configFile)
            : Event(EventType::ConfigReloaded), m_ConfigFile(configFile)
        {
        }

        std::unique_ptr<Event> ConfigReloadedEvent::Clone() const
        {
            return std::make_unique<ConfigReloadedEvent>(m_ConfigFile);
        }

        LoggingConfigChangedEvent::LoggingConfigChangedEvent(const std::string& changedKey, const ConfigValue& newValue)
            : Event(EventType::LoggingConfigChanged), m_ChangedKey(changedKey), m_NewValue(newValue)
        {
        }

        std::unique_ptr<Event> LoggingConfigChangedEvent::Clone() const
        {
            return std::make_unique<LoggingConfigChangedEvent>(m_ChangedKey, m_NewValue);
        }

        WindowConfigChangedEvent::WindowConfigChangedEvent(const std::string& changedKey, const ConfigValue& newValue)
            : Event(EventType::WindowConfigChanged), m_ChangedKey(changedKey), m_NewValue(newValue)
        {
        }

        std::unique_ptr<Event> WindowConfigChangedEvent::Clone() const
        {
            return std::make_unique<WindowConfigChangedEvent>(m_ChangedKey, m_NewValue);
        }
    }
}