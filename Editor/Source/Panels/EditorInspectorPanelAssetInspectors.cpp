#include "EditorInspectorPanelAssetInspectors.h"

#include "EditorAssetNaming.h"
#include "EditorInspectorPanel.h"

#include "Audio/AudioMixerAsset.h"
#include "Assets/AnimationClipAsset.h"
#include "Assets/AnimatorControllerAsset.h"
#include "Assets/AssetDatabase.h"
#include "Assets/AssetImportPipeline.h"
#include "Assets/AssetManager.h"
#include "Assets/AssetPaths.h"
#include "Assets/AssetTypes.h"
#include "Assets/InputActionsAssetResource.h"
#include "Assets/MaterialAssetImporter.h"
#include "Assets/ShaderAsset.h"
#include "Assets/SpriteImportSettings.h"
#include "Assets/TextureAssetImporter.h"
#include "Graphics/Renderer.h"
#include "Scene/Scene.h"
#include "imgui/imgui.h"

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_mouse.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

namespace Limitless::EditorInspectorPanel
{
    namespace
    {
        constexpr const char* kSubSpritePayloadId = "SUB_SPRITE_KEY";

        std::string ResolveTextureKeyFromDroppedKey(const std::string& droppedKey)
        {
            std::string textureKey;
            int32_t subSpriteIndex = -1;
            if (Assets::TryParseSubSpriteAssetKey(droppedKey, textureKey, subSpriteIndex))
                return textureKey;
            return droppedKey;
        }

        std::vector<std::string> ParseAssetKeyListPayload(const ImGuiPayload* payload)
        {
            std::vector<std::string> keys;
            if (!payload || !payload->Data || payload->DataSize <= 0)
                return keys;

            std::string payloadText(static_cast<const char*>(payload->Data), static_cast<size_t>(payload->DataSize));
            while (!payloadText.empty() && payloadText.back() == '\0')
                payloadText.pop_back();
            if (payloadText.empty())
                return keys;

            size_t lineStart = 0;
            while (lineStart < payloadText.size())
            {
                const size_t lineEnd = payloadText.find('\n', lineStart);
                const size_t count = (lineEnd == std::string::npos) ? (payloadText.size() - lineStart) : (lineEnd - lineStart);
                std::string key = payloadText.substr(lineStart, count);
                if (!key.empty())
                    keys.push_back(std::move(key));
                if (lineEnd == std::string::npos)
                    break;
                lineStart = lineEnd + 1;
            }

            return keys;
        }

        struct ResolvedTextureDrop
        {
            std::string TextureKey;
            bool HasSubRect = false;
            glm::vec2 UvMin = glm::vec2(0.0f);
            glm::vec2 UvMax = glm::vec2(1.0f);
        };

        bool TryResolveTextureDrop(const std::string& droppedKey, ResolvedTextureDrop& outDrop)
        {
            outDrop = ResolvedTextureDrop{};
            if (droppedKey.empty())
                return false;

            std::string textureKey;
            int32_t subSpriteIndex = -1;
            if (!Assets::TryParseSubSpriteAssetKey(droppedKey, textureKey, subSpriteIndex))
            {
                outDrop.TextureKey = droppedKey;
                return !outDrop.TextureKey.empty();
            }

            outDrop.TextureKey = textureKey;
            if (textureKey.empty())
                return false;

            const auto spriteSettings = Assets::LoadSpriteImportSettings(textureKey);
            if (subSpriteIndex < 0 || subSpriteIndex >= static_cast<int32_t>(spriteSettings.SubSprites.size()))
                return true;

            auto textureAsset = Assets::AssetManager::LoadBlocking<Assets::TextureAsset>(textureKey);
            if (!textureAsset || !textureAsset->GetTexture())
                return true;

            const auto uvs = Assets::ComputeSubSpriteUvs(
                spriteSettings.SubSprites[static_cast<size_t>(subSpriteIndex)].RectPixels,
                textureAsset->GetTexture()->GetWidth(),
                textureAsset->GetTexture()->GetHeight());
            outDrop.HasSubRect = true;
            outDrop.UvMin = glm::vec2(uvs.x, uvs.y);
            outDrop.UvMax = glm::vec2(uvs.z, uvs.w);
            return true;
        }

        std::vector<std::string> BuildAssetPickerKeysByType(Assets::AssetType assetType)
        {
            std::vector<std::string> keys;
            std::unordered_set<std::string> seen;

            const auto records = Assets::AssetDatabase::GetInstance().GetAllRecords();
            for (const auto& record : records)
            {
                if (record.Type != assetType || record.Key.empty())
                    continue;
                if (seen.insert(record.Key).second)
                    keys.push_back(record.Key);
            }

            auto tryAddKnownDefault = [&](const char* key) {
                if (!key || !key[0] || seen.contains(key))
                    return;
                const auto resolved = Assets::ResolveAssetKeyToPath(key);
                if (resolved.IsFailure())
                    return;
                std::error_code ec;
                if (std::filesystem::exists(resolved.GetValue(), ec))
                {
                    seen.insert(key);
                    keys.emplace_back(key);
                }
            };

            if (assetType == Assets::AssetType::Shader)
            {
                tryAddKnownDefault("Assets/Shaders/Renderer2D_TexturedQuad.glsl");
                tryAddKnownDefault("Assets/Shaders/Renderer2D_MSDFText.glsl");
                tryAddKnownDefault("Assets/Shaders/TexturedTriangle.glsl");
                tryAddKnownDefault("Assets/Shaders/Lighting2D_GBufferNormalPass.glsl");
                tryAddKnownDefault("Assets/Shaders/Lighting2D_DirectionalLight.glsl");
                tryAddKnownDefault("Assets/Shaders/Lighting2D_PointLight.glsl");
                tryAddKnownDefault("Assets/Shaders/Lighting2D_Composite.glsl");
            }
            else if (assetType == Assets::AssetType::Material)
            {
                tryAddKnownDefault("Assets/Materials/Renderer2D_TexturedQuad.material.json");
                tryAddKnownDefault("Assets/Materials/Renderer2D_MSDFText.material.json");
                tryAddKnownDefault("Assets/Materials/Lighting2D_DefaultLit.material.json");
            }

            std::sort(keys.begin(), keys.end());
            return keys;
        }

        void InvalidateSpriteCachesForTexture(Scene* scene, const std::string& textureKey)
        {
            if (!scene || textureKey.empty())
                return;

            auto& registry = scene->GetRegistry();
            auto view = registry.view<SpriteComponent>();
            for (entt::entity entity : view)
            {
                auto& sprite = view.get<SpriteComponent>(entity);
                if (sprite.TextureKey == textureKey)
                {
                    sprite.CachedTexture.reset();
                    sprite.TextureLoadAttempted = false;
                }
            }
        }

        void ApplyTextureSpecificationAndPersist(Scene* scene,
                                                 Assets::TextureAsset::Ptr textureAsset,
                                                 const TextureSpecification& specification)
        {
            auto texture = textureAsset->GetTexture();
            if (!texture)
                return;

            Renderer::GetInstance().ExecuteImmediate(std::make_unique<SetTextureSpecificationCommand>(texture, specification));
            textureAsset->SetSpecification(specification);

            auto& database = Assets::AssetDatabase::GetInstance();
            const auto settingsJson = Assets::AssetImporter<Assets::TextureAsset>::SettingsToJson(specification);
            database.ImportOrUpdate(textureAsset->GetKey(), Assets::AssetType::Texture2D, settingsJson);
            InvalidateSpriteCachesForTexture(scene, textureAsset->GetKey());
        }

        void PersistTextureSpecificationAndReload(Scene* scene,
                                                  Assets::TextureAsset::Ptr textureAsset,
                                                  const TextureSpecification& specification)
        {
            auto& database = Assets::AssetDatabase::GetInstance();
            const auto settingsJson = Assets::AssetImporter<Assets::TextureAsset>::SettingsToJson(specification);
            database.ImportOrUpdate(textureAsset->GetKey(), Assets::AssetType::Texture2D, settingsJson);
            textureAsset->SetSpecification(specification);
            textureAsset->Reload();
            InvalidateSpriteCachesForTexture(scene, textureAsset->GetKey());
        }

        bool LoadTilesetJson(const std::string& tilesetKey, nlohmann::json& outJson, std::filesystem::path& outResolvedPath)
        {
            const auto resolvedResult = Assets::ResolveAssetKeyToPath(tilesetKey);
            if (resolvedResult.IsFailure())
                return false;

            outResolvedPath = resolvedResult.GetValue();
            std::ifstream input(outResolvedPath, std::ios::in | std::ios::binary);
            if (!input.is_open())
                return false;

            try
            {
                input >> outJson;
            }
            catch (...)
            {
                return false;
            }
            return true;
        }

        bool SaveTilesetJson(Scene* scene,
                             const std::string& selectedTilesetAssetKey,
                             const nlohmann::json& json,
                             const std::filesystem::path& resolvedPath)
        {
            std::ofstream output(resolvedPath, std::ios::out | std::ios::binary | std::ios::trunc);
            if (!output.is_open())
                return false;
            output << json.dump(2);
            output.close();

            (void)Assets::AssetDatabase::GetInstance().ImportOrUpdate(selectedTilesetAssetKey, Assets::AssetType::Tileset);
            (void)Assets::AssetImportPipeline::ReimportChanged(true);

            (void)scene;
            return true;
        }

        bool LoadMaterialJson(const std::string& materialKey, nlohmann::json& outJson, std::filesystem::path& outResolvedPath)
        {
            const auto resolvedResult = Assets::ResolveAssetKeyToPath(materialKey);
            if (resolvedResult.IsFailure())
                return false;

            outResolvedPath = resolvedResult.GetValue();

            std::ifstream in(outResolvedPath, std::ios::in | std::ios::binary);
            if (!in.is_open())
                return false;

            try
            {
                in >> outJson;
            }
            catch (...)
            {
                return false;
            }

            if (!outJson.is_object())
                outJson = nlohmann::json::object();
            return true;
        }

        bool SaveMaterialJsonAndReload(const std::string& materialKey,
                                       Assets::MaterialAsset::Ptr& cachedMaterialAsset,
                                       const nlohmann::json& jsonToSave,
                                       const std::filesystem::path& resolvedPath)
        {
            std::ofstream out(resolvedPath, std::ios::out | std::ios::binary | std::ios::trunc);
            if (!out.is_open())
                return false;

            out << jsonToSave.dump(2);
            out.flush();

            (void)Assets::AssetDatabase::GetInstance().ImportOrUpdate(materialKey, Assets::AssetType::Material);

            if (cachedMaterialAsset && cachedMaterialAsset->GetKey() == materialKey)
                (void)cachedMaterialAsset->Reload();
            else
                cachedMaterialAsset = Assets::AssetManager::LoadBlocking<Assets::MaterialAsset>(materialKey);

            return cachedMaterialAsset != nullptr;
        }

        bool LoadInputActionsJson(const std::string& assetKey, nlohmann::json& outJson, std::filesystem::path& outResolvedPath)
        {
            const auto resolvedResult = Assets::ResolveAssetKeyToPath(assetKey);
            if (resolvedResult.IsFailure())
                return false;

            outResolvedPath = resolvedResult.GetValue();
            std::ifstream input(outResolvedPath, std::ios::in | std::ios::binary);
            if (!input.is_open())
                return false;

            try
            {
                input >> outJson;
            }
            catch (...)
            {
                return false;
            }

            if (!outJson.is_object())
                outJson = nlohmann::json::object();
            return true;
        }

        bool SaveInputActionsJsonAndReload(const std::string& assetKey,
                                           const nlohmann::json& jsonToSave,
                                           const std::filesystem::path& resolvedPath)
        {
            std::ofstream output(resolvedPath, std::ios::out | std::ios::binary | std::ios::trunc);
            if (!output.is_open())
                return false;

            output << jsonToSave.dump(2);
            output.close();

            (void)Assets::AssetDatabase::GetInstance().ImportOrUpdate(assetKey, Assets::AssetType::InputActions);
            (void)Assets::AssetImportPipeline::ReimportChanged(true);

            if (auto cachedBase = Assets::AssetManager::GetCachedByKey(assetKey))
            {
                if (auto cachedInputActions = std::dynamic_pointer_cast<Assets::InputActionsAssetResource>(cachedBase))
                    (void)cachedInputActions->Reload();
            }

            return true;
        }

        constexpr std::array<const char*, 3> kInputActionValueTypes = {
            "Button",
            "Axis1D",
            "Axis2D"
        };

        constexpr std::array<const char*, 8> kInputBindingTypes = {
            "KeyboardButton",
            "MouseButton",
            "KeyboardAxis1D",
            "KeyboardAxis2D",
            "MouseDelta",
            "GamepadButton",
            "GamepadAxis1D",
            "GamepadAxis2D"
        };

        std::string GetScancodeDisplayName(int scancode)
        {
            if (scancode < 0 || scancode >= static_cast<int>(SDL_SCANCODE_COUNT))
                return "Unknown";
            const char* name = SDL_GetScancodeName(static_cast<SDL_Scancode>(scancode));
            if (name && name[0] != '\0')
                return name;
            return "Unknown";
        }

        int ReadScancodeValue(const nlohmann::json& binding, const char* scancodeKey, const char* nameKey)
        {
            int scancodeValue = binding.value(scancodeKey, static_cast<int>(SDL_SCANCODE_UNKNOWN));
            if (binding.contains(nameKey) && binding[nameKey].is_string())
            {
                const std::string keyName = binding[nameKey].get<std::string>();
                if (!keyName.empty())
                    scancodeValue = static_cast<int>(SDL_GetScancodeFromName(keyName.c_str()));
            }

            if (scancodeValue < 0 || scancodeValue >= static_cast<int>(SDL_SCANCODE_COUNT))
                scancodeValue = static_cast<int>(SDL_SCANCODE_UNKNOWN);
            return scancodeValue;
        }

        void WriteScancodeValue(nlohmann::json& binding, const char* scancodeKey, const char* nameKey, int scancodeValue)
        {
            const int clamped = std::clamp(scancodeValue, 0, static_cast<int>(SDL_SCANCODE_COUNT) - 1);
            binding[scancodeKey] = clamped;

            const char* name = SDL_GetScancodeName(static_cast<SDL_Scancode>(clamped));
            if (name && name[0] != '\0')
                binding[nameKey] = std::string(name);
            else
                binding.erase(nameKey);
        }

        std::string GetGamepadButtonDisplayName(int buttonId)
        {
            if (buttonId < 0 || buttonId >= static_cast<int>(SDL_GAMEPAD_BUTTON_COUNT))
                return "Invalid";
            const char* name = SDL_GetGamepadStringForButton(static_cast<SDL_GamepadButton>(buttonId));
            if (name && name[0] != '\0')
                return name;
            return "Unknown";
        }

        int ReadGamepadButtonValue(const nlohmann::json& binding, const char* buttonIdKey, const char* buttonNameKey)
        {
            int buttonId = binding.value(buttonIdKey, static_cast<int>(SDL_GAMEPAD_BUTTON_INVALID));
            if (binding.contains(buttonNameKey) && binding[buttonNameKey].is_string())
            {
                const std::string buttonName = binding[buttonNameKey].get<std::string>();
                if (!buttonName.empty())
                    buttonId = static_cast<int>(SDL_GetGamepadButtonFromString(buttonName.c_str()));
            }

            if (buttonId < 0 || buttonId >= static_cast<int>(SDL_GAMEPAD_BUTTON_COUNT))
                return static_cast<int>(SDL_GAMEPAD_BUTTON_INVALID);
            return buttonId;
        }

        void WriteGamepadButtonValue(nlohmann::json& binding, const char* buttonIdKey, const char* buttonNameKey, int buttonId)
        {
            if (buttonId < 0 || buttonId >= static_cast<int>(SDL_GAMEPAD_BUTTON_COUNT))
            {
                binding[buttonIdKey] = static_cast<int>(SDL_GAMEPAD_BUTTON_INVALID);
                binding.erase(buttonNameKey);
                return;
            }

            binding[buttonIdKey] = buttonId;
            const char* name = SDL_GetGamepadStringForButton(static_cast<SDL_GamepadButton>(buttonId));
            if (name && name[0] != '\0')
                binding[buttonNameKey] = std::string(name);
            else
                binding.erase(buttonNameKey);
        }

        std::string GetGamepadAxisDisplayName(int axisId)
        {
            if (axisId < 0 || axisId >= static_cast<int>(SDL_GAMEPAD_AXIS_COUNT))
                return "Invalid";
            const char* name = SDL_GetGamepadStringForAxis(static_cast<SDL_GamepadAxis>(axisId));
            if (name && name[0] != '\0')
                return name;
            return "Unknown";
        }

        int ReadGamepadAxisValue(const nlohmann::json& binding, const char* axisIdKey, const char* axisNameKey)
        {
            int axisId = binding.value(axisIdKey, static_cast<int>(SDL_GAMEPAD_AXIS_INVALID));
            if (binding.contains(axisNameKey) && binding[axisNameKey].is_string())
            {
                const std::string axisName = binding[axisNameKey].get<std::string>();
                if (!axisName.empty())
                    axisId = static_cast<int>(SDL_GetGamepadAxisFromString(axisName.c_str()));
            }

            if (axisId < 0 || axisId >= static_cast<int>(SDL_GAMEPAD_AXIS_COUNT))
                return static_cast<int>(SDL_GAMEPAD_AXIS_INVALID);
            return axisId;
        }

        void WriteGamepadAxisValue(nlohmann::json& binding, const char* axisIdKey, const char* axisNameKey, int axisId)
        {
            if (axisId < 0 || axisId >= static_cast<int>(SDL_GAMEPAD_AXIS_COUNT))
            {
                binding[axisIdKey] = static_cast<int>(SDL_GAMEPAD_AXIS_INVALID);
                binding.erase(axisNameKey);
                return;
            }

            binding[axisIdKey] = axisId;
            const char* name = SDL_GetGamepadStringForAxis(static_cast<SDL_GamepadAxis>(axisId));
            if (name && name[0] != '\0')
                binding[axisNameKey] = std::string(name);
            else
                binding.erase(axisNameKey);
        }

        nlohmann::json CreateDefaultBindingJson(const std::string& bindingType)
        {
            if (bindingType == "KeyboardButton")
            {
                nlohmann::json binding = nlohmann::json::object();
                binding["binding"] = "KeyboardButton";
                WriteScancodeValue(binding, "scancode", "key", static_cast<int>(SDL_SCANCODE_SPACE));
                return binding;
            }
            if (bindingType == "MouseButton")
            {
                return nlohmann::json{
                    { "binding", "MouseButton" },
                    { "button", static_cast<int>(SDL_BUTTON_LEFT) }
                };
            }
            if (bindingType == "KeyboardAxis1D")
            {
                nlohmann::json binding = nlohmann::json::object();
                binding["binding"] = "KeyboardAxis1D";
                WriteScancodeValue(binding, "negative_scancode", "negative", static_cast<int>(SDL_SCANCODE_A));
                WriteScancodeValue(binding, "positive_scancode", "positive", static_cast<int>(SDL_SCANCODE_D));
                binding["negative_scale"] = -1.0f;
                binding["positive_scale"] = 1.0f;
                return binding;
            }
            if (bindingType == "KeyboardAxis2D")
            {
                nlohmann::json binding = nlohmann::json::object();
                binding["binding"] = "KeyboardAxis2D";
                WriteScancodeValue(binding, "up_scancode", "up", static_cast<int>(SDL_SCANCODE_W));
                WriteScancodeValue(binding, "down_scancode", "down", static_cast<int>(SDL_SCANCODE_S));
                WriteScancodeValue(binding, "left_scancode", "left", static_cast<int>(SDL_SCANCODE_A));
                WriteScancodeValue(binding, "right_scancode", "right", static_cast<int>(SDL_SCANCODE_D));
                binding["scale"] = 1.0f;
                return binding;
            }
            if (bindingType == "MouseDelta")
            {
                return nlohmann::json{
                    { "binding", "MouseDelta" },
                    { "sensitivity", 1.0f },
                    { "invert_y", false }
                };
            }
            if (bindingType == "GamepadButton")
            {
                nlohmann::json binding = nlohmann::json::object();
                binding["binding"] = "GamepadButton";
                WriteGamepadButtonValue(binding, "button_id", "button", static_cast<int>(SDL_GAMEPAD_BUTTON_SOUTH));
                return binding;
            }
            if (bindingType == "GamepadAxis1D")
            {
                nlohmann::json binding = nlohmann::json::object();
                binding["binding"] = "GamepadAxis1D";
                WriteGamepadAxisValue(binding, "axis_id", "axis", static_cast<int>(SDL_GAMEPAD_AXIS_LEFTX));
                binding["scale"] = 1.0f;
                binding["deadzone"] = 0.15f;
                return binding;
            }

            nlohmann::json binding = nlohmann::json::object();
            binding["binding"] = "GamepadAxis2D";
            WriteGamepadAxisValue(binding, "x_axis_id", "x_axis", static_cast<int>(SDL_GAMEPAD_AXIS_LEFTX));
            WriteGamepadAxisValue(binding, "y_axis_id", "y_axis", static_cast<int>(SDL_GAMEPAD_AXIS_LEFTY));
            binding["scale"] = 1.0f;
            binding["deadzone"] = 0.15f;
            binding["invert_y"] = false;
            return binding;
        }

        std::string GetDefaultBindingTypeForActionType(const std::string& actionType)
        {
            if (actionType == "Axis1D")
                return "KeyboardAxis1D";
            if (actionType == "Axis2D")
                return "KeyboardAxis2D";
            return "KeyboardButton";
        }

        bool DrawInputBindingEditor(nlohmann::json& bindingJson)
        {
            bool modified = false;
            if (!bindingJson.is_object())
            {
                bindingJson = CreateDefaultBindingJson("KeyboardButton");
                return true;
            }

            std::string bindingType = bindingJson.value("binding", std::string("KeyboardButton"));
            int bindingTypeIndex = 0;
            for (size_t i = 0; i < kInputBindingTypes.size(); ++i)
            {
                if (bindingType == kInputBindingTypes[i])
                {
                    bindingTypeIndex = static_cast<int>(i);
                    break;
                }
            }

            if (ImGui::Combo("Binding Type", &bindingTypeIndex, kInputBindingTypes.data(), static_cast<int>(kInputBindingTypes.size())))
            {
                bindingType = kInputBindingTypes[static_cast<size_t>(bindingTypeIndex)];
                bindingJson = CreateDefaultBindingJson(bindingType);
                modified = true;
            }

            if (bindingType == "KeyboardButton")
            {
                int scancode = ReadScancodeValue(bindingJson, "scancode", "key");
                if (ImGui::DragInt("Scancode", &scancode, 1.0f, 0, static_cast<int>(SDL_SCANCODE_COUNT) - 1))
                {
                    WriteScancodeValue(bindingJson, "scancode", "key", scancode);
                    modified = true;
                }
                ImGui::TextDisabled("Key: %s", GetScancodeDisplayName(scancode).c_str());
            }
            else if (bindingType == "MouseButton")
            {
                int mouseButton = bindingJson.value("button", static_cast<int>(SDL_BUTTON_LEFT));
                const char* mouseButtonNames[] = { "Left", "Middle", "Right", "X1", "X2" };
                const int mouseButtonValues[] = {
                    static_cast<int>(SDL_BUTTON_LEFT),
                    static_cast<int>(SDL_BUTTON_MIDDLE),
                    static_cast<int>(SDL_BUTTON_RIGHT),
                    static_cast<int>(SDL_BUTTON_X1),
                    static_cast<int>(SDL_BUTTON_X2)
                };

                int selectedMouseIndex = 0;
                for (int i = 0; i < 5; ++i)
                {
                    if (mouseButton == mouseButtonValues[i])
                    {
                        selectedMouseIndex = i;
                        break;
                    }
                }

                if (ImGui::Combo("Mouse Button", &selectedMouseIndex, mouseButtonNames, 5))
                {
                    bindingJson["button"] = mouseButtonValues[selectedMouseIndex];
                    modified = true;
                }
            }
            else if (bindingType == "KeyboardAxis1D")
            {
                int negativeScancode = ReadScancodeValue(bindingJson, "negative_scancode", "negative");
                int positiveScancode = ReadScancodeValue(bindingJson, "positive_scancode", "positive");
                float negativeScale = bindingJson.value("negative_scale", -1.0f);
                float positiveScale = bindingJson.value("positive_scale", 1.0f);

                if (ImGui::DragInt("Negative Scancode", &negativeScancode, 1.0f, 0, static_cast<int>(SDL_SCANCODE_COUNT) - 1))
                {
                    WriteScancodeValue(bindingJson, "negative_scancode", "negative", negativeScancode);
                    modified = true;
                }
                ImGui::TextDisabled("Negative Key: %s", GetScancodeDisplayName(negativeScancode).c_str());

                if (ImGui::DragInt("Positive Scancode", &positiveScancode, 1.0f, 0, static_cast<int>(SDL_SCANCODE_COUNT) - 1))
                {
                    WriteScancodeValue(bindingJson, "positive_scancode", "positive", positiveScancode);
                    modified = true;
                }
                ImGui::TextDisabled("Positive Key: %s", GetScancodeDisplayName(positiveScancode).c_str());

                if (ImGui::DragFloat("Negative Scale", &negativeScale, 0.05f))
                {
                    bindingJson["negative_scale"] = negativeScale;
                    modified = true;
                }
                if (ImGui::DragFloat("Positive Scale", &positiveScale, 0.05f))
                {
                    bindingJson["positive_scale"] = positiveScale;
                    modified = true;
                }
            }
            else if (bindingType == "KeyboardAxis2D")
            {
                int upScancode = ReadScancodeValue(bindingJson, "up_scancode", "up");
                int downScancode = ReadScancodeValue(bindingJson, "down_scancode", "down");
                int leftScancode = ReadScancodeValue(bindingJson, "left_scancode", "left");
                int rightScancode = ReadScancodeValue(bindingJson, "right_scancode", "right");
                float scale = bindingJson.value("scale", 1.0f);

                if (ImGui::DragInt("Up Scancode", &upScancode, 1.0f, 0, static_cast<int>(SDL_SCANCODE_COUNT) - 1))
                {
                    WriteScancodeValue(bindingJson, "up_scancode", "up", upScancode);
                    modified = true;
                }
                ImGui::TextDisabled("Up Key: %s", GetScancodeDisplayName(upScancode).c_str());

                if (ImGui::DragInt("Down Scancode", &downScancode, 1.0f, 0, static_cast<int>(SDL_SCANCODE_COUNT) - 1))
                {
                    WriteScancodeValue(bindingJson, "down_scancode", "down", downScancode);
                    modified = true;
                }
                ImGui::TextDisabled("Down Key: %s", GetScancodeDisplayName(downScancode).c_str());

                if (ImGui::DragInt("Left Scancode", &leftScancode, 1.0f, 0, static_cast<int>(SDL_SCANCODE_COUNT) - 1))
                {
                    WriteScancodeValue(bindingJson, "left_scancode", "left", leftScancode);
                    modified = true;
                }
                ImGui::TextDisabled("Left Key: %s", GetScancodeDisplayName(leftScancode).c_str());

                if (ImGui::DragInt("Right Scancode", &rightScancode, 1.0f, 0, static_cast<int>(SDL_SCANCODE_COUNT) - 1))
                {
                    WriteScancodeValue(bindingJson, "right_scancode", "right", rightScancode);
                    modified = true;
                }
                ImGui::TextDisabled("Right Key: %s", GetScancodeDisplayName(rightScancode).c_str());

                if (ImGui::DragFloat("Scale", &scale, 0.05f))
                {
                    bindingJson["scale"] = scale;
                    modified = true;
                }
            }
            else if (bindingType == "MouseDelta")
            {
                float sensitivity = bindingJson.value("sensitivity", 1.0f);
                bool invertY = bindingJson.value("invert_y", false);
                if (ImGui::DragFloat("Sensitivity", &sensitivity, 0.05f, 0.0f, 50.0f))
                {
                    bindingJson["sensitivity"] = sensitivity;
                    modified = true;
                }
                if (ImGui::Checkbox("Invert Y", &invertY))
                {
                    bindingJson["invert_y"] = invertY;
                    modified = true;
                }
            }
            else if (bindingType == "GamepadButton")
            {
                int buttonId = ReadGamepadButtonValue(bindingJson, "button_id", "button");
                if (ImGui::BeginCombo("Gamepad Button", GetGamepadButtonDisplayName(buttonId).c_str()))
                {
                    for (int id = -1; id < static_cast<int>(SDL_GAMEPAD_BUTTON_COUNT); ++id)
                    {
                        const bool selected = (buttonId == id);
                        const std::string optionLabel = GetGamepadButtonDisplayName(id);
                        if (ImGui::Selectable(optionLabel.c_str(), selected))
                        {
                            WriteGamepadButtonValue(bindingJson, "button_id", "button", id);
                            modified = true;
                        }
                        if (selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
            }
            else if (bindingType == "GamepadAxis1D")
            {
                int axisId = ReadGamepadAxisValue(bindingJson, "axis_id", "axis");
                float scale = bindingJson.value("scale", 1.0f);
                float deadzone = bindingJson.value("deadzone", 0.15f);

                if (ImGui::BeginCombo("Gamepad Axis", GetGamepadAxisDisplayName(axisId).c_str()))
                {
                    for (int id = -1; id < static_cast<int>(SDL_GAMEPAD_AXIS_COUNT); ++id)
                    {
                        const bool selected = (axisId == id);
                        const std::string optionLabel = GetGamepadAxisDisplayName(id);
                        if (ImGui::Selectable(optionLabel.c_str(), selected))
                        {
                            WriteGamepadAxisValue(bindingJson, "axis_id", "axis", id);
                            modified = true;
                        }
                        if (selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                if (ImGui::DragFloat("Scale", &scale, 0.05f))
                {
                    bindingJson["scale"] = scale;
                    modified = true;
                }
                if (ImGui::DragFloat("Deadzone", &deadzone, 0.01f, 0.0f, 1.0f))
                {
                    bindingJson["deadzone"] = std::clamp(deadzone, 0.0f, 1.0f);
                    modified = true;
                }
            }
            else if (bindingType == "GamepadAxis2D")
            {
                int xAxisId = ReadGamepadAxisValue(bindingJson, "x_axis_id", "x_axis");
                int yAxisId = ReadGamepadAxisValue(bindingJson, "y_axis_id", "y_axis");
                float scale = bindingJson.value("scale", 1.0f);
                float deadzone = bindingJson.value("deadzone", 0.15f);
                bool invertY = bindingJson.value("invert_y", false);

                if (ImGui::BeginCombo("X Axis", GetGamepadAxisDisplayName(xAxisId).c_str()))
                {
                    for (int id = -1; id < static_cast<int>(SDL_GAMEPAD_AXIS_COUNT); ++id)
                    {
                        const bool selected = (xAxisId == id);
                        const std::string optionLabel = GetGamepadAxisDisplayName(id);
                        if (ImGui::Selectable(optionLabel.c_str(), selected))
                        {
                            WriteGamepadAxisValue(bindingJson, "x_axis_id", "x_axis", id);
                            modified = true;
                        }
                        if (selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                if (ImGui::BeginCombo("Y Axis", GetGamepadAxisDisplayName(yAxisId).c_str()))
                {
                    for (int id = -1; id < static_cast<int>(SDL_GAMEPAD_AXIS_COUNT); ++id)
                    {
                        const bool selected = (yAxisId == id);
                        const std::string optionLabel = GetGamepadAxisDisplayName(id);
                        if (ImGui::Selectable(optionLabel.c_str(), selected))
                        {
                            WriteGamepadAxisValue(bindingJson, "y_axis_id", "y_axis", id);
                            modified = true;
                        }
                        if (selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                if (ImGui::DragFloat("Scale", &scale, 0.05f))
                {
                    bindingJson["scale"] = scale;
                    modified = true;
                }
                if (ImGui::DragFloat("Deadzone", &deadzone, 0.01f, 0.0f, 1.0f))
                {
                    bindingJson["deadzone"] = std::clamp(deadzone, 0.0f, 1.0f);
                    modified = true;
                }
                if (ImGui::Checkbox("Invert Y", &invertY))
                {
                    bindingJson["invert_y"] = invertY;
                    modified = true;
                }
            }

            bindingJson["binding"] = bindingType;
            return modified;
        }
    }

    // Sprite Editor open request — set by the inspector, polled by EditorLayer.
    static std::string s_PendingSpriteEditorTextureKey;

    const std::string& GetPendingSpriteEditorRequest()
    {
        return s_PendingSpriteEditorTextureKey;
    }

    void ClearPendingSpriteEditorRequest()
    {
        s_PendingSpriteEditorTextureKey.clear();
    }

    void DrawTextureInspector(Scene* scene,
                              std::string& selectedTextureAssetKey,
                              Assets::TextureAsset::Ptr& cachedTextureAsset)
    {
        std::string resolvedTextureAssetKey = selectedTextureAssetKey;
        int32_t selectedSubSpriteIndex = -1;
        std::string parsedTextureAssetKey;
        if (Assets::TryParseSubSpriteAssetKey(selectedTextureAssetKey, parsedTextureAssetKey, selectedSubSpriteIndex))
            resolvedTextureAssetKey = parsedTextureAssetKey;

        if (resolvedTextureAssetKey.empty())
        {
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Invalid texture selection.");
            return;
        }

        if (!cachedTextureAsset || cachedTextureAsset->GetKey() != resolvedTextureAssetKey)
            cachedTextureAsset = Assets::AssetManager::LoadBlocking<Assets::TextureAsset>(resolvedTextureAssetKey);

        auto textureAsset = cachedTextureAsset;
        if (!textureAsset)
        {
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Failed to load texture: %s", resolvedTextureAssetKey.c_str());
            cachedTextureAsset.reset();
            return;
        }

        const auto* texture = textureAsset->GetTexture().get();
        if (!texture)
        {
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Texture not ready.");
            return;
        }

        const std::string fileName = std::filesystem::path(resolvedTextureAssetKey).filename().string();
        ImGui::Text("Texture: %s", fileName.c_str());
        ImGui::Text("%u x %u", texture->GetWidth(), texture->GetHeight());
        ImGui::Spacing();

        // Cache the sprite settings per-texture to avoid reading .meta every frame.
        struct SpriteSettingsCache
        {
            std::string TextureKey;
            Assets::SpriteImportSettings Settings;
            bool Loaded = false;
        };
        static SpriteSettingsCache s_SpriteCache;

        if (!s_SpriteCache.Loaded || s_SpriteCache.TextureKey != resolvedTextureAssetKey)
        {
            s_SpriteCache.TextureKey = resolvedTextureAssetKey;
            s_SpriteCache.Settings = Assets::LoadSpriteImportSettings(resolvedTextureAssetKey);
            s_SpriteCache.Loaded = true;
        }

        auto& spriteSettings = s_SpriteCache.Settings;
        const bool showingSubSpritePreview =
            selectedSubSpriteIndex >= 0 &&
            selectedSubSpriteIndex < static_cast<int32_t>(spriteSettings.SubSprites.size());

        float previewSourceWidth = static_cast<float>(texture->GetWidth());
        float previewSourceHeight = static_cast<float>(texture->GetHeight());
        ImVec2 uv0(0.0f, 1.0f);
        ImVec2 uv1(1.0f, 0.0f);
        if (showingSubSpritePreview)
        {
            const auto& sub = spriteSettings.SubSprites[static_cast<size_t>(selectedSubSpriteIndex)];
            previewSourceWidth = static_cast<float>(std::max(1, sub.RectPixels.z));
            previewSourceHeight = static_cast<float>(std::max(1, sub.RectPixels.w));
            const glm::vec4 subUvs = Assets::ComputeSubSpriteUvs(
                sub.RectPixels,
                texture->GetWidth(),
                texture->GetHeight());
            uv0 = ImVec2(subUvs.x, 1.0f - subUvs.y);
            uv1 = ImVec2(subUvs.z, 1.0f - subUvs.w);

            ImGui::TextDisabled("Sub-Sprite: %s (#%d)",
                                sub.Name.empty() ? "(unnamed)" : sub.Name.c_str(),
                                selectedSubSpriteIndex);
        }
        else if (selectedSubSpriteIndex >= 0)
        {
            ImGui::TextDisabled("Sub-Sprite: invalid index #%d", selectedSubSpriteIndex);
        }

        const float previewSize = 256.0f;
        const float aspect = previewSourceHeight / std::max(1.0f, previewSourceWidth);
        const ImVec4 tintColor(1.0f, 1.0f, 1.0f, 1.0f);
        const ImVec4 borderColor(0.4f, 0.4f, 0.4f, 1.0f);
        if (aspect > 1.0f)
        {
            const float width = previewSize / aspect;
            ImGui::Image((ImTextureID)(void*)(uintptr_t)texture->GetRendererID(), ImVec2(width, previewSize), uv0, uv1, tintColor, borderColor);
        }
        else
        {
            const float height = previewSize * aspect;
            ImGui::Image((ImTextureID)(void*)(uintptr_t)texture->GetRendererID(), ImVec2(previewSize, height), uv0, uv1, tintColor, borderColor);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        const char* spriteModeNames[] = { "Single", "Multiple" };
        int spriteModeIndex = static_cast<int>(spriteSettings.Mode);
        if (ImGui::Combo("Sprite Mode", &spriteModeIndex, spriteModeNames, 2))
        {
            spriteSettings.Mode = static_cast<Assets::SpriteImportSettings::SpriteMode>(spriteModeIndex);
            const auto saveResult = Assets::SaveSpriteImportSettings(resolvedTextureAssetKey, spriteSettings);
            if (!saveResult.IsSuccess())
                LT_CORE_WARN("Failed to save sprite import settings for '{}': {}", resolvedTextureAssetKey, saveResult.GetError().GetErrorMessage());
        }

        float ppu = spriteSettings.PixelsPerUnit;
        if (ImGui::DragFloat("Pixels Per Unit", &ppu, 0.5f, 0.01f, 4096.0f, "%.1f"))
        {
            spriteSettings.PixelsPerUnit = std::max(0.01f, ppu);
            const auto saveResult = Assets::SaveSpriteImportSettings(resolvedTextureAssetKey, spriteSettings);
            if (!saveResult.IsSuccess())
                LT_CORE_WARN("Failed to save sprite import settings for '{}': {}", resolvedTextureAssetKey, saveResult.GetError().GetErrorMessage());
        }

        if (spriteSettings.Mode == Assets::SpriteImportSettings::SpriteMode::Multiple)
        {
            ImGui::Spacing();
            if (ImGui::Button("Open Sprite Editor", ImVec2(-1, 0)))
            {
                s_PendingSpriteEditorTextureKey = resolvedTextureAssetKey;
            }

            if (!spriteSettings.SubSprites.empty())
            {
                ImGui::TextDisabled("%zu sub-sprites defined", spriteSettings.SubSprites.size());
            }
            else
            {
                ImGui::TextDisabled("No sub-sprites. Open Sprite Editor to slice.");
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // --- Texture Specification ---
        TextureSpecification specification = textureAsset->GetSpecification();

        const char* filterNames[] = { "Nearest", "Linear" };
        int minimumFilterIndex = static_cast<int>(specification.MinFilter);
        if (ImGui::Combo("Min Filter", &minimumFilterIndex, filterNames, 2))
        {
            specification.MinFilter = static_cast<TextureFilter>(minimumFilterIndex);
            ApplyTextureSpecificationAndPersist(scene, textureAsset, specification);
        }

        int magnificationFilterIndex = static_cast<int>(specification.MagFilter);
        if (ImGui::Combo("Mag Filter", &magnificationFilterIndex, filterNames, 2))
        {
            specification.MagFilter = static_cast<TextureFilter>(magnificationFilterIndex);
            ApplyTextureSpecificationAndPersist(scene, textureAsset, specification);
        }

        const char* wrapNames[] = { "Repeat", "Clamp To Edge" };
        int wrapUIndex = static_cast<int>(specification.WrapU);
        if (ImGui::Combo("Wrap U", &wrapUIndex, wrapNames, 2))
        {
            specification.WrapU = static_cast<TextureWrap>(wrapUIndex);
            ApplyTextureSpecificationAndPersist(scene, textureAsset, specification);
        }

        int wrapVIndex = static_cast<int>(specification.WrapV);
        if (ImGui::Combo("Wrap V", &wrapVIndex, wrapNames, 2))
        {
            specification.WrapV = static_cast<TextureWrap>(wrapVIndex);
            ApplyTextureSpecificationAndPersist(scene, textureAsset, specification);
        }

        ImGui::AlignTextToFramePadding();
        ImGui::Text("Generate Mipmaps");
        bool generateMipmaps = specification.GenerateMipmaps;
        if (ImGui::Checkbox("##GenerateMipmaps", &generateMipmaps))
        {
            specification.GenerateMipmaps = generateMipmaps;
            PersistTextureSpecificationAndReload(scene, textureAsset, specification);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Requires texture reload to take effect.");

        ImGui::AlignTextToFramePadding();
        ImGui::Text("Flip Vertically On Load");
        bool flipVerticallyOnLoad = specification.FlipVerticallyOnLoad;
        if (ImGui::Checkbox("##FlipVerticallyOnLoad", &flipVerticallyOnLoad))
        {
            specification.FlipVerticallyOnLoad = flipVerticallyOnLoad;
            PersistTextureSpecificationAndReload(scene, textureAsset, specification);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Requires texture reload to take effect.");
    }

    void DrawTilesetAssetInspector(Scene* scene, std::string& selectedTilesetAssetKey)
    {
        struct State
        {
            std::string LoadedKey;
            std::filesystem::path ResolvedPath;
            nlohmann::json Json = nlohmann::json::object();
            bool Loaded = false;
        };
        static State s_State;

        if (selectedTilesetAssetKey.empty())
            return;

        if (!s_State.Loaded || s_State.LoadedKey != selectedTilesetAssetKey)
        {
            s_State = {};
            s_State.LoadedKey = selectedTilesetAssetKey;
            s_State.Loaded = LoadTilesetJson(selectedTilesetAssetKey, s_State.Json, s_State.ResolvedPath);
            if (!s_State.Loaded)
            {
                ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Failed to load tileset JSON: %s", selectedTilesetAssetKey.c_str());
                return;
            }
        }

        ImGui::Text("Tileset: %s", std::filesystem::path(selectedTilesetAssetKey).filename().string().c_str());
        ImGui::TextDisabled("Asset Key: %s", selectedTilesetAssetKey.c_str());
        ImGui::Separator();

        std::string textureKey = s_State.Json.value("TextureKey", std::string{});
        std::string textureLabel = textureKey.empty()
            ? std::string("None")
            : EditorAssetNaming::GetAssetDisplayNameFromAssetKey(textureKey);
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Texture");
        ImGui::Button((textureLabel + "##TilesetTexture").c_str(),
                      ImVec2(std::max(60.0f, ImGui::GetContentRegionAvail().x - 90.0f), 0.0f));
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kSubSpritePayloadId))
            {
                const char* key = static_cast<const char*>(payload->Data);
                if (key && key[0])
                {
                    const std::string resolvedTextureKey = ResolveTextureKeyFromDroppedKey(key);
                    if (!resolvedTextureKey.empty() && textureKey != resolvedTextureKey)
                    {
                        textureKey = resolvedTextureKey;
                        s_State.Json["TextureKey"] = textureKey;
                        (void)SaveTilesetJson(scene, selectedTilesetAssetKey, s_State.Json, s_State.ResolvedPath);
                    }
                }
            }
            else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_TEXTURE"))
            {
                const char* key = static_cast<const char*>(payload->Data);
                if (key && key[0])
                {
                    const std::string resolvedTextureKey = ResolveTextureKeyFromDroppedKey(key);
                    if (!resolvedTextureKey.empty() && textureKey != resolvedTextureKey)
                    {
                        textureKey = resolvedTextureKey;
                        s_State.Json["TextureKey"] = textureKey;
                        (void)SaveTilesetJson(scene, selectedTilesetAssetKey, s_State.Json, s_State.ResolvedPath);
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::SameLine();
        if (ImGui::Button("...##TilesetTexturePicker"))
            ImGui::OpenPopup("TilesetTexturePickerPopup");
        if (ImGui::BeginPopup("TilesetTexturePickerPopup"))
        {
            const std::vector<std::string> textureKeys = BuildAssetPickerKeysByType(Assets::AssetType::Texture2D);
            for (const auto& key : textureKeys)
            {
                const std::string display = EditorAssetNaming::GetAssetDisplayNameFromAssetKey(key);
                if (ImGui::Selectable((display + "##TilesetTexturePicker_" + key).c_str(), textureKey == key))
                {
                    textureKey = key;
                    s_State.Json["TextureKey"] = textureKey;
                    (void)SaveTilesetJson(scene, selectedTilesetAssetKey, s_State.Json, s_State.ResolvedPath);
                }
            }
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("X##ClearTilesetTexture"))
        {
            s_State.Json["TextureKey"] = "";
            (void)SaveTilesetJson(scene, selectedTilesetAssetKey, s_State.Json, s_State.ResolvedPath);
        }

        std::vector<int> tileSize = s_State.Json.value("TileSizePixels", std::vector<int>{ 16, 16 });
        if (tileSize.size() < 2)
            tileSize = { 16, 16 };
        int32_t tileSizePixels[2] = { std::max(1, tileSize[0]), std::max(1, tileSize[1]) };
        if (ImGui::DragInt2("Tile Size Pixels", tileSizePixels, 1.0f, 1, 4096))
        {
            s_State.Json["TileSizePixels"] = { std::max(1, tileSizePixels[0]), std::max(1, tileSizePixels[1]) };
            (void)SaveTilesetJson(scene, selectedTilesetAssetKey, s_State.Json, s_State.ResolvedPath);
        }
    }

    void DrawInputActionsAssetInspector(std::string& selectedInputActionsAssetKey)
    {
        struct State
        {
            std::string LoadedKey;
            std::filesystem::path ResolvedPath;
            nlohmann::json Json = nlohmann::json::object();
            bool Loaded = false;
        };
        static State s_State;

        if (selectedInputActionsAssetKey.empty())
            return;

        if (!s_State.Loaded || s_State.LoadedKey != selectedInputActionsAssetKey)
        {
            s_State = {};
            s_State.LoadedKey = selectedInputActionsAssetKey;
            s_State.Loaded = LoadInputActionsJson(selectedInputActionsAssetKey, s_State.Json, s_State.ResolvedPath);
            if (!s_State.Loaded)
            {
                ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Failed to load input actions JSON: %s", selectedInputActionsAssetKey.c_str());
                return;
            }
        }

        bool modified = false;
        bool saveFailed = false;

        ImGui::Text("Input Actions: %s", std::filesystem::path(selectedInputActionsAssetKey).filename().string().c_str());
        ImGui::TextDisabled("Asset Key: %s", selectedInputActionsAssetKey.c_str());
        ImGui::Separator();

        if (!s_State.Json.contains("maps") || !s_State.Json["maps"].is_array())
        {
            s_State.Json["maps"] = nlohmann::json::array();
            modified = true;
        }

        auto& maps = s_State.Json["maps"];

        ImGui::TextUnformatted("Action Maps");
        if (ImGui::Button("Add Action Map", ImVec2(160.0f, 0.0f)))
        {
            nlohmann::json newMap = nlohmann::json::object();
            newMap["name"] = "NewMap" + std::to_string(maps.size() + 1);
            newMap["enabled"] = true;
            newMap["actions"] = nlohmann::json::array();
            maps.push_back(std::move(newMap));
            modified = true;
        }

        if (maps.empty())
            ImGui::TextDisabled("No action maps yet.");

        int removeMapIndex = -1;

        for (size_t mapIndex = 0; mapIndex < maps.size(); ++mapIndex)
        {
            auto& mapJson = maps[mapIndex];
            if (!mapJson.is_object())
            {
                mapJson = nlohmann::json::object();
                modified = true;
            }

            if (!mapJson.contains("name") || !mapJson["name"].is_string())
            {
                mapJson["name"] = "Map" + std::to_string(mapIndex + 1);
                modified = true;
            }
            if (!mapJson.contains("enabled") || !mapJson["enabled"].is_boolean())
            {
                mapJson["enabled"] = true;
                modified = true;
            }
            if (!mapJson.contains("actions") || !mapJson["actions"].is_array())
            {
                mapJson["actions"] = nlohmann::json::array();
                modified = true;
            }

            std::string mapName = mapJson.value("name", std::string("Map" + std::to_string(mapIndex + 1)));
            const std::string mapLabel = mapName.empty()
                ? ("Map " + std::to_string(mapIndex + 1) + "##InputMap_" + std::to_string(mapIndex))
                : ("Map: " + mapName + "##InputMap_" + std::to_string(mapIndex));

            ImGuiTreeNodeFlags mapFlags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
            const bool mapOpen = ImGui::TreeNodeEx(mapLabel.c_str(), mapFlags);

            ImGui::SameLine();
            const std::string removeMapButtonLabel = "Remove##RemoveMap_" + std::to_string(mapIndex);
            if (ImGui::Button(removeMapButtonLabel.c_str()))
                removeMapIndex = static_cast<int>(mapIndex);

            if (!mapOpen)
                continue;

            ImGui::PushID(static_cast<int>(mapIndex));

            std::array<char, 128> mapNameBuffer{};
            std::snprintf(mapNameBuffer.data(), mapNameBuffer.size(), "%s", mapName.c_str());
            if (ImGui::InputText("Map Name", mapNameBuffer.data(), mapNameBuffer.size()))
            {
                mapJson["name"] = std::string(mapNameBuffer.data());
                modified = true;
            }

            bool mapEnabled = mapJson.value("enabled", true);
            if (ImGui::Checkbox("Enabled", &mapEnabled))
            {
                mapJson["enabled"] = mapEnabled;
                modified = true;
            }

            auto& actions = mapJson["actions"];
            int removeActionIndex = -1;

            if (ImGui::Button("Add Action", ImVec2(120.0f, 0.0f)))
            {
                nlohmann::json newAction = nlohmann::json::object();
                newAction["name"] = "Action" + std::to_string(actions.size() + 1);
                newAction["type"] = "Button";
                newAction["bindings"] = nlohmann::json::array();
                actions.push_back(std::move(newAction));
                modified = true;
            }

            if (actions.empty())
                ImGui::TextDisabled("No actions in this map.");

            for (size_t actionIndex = 0; actionIndex < actions.size(); ++actionIndex)
            {
                auto& actionJson = actions[actionIndex];
                if (!actionJson.is_object())
                {
                    actionJson = nlohmann::json::object();
                    modified = true;
                }

                if (!actionJson.contains("name") || !actionJson["name"].is_string())
                {
                    actionJson["name"] = "Action" + std::to_string(actionIndex + 1);
                    modified = true;
                }
                if (!actionJson.contains("type") || !actionJson["type"].is_string())
                {
                    actionJson["type"] = "Button";
                    modified = true;
                }
                if (!actionJson.contains("bindings") || !actionJson["bindings"].is_array())
                {
                    actionJson["bindings"] = nlohmann::json::array();
                    modified = true;
                }

                const std::string actionName = actionJson.value("name", std::string("Action" + std::to_string(actionIndex + 1)));
                const std::string actionLabel = actionName.empty()
                    ? ("Action " + std::to_string(actionIndex + 1) + "##InputAction_" + std::to_string(actionIndex))
                    : ("Action: " + actionName + "##InputAction_" + std::to_string(actionIndex));
                const bool actionOpen = ImGui::TreeNodeEx(actionLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth);

                ImGui::SameLine();
                const std::string removeActionButtonLabel = "Remove##RemoveAction_" + std::to_string(actionIndex);
                if (ImGui::Button(removeActionButtonLabel.c_str()))
                    removeActionIndex = static_cast<int>(actionIndex);

                if (!actionOpen)
                    continue;

                ImGui::PushID(static_cast<int>(actionIndex));

                std::array<char, 128> actionNameBuffer{};
                std::snprintf(actionNameBuffer.data(), actionNameBuffer.size(), "%s", actionName.c_str());
                if (ImGui::InputText("Action Name", actionNameBuffer.data(), actionNameBuffer.size()))
                {
                    actionJson["name"] = std::string(actionNameBuffer.data());
                    modified = true;
                }

                std::string actionType = actionJson.value("type", std::string("Button"));
                int actionTypeIndex = 0;
                for (size_t typeIndex = 0; typeIndex < kInputActionValueTypes.size(); ++typeIndex)
                {
                    if (actionType == kInputActionValueTypes[typeIndex])
                    {
                        actionTypeIndex = static_cast<int>(typeIndex);
                        break;
                    }
                }
                if (ImGui::Combo("Action Type", &actionTypeIndex, kInputActionValueTypes.data(), static_cast<int>(kInputActionValueTypes.size())))
                {
                    actionJson["type"] = std::string(kInputActionValueTypes[static_cast<size_t>(actionTypeIndex)]);
                    actionType = actionJson["type"].get<std::string>();
                    modified = true;
                }

                auto& bindings = actionJson["bindings"];
                int removeBindingIndex = -1;

                if (ImGui::Button("Add Binding", ImVec2(120.0f, 0.0f)))
                {
                    const std::string defaultBindingType = GetDefaultBindingTypeForActionType(actionType);
                    bindings.push_back(CreateDefaultBindingJson(defaultBindingType));
                    modified = true;
                }

                if (bindings.empty())
                    ImGui::TextDisabled("No bindings on this action.");

                for (size_t bindingIndex = 0; bindingIndex < bindings.size(); ++bindingIndex)
                {
                    auto& bindingJson = bindings[bindingIndex];
                    const std::string bindingLabel = "Binding " + std::to_string(bindingIndex + 1) + "##Binding_" + std::to_string(bindingIndex);
                    const bool bindingOpen = ImGui::TreeNodeEx(bindingLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth);

                    ImGui::SameLine();
                    const std::string removeBindingButtonLabel = "Remove##RemoveBinding_" + std::to_string(bindingIndex);
                    if (ImGui::Button(removeBindingButtonLabel.c_str()))
                        removeBindingIndex = static_cast<int>(bindingIndex);

                    if (bindingOpen)
                    {
                        ImGui::PushID(static_cast<int>(bindingIndex));
                        if (DrawInputBindingEditor(bindingJson))
                            modified = true;
                        ImGui::PopID();
                        ImGui::TreePop();
                    }
                }

                if (removeBindingIndex >= 0)
                {
                    bindings.erase(static_cast<size_t>(removeBindingIndex));
                    modified = true;
                }

                ImGui::PopID();
                ImGui::TreePop();
            }

            if (removeActionIndex >= 0)
            {
                actions.erase(static_cast<size_t>(removeActionIndex));
                modified = true;
            }

            ImGui::PopID();
            ImGui::TreePop();
        }

        if (removeMapIndex >= 0)
        {
            maps.erase(static_cast<size_t>(removeMapIndex));
            modified = true;
        }

        if (modified)
        {
            if (!SaveInputActionsJsonAndReload(selectedInputActionsAssetKey, s_State.Json, s_State.ResolvedPath))
                saveFailed = true;
        }

        if (saveFailed)
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Failed to save input actions asset.");
    }

    void DrawAudioMixerAssetInspector(std::string& selectedAudioMixerAssetKey)
    {
        struct State
        {
            std::string LoadedKey;
            std::filesystem::path ResolvedPath;
            Audio::AudioMixerDefinition Definition{};
            bool Loaded = false;
        };
        static State s_State;

        if (selectedAudioMixerAssetKey.empty())
            return;

        if (!s_State.Loaded || s_State.LoadedKey != selectedAudioMixerAssetKey)
        {
            s_State = {};
            s_State.LoadedKey = selectedAudioMixerAssetKey;
            s_State.Loaded = Audio::LoadAudioMixerDefinitionFromAssetKey(
                selectedAudioMixerAssetKey,
                s_State.Definition,
                &s_State.ResolvedPath);
            if (!s_State.Loaded)
            {
                ImGui::TextColored(
                    ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                    "Failed to load audio mixer asset: %s",
                    selectedAudioMixerAssetKey.c_str());
                return;
            }
        }

        bool modified = false;
        bool saveFailed = false;

        ImGui::Text("Audio Mixer: %s", std::filesystem::path(selectedAudioMixerAssetKey).filename().string().c_str());
        ImGui::TextDisabled("Asset Key: %s", selectedAudioMixerAssetKey.c_str());
        ImGui::Separator();

        if (ImGui::Button("Add Group", ImVec2(120.0f, 0.0f)))
        {
            std::string groupName = "Group";
            int32_t suffix = 1;
            auto nameExists = [&](const std::string& name) {
                return std::any_of(
                    s_State.Definition.Groups.begin(),
                    s_State.Definition.Groups.end(),
                    [&name](const Audio::AudioMixerGroupEntry& group) {
                        return group.Name == name;
                    });
            };
            while (nameExists(groupName))
            {
                ++suffix;
                groupName = "Group" + std::to_string(suffix);
            }

            s_State.Definition.Groups.push_back(Audio::AudioMixerGroupEntry{ groupName, 1.0f });
            modified = true;
        }

        if (s_State.Definition.Groups.empty())
            ImGui::TextDisabled("No groups authored.");

        int32_t removeGroupIndex = -1;
        for (int32_t groupIndex = 0; groupIndex < static_cast<int32_t>(s_State.Definition.Groups.size()); ++groupIndex)
        {
            auto& group = s_State.Definition.Groups[static_cast<size_t>(groupIndex)];
            ImGui::PushID(groupIndex);

            std::array<char, 128> groupNameBuffer{};
            std::snprintf(groupNameBuffer.data(), groupNameBuffer.size(), "%s", group.Name.c_str());
            if (ImGui::InputText("Group", groupNameBuffer.data(), groupNameBuffer.size()))
            {
                group.Name = groupNameBuffer.data();
                modified = true;
            }

            if (ImGui::SliderFloat("Volume", &group.Volume, 0.0f, 2.0f, "%.2f"))
            {
                group.Volume = std::max(0.0f, group.Volume);
                modified = true;
            }

            if (ImGui::SliderFloat("Reverb Send", &group.ReverbSend, 0.0f, 1.0f, "%.2f"))
            {
                group.ReverbSend = std::clamp(group.ReverbSend, 0.0f, 1.0f);
                modified = true;
            }

            if (ImGui::Button("Remove Group", ImVec2(120.0f, 0.0f)))
                removeGroupIndex = groupIndex;

            ImGui::Separator();
            ImGui::PopID();
        }

        if (removeGroupIndex >= 0 &&
            removeGroupIndex < static_cast<int32_t>(s_State.Definition.Groups.size()))
        {
            s_State.Definition.Groups.erase(
                s_State.Definition.Groups.begin() + removeGroupIndex);
            modified = true;
        }

        if (modified)
        {
            Audio::NormalizeAudioMixerDefinition(s_State.Definition);
            if (!Audio::SaveAudioMixerDefinitionToPath(s_State.ResolvedPath, s_State.Definition))
            {
                saveFailed = true;
            }
            else
            {
                (void)Assets::AssetDatabase::GetInstance().ImportOrUpdate(
                    selectedAudioMixerAssetKey,
                    Assets::AssetType::AudioMixer);
                (void)Assets::AssetImportPipeline::ReimportChanged(true);
            }
        }

        if (saveFailed)
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Failed to save audio mixer asset.");
    }

    void DrawNativeScriptAssetInspector(std::string& selectedNativeScriptAssetKey)
    {
        if (selectedNativeScriptAssetKey.empty())
            return;

        std::filesystem::path selectedPath(selectedNativeScriptAssetKey);
        std::string extension = selectedPath.extension().string();
        for (char& character : extension)
            character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));

        if (extension != ".h" && extension != ".cpp")
        {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Selected asset is not a native script file.");
            if (ImGui::Button("Clear Selection", ImVec2(160.0f, 0.0f)))
                selectedNativeScriptAssetKey.clear();
            return;
        }

        const bool isHeader = (extension == ".h");
        const std::string selectedFileName = selectedPath.filename().string();
        const std::string scriptClassName = selectedPath.stem().string();

        std::filesystem::path pairedPath = selectedPath;
        pairedPath.replace_extension(isHeader ? ".cpp" : ".h");
        const std::string pairedAssetKey = pairedPath.generic_string();
        const auto resolvedSelectedPath = Assets::ResolveAssetKeyToPath(selectedNativeScriptAssetKey);
        const auto resolvedPairedPath = Assets::ResolveAssetKeyToPath(pairedAssetKey);
        const bool pairedExists = resolvedPairedPath.IsSuccess();

        ImGui::Text("Native Script: %s", selectedFileName.c_str());
        ImGui::TextDisabled("Class: %s", scriptClassName.c_str());
        ImGui::TextDisabled("Type: %s", isHeader ? "Header (.h)" : "Source (.cpp)");
        ImGui::TextDisabled("Asset Key: %s", selectedNativeScriptAssetKey.c_str());
        if (resolvedSelectedPath.IsSuccess())
            ImGui::TextDisabled("Path: %s", resolvedSelectedPath.GetValue().string().c_str());
        else
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Could not resolve selected script path.");

        ImGui::TextDisabled("Paired File: %s", pairedAssetKey.c_str());
        if (!pairedExists)
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "Pair file missing (.h + .cpp should both exist).");

        ImGui::Spacing();
        if (ImGui::Button("Open Script", ImVec2(-1.0f, 0.0f)))
            (void)EditorInspectorPanel::OpenNativeScriptEditorForAssetKey(selectedNativeScriptAssetKey);
        if (pairedExists && ImGui::Button("Open Paired Script", ImVec2(-1.0f, 0.0f)))
            (void)EditorInspectorPanel::OpenNativeScriptEditorForAssetKey(pairedAssetKey);
    }

    void DrawPrefabAssetInspector(std::string& selectedPrefabAssetKey)
    {
        if (selectedPrefabAssetKey.empty())
            return;

        const std::filesystem::path selectedPath(selectedPrefabAssetKey);
        std::string lowerFileName = selectedPath.filename().string();
        std::transform(lowerFileName.begin(), lowerFileName.end(), lowerFileName.begin(), [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
        if (!lowerFileName.ends_with(".prefab.json"))
        {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Selected asset is not a prefab.");
            if (ImGui::Button("Clear Selection", ImVec2(160.0f, 0.0f)))
                selectedPrefabAssetKey.clear();
            return;
        }

        const auto resolvedPathResult = Assets::ResolveAssetKeyToPath(selectedPrefabAssetKey);
        if (resolvedPathResult.IsFailure())
        {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Could not resolve prefab path.");
            ImGui::TextDisabled("%s", selectedPrefabAssetKey.c_str());
            return;
        }

        const std::filesystem::path resolvedPath = resolvedPathResult.GetValue();
        const auto loadedSceneResult = Scene::LoadFromFile(resolvedPath);
        if (loadedSceneResult.IsFailure() || !loadedSceneResult.GetValue())
        {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Failed to load prefab.");
            ImGui::TextDisabled("%s", selectedPrefabAssetKey.c_str());
            return;
        }

        const Scene& prefabScene = *loadedSceneResult.GetValue();
        const auto& registry = prefabScene.GetRegistry();
        auto tagView = registry.view<TagComponent>();
        const auto rootEntities = prefabScene.GetChildren(entt::null);
        uint32_t entityCount = 0;
        for (entt::entity entity : tagView)
        {
            (void)entity;
            ++entityCount;
        }

        ImGui::Text("Prefab: %s", EditorAssetNaming::GetAssetDisplayNameFromAssetKey(selectedPrefabAssetKey).c_str());
        ImGui::TextDisabled("Asset Key: %s", selectedPrefabAssetKey.c_str());
        ImGui::TextDisabled("Path: %s", resolvedPath.string().c_str());
        ImGui::Separator();

        ImGui::Text("Entities: %u", entityCount);
        ImGui::Text("Root Objects: %u", static_cast<uint32_t>(rootEntities.size()));

        if (!rootEntities.empty())
        {
            ImGui::Spacing();
            ImGui::Text("Root Preview");
            ImGui::BeginChild("PrefabRootPreview", ImVec2(0.0f, 120.0f), true);
            for (entt::entity root : rootEntities)
            {
                const auto* tag = registry.try_get<TagComponent>(root);
                const std::string label = (tag && !tag->Tag.empty()) ? tag->Tag : "Entity";
                ImGui::BulletText("%s", label.c_str());
            }
            ImGui::EndChild();
        }
    }

    void DrawMaterialInspector(const char* texturePayloadId,
                               const char* shaderPayloadId,
                               std::string& selectedMaterialAssetKey,
                               Assets::MaterialAsset::Ptr& cachedMaterialAsset)
    {
        struct State
        {
            std::string LoadedKey;
            std::filesystem::path ResolvedPath;
            nlohmann::json Json = nlohmann::json::object();
            bool Loaded = false;
        };
        static State s_State;

        if (selectedMaterialAssetKey.empty())
            return;

        if (!cachedMaterialAsset || cachedMaterialAsset->GetKey() != selectedMaterialAssetKey)
            cachedMaterialAsset = Assets::AssetManager::LoadBlocking<Assets::MaterialAsset>(selectedMaterialAssetKey);

        if (!s_State.Loaded || s_State.LoadedKey != selectedMaterialAssetKey)
        {
            s_State = {};
            s_State.LoadedKey = selectedMaterialAssetKey;
            s_State.Loaded = LoadMaterialJson(selectedMaterialAssetKey, s_State.Json, s_State.ResolvedPath);
            if (!s_State.Loaded)
            {
                ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Failed to load material JSON: %s", selectedMaterialAssetKey.c_str());
                return;
            }
        }

        const std::string fileName = std::filesystem::path(selectedMaterialAssetKey).filename().string();
        ImGui::Text("Material: %s", fileName.c_str());
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Keep implementation in dedicated module. This function intentionally
        // remains behavior-identical to the previous in-file implementation.
        // The large editor material UI block is unchanged below.
        // -----------------------------------------------------------------------------------
        // NOTE: The body is retained from the original EditorInspectorPanel.cpp implementation.
        // -----------------------------------------------------------------------------------

        // Shader slot (required).
        std::string shaderLabel = "None";
        if (s_State.Json.contains("shader") && s_State.Json["shader"].is_object())
        {
            const auto& ref = s_State.Json["shader"];
            if (ref.contains("key") && ref["key"].is_string())
                shaderLabel = std::filesystem::path(ref["key"].get<std::string>()).filename().string();
        }
        if (cachedMaterialAsset)
        {
            if (auto shaderAsset = cachedMaterialAsset->GetShaderHandle().Lock())
                shaderLabel = std::filesystem::path(shaderAsset->GetKey()).filename().string();
        }

        ImGui::AlignTextToFramePadding();
        ImGui::Text("Shader");
        ImGui::Button((shaderLabel + "##MaterialShader").c_str(),
                      ImVec2(std::max(60.0f, ImGui::GetContentRegionAvail().x - 30.0f), 0));
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(shaderPayloadId))
            {
                const char* key = static_cast<const char*>(payload->Data);
                if (key && key[0])
                {
                    auto shaderAsset = Assets::ShaderAsset::LoadBlocking(key);
                    if (shaderAsset)
                    {
                        s_State.Json["shader"] = {
                            { "guid", shaderAsset->GetGuid() },
                            { "key", shaderAsset->GetKey() }
                        };
                        (void)SaveMaterialJsonAndReload(selectedMaterialAssetKey, cachedMaterialAsset, s_State.Json, s_State.ResolvedPath);
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::SameLine();
        if (ImGui::Button("...##MaterialShaderPicker"))
            ImGui::OpenPopup("MaterialShaderPickerPopup");
        if (ImGui::BeginPopup("MaterialShaderPickerPopup"))
        {
            const std::vector<std::string> shaderKeys = BuildAssetPickerKeysByType(Assets::AssetType::Shader);
            for (const auto& key : shaderKeys)
            {
                const bool isSelected = (shaderLabel == std::filesystem::path(key).filename().string());
                const std::string display = EditorAssetNaming::GetAssetDisplayNameFromAssetKey(key);
                if (ImGui::Selectable((display + "##MaterialShaderPicker_" + key).c_str(), isSelected))
                {
                    auto shaderAsset = Assets::ShaderAsset::LoadBlocking(key);
                    if (shaderAsset)
                    {
                        s_State.Json["shader"] = {
                            { "guid", shaderAsset->GetGuid() },
                            { "key", shaderAsset->GetKey() }
                        };
                        (void)SaveMaterialJsonAndReload(selectedMaterialAssetKey, cachedMaterialAsset, s_State.Json, s_State.ResolvedPath);
                    }
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    ImGui::SetTooltip("%s", key.c_str());
            }
            ImGui::EndPopup();
        }

        struct TextureSlotDescriptor
        {
            const char* Id;
            const char* DisplayName;
            bool IsAlbedo;
        };
        static const std::array<TextureSlotDescriptor, 5> kTextureSlots = {{
            { "albedo", "Albedo (Diffuse)", true },
            { "normal", "Normal", false },
            { "metallic", "Metallic", false },
            { "occlusion", "Occlusion", false },
            { "emission", "Emission", false },
        }};

        auto ensureTextureSlotsRoot = [&]() -> nlohmann::json& {
            if (!s_State.Json.contains("textureSlots") || !s_State.Json["textureSlots"].is_object())
                s_State.Json["textureSlots"] = nlohmann::json::object();
            return s_State.Json["textureSlots"];
        };

        auto getSlotObject = [&](const TextureSlotDescriptor& slot, bool createIfMissing) -> nlohmann::json* {
            if (createIfMissing)
            {
                auto& slotsRoot = ensureTextureSlotsRoot();
                if (!slotsRoot.contains(slot.Id) || !slotsRoot[slot.Id].is_object())
                    slotsRoot[slot.Id] = nlohmann::json::object();
                return &slotsRoot[slot.Id];
            }

            if (!s_State.Json.contains("textureSlots") || !s_State.Json["textureSlots"].is_object())
                return nullptr;
            auto& slotsRoot = s_State.Json["textureSlots"];
            if (!slotsRoot.contains(slot.Id) || !slotsRoot[slot.Id].is_object())
                return nullptr;
            return &slotsRoot[slot.Id];
        };

        auto getTextureRefForSlot = [&](const TextureSlotDescriptor& slot) -> nlohmann::json* {
            if (slot.IsAlbedo)
            {
                if (s_State.Json.contains("mainTexture") && s_State.Json["mainTexture"].is_object())
                    return &s_State.Json["mainTexture"];

                nlohmann::json* slotObject = getSlotObject(slot, false);
                if (slotObject && slotObject->contains("texture") && (*slotObject)["texture"].is_object())
                    return &(*slotObject)["texture"];
                return nullptr;
            }

            nlohmann::json* slotObject = getSlotObject(slot, false);
            if (slotObject && slotObject->contains("texture") && (*slotObject)["texture"].is_object())
                return &(*slotObject)["texture"];
            return nullptr;
        };

        auto getTextureLabelForRef = [](const nlohmann::json* textureRef) -> std::string {
            if (!textureRef || !textureRef->is_object())
                return "None";
            if (textureRef->contains("key") && (*textureRef)["key"].is_string())
            {
                const std::string key = (*textureRef)["key"].get<std::string>();
                return key.empty() ? "None" : std::filesystem::path(key).filename().string();
            }
            if (textureRef->contains("guid") && (*textureRef)["guid"].is_string())
                return std::string("GUID: ") + (*textureRef)["guid"].get<std::string>();
            return "None";
        };

        auto setTextureForSlot = [&](const TextureSlotDescriptor& slot,
                                     Assets::TextureAsset::Ptr textureAsset,
                                     bool hasSubRect = false,
                                     const glm::vec2& subUvMin = glm::vec2(0.0f),
                                     const glm::vec2& subUvMax = glm::vec2(1.0f)) {
            if (!textureAsset)
                return;

            nlohmann::json textureRef = {
                { "guid", textureAsset->GetGuid() },
                { "key", textureAsset->GetKey() }
            };

            if (slot.IsAlbedo)
            {
                // Runtime compatibility: renderer currently consumes mainTexture/mainTextureSpec.
                s_State.Json["mainTexture"] = textureRef;
                if (hasSubRect)
                {
                    s_State.Json["mainTextureSubRect"] = {
                        { "uvMin", { subUvMin.x, subUvMin.y } },
                        { "uvMax", { subUvMax.x, subUvMax.y } }
                    };
                }
                else
                {
                    s_State.Json.erase("mainTextureSubRect");
                }
            }

            nlohmann::json* slotObject = getSlotObject(slot, true);
            (*slotObject)["texture"] = textureRef;

            (void)SaveMaterialJsonAndReload(selectedMaterialAssetKey, cachedMaterialAsset, s_State.Json, s_State.ResolvedPath);
        };

        auto clearTextureForSlot = [&](const TextureSlotDescriptor& slot) {
            if (slot.IsAlbedo)
            {
                s_State.Json.erase("mainTexture");
                s_State.Json.erase("mainTextureSubRect");
            }

            if (nlohmann::json* slotObject = getSlotObject(slot, false))
            {
                slotObject->erase("texture");
                if (slotObject->empty() && s_State.Json.contains("textureSlots") && s_State.Json["textureSlots"].is_object())
                    s_State.Json["textureSlots"].erase(slot.Id);
            }

            (void)SaveMaterialJsonAndReload(selectedMaterialAssetKey, cachedMaterialAsset, s_State.Json, s_State.ResolvedPath);
        };

        auto hasSamplerOverrideForSlot = [&](const TextureSlotDescriptor& slot) -> bool {
            if (slot.IsAlbedo)
                return s_State.Json.contains("mainTextureSpec") && s_State.Json["mainTextureSpec"].is_object();
            nlohmann::json* slotObject = getSlotObject(slot, false);
            return slotObject && slotObject->contains("spec") && (*slotObject)["spec"].is_object();
        };

        auto setSamplerOverrideForSlot = [&](const TextureSlotDescriptor& slot, bool enabled) {
            if (slot.IsAlbedo)
            {
                if (enabled)
                {
                    if (!s_State.Json.contains("mainTextureSpec") || !s_State.Json["mainTextureSpec"].is_object())
                        s_State.Json["mainTextureSpec"] = nlohmann::json::object();
                }
                else
                {
                    s_State.Json.erase("mainTextureSpec");
                }
            }

            nlohmann::json* slotObject = getSlotObject(slot, enabled);
            if (enabled)
            {
                if (!slotObject->contains("spec") || !(*slotObject)["spec"].is_object())
                    (*slotObject)["spec"] = nlohmann::json::object();
            }
            else if (slotObject)
            {
                slotObject->erase("spec");
                if (slotObject->empty() && s_State.Json.contains("textureSlots") && s_State.Json["textureSlots"].is_object())
                    s_State.Json["textureSlots"].erase(slot.Id);
            }

            (void)SaveMaterialJsonAndReload(selectedMaterialAssetKey, cachedMaterialAsset, s_State.Json, s_State.ResolvedPath);
        };

        auto getSpecObjectForSlot = [&](const TextureSlotDescriptor& slot, bool createIfMissing) -> nlohmann::json* {
            if (slot.IsAlbedo)
            {
                if (createIfMissing && (!s_State.Json.contains("mainTextureSpec") || !s_State.Json["mainTextureSpec"].is_object()))
                    s_State.Json["mainTextureSpec"] = nlohmann::json::object();

                if (!s_State.Json.contains("mainTextureSpec") || !s_State.Json["mainTextureSpec"].is_object())
                    return nullptr;

                // Keep Albedo slot mirror in textureSlots so non-runtime UI data stays in one structure too.
                nlohmann::json* slotObject = getSlotObject(slot, true);
                (*slotObject)["spec"] = s_State.Json["mainTextureSpec"];
                return &s_State.Json["mainTextureSpec"];
            }

            nlohmann::json* slotObject = getSlotObject(slot, createIfMissing);
            if (!slotObject)
                return nullptr;
            if (createIfMissing && (!slotObject->contains("spec") || !(*slotObject)["spec"].is_object()))
                (*slotObject)["spec"] = nlohmann::json::object();
            if (!slotObject->contains("spec") || !(*slotObject)["spec"].is_object())
                return nullptr;
            return &(*slotObject)["spec"];
        };

        auto drawSamplerSpecification = [&](const TextureSlotDescriptor& slot) {
            nlohmann::json* spec = getSpecObjectForSlot(slot, true);
            if (!spec)
                return;

            const char* filterNames[] = { "Nearest", "Linear" };
            auto getFilterIndex = [](const char* name) -> int {
                if (!name) return 1;
                return std::string(name) == "Nearest" ? 0 : 1;
            };
            auto getWrapIndex = [](const char* name) -> int {
                if (!name) return 0;
                return std::string(name) == "ClampToEdge" ? 1 : 0;
            };

            int minFilter = getFilterIndex(spec->value("minFilter", "Linear").c_str());
            if (ImGui::Combo("Min Filter", &minFilter, filterNames, 2))
            {
                (*spec)["minFilter"] = (minFilter == 0) ? "Nearest" : "Linear";
                if (slot.IsAlbedo)
                    (*getSlotObject(slot, true))["spec"] = *spec;
                (void)SaveMaterialJsonAndReload(selectedMaterialAssetKey, cachedMaterialAsset, s_State.Json, s_State.ResolvedPath);
            }

            int magFilter = getFilterIndex(spec->value("magFilter", "Linear").c_str());
            if (ImGui::Combo("Mag Filter", &magFilter, filterNames, 2))
            {
                (*spec)["magFilter"] = (magFilter == 0) ? "Nearest" : "Linear";
                if (slot.IsAlbedo)
                    (*getSlotObject(slot, true))["spec"] = *spec;
                (void)SaveMaterialJsonAndReload(selectedMaterialAssetKey, cachedMaterialAsset, s_State.Json, s_State.ResolvedPath);
            }

            const char* wrapNames[] = { "Repeat", "Clamp To Edge" };
            int wrapU = getWrapIndex(spec->value("wrapU", "Repeat").c_str());
            if (ImGui::Combo("Wrap U", &wrapU, wrapNames, 2))
            {
                (*spec)["wrapU"] = (wrapU == 1) ? "ClampToEdge" : "Repeat";
                if (slot.IsAlbedo)
                    (*getSlotObject(slot, true))["spec"] = *spec;
                (void)SaveMaterialJsonAndReload(selectedMaterialAssetKey, cachedMaterialAsset, s_State.Json, s_State.ResolvedPath);
            }

            int wrapV = getWrapIndex(spec->value("wrapV", "Repeat").c_str());
            if (ImGui::Combo("Wrap V", &wrapV, wrapNames, 2))
            {
                (*spec)["wrapV"] = (wrapV == 1) ? "ClampToEdge" : "Repeat";
                if (slot.IsAlbedo)
                    (*getSlotObject(slot, true))["spec"] = *spec;
                (void)SaveMaterialJsonAndReload(selectedMaterialAssetKey, cachedMaterialAsset, s_State.Json, s_State.ResolvedPath);
            }

            bool generateMipmaps = spec->value("generateMipmaps", false);
            if (ImGui::Checkbox("Generate Mipmaps", &generateMipmaps))
            {
                (*spec)["generateMipmaps"] = generateMipmaps;
                if (slot.IsAlbedo)
                    (*getSlotObject(slot, true))["spec"] = *spec;
                (void)SaveMaterialJsonAndReload(selectedMaterialAssetKey, cachedMaterialAsset, s_State.Json, s_State.ResolvedPath);
            }
        };

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextUnformatted("Surface Inputs");

        for (const auto& slot : kTextureSlots)
        {
            ImGui::PushID(slot.Id);

            std::string textureLabel = "None";
            if (slot.IsAlbedo && cachedMaterialAsset)
            {
                if (auto texAsset = cachedMaterialAsset->GetMainTextureHandle().Lock())
                    textureLabel = std::filesystem::path(texAsset->GetKey()).filename().string();
            }
            if (textureLabel == "None")
                textureLabel = getTextureLabelForRef(getTextureRefForSlot(slot));
            if (slot.IsAlbedo &&
                s_State.Json.contains("mainTextureSubRect") &&
                s_State.Json["mainTextureSubRect"].is_object() &&
                textureLabel != "None")
            {
                textureLabel += " (Sub)";
            }

            ImGuiTreeNodeFlags slotFlags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
            const bool slotOpen = ImGui::TreeNodeEx(slot.DisplayName, slotFlags);

            ImGui::Button((textureLabel + "##Texture").c_str(),
                          ImVec2(std::max(60.0f, ImGui::GetContentRegionAvail().x - 90.0f), 0.0f));
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kSubSpritePayloadId))
                {
                    const char* key = static_cast<const char*>(payload->Data);
                    if (key && key[0])
                    {
                        ResolvedTextureDrop drop;
                        if (!TryResolveTextureDrop(key, drop))
                            drop.TextureKey = ResolveTextureKeyFromDroppedKey(key);
                        auto textureAsset = Assets::AssetManager::LoadBlocking<Assets::TextureAsset>(drop.TextureKey);
                        if (textureAsset)
                            setTextureForSlot(slot, textureAsset, slot.IsAlbedo && drop.HasSubRect, drop.UvMin, drop.UvMax);
                    }
                }
                else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(texturePayloadId))
                {
                    const char* key = static_cast<const char*>(payload->Data);
                    if (key && key[0])
                    {
                        ResolvedTextureDrop drop;
                        if (!TryResolveTextureDrop(key, drop))
                            drop.TextureKey = ResolveTextureKeyFromDroppedKey(key);
                        auto textureAsset = Assets::AssetManager::LoadBlocking<Assets::TextureAsset>(drop.TextureKey);
                        if (textureAsset)
                            setTextureForSlot(slot, textureAsset, slot.IsAlbedo && drop.HasSubRect, drop.UvMin, drop.UvMax);
                    }
                }
                else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_MULTI_KEYS"))
                {
                    const std::vector<std::string> droppedKeys = ParseAssetKeyListPayload(payload);
                    for (const auto& key : droppedKeys)
                    {
                        ResolvedTextureDrop drop;
                        if (!TryResolveTextureDrop(key, drop))
                            drop.TextureKey = ResolveTextureKeyFromDroppedKey(key);
                        auto textureAsset = Assets::AssetManager::LoadBlocking<Assets::TextureAsset>(drop.TextureKey);
                        if (textureAsset)
                        {
                            setTextureForSlot(slot, textureAsset, slot.IsAlbedo && drop.HasSubRect, drop.UvMin, drop.UvMax);
                            break;
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::SameLine();
            if (ImGui::Button("...##TexturePicker"))
                ImGui::OpenPopup("TexturePickerPopup");
            if (ImGui::BeginPopup("TexturePickerPopup"))
            {
                const std::vector<std::string> textureKeys = BuildAssetPickerKeysByType(Assets::AssetType::Texture2D);
                for (const auto& key : textureKeys)
                {
                    const std::string display = EditorAssetNaming::GetAssetDisplayNameFromAssetKey(key);
                    if (ImGui::Selectable((display + "##TexturePicker_" + key).c_str(), textureLabel == std::filesystem::path(key).filename().string()))
                    {
                        auto textureAsset = Assets::AssetManager::LoadBlocking<Assets::TextureAsset>(key);
                        if (textureAsset)
                            setTextureForSlot(slot, textureAsset, false);
                        ImGui::CloseCurrentPopup();
                    }
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                        ImGui::SetTooltip("%s", key.c_str());
                }
                ImGui::EndPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("X##ClearTextureSlot"))
                clearTextureForSlot(slot);

            if (slotOpen)
            {
                bool hasSamplerOverride = hasSamplerOverrideForSlot(slot);
                if (ImGui::Checkbox("Override Sampler", &hasSamplerOverride))
                    setSamplerOverrideForSlot(slot, hasSamplerOverride);

                if (hasSamplerOverride)
                    drawSamplerSpecification(slot);

                ImGui::TreePop();
            }

            ImGui::PopID();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextUnformatted("Lighting Response");

        float normalStrength = s_State.Json.value("normalStrength", 1.0f);
        if (ImGui::DragFloat("Normal Strength", &normalStrength, 0.01f, 0.0f, 8.0f, "%.2f"))
        {
            s_State.Json["normalStrength"] = std::clamp(normalStrength, 0.0f, 8.0f);
            (void)SaveMaterialJsonAndReload(selectedMaterialAssetKey, cachedMaterialAsset, s_State.Json, s_State.ResolvedPath);
        }

        float roughness = s_State.Json.value("roughness", 0.5f);
        if (ImGui::SliderFloat("Roughness", &roughness, 0.0f, 1.0f, "%.2f"))
        {
            s_State.Json["roughness"] = std::clamp(roughness, 0.0f, 1.0f);
            (void)SaveMaterialJsonAndReload(selectedMaterialAssetKey, cachedMaterialAsset, s_State.Json, s_State.ResolvedPath);
        }

        float specularIntensity = s_State.Json.value("specularIntensity", s_State.Json.value("specular", 0.5f));
        if (ImGui::DragFloat("Specular Intensity", &specularIntensity, 0.01f, 0.0f, 8.0f, "%.2f"))
        {
            s_State.Json["specularIntensity"] = std::clamp(specularIntensity, 0.0f, 8.0f);
            (void)SaveMaterialJsonAndReload(selectedMaterialAssetKey, cachedMaterialAsset, s_State.Json, s_State.ResolvedPath);
        }
    }

    void DrawAnimationClipAssetInspector(std::string& selectedAnimationClipAssetKey)
    {
        if (selectedAnimationClipAssetKey.empty())
            return;

        auto clipAsset = Assets::AnimationClipAsset::LoadBlocking(selectedAnimationClipAssetKey);
        if (!clipAsset)
        {
            ImGui::TextColored(
                ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                "Failed to load animation clip asset: %s",
                selectedAnimationClipAssetKey.c_str());
            return;
        }

        const auto& clipData = clipAsset->GetData();
        ImGui::Text("Animation Clip: %s", std::filesystem::path(selectedAnimationClipAssetKey).filename().string().c_str());
        ImGui::TextDisabled("Asset Key: %s", selectedAnimationClipAssetKey.c_str());
        ImGui::Separator();

        ImGui::Text("Name: %s", clipData.Name.empty() ? "<unnamed>" : clipData.Name.c_str());
        ImGui::Text("Duration: %.3f sec", clipData.DurationSeconds);
        ImGui::Text("Samples Per Second: %.1f", clipData.SamplesPerSecond);
        ImGui::Text("Loop: %s", clipData.Loop ? "Yes" : "No");
        ImGui::Separator();
        ImGui::Text("Tracks");
        ImGui::BulletText("Sprite Sub-Rect Keys: %u", static_cast<uint32_t>(clipData.SpriteSubRectTrack.size()));
        ImGui::BulletText("Sprite Texture Keys: %u", static_cast<uint32_t>(clipData.SpriteTextureTrack.size()));
        ImGui::BulletText("Position Keys: %u", static_cast<uint32_t>(clipData.PositionTrack.size()));
        ImGui::BulletText("Scale Keys: %u", static_cast<uint32_t>(clipData.ScaleTrack.size()));
        ImGui::BulletText("Rotation Z Keys: %u", static_cast<uint32_t>(clipData.RotationZTrack.size()));
        ImGui::BulletText("Events: %u", static_cast<uint32_t>(clipData.EventTrack.size()));
        ImGui::Spacing();
        ImGui::TextDisabled("Edit detailed tracks in the Animation Timeline panel.");
    }

    void DrawAnimatorControllerAssetInspector(std::string& selectedAnimatorControllerAssetKey)
    {
        if (selectedAnimatorControllerAssetKey.empty())
            return;

        auto controllerAsset = Assets::AnimatorControllerAsset::LoadBlocking(selectedAnimatorControllerAssetKey);
        if (!controllerAsset)
        {
            ImGui::TextColored(
                ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                "Failed to load animator controller asset: %s",
                selectedAnimatorControllerAssetKey.c_str());
            return;
        }

        const auto& controllerData = controllerAsset->GetData();
        ImGui::Text("Animator Controller: %s", std::filesystem::path(selectedAnimatorControllerAssetKey).filename().string().c_str());
        ImGui::TextDisabled("Asset Key: %s", selectedAnimatorControllerAssetKey.c_str());
        ImGui::Separator();

        ImGui::Text("Name: %s", controllerData.Name.empty() ? "<unnamed>" : controllerData.Name.c_str());
        ImGui::Text("Default State: %s", controllerData.DefaultStateName.empty() ? "<none>" : controllerData.DefaultStateName.c_str());
        ImGui::Text("Parameters: %u", static_cast<uint32_t>(controllerData.Parameters.size()));
        ImGui::Text("States: %u", static_cast<uint32_t>(controllerData.States.size()));

        uint32_t transitionCount = 0;
        for (const auto& state : controllerData.States)
            transitionCount += static_cast<uint32_t>(state.Transitions.size());
        ImGui::Text("Transitions: %u", transitionCount);
        ImGui::Spacing();
        ImGui::TextDisabled("Edit states, transitions, and conditions in the Animator Graph panel.");
    }
}
