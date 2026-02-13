#include "EditorInspectorPanel.h"

#include "EditorAssetNaming.h"
#include "Audio/AudioEngine.h"
#include "Assets/AudioClipAsset.h"
#include "Assets/AssetDatabase.h"
#include "Assets/AssetManager.h"
#include "Assets/AssetPaths.h"
#include "Assets/AssetTypes.h"
#include "Assets/MaterialAssetImporter.h"
#include "Assets/ShaderAssetImporter.h"
#include "Assets/ShaderAsset.h"
#include "Assets/TextureAssetImporter.h"
#include "Graphics/Renderer.h"
#include "Scene/Scene.h"
#include "imgui/imgui.h"

#include <array>
#include <fstream>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace Limitless::EditorInspectorPanel
{
    namespace
    {
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

        void DrawTextureInspector(Scene* scene,
                                  std::string& selectedTextureAssetKey,
                                  Assets::TextureAsset::Ptr& cachedTextureAsset)
        {
            if (!cachedTextureAsset || cachedTextureAsset->GetKey() != selectedTextureAssetKey)
                cachedTextureAsset = Assets::AssetManager::LoadBlocking<Assets::TextureAsset>(selectedTextureAssetKey);

            auto textureAsset = cachedTextureAsset;
            if (!textureAsset)
            {
                ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Failed to load texture: %s", selectedTextureAssetKey.c_str());
                cachedTextureAsset.reset();
                return;
            }

            const auto* texture = textureAsset->GetTexture().get();
            if (!texture)
            {
                ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Texture not ready.");
                return;
            }

            const std::string fileName = std::filesystem::path(selectedTextureAssetKey).filename().string();
            ImGui::Text("Texture: %s", fileName.c_str());
            ImGui::Text("%u x %u", texture->GetWidth(), texture->GetHeight());
            ImGui::Spacing();

            const float previewSize = 256.0f;
            const float aspect = static_cast<float>(texture->GetHeight()) / static_cast<float>(texture->GetWidth());
            const ImVec2 uv0(0.0f, 1.0f);
            const ImVec2 uv1(1.0f, 0.0f);
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
            ImGui::SameLine(160);
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
            ImGui::SameLine(160);
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
            ImGui::SameLine(80);
            ImGui::Button((shaderLabel + "##MaterialShader").c_str(), ImVec2(ImGui::GetContentRegionAvail().x, 0));
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(shaderPayloadId))
                {
                    const char* key = static_cast<const char*>(payload->Data);
                    if (key && key[0])
                    {
                        auto shaderAsset = Assets::AssetManager::LoadBlocking<Assets::ShaderAsset>(key);
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

            auto setTextureForSlot = [&](const TextureSlotDescriptor& slot, Assets::TextureAsset::Ptr textureAsset) {
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
                }

                nlohmann::json* slotObject = getSlotObject(slot, true);
                (*slotObject)["texture"] = textureRef;

                (void)SaveMaterialJsonAndReload(selectedMaterialAssetKey, cachedMaterialAsset, s_State.Json, s_State.ResolvedPath);
            };

            auto clearTextureForSlot = [&](const TextureSlotDescriptor& slot) {
                if (slot.IsAlbedo)
                    s_State.Json.erase("mainTexture");

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

                ImGuiTreeNodeFlags slotFlags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
                const bool slotOpen = ImGui::TreeNodeEx(slot.DisplayName, slotFlags);

                ImGui::SameLine(220.0f);
                ImGui::Button((textureLabel + "##Texture").c_str(), ImVec2(ImGui::GetContentRegionAvail().x - 60.0f, 0.0f));
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(texturePayloadId))
                    {
                        const char* key = static_cast<const char*>(payload->Data);
                        if (key && key[0])
                        {
                            auto textureAsset = Assets::AssetManager::LoadBlocking<Assets::TextureAsset>(key);
                            if (textureAsset)
                                setTextureForSlot(slot, textureAsset);
                        }
                    }
                    ImGui::EndDragDropTarget();
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
        }

        void ClearPrimaryFlagFromOtherCameras(entt::registry& registry, entt::entity currentEntity)
        {
            auto view = registry.view<CameraComponent>();
            for (entt::entity entity : view)
            {
                if (entity == currentEntity)
                    continue;

                auto& otherCamera = view.get<CameraComponent>(entity);
                otherCamera.IsPrimary = false;
            }
        }
    }

    void Draw(Scene* scene,
              entt::entity selectedEntity,
              const char* texturePayloadId,
              std::string& selectedTextureAssetKey,
              Assets::TextureAsset::Ptr& cachedTextureAsset,
              const char* audioPayloadId,
              const char* materialPayloadId,
              const char* shaderPayloadId,
              std::string& selectedMaterialAssetKey,
              Assets::MaterialAsset::Ptr& cachedMaterialAsset)
    {
        ImGui::Begin("Inspector");

        if (!selectedMaterialAssetKey.empty())
        {
            DrawMaterialInspector(texturePayloadId, shaderPayloadId, selectedMaterialAssetKey, cachedMaterialAsset);
        }
        else if (!selectedTextureAssetKey.empty())
        {
            DrawTextureInspector(scene, selectedTextureAssetKey, cachedTextureAsset);
        }
        else if (!scene || selectedEntity == entt::null || !scene->IsValid(selectedEntity))
        {
            ImGui::Text("Select an object to edit.");
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Text("No selection.");
        }
        else
        {
            auto& registry = scene->GetRegistry();
            bool removeSpriteComponent = false;
            bool removeCameraComponent = false;
            bool removeMaterialComponent = false;
            bool removeAudioSourceComponent = false;
            if (auto* tag = registry.try_get<TagComponent>(selectedEntity))
            {
                static entt::entity renameEntity = entt::null;
                static std::array<char, 256> renameBuffer{};
                if (renameEntity != selectedEntity)
                {
                    renameEntity = selectedEntity;
                    std::snprintf(renameBuffer.data(), renameBuffer.size(), "%s", tag->Tag.c_str());
                }

                ImGui::AlignTextToFramePadding();
                ImGui::Text("Name");
                ImGui::SameLine(80);
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputText("##EntityName", renameBuffer.data(), renameBuffer.size());
                if (ImGui::IsItemDeactivatedAfterEdit())
                    tag->Tag = renameBuffer.data();
            }

            ImGui::Spacing();
            ImGui::Separator();

            if (auto* transform = registry.try_get<TransformComponent>(selectedEntity))
            {
                const bool transformOpen = ImGui::TreeNodeEx("Transform", ImGuiTreeNodeFlags_DefaultOpen);
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                    ImGui::OpenPopup("TransformComponentOptions");
                ImGui::SameLine();
                if (ImGui::Button("...##TransformComponentOptionsButton"))
                    ImGui::OpenPopup("TransformComponentOptions");

                if (ImGui::BeginPopup("TransformComponentOptions"))
                {
                    ImGui::BeginDisabled();
                    ImGui::MenuItem("Remove Component");
                    ImGui::EndDisabled();
                    ImGui::Separator();
                    ImGui::TextDisabled("Transform cannot be removed.");
                    ImGui::EndPopup();
                }

                if (transformOpen)
                {
                    ImGui::DragFloat3("Position", &transform->Position.x, 0.1f);
                    ImGui::DragFloat3("Rotation", &transform->Rotation.x, 1.0f);
                    ImGui::DragFloat3("Scale", &transform->Scale.x, 0.1f);
                    ImGui::TreePop();
                }
            }

            if (auto* sprite = registry.try_get<SpriteComponent>(selectedEntity))
            {
                const bool spriteOpen = ImGui::TreeNodeEx("Sprite", ImGuiTreeNodeFlags_DefaultOpen);
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                    ImGui::OpenPopup("SpriteComponentOptions");
                ImGui::SameLine();
                if (ImGui::Button("...##SpriteComponentOptionsButton"))
                    ImGui::OpenPopup("SpriteComponentOptions");

                if (ImGui::BeginPopup("SpriteComponentOptions"))
                {
                    if (ImGui::MenuItem("Remove Component"))
                        removeSpriteComponent = true;
                    ImGui::EndPopup();
                }

                if (spriteOpen)
                {
                    ImGui::ColorEdit4("Color", &sprite->Color.r);

                    // Material slot (Unity-style): dropping a material assigns it to the renderer.
                    auto* material = registry.try_get<MaterialComponent>(selectedEntity);
                    const std::string materialLabel = (material && !material->MaterialKey.empty())
                        ? EditorAssetNaming::GetAssetDisplayNameFromAssetKey(material->MaterialKey)
                        : std::string("None");
                    ImGui::Text("Material");
                    ImGui::SameLine(80);
                    ImGui::Button((materialLabel + "##SpriteMaterial").c_str(), ImVec2(ImGui::GetContentRegionAvail().x - 60, 0));

                    if (ImGui::BeginDragDropTarget())
                    {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(materialPayloadId))
                        {
                            const char* key = static_cast<const char*>(payload->Data);
                            if (key && key[0])
                            {
                                if (!material)
                                    material = &registry.emplace<MaterialComponent>(selectedEntity);
                                material->MaterialKey = key;
                                material->CachedMaterial.reset();
                                material->MaterialLoadAttempted = false;
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }

                    if (material && !material->MaterialKey.empty())
                    {
                        ImGui::SameLine();
                        if (ImGui::Button("Clear##Material"))
                        {
                            material->MaterialKey.clear();
                            material->CachedMaterial.reset();
                            material->MaterialLoadAttempted = false;
                        }
                    }

                    ImGui::TreePop();
                }
            }

            if (auto* camera = registry.try_get<CameraComponent>(selectedEntity))
            {
                const bool cameraOpen = ImGui::TreeNodeEx("Camera", ImGuiTreeNodeFlags_DefaultOpen);
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                    ImGui::OpenPopup("CameraComponentOptions");
                ImGui::SameLine();
                if (ImGui::Button("...##CameraComponentOptionsButton"))
                    ImGui::OpenPopup("CameraComponentOptions");

                if (ImGui::BeginPopup("CameraComponentOptions"))
                {
                    if (ImGui::MenuItem("Remove Component"))
                        removeCameraComponent = true;
                    ImGui::EndPopup();
                }

                if (cameraOpen)
                {
                    int projectionIndex = static_cast<int>(camera->Projection);
                    const char* projectionOptions[] = { "Orthographic 2D", "Perspective 3D" };
                    const CameraComponent::ProjectionType previousProjection = camera->Projection;
                    if (ImGui::Combo("Projection", &projectionIndex, projectionOptions, 2))
                    {
                        camera->Projection = static_cast<CameraComponent::ProjectionType>(projectionIndex);
                        if (previousProjection != camera->Projection)
                        {
                            if (camera->Projection == CameraComponent::ProjectionType::Perspective3D)
                            {
                                // Switching from ortho to perspective should use perspective-safe clip defaults.
                                camera->NearPlane = 0.1f;
                                camera->FarPlane = 1000.0f;
                            }
                            else
                            {
                                // Switching from perspective to ortho uses the classic 2D clip volume.
                                camera->NearPlane = -1.0f;
                                camera->FarPlane = 1.0f;
                            }
                        }
                    }

                    if (camera->Projection == CameraComponent::ProjectionType::Orthographic2D)
                    {
                        if (camera->NearPlane >= camera->FarPlane)
                            camera->FarPlane = camera->NearPlane + 2.0f;
                        ImGui::DragFloat("Zoom", &camera->Zoom, 0.05f, 0.01f, 100.0f);
                        ImGui::DragFloat("Near Plane", &camera->NearPlane, 0.01f);
                        ImGui::DragFloat("Far Plane", &camera->FarPlane, 0.01f);
                    }
                    else
                    {
                        if (camera->NearPlane <= 0.0f)
                            camera->NearPlane = 0.01f;
                        if (camera->FarPlane <= camera->NearPlane)
                            camera->FarPlane = camera->NearPlane + 1000.0f;
                        ImGui::DragFloat("Field Of View", &camera->FieldOfViewYDegrees, 0.1f, 1.0f, 179.0f);
                        ImGui::DragFloat("Near Plane", &camera->NearPlane, 0.01f, 0.001f, 1000.0f);
                        ImGui::DragFloat("Far Plane", &camera->FarPlane, 1.0f, 0.01f, 100000.0f);
                    }

                    bool isPrimary = camera->IsPrimary;
                    if (ImGui::Checkbox("Primary", &isPrimary))
                    {
                        camera->IsPrimary = isPrimary;
                        if (camera->IsPrimary)
                            ClearPrimaryFlagFromOtherCameras(registry, selectedEntity);
                    }

                    ImGui::TreePop();
                }
            }

            if (auto* audioSource = registry.try_get<AudioSourceComponent>(selectedEntity))
            {
                const bool audioOpen = ImGui::TreeNodeEx("Audio Source", ImGuiTreeNodeFlags_DefaultOpen);
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                    ImGui::OpenPopup("AudioSourceComponentOptions");
                ImGui::SameLine();
                if (ImGui::Button("...##AudioSourceComponentOptionsButton"))
                    ImGui::OpenPopup("AudioSourceComponentOptions");

                if (ImGui::BeginPopup("AudioSourceComponentOptions"))
                {
                    if (ImGui::MenuItem("Remove Component"))
                        removeAudioSourceComponent = true;
                    ImGui::EndPopup();
                }

                if (audioOpen)
                {
                    const std::string clipLabel = audioSource->AudioClipKey.empty()
                        ? std::string("None")
                        : EditorAssetNaming::GetAssetDisplayNameFromAssetKey(audioSource->AudioClipKey);

                    ImGui::Text("Clip");
                    ImGui::SameLine(80);
                    ImGui::Button((clipLabel + "##AudioClip").c_str(), ImVec2(ImGui::GetContentRegionAvail().x - 60.0f, 0.0f));
                    if (ImGui::BeginDragDropTarget())
                    {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(audioPayloadId))
                        {
                            const char* key = static_cast<const char*>(payload->Data);
                            if (key && key[0])
                            {
                                audioSource->AudioClipKey = key;
                                audioSource->RuntimePlaybackStarted = false;
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }

                    if (!audioSource->AudioClipKey.empty())
                    {
                        ImGui::SameLine();
                        if (ImGui::Button("Clear##AudioClip"))
                        {
                            if (audioSource->RuntimeVoiceId != 0)
                                Audio::AudioEngine::GetInstance().Stop(audioSource->RuntimeVoiceId);
                            audioSource->AudioClipKey.clear();
                            audioSource->RuntimeVoiceId = 0;
                            audioSource->RuntimePlaybackStarted = false;
                        }
                    }

                    ImGui::Checkbox("Play On Start", &audioSource->PlayOnStart);
                    ImGui::Checkbox("Loop", &audioSource->Loop);
                    ImGui::Checkbox("Muted", &audioSource->Muted);
                    ImGui::SliderFloat("Volume", &audioSource->Volume, 0.0f, 2.0f, "%.2f");

                    if (audioSource->RuntimeVoiceId != 0 &&
                        !Audio::AudioEngine::GetInstance().IsVoiceActive(audioSource->RuntimeVoiceId))
                    {
                        audioSource->RuntimeVoiceId = 0;
                        audioSource->RuntimePlaybackStarted = false;
                    }
                    const bool isPlaying = (audioSource->RuntimeVoiceId != 0);
                    if (isPlaying)
                    {
                        if (ImGui::Button("Stop##AudioSourcePreview", ImVec2(120, 0)))
                        {
                            Audio::AudioEngine::GetInstance().Stop(audioSource->RuntimeVoiceId);
                            audioSource->RuntimeVoiceId = 0;
                            audioSource->RuntimePlaybackStarted = false;
                        }
                    }
                    else
                    {
                        if (ImGui::Button("Play##AudioSourcePreview", ImVec2(120, 0)))
                        {
                            if (!audioSource->AudioClipKey.empty())
                            {
                                auto clipAsset = Assets::AudioClipAsset::LoadBlocking(audioSource->AudioClipKey);
                                if (clipAsset && clipAsset->GetClip())
                                {
                                    const float volume = audioSource->Muted ? 0.0f : audioSource->Volume;
                                    audioSource->RuntimeVoiceId = Audio::AudioEngine::GetInstance().PlayClip(
                                        clipAsset->GetClip(),
                                        volume,
                                        audioSource->Loop);
                                    audioSource->RuntimePlaybackStarted = (audioSource->RuntimeVoiceId != 0);
                                }
                            }
                        }
                    }

                    ImGui::TreePop();
                }
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::Button("Add Component", ImVec2(-1.0f, 0.0f)))
                ImGui::OpenPopup("AddComponentPopup");

            if (ImGui::BeginPopup("AddComponentPopup"))
            {
                const bool hasSpriteComponent = registry.all_of<SpriteComponent>(selectedEntity);
                const bool hasCameraComponent = registry.all_of<CameraComponent>(selectedEntity);
                const bool hasAudioSourceComponent = registry.all_of<AudioSourceComponent>(selectedEntity);
                if (hasSpriteComponent)
                    ImGui::BeginDisabled();

                if (ImGui::MenuItem("Sprite Component"))
                    registry.emplace<SpriteComponent>(selectedEntity);

                if (hasSpriteComponent)
                    ImGui::EndDisabled();

                if (hasCameraComponent)
                    ImGui::BeginDisabled();

                if (ImGui::MenuItem("Camera Component"))
                {
                    auto& camera = registry.emplace<CameraComponent>(selectedEntity);
                    camera.IsPrimary = true;
                    ClearPrimaryFlagFromOtherCameras(registry, selectedEntity);
                }

                if (hasCameraComponent)
                    ImGui::EndDisabled();

                if (hasAudioSourceComponent)
                    ImGui::BeginDisabled();

                if (ImGui::MenuItem("Audio Source"))
                    registry.emplace<AudioSourceComponent>(selectedEntity);

                if (hasAudioSourceComponent)
                    ImGui::EndDisabled();

                ImGui::EndPopup();
            }

            if (removeSpriteComponent)
            {
                registry.remove<SpriteComponent>(selectedEntity);
                // Material is currently only consumed by sprite rendering; remove it with sprite
                // to keep components aligned with what the renderer expects.
                if (registry.all_of<MaterialComponent>(selectedEntity))
                    removeMaterialComponent = true;
            }

            if (removeMaterialComponent)
                registry.remove<MaterialComponent>(selectedEntity);

            if (removeCameraComponent)
                registry.remove<CameraComponent>(selectedEntity);

            if (removeAudioSourceComponent)
            {
                if (auto* audioSource = registry.try_get<AudioSourceComponent>(selectedEntity))
                {
                    if (audioSource->RuntimeVoiceId != 0)
                        Audio::AudioEngine::GetInstance().Stop(audioSource->RuntimeVoiceId);
                }
                registry.remove<AudioSourceComponent>(selectedEntity);
            }
        }

        ImGui::End();
    }
}
