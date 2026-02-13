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
#include "Project/BuildTargetsSettings.h"
#include "Project/ProjectManager.h"
#include "Scene/Scene.h"
#include "Scripting/NativeScriptRegistry.h"
#include "imgui/imgui.h"

#include <array>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <optional>
#include <atomic>
#include <thread>
#include <regex>
#include <unordered_set>
#include <vector>
#include <nlohmann/json.hpp>

#ifdef LT_PLATFORM_WINDOWS
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#endif

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
            if (ImGui::Button("Open In Native Script Editor", ImVec2(-1.0f, 0.0f)))
                (void)EditorInspectorPanel::OpenNativeScriptEditorForAssetKey(selectedNativeScriptAssetKey);
            if (pairedExists && ImGui::Button("Open Paired File", ImVec2(-1.0f, 0.0f)))
                (void)EditorInspectorPanel::OpenNativeScriptEditorForAssetKey(pairedAssetKey);
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

        constexpr size_t kNativeScriptEditorBufferSize = 256 * 1024;

        struct NativeScriptAuthoringState
        {
            bool EditorWindowOpen = false;
            bool FocusEditorWindowRequested = false;
            bool ShowDebugInfo = false;
            bool SelectHeaderTabRequested = false;
            bool SelectSourceTabRequested = false;
            std::string ClassName;
            std::string AssetRelativePath;
            std::filesystem::path HeaderPath;
            std::filesystem::path SourcePath;
            std::array<char, kNativeScriptEditorBufferSize> HeaderBuffer{};
            std::array<char, kNativeScriptEditorBufferSize> SourceBuffer{};
            std::array<char, 128> NewScriptClassNameBuffer{};
            std::array<char, 256> NewScriptRelativeDirectoryBuffer{};
            std::string StatusMessage;
            bool StatusIsError = false;
            bool AutoBuildAfterSave = true;
            std::atomic<bool> BuildInProgress{ false };
            std::atomic<int> LastBuildExitCode{ -1 };
            std::unique_ptr<std::jthread> BuildThread;
        };

        NativeScriptAuthoringState& GetNativeScriptAuthoringState()
        {
            static NativeScriptAuthoringState state;
            return state;
        }

        bool s_HasPendingNativeScriptEditorSessionRestore = false;
        EditorInspectorPanel::NativeScriptEditorSessionState s_PendingNativeScriptEditorSessionState;

        std::optional<std::filesystem::path> FindEngineWorkspaceRoot()
        {
            std::error_code errorCode;
            std::filesystem::path probe = std::filesystem::current_path(errorCode);
            if (errorCode)
                return std::nullopt;

            for (int depth = 0; depth < 32; ++depth)
            {
                const std::filesystem::path buildScriptPath = probe / "Scripts" / "build-windows.bat";
                const std::filesystem::path solutionPath = probe / "LimitlessRemaster.sln";
                if (std::filesystem::exists(buildScriptPath, errorCode) &&
                    std::filesystem::is_regular_file(buildScriptPath, errorCode) &&
                    std::filesystem::exists(solutionPath, errorCode) &&
                    std::filesystem::is_regular_file(solutionPath, errorCode))
                {
                    return probe;
                }

                if (!probe.has_parent_path())
                    break;
                const std::filesystem::path parent = probe.parent_path();
                if (parent == probe)
                    break;
                probe = parent;
            }

            return std::nullopt;
        }

        std::optional<std::filesystem::path> GetGeneratedScriptCoreMirrorDirectory()
        {
            const auto engineRoot = FindEngineWorkspaceRoot();
            if (!engineRoot.has_value())
                return std::nullopt;
            return engineRoot.value() / "Build" / "Generated" / "ScriptCore";
        }

        std::optional<std::filesystem::path> GetOpenedProjectAssetScriptsDirectory()
        {
            const auto& projectManager = Project::ProjectManager::GetInstance();
            if (!projectManager.HasOpenProject())
                return std::nullopt;
            return projectManager.GetProjectRoot() / "Assets" / "Scripts";
        }

        std::optional<std::filesystem::path> GetOpenedProjectRoot()
        {
            const auto& projectManager = Project::ProjectManager::GetInstance();
            if (!projectManager.HasOpenProject())
                return std::nullopt;
            return projectManager.GetProjectRoot();
        }

        std::optional<std::filesystem::path> GetOpenedProjectAssetsRoot()
        {
            const auto projectRoot = GetOpenedProjectRoot();
            if (!projectRoot.has_value())
                return std::nullopt;
            return projectRoot.value() / "Assets";
        }

        std::optional<std::filesystem::path> GetAuthoringNativeScriptsDirectory()
        {
            const auto openedProjectDirectory = GetOpenedProjectAssetScriptsDirectory();
            if (openedProjectDirectory.has_value())
                return openedProjectDirectory;
            return std::nullopt;
        }

        std::pair<std::string, std::string> GetBuildConfigurationAndPlatform(const std::filesystem::path& settingsRoot)
        {
            const auto buildTargetsResult = Project::LoadBuildTargetsSettings(settingsRoot);
            if (buildTargetsResult.IsSuccess())
            {
                const auto& settings = buildTargetsResult.GetValue();
                const std::string configuration = settings.Configuration.empty() ? "Debug" : settings.Configuration;
                const std::string platform = settings.Platform.empty() ? "x64" : settings.Platform;
                return { configuration, platform };
            }

            return { "Debug", "x64" };
        }

        int RunBuildScriptBlockingWindows(const std::filesystem::path& projectRoot, const std::string& configuration, const std::string& platform)
        {
#ifdef LT_PLATFORM_WINDOWS
            const std::string scriptCommand = "cmd.exe /c \"Scripts\\build-scriptcore-windows.bat " + configuration + " " + platform + "\"";
            STARTUPINFOA startupInfo{};
            startupInfo.cb = sizeof(startupInfo);
            PROCESS_INFORMATION processInformation{};

            std::string mutableCommandLine = scriptCommand;
            const BOOL created = CreateProcessA(
                nullptr,
                mutableCommandLine.data(),
                nullptr,
                nullptr,
                FALSE,
                0,
                nullptr,
                projectRoot.string().c_str(),
                &startupInfo,
                &processInformation);

            if (!created)
                return 1;

            WaitForSingleObject(processInformation.hProcess, INFINITE);

            DWORD exitCode = 1;
            (void)GetExitCodeProcess(processInformation.hProcess, &exitCode);
            CloseHandle(processInformation.hThread);
            CloseHandle(processInformation.hProcess);
            return static_cast<int>(exitCode);
#else
            (void)projectRoot;
            (void)configuration;
            (void)platform;
            return 1;
#endif
        }

        bool MirrorAllProjectNativeScriptsToGeneratedDirectory(std::string& outError);

        bool TriggerNativeScriptsBuild(NativeScriptAuthoringState& state)
        {
            if (state.BuildInProgress.load(std::memory_order_relaxed))
                return false;

            std::string mirrorError;
            if (!MirrorAllProjectNativeScriptsToGeneratedDirectory(mirrorError))
            {
                state.StatusMessage = mirrorError;
                state.StatusIsError = true;
                return false;
            }

            const auto engineRoot = FindEngineWorkspaceRoot();
            if (!engineRoot.has_value())
            {
                state.StatusMessage = "Could not locate engine workspace root to run build script.";
                state.StatusIsError = true;
                return false;
            }

            if (state.BuildThread && state.BuildThread->joinable())
                state.BuildThread->join();

            state.BuildInProgress.store(true, std::memory_order_relaxed);
            state.LastBuildExitCode.store(-1, std::memory_order_relaxed);
            state.StatusMessage = "Building native scripts...";
            state.StatusIsError = false;

            const auto openedProjectRoot = GetOpenedProjectRoot();
            const std::filesystem::path settingsRoot = openedProjectRoot.has_value()
                ? openedProjectRoot.value()
                : engineRoot.value();
            const auto [configuration, platform] = GetBuildConfigurationAndPlatform(settingsRoot);
            state.BuildThread = std::make_unique<std::jthread>([&state, root = engineRoot.value(), configuration, platform]() {
                const int exitCode = RunBuildScriptBlockingWindows(root, configuration, platform);
                state.LastBuildExitCode.store(exitCode, std::memory_order_relaxed);
                state.BuildInProgress.store(false, std::memory_order_relaxed);
            });

            return true;
        }

        bool MirrorAllProjectNativeScriptsToGeneratedDirectory(std::string& outError)
        {
            const auto assetsRoot = GetOpenedProjectAssetsRoot();
            if (!assetsRoot.has_value())
            {
                outError = "Cannot mirror scripts: no opened project assets root.";
                return false;
            }

            const auto generatedDirectory = GetGeneratedScriptCoreMirrorDirectory();
            if (!generatedDirectory.has_value())
            {
                outError = "Cannot mirror scripts: generated ScriptCore mirror directory was not found.";
                return false;
            }

            std::error_code createDirectoriesError;
            std::filesystem::remove_all(generatedDirectory.value(), createDirectoriesError);
            if (createDirectoriesError)
            {
                outError = "Cannot clear generated ScriptCore mirror directory: " + createDirectoriesError.message();
                return false;
            }

            std::filesystem::create_directories(generatedDirectory.value(), createDirectoriesError);
            if (createDirectoriesError)
            {
                outError = "Cannot create generated ScriptCore mirror directory: " + createDirectoriesError.message();
                return false;
            }

            for (const auto& entry : std::filesystem::recursive_directory_iterator(assetsRoot.value(), std::filesystem::directory_options::skip_permission_denied))
            {
                if (!entry.is_regular_file())
                    continue;
                if (entry.path().extension() != ".cpp")
                    continue;

                const std::filesystem::path sourceCppPath = entry.path();
                const std::filesystem::path sourceHeaderPath = sourceCppPath.parent_path() / (sourceCppPath.stem().string() + ".h");
                if (!std::filesystem::exists(sourceHeaderPath))
                    continue;

                std::error_code relativeError;
                const std::filesystem::path relativeCppPath = std::filesystem::relative(sourceCppPath, assetsRoot.value(), relativeError);
                if (relativeError || relativeCppPath.empty())
                    continue;

                const std::filesystem::path relativeHeaderPath = std::filesystem::relative(sourceHeaderPath, assetsRoot.value(), relativeError);
                if (relativeError || relativeHeaderPath.empty())
                    continue;

                const std::filesystem::path destinationCppPath = generatedDirectory.value() / relativeCppPath;
                const std::filesystem::path destinationHeaderPath = generatedDirectory.value() / relativeHeaderPath;

                std::filesystem::create_directories(destinationCppPath.parent_path(), createDirectoriesError);
                if (createDirectoriesError)
                {
                    outError = "Failed to create generated script directory: " + createDirectoriesError.message();
                    return false;
                }

                std::error_code copyError;
                std::filesystem::copy_file(sourceCppPath, destinationCppPath, std::filesystem::copy_options::overwrite_existing, copyError);
                if (copyError)
                {
                    outError = "Failed to mirror source file '" + sourceCppPath.string() + "': " + copyError.message();
                    return false;
                }

                std::filesystem::copy_file(sourceHeaderPath, destinationHeaderPath, std::filesystem::copy_options::overwrite_existing, copyError);
                if (copyError)
                {
                    outError = "Failed to mirror header file '" + sourceHeaderPath.string() + "': " + copyError.message();
                    return false;
                }

            }

            outError.clear();
            return true;
        }

        bool HasAnyProjectNativeScriptSources()
        {
            const auto assetsRoot = GetOpenedProjectAssetsRoot();
            if (!assetsRoot.has_value())
                return false;

            for (const auto& entry : std::filesystem::recursive_directory_iterator(assetsRoot.value(), std::filesystem::directory_options::skip_permission_denied))
            {
                if (!entry.is_regular_file())
                    continue;
                if (entry.path().extension() != ".cpp")
                    continue;

                const std::filesystem::path headerPath = entry.path().parent_path() / (entry.path().stem().string() + ".h");
                if (std::filesystem::exists(headerPath))
                    return true;
            }

            return false;
        }

        std::vector<std::string> DiscoverNativeScriptClassNamesFromProjectAssets()
        {
            std::vector<std::string> discoveredClassNames;
            const auto assetsRoot = GetOpenedProjectAssetsRoot();
            if (!assetsRoot.has_value())
                return discoveredClassNames;

            std::unordered_set<std::string> uniqueClassNames;
            for (const auto& entry : std::filesystem::recursive_directory_iterator(assetsRoot.value(), std::filesystem::directory_options::skip_permission_denied))
            {
                if (!entry.is_regular_file())
                    continue;
                if (entry.path().extension() != ".cpp")
                    continue;

                const std::filesystem::path headerPath = entry.path().parent_path() / (entry.path().stem().string() + ".h");
                if (!std::filesystem::exists(headerPath))
                    continue;

                const std::string className = entry.path().stem().string();
                if (!className.empty() && uniqueClassNames.insert(className).second)
                    discoveredClassNames.push_back(className);
            }

            std::sort(discoveredClassNames.begin(), discoveredClassNames.end());
            return discoveredClassNames;
        }

        std::string SanitizeNativeScriptClassName(const char* rawName)
        {
            std::string className = rawName ? rawName : "";
            className.erase(std::remove_if(className.begin(), className.end(), [](unsigned char character) {
                return std::isspace(character) != 0;
            }), className.end());

            std::string sanitized;
            sanitized.reserve(className.size() + 8);
            for (char character : className)
            {
                const unsigned char value = static_cast<unsigned char>(character);
                if (std::isalnum(value) || character == '_')
                    sanitized.push_back(character);
            }

            if (sanitized.empty())
                sanitized = "NewNativeScript";
            if (std::isdigit(static_cast<unsigned char>(sanitized.front())))
                sanitized.insert(0, "Script_");
            return sanitized;
        }

        std::string SanitizeRelativeAssetDirectory(const char* rawPath)
        {
            std::string relativePath = rawPath ? rawPath : "";
            std::replace(relativePath.begin(), relativePath.end(), '\\', '/');

            std::string sanitized;
            sanitized.reserve(relativePath.size());
            for (char character : relativePath)
            {
                const unsigned char value = static_cast<unsigned char>(character);
                if (std::isalnum(value) || character == '_' || character == '-' || character == '/')
                    sanitized.push_back(character);
            }

            while (!sanitized.empty() && (sanitized.front() == '/' || sanitized.front() == '.'))
                sanitized.erase(sanitized.begin());
            while (!sanitized.empty() && sanitized.back() == '/')
                sanitized.pop_back();

            if (sanitized.empty())
                sanitized = "Scripts";
            return sanitized;
        }

        bool LoadTextFileIntoBuffer(const std::filesystem::path& path, std::array<char, kNativeScriptEditorBufferSize>& buffer, std::string& outError)
        {
            std::ifstream input(path, std::ios::in | std::ios::binary);
            if (!input.is_open())
            {
                outError = "Failed to open file: " + path.string();
                return false;
            }

            const std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
            if (content.size() >= buffer.size())
            {
                outError = "File is too large for editor buffer: " + path.string();
                return false;
            }

            std::fill(buffer.begin(), buffer.end(), '\0');
            std::memcpy(buffer.data(), content.data(), content.size());
            return true;
        }

        bool SaveBufferToTextFile(const std::filesystem::path& path, const std::array<char, kNativeScriptEditorBufferSize>& buffer, std::string& outError)
        {
            std::ofstream output(path, std::ios::out | std::ios::binary | std::ios::trunc);
            if (!output.is_open())
            {
                outError = "Failed to open file for writing: " + path.string();
                return false;
            }

            output << buffer.data();
            if (!output.good())
            {
                outError = "Failed to write file: " + path.string();
                return false;
            }

            return true;
        }

        struct ScriptPublicFieldDefinition final
        {
            std::string Name;
            ScriptPropertyValue DefaultValue;
        };

        std::string TrimString(const std::string& value)
        {
            size_t start = 0;
            while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0)
                ++start;

            size_t end = value.size();
            while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0)
                --end;

            return value.substr(start, end - start);
        }

        bool TryParseFloatLiteral(const std::string& rawValue, float& outValue)
        {
            std::string value = TrimString(rawValue);
            if (!value.empty() && (value.back() == 'f' || value.back() == 'F'))
                value.pop_back();
            if (value.empty())
                return false;

            char* parseEnd = nullptr;
            const float parsedValue = std::strtof(value.c_str(), &parseEnd);
            if (parseEnd == value.c_str() || *parseEnd != '\0')
                return false;
            outValue = parsedValue;
            return true;
        }

        bool TryParseIntegerLiteral(const std::string& rawValue, int32_t& outValue)
        {
            const std::string value = TrimString(rawValue);
            if (value.empty())
                return false;

            char* parseEnd = nullptr;
            const long parsedValue = std::strtol(value.c_str(), &parseEnd, 10);
            if (parseEnd == value.c_str() || *parseEnd != '\0')
                return false;
            outValue = static_cast<int32_t>(parsedValue);
            return true;
        }

        bool TryParseVector3Literal(const std::string& rawValue, glm::vec3& outValue)
        {
            const std::regex numberPattern(R"([+-]?\d*\.?\d+(?:[eE][+-]?\d+)?)");
            std::vector<float> values;
            for (std::sregex_iterator iterator(rawValue.begin(), rawValue.end(), numberPattern), end; iterator != end; ++iterator)
            {
                float parsedValue = 0.0f;
                if (!TryParseFloatLiteral(iterator->str(), parsedValue))
                    continue;
                values.push_back(parsedValue);
                if (values.size() == 3)
                    break;
            }

            if (values.size() != 3)
                return false;

            outValue = glm::vec3(values[0], values[1], values[2]);
            return true;
        }

        bool TryBuildDefaultFieldValue(const std::string& typeName,
                                       const std::optional<std::string>& rawInitializer,
                                       ScriptPropertyValue& outValue)
        {
            const std::string initializer = rawInitializer.has_value() ? TrimString(rawInitializer.value()) : std::string();

            if (typeName == "float")
            {
                float value = 0.0f;
                if (!initializer.empty() && !TryParseFloatLiteral(initializer, value))
                    return false;
                outValue = value;
                return true;
            }
            if (typeName == "int" || typeName == "int32_t")
            {
                int32_t value = 0;
                if (!initializer.empty() && !TryParseIntegerLiteral(initializer, value))
                    return false;
                outValue = value;
                return true;
            }
            if (typeName == "bool")
            {
                bool value = false;
                if (!initializer.empty())
                {
                    if (initializer == "true")
                        value = true;
                    else if (initializer == "false")
                        value = false;
                    else
                        return false;
                }
                outValue = value;
                return true;
            }
            if (typeName == "glm::vec3")
            {
                glm::vec3 value(0.0f);
                if (!initializer.empty() && !TryParseVector3Literal(initializer, value))
                    return false;
                outValue = value;
                return true;
            }
            if (typeName == "std::string")
            {
                std::string value;
                if (!initializer.empty())
                {
                    if (initializer.size() < 2 || initializer.front() != '"' || initializer.back() != '"')
                        return false;
                    value = initializer.substr(1, initializer.size() - 2);
                }
                outValue = value;
                return true;
            }

            return false;
        }

        bool ParsePublicScriptFieldsFromHeader(const std::filesystem::path& headerPath,
                                               std::vector<ScriptPublicFieldDefinition>& outFields,
                                               std::string& outError)
        {
            std::ifstream input(headerPath, std::ios::in | std::ios::binary);
            if (!input.is_open())
            {
                outError = "Failed to open script header: " + headerPath.string();
                return false;
            }

            // Supported serializable declaration forms:
            // float Speed = 120.0f;
            // int Health = 100;
            // bool Enabled = true;
            // glm::vec3 Offset = glm::vec3(0.0f, 1.0f, 0.0f);
            // std::string Label = "Player";
            const std::regex fieldPattern(
                R"(^\s*(?:const\s+)?(?:static\s+)?(float|int32_t|int|bool|glm::vec3|std::string)\s+([A-Za-z_][A-Za-z0-9_]*)\s*(?:=\s*([^;]+))?\s*;\s*$)");

            bool insidePublicSection = false;
            std::string line;
            while (std::getline(input, line))
            {
                const size_t commentIndex = line.find("//");
                const std::string content = TrimString(commentIndex == std::string::npos ? line : line.substr(0, commentIndex));
                if (content.empty())
                    continue;

                if (content == "public:")
                {
                    insidePublicSection = true;
                    continue;
                }
                if (content == "private:" || content == "protected:")
                {
                    insidePublicSection = false;
                    continue;
                }

                if (!insidePublicSection)
                    continue;
                if (content.find('(') != std::string::npos)
                    continue;

                std::smatch fieldMatch;
                if (!std::regex_match(content, fieldMatch, fieldPattern))
                    continue;

                ScriptPublicFieldDefinition fieldDefinition;
                fieldDefinition.Name = fieldMatch[2].str();

                std::optional<std::string> initializer;
                if (fieldMatch[3].matched)
                    initializer = fieldMatch[3].str();

                if (!TryBuildDefaultFieldValue(fieldMatch[1].str(), initializer, fieldDefinition.DefaultValue))
                    continue;

                outFields.push_back(std::move(fieldDefinition));
            }

            outError.clear();
            return true;
        }

        bool ParseLegacyExposedFieldsFromSource(const std::filesystem::path& sourcePath,
                                                std::vector<ScriptPublicFieldDefinition>& outFields,
                                                std::string& outError)
        {
            std::ifstream input(sourcePath, std::ios::in | std::ios::binary);
            if (!input.is_open())
            {
                outError = "Failed to open script source: " + sourcePath.string();
                return false;
            }

            const std::regex callPattern(
                R"LT(GetExposed(Float|Integer|Boolean|Vector3|String)\s*\(\s*"([A-Za-z_][A-Za-z0-9_]*)"\s*,\s*([^)]+)\))LT");

            std::unordered_set<std::string> existingNames;
            for (const auto& existingField : outFields)
                existingNames.insert(existingField.Name);

            std::string line;
            while (std::getline(input, line))
            {
                std::smatch callMatch;
                if (!std::regex_search(line, callMatch, callPattern))
                    continue;

                const std::string functionSuffix = callMatch[1].str();
                const std::string propertyName = callMatch[2].str();
                const std::string fallbackExpression = TrimString(callMatch[3].str());

                if (existingNames.find(propertyName) != existingNames.end())
                    continue;

                ScriptPublicFieldDefinition fieldDefinition;
                fieldDefinition.Name = propertyName;

                bool parsed = false;
                if (functionSuffix == "Float")
                {
                    float value = 0.0f;
                    parsed = TryParseFloatLiteral(fallbackExpression, value);
                    if (parsed)
                        fieldDefinition.DefaultValue = value;
                }
                else if (functionSuffix == "Integer")
                {
                    int32_t value = 0;
                    parsed = TryParseIntegerLiteral(fallbackExpression, value);
                    if (parsed)
                        fieldDefinition.DefaultValue = value;
                }
                else if (functionSuffix == "Boolean")
                {
                    if (fallbackExpression == "true")
                    {
                        fieldDefinition.DefaultValue = true;
                        parsed = true;
                    }
                    else if (fallbackExpression == "false")
                    {
                        fieldDefinition.DefaultValue = false;
                        parsed = true;
                    }
                }
                else if (functionSuffix == "Vector3")
                {
                    glm::vec3 value(0.0f);
                    parsed = TryParseVector3Literal(fallbackExpression, value);
                    if (parsed)
                        fieldDefinition.DefaultValue = value;
                }
                else if (functionSuffix == "String")
                {
                    if (fallbackExpression.size() >= 2 && fallbackExpression.front() == '"' && fallbackExpression.back() == '"')
                    {
                        fieldDefinition.DefaultValue = fallbackExpression.substr(1, fallbackExpression.size() - 2);
                        parsed = true;
                    }
                }

                if (!parsed)
                    continue;

                outFields.push_back(std::move(fieldDefinition));
                existingNames.insert(propertyName);
            }

            outError.clear();
            return true;
        }

        bool ResolveNativeScriptFilePaths(const std::string& className,
                                          const std::string& preferredAssetRelativePath,
                                          std::filesystem::path& outHeaderPath,
                                          std::filesystem::path& outSourcePath)
        {
            const auto authoringDirectory = GetAuthoringNativeScriptsDirectory();

            auto tryDirectory = [&](const std::optional<std::filesystem::path>& directory) {
                if (!directory.has_value())
                    return false;
                const std::filesystem::path candidateHeaderPath = directory.value() / (className + ".h");
                const std::filesystem::path candidateSourcePath = directory.value() / (className + ".cpp");
                if (std::filesystem::exists(candidateHeaderPath) && std::filesystem::exists(candidateSourcePath))
                {
                    outHeaderPath = candidateHeaderPath;
                    outSourcePath = candidateSourcePath;
                    return true;
                }
                return false;
            };

            if (!preferredAssetRelativePath.empty())
            {
                if (const auto assetsRoot = GetOpenedProjectAssetsRoot(); assetsRoot.has_value())
                {
                    const std::filesystem::path preferredSourceRoot = assetsRoot.value() / preferredAssetRelativePath;
                    const std::filesystem::path preferredHeaderFile = preferredSourceRoot.string() + ".h";
                    const std::filesystem::path preferredSourceFile = preferredSourceRoot.string() + ".cpp";
                    if (std::filesystem::exists(preferredHeaderFile) && std::filesystem::exists(preferredSourceFile))
                    {
                        outHeaderPath = preferredHeaderFile;
                        outSourcePath = preferredSourceFile;
                        return true;
                    }
                }
            }

            if (tryDirectory(authoringDirectory))
                return true;
            return false;
        }

        bool SynchronizeExposedPropertiesFromScript(NativeScriptEntry& nativeScript,
                                                    std::vector<std::string>& outOrderedFieldNames,
                                                    std::string& outError)
        {
            outOrderedFieldNames.clear();
            outError.clear();

            if (nativeScript.ScriptClassName.empty())
                return true;

            std::filesystem::path headerPath;
            std::filesystem::path sourcePath;
            if (!ResolveNativeScriptFilePaths(nativeScript.ScriptClassName, nativeScript.ScriptAssetRelativePath, headerPath, sourcePath))
            {
                outError = "Script files not found for class '" + nativeScript.ScriptClassName + "'.";
                return false;
            }

            std::vector<ScriptPublicFieldDefinition> fields;
            if (!ParsePublicScriptFieldsFromHeader(headerPath, fields, outError))
                return false;

            if (fields.empty())
            {
                // Backward compatibility: older scripts may still define inspector fields
                // by calling GetExposed* in source without public field declarations.
                (void)ParseLegacyExposedFieldsFromSource(sourcePath, fields, outError);
            }

            std::unordered_set<std::string> declaredFieldNames;
            declaredFieldNames.reserve(fields.size());
            for (const auto& field : fields)
            {
                declaredFieldNames.insert(field.Name);
                outOrderedFieldNames.push_back(field.Name);

                const auto found = nativeScript.ExposedProperties.find(field.Name);
                if (found == nativeScript.ExposedProperties.end())
                {
                    nativeScript.ExposedProperties.emplace(field.Name, field.DefaultValue);
                    continue;
                }

                if (found->second.index() != field.DefaultValue.index())
                    found->second = field.DefaultValue;
            }

            for (auto iterator = nativeScript.ExposedProperties.begin(); iterator != nativeScript.ExposedProperties.end();)
            {
                if (declaredFieldNames.find(iterator->first) == declaredFieldNames.end())
                    iterator = nativeScript.ExposedProperties.erase(iterator);
                else
                    ++iterator;
            }

            return true;
        }

        bool OpenNativeScriptEditor(const std::string& className,
                                    const std::string& preferredAssetRelativePath,
                                    NativeScriptAuthoringState& state,
                                    std::string& outError)
        {
            std::filesystem::path headerPath;
            std::filesystem::path sourcePath;
            if (!ResolveNativeScriptFilePaths(className, preferredAssetRelativePath, headerPath, sourcePath))
            {
                outError = "Script files do not exist for class '" + className + "'.";
                return false;
            }

            if (!LoadTextFileIntoBuffer(headerPath, state.HeaderBuffer, outError))
                return false;
            if (!LoadTextFileIntoBuffer(sourcePath, state.SourceBuffer, outError))
                return false;

            state.ClassName = className;
            state.AssetRelativePath = preferredAssetRelativePath;
            state.HeaderPath = headerPath;
            state.SourcePath = sourcePath;
            state.EditorWindowOpen = true;
            state.FocusEditorWindowRequested = true;
            state.StatusMessage = "Editing script: " + className;
            state.StatusIsError = false;
            return true;
        }

        bool MirrorScriptToGeneratedDirectory(const NativeScriptAuthoringState& state, std::string& outError)
        {
            const auto generatedDirectory = GetGeneratedScriptCoreMirrorDirectory();
            if (!generatedDirectory.has_value())
            {
                outError = "Could not locate generated ScriptCore mirror directory.";
                return false;
            }

            std::filesystem::path relativeMirrorPathWithoutExtension = state.ClassName;
            if (const auto assetsRoot = GetOpenedProjectAssetsRoot(); assetsRoot.has_value())
            {
                std::error_code relativeError;
                const std::filesystem::path scriptRelativePath =
                    std::filesystem::relative(state.SourcePath, assetsRoot.value(), relativeError);
                if (!relativeError && !scriptRelativePath.empty())
                {
                    relativeMirrorPathWithoutExtension = scriptRelativePath;
                    relativeMirrorPathWithoutExtension.replace_extension("");
                }
            }

            const std::filesystem::path generatedHeaderPath = generatedDirectory.value() / relativeMirrorPathWithoutExtension;
            const std::filesystem::path generatedSourcePath = generatedDirectory.value() / relativeMirrorPathWithoutExtension;
            const std::filesystem::path generatedHeaderFile = generatedHeaderPath.string() + ".h";
            const std::filesystem::path generatedSourceFile = generatedSourcePath.string() + ".cpp";

            std::error_code createDirectoriesError;
            std::filesystem::create_directories(generatedHeaderFile.parent_path(), createDirectoriesError);
            if (createDirectoriesError)
            {
                outError = "Failed to create generated ScriptCore mirror directory: " + createDirectoriesError.message();
                return false;
            }

            if (!SaveBufferToTextFile(generatedHeaderFile, state.HeaderBuffer, outError))
                return false;
            if (!SaveBufferToTextFile(generatedSourceFile, state.SourceBuffer, outError))
                return false;
            return true;
        }

        bool CreateNativeScriptFromTemplate(const std::string& className,
                                            const std::string& assetRelativeDirectory,
                                            std::filesystem::path& outHeaderPath,
                                            std::filesystem::path& outSourcePath,
                                            std::string& outAssetRelativePathWithoutExtension,
                                            std::string& outError)
        {
            std::optional<std::filesystem::path> scriptDirectory;
            if (const auto assetsRoot = GetOpenedProjectAssetsRoot(); assetsRoot.has_value())
                scriptDirectory = assetsRoot.value() / assetRelativeDirectory;
            else
                scriptDirectory = GetAuthoringNativeScriptsDirectory();
            if (!scriptDirectory.has_value())
            {
                outError = "Could not locate script authoring directory.";
                return false;
            }

            std::error_code directoryError;
            std::filesystem::create_directories(scriptDirectory.value(), directoryError);
            if (directoryError)
            {
                outError = "Failed to create script directory: " + directoryError.message();
                return false;
            }

            outHeaderPath = scriptDirectory.value() / (className + ".h");
            outSourcePath = scriptDirectory.value() / (className + ".cpp");
            if (const auto assetsRoot = GetOpenedProjectAssetsRoot(); assetsRoot.has_value())
            {
                std::error_code relativeError;
                const std::filesystem::path relativePath = std::filesystem::relative(scriptDirectory.value() / className, assetsRoot.value(), relativeError);
                if (!relativeError)
                    outAssetRelativePathWithoutExtension = relativePath.generic_string();
            }
            if (std::filesystem::exists(outHeaderPath) || std::filesystem::exists(outSourcePath))
            {
                outError = "Script already exists: " + className;
                return false;
            }

            const std::string headerTemplate =
                "#pragma once\n\n"
                "#include \"Limitless.h\"\n\n"
                "class " + className + " final : public Limitless::ScriptableEntity\n"
                "{\n"
                "public:\n"
                "    float RotationSpeed = 90.0f;\n\n"
                "protected:\n"
                "    LT_BEGIN_AUTO_EXPOSED_FIELD_SYNC()\n"
                "        LT_AUTO_EXPOSED_FIELD(RotationSpeed)\n"
                "    LT_END_AUTO_EXPOSED_FIELD_SYNC()\n\n"
                "    void OnCreate() override;\n"
                "    void OnUpdate(float deltaTime) override;\n"
                "    void OnDestroy() override;\n"
                "};\n";

            const std::string sourceTemplate =
                "#include \"" + className + ".h\"\n\n"
                "#include \"ScriptCoreRegistration.h\"\n\n"
                "void " + className + "::OnCreate()\n"
                "{\n"
                "}\n\n"
                "void " + className + "::OnUpdate(float deltaTime)\n"
                "{\n"
                "    auto& transform = GetComponent<Limitless::TransformComponent>();\n"
                "    transform.Rotation.z += RotationSpeed * deltaTime;\n"
                "    if (transform.Rotation.z > 360.0f)\n"
                "        transform.Rotation.z -= 360.0f;\n"
                "}\n\n"
                "void " + className + "::OnDestroy()\n"
                "{\n"
                "}\n\n"
                "LT_REGISTER_SCRIPTCORE_SCRIPT(" + className + ");\n";

            {
                std::ofstream headerOutput(outHeaderPath, std::ios::out | std::ios::binary | std::ios::trunc);
                if (!headerOutput.is_open())
                {
                    outError = "Failed to create header file: " + outHeaderPath.string();
                    return false;
                }
                headerOutput << headerTemplate;
            }

            {
                std::ofstream sourceOutput(outSourcePath, std::ios::out | std::ios::binary | std::ios::trunc);
                if (!sourceOutput.is_open())
                {
                    outError = "Failed to create source file: " + outSourcePath.string();
                    return false;
                }
                sourceOutput << sourceTemplate;
            }

            return true;
        }

        void DrawNativeScriptEditorWindow(NativeScriptAuthoringState& state)
        {
            if (!state.EditorWindowOpen)
                return;

            if (state.FocusEditorWindowRequested)
                ImGui::SetNextWindowFocus();
            if (!ImGui::Begin("Native Script Editor", &state.EditorWindowOpen))
            {
                state.FocusEditorWindowRequested = false;
                ImGui::End();
                return;
            }
            if (state.FocusEditorWindowRequested)
            {
                ImGui::SetWindowFocus();
                state.FocusEditorWindowRequested = false;
            }

            ImGui::Text("Class: %s", state.ClassName.c_str());
            ImGui::Text("Header: %s", state.HeaderPath.string().c_str());
            ImGui::Text("Source: %s", state.SourcePath.string().c_str());
            if (const auto generatedDirectory = GetGeneratedScriptCoreMirrorDirectory(); generatedDirectory.has_value())
            {
                std::filesystem::path mirrorPath = generatedDirectory.value() / (state.ClassName + ".cpp");
                if (const auto assetsRoot = GetOpenedProjectAssetsRoot(); assetsRoot.has_value())
                {
                    std::error_code relativeError;
                    const std::filesystem::path relativeSourcePath =
                        std::filesystem::relative(state.SourcePath, assetsRoot.value(), relativeError);
                    if (!relativeError && !relativeSourcePath.empty())
                        mirrorPath = generatedDirectory.value() / relativeSourcePath;
                }
                ImGui::Text("Generated Mirror: %s", mirrorPath.string().c_str());
            }
            ImGui::TextWrapped("After creating or editing scripts, run the build script to compile and register script classes.");
            ImGui::Checkbox("Auto Build On Save", &state.AutoBuildAfterSave);
            if (state.BuildInProgress.load(std::memory_order_relaxed))
                ImGui::TextDisabled("Build in progress...");

            if (ImGui::Button("Save Files", ImVec2(140.0f, 0.0f)))
            {
                std::string saveError;
                const bool headerSaved = SaveBufferToTextFile(state.HeaderPath, state.HeaderBuffer, saveError);
                const bool sourceSaved = SaveBufferToTextFile(state.SourcePath, state.SourceBuffer, saveError);
                if (headerSaved && sourceSaved)
                {
                    bool canBuild = true;
                    if (!MirrorScriptToGeneratedDirectory(state, saveError))
                    {
                        state.StatusMessage = saveError;
                        state.StatusIsError = true;
                        canBuild = false;
                    }
                    else
                    {
                        state.StatusMessage = "Script files saved and mirrored to generated build directory.";
                        state.StatusIsError = false;
                    }
                    if (state.AutoBuildAfterSave && canBuild)
                        (void)TriggerNativeScriptsBuild(state);
                }
                else
                {
                    state.StatusMessage = saveError;
                    state.StatusIsError = true;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Reload From Disk", ImVec2(160.0f, 0.0f)))
            {
                std::string reloadError;
                const bool headerLoaded = LoadTextFileIntoBuffer(state.HeaderPath, state.HeaderBuffer, reloadError);
                const bool sourceLoaded = LoadTextFileIntoBuffer(state.SourcePath, state.SourceBuffer, reloadError);
                if (headerLoaded && sourceLoaded)
                {
                    state.StatusMessage = "Reloaded script files from disk.";
                    state.StatusIsError = false;
                }
                else
                {
                    state.StatusMessage = reloadError;
                    state.StatusIsError = true;
                }
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(state.BuildInProgress.load(std::memory_order_relaxed));
            if (ImGui::Button("Build Scripts Now", ImVec2(150.0f, 0.0f)))
                (void)TriggerNativeScriptsBuild(state);
            ImGui::EndDisabled();

            const int finishedBuildExitCode = state.LastBuildExitCode.exchange(-1, std::memory_order_relaxed);
            if (finishedBuildExitCode >= 0)
            {
                if (finishedBuildExitCode == 0)
                {
                    state.StatusMessage = "Native script build succeeded.";
                    state.StatusIsError = false;
                }
                else
                {
                    state.StatusMessage = "Native script build failed (exit code " + std::to_string(finishedBuildExitCode) + ").";
                    state.StatusIsError = true;
                }
            }

            if (!state.StatusMessage.empty())
            {
                const ImVec4 statusColor = state.StatusIsError
                    ? ImVec4(1.0f, 0.35f, 0.35f, 1.0f)
                    : ImVec4(0.35f, 1.0f, 0.45f, 1.0f);
                ImGui::TextColored(statusColor, "%s", state.StatusMessage.c_str());
            }

            ImGui::Separator();
            if (ImGui::BeginTabBar("NativeScriptEditorTabs"))
            {
                ImGuiTabItemFlags headerTabFlags = ImGuiTabItemFlags_None;
                ImGuiTabItemFlags sourceTabFlags = ImGuiTabItemFlags_None;
                if (state.SelectHeaderTabRequested)
                    headerTabFlags |= ImGuiTabItemFlags_SetSelected;
                else if (state.SelectSourceTabRequested)
                    sourceTabFlags |= ImGuiTabItemFlags_SetSelected;

                if (ImGui::BeginTabItem("Header (.h)", nullptr, headerTabFlags))
                {
                    ImGui::InputTextMultiline("##NativeScriptHeaderEditor", state.HeaderBuffer.data(), state.HeaderBuffer.size(), ImVec2(-1.0f, 360.0f));
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Source (.cpp)", nullptr, sourceTabFlags))
                {
                    ImGui::InputTextMultiline("##NativeScriptSourceEditor", state.SourceBuffer.data(), state.SourceBuffer.size(), ImVec2(-1.0f, 360.0f));
                    ImGui::EndTabItem();
                }
                state.SelectHeaderTabRequested = false;
                state.SelectSourceTabRequested = false;
                ImGui::EndTabBar();
            }

            ImGui::End();
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
              const char* fontPayloadId,
              std::string& selectedMaterialAssetKey,
              Assets::MaterialAsset::Ptr& cachedMaterialAsset,
              std::string& selectedNativeScriptAssetKey)
    {
        auto& nativeScriptAuthoringState = GetNativeScriptAuthoringState();

        if (s_HasPendingNativeScriptEditorSessionRestore)
        {
            const auto pendingState = s_PendingNativeScriptEditorSessionState;
            s_HasPendingNativeScriptEditorSessionRestore = false;
            s_PendingNativeScriptEditorSessionState = {};
            nativeScriptAuthoringState.ShowDebugInfo = pendingState.ShowDebugInfo;

            if (pendingState.IsOpen && !pendingState.LastEditedScriptClassName.empty())
            {
                std::string openError;
                if (!OpenNativeScriptEditor(
                    pendingState.LastEditedScriptClassName,
                    pendingState.LastEditedScriptAssetRelativePath,
                    nativeScriptAuthoringState,
                    openError))
                {
                    nativeScriptAuthoringState.StatusMessage = openError;
                    nativeScriptAuthoringState.StatusIsError = true;
                    nativeScriptAuthoringState.EditorWindowOpen = true;
                    nativeScriptAuthoringState.FocusEditorWindowRequested = true;
                }
            }
            else
            {
                nativeScriptAuthoringState.EditorWindowOpen = false;
            }
        }

        ImGui::Begin("Inspector");

        if (!selectedMaterialAssetKey.empty())
        {
            DrawMaterialInspector(texturePayloadId, shaderPayloadId, selectedMaterialAssetKey, cachedMaterialAsset);
        }
        else if (!selectedTextureAssetKey.empty())
        {
            DrawTextureInspector(scene, selectedTextureAssetKey, cachedTextureAsset);
        }
        else if (!selectedNativeScriptAssetKey.empty())
        {
            DrawNativeScriptAssetInspector(selectedNativeScriptAssetKey);
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
            bool removeTextComponent = false;
            bool removeNativeScriptComponent = false;
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

            if (auto* text = registry.try_get<TextComponent>(selectedEntity))
            {
                const bool textOpen = ImGui::TreeNodeEx("Text", ImGuiTreeNodeFlags_DefaultOpen);
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                    ImGui::OpenPopup("TextComponentOptions");
                ImGui::SameLine();
                if (ImGui::Button("...##TextComponentOptionsButton"))
                    ImGui::OpenPopup("TextComponentOptions");

                if (ImGui::BeginPopup("TextComponentOptions"))
                {
                    if (ImGui::MenuItem("Remove Component"))
                        removeTextComponent = true;
                    ImGui::EndPopup();
                }

                if (textOpen)
                {
                    static entt::entity textEditEntity = entt::null;
                    static std::array<char, 2048> textValueBuffer{};
                    static std::array<char, 512> fontPathBuffer{};
                    if (textEditEntity != selectedEntity)
                    {
                        textEditEntity = selectedEntity;
                        std::snprintf(textValueBuffer.data(), textValueBuffer.size(), "%s", text->Text.c_str());
                        std::snprintf(fontPathBuffer.data(), fontPathBuffer.size(), "%s", text->FontFilePath.c_str());
                    }

                    ImGui::InputTextMultiline("Text Value", textValueBuffer.data(), textValueBuffer.size(), ImVec2(-1.0f, 84.0f));
                    if (ImGui::IsItemDeactivatedAfterEdit())
                        text->Text = textValueBuffer.data();

                    ImGui::InputText("Font File Path", fontPathBuffer.data(), fontPathBuffer.size());
                    if (ImGui::IsItemDeactivatedAfterEdit())
                    {
                        text->FontFilePath = fontPathBuffer.data();
                        text->CachedFont.reset();
                        text->FontLoadAttempted = false;
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Example: Assets/Fonts/YourFont.ttf");

                    const std::string fontLabel = text->FontFilePath.empty()
                        ? std::string("None")
                        : EditorAssetNaming::GetAssetDisplayNameFromAssetKey(text->FontFilePath);
                    ImGui::Text("Font Asset");
                    ImGui::SameLine(80);
                    ImGui::Button((fontLabel + "##TextFontAsset").c_str(), ImVec2(ImGui::GetContentRegionAvail().x - 60.0f, 0.0f));
                    if (ImGui::BeginDragDropTarget())
                    {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(fontPayloadId))
                        {
                            const char* key = static_cast<const char*>(payload->Data);
                            if (key && key[0])
                            {
                                text->FontFilePath = key;
                                std::snprintf(fontPathBuffer.data(), fontPathBuffer.size(), "%s", text->FontFilePath.c_str());
                                text->CachedFont.reset();
                                text->FontLoadAttempted = false;
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }
                    if (!text->FontFilePath.empty())
                    {
                        ImGui::SameLine();
                        if (ImGui::Button("Clear##TextFontAsset"))
                        {
                            text->FontFilePath.clear();
                            fontPathBuffer[0] = '\0';
                            text->CachedFont.reset();
                            text->FontLoadAttempted = false;
                        }
                    }

                    if (ImGui::DragFloat("Font Size", &text->FontSize, 1.0f, 4.0f, 512.0f))
                        text->FontSize = std::max(4.0f, text->FontSize);
                    ImGui::ColorEdit4("Color", &text->Color.r);

                    ImGui::TreePop();
                }
            }

            if (auto* nativeScript = registry.try_get<NativeScriptComponent>(selectedEntity))
            {
                const bool nativeScriptOpen = ImGui::TreeNodeEx("Native Script", ImGuiTreeNodeFlags_DefaultOpen);
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                    ImGui::OpenPopup("NativeScriptComponentOptions");
                ImGui::SameLine();
                if (ImGui::Button("...##NativeScriptComponentOptionsButton"))
                    ImGui::OpenPopup("NativeScriptComponentOptions");

                if (ImGui::BeginPopup("NativeScriptComponentOptions"))
                {
                    ImGui::MenuItem("Show Debug Info", nullptr, &nativeScriptAuthoringState.ShowDebugInfo);
                    ImGui::Separator();
                    if (ImGui::MenuItem("Remove Component"))
                        removeNativeScriptComponent = true;
                    ImGui::EndPopup();
                }

                if (nativeScriptOpen)
                {
                    const std::vector<std::string> registeredScriptNames = NativeScriptRegistry::GetRegisteredScriptNames();
                    const auto discoveredScriptNames = DiscoverNativeScriptClassNamesFromProjectAssets();
                    std::vector<std::string> availableScriptNames = discoveredScriptNames.empty()
                        ? registeredScriptNames
                        : discoveredScriptNames;
                    if (ImGui::Button("Add Script", ImVec2(-1.0f, 0.0f)))
                    {
                        nativeScript->Scripts.emplace_back();
                    }

                    if (registeredScriptNames.empty())
                    {
                        static bool attemptedAutoScriptBuild = false;
                        if (!attemptedAutoScriptBuild &&
                            !nativeScriptAuthoringState.BuildInProgress.load(std::memory_order_relaxed) &&
                            HasAnyProjectNativeScriptSources())
                        {
                            attemptedAutoScriptBuild = TriggerNativeScriptsBuild(nativeScriptAuthoringState);
                        }

                        if (availableScriptNames.empty())
                            ImGui::TextDisabled("No scripts found.");
                        else
                            ImGui::TextDisabled("No scripts registered yet.");
                        ImGui::TextWrapped("Detected scripts under project Assets are not compiled into ScriptCore yet.");
                        ImGui::BeginDisabled(nativeScriptAuthoringState.BuildInProgress.load(std::memory_order_relaxed));
                        if (ImGui::Button("Build ScriptCore From Project Scripts", ImVec2(-1.0f, 0.0f)))
                            (void)TriggerNativeScriptsBuild(nativeScriptAuthoringState);
                        ImGui::EndDisabled();
                        if (nativeScriptAuthoringState.BuildInProgress.load(std::memory_order_relaxed))
                            ImGui::TextDisabled("Building ScriptCore...");
                    }

                    if (nativeScript->Scripts.empty())
                    {
                        ImGui::TextDisabled("No scripts attached. Click Add Script.");
                    }
                    else
                    {
                        int removeScriptIndex = -1;
                        for (size_t scriptIndex = 0; scriptIndex < nativeScript->Scripts.size(); ++scriptIndex)
                        {
                            auto& scriptEntry = nativeScript->Scripts[scriptIndex];
                            ImGui::PushID(static_cast<int>(scriptIndex));
                            std::string scriptLabel = scriptEntry.ScriptClassName.empty()
                                ? ("Script " + std::to_string(scriptIndex + 1))
                                : scriptEntry.ScriptClassName;
                            const bool scriptOpen = ImGui::TreeNodeEx(scriptLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
                            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                                ImGui::OpenPopup("NativeScriptEntryOptions");
                            ImGui::SameLine();
                            if (ImGui::Button("...##NativeScriptEntryOptionsButton"))
                                ImGui::OpenPopup("NativeScriptEntryOptions");
                            if (ImGui::BeginPopup("NativeScriptEntryOptions"))
                            {
                                if (ImGui::MenuItem("Remove Script"))
                                    removeScriptIndex = static_cast<int>(scriptIndex);
                                ImGui::EndPopup();
                            }

                            if (scriptOpen)
                            {
                                ImGui::Checkbox("Enabled", &scriptEntry.Enabled);

                                std::string previewLabel = scriptEntry.ScriptClassName.empty() ? std::string("None") : scriptEntry.ScriptClassName;
                                if (ImGui::BeginCombo("Class", previewLabel.c_str()))
                                {
                                    const bool noneSelected = scriptEntry.ScriptClassName.empty();
                                    if (ImGui::Selectable("None", noneSelected))
                                    {
                                        scriptEntry.ScriptClassName.clear();
                                        scriptEntry.ScriptAssetRelativePath.clear();
                                        scriptEntry.ExposedProperties.clear();
                                        scriptEntry.RuntimeInitialized = false;
                                        scriptEntry.RuntimeInstance.reset();
                                    }
                                    if (noneSelected)
                                        ImGui::SetItemDefaultFocus();

                                    for (const auto& scriptName : availableScriptNames)
                                    {
                                        const bool scriptSelected = (scriptEntry.ScriptClassName == scriptName);
                                        if (ImGui::Selectable(scriptName.c_str(), scriptSelected))
                                        {
                                            scriptEntry.ScriptClassName = scriptName;
                                            scriptEntry.ScriptAssetRelativePath.clear();
                                            scriptEntry.ExposedProperties.clear();
                                            scriptEntry.RuntimeInitialized = false;
                                            scriptEntry.RuntimeInstance.reset();
                                        }
                                        if (scriptSelected)
                                            ImGui::SetItemDefaultFocus();
                                    }
                                    ImGui::EndCombo();
                                }

                                const bool selectedClassCompiled =
                                    scriptEntry.ScriptClassName.empty() || NativeScriptRegistry::HasScript(scriptEntry.ScriptClassName);
                                if (!selectedClassCompiled)
                                    ImGui::TextDisabled("Selected script is discovered in Assets but not compiled yet. Build ScriptCore.");

                                if (!scriptEntry.ScriptAssetRelativePath.empty())
                                    ImGui::TextDisabled("Asset: Assets/%s", scriptEntry.ScriptAssetRelativePath.c_str());
                                if (nativeScriptAuthoringState.ShowDebugInfo)
                                    ImGui::TextDisabled("Runtime updates: %llu",
                                                        static_cast<unsigned long long>(scriptEntry.RuntimeUpdateCount));

                                ImGui::Separator();
                                std::vector<std::string> declaredFieldNames;
                                std::string fieldSyncError;
                                const bool syncedFromScript = SynchronizeExposedPropertiesFromScript(scriptEntry, declaredFieldNames, fieldSyncError);

                                ImGui::TextUnformatted("Exposed Variables");
                                if (scriptEntry.ScriptClassName.empty())
                                {
                                    ImGui::TextDisabled("Assign a script class to view exposed variables.");
                                }
                                else if (!syncedFromScript)
                                {
                                    ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "%s", fieldSyncError.c_str());
                                    ImGui::TextDisabled("Supported public field types: float, int/int32_t, bool, glm::vec3, std::string.");
                                }
                                else if (declaredFieldNames.empty())
                                {
                                    ImGui::TextDisabled("No supported public fields found on this script.");
                                }
                                else
                                {
                                    for (const std::string& propertyName : declaredFieldNames)
                                    {
                                        auto propertyIterator = scriptEntry.ExposedProperties.find(propertyName);
                                        if (propertyIterator == scriptEntry.ExposedProperties.end())
                                            continue;

                                        auto& propertyValue = propertyIterator->second;
                                        ImGui::PushID(propertyName.c_str());
                                        if (auto* floatValue = std::get_if<float>(&propertyValue))
                                        {
                                            ImGui::DragFloat(propertyName.c_str(), floatValue, 0.1f);
                                        }
                                        else if (auto* integerValue = std::get_if<int32_t>(&propertyValue))
                                        {
                                            ImGui::DragInt(propertyName.c_str(), integerValue, 1.0f);
                                        }
                                        else if (auto* booleanValue = std::get_if<bool>(&propertyValue))
                                        {
                                            ImGui::Checkbox(propertyName.c_str(), booleanValue);
                                        }
                                        else if (auto* vectorValue = std::get_if<glm::vec3>(&propertyValue))
                                        {
                                            ImGui::DragFloat3(propertyName.c_str(), &vectorValue->x, 0.1f);
                                        }
                                        else if (auto* stringValue = std::get_if<std::string>(&propertyValue))
                                        {
                                            std::array<char, 256> textBuffer{};
                                            std::snprintf(textBuffer.data(), textBuffer.size(), "%s", stringValue->c_str());
                                            if (ImGui::InputText(propertyName.c_str(), textBuffer.data(), textBuffer.size()))
                                                *stringValue = textBuffer.data();
                                        }
                                        ImGui::PopID();
                                    }
                                }

                                ImGui::TreePop();
                            }
                            ImGui::PopID();
                        }

                        if (removeScriptIndex >= 0 && removeScriptIndex < static_cast<int>(nativeScript->Scripts.size()))
                            nativeScript->Scripts.erase(nativeScript->Scripts.begin() + removeScriptIndex);
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
                const bool hasTextComponent = registry.all_of<TextComponent>(selectedEntity);
                const bool hasNativeScriptComponent = registry.all_of<NativeScriptComponent>(selectedEntity);
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

                if (hasTextComponent)
                    ImGui::BeginDisabled();

                if (ImGui::MenuItem("Text Component"))
                    registry.emplace<TextComponent>(selectedEntity);

                if (hasTextComponent)
                    ImGui::EndDisabled();

                if (hasNativeScriptComponent)
                    ImGui::BeginDisabled();

                if (ImGui::MenuItem("Native Script"))
                    registry.emplace<NativeScriptComponent>(selectedEntity);

                if (hasNativeScriptComponent)
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

            if (removeTextComponent)
                registry.remove<TextComponent>(selectedEntity);

            if (removeNativeScriptComponent)
            {
                if (auto* nativeScript = registry.try_get<NativeScriptComponent>(selectedEntity))
                {
                    for (auto& scriptEntry : nativeScript->Scripts)
                    {
                        scriptEntry.RuntimeInitialized = false;
                        scriptEntry.RuntimeInstance.reset();
                    }
                }
                registry.remove<NativeScriptComponent>(selectedEntity);
            }
        }

        ImGui::End();
        DrawNativeScriptEditorWindow(nativeScriptAuthoringState);
    }

    void GetNativeScriptEditorSessionState(NativeScriptEditorSessionState& outState)
    {
        const auto& state = GetNativeScriptAuthoringState();
        outState.IsOpen = state.EditorWindowOpen;
        outState.LastEditedScriptClassName = state.ClassName;
        outState.LastEditedScriptAssetRelativePath = state.AssetRelativePath;
        outState.ShowDebugInfo = state.ShowDebugInfo;
    }

    void ApplyNativeScriptEditorSessionState(const NativeScriptEditorSessionState& state)
    {
        s_PendingNativeScriptEditorSessionState = state;
        s_HasPendingNativeScriptEditorSessionRestore = true;
    }

    bool OpenNativeScriptEditorForAssetKey(const std::string& assetKey)
    {
        const std::filesystem::path assetPath(assetKey);
        std::string extension = assetPath.extension().string();
        for (char& character : extension)
            character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
        if (extension != ".h" && extension != ".cpp")
            return false;
        const bool preferHeaderTab = (extension == ".h");
        const bool preferSourceTab = (extension == ".cpp");

        const std::string normalizedKey = assetPath.generic_string();
        constexpr const char* assetsPrefix = "Assets/";
        if (normalizedKey.rfind(assetsPrefix, 0) != 0)
            return false;

        std::filesystem::path relativeWithoutAssets = normalizedKey.substr(std::strlen(assetsPrefix));
        relativeWithoutAssets.replace_extension("");
        const std::string assetRelativePathWithoutExtension = relativeWithoutAssets.generic_string();
        if (assetRelativePathWithoutExtension.empty())
            return false;

        auto& nativeScriptAuthoringState = GetNativeScriptAuthoringState();
        std::string openError;
        const std::string className = assetPath.stem().string();
        if (!OpenNativeScriptEditor(className, assetRelativePathWithoutExtension, nativeScriptAuthoringState, openError))
        {
            nativeScriptAuthoringState.StatusMessage = openError;
            nativeScriptAuthoringState.StatusIsError = true;
            nativeScriptAuthoringState.EditorWindowOpen = true;
            nativeScriptAuthoringState.FocusEditorWindowRequested = true;
            return false;
        }
        nativeScriptAuthoringState.SelectHeaderTabRequested = preferHeaderTab;
        nativeScriptAuthoringState.SelectSourceTabRequested = preferSourceTab;

        return true;
    }

    void OnNativeScriptAssetRenamed(const std::string& oldAssetKey, const std::string& newAssetKey)
    {
        auto parseScriptKey = [](const std::string& key, std::string& outClassName, std::string& outRelativePathWithoutExtension) -> bool {
            if (key.rfind("Assets/", 0) != 0)
                return false;
            std::filesystem::path path = key.substr(std::strlen("Assets/"));
            std::string extension = path.extension().string();
            for (char& character : extension)
                character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
            if (extension != ".h" && extension != ".cpp")
                return false;
            path.replace_extension("");
            outRelativePathWithoutExtension = path.generic_string();
            outClassName = path.stem().string();
            return !outClassName.empty() && !outRelativePathWithoutExtension.empty();
        };

        std::string oldClassName;
        std::string oldRelativePath;
        std::string newClassName;
        std::string newRelativePath;
        if (!parseScriptKey(oldAssetKey, oldClassName, oldRelativePath) ||
            !parseScriptKey(newAssetKey, newClassName, newRelativePath))
        {
            return;
        }

        auto& state = GetNativeScriptAuthoringState();
        const bool matchesOpenEditor =
            (state.ClassName == oldClassName) ||
            (!state.AssetRelativePath.empty() && state.AssetRelativePath == oldRelativePath);
        if (!matchesOpenEditor)
            return;

        std::string openError;
        if (!OpenNativeScriptEditor(newClassName, newRelativePath, state, openError))
        {
            state.StatusMessage = openError;
            state.StatusIsError = true;
            state.EditorWindowOpen = true;
            state.FocusEditorWindowRequested = true;
        }
    }
}
