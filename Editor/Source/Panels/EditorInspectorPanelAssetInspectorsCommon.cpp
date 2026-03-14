#include "EditorInspectorPanelAssetInspectorsShared.h"

namespace Limitless::EditorInspectorPanel::Internal
{
    namespace
    {
        std::string s_PendingSpriteEditorTextureKey;
    }

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

    const char* GetScriptPropertyTypeLabel(ScriptPropertyType type)
    {
        switch (type)
        {
            case ScriptPropertyType::Float:
                return "Float";
            case ScriptPropertyType::Integer:
                return "Integer";
            case ScriptPropertyType::Boolean:
                return "Boolean";
            case ScriptPropertyType::Vector3:
                return "Vector3";
            case ScriptPropertyType::String:
                return "String";
            case ScriptPropertyType::Entity:
                return "Entity";
            case ScriptPropertyType::Prefab:
                return "Prefab";
            case ScriptPropertyType::Vector2:
                return "Vector2";
            case ScriptPropertyType::Vector4:
                return "Vector4";
            case ScriptPropertyType::Enum:
                return "Enum";
        }

        return "Unknown";
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

        auto tryAddKnownDefault = [&](std::string_view key) {
            if (key.empty())
                return;
            const std::string keyText(key);
            if (seen.contains(keyText))
                return;
            const auto resolved = Assets::ResolveAssetKeyToPath(keyText);
            if (resolved.IsFailure())
                return;
            std::error_code ec;
            if (std::filesystem::exists(resolved.GetValue(), ec))
            {
                seen.insert(keyText);
                keys.emplace_back(keyText);
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
                                   EditorAssetPreview::MaterialPreviewCache& materialPreviewCache,
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

        EditorAssetPreview::InvalidateCachedMaterialPreview(materialPreviewCache, materialKey);

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

    const std::string& GetPendingSpriteEditorRequestState()
    {
        return s_PendingSpriteEditorTextureKey;
    }

    void SetPendingSpriteEditorRequestState(const std::string& textureAssetKey)
    {
        s_PendingSpriteEditorTextureKey = textureAssetKey;
    }

    void ClearPendingSpriteEditorRequestState()
    {
        s_PendingSpriteEditorTextureKey.clear();
    }
}
