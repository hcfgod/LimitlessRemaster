#pragma once

#include "EditorInspectorPanelAssetInspectors.h"

#include "EditorAssetPreview.h"
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
#include "Graphics/NativeRenderHandles.h"
#include "Graphics/Renderer.h"
#include "Scene/Scene.h"
#include "Scripting/ManagedScriptHost.h"
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
#include <string_view>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

namespace Limitless::EditorInspectorPanel::Internal
{
    inline constexpr const char* kSubSpritePayloadId = "SUB_SPRITE_KEY";
    inline constexpr std::array<const char*, 3> kInputActionValueTypes = {
        "Button",
        "Axis1D",
        "Axis2D"
    };
    inline constexpr std::array<const char*, 8> kInputBindingTypes = {
        "KeyboardButton",
        "MouseButton",
        "KeyboardAxis1D",
        "KeyboardAxis2D",
        "MouseDelta",
        "GamepadButton",
        "GamepadAxis1D",
        "GamepadAxis2D"
    };

    struct ResolvedTextureDrop
    {
        std::string TextureKey;
        bool HasSubRect = false;
        glm::vec2 UvMin = glm::vec2(0.0f);
        glm::vec2 UvMax = glm::vec2(1.0f);
    };

    std::string ResolveTextureKeyFromDroppedKey(const std::string& droppedKey);
    std::vector<std::string> ParseAssetKeyListPayload(const ImGuiPayload* payload);
    bool TryResolveTextureDrop(const std::string& droppedKey, ResolvedTextureDrop& outDrop);
    const char* GetScriptPropertyTypeLabel(ScriptPropertyType type);
    std::vector<std::string> BuildAssetPickerKeysByType(Assets::AssetType assetType);
    void InvalidateSpriteCachesForTexture(Scene* scene, const std::string& textureKey);
    void ApplyTextureSpecificationAndPersist(Scene* scene,
                                            Assets::TextureAsset::Ptr textureAsset,
                                            const TextureSpecification& specification);
    void PersistTextureSpecificationAndReload(Scene* scene,
                                              Assets::TextureAsset::Ptr textureAsset,
                                              const TextureSpecification& specification);
    bool LoadTilesetJson(const std::string& tilesetKey,
                         nlohmann::json& outJson,
                         std::filesystem::path& outResolvedPath);
    bool SaveTilesetJson(Scene* scene,
                         const std::string& selectedTilesetAssetKey,
                         const nlohmann::json& json,
                         const std::filesystem::path& resolvedPath);
    bool LoadMaterialJson(const std::string& materialKey,
                          nlohmann::json& outJson,
                          std::filesystem::path& outResolvedPath);
    bool SaveMaterialJsonAndReload(const std::string& materialKey,
                                   EditorAssetPreview::MaterialPreviewCache& materialPreviewCache,
                                   Assets::MaterialAsset::Ptr& cachedMaterialAsset,
                                   const nlohmann::json& jsonToSave,
                                   const std::filesystem::path& resolvedPath);
    bool LoadInputActionsJson(const std::string& assetKey,
                              nlohmann::json& outJson,
                              std::filesystem::path& outResolvedPath);
    bool SaveInputActionsJsonAndReload(const std::string& assetKey,
                                       const nlohmann::json& jsonToSave,
                                       const std::filesystem::path& resolvedPath);
    std::string GetScancodeDisplayName(int scancode);
    int ReadScancodeValue(const nlohmann::json& binding, const char* scancodeKey, const char* nameKey);
    void WriteScancodeValue(nlohmann::json& binding, const char* scancodeKey, const char* nameKey, int scancodeValue);
    std::string GetGamepadButtonDisplayName(int buttonId);
    int ReadGamepadButtonValue(const nlohmann::json& binding, const char* buttonIdKey, const char* buttonNameKey);
    void WriteGamepadButtonValue(nlohmann::json& binding, const char* buttonIdKey, const char* buttonNameKey, int buttonId);
    std::string GetGamepadAxisDisplayName(int axisId);
    int ReadGamepadAxisValue(const nlohmann::json& binding, const char* axisIdKey, const char* axisNameKey);
    void WriteGamepadAxisValue(nlohmann::json& binding, const char* axisIdKey, const char* axisNameKey, int axisId);
    nlohmann::json CreateDefaultBindingJson(const std::string& bindingType);
    std::string GetDefaultBindingTypeForActionType(const std::string& actionType);
    bool DrawInputBindingEditor(nlohmann::json& bindingJson);

    const std::string& GetPendingSpriteEditorRequestState();
    void SetPendingSpriteEditorRequestState(const std::string& textureAssetKey);
    void ClearPendingSpriteEditorRequestState();

    void DrawTextureInspectorInternal(Scene* scene,
                                      std::string& selectedTextureAssetKey,
                                      Assets::TextureAsset::Ptr& cachedTextureAsset);
    void DrawTilesetAssetInspectorInternal(Scene* scene, std::string& selectedTilesetAssetKey);
    void DrawInputActionsAssetInspectorInternal(std::string& selectedInputActionsAssetKey);
    void DrawAudioMixerAssetInspectorInternal(std::string& selectedAudioMixerAssetKey);
    void DrawMaterialInspectorInternal(const char* texturePayloadId,
                                       const char* shaderPayloadId,
                                       EditorAssetPreview::MaterialPreviewCache& materialPreviewCache,
                                       std::string& selectedMaterialAssetKey,
                                       Assets::MaterialAsset::Ptr& cachedMaterialAsset);
    void DrawNativeScriptAssetInspectorInternal(std::string& selectedNativeScriptAssetKey);
    void DrawPrefabAssetInspectorInternal(std::string& selectedPrefabAssetKey);
    void DrawAnimationClipAssetInspectorInternal(std::string& selectedAnimationClipAssetKey);
    void DrawAnimatorControllerAssetInspectorInternal(std::string& selectedAnimatorControllerAssetKey);
}
