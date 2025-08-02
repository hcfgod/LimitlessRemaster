#define DOCTEST_CONFIG_DISABLE_EXCEPTIONS
#define DOCTEST_CONFIG_WITH_VARIADIC_MACROS
#include <doctest/doctest.h>
#include "Core/EventSystem.h"
#include <thread>
#include <chrono>
#include <vector>
#include <string>

TEST_SUITE("Event System")
{
    TEST_CASE("Basic Event Operations")
    {
        auto& eventSystem = Limitless::GetEventSystem();
        
        // Test initialization
        CHECK(eventSystem.IsInitialized() == false);
        eventSystem.Initialize();
        CHECK(eventSystem.IsInitialized() == true);
        
        // Test event callback registration
        bool callbackCalled = false;
        eventSystem.AddCallback(Limitless::EventType::AppTick, [&callbackCalled](Limitless::Event& event) {
            callbackCalled = true;
        });
        
        // Test event dispatching
        auto tickEvent = std::make_unique<Limitless::Events::AppTickEvent>(0.016f);
        eventSystem.Dispatch(*tickEvent);
        
        // Process events
        eventSystem.ProcessEvents();
        
        // Verify callback was called
        CHECK(callbackCalled == true);
        
        // Cleanup
        eventSystem.Shutdown();
    }
    
    TEST_CASE("Event Priority Handling")
    {
        auto& eventSystem = Limitless::GetEventSystem();
        eventSystem.Initialize();
        
        std::vector<int> executionOrder;
        
        // Add callbacks with different priorities
        eventSystem.AddCallback(Limitless::EventType::AppUpdate, [&executionOrder](Limitless::Event& event) {
            executionOrder.push_back(3); // Low priority
        }, Limitless::EventPriority::Low);
        
        eventSystem.AddCallback(Limitless::EventType::AppUpdate, [&executionOrder](Limitless::Event& event) {
            executionOrder.push_back(1); // High priority
        }, Limitless::EventPriority::High);
        
        eventSystem.AddCallback(Limitless::EventType::AppUpdate, [&executionOrder](Limitless::Event& event) {
            executionOrder.push_back(2); // Normal priority
        }, Limitless::EventPriority::Normal);
        
        // Dispatch event
        auto updateEvent = std::make_unique<Limitless::Events::AppUpdateEvent>(0.016f);
        eventSystem.Dispatch(*updateEvent);
        eventSystem.ProcessEvents();
        
        // Verify execution order (High -> Normal -> Low)
        CHECK(executionOrder.size() == 3);
        CHECK(executionOrder[0] == 1); // High priority first
        CHECK(executionOrder[1] == 2); // Normal priority second
        CHECK(executionOrder[2] == 3); // Low priority last
        
        eventSystem.Shutdown();
    }
    
    TEST_CASE("Event Filtering")
    {
        auto& eventSystem = Limitless::GetEventSystem();
        eventSystem.Initialize();
        
        // Set event filter to only allow AppTick events
        eventSystem.SetEventFilter([](const Limitless::Event& event) {
            return event.GetType() == Limitless::EventType::AppTick;
        });
        
        bool tickCallbackCalled = false;
        bool updateCallbackCalled = false;
        
        // Add callbacks for different event types
        eventSystem.AddCallback(Limitless::EventType::AppTick, [&tickCallbackCalled](Limitless::Event& event) {
            tickCallbackCalled = true;
        });
        
        eventSystem.AddCallback(Limitless::EventType::AppUpdate, [&updateCallbackCalled](Limitless::Event& event) {
            updateCallbackCalled = true;
        });
        
        // Dispatch both event types
        auto tickEvent = std::make_unique<Limitless::Events::AppTickEvent>(0.016f);
        auto updateEvent = std::make_unique<Limitless::Events::AppUpdateEvent>(0.016f);
        
        eventSystem.Dispatch(*tickEvent);
        eventSystem.Dispatch(*updateEvent);
        eventSystem.ProcessEvents();
        
        // Verify only AppTick callback was called
        CHECK(tickCallbackCalled == true);
        CHECK(updateCallbackCalled == false);
        
        // Clear filter and test again
        eventSystem.SetEventFilter(nullptr);
        
        tickCallbackCalled = false;
        updateCallbackCalled = false;
        
        eventSystem.Dispatch(*tickEvent);
        eventSystem.Dispatch(*updateEvent);
        eventSystem.ProcessEvents();
        
        // Verify both callbacks were called
        CHECK(tickCallbackCalled == true);
        CHECK(updateCallbackCalled == true);
        
        eventSystem.Shutdown();
    }
    
    TEST_CASE("Multiple Event Types")
    {
        auto& eventSystem = Limitless::GetEventSystem();
        eventSystem.Initialize();
        
        std::vector<Limitless::EventType> receivedEvents;
        
        // Add callback for all event types
        eventSystem.AddCallback(Limitless::EventType::AppTick, [&receivedEvents](Limitless::Event& event) {
            receivedEvents.push_back(event.GetType());
        });
        
        eventSystem.AddCallback(Limitless::EventType::AppUpdate, [&receivedEvents](Limitless::Event& event) {
            receivedEvents.push_back(event.GetType());
        });
        
        eventSystem.AddCallback(Limitless::EventType::KeyPressed, [&receivedEvents](Limitless::Event& event) {
            receivedEvents.push_back(event.GetType());
        });
        
        eventSystem.AddCallback(Limitless::EventType::MouseMoved, [&receivedEvents](Limitless::Event& event) {
            receivedEvents.push_back(event.GetType());
        });
        
        // Dispatch different event types
        auto tickEvent = std::make_unique<Limitless::Events::AppTickEvent>(0.016f);
        auto updateEvent = std::make_unique<Limitless::Events::AppUpdateEvent>(0.016f);
        auto keyEvent = std::make_unique<Limitless::Events::KeyPressedEvent>(65, 0); // 'A' key
        auto mouseEvent = std::make_unique<Limitless::Events::MouseMovedEvent>(100, 200);
        
        eventSystem.Dispatch(*tickEvent);
        eventSystem.Dispatch(*updateEvent);
        eventSystem.Dispatch(*keyEvent);
        eventSystem.Dispatch(*mouseEvent);
        
        eventSystem.ProcessEvents();
        
        // Verify all events were received
        CHECK(receivedEvents.size() == 4);
        CHECK(receivedEvents[0] == Limitless::EventType::AppTick);
        CHECK(receivedEvents[1] == Limitless::EventType::AppUpdate);
        CHECK(receivedEvents[2] == Limitless::EventType::KeyPressed);
        CHECK(receivedEvents[3] == Limitless::EventType::MouseMoved);
        
        eventSystem.Shutdown();
    }
    
    TEST_CASE("Event Data Access")
    {
        auto& eventSystem = Limitless::GetEventSystem();
        eventSystem.Initialize();
        
        // Test key event data
        int receivedKeyCode = 0;
        int receivedModifiers = 0;
        
        eventSystem.AddCallback(Limitless::EventType::KeyPressed, [&receivedKeyCode, &receivedModifiers](Limitless::Event& event) {
            auto& keyEvent = static_cast<Limitless::Events::KeyPressedEvent&>(event);
            receivedKeyCode = keyEvent.GetKeyCode();
            receivedModifiers = keyEvent.IsRepeat() ? 1 : 0; // Use IsRepeat instead of GetModifiers
        });
        
        auto keyEvent = std::make_unique<Limitless::Events::KeyPressedEvent>(65, true); // 'A' key with repeat
        eventSystem.Dispatch(*keyEvent);
        eventSystem.ProcessEvents();
        
        CHECK(receivedKeyCode == 65);
        CHECK(receivedModifiers == 1);
        
        // Test mouse event data
        float receivedX = 0.0f;
        float receivedY = 0.0f;
        
        eventSystem.AddCallback(Limitless::EventType::MouseMoved, [&receivedX, &receivedY](Limitless::Event& event) {
            auto& mouseEvent = static_cast<Limitless::Events::MouseMovedEvent&>(event);
            receivedX = mouseEvent.GetX();
            receivedY = mouseEvent.GetY();
        });
        
        auto mouseEvent = std::make_unique<Limitless::Events::MouseMovedEvent>(150.5f, 250.75f);
        eventSystem.Dispatch(*mouseEvent);
        eventSystem.ProcessEvents();
        
        CHECK(receivedX == 150.5f);
        CHECK(receivedY == 250.75f);
        
        // Test app event data
        float receivedDeltaTime = 0.0f;
        
        eventSystem.AddCallback(Limitless::EventType::AppTick, [&receivedDeltaTime](Limitless::Event& event) {
            auto& appEvent = static_cast<Limitless::Events::AppTickEvent&>(event);
            receivedDeltaTime = appEvent.GetDeltaTime();
        });
        
        auto appEvent = std::make_unique<Limitless::Events::AppTickEvent>(0.033f);
        eventSystem.Dispatch(*appEvent);
        eventSystem.ProcessEvents();
        
        CHECK(receivedDeltaTime == 0.033f);
        
        eventSystem.Shutdown();
    }
    
    TEST_CASE("Event Callback Removal")
    {
        auto& eventSystem = Limitless::GetEventSystem();
        eventSystem.Initialize();
        
        int callbackCount = 0;
        
        // Add multiple callbacks
        Limitless::EventCallback callback1 = [&callbackCount](Limitless::Event& event) {
            callbackCount++;
        };
        eventSystem.AddCallback(Limitless::EventType::AppTick, callback1);
        
        Limitless::EventCallback callback2 = [&callbackCount](Limitless::Event& event) {
            callbackCount++;
        };
        eventSystem.AddCallback(Limitless::EventType::AppTick, callback2);
        
        // Dispatch event - both callbacks should be called
        auto tickEvent = std::make_unique<Limitless::Events::AppTickEvent>(0.016f);
        eventSystem.Dispatch(*tickEvent);
        eventSystem.ProcessEvents();
        
        CHECK(callbackCount == 2);
        
        // Remove first callback
        eventSystem.RemoveCallback(Limitless::EventType::AppTick, callback1);
        
        callbackCount = 0;
        
        // Dispatch event again - only second callback should be called
        eventSystem.Dispatch(*tickEvent);
        eventSystem.ProcessEvents();
        
        CHECK(callbackCount == 1);
        
        // Remove second callback
        eventSystem.RemoveCallback(Limitless::EventType::AppTick, callback2);
        
        callbackCount = 0;
        
        // Dispatch event again - no callbacks should be called
        eventSystem.Dispatch(*tickEvent);
        eventSystem.ProcessEvents();
        
        CHECK(callbackCount == 0);
        
        eventSystem.Shutdown();
    }
     
    TEST_CASE("Event System Performance")
    {
        auto& eventSystem = Limitless::GetEventSystem();
        eventSystem.Initialize();
        
        const int numEvents = 10000;
        
        // Add a simple callback
        eventSystem.AddCallback(Limitless::EventType::AppTick, [](Limitless::Event& event) {
            // Simple callback that does nothing
        });
        
        // Measure dispatch performance
        auto dispatchStart = std::chrono::high_resolution_clock::now();
        
        for (int i = 0; i < numEvents; ++i)
        {
            auto tickEvent = std::make_unique<Limitless::Events::AppTickEvent>(0.016f);
            eventSystem.Dispatch(*tickEvent);
        }
        
        auto dispatchEnd = std::chrono::high_resolution_clock::now();
        auto dispatchDuration = std::chrono::duration_cast<std::chrono::microseconds>(dispatchEnd - dispatchStart);
        
        // Measure processing performance
        auto processStart = std::chrono::high_resolution_clock::now();
        
        int processedEvents = 0;
        while (processedEvents < numEvents)
        {
            eventSystem.ProcessEvents();
            processedEvents += 1; // ProcessEvents() returns void, so we count iterations
        }
        
        auto processEnd = std::chrono::high_resolution_clock::now();
        auto processDuration = std::chrono::duration_cast<std::chrono::microseconds>(processEnd - processStart);
        
        // Performance should be reasonable
        double dispatchTimePerEvent = static_cast<double>(dispatchDuration.count()) / numEvents;
        double processTimePerEvent = static_cast<double>(processDuration.count()) / numEvents;
        
        CHECK(dispatchTimePerEvent < 100.0); // Less than 100 microseconds per dispatch
        CHECK(processTimePerEvent < 100.0);  // Less than 100 microseconds per process
        
        eventSystem.Shutdown();
    }
    
    TEST_CASE("Event System Edge Cases")
    {
        auto& eventSystem = Limitless::GetEventSystem();
        eventSystem.Initialize();
        
        // Test with no callbacks
        auto tickEvent = std::make_unique<Limitless::Events::AppTickEvent>(0.016f);
        eventSystem.Dispatch(*tickEvent);
        eventSystem.ProcessEvents(); // ProcessEvents() returns void
        // No events should be processed since there are no callbacks
        
        // Test with multiple callbacks for same event
        int callbackCount = 0;
        
        for (int i = 0; i < 10; ++i)
        {
            eventSystem.AddCallback(Limitless::EventType::AppTick, [&callbackCount](Limitless::Event& event) {
                callbackCount++;
            });
        }
        
        eventSystem.Dispatch(*tickEvent);
        eventSystem.ProcessEvents();
        
        CHECK(callbackCount == 10); // All callbacks should be called
        
        // Test event system shutdown and reinitialization
        eventSystem.Shutdown();
        CHECK(eventSystem.IsInitialized() == false);
        
        eventSystem.Initialize();
        CHECK(eventSystem.IsInitialized() == true);
        
        // Test that callbacks are cleared after shutdown
        callbackCount = 0;
        eventSystem.Dispatch(*tickEvent);
        eventSystem.ProcessEvents();
        
        CHECK(callbackCount == 0); // No callbacks should remain
        
        eventSystem.Shutdown();
    }  
} 