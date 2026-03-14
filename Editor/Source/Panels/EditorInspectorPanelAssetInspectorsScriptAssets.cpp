#include "EditorInspectorPanelAssetInspectorsShared.h"

namespace Limitless::EditorInspectorPanel::Internal
{
    void DrawNativeScriptAssetInspectorInternal(std::string& selectedNativeScriptAssetKey)
    {
        if (selectedNativeScriptAssetKey.empty())
            return;

        std::filesystem::path selectedPath(selectedNativeScriptAssetKey);
        std::string extension = selectedPath.extension().string();
        for (char& character : extension)
            character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));

        if (extension == ".cs")
        {
            const std::string selectedFileName = selectedPath.filename().string();
            const std::string scriptClassName = selectedPath.stem().string();
            const auto resolvedSelectedPath = Assets::ResolveAssetKeyToPath(selectedNativeScriptAssetKey);
            const std::string resolvedClassName = ManagedScriptHost::ResolveDiscoveredClassName(scriptClassName);
            const auto& snapshot = ManagedScriptHost::GetSnapshot();

            const ManagedScriptHost::DiscoveredScriptClass* discoveredClass = nullptr;
            if (!resolvedClassName.empty())
            {
                const auto discoveredIt = std::find_if(snapshot.Classes.begin(),
                                                       snapshot.Classes.end(),
                                                       [&](const ManagedScriptHost::DiscoveredScriptClass& candidate) {
                                                           return candidate.FullName == resolvedClassName;
                                                       });
                if (discoveredIt != snapshot.Classes.end())
                    discoveredClass = &(*discoveredIt);
            }

            ImGui::Text("C# Script: %s", selectedFileName.c_str());
            ImGui::TextDisabled("Class: %s", scriptClassName.c_str());
            ImGui::TextDisabled("Type: C# Source (.cs)");
            ImGui::TextDisabled("Asset Key: %s", selectedNativeScriptAssetKey.c_str());
            if (resolvedSelectedPath.IsSuccess())
                ImGui::TextDisabled("Path: %s", resolvedSelectedPath.GetValue().string().c_str());
            else
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Could not resolve selected script path.");

            if (discoveredClass)
            {
                ImGui::TextDisabled("Discovered Class: %s", discoveredClass->FullName.c_str());
                ImGui::TextDisabled("Assembly: %s", discoveredClass->AssemblyPath.filename().string().c_str());
            }
            else
            {
                const char* statusMessage = snapshot.HostInitialized
                    ? "Managed class is not discovered in the active managed payload."
                    : "Managed payload is not loaded in the editor yet.";
                ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "%s", statusMessage);
            }

            ImGui::Spacing();
            if (ImGui::Button("Open Script", ImVec2(-1.0f, 0.0f)))
                (void)EditorInspectorPanel::OpenNativeScriptEditorForAssetKey(selectedNativeScriptAssetKey);
            if (ImGui::Button("Build Project Scripts", ImVec2(-1.0f, 0.0f)))
            {
                std::string buildStatusMessage;
                const bool buildStarted = EditorInspectorPanel::BuildProjectNativeScripts(&buildStatusMessage);
                if (!buildStatusMessage.empty())
                {
                    if (buildStarted)
                        ImGui::SetTooltip("%s", buildStatusMessage.c_str());
                }
            }

            std::string lastBuildFailure;
            if (EditorInspectorPanel::GetLastNativeScriptBuildFailure(&lastBuildFailure) && !lastBuildFailure.empty())
                ImGui::TextWrapped("%s", lastBuildFailure.c_str());

            ImGui::Separator();
            ImGui::TextUnformatted("Reflected Fields");
            if (!discoveredClass)
            {
                const char* reflectedFieldsHint = snapshot.HostInitialized
                    ? "Build project scripts to inspect managed reflected fields."
                    : "Build project scripts to stage the editor managed payload and inspect reflected fields.";
                ImGui::TextDisabled("%s", reflectedFieldsHint);
            }
            else if (discoveredClass->ReflectedFields.empty())
            {
                ImGui::TextDisabled("No public reflected fields were discovered for this managed script.");
            }
            else
            {
                for (const auto& field : discoveredClass->ReflectedFields)
                {
                    ImGui::Text("%s", field.Name.c_str());
                    ImGui::SameLine();
                    ImGui::TextDisabled("(%s)", GetScriptPropertyTypeLabel(field.Type));
                }
            }
            return;
        }

        if (extension != ".h" && extension != ".cpp")
        {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Selected asset is not a script file.");
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

    void DrawPrefabAssetInspectorInternal(std::string& selectedPrefabAssetKey)
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

    void DrawAnimationClipAssetInspectorInternal(std::string& selectedAnimationClipAssetKey)
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
        ImGui::BulletText("Rotation Keys: %u", static_cast<uint32_t>(clipData.RotationTrack.size()));
        ImGui::BulletText("Events: %u", static_cast<uint32_t>(clipData.EventTrack.size()));
        ImGui::Spacing();
        ImGui::TextDisabled("Edit detailed tracks in the Animation Timeline panel.");
    }

    void DrawAnimatorControllerAssetInspectorInternal(std::string& selectedAnimatorControllerAssetKey)
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
