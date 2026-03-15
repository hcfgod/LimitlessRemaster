#include "EditorInspectorPanelAssetInspectorsShared.h"

#include "Assets/AssetLoadProgress.h"

namespace Limitless::EditorInspectorPanel::Internal
{
    void DrawMaterialInspectorInternal(const char* texturePayloadId,
                                       const char* shaderPayloadId,
                                       EditorAssetPreview::MaterialPreviewCache& materialPreviewCache,
                                       std::string& selectedMaterialAssetKey,
                                       Assets::MaterialAsset::Ptr& cachedMaterialAsset)
    {
        struct MaterialLoadState
        {
            std::string PendingMaterialKey;
            Async::Task<Assets::MaterialAsset::Ptr> PendingTask;
        };
        static MaterialLoadState s_LoadState;

        struct State
        {
            std::string LoadedKey;
            std::string FileName;
            std::filesystem::path ResolvedPath;
            nlohmann::json Json = nlohmann::json::object();
            bool Loaded = false;
        };
        static State s_State;

        if (selectedMaterialAssetKey.empty())
            return;

        if (!cachedMaterialAsset || cachedMaterialAsset->GetKey() != selectedMaterialAssetKey)
        {
            // Poll the pending task first — it holds the strong ref that keeps
            // the MaterialAsset alive (AssetManager stores only weak_ptr).
            if (s_LoadState.PendingMaterialKey == selectedMaterialAssetKey &&
                s_LoadState.PendingTask.IsValid() && s_LoadState.PendingTask.IsDone())
            {
                auto result = s_LoadState.PendingTask.Get();
                s_LoadState.PendingTask = {};
                s_LoadState.PendingMaterialKey.clear();
                if (result)
                    cachedMaterialAsset = std::move(result);
            }
            else if (s_LoadState.PendingMaterialKey != selectedMaterialAssetKey)
            {
                // Selection changed — check cache first, then fire async load.
                auto cached = Assets::AssetManager::GetCachedByKey(selectedMaterialAssetKey);
                if (cached)
                {
                    cachedMaterialAsset = std::dynamic_pointer_cast<Assets::MaterialAsset>(cached);
                    s_LoadState.PendingMaterialKey.clear();
                    s_LoadState.PendingTask = {};
                }
                else
                {
                    cachedMaterialAsset.reset();
                    s_LoadState.PendingMaterialKey = selectedMaterialAssetKey;
                    s_LoadState.PendingTask = Assets::MaterialAsset::LoadAsync(selectedMaterialAssetKey);
                }
            }
        }

        if (!s_State.Loaded || s_State.LoadedKey != selectedMaterialAssetKey)
        {
            s_State = {};
            s_State.LoadedKey = selectedMaterialAssetKey;
            s_State.FileName = std::filesystem::path(selectedMaterialAssetKey).filename().string();
            s_State.Loaded = LoadMaterialJson(selectedMaterialAssetKey, s_State.Json, s_State.ResolvedPath);
            if (!s_State.Loaded)
            {
                ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Failed to load material JSON: %s", selectedMaterialAssetKey.c_str());
                return;
            }
        }

        ImGui::Text("Material: %s", s_State.FileName.c_str());
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (!cachedMaterialAsset)
        {
            if (const auto loadInfo = Assets::AssetLoadProgress::GetProgress(selectedMaterialAssetKey); loadInfo.has_value())
            {
                const float progress = std::clamp(loadInfo->Progress, 0.0f, 1.0f);
                ImGui::ProgressBar(progress, ImVec2(-1.0f, 0.0f));
                if (!loadInfo->Status.empty())
                    ImGui::TextDisabled("%s", loadInfo->Status.c_str());
                else
                    ImGui::TextDisabled("Loading material...");
            }
            else
            {
                ImGui::TextDisabled("Loading material...");
            }
        }

        if (const EditorAssetPreview::MaterialPreviewData* materialPreview = EditorAssetPreview::GetCachedMaterialPreview(materialPreviewCache, selectedMaterialAssetKey))
        {
            const float previewSize = 256.0f;
            const float aspect = materialPreview->SourceHeight / std::max(1.0f, materialPreview->SourceWidth);
            const ImVec4 tintColor(1.0f, 1.0f, 1.0f, 1.0f);
            const ImVec4 borderColor(0.4f, 0.4f, 0.4f, 1.0f);
            ImGui::TextDisabled("Preview");
            if (aspect > 1.0f)
            {
                const float width = previewSize / aspect;
                ImGui::Image(
                    static_cast<ImTextureID>(GetTextureNativeHandle(materialPreview->PreviewTexture)),
                    ImVec2(width, previewSize),
                    materialPreview->UvMin,
                    materialPreview->UvMax,
                    tintColor,
                    borderColor);
            }
            else
            {
                const float height = previewSize * aspect;
                ImGui::Image(
                    static_cast<ImTextureID>(GetTextureNativeHandle(materialPreview->PreviewTexture)),
                    ImVec2(previewSize, height),
                    materialPreview->UvMin,
                    materialPreview->UvMax,
                    tintColor,
                    borderColor);
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
        }

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
                        (void)SaveMaterialJsonAndReload(selectedMaterialAssetKey, materialPreviewCache, cachedMaterialAsset, s_State.Json, s_State.ResolvedPath);
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
                        (void)SaveMaterialJsonAndReload(selectedMaterialAssetKey, materialPreviewCache, cachedMaterialAsset, s_State.Json, s_State.ResolvedPath);
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

            (void)SaveMaterialJsonAndReload(selectedMaterialAssetKey, materialPreviewCache, cachedMaterialAsset, s_State.Json, s_State.ResolvedPath);
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

            (void)SaveMaterialJsonAndReload(selectedMaterialAssetKey, materialPreviewCache, cachedMaterialAsset, s_State.Json, s_State.ResolvedPath);
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

            (void)SaveMaterialJsonAndReload(selectedMaterialAssetKey, materialPreviewCache, cachedMaterialAsset, s_State.Json, s_State.ResolvedPath);
        };

        auto getSpecObjectForSlot = [&](const TextureSlotDescriptor& slot, bool createIfMissing) -> nlohmann::json* {
            if (slot.IsAlbedo)
            {
                if (createIfMissing && (!s_State.Json.contains("mainTextureSpec") || !s_State.Json["mainTextureSpec"].is_object()))
                    s_State.Json["mainTextureSpec"] = nlohmann::json::object();

                if (!s_State.Json.contains("mainTextureSpec") || !s_State.Json["mainTextureSpec"].is_object())
                    return nullptr;

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
                (void)SaveMaterialJsonAndReload(selectedMaterialAssetKey, materialPreviewCache, cachedMaterialAsset, s_State.Json, s_State.ResolvedPath);
            }

            int magFilter = getFilterIndex(spec->value("magFilter", "Linear").c_str());
            if (ImGui::Combo("Mag Filter", &magFilter, filterNames, 2))
            {
                (*spec)["magFilter"] = (magFilter == 0) ? "Nearest" : "Linear";
                if (slot.IsAlbedo)
                    (*getSlotObject(slot, true))["spec"] = *spec;
                (void)SaveMaterialJsonAndReload(selectedMaterialAssetKey, materialPreviewCache, cachedMaterialAsset, s_State.Json, s_State.ResolvedPath);
            }

            const char* wrapNames[] = { "Repeat", "Clamp To Edge" };
            int wrapU = getWrapIndex(spec->value("wrapU", "Repeat").c_str());
            if (ImGui::Combo("Wrap U", &wrapU, wrapNames, 2))
            {
                (*spec)["wrapU"] = (wrapU == 1) ? "ClampToEdge" : "Repeat";
                if (slot.IsAlbedo)
                    (*getSlotObject(slot, true))["spec"] = *spec;
                (void)SaveMaterialJsonAndReload(selectedMaterialAssetKey, materialPreviewCache, cachedMaterialAsset, s_State.Json, s_State.ResolvedPath);
            }

            int wrapV = getWrapIndex(spec->value("wrapV", "Repeat").c_str());
            if (ImGui::Combo("Wrap V", &wrapV, wrapNames, 2))
            {
                (*spec)["wrapV"] = (wrapV == 1) ? "ClampToEdge" : "Repeat";
                if (slot.IsAlbedo)
                    (*getSlotObject(slot, true))["spec"] = *spec;
                (void)SaveMaterialJsonAndReload(selectedMaterialAssetKey, materialPreviewCache, cachedMaterialAsset, s_State.Json, s_State.ResolvedPath);
            }

            bool generateMipmaps = spec->value("generateMipmaps", false);
            if (ImGui::Checkbox("Generate Mipmaps", &generateMipmaps))
            {
                (*spec)["generateMipmaps"] = generateMipmaps;
                if (slot.IsAlbedo)
                    (*getSlotObject(slot, true))["spec"] = *spec;
                (void)SaveMaterialJsonAndReload(selectedMaterialAssetKey, materialPreviewCache, cachedMaterialAsset, s_State.Json, s_State.ResolvedPath);
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
            (void)SaveMaterialJsonAndReload(selectedMaterialAssetKey, materialPreviewCache, cachedMaterialAsset, s_State.Json, s_State.ResolvedPath);
        }

        float roughness = s_State.Json.value("roughness", 0.5f);
        if (ImGui::SliderFloat("Roughness", &roughness, 0.0f, 1.0f, "%.2f"))
        {
            s_State.Json["roughness"] = std::clamp(roughness, 0.0f, 1.0f);
            (void)SaveMaterialJsonAndReload(selectedMaterialAssetKey, materialPreviewCache, cachedMaterialAsset, s_State.Json, s_State.ResolvedPath);
        }

        float specularIntensity = s_State.Json.value("specularIntensity", s_State.Json.value("specular", 0.5f));
        if (ImGui::DragFloat("Specular Intensity", &specularIntensity, 0.01f, 0.0f, 8.0f, "%.2f"))
        {
            s_State.Json["specularIntensity"] = std::clamp(specularIntensity, 0.0f, 8.0f);
            (void)SaveMaterialJsonAndReload(selectedMaterialAssetKey, materialPreviewCache, cachedMaterialAsset, s_State.Json, s_State.ResolvedPath);
        }
    }
}
