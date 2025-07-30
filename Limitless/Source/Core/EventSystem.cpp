#include "EventSystem.h"
#include "Debug/Log.h"
#include <algorithm>
#include <chrono>
#include <sstream>

namespace Limitless
{
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
        // Check if event filter exists and if it should filter this event
        if (m_EventFilter && !m_EventFilter(event))
        {
            m_EventsFiltered++;
            LT_CORE_DEBUG("Event filtered out: {}", event.ToString());
            return; // Event is filtered out, don't dispatch
        }

        m_TotalEventsDispatched++;
        auto startTime = std::chrono::high_resolution_clock::now();
        
        auto eventType = event.GetType();
        auto it = m_Callbacks.find(eventType);
        
        if (it != m_Callbacks.end())
        {
            for (auto& callback : it->second)
            {
                try
                {
                    callback.first(event);
                    m_EventsHandled++;
                }
                catch (const std::exception& e)
                {
                    LT_CORE_ERROR("Exception in event callback: {}", e.what());
                }
            }
        }
        
        // Also dispatch to listeners
        for (auto& listener : m_Listeners)
        {
            try
            {
                listener.listener->OnEvent(event); 
                m_EventsHandled++;
            }
            catch (const std::exception& e)
            {
                LT_CORE_ERROR("Exception in event listener: {}", e.what());
            }
        }
        
        auto endTime = std::chrono::high_resolution_clock::now();
        m_TotalDispatchTime += std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
    }

    void EventDispatcher::DispatchImmediate(Event& event)
    {
        Dispatch(event);
    }

    void EventDispatcher::AddCallback(EventType type, EventCallback callback, EventPriority priority)
    {
        m_Callbacks[type].push_back({std::move(callback), priority});
    }

    void EventDispatcher::RemoveCallback(EventType type, const EventCallback& callback)
    {
        auto it = m_Callbacks.find(type);
        if (it != m_Callbacks.end())
        {
            auto& callbacks = it->second;
            callbacks.erase(
                std::remove_if(callbacks.begin(), callbacks.end(),
                    [&callback](const std::pair<EventCallback, EventPriority>& entry) {
                        return entry.first.target_type() == callback.target_type();
                    }),
                callbacks.end()
            );
        }
    }

    void EventDispatcher::AddListener(std::shared_ptr<EventListener> listener)
    {
        ListenerEntry entry;
        entry.listener = std::move(listener);
        entry.priority = entry.listener->GetPriority();
        m_Listeners.push_back(std::move(entry));
        std::sort(m_Listeners.begin(), m_Listeners.end());
    }

    void EventDispatcher::RemoveListener(std::shared_ptr<EventListener> listener)
    {
        RemoveListener(listener.get());
    }

    void EventDispatcher::RemoveListener(const EventListener* listener)
    {
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
        m_EventFilter = std::move(filter);
    }

    void EventDispatcher::ClearEventFilter()
    {
        m_EventFilter = nullptr;
    }

    EventDispatcher::DispatchStats EventDispatcher::GetStats() const
    {
        DispatchStats stats;
        stats.totalEventsDispatched = m_TotalEventsDispatched;
        stats.eventsHandled = m_EventsHandled;
        stats.eventsFiltered = m_EventsFiltered;
        stats.totalDispatchTime = m_TotalDispatchTime;
        stats.averageDispatchTime = m_TotalEventsDispatched > 0 
            ? static_cast<double>(m_TotalDispatchTime.count()) / m_TotalEventsDispatched 
            : 0.0;
        return stats;
    }

    void EventDispatcher::ResetStats()
    {
        m_TotalEventsDispatched = 0;
        m_EventsHandled = 0;
        m_EventsFiltered = 0;
        m_TotalDispatchTime = std::chrono::microseconds{0};
    }

    // EventQueue implementation with lock-free queue
    EventQueue::EventQueue(size_t maxSize)
        : m_MaxSize(maxSize)
    {
    }

    void EventQueue::Enqueue(std::unique_ptr<Event> event)
    {
        QueuedEvent queuedEvent(std::move(event));
        
        if (!m_Queue.TryPush(std::move(queuedEvent)))
        {
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
        
        m_TotalDequeued.fetch_add(1, std::memory_order_relaxed);
        return std::move(result->event);
    }

    void EventQueue::Clear()
    {
        m_Queue.Clear();
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
        return m_Queue.GetSize();
    }

    void EventQueue::ProcessAll(EventDispatcher& dispatcher)
    {
        while (!m_Queue.IsEmpty())
        {
            auto result = m_Queue.TryPop();
            if (result && result->event)
            {
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
                m_TotalDequeued.fetch_add(1, std::memory_order_relaxed);
                dispatcher.Dispatch(*result->event);
                processed++;
            }
        }
    }

    EventQueue::QueueStats EventQueue::GetStats() const
    {
        QueueStats stats;
        stats.currentSize = m_Queue.GetSize();
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
        if (m_Initialized)
        {
            LT_CORE_WARN("EventSystem already initialized");
            return;
        }

        LT_CORE_INFO("Initializing EventSystem");
        m_Dispatcher = std::make_unique<EventDispatcher>();
        m_Queue = std::make_unique<EventQueue>(1000);
        m_Initialized = true;
        LT_CORE_INFO("EventSystem initialized successfully");
    }

    void EventSystem::Shutdown()
    {
        if (!m_Initialized) return;

        LT_CORE_INFO("Shutting down EventSystem");
        
        // Clear all callbacks and listeners
        m_Dispatcher.reset();
        m_Queue.reset();
        m_Initialized = false;
        
        LT_CORE_INFO("EventSystem shutdown complete");
    }

    void EventSystem::Dispatch(Event& event)
    {
        if (!m_Initialized)
        {
            LT_CORE_WARN("Attempting to dispatch event when EventSystem is not initialized");
            return;
        }

        LT_CORE_DEBUG("Dispatching event: {} (Type: {})", event.GetName(), static_cast<int>(event.GetType()));
        m_Dispatcher->Dispatch(event);
    }

    void EventSystem::DispatchImmediate(Event& event)
    {
        Dispatch(event);
    }

    void EventSystem::DispatchDeferred(std::unique_ptr<Event> event)
    {
        if (!m_Initialized)
        {
            LT_CORE_WARN("Attempting to dispatch deferred event when EventSystem is not initialized");
            return;
        }

        LT_CORE_DEBUG("Enqueuing deferred event: {} (Type: {})", event->GetName(), static_cast<int>(event->GetType()));
        m_Queue->Enqueue(std::move(event));
    }

    void EventSystem::ProcessEvents()
    {
        if (!m_Initialized) return;

        m_Queue->ProcessAll(*m_Dispatcher);
    }

    void EventSystem::ProcessEvents(size_t maxEvents)
    {
        if (!m_Initialized) return;

        m_Queue->ProcessBatch(*m_Dispatcher, maxEvents);
    }

    void EventSystem::AddListener(std::shared_ptr<EventListener> listener)
    {
        if (!m_Initialized)
        {
            return;
        }

        m_Dispatcher->AddListener(std::move(listener));
    }

    void EventSystem::RemoveListener(std::shared_ptr<EventListener> listener)
    {
        if (!m_Initialized) return;
        m_Dispatcher->RemoveListener(std::move(listener));
    }

    void EventSystem::RemoveListener(const EventListener* listener)
    {
        if (!m_Initialized) return;
        m_Dispatcher->RemoveListener(listener);
    }

    void EventSystem::AddCallback(EventType type, EventCallback callback, EventPriority priority)
    {
        if (!m_Initialized)
        {
            return;
        }

        m_Dispatcher->AddCallback(type, std::move(callback), priority);
    }

    void EventSystem::RemoveCallback(EventType type, const EventCallback& callback)
    {
        if (!m_Initialized) return;
        m_Dispatcher->RemoveCallback(type, callback);
    }

    void EventSystem::SetEventFilter(std::function<bool(const Event&)> filter)
    {
        if (!m_Initialized) return;
        m_Dispatcher->SetEventFilter(std::move(filter));
    }

    void EventSystem::ClearEventFilter()
    {
        if (!m_Initialized) return;
        m_Dispatcher->ClearEventFilter();
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
        if (m_Dispatcher)
        {
            stats.dispatchStats = m_Dispatcher->GetStats();
        }
        if (m_Queue)
        {
            stats.queueStats = m_Queue->GetStats();
        }
        stats.totalListeners = m_Dispatcher ? m_Dispatcher->GetStats().totalEventsDispatched : 0;
        stats.totalCallbacks = 0; // Would need to track this separately
        return stats;
    }

    void EventSystem::ResetStats()
    {
        if (m_Dispatcher)
        {
            m_Dispatcher->ResetStats();
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