#include "Core/Input/InputActionAssetSerializer.h"

#include "Core/Debug/Log.h"
#include "Core/Input/InputAction.h"

#include <SDL3/SDL_keyboard.h>

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
        // Minimal: serialization is not yet required by the engine runtime.
        // Implement when you introduce an editor tool that writes assets.
        (void)asset;
        (void)path;
        return Result<void>(ErrorCode::NotSupported, "InputActionAssetSerializer::SaveToFile is not implemented yet");
    }
}

