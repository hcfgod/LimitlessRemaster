#pragma once

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace Limitless::Editor
{
    struct EditorLayoutWindowState final
    {
        bool ShowScenePanel = true;
        bool ShowInspectorPanel = true;
        bool ShowProjectPanel = true;
        bool ShowSceneView = true;
        bool ShowGameView = true;
        bool ShowProjectSettingsWindow = false;
        bool ShowBuildSettingsWindow = false;
        bool ShowAssetDiagnosticsWindow = false;
        bool ShowPhysicsDiagnosticsWindow = true;
        bool ShowConsoleWindow = true;
        bool ShowEditorFpsOverlay = true;
        bool ShowGizmoToolbar = true;
        bool ShowPerformancePanel = false;
        bool ShowAnimationTimelinePanel = true;
        bool ShowAnimatorGraphPanel = true;
        bool ShowTilePalettePanel = true;
        bool ShowSpriteEditorWindow = false;
        bool ShowDemoWindow = false;
    };

    struct EditorLayoutDescriptor final
    {
        std::string Name;
        bool IsDefault = false;
        bool IsProtected = false;
    };

    class EditorLayoutManager final
    {
    public:
        static const std::string& GetDefaultLayoutName();
        static EditorLayoutWindowState CreateDefaultWindowState();
        static std::string NormalizeLayoutName(const std::string& rawName);
        static std::filesystem::path GetProjectWorkingLayoutPath(const std::filesystem::path& projectRoot);

        std::vector<EditorLayoutDescriptor> ListLayouts() const;
        bool IsCustomLayoutName(const std::string& layoutName) const;
        bool SaveCustomLayout(const std::string& layoutName,
                              const std::filesystem::path& sourceIniPath,
                              const EditorLayoutWindowState& windowState);
        bool DeleteCustomLayout(const std::string& layoutName);
        bool LoadLayoutToPath(const std::string& layoutName,
                              const std::filesystem::path& defaultLayoutIniPath,
                              const std::filesystem::path& destinationIniPath,
                              EditorLayoutWindowState& outWindowState) const;

    private:
        std::filesystem::path GetStorageRoot() const;
        std::filesystem::path GetCustomLayoutsRoot() const;
        std::vector<std::pair<EditorLayoutDescriptor, std::filesystem::path>> ListCustomLayoutsWithDirectories() const;
        bool TryResolveCustomLayoutDirectory(const std::string& layoutName,
                                             std::filesystem::path& outDirectory,
                                             EditorLayoutWindowState* outWindowState = nullptr) const;
        std::filesystem::path AllocateCustomLayoutDirectory(const std::string& layoutName) const;
    };
}
