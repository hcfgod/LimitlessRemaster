#include "EditorInspectorPanelComponentManagement.h"

#include "EditorComponentRegistry.h"
#include "EditorInspectorPanel.h"
#include "EditorInspectorPanelNativeScriptEditor.h"
#include "Project/ProjectManager.h"
#include "Undo/EditorUndoService.h"
#include "Scene/Scene.h"
#include "Scripting/ManagedScriptHost.h"
#include "Scripting/NativeScriptRegistry.h"
#include "imgui/imgui.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <unordered_set>
#include <vector>

namespace Limitless::EditorInspectorPanel
{
    namespace
    {
        struct ProjectScriptFolderNode final
        {
            std::map<std::string, ProjectScriptFolderNode> Children;
            std::vector<ProjectNativeScriptInfo> Scripts;
        };

        template<typename ComponentType>
        bool HasComponent(const entt::registry& registry, entt::entity entity)
        {
            return registry.all_of<ComponentType>(entity);
        }

        void DrawAddComponentMenuItem(const ComponentRegistryEntry& entry,
                                      Scene* scene,
                                      entt::registry& registry,
                                      entt::entity selectedEntity,
                                      EditorUndoService* undoService)
        {
            const bool wasDisabled = entry.HasComponent(registry, selectedEntity);
            if (wasDisabled)
                ImGui::BeginDisabled();

            if (ImGui::MenuItem(entry.MenuItemLabel))
            {
                if (undoService)
                {
                    (void)undoService->ExecuteSceneMutation(entry.AddMutationLabel, [&](Scene& mutableScene) {
                        entry.AddComponent(mutableScene.GetRegistry(), selectedEntity);
                        return true;
                    });
                }
                else
                {
                    (void)scene;
                    entry.AddComponent(registry, selectedEntity);
                }
            }

            if (wasDisabled)
                ImGui::EndDisabled();
        }

        void ApplyPendingRemoval(const ComponentRegistryEntry& entry,
                                 bool shouldRemove,
                                 entt::registry& registry,
                                 entt::entity selectedEntity,
                                 EditorUndoService* undoService)
        {
            if (!shouldRemove)
                return;

            if (undoService)
            {
                (void)undoService->ExecuteSceneMutation(entry.RemoveMutationLabel, [&](Scene& mutableScene) {
                    entry.RemoveComponent(mutableScene.GetRegistry(), selectedEntity);
                    return true;
                });
            }
            else
            {
                entry.RemoveComponent(registry, selectedEntity);
            }
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

        NativeScriptEntry BuildScriptEntryForComponentAdd(const ProjectNativeScriptInfo& requestedScript)
        {
            NativeScriptEntry scriptEntry{};
            scriptEntry.ScriptAssetRelativePath = requestedScript.ScriptAssetRelativePath;
            const std::string resolvedScriptClassName = ResolveRegisteredScriptClassNameForInspector(requestedScript.ScriptClassName);
            scriptEntry.ScriptClassName = resolvedScriptClassName.empty() ? requestedScript.ScriptClassName : resolvedScriptClassName;
            return scriptEntry;
        }

        ManagedScriptEntry BuildManagedScriptEntryForComponentAdd(const ProjectNativeScriptInfo& requestedScript)
        {
            ManagedScriptEntry scriptEntry{};
            scriptEntry.ScriptAssetRelativePath = requestedScript.ScriptAssetRelativePath;
            const std::string resolvedScriptClassName = ManagedScriptHost::ResolveDiscoveredClassName(requestedScript.ScriptClassName);
            scriptEntry.ScriptClassName = resolvedScriptClassName.empty() ? requestedScript.ScriptClassName : resolvedScriptClassName;
            return scriptEntry;
        }

        bool CsFileContainsScriptableEntityDerivation(const std::filesystem::path& csPath)
        {
            std::ifstream csFile(csPath, std::ios::in);
            if (!csFile.is_open())
                return true;

            std::string line;
            while (std::getline(csFile, line))
            {
                if (line.find("ScriptableEntity") != std::string::npos)
                {
                    if (line.find(":") != std::string::npos)
                        return true;
                }
            }
            return false;
        }

        std::vector<ProjectNativeScriptInfo> DiscoverProjectManagedScriptsForComponentAdd()
        {
            std::vector<ProjectNativeScriptInfo> availableScripts;

            const auto& projectManager = Project::ProjectManager::GetInstance();
            if (!projectManager.HasOpenProject())
                return availableScripts;

            const std::filesystem::path assetsDirectory = projectManager.GetProjectRoot() / "Assets";
            std::error_code errorCode;
            if (!std::filesystem::exists(assetsDirectory, errorCode) || !std::filesystem::is_directory(assetsDirectory, errorCode))
                return availableScripts;

            for (std::filesystem::recursive_directory_iterator iterator(assetsDirectory, std::filesystem::directory_options::skip_permission_denied, errorCode), end;
                 iterator != end;
                 iterator.increment(errorCode))
            {
                if (errorCode)
                {
                    errorCode.clear();
                    continue;
                }

                const auto& entry = *iterator;
                const bool isRegularFile = entry.is_regular_file(errorCode);
                if (errorCode || !isRegularFile)
                {
                    errorCode.clear();
                    continue;
                }

                std::string extension = entry.path().extension().string();
                std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char character) {
                    return static_cast<char>(std::tolower(character));
                });
                if (extension != ".cs")
                    continue;

                const std::filesystem::path relativePath = std::filesystem::relative(entry.path(), assetsDirectory, errorCode);
                if (errorCode)
                {
                    errorCode.clear();
                    continue;
                }

                const bool likelyDerives = CsFileContainsScriptableEntityDerivation(entry.path());

                ProjectNativeScriptInfo scriptInfo{};
                scriptInfo.ScriptClassName = entry.path().stem().string();
                scriptInfo.ScriptAssetRelativePath = relativePath.generic_string();
                scriptInfo.FolderRelativePath = relativePath.parent_path().generic_string();
                scriptInfo.DisplayName = scriptInfo.ScriptClassName;
                scriptInfo.LikelyDerivesFromScriptableEntity = likelyDerives;
                availableScripts.push_back(std::move(scriptInfo));
            }

            std::sort(availableScripts.begin(), availableScripts.end(), [](const ProjectNativeScriptInfo& left, const ProjectNativeScriptInfo& right) {
                if (left.FolderRelativePath != right.FolderRelativePath)
                    return left.FolderRelativePath < right.FolderRelativePath;
                return left.DisplayName < right.DisplayName;
            });

            return availableScripts;
        }

        bool IsBuiltinManagedScriptClassForComponentAdd(const ManagedScriptHost::DiscoveredScriptClass& discoveredClass)
        {
            const std::string assemblyFileName = discoveredClass.AssemblyPath.filename().generic_string();
            return assemblyFileName == "Limitless.Managed.TestScripts.dll" ||
                   discoveredClass.AssemblyName == "Limitless.Managed.TestScripts";
        }

        void DrawAddComponentPopupSectionHeader(const char* label)
        {
            ImGui::Spacing();
            ImGui::TextDisabled("%s", label);
            ImGui::Separator();
        }

        bool IsNativeScriptEligibleForComponentAdd(const ProjectNativeScriptInfo& scriptInfo)
        {
            if (NativeScriptRegistry::HasScript(scriptInfo.ScriptClassName))
                return true;
            const std::string resolvedName = ResolveRegisteredScriptClassNameForInspector(scriptInfo.ScriptClassName);
            if (!resolvedName.empty() && NativeScriptRegistry::HasScript(resolvedName))
                return true;
            if (NativeScriptRegistry::GetRegisteredScriptNames().empty())
                return scriptInfo.LikelyDerivesFromScriptableEntity;
            return false;
        }

        void DrawAddScriptComponentMenu(Scene* scene,
                                        entt::entity selectedEntity,
                                        EditorUndoService* undoService)
        {
            std::vector<ProjectNativeScriptInfo> allScripts = GetAvailableProjectScriptsForInspector();
            std::vector<ProjectNativeScriptInfo> availableScripts;
            availableScripts.reserve(allScripts.size());
            for (auto& scriptInfo : allScripts)
            {
                if (IsNativeScriptEligibleForComponentAdd(scriptInfo))
                    availableScripts.push_back(std::move(scriptInfo));
            }
            const ProjectScriptFolderNode scriptFolderTree = BuildProjectScriptFolderTree(availableScripts);
            const std::vector<ProjectNativeScriptInfo> availableManagedScripts = DiscoverProjectManagedScriptsForComponentAdd();
            const auto& managedSnapshot = ManagedScriptHost::GetSnapshot();

            DrawAddComponentPopupSectionHeader("Scripts");

            const auto attachScript = [&](const ProjectNativeScriptInfo& scriptInfo) {
                NativeScriptEntry scriptEntry = BuildScriptEntryForComponentAdd(scriptInfo);
                if (undoService)
                {
                    (void)undoService->ExecuteSceneMutation("Attach Script Component", [&](Scene& mutableScene) {
                        return mutableScene.AttachScriptComponent(selectedEntity, std::move(scriptEntry)) != entt::null;
                    });
                }
                else if (scene)
                {
                    (void)scene->AttachScriptComponent(selectedEntity, std::move(scriptEntry));
                }
            };

            std::function<void(const ProjectScriptFolderNode&)> drawFolderContents;
            drawFolderContents = [&](const ProjectScriptFolderNode& folderNode) {
                for (const ProjectNativeScriptInfo& scriptInfo : folderNode.Scripts)
                {
                    if (ImGui::MenuItem(scriptInfo.DisplayName.c_str()))
                        attachScript(scriptInfo);
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

            if (!availableScripts.empty())
            {
                ImGui::Separator();
            }

            if (ImGui::MenuItem("Empty Script Component"))
            {
                if (undoService)
                {
                    (void)undoService->ExecuteSceneMutation("Add Script Component", [&](Scene& mutableScene) {
                        return mutableScene.AttachScriptComponent(selectedEntity) != entt::null;
                    });
                }
                else if (scene)
                {
                    (void)scene->AttachScriptComponent(selectedEntity);
                }
            }

            if (!managedSnapshot.Classes.empty() || !availableManagedScripts.empty())
            {
                ImGui::Separator();
                const auto attachManagedScript = [&](const ManagedScriptHost::DiscoveredScriptClass& discoveredClass) {
                    ManagedScriptEntry scriptEntry{};
                    scriptEntry.ScriptClassName = discoveredClass.FullName;
                    scriptEntry.ScriptAssetRelativePath = discoveredClass.AssemblyPath.filename().generic_string();
                    if (undoService)
                    {
                        (void)undoService->ExecuteSceneMutation("Attach Managed Script Component", [&](Scene& mutableScene) {
                            return mutableScene.AttachManagedScriptComponent(selectedEntity, std::move(scriptEntry)) != entt::null;
                        });
                    }
                    else if (scene)
                    {
                        (void)scene->AttachManagedScriptComponent(selectedEntity, std::move(scriptEntry));
                    }
                };

                const auto attachManagedScriptAsset = [&](const ProjectNativeScriptInfo& scriptInfo) {
                    ManagedScriptEntry scriptEntry = BuildManagedScriptEntryForComponentAdd(scriptInfo);
                    if (ManagedScriptHost::ResolveDiscoveredClassName(scriptInfo.ScriptClassName).empty())
                        (void)BuildProjectNativeScripts(nullptr);
                    if (undoService)
                    {
                        (void)undoService->ExecuteSceneMutation("Attach Managed Script Component", [&](Scene& mutableScene) {
                            return mutableScene.AttachManagedScriptComponent(selectedEntity, std::move(scriptEntry)) != entt::null;
                        });
                    }
                    else if (scene)
                    {
                        (void)scene->AttachManagedScriptComponent(selectedEntity, std::move(scriptEntry));
                    }
                };

                std::vector<const ManagedScriptHost::DiscoveredScriptClass*> builtinManagedClasses;
                std::vector<const ManagedScriptHost::DiscoveredScriptClass*> otherManagedClasses;
                std::unordered_set<std::string> projectManagedResolvedClassNames;
                builtinManagedClasses.reserve(managedSnapshot.Classes.size());
                otherManagedClasses.reserve(managedSnapshot.Classes.size());
                projectManagedResolvedClassNames.reserve(availableManagedScripts.size());
                for (const ProjectNativeScriptInfo& scriptInfo : availableManagedScripts)
                {
                    const std::string resolvedClassName = ManagedScriptHost::ResolveDiscoveredClassName(scriptInfo.ScriptClassName);
                    if (!resolvedClassName.empty())
                        projectManagedResolvedClassNames.insert(resolvedClassName);
                }
                for (const auto& discoveredClass : managedSnapshot.Classes)
                {
                    if (IsBuiltinManagedScriptClassForComponentAdd(discoveredClass))
                        builtinManagedClasses.push_back(&discoveredClass);
                    else if (!projectManagedResolvedClassNames.contains(discoveredClass.FullName))
                        otherManagedClasses.push_back(&discoveredClass);
                    else
                        (void)discoveredClass;
                }

                if (!builtinManagedClasses.empty() && ImGui::BeginMenu("Engine"))
                {
                    if (ImGui::BeginMenu("Builtin"))
                    {
                        if (ImGui::BeginMenu("C#"))
                        {
                            for (const ManagedScriptHost::DiscoveredScriptClass* discoveredClass : builtinManagedClasses)
                            {
                                if (discoveredClass && ImGui::MenuItem(discoveredClass->FullName.c_str()))
                                    attachManagedScript(*discoveredClass);
                            }
                            ImGui::EndMenu();
                        }
                        ImGui::EndMenu();
                    }
                    ImGui::EndMenu();
                }

                if (!availableManagedScripts.empty() && ImGui::BeginMenu("Project"))
                {
                    if (ImGui::BeginMenu("C#"))
                    {
                        for (const ProjectNativeScriptInfo& scriptInfo : availableManagedScripts)
                        {
                            const bool resolvedInHost = !ManagedScriptHost::ResolveDiscoveredClassName(scriptInfo.ScriptClassName).empty();
                            if (!resolvedInHost && !scriptInfo.LikelyDerivesFromScriptableEntity)
                                continue;
                            const std::string menuLabel = scriptInfo.DisplayName + "##ManagedProjectScript_" + scriptInfo.ScriptAssetRelativePath;
                            if (ImGui::MenuItem(menuLabel.c_str()))
                                attachManagedScriptAsset(scriptInfo);
                        }
                        if (!availableManagedScripts.empty())
                            ImGui::Separator();
                        if (ImGui::MenuItem("Build Project Scripts"))
                            (void)BuildProjectNativeScripts(nullptr);
                        ImGui::EndMenu();
                    }
                    ImGui::EndMenu();
                }

                for (const ManagedScriptHost::DiscoveredScriptClass* discoveredClass : otherManagedClasses)
                {
                    if (discoveredClass && ImGui::MenuItem(discoveredClass->FullName.c_str()))
                        attachManagedScript(*discoveredClass);
                }

                if (ImGui::MenuItem("Empty Managed Script Component"))
                {
                    if (undoService)
                    {
                        (void)undoService->ExecuteSceneMutation("Add Managed Script Component", [&](Scene& mutableScene) {
                            return mutableScene.AttachManagedScriptComponent(selectedEntity) != entt::null;
                        });
                    }
                    else if (scene)
                    {
                        (void)scene->AttachManagedScriptComponent(selectedEntity);
                    }
                }
            }
        }
    }

    void DrawAddComponentPopup(Scene* scene,
                               entt::registry& registry,
                               entt::entity selectedEntity,
                               EditorUndoService* undoService)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.24f, 0.38f, 0.58f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.31f, 0.46f, 0.68f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.37f, 0.53f, 0.74f, 1.0f));
        if (ImGui::Button("Add Component", ImVec2(-1.0f, 0.0f)))
            ImGui::OpenPopup("AddComponentPopup");
        ImGui::PopStyleColor(3);

        if (!ImGui::BeginPopup("AddComponentPopup"))
            return;

        DrawAddComponentPopupSectionHeader("Components");

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::Canvas))
            DrawAddComponentMenuItem(*entry, scene, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::RectTransform))
            DrawAddComponentMenuItem(*entry, scene, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::UIImage))
            DrawAddComponentMenuItem(*entry, scene, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::UIPanel))
            DrawAddComponentMenuItem(*entry, scene, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::UIText))
            DrawAddComponentMenuItem(*entry, scene, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::UIButton))
            DrawAddComponentMenuItem(*entry, scene, registry, selectedEntity, undoService);

        const bool wasSliderDisabled = HasComponent<UISliderComponent>(registry, selectedEntity);
        if (wasSliderDisabled)
            ImGui::BeginDisabled();

        if (ImGui::MenuItem("UI Slider"))
        {
            if (undoService)
                (void)undoService->ExecuteSceneMutation("Add UISlider Component", [&](Scene& mutableScene) {
                    auto& mutableRegistry = mutableScene.GetRegistry();
                    auto& slider = mutableRegistry.emplace<UISliderComponent>(selectedEntity);
                    slider.Value = std::clamp(0.5f, slider.MinValue, std::max(slider.MinValue, slider.MaxValue));
                    if (!mutableRegistry.all_of<UIImageComponent>(selectedEntity))
                        mutableRegistry.emplace<UIImageComponent>(selectedEntity);
                    if (!mutableRegistry.all_of<RectTransformComponent>(selectedEntity))
                        mutableRegistry.emplace<RectTransformComponent>(selectedEntity);

                    const float sliderRange = std::max(0.0001f, slider.MaxValue - slider.MinValue);
                    const float sliderNormalized = std::clamp((slider.Value - slider.MinValue) / sliderRange, 0.0f, 1.0f);
                    auto ensureSliderVisualChild = [&](const char* childName,
                                                       const glm::vec4& defaultColor,
                                                       int32_t siblingOrder,
                                                       auto&& initializeRectTransform) {
                        entt::entity childEntity = entt::null;
                        auto childView = mutableRegistry.view<TagComponent, HierarchyComponent>();
                        for (entt::entity candidate : childView)
                        {
                            const auto& hierarchy = childView.get<HierarchyComponent>(candidate);
                            if (hierarchy.Parent != selectedEntity)
                                continue;
                            const auto& tag = childView.get<TagComponent>(candidate);
                            if (tag.Tag == childName)
                            {
                                childEntity = candidate;
                                break;
                            }
                        }

                        bool created = false;
                        if (childEntity == entt::null)
                        {
                            childEntity = mutableScene.CreateEntity(childName);
                            mutableScene.SetParent(childEntity, selectedEntity);
                            created = true;
                        }

                        if (auto* hierarchy = mutableRegistry.try_get<HierarchyComponent>(childEntity))
                            hierarchy->SiblingOrder = siblingOrder;
                        if (!mutableRegistry.all_of<RectTransformComponent>(childEntity))
                            mutableRegistry.emplace<RectTransformComponent>(childEntity);
                        if (!mutableRegistry.all_of<UIImageComponent>(childEntity))
                            mutableRegistry.emplace<UIImageComponent>(childEntity);
                        if (!mutableRegistry.all_of<SpriteComponent>(childEntity))
                        {
                            auto& childSprite = mutableRegistry.emplace<SpriteComponent>(childEntity);
                            childSprite.Color = defaultColor;
                        }

                        if (created)
                        {
                            auto& rect = mutableRegistry.get<RectTransformComponent>(childEntity);
                            initializeRectTransform(rect);
                        }
                    };

                    ensureSliderVisualChild("Slider Background", slider.BackgroundColor, 0, [](RectTransformComponent& rect) {
                        rect.AnchorMin = glm::vec2(0.0f, 0.0f);
                        rect.AnchorMax = glm::vec2(1.0f, 1.0f);
                        rect.Pivot = glm::vec2(0.5f, 0.5f);
                        rect.SizeDelta = glm::vec2(0.0f, 0.0f);
                        rect.AnchoredPosition = glm::vec2(0.0f, 0.0f);
                    });
                    ensureSliderVisualChild("Slider Fill", slider.FillColor, 10, [sliderNormalized](RectTransformComponent& rect) {
                        rect.AnchorMin = glm::vec2(0.0f, 0.0f);
                        rect.AnchorMax = glm::vec2(sliderNormalized, 1.0f);
                        rect.Pivot = glm::vec2(0.5f, 0.5f);
                        rect.SizeDelta = glm::vec2(0.0f, 0.0f);
                        rect.AnchoredPosition = glm::vec2(0.0f, 0.0f);
                    });
                    ensureSliderVisualChild("Slider Handle", slider.HandleColor, 20, [sliderNormalized](RectTransformComponent& rect) {
                        rect.AnchorMin = glm::vec2(sliderNormalized, 0.5f);
                        rect.AnchorMax = glm::vec2(sliderNormalized, 0.5f);
                        rect.Pivot = glm::vec2(0.5f, 0.5f);
                        rect.SizeDelta = glm::vec2(16.0f, 48.0f);
                        rect.AnchoredPosition = glm::vec2(0.0f, 0.0f);
                    });
                    return true;
                });
            else {
                auto& slider = registry.emplace<UISliderComponent>(selectedEntity);
                slider.Value = std::clamp(0.5f, slider.MinValue, std::max(slider.MinValue, slider.MaxValue));
                if (!registry.all_of<UIImageComponent>(selectedEntity))
                    registry.emplace<UIImageComponent>(selectedEntity);
                if (!registry.all_of<RectTransformComponent>(selectedEntity))
                    registry.emplace<RectTransformComponent>(selectedEntity);

                const float sliderRange = std::max(0.0001f, slider.MaxValue - slider.MinValue);
                const float sliderNormalized = std::clamp((slider.Value - slider.MinValue) / sliderRange, 0.0f, 1.0f);
                auto ensureSliderVisualChild = [&](const char* childName,
                                                   const glm::vec4& defaultColor,
                                                   int32_t siblingOrder,
                                                   auto&& initializeRectTransform) {
                    entt::entity childEntity = entt::null;
                    auto childView = registry.view<TagComponent, HierarchyComponent>();
                    for (entt::entity candidate : childView)
                    {
                        const auto& hierarchy = childView.get<HierarchyComponent>(candidate);
                        if (hierarchy.Parent != selectedEntity)
                            continue;
                        const auto& tag = childView.get<TagComponent>(candidate);
                        if (tag.Tag == childName)
                        {
                            childEntity = candidate;
                            break;
                        }
                    }

                    bool created = false;
                    if (childEntity == entt::null)
                    {
                        childEntity = scene->CreateEntity(childName);
                        scene->SetParent(childEntity, selectedEntity);
                        created = true;
                    }

                    if (auto* hierarchy = registry.try_get<HierarchyComponent>(childEntity))
                        hierarchy->SiblingOrder = siblingOrder;
                    if (!registry.all_of<RectTransformComponent>(childEntity))
                        registry.emplace<RectTransformComponent>(childEntity);
                    if (!registry.all_of<UIImageComponent>(childEntity))
                        registry.emplace<UIImageComponent>(childEntity);
                    if (!registry.all_of<SpriteComponent>(childEntity))
                    {
                        auto& childSprite = registry.emplace<SpriteComponent>(childEntity);
                        childSprite.Color = defaultColor;
                    }

                    if (created)
                    {
                        auto& rect = registry.get<RectTransformComponent>(childEntity);
                        initializeRectTransform(rect);
                    }
                };

                ensureSliderVisualChild("Slider Background", slider.BackgroundColor, 0, [](RectTransformComponent& rect) {
                    rect.AnchorMin = glm::vec2(0.0f, 0.0f);
                    rect.AnchorMax = glm::vec2(1.0f, 1.0f);
                    rect.Pivot = glm::vec2(0.5f, 0.5f);
                    rect.SizeDelta = glm::vec2(0.0f, 0.0f);
                    rect.AnchoredPosition = glm::vec2(0.0f, 0.0f);
                });
                ensureSliderVisualChild("Slider Fill", slider.FillColor, 10, [sliderNormalized](RectTransformComponent& rect) {
                    rect.AnchorMin = glm::vec2(0.0f, 0.0f);
                    rect.AnchorMax = glm::vec2(sliderNormalized, 1.0f);
                    rect.Pivot = glm::vec2(0.5f, 0.5f);
                    rect.SizeDelta = glm::vec2(0.0f, 0.0f);
                    rect.AnchoredPosition = glm::vec2(0.0f, 0.0f);
                });
                ensureSliderVisualChild("Slider Handle", slider.HandleColor, 20, [sliderNormalized](RectTransformComponent& rect) {
                    rect.AnchorMin = glm::vec2(sliderNormalized, 0.5f);
                    rect.AnchorMax = glm::vec2(sliderNormalized, 0.5f);
                    rect.Pivot = glm::vec2(0.5f, 0.5f);
                    rect.SizeDelta = glm::vec2(16.0f, 48.0f);
                    rect.AnchoredPosition = glm::vec2(0.0f, 0.0f);
                });
            }
        }

        if (wasSliderDisabled)
            ImGui::EndDisabled();

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::Sprite))
            DrawAddComponentMenuItem(*entry, scene, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::Camera))
            DrawAddComponentMenuItem(*entry, scene, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::AudioListener2D))
            DrawAddComponentMenuItem(*entry, scene, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::AudioListener3D))
            DrawAddComponentMenuItem(*entry, scene, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::AudioSource))
            DrawAddComponentMenuItem(*entry, scene, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::Animator))
            DrawAddComponentMenuItem(*entry, scene, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::AnimationEventReceiver))
            DrawAddComponentMenuItem(*entry, scene, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::Rigidbody2D))
            DrawAddComponentMenuItem(*entry, scene, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::BoxCollider2D))
            DrawAddComponentMenuItem(*entry, scene, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::CircleCollider2D))
            DrawAddComponentMenuItem(*entry, scene, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::PolygonCollider2D))
            DrawAddComponentMenuItem(*entry, scene, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::EdgeCollider2D))
            DrawAddComponentMenuItem(*entry, scene, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::CapsuleCollider2D))
            DrawAddComponentMenuItem(*entry, scene, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::Joint2D))
            DrawAddComponentMenuItem(*entry, scene, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::DirectionalLight2D))
            DrawAddComponentMenuItem(*entry, scene, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::PointLight2D))
            DrawAddComponentMenuItem(*entry, scene, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::ShadowOccluder2D))
            DrawAddComponentMenuItem(*entry, scene, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::Grid2D))
            DrawAddComponentMenuItem(*entry, scene, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::TilemapLayer))
            DrawAddComponentMenuItem(*entry, scene, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::ParticleEmitter))
            DrawAddComponentMenuItem(*entry, scene, registry, selectedEntity, undoService);

        DrawAddScriptComponentMenu(scene, selectedEntity, undoService);

        ImGui::EndPopup();
    }

    void ApplyPendingEntityComponentRemovals(Scene* scene,
                                             entt::registry& registry,
                                             entt::entity selectedEntity,
                                             PendingEntityComponentRemovals& pendingRemovals,
                                             EditorUndoService* undoService)
    {
        (void)scene;

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::Canvas))
            ApplyPendingRemoval(*entry, pendingRemovals.RemoveCanvasComponent, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::RectTransform))
            ApplyPendingRemoval(*entry, pendingRemovals.RemoveRectTransformComponent, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::UIImage))
            ApplyPendingRemoval(*entry, pendingRemovals.RemoveUIImageComponent, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::UIPanel))
            ApplyPendingRemoval(*entry, pendingRemovals.RemoveUIPanelComponent, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::UIText))
            ApplyPendingRemoval(*entry, pendingRemovals.RemoveUITextComponent, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::UIButton))
            ApplyPendingRemoval(*entry, pendingRemovals.RemoveUIButtonComponent, registry, selectedEntity, undoService);

        if (pendingRemovals.RemoveUISliderComponent)
        {
            if (undoService)
            {
                (void)undoService->ExecuteSceneMutation("Remove UISlider Component", [&](Scene& mutableScene) {
                    mutableScene.GetRegistry().remove<UISliderComponent>(selectedEntity);
                    return true;
                });
            }
            else
            {
                registry.remove<UISliderComponent>(selectedEntity);
            }
        }

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::Sprite))
            ApplyPendingRemoval(*entry, pendingRemovals.RemoveSpriteComponent, registry, selectedEntity, undoService);

        if (pendingRemovals.RemoveMaterialComponent)
        {
            if (undoService)
            {
                (void)undoService->ExecuteSceneMutation("Remove Material Component", [&](Scene& mutableScene) {
                    mutableScene.GetRegistry().remove<MaterialComponent>(selectedEntity);
                    return true;
                });
            }
            else
            {
                registry.remove<MaterialComponent>(selectedEntity);
            }
        }

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::Camera))
            ApplyPendingRemoval(*entry, pendingRemovals.RemoveCameraComponent, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::AudioListener2D))
            ApplyPendingRemoval(*entry, pendingRemovals.RemoveAudioListener2DComponent, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::AudioListener3D))
            ApplyPendingRemoval(*entry, pendingRemovals.RemoveAudioListener3DComponent, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::AudioSource))
            ApplyPendingRemoval(*entry, pendingRemovals.RemoveAudioSourceComponent, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::Animator))
            ApplyPendingRemoval(*entry, pendingRemovals.RemoveAnimatorComponent, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::AnimationEventReceiver))
            ApplyPendingRemoval(*entry, pendingRemovals.RemoveAnimationEventReceiverComponent, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::Rigidbody2D))
            ApplyPendingRemoval(*entry, pendingRemovals.RemoveRigidbody2DComponent, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::BoxCollider2D))
            ApplyPendingRemoval(*entry, pendingRemovals.RemoveBoxCollider2DComponent, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::CircleCollider2D))
            ApplyPendingRemoval(*entry, pendingRemovals.RemoveCircleCollider2DComponent, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::PolygonCollider2D))
            ApplyPendingRemoval(*entry, pendingRemovals.RemovePolygonCollider2DComponent, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::EdgeCollider2D))
            ApplyPendingRemoval(*entry, pendingRemovals.RemoveEdgeCollider2DComponent, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::CapsuleCollider2D))
            ApplyPendingRemoval(*entry, pendingRemovals.RemoveCapsuleCollider2DComponent, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::Joint2D))
            ApplyPendingRemoval(*entry, pendingRemovals.RemoveJoint2DComponent, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::DirectionalLight2D))
            ApplyPendingRemoval(*entry, pendingRemovals.RemoveDirectionalLight2DComponent, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::PointLight2D))
            ApplyPendingRemoval(*entry, pendingRemovals.RemovePointLight2DComponent, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::ShadowOccluder2D))
            ApplyPendingRemoval(*entry, pendingRemovals.RemoveShadowOccluder2DComponent, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::ParticleEmitter))
            ApplyPendingRemoval(*entry, pendingRemovals.RemoveParticleEmitterComponent, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::Grid2D))
            ApplyPendingRemoval(*entry, pendingRemovals.RemoveGrid2DComponent, registry, selectedEntity, undoService);

        if (const ComponentRegistryEntry* entry = FindComponentRegistryEntry(ComponentRegistryKey::TilemapLayer))
            ApplyPendingRemoval(*entry, pendingRemovals.RemoveTilemapLayerComponent, registry, selectedEntity, undoService);
    }
}
