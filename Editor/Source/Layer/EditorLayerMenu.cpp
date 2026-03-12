#include "PrecompiledHeader.h"
#include "EditorLayer.h"

#include "EditorAssetNaming.h"
#include "Assets/AssetImportPipeline.h"
#include "Core/Debug/Log.h"
#include "EditorMenuBar.h"
#include "imgui/imgui.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <vector>

namespace Limitless
{
    namespace
    {
        bool IsPrefabAssetKeyForMenu(const std::string& assetKey)
        {
            if (assetKey.empty())
                return false;
            std::string lowerKey = assetKey;
            std::replace(lowerKey.begin(), lowerKey.end(), '\\', '/');
            std::transform(lowerKey.begin(), lowerKey.end(), lowerKey.begin(), [](unsigned char character) {
                return static_cast<char>(std::tolower(character));
            });
            return lowerKey.ends_with(".prefab.json");
        }

        std::string SceneDisplayNameFromMenuFileName(const std::string& fileName)
        {
            return EditorAssetNaming::GetAssetDisplayNameFromFileName(fileName);
        }
    }

    void EditorLayer::DrawMenuBar()
    {
        const bool isEditingPrefabAsset = IsPrefabAssetKeyForMenu(m_CurrentSceneAssetKey);
        const bool canReturnFromPrefabMode = isEditingPrefabAsset && !m_PrefabModeReturnSceneAssetKey.empty();
        const bool canApplyPrefabToInstances = canReturnFromPrefabMode;

        EditorMenuBar::Draw(
            m_PlayModeState,
            m_ShowScenePanel,
            m_ShowInspectorPanel,
            m_ShowSceneView,
            m_ShowGameView,
            m_ShowProjectPanel,
            m_ShowDemoWindow,
            m_ShowEditorPreferencesWindow,
            m_ShowAssetDiagnosticsWindow,
            m_ShowPhysicsDiagnosticsWindow,
            m_ShowConsoleWindow,
            m_ShowEditorFpsOverlay,
            m_ShowGizmoToolbar,
            m_ShowPerformancePanel,
            m_ShowAnimationTimelinePanel,
            m_ShowAnimatorGraphPanel,
            m_TilePaletteState.PanelOpen,
            [this]() { EditorProjectDialog::RequestOpen(m_ProjectDialogState, EditorProjectDialog::ProjectDialogMode::Open); },
            [this]() { EditorProjectDialog::RequestOpen(m_ProjectDialogState, EditorProjectDialog::ProjectDialogMode::Create); },
            [this]() { m_ShowProjectSettingsWindow = true; },
            [this]() { m_ShowBuildSettingsWindow = true; },
            [this]() { BuildProjectScripts(); },
            [this]() {
                const auto result = Assets::AssetImportPipeline::ReimportChanged(true);
                if (result.IsFailure())
                {
                    LT_ERROR("Reimport Changed failed: {}", result.GetError().GetErrorMessage());
                    return;
                }
                const auto& s = result.GetValue();
                LT_INFO("Reimport Changed: discovered={} imported={} skipped={} missing={} errors={}",
                        s.DiscoveredFiles, s.Imported, s.SkippedUpToDate, s.MissingOnDisk, s.Errors);
            },
            [this]() {
                const auto result = Assets::AssetImportPipeline::ReimportAll(true);
                if (result.IsFailure())
                {
                    LT_ERROR("Reimport All failed: {}", result.GetError().GetErrorMessage());
                    return;
                }
                const auto& s = result.GetValue();
                LT_INFO("Reimport All: discovered={} imported={} skipped={} missing={} errors={}",
                        s.DiscoveredFiles, s.Imported, s.SkippedUpToDate, s.MissingOnDisk, s.Errors);
            },
            [this]() {
                const auto result = Assets::AssetImportPipeline::ValidateAssetDatabase();
                if (result.IsFailure())
                {
                    LT_ERROR("Validate Asset Database failed: {}", result.GetError().GetErrorMessage());
                    return;
                }

                const auto& issues = result.GetValue();
                if (issues.empty())
                {
                    LT_INFO("Asset Database validation: no issues found.");
                }
                else
                {
                    LT_WARN("Asset Database validation: {} issue(s) found.", issues.size());
                    for (const auto& issue : issues)
                    {
                        LT_WARN(" - {} (key='{}' guid='{}' path='{}')",
                                issue.Message, issue.Key, issue.Guid, issue.ResolvedPath);
                    }
                }
            },
            [this]() { NewScene(); },
            [this]() { SaveScene(); },
            [this]() { SaveSceneAs(); },
            [this]() { (void)m_EditorUndoService.Undo(); },
            [this]() { (void)m_EditorUndoService.Redo(); },
            m_EditorUndoService.CanUndo(),
            m_EditorUndoService.CanRedo(),
            m_EditorUndoService.GetUndoLabel(),
            m_EditorUndoService.GetRedoLabel(),
            [this]() { RequestPlayModeTransition(PendingPlayModeTransition::EnterPlay); },
            [this]() { RequestPlayModeTransition(PendingPlayModeTransition::EnterSimulate); },
            [this]() { RequestPlayModeTransition(PendingPlayModeTransition::Exit); },
            [this]() { RequestPlayModeTransition(PendingPlayModeTransition::TogglePause); },
            isEditingPrefabAsset,
            isEditingPrefabAsset
                ? SceneDisplayNameFromMenuFileName(std::filesystem::path(m_CurrentSceneAssetKey).filename().string())
                : std::string{},
            canReturnFromPrefabMode,
            [this]() { (void)ReturnFromPrefabMode(false); },
            canApplyPrefabToInstances,
            [this]() { (void)ApplyPrefabStageChangesToInstances(); },
            [this]() {
                Editor::EditorLayoutManager layoutManager;
                const std::vector<Editor::EditorLayoutDescriptor> layouts = layoutManager.ListLayouts();
                ImGui::TextDisabled("Current: %s", m_ActiveLayoutName.c_str());
                ImGui::Separator();

                for (const auto& layout : layouts)
                {
                    const bool selected = layout.Name == m_ActiveLayoutName;
                    if (ImGui::MenuItem(layout.Name.c_str(), nullptr, selected))
                        (void)LoadLayoutByName(layout.Name);
                }

                ImGui::Separator();
                const bool activeCustomLayout = layoutManager.IsCustomLayoutName(m_ActiveLayoutName);
                ImGui::BeginDisabled(!activeCustomLayout);
                if (ImGui::MenuItem("Save Layout"))
                    (void)SaveCurrentLayoutAs(m_ActiveLayoutName);
                ImGui::EndDisabled();

                if (ImGui::MenuItem("Save Layout As..."))
                    RequestOpenSaveLayoutPopup();

                ImGui::BeginDisabled(!activeCustomLayout);
                if (ImGui::MenuItem("Delete Layout..."))
                    RequestOpenDeleteLayoutPopup(m_ActiveLayoutName);
                ImGui::EndDisabled();

                if (ImGui::MenuItem("Revert to Default"))
                    ResetLayoutToDefault();
            },
            [this]() { ResetLayoutToDefault(); },
            [this]() { SpawnAdditionalInspectorPanel(); },
            [this]() { SpawnAdditionalProjectPanel(); });
    }
}
