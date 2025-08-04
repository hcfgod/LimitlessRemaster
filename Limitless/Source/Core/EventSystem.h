#pragma once

#include "Error.h"
#include "ConfigManager.h"
#include "Concurrency/LockFreeQueue.h"
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>
#include <queue>
#include <mutex>
#include <typeindex>
#include <chrono>
#include <string>

namespace Limitless
{
    // Forward declarations
    class Event;
    class EventDispatcher;
    class EventQueue;
    class EventListener;

    // Event types
    enum class EventType
    {
        // Window events
        WindowResize,
        WindowClose,
        WindowFocus,
        WindowLostFocus,
        WindowMove,
        
        // Input events
        KeyPressed,
        KeyReleased,
        KeyTyped,
        MouseButtonPressed,
        MouseButtonReleased,
        MouseMoved,
        MouseScrolled,
        GamepadConnected,
        GamepadDisconnected,
        GamepadButtonPressed,
        GamepadButtonReleased,
        GamepadAxisMoved,
        
        // Application events
        AppTick,
        AppUpdate,
        AppRender,
        AppShutdown,
        
        // Hot reload events
        ConfigReloaded,
        LoggingConfigChanged,
        WindowConfigChanged,
        
        // Resource events
        ResourceLoaded,
        ResourceUnloaded,
        ResourceError,
        ResourceReloaded,
        
        // Scene events
        SceneLoaded,
        SceneUnloaded,
        EntityCreated,
        EntityDestroyed,
        EntityMoved,
        EntityRotated,
        EntityScaled,
        
        // Audio events
        AudioPlay,
        AudioStop,
        AudioPause,
        AudioResume,
        AudioVolumeChanged,
        
        // Network events
        NetworkConnected,
        NetworkDisconnected,
        NetworkMessageReceived,
        NetworkError,
        
        // Custom events
        Custom
    };

    // Event priority levels
    enum class EventPriority
    {
        Critical = 0,    // Highest priority
        High = 1,
        Normal = 2,
        Low = 3,
        Background = 4   // Lowest priority
    };

    // Base event class
    class Event
    {
    public:
        Event(EventType type, EventPriority priority = EventPriority::Normal);
        virtual ~Event() = default;

        // Disable copy
        Event(const Event&) = delete;
        Event& operator=(const Event&) = delete;

        // Allow move
        Event(Event&&) = default;
        Event& operator=(Event&&) = default;

        // Event interface
        EventType GetType() const { return m_Type; }
        EventPriority GetPriority() const { return m_Priority; }
        std::chrono::system_clock::time_point GetTimestamp() const { return m_Timestamp; }
        
        // Event handling
        virtual bool IsHandled() const { return m_Handled; }
        virtual void SetHandled(bool handled) { m_Handled = handled; }
        
        // Event categorization
        virtual std::string GetCategory() const = 0;
        virtual std::string GetName() const = 0;
        
        // Event serialization (for debugging/logging)
        virtual std::string ToString() const;

        // Event cloning (for queuing)
        virtual std::unique_ptr<Event> Clone() const = 0;

    protected:
        EventType m_Type;
        EventPriority m_Priority;
        std::chrono::system_clock::time_point m_Timestamp;
        bool m_Handled = false;
    };

    // Event listener interface
    class EventListener
    {
    public:
        virtual ~EventListener() = default;
        virtual void OnEvent(Event& event) = 0;
        virtual bool ShouldReceiveEvent(const Event& event) const = 0;
        virtual EventPriority GetPriority() const { return EventPriority::Normal; }
    };

    // Event callback type
    using EventCallback = std::function<void(Event&)>;

    // Event dispatcher
    class EventDispatcher
    {
    public:
        EventDispatcher() = default;
        ~EventDispatcher() = default;

        // Event dispatching
        void Dispatch(Event& event);
        void DispatchImmediate(Event& event);
        
        // Listener management
        void AddListener(std::shared_ptr<EventListener> listener);
        void RemoveListener(std::shared_ptr<EventListener> listener);
        void RemoveListener(const EventListener* listener);
        
        // Callback management
        void AddCallback(EventType type, EventCallback callback, EventPriority priority = EventPriority::Normal);
        void RemoveCallback(EventType type, const EventCallback& callback);
        
        // Event filtering
        void SetEventFilter(std::function<bool(const Event&)> filter);
        void ClearEventFilter();

        // Statistics
        struct DispatchStats
        {
            size_t totalEventsDispatched;
            size_t eventsHandled;
            size_t eventsFiltered;
            std::chrono::microseconds totalDispatchTime;
            double averageDispatchTime;
        };
        
        DispatchStats GetStats() const;
        void ResetStats();

    private:
        struct ListenerEntry
        {
            std::shared_ptr<EventListener> listener;
            EventPriority priority;
            
            bool operator<(const ListenerEntry& other) const
            {
                return static_cast<int>(priority) > static_cast<int>(other.priority);
            }
        };

        std::vector<ListenerEntry> m_Listeners;
        std::unordered_map<EventType, std::vector<std::pair<EventCallback, EventPriority>>> m_Callbacks;
        std::function<bool(const Event&)> m_EventFilter;
        
        mutable std::mutex m_Mutex;
        
        // Statistics
        mutable size_t m_TotalEventsDispatched = 0;
        mutable size_t m_EventsHandled = 0;
        mutable size_t m_EventsFiltered = 0;
        mutable std::chrono::microseconds m_TotalDispatchTime{0};
    };

    // Event queue for deferred processing
    class EventQueue
    {
    public:
        explicit EventQueue(size_t maxSize = 10000);
        ~EventQueue() = default;

        // Queue management
        void Enqueue(std::unique_ptr<Event> event);
        std::unique_ptr<Event> Dequeue();
        void Clear();
        
        // Queue status
        bool IsEmpty() const;
        bool IsFull() const;
        size_t GetSize() const;
        size_t GetMaxSize() const { return m_MaxSize; }
        
        // Queue processing
        void ProcessAll(EventDispatcher& dispatcher);
        void ProcessBatch(EventDispatcher& dispatcher, size_t maxEvents = 100);
        
        // Queue statistics
        struct QueueStats
        {
            size_t currentSize;
            size_t maxSize;
            size_t totalEnqueued;
            size_t totalDequeued;
            size_t totalDropped;
            double averageQueueTime;
        };
        
        QueueStats GetStats() const;

    private:
        struct QueuedEvent
        {
            std::unique_ptr<Event> event;
            std::chrono::system_clock::time_point enqueueTime;
            
            QueuedEvent() : event(nullptr), enqueueTime(std::chrono::system_clock::now()) {}
            
            QueuedEvent(std::unique_ptr<Event> e) 
                : event(std::move(e)), enqueueTime(std::chrono::system_clock::now()) {}
        };

        Concurrency::LockFreeMPMCQueue<QueuedEvent, 16384> m_Queue; // Power of 2 for efficiency
        size_t m_MaxSize;
        
        // Statistics (atomic for thread safety)
        std::atomic<size_t> m_TotalEnqueued{0};
        std::atomic<size_t> m_TotalDequeued{0};
        std::atomic<size_t> m_TotalDropped{0};
        std::atomic<std::chrono::microseconds> m_TotalQueueTime{std::chrono::microseconds{0}};
    };

    // Main event system
    class EventSystem
    {
    public:
        static EventSystem& GetInstance();
        
        // Disable copy and assignment
        EventSystem(const EventSystem&) = delete;
        EventSystem& operator=(const EventSystem&) = delete;

        // Initialize/shutdown
        void Initialize();
        void Shutdown();
        bool IsInitialized() const { return m_Initialized; }

        // Event dispatching
        void Dispatch(Event& event);
        void DispatchImmediate(Event& event);
        void DispatchDeferred(std::unique_ptr<Event> event);
        
        // Event processing
        void ProcessEvents();
        void ProcessEvents(size_t maxEvents);
        
        // Listener management
        void AddListener(std::shared_ptr<EventListener> listener);
        void RemoveListener(std::shared_ptr<EventListener> listener);
        void RemoveListener(const EventListener* listener);
        
        // Callback management
        void AddCallback(EventType type, EventCallback callback, EventPriority priority = EventPriority::Normal);
        void RemoveCallback(EventType type, const EventCallback& callback);
        
        // Event filtering
        void SetEventFilter(std::function<bool(const Event&)> filter);
        void ClearEventFilter();

        // Configuration
        void SetMaxQueueSize(size_t maxSize);
        void EnableAsyncProcessing(bool enable);
        bool IsAsyncProcessingEnabled() const { return m_AsyncProcessingEnabled; }
        
        // Statistics
        struct EventSystemStats
        {
            EventDispatcher::DispatchStats dispatchStats;
            EventQueue::QueueStats queueStats;
            size_t totalListeners;
            size_t totalCallbacks;
        };
        
        EventSystemStats GetStats() const;
        void ResetStats();

    private:
        EventSystem() = default;
        ~EventSystem() = default;

        std::unique_ptr<EventDispatcher> m_Dispatcher;
        std::unique_ptr<EventQueue> m_Queue;
        bool m_Initialized = false;
        bool m_AsyncProcessingEnabled = false;
        mutable std::mutex m_Mutex;
    };

    // Convenience functions
    inline EventSystem& GetEventSystem() { return EventSystem::GetInstance(); }

    // Event dispatching macros
    #define LT_DISPATCH_EVENT(event) \
        Limitless::GetEventSystem().Dispatch(event)
    
    #define LT_DISPATCH_IMMEDIATE(event) \
        Limitless::GetEventSystem().DispatchImmediate(event)
    
    #define LT_DISPATCH_DEFERRED(event) \
        Limitless::GetEventSystem().DispatchDeferred(std::move(event))

    // Common event types
    namespace Events
    {
        // Window events
        class WindowResizeEvent : public Event
        {
        public:
            WindowResizeEvent(uint32_t width, uint32_t height);
            
            uint32_t GetWidth() const { return m_Width; }
            uint32_t GetHeight() const { return m_Height; }
            
            std::string GetCategory() const override { return "Window"; }
            std::string GetName() const override { return "WindowResize"; }
            std::unique_ptr<Event> Clone() const override;

        private:
            uint32_t m_Width;
            uint32_t m_Height;
        };

        class WindowCloseEvent : public Event
        {
        public:
            WindowCloseEvent();
            
            std::string GetCategory() const override { return "Window"; }
            std::string GetName() const override { return "WindowClose"; }
            std::unique_ptr<Event> Clone() const override;
        };

        // Input events
        class KeyPressedEvent : public Event
        {
        public:
            KeyPressedEvent(int keyCode, bool isRepeat = false);
            
            int GetKeyCode() const { return m_KeyCode; }
            bool IsRepeat() const { return m_IsRepeat; }
            
            std::string GetCategory() const override { return "Input"; }
            std::string GetName() const override { return "KeyPressed"; }
            std::unique_ptr<Event> Clone() const override;

        private:
            int m_KeyCode;
            bool m_IsRepeat;
        };

        class KeyReleasedEvent : public Event
        {
        public:
            KeyReleasedEvent(int keyCode);
            
            int GetKeyCode() const { return m_KeyCode; }
            
            std::string GetCategory() const override { return "Input"; }
            std::string GetName() const override { return "KeyReleased"; }
            std::unique_ptr<Event> Clone() const override;

        private:
            int m_KeyCode;
        };

        class MouseMovedEvent : public Event
        {
        public:
            MouseMovedEvent(float x, float y);
            
            float GetX() const { return m_X; }
            float GetY() const { return m_Y; }
            
            std::string GetCategory() const override { return "Input"; }
            std::string GetName() const override { return "MouseMoved"; }
            std::unique_ptr<Event> Clone() const override;

        private:
            float m_X;
            float m_Y;
        };

        class MouseButtonPressedEvent : public Event
        {
        public:
            MouseButtonPressedEvent(int button);
            
            int GetButton() const { return m_Button; }
            
            std::string GetCategory() const override { return "Input"; }
            std::string GetName() const override { return "MouseButtonPressed"; }
            std::unique_ptr<Event> Clone() const override;

        private:
            int m_Button;
        };

        class MouseButtonReleasedEvent : public Event
        {
        public:
            MouseButtonReleasedEvent(int button);
            
            int GetButton() const { return m_Button; }
            
            std::string GetCategory() const override { return "Input"; }
            std::string GetName() const override { return "MouseButtonReleased"; }
            std::unique_ptr<Event> Clone() const override;

        private:
            int m_Button;
        };

        class MouseScrolledEvent : public Event
        {
        public:
            MouseScrolledEvent(float xOffset, float yOffset);
            
            float GetXOffset() const { return m_XOffset; }
            float GetYOffset() const { return m_YOffset; }
            
            std::string GetCategory() const override { return "Input"; }
            std::string GetName() const override { return "MouseScrolled"; }
            std::unique_ptr<Event> Clone() const override;

        private:
            float m_XOffset;
            float m_YOffset;
        };

        // Application events
        class AppTickEvent : public Event
        {
        public:
            AppTickEvent(float deltaTime);
            
            float GetDeltaTime() const { return m_DeltaTime; }
            
            std::string GetCategory() const override { return "Application"; }
            std::string GetName() const override { return "AppTick"; }
            std::unique_ptr<Event> Clone() const override;

        private:
            float m_DeltaTime;
        };

        class AppUpdateEvent : public Event
        {
        public:
            AppUpdateEvent(float deltaTime);
            
            float GetDeltaTime() const { return m_DeltaTime; }
            
            std::string GetCategory() const override { return "Application"; }
            std::string GetName() const override { return "AppUpdate"; }
            std::unique_ptr<Event> Clone() const override;

        private:
            float m_DeltaTime;
        };

        class AppRenderEvent : public Event
        {
        public:
            AppRenderEvent();
            
            std::string GetCategory() const override { return "Application"; }
            std::string GetName() const override { return "AppRender"; }
            std::unique_ptr<Event> Clone() const override;
        };

        // Hot reload events
        class ConfigReloadedEvent : public Event
        {
        public:
            ConfigReloadedEvent(const std::string& configFile);
            
            const std::string& GetConfigFile() const { return m_ConfigFile; }
            
            std::string GetCategory() const override { return "HotReload"; }
            std::string GetName() const override { return "ConfigReloaded"; }
            std::unique_ptr<Event> Clone() const override;

        private:
            std::string m_ConfigFile;
        };

        class LoggingConfigChangedEvent : public Event
        {
        public:
            LoggingConfigChangedEvent(const std::string& changedKey, const ConfigValue& newValue);
            
            const std::string& GetChangedKey() const { return m_ChangedKey; }
            const ConfigValue& GetNewValue() const { return m_NewValue; }
            
            std::string GetCategory() const override { return "HotReload"; }
            std::string GetName() const override { return "LoggingConfigChanged"; }
            std::unique_ptr<Event> Clone() const override;

        private:
            std::string m_ChangedKey;
            ConfigValue m_NewValue;
        };

        class WindowConfigChangedEvent : public Event
        {
        public:
            WindowConfigChangedEvent(const std::string& changedKey, const ConfigValue& newValue);
            
            const std::string& GetChangedKey() const { return m_ChangedKey; }
            const ConfigValue& GetNewValue() const { return m_NewValue; }
            
            std::string GetCategory() const override { return "HotReload"; }
            std::string GetName() const override { return "WindowConfigChanged"; }
            std::unique_ptr<Event> Clone() const override;

        private:
            std::string m_ChangedKey;
            ConfigValue m_NewValue;
        };
    }

    // Event listener base class
    class EventListenerBase : public EventListener
    {
    public:
        EventListenerBase() = default;
        virtual ~EventListenerBase() = default;

        // Default implementation
        bool ShouldReceiveEvent(const Event& event) const override { return true; }
        EventPriority GetPriority() const override { return EventPriority::Normal; }
    };

    // Event callback wrapper
    template<typename EventType>
    class EventCallbackWrapper
    {
    public:
        using CallbackType = std::function<void(EventType&)>;
        
        explicit EventCallbackWrapper(CallbackType callback)
            : m_Callback(std::move(callback)) {}
        
        void operator()(Event& event)
        {
            if (auto* typedEvent = dynamic_cast<EventType*>(&event))
            {
                m_Callback(*typedEvent);
            }
        }
        
    private:
        CallbackType m_Callback;
    };

    // Event callback registration helper
    template<typename EventType>
    void AddEventCallback(std::function<void(EventType&)> callback, EventPriority priority = EventPriority::Normal)
    {
        EventCallbackWrapper<EventType> wrapper(std::move(callback));
        GetEventSystem().AddCallback(EventType::GetStaticType(), wrapper, priority);
    }
}