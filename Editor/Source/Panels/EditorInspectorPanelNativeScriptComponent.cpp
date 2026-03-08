#include "EditorInspectorPanelNativeScriptComponent.h"

#include "EditorAssetNaming.h"
#include "EditorInspectorPanelNativeScriptEditor.h"
#include "Undo/EditorUndoService.h"
#include "Assets/AssetDatabase.h"
#include "Assets/AssetPaths.h"
#include "Project/ProjectManager.h"
#include "Scene/Scene.h"
#include "Scene/SceneSystemScheduler.h"
#include "Scripting/ManagedScriptHost.h"
#include "Scripting/NativeScriptRegistry.h"
#include "imgui/imgui.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <map>
#include <unordered_set>

namespace Limitless::EditorInspectorPanel
{
    namespace
    {
        constexpr const char* kSceneEntityPayload = "SCENE_ENTITY";
        constexpr const char* kAssetMovePayload = "ASSET_MOVE";

        struct ProjectScriptFolderNode final
        {
            std::map<std::string, ProjectScriptFolderNode> Children;
            std::vector<ProjectNativeScriptInfo> Scripts;
        };

        bool BeginInspectorScriptSectionHeader(const char* label,
                                               const char* popupId,
                                               const char* optionsButtonId)
        {
            ImGui::Spacing();
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f, 10.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.12f, 0.19f, 0.29f, 0.96f));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.18f, 0.27f, 0.41f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.24f, 0.34f, 0.50f, 1.0f));
            const bool isOpen = ImGui::TreeNodeEx(label,
                                                  ImGuiTreeNodeFlags_DefaultOpen |
                                                      ImGuiTreeNodeFlags_Framed |
                                                      ImGuiTreeNodeFlags_AllowItemOverlap);
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar(2);

            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                ImGui::OpenPopup(popupId);

            const ImVec2 headerMin = ImGui::GetItemRectMin();
            const ImVec2 headerMax = ImGui::GetItemRectMax();
            const float optionsButtonWidth = ImGui::CalcTextSize("...").x + 16.0f;
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 4.0f));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.23f, 0.34f, 0.50f, 0.95f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.43f, 0.60f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.35f, 0.49f, 0.67f, 1.0f));
            const float optionsButtonHeight = ImGui::GetFrameHeight();
            ImGui::SetCursorScreenPos(ImVec2(headerMax.x - optionsButtonWidth - 8.0f,
                                             headerMin.y + std::max(0.0f, (headerMax.y - headerMin.y - optionsButtonHeight) * 0.5f) + 1.0f));
            if (ImGui::Button(optionsButtonId))
                ImGui::OpenPopup(popupId);
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar();

            return isOpen;
        }

        ProjectScriptFolderNode BuildProjectScriptFolderTree(const std::vector<ProjectNativeScriptInfo>& availableScripts)
        {
            ProjectScriptFolderNode root;
            for (const ProjectNativeScriptInfo& scriptInfo : availableScripts)
            {
                ProjectScriptFolderNode* node = &root;
                const std::filesystem::path folderPath(scriptInfo.FolderRelativePath);
                for (const auto& segment : folderPath)
                {
                    const std::string folderName = segment.string();
                    if (folderName.empty() || folderName == ".")
                        continue;
                    node = &node->Children[folderName];
                }
                node->Scripts.push_back(scriptInfo);
            }
            return root;
        }

        struct AccessMaskUiEntry
        {
            const char* Label;
            SceneSystemAccessComponent Component;
            const char* Tooltip;
        };

        constexpr std::array<AccessMaskUiEntry, 16> kAccessMaskEntries = {
            AccessMaskUiEntry{ "Transform", SceneSystemAccessComponent::Transform, "TransformComponent" },
            AccessMaskUiEntry{ "Hierarchy", SceneSystemAccessComponent::Hierarchy, "HierarchyComponent" },
            AccessMaskUiEntry{ "Rigidbody2D", SceneSystemAccessComponent::Rigidbody2D, "Rigidbody2DComponent" },
            AccessMaskUiEntry{ "Animator", SceneSystemAccessComponent::Animator, "AnimatorComponent and animation event receiver data" },
            AccessMaskUiEntry{ "ParticleEmitter", SceneSystemAccessComponent::ParticleEmitter, "ParticleEmitterComponent" },
            AccessMaskUiEntry{ "NativeScript", SceneSystemAccessComponent::NativeScript, "ScriptComponent state" },
            AccessMaskUiEntry{ "BoxCollider2D", SceneSystemAccessComponent::BoxCollider2D, "BoxCollider2DComponent" },
            AccessMaskUiEntry{ "CircleCollider2D", SceneSystemAccessComponent::CircleCollider2D, "CircleCollider2DComponent" },
            AccessMaskUiEntry{ "Joint2D", SceneSystemAccessComponent::Joint2D, "Joint2DComponent" },
            AccessMaskUiEntry{ "Rendering2D", SceneSystemAccessComponent::Rendering2D, "Sprite/Material and render-facing 2D data" },
            AccessMaskUiEntry{ "Lighting2D", SceneSystemAccessComponent::Lighting2D, "DirectionalLight2D/PointLight2D/ShadowOccluder2D" },
            AccessMaskUiEntry{ "UI", SceneSystemAccessComponent::UI, "Canvas/RectTransform/UI components" },
            AccessMaskUiEntry{ "Audio", SceneSystemAccessComponent::Audio, "AudioListener2D/AudioListener3D/AudioSource" },
            AccessMaskUiEntry{ "Camera", SceneSystemAccessComponent::Camera, "CameraComponent" },
            AccessMaskUiEntry{ "Tilemap", SceneSystemAccessComponent::Tilemap, "Grid2D/TilemapLayer components" },
            AccessMaskUiEntry{ "Metadata", SceneSystemAccessComponent::Metadata, "Tag/prefab metadata and related bookkeeping" }
        };

        void DrawAccessMaskEditor(const char* idSuffix, uint64_t& maskValue)
        {
            ImGui::PushID(idSuffix);
            if (ImGui::Button("All"))
            {
                maskValue = 0;
                for (const auto& entry : kAccessMaskEntries)
                    maskValue |= ToAccessMask(entry.Component);
            }
            ImGui::SameLine();
            if (ImGui::Button("None"))
                maskValue = 0;

            for (const auto& entry : kAccessMaskEntries)
            {
                const uint64_t bit = ToAccessMask(entry.Component);
                bool enabled = (maskValue & bit) != 0;
                if (ImGui::Checkbox(entry.Label, &enabled))
                {
                    if (enabled)
                        maskValue |= bit;
                    else
                        maskValue &= ~bit;
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    ImGui::SetTooltip("%s", entry.Tooltip);
            }
            ImGui::PopID();
        }

        entt::entity FindFirstEntityByTag(const Scene* scene, const std::string& tag)
        {
            if (!scene || tag.empty())
                return entt::null;

            const auto& registry = scene->GetRegistry();
            auto view = registry.view<TagComponent>();
            for (entt::entity entity : view)
            {
                const auto& tagComponent = view.get<TagComponent>(entity);
                if (tagComponent.Tag == tag)
                    return entity;
            }

            return entt::null;
        }

        int CountEntitiesByTag(const Scene* scene, const std::string& tag)
        {
            if (!scene || tag.empty())
                return 0;

            int tagMatchCount = 0;
            const auto& registry = scene->GetRegistry();
            auto view = registry.view<TagComponent>();
            for (entt::entity entity : view)
            {
                const auto& tagComponent = view.get<TagComponent>(entity);
                if (tagComponent.Tag == tag)
                    ++tagMatchCount;
            }
            return tagMatchCount;
        }

        std::string BuildEntityReferencePreviewLabel(const Scene* scene, const ScriptEntityReference& value)
        {
            if (!value.PrefabAssetKey.empty())
            {
                const auto record = Assets::AssetDatabase::GetInstance().FindByKey(value.PrefabAssetKey);
                if (record.IsFailure())
                    return value.PrefabAssetKey + " (Missing Prefab)";
                return EditorAssetNaming::GetAssetDisplayNameFromAssetKey(value.PrefabAssetKey) + " (Prefab)";
            }

            if (value.Tag.empty())
                return "None (Entity)";

            const entt::entity resolvedEntity = FindFirstEntityByTag(scene, value.Tag);
            if (!scene || resolvedEntity == entt::null)
                return value.Tag + " (Missing)";

            return value.Tag + "##" + std::to_string(static_cast<uint32_t>(resolvedEntity));
        }

        std::vector<std::string> BuildPrefabReferencePickerKeys()
        {
            auto isAssetKeyUnderOpenProjectAssets = [](const std::string& assetKey) -> bool {
                if (assetKey.empty())
                    return false;

                const auto& projectManager = Project::ProjectManager::GetInstance();
                if (!projectManager.HasOpenProject())
                    return true;

                const auto resolvedResult = Assets::ResolveAssetKeyToPath(assetKey);
                if (resolvedResult.IsFailure())
                    return false;

                std::error_code ec;
                const std::filesystem::path resolvedPath = std::filesystem::weakly_canonical(resolvedResult.GetValue(), ec);
                if (ec)
                    return false;

                ec.clear();
                if (!std::filesystem::exists(resolvedPath, ec))
                    return false;

                ec.clear();
                const std::filesystem::path assetsRoot = std::filesystem::weakly_canonical(projectManager.GetProjectRoot() / "Assets", ec);
                if (ec)
                    return false;

                ec.clear();
                const std::filesystem::path rel = std::filesystem::relative(resolvedPath, assetsRoot, ec);
                if (ec)
                    return false;
                if (rel.empty())
                    return true;

                const std::string relText = rel.generic_string();
                return !(relText == ".." || relText.rfind("../", 0) == 0);
            };

            std::vector<std::string> keys;
            std::unordered_set<std::string> seen;

            const auto records = Assets::AssetDatabase::GetInstance().GetAllRecords();
            for (const auto& record : records)
            {
                if (record.Type != Assets::AssetType::Prefab || record.Key.empty())
                    continue;
                if (!isAssetKeyUnderOpenProjectAssets(record.Key))
                    continue;
                if (seen.insert(record.Key).second)
                    keys.push_back(record.Key);
            }

            std::sort(keys.begin(), keys.end());
            return keys;
        }

        std::string BuildPrefabReferencePreviewLabel(const ScriptPrefabReference& value)
        {
            if (value.AssetKey.empty())
                return "None (Prefab)";

            const auto record = Assets::AssetDatabase::GetInstance().FindByKey(value.AssetKey);
            if (record.IsFailure())
                return value.AssetKey + " (Missing)";

            return EditorAssetNaming::GetAssetDisplayNameFromAssetKey(value.AssetKey);
        }

        std::string NormalizeSlashes(std::string pathText)
        {
            std::replace(pathText.begin(), pathText.end(), '\\', '/');
            return pathText;
        }

        std::string ToLowerAscii(std::string text)
        {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char character) {
                return static_cast<char>(std::tolower(character));
            });
            return text;
        }

        bool IsNativeScriptAssetKey(const std::string& assetKey)
        {
            if (assetKey.empty())
                return false;

            const std::string lowerKey = ToLowerAscii(NormalizeSlashes(assetKey));
            return lowerKey.rfind("assets/", 0) == 0 &&
                   (lowerKey.ends_with(".h") || lowerKey.ends_with(".cpp"));
        }

        std::string BuildScriptAssetRelativePathFromAssetKey(const std::string& assetKey)
        {
            if (!IsNativeScriptAssetKey(assetKey))
                return {};

            std::string relativePath = NormalizeSlashes(assetKey);
            if (relativePath.rfind("Assets/", 0) == 0)
                relativePath.erase(0, 7);

            std::filesystem::path scriptPath(relativePath);
            scriptPath.replace_extension();
            return scriptPath.generic_string();
        }

        NativeScriptEntry BuildScriptEntryFromAssetKey(const std::string& assetKey)
        {
            NativeScriptEntry scriptEntry{};
            scriptEntry.ScriptAssetRelativePath = BuildScriptAssetRelativePathFromAssetKey(assetKey);
            const std::string requestedClassName = std::filesystem::path(scriptEntry.ScriptAssetRelativePath).stem().string();
            const std::string resolvedClassName = ResolveRegisteredScriptClassNameForInspector(requestedClassName);
            scriptEntry.ScriptClassName = resolvedClassName.empty() ? requestedClassName : resolvedClassName;
            return scriptEntry;
        }

        bool TryAttachScriptAssetToEntity(Scene* scene,
                                          entt::entity ownerEntity,
                                          const std::string& assetKey,
                                          EditorUndoService* undoService)
        {
            if (!scene || !scene->IsValid(ownerEntity) || !IsNativeScriptAssetKey(assetKey))
                return false;

            NativeScriptEntry scriptEntry = BuildScriptEntryFromAssetKey(assetKey);
            if (scriptEntry.ScriptClassName.empty() && scriptEntry.ScriptAssetRelativePath.empty())
                return false;

            if (undoService)
            {
                return undoService->ExecuteSceneMutation("Attach Script Component", [&](Scene& mutableScene) {
                    return mutableScene.AttachScriptComponent(ownerEntity, std::move(scriptEntry)) != entt::null;
                });
            }

            return scene->AttachScriptComponent(ownerEntity, std::move(scriptEntry)) != entt::null;
        }

        const ManagedScriptHost::DiscoveredScriptClass* FindManagedDiscoveredClassForInspector(const std::string& className)
        {
            if (className.empty())
                return nullptr;

            const auto& managedSnapshot = ManagedScriptHost::GetSnapshot();
            const auto iterator = std::find_if(managedSnapshot.Classes.begin(),
                                               managedSnapshot.Classes.end(),
                                               [&](const ManagedScriptHost::DiscoveredScriptClass& discoveredClass) {
                                                   return discoveredClass.FullName == className;
                                               });
            if (iterator == managedSnapshot.Classes.end())
                return nullptr;

            return &(*iterator);
        }

        bool SynchronizeManagedExposedPropertiesForInspector(ManagedScriptEntry& managedScript,
                                                             std::vector<std::string>& outFieldOrder,
                                                             std::string& outError)
        {
            outFieldOrder.clear();
            outError.clear();

            if (managedScript.ScriptClassName.empty())
                return true;

            const ManagedScriptHost::DiscoveredScriptClass* discoveredClass = FindManagedDiscoveredClassForInspector(managedScript.ScriptClassName);
            if (discoveredClass == nullptr)
            {
                outError = "Managed class '" + managedScript.ScriptClassName + "' is not discovered in the active managed payload.";
                return false;
            }

            outFieldOrder.reserve(discoveredClass->ReflectedFields.size());
            for (const auto& field : discoveredClass->ReflectedFields)
            {
                outFieldOrder.push_back(field.Name);

                const auto found = managedScript.ExposedProperties.find(field.Name);
                if (found == managedScript.ExposedProperties.end())
                {
                    managedScript.ExposedProperties.emplace(field.Name, field.DefaultValue);
                    continue;
                }

                if (found->second.index() == field.DefaultValue.index())
                    continue;

                if (std::holds_alternative<ScriptEntityReference>(field.DefaultValue))
                {
                    if (const auto* legacyPrefab = std::get_if<Prefab>(&found->second))
                    {
                        ScriptEntityReference migratedReference{};
                        migratedReference.PrefabAssetKey = legacyPrefab->AssetKey;
                        found->second = std::move(migratedReference);
                        continue;
                    }
                }

                found->second = field.DefaultValue;
            }

            return true;
        }

        bool ScriptPropertyValuesEqual(const ScriptPropertyValue& left, const ScriptPropertyValue& right)
        {
            if (left.index() != right.index())
                return false;

            if (const auto* floatValue = std::get_if<float>(&left))
                return *floatValue == std::get<float>(right);
            if (const auto* integerValue = std::get_if<int32_t>(&left))
                return *integerValue == std::get<int32_t>(right);
            if (const auto* booleanValue = std::get_if<bool>(&left))
                return *booleanValue == std::get<bool>(right);
            if (const auto* vectorValue = std::get_if<glm::vec3>(&left))
            {
                const glm::vec3& rightValue = std::get<glm::vec3>(right);
                return vectorValue->x == rightValue.x && vectorValue->y == rightValue.y && vectorValue->z == rightValue.z;
            }
            if (const auto* stringValue = std::get_if<std::string>(&left))
                return *stringValue == std::get<std::string>(right);
            if (const auto* entityValue = std::get_if<ScriptEntityReference>(&left))
            {
                const auto& rightValue = std::get<ScriptEntityReference>(right);
                return entityValue->Tag == rightValue.Tag && entityValue->PrefabAssetKey == rightValue.PrefabAssetKey;
            }
            if (const auto* prefabValue = std::get_if<Prefab>(&left))
                return prefabValue->AssetKey == std::get<Prefab>(right).AssetKey;

            return false;
        }

        void IncrementScriptPropertyRevision(ScriptComponent& scriptComponent)
        {
            if (uint64_t* revision = scriptComponent.TryGetRuntimeExposedPropertiesRevision())
                ++(*revision);
        }

        bool UpdateScriptPropertyValue(ScriptComponent& scriptComponent,
                                       const std::string& propertyName,
                                       const ScriptPropertyValue& newValue)
        {
            auto* exposedProperties = scriptComponent.TryGetExposedProperties();
            if (exposedProperties == nullptr)
                return false;

            const auto propertyIterator = exposedProperties->find(propertyName);
            if (propertyIterator == exposedProperties->end())
            {
                exposedProperties->emplace(propertyName, newValue);
                IncrementScriptPropertyRevision(scriptComponent);
                return true;
            }

            if (ScriptPropertyValuesEqual(propertyIterator->second, newValue))
                return true;

            propertyIterator->second = newValue;
            IncrementScriptPropertyRevision(scriptComponent);
            return true;
        }

        void HandleInteractiveScriptPropertyUndo(EditorUndoService* undoService, const std::string& propertyEditLabel)
        {
            if (!undoService)
                return;

            if (ImGui::IsItemActivated())
                undoService->BeginInteractiveSceneMutation();
            if (ImGui::IsItemDeactivatedAfterEdit())
                (void)undoService->CommitInteractiveSceneMutation(propertyEditLabel);
        }

        bool MutateScriptComponent(Scene* scene,
                                   entt::entity scriptComponentEntity,
                                   entt::entity selectedEntity,
                                   EditorUndoService* undoService,
                                   const std::string& mutationLabel,
                                   const std::function<bool(ScriptComponent&)>& mutation)
        {
            if (undoService)
            {
                return undoService->ExecuteSceneMutation(mutationLabel, [&](Scene& mutableScene) {
                    auto* mutableScriptComponent = mutableScene.GetScriptComponent(scriptComponentEntity);
                    if (!mutableScriptComponent || mutableScriptComponent->OwnerEntity != selectedEntity)
                        return false;
                    return mutation(*mutableScriptComponent);
                });
            }

            auto* mutableScriptComponent = scene ? scene->GetScriptComponent(scriptComponentEntity) : nullptr;
            if (!mutableScriptComponent || mutableScriptComponent->OwnerEntity != selectedEntity)
                return false;
            return mutation(*mutableScriptComponent);
        }

        void DrawExposedScriptPropertyEditors(Scene* scene,
                                              entt::entity selectedEntity,
                                              entt::entity scriptComponentEntity,
                                              ScriptComponent& scriptComponent,
                                              const std::vector<std::string>& declaredFieldNames,
                                              EditorUndoService* undoService)
        {
            auto* exposedProperties = scriptComponent.TryGetExposedProperties();
            if (exposedProperties == nullptr)
                return;

            for (const std::string& propertyName : declaredFieldNames)
            {
                auto propertyIterator = exposedProperties->find(propertyName);
                if (propertyIterator == exposedProperties->end())
                    continue;

                auto& propertyValue = propertyIterator->second;
                const std::string propertyEditLabel = "Edit Script Property: " + propertyName;
                ImGui::PushID(propertyName.c_str());
                ImGui::TextUnformatted(propertyName.c_str());
                if (auto* floatValue = std::get_if<float>(&propertyValue))
                {
                    if (ImGui::DragFloat("##ScriptPropertyValue", floatValue, 0.1f))
                        IncrementScriptPropertyRevision(scriptComponent);
                    HandleInteractiveScriptPropertyUndo(undoService, propertyEditLabel);
                }
                else if (auto* integerValue = std::get_if<int32_t>(&propertyValue))
                {
                    if (ImGui::DragInt("##ScriptPropertyValue", integerValue, 1.0f))
                        IncrementScriptPropertyRevision(scriptComponent);
                    HandleInteractiveScriptPropertyUndo(undoService, propertyEditLabel);
                }
                else if (auto* booleanValue = std::get_if<bool>(&propertyValue))
                {
                    if (ImGui::Checkbox("##ScriptPropertyValue", booleanValue))
                        IncrementScriptPropertyRevision(scriptComponent);
                    HandleInteractiveScriptPropertyUndo(undoService, propertyEditLabel);
                }
                else if (auto* vectorValue = std::get_if<glm::vec3>(&propertyValue))
                {
                    if (ImGui::DragFloat3("##ScriptPropertyValue", &vectorValue->x, 0.1f))
                        IncrementScriptPropertyRevision(scriptComponent);
                    HandleInteractiveScriptPropertyUndo(undoService, propertyEditLabel);
                }
                else if (auto* stringValue = std::get_if<std::string>(&propertyValue))
                {
                    std::array<char, 256> textBuffer{};
                    std::snprintf(textBuffer.data(), textBuffer.size(), "%s", stringValue->c_str());
                    if (ImGui::InputText("##ScriptPropertyValue", textBuffer.data(), textBuffer.size()))
                    {
                        *stringValue = textBuffer.data();
                        IncrementScriptPropertyRevision(scriptComponent);
                    }
                    HandleInteractiveScriptPropertyUndo(undoService, propertyEditLabel);
                }
                else if (auto* entityValue = std::get_if<ScriptEntityReference>(&propertyValue))
                {
                    auto assignEntityReference = [&](const std::string& tagValue, const std::string& prefabAssetKeyValue) {
                        ScriptEntityReference referenceValue{};
                        referenceValue.Tag = tagValue;
                        referenceValue.PrefabAssetKey = prefabAssetKeyValue;
                        return MutateScriptComponent(scene,
                                                     scriptComponentEntity,
                                                     selectedEntity,
                                                     undoService,
                                                     propertyEditLabel,
                                                     [&](ScriptComponent& mutableScriptComponent) {
                                                         return UpdateScriptPropertyValue(mutableScriptComponent, propertyName, referenceValue);
                                                     });
                    };

                    const std::string previewLabel = BuildEntityReferencePreviewLabel(scene, *entityValue);
                    if (ImGui::BeginCombo("##ScriptPropertyValue", previewLabel.c_str()))
                    {
                        const bool noneSelected = entityValue->Tag.empty() && entityValue->PrefabAssetKey.empty();
                        if (ImGui::Selectable("None (Entity/Prefab)", noneSelected))
                            (void)assignEntityReference({}, {});
                        if (noneSelected)
                            ImGui::SetItemDefaultFocus();

                        ImGui::Separator();
                        ImGui::TextDisabled("Scene Entities");
                        if (scene)
                        {
                            const auto& sceneRegistry = scene->GetRegistry();
                            auto entityView = sceneRegistry.view<TagComponent>();
                            for (entt::entity candidateEntity : entityView)
                            {
                                const auto& candidateTag = entityView.get<TagComponent>(candidateEntity).Tag;
                                const bool isSelected = entityValue->PrefabAssetKey.empty() && candidateTag == entityValue->Tag;
                                std::string optionLabel = candidateTag.empty() ? "Entity" : candidateTag;
                                optionLabel += "##EntityReferenceOption_" + std::to_string(static_cast<uint32_t>(candidateEntity));
                                if (ImGui::Selectable(optionLabel.c_str(), isSelected))
                                    (void)assignEntityReference(candidateTag, {});
                                if (isSelected)
                                    ImGui::SetItemDefaultFocus();
                            }
                        }

                        ImGui::Separator();
                        ImGui::TextDisabled("Prefab Assets");
                        const std::vector<std::string> prefabKeys = BuildPrefabReferencePickerKeys();
                        for (const std::string& prefabKey : prefabKeys)
                        {
                            const bool isSelected = prefabKey == entityValue->PrefabAssetKey;
                            const std::string displayName = EditorAssetNaming::GetAssetDisplayNameFromAssetKey(prefabKey);
                            if (ImGui::Selectable((displayName + "##EntityReferencePrefabOption_" + prefabKey).c_str(), isSelected))
                                (void)assignEntityReference({}, prefabKey);
                            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                                ImGui::SetTooltip("%s", prefabKey.c_str());
                            if (isSelected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }

                    if (ImGui::BeginDragDropTarget())
                    {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kSceneEntityPayload))
                        {
                            if (scene && payload->Data && payload->DataSize == sizeof(entt::entity))
                            {
                                const entt::entity droppedEntity = *static_cast<const entt::entity*>(payload->Data);
                                if (scene->IsValid(droppedEntity))
                                {
                                    if (const auto* tagComponent = scene->GetRegistry().try_get<TagComponent>(droppedEntity))
                                        (void)assignEntityReference(tagComponent->Tag, {});
                                }
                            }
                        }
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PREFAB"))
                        {
                            if (payload->Data && payload->DataSize > 0)
                            {
                                const char* prefabKey = static_cast<const char*>(payload->Data);
                                if (prefabKey && prefabKey[0])
                                    (void)assignEntityReference({}, prefabKey);
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }

                    if (!entityValue->Tag.empty() || !entityValue->PrefabAssetKey.empty())
                    {
                        ImGui::SameLine();
                        if (ImGui::Button("X##ClearEntityReference"))
                            (void)assignEntityReference({}, {});
                    }

                    const int matchingTagCount = CountEntitiesByTag(scene, entityValue->Tag);
                    if (entityValue->PrefabAssetKey.empty() && !entityValue->Tag.empty() && matchingTagCount > 1)
                    {
                        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f),
                                           "Tag '%s' matches %d entities. Entity references by tag require unique tags.",
                                           entityValue->Tag.c_str(),
                                           matchingTagCount);
                    }
                }
                else if (auto* prefabValue = std::get_if<ScriptPrefabReference>(&propertyValue))
                {
                    ImGui::TextDisabled("Legacy prefab field. Prefer Limitless::Entity for new script references.");
                    auto assignPrefabKey = [&](const std::string& prefabKey) {
                        return MutateScriptComponent(scene,
                                                     scriptComponentEntity,
                                                     selectedEntity,
                                                     undoService,
                                                     propertyEditLabel,
                                                     [&](ScriptComponent& mutableScriptComponent) {
                                                         return UpdateScriptPropertyValue(mutableScriptComponent,
                                                                                          propertyName,
                                                                                          ScriptPrefabReference{ prefabKey });
                                                     });
                    };

                    const std::string previewLabel = BuildPrefabReferencePreviewLabel(*prefabValue);
                    if (ImGui::BeginCombo("##ScriptPropertyValue", previewLabel.c_str()))
                    {
                        const bool noneSelected = prefabValue->AssetKey.empty();
                        if (ImGui::Selectable("None (Prefab)", noneSelected))
                            (void)assignPrefabKey({});
                        if (noneSelected)
                            ImGui::SetItemDefaultFocus();

                        const std::vector<std::string> prefabKeys = BuildPrefabReferencePickerKeys();
                        for (const std::string& prefabKey : prefabKeys)
                        {
                            const bool isSelected = prefabKey == prefabValue->AssetKey;
                            const std::string displayName = EditorAssetNaming::GetAssetDisplayNameFromAssetKey(prefabKey);
                            if (ImGui::Selectable((displayName + "##PrefabReferenceOption_" + prefabKey).c_str(), isSelected))
                                (void)assignPrefabKey(prefabKey);
                            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                                ImGui::SetTooltip("%s", prefabKey.c_str());
                            if (isSelected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }

                    if (ImGui::BeginDragDropTarget())
                    {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PREFAB"))
                        {
                            if (payload->Data && payload->DataSize > 0)
                            {
                                const char* prefabKey = static_cast<const char*>(payload->Data);
                                if (prefabKey && prefabKey[0])
                                    (void)assignPrefabKey(prefabKey);
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }

                    if (!prefabValue->AssetKey.empty())
                    {
                        ImGui::SameLine();
                        if (ImGui::Button("X##ClearPrefabReference"))
                            (void)assignPrefabKey({});
                    }
                }
                ImGui::PopID();
            }
        }
    }

    void DrawScriptComponentSections(Scene* scene,
                                     entt::registry& registry,
                                     entt::entity selectedEntity,
                                     EditorUndoService* undoService)
    {
        (void)registry;

        if (!scene || selectedEntity == entt::null || !scene->IsValid(selectedEntity))
            return;

        auto scriptComponentEntities = scene->GetScriptComponentEntities(selectedEntity);
        const auto refreshScriptEntities = [&]() {
            scriptComponentEntities = scene->GetScriptComponentEntities(selectedEntity);
        };

        const std::vector<std::string> registeredScriptNames = NativeScriptRegistry::GetRegisteredScriptNames();
        const std::vector<ProjectNativeScriptInfo> availableScripts = GetAvailableProjectScriptsForInspector();
        const ProjectScriptFolderNode scriptFolderTree = BuildProjectScriptFolderTree(availableScripts);

        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kAssetMovePayload))
            {
                if (payload->Data && payload->DataSize > 0)
                {
                    const std::string assetKey(static_cast<const char*>(payload->Data), static_cast<size_t>(std::max(0, payload->DataSize - 1)));
                    if (TryAttachScriptAssetToEntity(scene, selectedEntity, assetKey, undoService))
                        refreshScriptEntities();
                }
            }
            ImGui::EndDragDropTarget();
        }

        if (registeredScriptNames.empty())
        {
            static bool attemptedAutoScriptBuild = false;
            if (!attemptedAutoScriptBuild &&
                !IsNativeScriptBuildInProgress() &&
                HasAnyProjectNativeScriptSourcesForInspector())
            {
                attemptedAutoScriptBuild = TriggerNativeScriptBuildFromInspector();
            }

            if (availableScripts.empty())
                ImGui::TextDisabled("No scripts found.");
            else
                ImGui::TextDisabled("No scripts registered yet.");
            ImGui::TextWrapped("Detected scripts under project Assets are not compiled into ScriptCore yet.");
            ImGui::BeginDisabled(IsNativeScriptBuildInProgress());
            if (ImGui::Button("Build ScriptCore From Project Scripts", ImVec2(-1.0f, 0.0f)))
                (void)TriggerNativeScriptBuildFromInspector();
            ImGui::EndDisabled();
            if (IsNativeScriptBuildInProgress())
                ImGui::TextDisabled("Building ScriptCore...");
        }

        if (scriptComponentEntities.empty())
        {
            ImGui::TextDisabled("No script components attached.");
        }
        else
        {
            entt::entity scriptComponentToRemove = entt::null;
            for (entt::entity scriptComponentEntity : scriptComponentEntities)
            {
                auto* scriptComponent = scene->GetScriptComponent(scriptComponentEntity);
                if (!scriptComponent || scriptComponent->OwnerEntity != selectedEntity)
                    continue;

                if (ManagedScriptEntry* managedScriptEntry = scriptComponent->TryGetManagedEntry())
                {
                    ImGui::PushID(static_cast<int>(static_cast<uint32_t>(scriptComponentEntity)));
                    std::string scriptLabel = managedScriptEntry->ScriptClassName.empty()
                        ? ("Managed Script Component " + std::to_string(scriptComponent->ComponentOrder + 1))
                        : managedScriptEntry->ScriptClassName;
                    const bool scriptOpen = BeginInspectorScriptSectionHeader(scriptLabel.c_str(), "ScriptComponentOptions", "...##ScriptComponentOptionsButton");
                    if (ImGui::BeginPopup("ScriptComponentOptions"))
                    {
                        if (ImGui::MenuItem("Remove Component"))
                            scriptComponentToRemove = scriptComponentEntity;
                        ImGui::EndPopup();
                    }

                    if (scriptOpen)
                    {
                        const auto mutateManagedScriptEntry = [&](const char* mutationLabel, auto&& mutation) {
                            if (undoService)
                            {
                                return undoService->ExecuteSceneMutation(mutationLabel, [&](Scene& mutableScene) {
                                    auto* mutableScriptComponent = mutableScene.GetScriptComponent(scriptComponentEntity);
                                    if (!mutableScriptComponent || mutableScriptComponent->OwnerEntity != selectedEntity)
                                        return false;
                                    ManagedScriptEntry* mutableManagedEntry = mutableScriptComponent->TryGetManagedEntry();
                                    if (!mutableManagedEntry)
                                        return false;
                                    mutation(*mutableManagedEntry);
                                    return true;
                                });
                            }

                            auto* mutableScriptComponent = scene->GetScriptComponent(scriptComponentEntity);
                            if (!mutableScriptComponent || mutableScriptComponent->OwnerEntity != selectedEntity)
                                return false;
                            ManagedScriptEntry* mutableManagedEntry = mutableScriptComponent->TryGetManagedEntry();
                            if (!mutableManagedEntry)
                                return false;
                            mutation(*mutableManagedEntry);
                            return true;
                        };

                        ImGui::TextUnformatted("Enabled");
                        ImGui::Checkbox("##ManagedScriptEnabled", &managedScriptEntry->Enabled);

                        std::string previewLabel = managedScriptEntry->ScriptClassName.empty()
                            ? std::string("None")
                            : (!managedScriptEntry->ScriptAssetRelativePath.empty() ? managedScriptEntry->ScriptAssetRelativePath : managedScriptEntry->ScriptClassName);
                        ImGui::TextUnformatted("Class");
                        if (ImGui::BeginCombo("##ManagedScriptClass", previewLabel.c_str()))
                        {
                            const bool noneSelected = managedScriptEntry->ScriptClassName.empty();
                            if (ImGui::Selectable("None", noneSelected))
                            {
                                (void)mutateManagedScriptEntry("Change Managed Script Class", [&](ManagedScriptEntry& mutableEntry) {
                                    mutableEntry.ScriptClassName.clear();
                                    mutableEntry.ScriptAssetRelativePath.clear();
                                    mutableEntry.ExposedProperties.clear();
                                    mutableEntry.RuntimeExposedPropertiesRevision = 1;
                                    mutableEntry.RuntimeInstanceId = 0;
                                    mutableEntry.RuntimeInitialized = false;
                                    mutableEntry.RuntimeUpdateCount = 0;
                                    mutableEntry.RuntimeWarnedMissingHost = false;
                                    mutableEntry.RuntimeWarnedMissingClass = false;
                                });
                            }
                            if (noneSelected)
                                ImGui::SetItemDefaultFocus();

                            const auto& managedSnapshot = ManagedScriptHost::GetSnapshot();
                            for (const auto& discoveredClass : managedSnapshot.Classes)
                            {
                                const bool classSelected = managedScriptEntry->ScriptClassName == discoveredClass.FullName;
                                if (ImGui::Selectable(discoveredClass.FullName.c_str(), classSelected))
                                {
                                    (void)mutateManagedScriptEntry("Change Managed Script Class", [&](ManagedScriptEntry& mutableEntry) {
                                        mutableEntry.ScriptClassName = discoveredClass.FullName;
                                        mutableEntry.ScriptAssetRelativePath = discoveredClass.AssemblyPath.filename().generic_string();
                                        mutableEntry.ExposedProperties.clear();
                                        mutableEntry.RuntimeExposedPropertiesRevision = 1;
                                        mutableEntry.RuntimeInstanceId = 0;
                                        mutableEntry.RuntimeInitialized = false;
                                        mutableEntry.RuntimeUpdateCount = 0;
                                        mutableEntry.RuntimeWarnedMissingHost = false;
                                        mutableEntry.RuntimeWarnedMissingClass = false;
                                    });
                                }
                                if (classSelected)
                                    ImGui::SetItemDefaultFocus();
                            }
                            ImGui::EndCombo();
                        }

                        if (!managedScriptEntry->ScriptAssetRelativePath.empty())
                            ImGui::TextDisabled("Assembly: %s", managedScriptEntry->ScriptAssetRelativePath.c_str());
                        if (managedScriptEntry->RuntimeWarnedMissingHost)
                            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "Managed host is unavailable.");
                        if (managedScriptEntry->RuntimeWarnedMissingClass)
                            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "Selected managed class is not discovered in the current payload.");
                        ImGui::TextDisabled("Runtime updates: %llu",
                                            static_cast<unsigned long long>(managedScriptEntry->RuntimeUpdateCount));

                        ImGui::Separator();
                        std::vector<std::string> declaredFieldNames;
                        std::string fieldSyncError;
                        const bool syncedFromScript = SynchronizeManagedExposedPropertiesForInspector(*managedScriptEntry, declaredFieldNames, fieldSyncError);
                        ImGui::TextUnformatted("Exposed Variables");
                        if (managedScriptEntry->ScriptClassName.empty())
                        {
                            ImGui::TextDisabled("Assign a managed script class to view exposed variables.");
                        }
                        else if (!syncedFromScript)
                        {
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.45f, 1.0f));
                            ImGui::TextWrapped("%s", fieldSyncError.c_str());
                            ImGui::PopStyleColor();
                            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                            ImGui::TextWrapped("Supported managed public field types: float, int, bool, string, Limitless.Managed.Vector3, Limitless.Managed.Entity.");
                            ImGui::PopStyleColor();
                        }
                        else if (declaredFieldNames.empty())
                        {
                            ImGui::TextDisabled("No supported public fields found on this managed script.");
                        }
                        else
                        {
                            DrawExposedScriptPropertyEditors(scene,
                                                             selectedEntity,
                                                             scriptComponentEntity,
                                                             *scriptComponent,
                                                             declaredFieldNames,
                                                             undoService);
                        }
                        ImGui::TreePop();
                    }

                    ImGui::PopID();
                    continue;
                }

                auto& scriptEntry = scriptComponent->Script;
                ImGui::PushID(static_cast<int>(static_cast<uint32_t>(scriptComponentEntity)));
                std::string scriptLabel = scriptEntry.ScriptClassName.empty()
                    ? ("Script Component " + std::to_string(scriptComponent->ComponentOrder + 1))
                    : scriptEntry.ScriptClassName;
                const bool scriptOpen = BeginInspectorScriptSectionHeader(scriptLabel.c_str(), "ScriptComponentOptions", "...##ScriptComponentOptionsButton");
                if (ImGui::BeginPopup("ScriptComponentOptions"))
                {
                    bool showDebugInfo = GetNativeScriptDebugInfoEnabled();
                    if (ImGui::MenuItem("Show Debug Info", nullptr, &showDebugInfo))
                        SetNativeScriptDebugInfoEnabled(showDebugInfo);
                    ImGui::Separator();
                    if (ImGui::MenuItem("Remove Component"))
                        scriptComponentToRemove = scriptComponentEntity;
                    ImGui::EndPopup();
                }

                if (scriptOpen)
                {
                    const auto mutateScriptEntry = [&](const char* mutationLabel, auto&& mutation) {
                        if (undoService)
                        {
                            return undoService->ExecuteSceneMutation(mutationLabel, [&](Scene& mutableScene) {
                                auto* mutableScriptComponent = mutableScene.GetScriptComponent(scriptComponentEntity);
                                if (!mutableScriptComponent || mutableScriptComponent->OwnerEntity != selectedEntity)
                                    return false;
                                mutation(mutableScriptComponent->Script);
                                return true;
                            });
                        }

                        auto* mutableScriptComponent = scene->GetScriptComponent(scriptComponentEntity);
                        if (!mutableScriptComponent || mutableScriptComponent->OwnerEntity != selectedEntity)
                            return false;
                        mutation(mutableScriptComponent->Script);
                        return true;
                    };

                    ImGui::TextUnformatted("Enabled");
                    ImGui::Checkbox("##NativeScriptEnabled", &scriptEntry.Enabled);

                    std::string previewLabel = scriptEntry.ScriptClassName.empty()
                        ? std::string("None")
                        : (!scriptEntry.ScriptAssetRelativePath.empty() ? scriptEntry.ScriptAssetRelativePath : scriptEntry.ScriptClassName);
                    ImGui::TextUnformatted("Class");
                    if (ImGui::BeginCombo("##NativeScriptClass", previewLabel.c_str()))
                    {
                        const bool noneSelected = scriptEntry.ScriptClassName.empty();
                        if (ImGui::Selectable("None", noneSelected))
                        {
                            (void)mutateScriptEntry("Change Script Class", [&](NativeScriptEntry& mutableEntry) {
                                scriptEntry.ScriptClassName.clear();
                                mutableEntry.ScriptClassName.clear();
                                mutableEntry.ScriptAssetRelativePath.clear();
                                mutableEntry.ExposedProperties.clear();
                                mutableEntry.RuntimeInitialized = false;
                                mutableEntry.RuntimeInstance.reset();
                            });
                        }
                        if (noneSelected)
                            ImGui::SetItemDefaultFocus();

                        std::function<void(const ProjectScriptFolderNode&)> drawFolderContents;
                        drawFolderContents = [&](const ProjectScriptFolderNode& folderNode) {
                            for (const ProjectNativeScriptInfo& scriptInfo : folderNode.Scripts)
                            {
                                const bool scriptSelected = (scriptEntry.ScriptAssetRelativePath == scriptInfo.ScriptAssetRelativePath) ||
                                                            (scriptEntry.ScriptAssetRelativePath.empty() && scriptEntry.ScriptClassName == scriptInfo.ScriptClassName);
                                if (ImGui::Selectable(scriptInfo.DisplayName.c_str(), scriptSelected))
                                {
                                    const std::string resolvedScriptClassName = ResolveRegisteredScriptClassNameForInspector(scriptInfo.ScriptClassName);
                                    const std::string& assignedScriptClassName = resolvedScriptClassName.empty() ? scriptInfo.ScriptClassName : resolvedScriptClassName;
                                    (void)mutateScriptEntry("Change Script Class", [&](NativeScriptEntry& mutableEntry) {
                                        scriptEntry.ScriptClassName = assignedScriptClassName;
                                        scriptEntry.ScriptAssetRelativePath = scriptInfo.ScriptAssetRelativePath;
                                        mutableEntry.ScriptClassName = assignedScriptClassName;
                                        mutableEntry.ScriptAssetRelativePath = scriptInfo.ScriptAssetRelativePath;
                                        mutableEntry.ExposedProperties.clear();
                                        mutableEntry.RuntimeInitialized = false;
                                        mutableEntry.RuntimeInstance.reset();
                                    });
                                }
                                if (scriptSelected)
                                    ImGui::SetItemDefaultFocus();
                            }

                            for (const auto& [folderName, childNode] : folderNode.Children)
                            {
                                if (!ImGui::BeginMenu(folderName.c_str()))
                                    continue;
                                drawFolderContents(childNode);
                                ImGui::EndMenu();
                            }
                        };

                        drawFolderContents(scriptFolderTree);
                        ImGui::EndCombo();
                    }

                    if (ImGui::BeginDragDropTarget())
                    {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kAssetMovePayload))
                        {
                            if (payload->Data && payload->DataSize > 0)
                            {
                                const std::string assetKey(static_cast<const char*>(payload->Data), static_cast<size_t>(std::max(0, payload->DataSize - 1)));
                                if (IsNativeScriptAssetKey(assetKey))
                                {
                                    const NativeScriptEntry droppedScriptEntry = BuildScriptEntryFromAssetKey(assetKey);
                                    (void)mutateScriptEntry("Assign Script Asset", [&](NativeScriptEntry& mutableEntry) {
                                        scriptEntry.ScriptClassName = droppedScriptEntry.ScriptClassName;
                                        mutableEntry.ScriptClassName = droppedScriptEntry.ScriptClassName;
                                        mutableEntry.ScriptAssetRelativePath = droppedScriptEntry.ScriptAssetRelativePath;
                                        mutableEntry.ExposedProperties.clear();
                                        mutableEntry.RuntimeInitialized = false;
                                        mutableEntry.RuntimeInstance.reset();
                                    });
                                }
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }

                    const std::string resolvedSelectedClassName = ResolveRegisteredScriptClassNameForInspector(scriptEntry.ScriptClassName);
                    if (!resolvedSelectedClassName.empty() && resolvedSelectedClassName != scriptEntry.ScriptClassName)
                        scriptEntry.ScriptClassName = resolvedSelectedClassName;

                    const bool selectedClassCompiled =
                        scriptEntry.ScriptClassName.empty() || !ResolveRegisteredScriptClassNameForInspector(scriptEntry.ScriptClassName).empty();
                    if (!selectedClassCompiled)
                    {
                        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f),
                                           "Selected script is discovered in Assets but not compiled yet.");
                        if (ImGui::Button("Build ScriptCore From Project Scripts"))
                            (void)TriggerNativeScriptBuildFromInspector();
                    }

                    const bool executionAndAccessOpen = ImGui::TreeNodeEx("Execution & Access", ImGuiTreeNodeFlags_DefaultOpen);
                    if (executionAndAccessOpen)
                    {
                        ImGui::TextUnformatted("Execution Policy");
                        int executionPolicyIndex = (scriptEntry.ExecutionPolicy == ScriptExecutionPolicy::ParallelSafe) ? 1 : 0;
                        constexpr const char* executionPolicyOptions[] = { "MainThread", "ParallelSafe" };
                        if (ImGui::Combo("##NativeScriptExecutionPolicy", &executionPolicyIndex, executionPolicyOptions, IM_ARRAYSIZE(executionPolicyOptions)))
                        {
                            scriptEntry.ExecutionPolicy =
                                (executionPolicyIndex == 1) ? ScriptExecutionPolicy::ParallelSafe : ScriptExecutionPolicy::MainThread;
                        }
                        ImGui::TextDisabled("ParallelSafe scripts should declare accurate read/write access masks.");

                        const bool readAccessOpen = ImGui::TreeNodeEx("Declared Read Access", ImGuiTreeNodeFlags_DefaultOpen);
                        if (readAccessOpen)
                        {
                            DrawAccessMaskEditor("NativeScriptDeclaredReadAccessMask", scriptEntry.DeclaredReadAccessMask);
                            ImGui::TreePop();
                        }

                        const bool writeAccessOpen = ImGui::TreeNodeEx("Declared Write Access", ImGuiTreeNodeFlags_DefaultOpen);
                        if (writeAccessOpen)
                        {
                            DrawAccessMaskEditor("NativeScriptDeclaredWriteAccessMask", scriptEntry.DeclaredWriteAccessMask);
                            ImGui::TreePop();
                        }

                        ImGui::TextDisabled("Tip: masks of 0 use script defaults from LT_DECLARE_SCRIPT_ACCESS at runtime.");

                        const bool hasCompiledClass = !scriptEntry.ScriptClassName.empty() &&
                                                      NativeScriptRegistry::HasScript(scriptEntry.ScriptClassName);
                        ImGui::BeginDisabled(!hasCompiledClass);
                        if (ImGui::Button("Apply Script Declared Defaults"))
                        {
                            if (auto scriptDefaults = NativeScriptRegistry::CreateScript(scriptEntry.ScriptClassName))
                            {
                                scriptEntry.DeclaredReadAccessMask = scriptDefaults->GetDeclaredReadAccessMask();
                                scriptEntry.DeclaredWriteAccessMask = scriptDefaults->GetDeclaredWriteAccessMask();
                            }
                        }
                        ImGui::EndDisabled();
                        ImGui::SameLine();
                        if (ImGui::Button("Clear Access Masks"))
                        {
                            scriptEntry.DeclaredReadAccessMask = 0;
                            scriptEntry.DeclaredWriteAccessMask = 0;
                        }

                        ImGui::TreePop();
                    }

                    if (!scriptEntry.ScriptAssetRelativePath.empty())
                        ImGui::TextDisabled("Asset: Assets/%s", scriptEntry.ScriptAssetRelativePath.c_str());
                    if (GetNativeScriptDebugInfoEnabled())
                        ImGui::TextDisabled("Runtime updates: %llu",
                                            static_cast<unsigned long long>(scriptEntry.RuntimeUpdateCount));

                    ImGui::Separator();
                    std::vector<std::string> declaredFieldNames;
                    std::string fieldSyncError;
                    const bool syncedFromScript = SynchronizeExposedPropertiesFromScriptForInspector(scriptEntry, declaredFieldNames, fieldSyncError);

                    ImGui::TextUnformatted("Exposed Variables");
                    if (scriptEntry.ScriptClassName.empty())
                    {
                        ImGui::TextDisabled("Assign a script class to view exposed variables.");
                    }
                    else if (!syncedFromScript)
                    {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.45f, 1.0f));
                        ImGui::TextWrapped("%s", fieldSyncError.c_str());
                        ImGui::PopStyleColor();
                        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                        ImGui::TextWrapped("Supported public field types: float, int/int32_t, bool, glm::vec3, std::string, Limitless::Entity.");
                        ImGui::TextWrapped("Legacy (deprecated): Limitless::Prefab / Limitless::ScriptPrefabReference.");
                        ImGui::PopStyleColor();
                    }
                    else if (declaredFieldNames.empty())
                    {
                        ImGui::TextDisabled("No supported public fields found on this script.");
                    }
                    else
                    {
                        DrawExposedScriptPropertyEditors(scene,
                                                         selectedEntity,
                                                         scriptComponentEntity,
                                                         *scriptComponent,
                                                         declaredFieldNames,
                                                         undoService);
                    }

                    ImGui::TreePop();
                }
                ImGui::PopID();
            }

            if (scriptComponentToRemove != entt::null)
            {
                if (undoService)
                {
                    (void)undoService->ExecuteSceneMutation("Remove Script Component", [&](Scene& mutableScene) {
                        return mutableScene.RemoveScriptComponent(scriptComponentToRemove);
                    });
                }
                else
                {
                    (void)scene->RemoveScriptComponent(scriptComponentToRemove);
                }
            }
        }
    }
}
