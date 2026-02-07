#include "Core/Input/InputActionAssetSerializer.h"

#include "Core/Debug/Log.h"
#include "Core/Input/InputAction.h"

#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_gamepad.h>

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>

namespace Limitless
{
    using json = nlohmann::json;

    static InputActionValueType ParseValueType(const std::string& s)
    {
        if (s == "Button") return InputActionValueType::Button;
        if (s == "Axis1D") return InputActionValueType::Axis1D;
        if (s == "Axis2D") return InputActionValueType::Axis2D;
        return InputActionValueType::Button;
    }

    static std::string ToString(InputActionValueType t)
    {
        switch (t)
        {
            case InputActionValueType::Button: return "Button";
            case InputActionValueType::Axis1D: return "Axis1D";
            case InputActionValueType::Axis2D: return "Axis2D";
            default: return "Button";
        }
    }

    static SDL_Scancode ParseScancode(const json& obj, const char* scancodeKey, const char* nameKey)
    {
        if (obj.contains(scancodeKey))
        {
            return static_cast<SDL_Scancode>(obj[scancodeKey].get<int>());
        }
        if (obj.contains(nameKey))
        {
            const std::string name = obj[nameKey].get<std::string>();
            return SDL_GetScancodeFromName(name.c_str());
        }
        return SDL_SCANCODE_UNKNOWN;
    }

    static SDL_GamepadButton ParseGamepadButton(const json& obj, const char* buttonKey, const char* nameKey)
    {
        if (obj.contains(buttonKey))
        {
            return static_cast<SDL_GamepadButton>(obj[buttonKey].get<int>());
        }
        if (obj.contains(nameKey))
        {
            const std::string name = obj[nameKey].get<std::string>();
            return SDL_GetGamepadButtonFromString(name.c_str());
        }
        return SDL_GAMEPAD_BUTTON_INVALID;
    }

    static SDL_GamepadAxis ParseGamepadAxis(const json& obj, const char* axisKey, const char* nameKey)
    {
        if (obj.contains(axisKey))
        {
            return static_cast<SDL_GamepadAxis>(obj[axisKey].get<int>());
        }
        if (obj.contains(nameKey))
        {
            const std::string name = obj[nameKey].get<std::string>();
            return SDL_GetGamepadAxisFromString(name.c_str());
        }
        return SDL_GAMEPAD_AXIS_INVALID;
    }

    Result<std::shared_ptr<InputActionAsset>> InputActionAssetSerializer::LoadFromFile(const std::string& path)
    {
        std::ifstream file(path, std::ios::in | std::ios::binary);
        if (!file.is_open())
        {
            return Result<std::shared_ptr<InputActionAsset>>(ErrorCode::FileNotFound, "InputActionAsset file not found: " + path);
        }

        std::stringstream buffer;
        buffer << file.rdbuf();

        json root;
        try
        {
            root = json::parse(buffer.str());
        }
        catch (const std::exception& e)
        {
            return Result<std::shared_ptr<InputActionAsset>>(ErrorCode::ConfigParseError, std::string("Failed to parse input action asset JSON: ") + e.what());
        }

        auto asset = std::make_shared<InputActionAsset>();

        if (!root.contains("maps") || !root["maps"].is_array())
        {
            return Result<std::shared_ptr<InputActionAsset>>(ErrorCode::InputConfigurationError, "InputActionAsset JSON missing 'maps' array");
        }

        for (const auto& mapJson : root["maps"])
        {
            const std::string mapName = mapJson.value("name", "Default");
            auto& map = asset->AddMap(mapName);

            const bool mapEnabled = mapJson.value("enabled", true);
            map.SetEnabled(mapEnabled);

            const auto actionsIt = mapJson.find("actions");
            if (actionsIt == mapJson.end() || !actionsIt->is_array())
            {
                continue;
            }

            for (const auto& actionJson : *actionsIt)
            {
                const std::string actionName = actionJson.value("name", "Action");
                const std::string typeStr = actionJson.value("type", "Button");
                auto& action = map.AddAction(actionName, ParseValueType(typeStr));

                const auto bindingsIt = actionJson.find("bindings");
                if (bindingsIt == actionJson.end() || !bindingsIt->is_array())
                {
                    continue;
                }

                for (const auto& bindingJson : *bindingsIt)
                {
                    const std::string bindingType = bindingJson.value("binding", "");

                    if (bindingType == "KeyboardButton")
                    {
                        KeyboardButtonBinding b{};
                        b.Key = ParseScancode(bindingJson, "scancode", "key");
                        action.AddBinding(b);
                    }
                    else if (bindingType == "MouseButton")
                    {
                        MouseButtonBinding b{};
                        b.Button = static_cast<uint8_t>(bindingJson.value("button", 0));
                        action.AddBinding(b);
                    }
                    else if (bindingType == "KeyboardAxis1D")
                    {
                        KeyboardAxis1DBinding b{};
                        b.Negative = ParseScancode(bindingJson, "negative_scancode", "negative");
                        b.Positive = ParseScancode(bindingJson, "positive_scancode", "positive");
                        b.NegativeScale = bindingJson.value("negative_scale", -1.0f);
                        b.PositiveScale = bindingJson.value("positive_scale", 1.0f);
                        action.AddBinding(b);
                    }
                    else if (bindingType == "KeyboardAxis2D")
                    {
                        KeyboardAxis2DBinding b{};
                        b.Up = ParseScancode(bindingJson, "up_scancode", "up");
                        b.Down = ParseScancode(bindingJson, "down_scancode", "down");
                        b.Left = ParseScancode(bindingJson, "left_scancode", "left");
                        b.Right = ParseScancode(bindingJson, "right_scancode", "right");
                        b.Scale = bindingJson.value("scale", 1.0f);
                        action.AddBinding(b);
                    }
                    else if (bindingType == "MouseDelta")
                    {
                        MouseDeltaBinding b{};
                        b.Sensitivity = bindingJson.value("sensitivity", 1.0f);
                        b.InvertY = bindingJson.value("invert_y", false);
                        action.AddBinding(b);
                    }
                    else if (bindingType == "GamepadButton")
                    {
                        GamepadButtonBinding b{};
                        b.Button = ParseGamepadButton(bindingJson, "button_id", "button");
                        action.AddBinding(b);
                    }
                    else if (bindingType == "GamepadAxis1D")
                    {
                        GamepadAxis1DBinding b{};
                        b.Axis = ParseGamepadAxis(bindingJson, "axis_id", "axis");
                        b.Scale = bindingJson.value("scale", 1.0f);
                        b.Deadzone = bindingJson.value("deadzone", 0.15f);
                        action.AddBinding(b);
                    }
                    else if (bindingType == "GamepadAxis2D")
                    {
                        GamepadAxis2DBinding b{};
                        b.XAxis = ParseGamepadAxis(bindingJson, "x_axis_id", "x_axis");
                        b.YAxis = ParseGamepadAxis(bindingJson, "y_axis_id", "y_axis");
                        b.Scale = bindingJson.value("scale", 1.0f);
                        b.Deadzone = bindingJson.value("deadzone", 0.15f);
                        b.InvertY = bindingJson.value("invert_y", false);
                        action.AddBinding(b);
                    }
                    else
                    {
                        LT_CORE_WARN("InputActionAssetSerializer: unknown binding type '{}' in '{}::{}'", bindingType, mapName, actionName);
                    }
                }
            }
        }

        return asset;
    }

    Result<void> InputActionAssetSerializer::SaveToFile(const InputActionAsset& asset, const std::string& path)
    {
        json root;
        root["maps"] = json::array();

        for (const auto& mapPtr : asset.GetMaps())
        {
            if (!mapPtr)
            {
                continue;
            }

            json mapJson;
            mapJson["name"] = mapPtr->GetName();
            mapJson["enabled"] = mapPtr->IsEnabled();
            mapJson["actions"] = json::array();

            for (const auto& actionPtr : mapPtr->GetActions())
            {
                if (!actionPtr)
                {
                    continue;
                }

                json actionJson;
                actionJson["name"] = actionPtr->GetName();
                actionJson["type"] = ToString(actionPtr->GetValueType());
                actionJson["bindings"] = json::array();

                for (const auto& binding : actionPtr->GetBindings())
                {
                    json b;

                    if (const auto* kb = std::get_if<KeyboardButtonBinding>(&binding))
                    {
                        b["binding"] = "KeyboardButton";
                        b["key"] = std::string(SDL_GetScancodeName(kb->Key));
                        b["scancode"] = static_cast<int>(kb->Key);
                    }
                    else if (const auto* mb = std::get_if<MouseButtonBinding>(&binding))
                    {
                        b["binding"] = "MouseButton";
                        b["button"] = static_cast<int>(mb->Button);
                    }
                    else if (const auto* a1 = std::get_if<KeyboardAxis1DBinding>(&binding))
                    {
                        b["binding"] = "KeyboardAxis1D";
                        b["negative"] = std::string(SDL_GetScancodeName(a1->Negative));
                        b["positive"] = std::string(SDL_GetScancodeName(a1->Positive));
                        b["negative_scancode"] = static_cast<int>(a1->Negative);
                        b["positive_scancode"] = static_cast<int>(a1->Positive);
                        b["negative_scale"] = a1->NegativeScale;
                        b["positive_scale"] = a1->PositiveScale;
                    }
                    else if (const auto* a2 = std::get_if<KeyboardAxis2DBinding>(&binding))
                    {
                        b["binding"] = "KeyboardAxis2D";
                        b["up"] = std::string(SDL_GetScancodeName(a2->Up));
                        b["down"] = std::string(SDL_GetScancodeName(a2->Down));
                        b["left"] = std::string(SDL_GetScancodeName(a2->Left));
                        b["right"] = std::string(SDL_GetScancodeName(a2->Right));
                        b["up_scancode"] = static_cast<int>(a2->Up);
                        b["down_scancode"] = static_cast<int>(a2->Down);
                        b["left_scancode"] = static_cast<int>(a2->Left);
                        b["right_scancode"] = static_cast<int>(a2->Right);
                        b["scale"] = a2->Scale;
                    }
                    else if (const auto* md = std::get_if<MouseDeltaBinding>(&binding))
                    {
                        b["binding"] = "MouseDelta";
                        b["sensitivity"] = md->Sensitivity;
                        b["invert_y"] = md->InvertY;
                    }
                    else if (const auto* gb = std::get_if<GamepadButtonBinding>(&binding))
                    {
                        b["binding"] = "GamepadButton";
                        b["button"] = std::string(SDL_GetGamepadStringForButton(gb->Button));
                        b["button_id"] = static_cast<int>(gb->Button);
                    }
                    else if (const auto* g1 = std::get_if<GamepadAxis1DBinding>(&binding))
                    {
                        b["binding"] = "GamepadAxis1D";
                        b["axis"] = std::string(SDL_GetGamepadStringForAxis(g1->Axis));
                        b["axis_id"] = static_cast<int>(g1->Axis);
                        b["scale"] = g1->Scale;
                        b["deadzone"] = g1->Deadzone;
                    }
                    else if (const auto* g2 = std::get_if<GamepadAxis2DBinding>(&binding))
                    {
                        b["binding"] = "GamepadAxis2D";
                        b["x_axis"] = std::string(SDL_GetGamepadStringForAxis(g2->XAxis));
                        b["y_axis"] = std::string(SDL_GetGamepadStringForAxis(g2->YAxis));
                        b["x_axis_id"] = static_cast<int>(g2->XAxis);
                        b["y_axis_id"] = static_cast<int>(g2->YAxis);
                        b["scale"] = g2->Scale;
                        b["deadzone"] = g2->Deadzone;
                        b["invert_y"] = g2->InvertY;
                    }

                    if (!b.empty())
                    {
                        actionJson["bindings"].push_back(std::move(b));
                    }
                }

                mapJson["actions"].push_back(std::move(actionJson));
            }

            root["maps"].push_back(std::move(mapJson));
        }

        std::ofstream file(path, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!file.is_open())
        {
            return Result<void>(ErrorCode::FileNotFound, "Failed to open input action asset for writing: " + path);
        }

        file << root.dump(2);
        file.close();

        return Result<void>();
    }
}

