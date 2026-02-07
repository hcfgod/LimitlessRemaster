#include "Core/Input/InputRebinding.h"

#include "Assets/AssetManager.h"
#include "Assets/AssetPaths.h"
#include "Assets/InputActionsAssetImporter.h"
#include "Core/Debug/Log.h"
#include "Core/Input/InputActionAssetSerializer.h"

#include <SDL3/SDL_gamepad.h>

namespace Limitless
{
    void InputRebinding::Start(Request request, CompletionCallback onComplete)
    {
        m_Request = std::move(request);
        m_OnComplete = std::move(onComplete);
        m_Active = true;
    }

    void InputRebinding::Cancel()
    {
        if (!m_Active)
        {
            return;
        }
        Complete(Result<InputBinding>(ErrorCode::Cancelled, "Rebinding cancelled"));
    }

    bool InputRebinding::AcceptsKeyboardMouse() const
    {
        return (m_Request.Filter == DeviceFilter::Any) || (m_Request.Filter == DeviceFilter::KeyboardMouse);
    }

    bool InputRebinding::AcceptsGamepad() const
    {
        return (m_Request.Filter == DeviceFilter::Any) || (m_Request.Filter == DeviceFilter::Gamepad);
    }

    void InputRebinding::Complete(Result<InputBinding> result)
    {
        m_Active = false;

        if (m_OnComplete)
        {
            m_OnComplete(std::move(result));
        }

        m_OnComplete = nullptr;
        m_Request = {};
    }

    static Result<void> ApplyAndMaybeSave(const InputRebinding::Request& request, const InputBinding& binding)
    {
        using namespace Limitless::Assets;

        auto resource = AssetManager::LoadBlocking<InputActionsAssetResource>(request.AssetKey);
        if (!resource || !resource->GetValue())
        {
            return Result<void>(ErrorCode::FileNotFound, "Failed to load InputActions asset: " + request.AssetKey);
        }

        auto asset = resource->GetValue();
        auto* map = asset->FindMap(request.MapName);
        if (!map)
        {
            return Result<void>(ErrorCode::InputConfigurationError, "InputActions asset missing map: " + request.MapName);
        }

        auto* action = map->FindAction(request.ActionName);
        if (!action)
        {
            return Result<void>(ErrorCode::InputConfigurationError, "InputActions asset missing action: " + request.ActionName);
        }

        if (!action->SetBinding(request.BindingIndex, binding))
        {
            return Result<void>(ErrorCode::InputConfigurationError, "BindingIndex out of range");
        }

        if (!request.SaveToDisk)
        {
            return Result<void>();
        }

        const auto resolved = ResolveAssetKeyToPath(request.AssetKey);
        if (resolved.IsFailure())
        {
            return Result<void>(resolved.GetError());
        }

        const auto saved = InputActionAssetSerializer::SaveToFile(*asset, resolved.GetValue().string());
        if (saved.IsFailure())
        {
            return Result<void>(saved.GetError());
        }

        return Result<void>();
    }

    bool InputRebinding::TryConsumeEvent(const SDL_Event& event)
    {
        if (!m_Active)
        {
            return false;
        }

        // Keyboard
        if (AcceptsKeyboardMouse() && event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
        {
            KeyboardButtonBinding b{};
            b.Key = event.key.scancode;

            const auto applied = ApplyAndMaybeSave(m_Request, b);
            if (applied.IsFailure())
            {
                Complete(Result<InputBinding>(applied.GetError()));
            }
            else
            {
                LT_INFO("Rebinding: {}::{} binding[{}] <- KeyboardButton({})",
                    m_Request.MapName, m_Request.ActionName, m_Request.BindingIndex, static_cast<int>(b.Key));
                Complete(Result<InputBinding>(InputBinding(b)));
            }
            return true;
        }

        // Mouse buttons
        if (AcceptsKeyboardMouse() && event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
        {
            MouseButtonBinding b{};
            b.Button = event.button.button;

            const auto applied = ApplyAndMaybeSave(m_Request, b);
            if (applied.IsFailure())
            {
                Complete(Result<InputBinding>(applied.GetError()));
            }
            else
            {
                LT_INFO("Rebinding: {}::{} binding[{}] <- MouseButton({})",
                    m_Request.MapName, m_Request.ActionName, m_Request.BindingIndex, static_cast<int>(b.Button));
                Complete(Result<InputBinding>(InputBinding(b)));
            }
            return true;
        }

        // Gamepad buttons
        if (AcceptsGamepad() && event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN)
        {
            GamepadButtonBinding b{};
            b.Button = static_cast<SDL_GamepadButton>(event.gbutton.button);

            const auto applied = ApplyAndMaybeSave(m_Request, b);
            if (applied.IsFailure())
            {
                Complete(Result<InputBinding>(applied.GetError()));
            }
            else
            {
                LT_INFO("Rebinding: {}::{} binding[{}] <- GamepadButton({})",
                    m_Request.MapName, m_Request.ActionName, m_Request.BindingIndex, static_cast<int>(b.Button));
                Complete(Result<InputBinding>(InputBinding(b)));
            }
            return true;
        }

        // Gamepad axis motion: capture common 2D sticks + triggers.
        if (AcceptsGamepad() && event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION)
        {
            const SDL_GamepadAxis axis = static_cast<SDL_GamepadAxis>(event.gaxis.axis);
            const int16_t value = event.gaxis.value;

            // Only treat large movement as an intentional capture.
            if (std::abs(static_cast<int>(value)) < 16000)
            {
                return false;
            }

            if (axis == SDL_GAMEPAD_AXIS_LEFTX || axis == SDL_GAMEPAD_AXIS_LEFTY)
            {
                GamepadAxis2DBinding b{};
                b.XAxis = SDL_GAMEPAD_AXIS_LEFTX;
                b.YAxis = SDL_GAMEPAD_AXIS_LEFTY;
                const auto applied = ApplyAndMaybeSave(m_Request, b);
                if (applied.IsFailure()) { Complete(Result<InputBinding>(applied.GetError())); }
                else { Complete(Result<InputBinding>(InputBinding(b))); }
                return true;
            }

            if (axis == SDL_GAMEPAD_AXIS_RIGHTX || axis == SDL_GAMEPAD_AXIS_RIGHTY)
            {
                GamepadAxis2DBinding b{};
                b.XAxis = SDL_GAMEPAD_AXIS_RIGHTX;
                b.YAxis = SDL_GAMEPAD_AXIS_RIGHTY;
                b.InvertY = true;
                const auto applied = ApplyAndMaybeSave(m_Request, b);
                if (applied.IsFailure()) { Complete(Result<InputBinding>(applied.GetError())); }
                else { Complete(Result<InputBinding>(InputBinding(b))); }
                return true;
            }

            if (axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)
            {
                GamepadAxis1DBinding b{};
                b.Axis = axis;
                b.Deadzone = 0.05f;
                const auto applied = ApplyAndMaybeSave(m_Request, b);
                if (applied.IsFailure()) { Complete(Result<InputBinding>(applied.GetError())); }
                else { Complete(Result<InputBinding>(InputBinding(b))); }
                return true;
            }
        }

        return false;
    }
}

