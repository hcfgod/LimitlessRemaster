#pragma once

#include "Core/Error.h"
#include "Core/Input/InputAction.h"

#include <SDL3/SDL_events.h>

#include <functional>
#include <string>

namespace Limitless
{
    // -----------------------------------------------------------------------------
    // InputRebinding
    // Minimal, engine-owned runtime rebinding:
    // - Start a session
    // - Capture next device input (keyboard/mouse/gamepad)
    // - Apply it to an InputAction binding slot
    // - Optionally persist the updated InputActions asset back to JSON
    //
    // This is intentionally simple (no UI). Editor tooling can build on top.
    // -----------------------------------------------------------------------------
    class InputRebinding final
    {
    public:
        enum class DeviceFilter : uint8_t
        {
            KeyboardMouse = 0,
            Gamepad = 1,
            Any = 2
        };

        struct Request
        {
            std::string AssetKey;   // e.g. "Assets/InputActions/EditorCamera.inputactions.json"
            std::string MapName;    // e.g. "Editor"
            std::string ActionName; // e.g. "Boost"
            size_t BindingIndex = 0;

            DeviceFilter Filter = DeviceFilter::Any;

            // If true, persists to disk immediately via InputActionAssetSerializer::SaveToFile.
            bool SaveToDisk = true;
        };

        using CompletionCallback = std::function<void(Result<InputBinding>)>;

        InputRebinding() = default;

        const Request& GetRequest() const { return m_Request; }
        bool IsActive() const { return m_Active; }

        void Start(Request request, CompletionCallback onComplete);
        void Cancel();

        // Called by InputSystem::OnSdlEvent when active.
        // Returns true if event was consumed by the rebinding session.
        bool TryConsumeEvent(const SDL_Event& event);

    private:
        bool AcceptsKeyboardMouse() const;
        bool AcceptsGamepad() const;

        void Complete(Result<InputBinding> result);

        Request m_Request{};
        CompletionCallback m_OnComplete;
        bool m_Active = false;
    };
}

