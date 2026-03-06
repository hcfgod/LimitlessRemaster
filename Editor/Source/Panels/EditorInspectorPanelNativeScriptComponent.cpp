#include "EditorInspectorPanelNativeScriptComponent.h"

#include "EditorAssetNaming.h"
#include "EditorInspectorPanelNativeScriptEditor.h"
#include "Undo/EditorUndoService.h"
#include "Assets/AssetDatabase.h"
#include "Assets/AssetPaths.h"
#include "Project/ProjectManager.h"
#include "Scene/Scene.h"
#include "Scene/SceneSystemScheduler.h"
#include "Scripting/NativeScriptRegistry.h"
#include "imgui/imgui.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <unordered_set>

namespace Limitless::EditorInspectorPanel
{
    namespace
    {
        constexpr const char* kSceneEntityPayload = "SCENE_ENTITY";

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
            AccessMaskUiEntry{ "NativeScript", SceneSystemAccessComponent::NativeScript, "NativeScriptComponent and script entry state" },
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
    }

    void DrawNativeScriptComponentSection(Scene* scene,
                                          entt::registry& registry,
                                          entt::entity selectedEntity,
                                          EditorUndoService* undoService,
                                          bool& outRemoveNativeScriptComponent)
    {
        outRemoveNativeScriptComponent = false;

        auto* nativeScript = registry.try_get<NativeScriptComponent>(selectedEntity);
        if (!nativeScript)
            return;

        const bool nativeScriptOpen = ImGui::TreeNodeEx("Native Script", ImGuiTreeNodeFlags_DefaultOpen);
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
            ImGui::OpenPopup("NativeScriptComponentOptions");
        ImGui::SameLine();
        if (ImGui::Button("...##NativeScriptComponentOptionsButton"))
            ImGui::OpenPopup("NativeScriptComponentOptions");

        if (ImGui::BeginPopup("NativeScriptComponentOptions"))
        {
            bool showDebugInfo = GetNativeScriptDebugInfoEnabled();
            if (ImGui::MenuItem("Show Debug Info", nullptr, &showDebugInfo))
                SetNativeScriptDebugInfoEnabled(showDebugInfo);
            ImGui::Separator();
            if (ImGui::MenuItem("Remove Component"))
                outRemoveNativeScriptComponent = true;
            ImGui::EndPopup();
        }

        if (!nativeScriptOpen)
            return;

        const std::vector<std::string> registeredScriptNames = NativeScriptRegistry::GetRegisteredScriptNames();
        const auto discoveredScriptNames = DiscoverNativeScriptClassNamesFromProjectAssetsForInspector();
        std::vector<std::string> availableScriptNames = registeredScriptNames.empty()
            ? discoveredScriptNames
            : registeredScriptNames;
        if (ImGui::Button("Add Script", ImVec2(-1.0f, 0.0f)))
        {
            if (undoService)
            {
                (void)undoService->ExecuteSceneMutation("Add Script Entry", [&](Scene& mutableScene) {
                    auto* mutableNativeScript = mutableScene.GetRegistry().try_get<NativeScriptComponent>(selectedEntity);
                    if (!mutableNativeScript)
                        return false;
                    mutableNativeScript->Scripts.emplace_back();
                    return true;
                });
                nativeScript = registry.try_get<NativeScriptComponent>(selectedEntity);
            }
            else
            {
                nativeScript->Scripts.emplace_back();
            }
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

            if (availableScriptNames.empty())
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
                    ImGui::TextUnformatted("Enabled");
                    ImGui::Checkbox("##NativeScriptEnabled", &scriptEntry.Enabled);

                    std::string previewLabel = scriptEntry.ScriptClassName.empty() ? std::string("None") : scriptEntry.ScriptClassName;
                    ImGui::TextUnformatted("Class");
                    if (ImGui::BeginCombo("##NativeScriptClass", previewLabel.c_str()))
                    {
                        const bool noneSelected = scriptEntry.ScriptClassName.empty();
                        if (ImGui::Selectable("None", noneSelected))
                        {
                            if (undoService)
                            {
                                (void)undoService->ExecuteSceneMutation("Change Script Class", [&](Scene& mutableScene) {
                                    auto* mutableNativeScript = mutableScene.GetRegistry().try_get<NativeScriptComponent>(selectedEntity);
                                    if (!mutableNativeScript || scriptIndex >= mutableNativeScript->Scripts.size())
                                        return false;
                                    auto& mutableEntry = mutableNativeScript->Scripts[scriptIndex];
                                    mutableEntry.ScriptClassName.clear();
                                    mutableEntry.ScriptAssetRelativePath.clear();
                                    mutableEntry.ExposedProperties.clear();
                                    mutableEntry.RuntimeInitialized = false;
                                    mutableEntry.RuntimeInstance.reset();
                                    return true;
                                });
                            }
                            else
                            {
                                scriptEntry.ScriptClassName.clear();
                                scriptEntry.ScriptAssetRelativePath.clear();
                                scriptEntry.ExposedProperties.clear();
                                scriptEntry.RuntimeInitialized = false;
                                scriptEntry.RuntimeInstance.reset();
                            }
                        }
                        if (noneSelected)
                            ImGui::SetItemDefaultFocus();

                        for (const auto& scriptName : availableScriptNames)
                        {
                            const bool scriptSelected = (scriptEntry.ScriptClassName == scriptName);
                            if (ImGui::Selectable(scriptName.c_str(), scriptSelected))
                            {
                                const std::string resolvedScriptClassName = ResolveRegisteredScriptClassNameForInspector(scriptName);
                                const std::string& assignedScriptClassName = resolvedScriptClassName.empty() ? scriptName : resolvedScriptClassName;
                                if (undoService)
                                {
                                    (void)undoService->ExecuteSceneMutation("Change Script Class", [&](Scene& mutableScene) {
                                        auto* mutableNativeScript = mutableScene.GetRegistry().try_get<NativeScriptComponent>(selectedEntity);
                                        if (!mutableNativeScript || scriptIndex >= mutableNativeScript->Scripts.size())
                                            return false;
                                        auto& mutableEntry = mutableNativeScript->Scripts[scriptIndex];
                                        mutableEntry.ScriptClassName = assignedScriptClassName;
                                        mutableEntry.ScriptAssetRelativePath.clear();
                                        mutableEntry.ExposedProperties.clear();
                                        mutableEntry.RuntimeInitialized = false;
                                        mutableEntry.RuntimeInstance.reset();
                                        return true;
                                    });
                                }
                                else
                                {
                                    scriptEntry.ScriptClassName = assignedScriptClassName;
                                    scriptEntry.ScriptAssetRelativePath.clear();
                                    scriptEntry.ExposedProperties.clear();
                                    scriptEntry.RuntimeInitialized = false;
                                    scriptEntry.RuntimeInstance.reset();
                                }
                            }
                            if (scriptSelected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
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
                        for (const std::string& propertyName : declaredFieldNames)
                        {
                            auto propertyIterator = scriptEntry.ExposedProperties.find(propertyName);
                            if (propertyIterator == scriptEntry.ExposedProperties.end())
                                continue;

                            auto& propertyValue = propertyIterator->second;
                            const std::string propertyEditLabel = "Edit Script Property: " + propertyName;
                            ImGui::PushID(propertyName.c_str());
                            ImGui::TextUnformatted(propertyName.c_str());
                            if (auto* floatValue = std::get_if<float>(&propertyValue))
                            {
                                ImGui::DragFloat("##ScriptPropertyValue", floatValue, 0.1f);
                                if (undoService)
                                {
                                    if (ImGui::IsItemActivated())
                                        undoService->BeginInteractiveSceneMutation();
                                    if (ImGui::IsItemDeactivatedAfterEdit())
                                        (void)undoService->CommitInteractiveSceneMutation(propertyEditLabel);
                                }
                            }
                            else if (auto* integerValue = std::get_if<int32_t>(&propertyValue))
                            {
                                ImGui::DragInt("##ScriptPropertyValue", integerValue, 1.0f);
                                if (undoService)
                                {
                                    if (ImGui::IsItemActivated())
                                        undoService->BeginInteractiveSceneMutation();
                                    if (ImGui::IsItemDeactivatedAfterEdit())
                                        (void)undoService->CommitInteractiveSceneMutation(propertyEditLabel);
                                }
                            }
                            else if (auto* booleanValue = std::get_if<bool>(&propertyValue))
                            {
                                ImGui::Checkbox("##ScriptPropertyValue", booleanValue);
                                if (undoService)
                                {
                                    if (ImGui::IsItemActivated())
                                        undoService->BeginInteractiveSceneMutation();
                                    if (ImGui::IsItemDeactivatedAfterEdit())
                                        (void)undoService->CommitInteractiveSceneMutation(propertyEditLabel);
                                }
                            }
                            else if (auto* vectorValue = std::get_if<glm::vec3>(&propertyValue))
                            {
                                ImGui::DragFloat3("##ScriptPropertyValue", &vectorValue->x, 0.1f);
                                if (undoService)
                                {
                                    if (ImGui::IsItemActivated())
                                        undoService->BeginInteractiveSceneMutation();
                                    if (ImGui::IsItemDeactivatedAfterEdit())
                                        (void)undoService->CommitInteractiveSceneMutation(propertyEditLabel);
                                }
                            }
                            else if (auto* stringValue = std::get_if<std::string>(&propertyValue))
                            {
                                std::array<char, 256> textBuffer{};
                                std::snprintf(textBuffer.data(), textBuffer.size(), "%s", stringValue->c_str());
                                if (ImGui::InputText("##ScriptPropertyValue", textBuffer.data(), textBuffer.size()))
                                    *stringValue = textBuffer.data();
                                if (undoService)
                                {
                                    if (ImGui::IsItemActivated())
                                        undoService->BeginInteractiveSceneMutation();
                                    if (ImGui::IsItemDeactivatedAfterEdit())
                                        (void)undoService->CommitInteractiveSceneMutation(propertyEditLabel);
                                }
                            }
                            else if (auto* entityValue = std::get_if<ScriptEntityReference>(&propertyValue))
                            {
                                auto assignEntityReference = [&](const std::string& tagValue, const std::string& prefabAssetKeyValue) {
                                    if (undoService)
                                    {
                                        return undoService->ExecuteSceneMutation(propertyEditLabel, [&](Scene& mutableScene) {
                                            auto* mutableNativeScript = mutableScene.GetRegistry().try_get<NativeScriptComponent>(selectedEntity);
                                            if (!mutableNativeScript || scriptIndex >= mutableNativeScript->Scripts.size())
                                                return false;

                                            auto& mutableEntry = mutableNativeScript->Scripts[scriptIndex];
                                            auto propertyIt = mutableEntry.ExposedProperties.find(propertyName);
                                            if (propertyIt == mutableEntry.ExposedProperties.end() ||
                                                !std::holds_alternative<ScriptEntityReference>(propertyIt->second))
                                            {
                                                ScriptEntityReference referenceValue{};
                                                referenceValue.Tag = tagValue;
                                                referenceValue.PrefabAssetKey = prefabAssetKeyValue;
                                                mutableEntry.ExposedProperties[propertyName] = std::move(referenceValue);
                                                return true;
                                            }

                                            auto* mutableReference = std::get_if<ScriptEntityReference>(&propertyIt->second);
                                            if (!mutableReference)
                                                return false;
                                            mutableReference->Tag = tagValue;
                                            mutableReference->PrefabAssetKey = prefabAssetKeyValue;
                                            return true;
                                        });
                                    }

                                    entityValue->Tag = tagValue;
                                    entityValue->PrefabAssetKey = prefabAssetKeyValue;
                                    return true;
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
                                    if (undoService)
                                    {
                                        return undoService->ExecuteSceneMutation(propertyEditLabel, [&](Scene& mutableScene) {
                                            auto* mutableNativeScript = mutableScene.GetRegistry().try_get<NativeScriptComponent>(selectedEntity);
                                            if (!mutableNativeScript || scriptIndex >= mutableNativeScript->Scripts.size())
                                                return false;

                                            auto& mutableEntry = mutableNativeScript->Scripts[scriptIndex];
                                            auto propertyIt = mutableEntry.ExposedProperties.find(propertyName);
                                            if (propertyIt == mutableEntry.ExposedProperties.end() ||
                                                !std::holds_alternative<ScriptPrefabReference>(propertyIt->second))
                                            {
                                                mutableEntry.ExposedProperties[propertyName] = ScriptPrefabReference{ prefabKey };
                                                return true;
                                            }

                                            auto* mutableReference = std::get_if<ScriptPrefabReference>(&propertyIt->second);
                                            if (!mutableReference)
                                                return false;
                                            mutableReference->AssetKey = prefabKey;
                                            return true;
                                        });
                                    }

                                    prefabValue->AssetKey = prefabKey;
                                    return true;
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

                    ImGui::TreePop();
                }
                ImGui::PopID();
            }

            if (removeScriptIndex >= 0 && removeScriptIndex < static_cast<int>(nativeScript->Scripts.size()))
            {
                if (undoService)
                {
                    (void)undoService->ExecuteSceneMutation("Remove Script Entry", [&](Scene& mutableScene) {
                        auto* mutableNativeScript = mutableScene.GetRegistry().try_get<NativeScriptComponent>(selectedEntity);
                        if (!mutableNativeScript)
                            return false;
                        if (removeScriptIndex < 0 || removeScriptIndex >= static_cast<int>(mutableNativeScript->Scripts.size()))
                            return false;
                        mutableNativeScript->Scripts.erase(mutableNativeScript->Scripts.begin() + removeScriptIndex);
                        return true;
                    });
                }
                else
                {
                    nativeScript->Scripts.erase(nativeScript->Scripts.begin() + removeScriptIndex);
                }
            }
        }

        ImGui::TreePop();
    }
}
