#include "Core/Input/InputSystem.h"

#include "Core/Debug/Log.h"

#include "Assets/AssetManager.h"
#include "Assets/InputActionsAssetImporter.h"

namespace Limitless
{
    InputSystem& InputSystem::GetInstance()
    {
        static InputSystem instance;
        return instance;
    }

    void InputSystem::BeginFrame()
    {
        m_KeyPressedThisFrame.fill(0);
        m_KeyReleasedThisFrame.fill(0);

        m_MousePressedThisFrame.fill(0);
        m_MouseReleasedThisFrame.fill(0);

        m_MouseDelta = glm::vec2(0.0f);
        m_MouseWheelDelta = glm::vec2(0.0f);
    }

    void InputSystem::OnSdlEvent(const SDL_Event& event)
    {
        switch (event.type)
        {
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP:
                OnKey(event.key.scancode, event.key.down, event.key.repeat);
                break;

            case SDL_EVENT_MOUSE_MOTION:
                OnMouseMotion(event.motion.x, event.motion.y, event.motion.xrel, event.motion.yrel);
                break;

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
                OnMouseButton(event.button.button, event.button.down);
                break;

            case SDL_EVENT_MOUSE_WHEEL:
                OnMouseWheel(event.wheel.x, event.wheel.y);
                break;
        }
    }

    void InputSystem::UpdateActions()
    {
        if (auto asset = GetActiveActionAsset())
        {
            asset->Update(*this);
        }
    }

    void InputSystem::PushOverrideActionAsset(std::shared_ptr<InputActionAsset> asset)
    {
        if (!asset)
        {
            LT_CORE_WARN("InputSystem: PushOverrideActionAsset called with null asset (ignored)");
            return;
        }

        m_ActionAssetOverrideStack.push_back(std::move(asset));
    }

    bool InputSystem::PopOverrideActionAsset()
    {
        if (m_ActionAssetOverrideStack.empty())
        {
            return false;
        }

        m_ActionAssetOverrideStack.pop_back();
        return true;
    }

    bool InputSystem::PopOverrideActionAsset(const std::shared_ptr<InputActionAsset>& expectedTop)
    {
        if (m_ActionAssetOverrideStack.empty())
        {
            return false;
        }

        if (expectedTop && m_ActionAssetOverrideStack.back() != expectedTop)
        {
            LT_CORE_WARN("InputSystem: override asset pop mismatch (expected top does not match). Not popping.");
            return false;
        }

        m_ActionAssetOverrideStack.pop_back();
        return true;
    }

    std::shared_ptr<InputActionAsset> InputSystem::GetActiveActionAsset() const
    {
        if (!m_ActionAssetOverrideStack.empty())
        {
            return m_ActionAssetOverrideStack.back();
        }
        return m_ProjectActionAsset;
    }

    void InputSystem::SetProjectActionAssetFromKey(const std::string& key)
    {
        auto asset = Assets::AssetManager::LoadBlocking<Assets::InputActionsAssetResource>(key);
        if (!asset || !asset->GetValue())
        {
            LT_CORE_ERROR("InputSystem: failed to load InputActions asset key='{}'", key);
            return;
        }

        SetProjectActionAsset(asset->GetValue());
    }

    bool InputSystem::IsKeyDown(SDL_Scancode scancode) const
    {
        if (scancode <= SDL_SCANCODE_UNKNOWN || scancode >= SDL_SCANCODE_COUNT)
        {
            return false;
        }
        return m_KeyDown[scancode] != 0;
    }

    bool InputSystem::WasKeyPressedThisFrame(SDL_Scancode scancode) const
    {
        if (scancode <= SDL_SCANCODE_UNKNOWN || scancode >= SDL_SCANCODE_COUNT)
        {
            return false;
        }
        return m_KeyPressedThisFrame[scancode] != 0;
    }

    bool InputSystem::WasKeyReleasedThisFrame(SDL_Scancode scancode) const
    {
        if (scancode <= SDL_SCANCODE_UNKNOWN || scancode >= SDL_SCANCODE_COUNT)
        {
            return false;
        }
        return m_KeyReleasedThisFrame[scancode] != 0;
    }

    bool InputSystem::IsMouseButtonDown(uint8_t button) const
    {
        if (button == 0 || button >= kMaxMouseButtons)
        {
            return false;
        }
        return m_MouseDown[button] != 0;
    }

    bool InputSystem::WasMouseButtonPressedThisFrame(uint8_t button) const
    {
        if (button == 0 || button >= kMaxMouseButtons)
        {
            return false;
        }
        return m_MousePressedThisFrame[button] != 0;
    }

    bool InputSystem::WasMouseButtonReleasedThisFrame(uint8_t button) const
    {
        if (button == 0 || button >= kMaxMouseButtons)
        {
            return false;
        }
        return m_MouseReleasedThisFrame[button] != 0;
    }

    void InputSystem::OnKey(SDL_Scancode scancode, bool down, bool repeat)
    {
        if (scancode <= SDL_SCANCODE_UNKNOWN || scancode >= SDL_SCANCODE_COUNT)
        {
            return;
        }

        const bool wasDown = (m_KeyDown[scancode] != 0);
        m_KeyDown[scancode] = down ? 1 : 0;

        if (!repeat)
        {
            if (!wasDown && down)
            {
                m_KeyPressedThisFrame[scancode] = 1;
            }
            if (wasDown && !down)
            {
                m_KeyReleasedThisFrame[scancode] = 1;
            }
        }
    }

    void InputSystem::OnMouseMotion(float x, float y, float dx, float dy)
    {
        m_MousePosition = glm::vec2(x, y);
        m_MouseDelta += glm::vec2(dx, dy);
    }

    void InputSystem::OnMouseButton(uint8_t button, bool down)
    {
        if (button == 0 || button >= kMaxMouseButtons)
        {
            return;
        }

        const bool wasDown = (m_MouseDown[button] != 0);
        m_MouseDown[button] = down ? 1 : 0;

        if (!wasDown && down)
        {
            m_MousePressedThisFrame[button] = 1;
        }
        if (wasDown && !down)
        {
            m_MouseReleasedThisFrame[button] = 1;
        }
    }

    void InputSystem::OnMouseWheel(float x, float y)
    {
        m_MouseWheelDelta += glm::vec2(x, y);
    }
}

