#include "EditorProjectPanel.h"
#include "EditorProjectPanelInternal.h"
#include "EditorProjectPanelShared.h"

#include "Assets/AssetPaths.h"
#include "Core/Debug/Log.h"
#include "EditorPanelLock.h"
#include "EditorPanelStyle.h"
#include "ProjectAssetOperations.h"
#include "imgui/imgui.h"

#include <algorithm>

namespace Limitless::EditorProjectPanel
{
    void Draw(const char* windowName,
              bool& isOpen,
              EditorProjectPanelState& state,
              EditorAssetPreview::MaterialPreviewCache& materialPreviewCache,
              entt::entity& selectedEntity,
              std::string& selectedTextureAssetKey,
              Assets::TextureAsset::Ptr& cachedTextureAsset,
              std::string& selectedMaterialAssetKey,
              Assets::MaterialAsset::Ptr& cachedMaterialAsset,
              std::string& selectedNativeScriptAssetKey,
              std::string& selectedPrefabAssetKey,
              std::string& selectedTilesetAssetKey,
              std::string& selectedAudioMixerAssetKey,
              std::string& selectedInputActionsAssetKey,
              std::string& selectedAnimationClipAssetKey,
              std::string& selectedAnimatorControllerAssetKey,
              const char* texturePayloadId,
              const char* audioPayloadId,
              const char* assetMovePayloadId,
              const char* scenePayloadId,
              const char* materialPayloadId,
              const char* prefabPayloadId,
              const char* shaderPayloadId,
              const char* fontPayloadId,
              const std::function<void(const std::string&)>& onSceneActivated,
              const std::function<void(const std::filesystem::path&)>& onCreateSceneRequested,
              const std::function<void(const std::filesystem::path&, const std::string&)>& onCreateMaterialRequested,
              const std::function<void(const std::filesystem::path&, const std::string&)>& onCreateTilesetRequested,
              const std::function<void(const std::filesystem::path&, const std::string&)>& onCreateAudioMixerRequested,
              const std::function<void(const std::filesystem::path&, const std::string&)>& onCreateInputActionsRequested,
              const std::function<void(const std::filesystem::path&, const std::string&)>& onCreateAnimationClipRequested,
              const std::function<void(const std::filesystem::path&, const std::string&)>& onCreateAnimatorControllerRequested,
              const std::function<void(entt::entity, const std::filesystem::path&)>& onCreatePrefabFromSceneEntityRequested,
              const std::function<void(const std::string&)>& onPrefabOpened,
              const std::function<void(const std::string&)>& onPrefabInstantiated,
              const std::function<void(const std::string&)>& onSetDefaultSceneRequested,
              const std::function<void(const std::string&, const std::string&)>& onAssetRenamed,
              const std::function<bool(const std::vector<std::string>&)>& onDeleteSceneAssetsRequested,
              const std::function<void(const std::string&)>& onNativeScriptAssetActivated)
    {
        Internal::ProjectPanelSelectionRefs selection{
            selectedEntity,
            selectedTextureAssetKey,
            cachedTextureAsset,
            selectedMaterialAssetKey,
            cachedMaterialAsset,
            selectedNativeScriptAssetKey,
            selectedPrefabAssetKey,
            selectedTilesetAssetKey,
            selectedAudioMixerAssetKey,
            selectedInputActionsAssetKey,
            selectedAnimationClipAssetKey,
            selectedAnimatorControllerAssetKey
        };
        Internal::ProjectPanelCallbacks callbacks{
            texturePayloadId,
            audioPayloadId,
            assetMovePayloadId,
            scenePayloadId,
            materialPayloadId,
            prefabPayloadId,
            shaderPayloadId,
            fontPayloadId,
            onSceneActivated,
            onCreateSceneRequested,
            onCreateMaterialRequested,
            onCreateTilesetRequested,
            onCreateAudioMixerRequested,
            onCreateInputActionsRequested,
            onCreateAnimationClipRequested,
            onCreateAnimatorControllerRequested,
            onCreatePrefabFromSceneEntityRequested,
            onPrefabOpened,
            onPrefabInstantiated,
            onSetDefaultSceneRequested,
            onAssetRenamed,
            onDeleteSceneAssetsRequested,
            onNativeScriptAssetActivated
        };

        EditorPanelStyle::PushPanelVisualStyle();
        if (!ImGui::Begin(windowName, &isOpen))
        {
            Internal::ClearProjectSearchMatchCache(state);
            ImGui::End();
            EditorPanelStyle::PopPanelVisualStyle();
            return;
        }

        state.IsLocked = EditorPanelLock::DrawLockToggle(state.IsLocked);

        state.TreeExpansionStateChanged = false;
        state.BrowseLocationChanged = false;
        state.GridScaleChanged = false;
        state.RequestFocusAnimationClipEditor = false;
        state.RequestFocusAnimatorControllerEditor = false;
        state.HoveredFolderRelativePathForExternalDrop.clear();
        Internal::SetProjectSearchFilter(state, std::string(state.SearchBuffer.data()));
        Internal::ClearProjectSearchMatchCache(state);
        state.GridScale = std::clamp(state.GridScale, 0.0f, 1.80f);

        const auto rootResult = Assets::FindProjectRootFromWorkingDirectory();
        if (rootResult.IsFailure())
        {
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Could not find Assets folder.");
            ImGui::End();
            EditorPanelStyle::PopPanelVisualStyle();
            return;
        }

        const std::filesystem::path assetsDirectory = rootResult.GetValue() / "Assets";
        std::error_code errorCode;
        if (!std::filesystem::exists(assetsDirectory, errorCode) || !std::filesystem::is_directory(assetsDirectory, errorCode))
        {
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Assets directory not found.");
            ImGui::End();
            EditorPanelStyle::PopPanelVisualStyle();
            return;
        }

        if (!state.ActiveFolderRelativePath.empty())
        {
            const std::filesystem::path activeFolderAbsolutePath = assetsDirectory / state.ActiveFolderRelativePath;
            if (!std::filesystem::exists(activeFolderAbsolutePath, errorCode) || !std::filesystem::is_directory(activeFolderAbsolutePath, errorCode))
            {
                state.ActiveFolderRelativePath.clear();
                state.BrowseLocationChanged = true;
            }
        }

        const size_t selectedCount = !state.MultiSelectedSubSpriteKeys.empty()
            ? state.MultiSelectedSubSpriteKeys.size()
            : state.MultiSelectedAssetKeys.size();

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 6.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 6.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 18.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.10f, 0.15f, 0.92f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.20f, 0.26f, 0.36f, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.10f, 0.13f, 0.18f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.13f, 0.17f, 0.24f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.16f, 0.22f, 0.32f, 0.55f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.24f, 0.33f, 0.48f, 0.75f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.27f, 0.38f, 0.56f, 0.95f));

        if (ImGui::BeginChild("##ProjectToolbar", ImVec2(0.0f, 112.0f), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
        {
            ImGui::TextUnformatted("Project Assets");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.55f, 0.72f, 0.98f, 1.0f), "(%zu selected)", selectedCount);

            ImGui::TextColored(ImVec4(0.63f, 0.68f, 0.78f, 1.0f),
                               Internal::HasProjectSearchFilter(state)
                                   ? "Filter active: showing matching assets and folders."
                                   : "Organize, rename, drag, and right-click to manage assets.");

            if (ImGui::Button("Refresh"))
                InvalidateProjectDirectoryCache(state);
            ImGui::SameLine();
            if (ImGui::Button("Collapse Folders"))
            {
                for (auto& [folderKey, expanded] : state.ExpandedFolderState)
                {
                    (void)folderKey;
                    expanded = false;
                }
                state.AssetsRootExpanded = true;
                state.TreeExpansionStateChanged = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Create"))
                ImGui::OpenPopup("##ProjectToolbarCreateMenu");
            if (ImGui::BeginPopup("##ProjectToolbarCreateMenu"))
            {
                Internal::DrawCreateMenuItems(state.ActiveFolderRelativePath, state, callbacks);
                ImGui::EndPopup();
            }
            ImGui::SameLine();
            if (!Internal::HasProjectSearchFilter(state))
                ImGui::SetNextItemWidth(-1.0f);
            else
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 70.0f);
            ImGui::InputTextWithHint("##ProjectSearch", "Filter assets by name or path...", state.SearchBuffer.data(), state.SearchBuffer.size());
            Internal::SetProjectSearchFilter(state, std::string(state.SearchBuffer.data()));
            if (Internal::HasProjectSearchFilter(state))
            {
                ImGui::SameLine();
                if (ImGui::Button("Clear"))
                {
                    state.SearchBuffer[0] = '\0';
                    Internal::SetProjectSearchFilter(state, {});
                    Internal::ClearProjectSearchMatchCache(state);
                }
            }

            ImGui::Spacing();
            ImGui::SetNextItemWidth(180.0f);
            if (ImGui::SliderFloat("Grid Scale", &state.GridScale, 0.0f, 1.80f, "%.2fx"))
            {
                state.GridScale = std::clamp(state.GridScale, 0.0f, 1.80f);
                state.GridScaleChanged = true;
            }
        }
        ImGui::EndChild();

        ImGui::Spacing();
        Internal::DrawProjectBrowserRegion(assetsDirectory, state, materialPreviewCache, selection, callbacks);

        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
        {
            const ImGuiIO& io = ImGui::GetIO();
            const bool canDeleteSelection =
                !io.WantTextInput &&
                !ImGui::IsAnyItemActive() &&
                !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
            if (canDeleteSelection &&
                ImGui::IsKeyPressed(ImGuiKey_Delete, false) &&
                !state.MultiSelectedAssetKeys.empty())
            {
                if (Internal::DeleteAssetKeysWithSceneHandling(state, assetsDirectory, state.MultiSelectedAssetKeys, onDeleteSceneAssetsRequested))
                    Internal::ClearProjectAssetSelection(state, selection);
            }
        }

        if (!state.PendingExternalDropPaths.empty() &&
            ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup))
        {
            const std::filesystem::path targetFolder = state.HoveredFolderRelativePathForExternalDrop;
            const bool importedAny = ProjectAssetOperations::ImportExternalPathsToFolder(
                state.PendingExternalDropPaths, targetFolder);
            if (importedAny)
            {
                InvalidateProjectDirectoryCache(state);
                LT_INFO("Imported {} external path(s) into Assets/{}",
                        state.PendingExternalDropPaths.size(),
                        targetFolder.generic_string());
            }
            state.PendingExternalDropPaths.clear();
        }

        DrawProjectFolderPopups(
            assetsDirectory,
            state,
            onCreateMaterialRequested,
            onCreateTilesetRequested,
            onCreateAudioMixerRequested,
            onCreateInputActionsRequested,
            onCreateAnimationClipRequested,
            onCreateAnimatorControllerRequested,
            onAssetRenamed);

        ImGui::PopStyleColor(7);
        ImGui::PopStyleVar(5);
        Internal::ClearProjectSearchMatchCache(state);

        ImGui::End();
        EditorPanelStyle::PopPanelVisualStyle();
    }
}
