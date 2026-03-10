#include "EditorLayoutManager.h"

#include "Core/Debug/Log.h"
#include "Platform/Platform.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <optional>

namespace Limitless::Editor
{
    namespace
    {
        using json = nlohmann::json;

        constexpr uint32_t kLayoutMetadataVersion = 1;
        constexpr const char* kLayoutMetadataFileName = "layout.json";
        constexpr const char* kLayoutIniFileName = "imgui.ini";

        std::string NormalizePathForStorage(const std::filesystem::path& path)
        {
            return path.lexically_normal().generic_string();
        }

        bool EqualsIgnoreCaseAscii(const std::string& left, const std::string& right)
        {
            if (left.size() != right.size())
                return false;

            for (size_t index = 0; index < left.size(); ++index)
            {
                if (std::tolower(static_cast<unsigned char>(left[index])) !=
                    std::tolower(static_cast<unsigned char>(right[index])))
                {
                    return false;
                }
            }

            return true;
        }

        std::filesystem::path ResolveFallbackUserDataPath()
        {
#if defined(LT_PLATFORM_WINDOWS)
            if (const char* appData = std::getenv("APPDATA"); appData && appData[0] != '\0')
                return std::filesystem::path(appData) / "Limitless";
#else
            if (const char* home = std::getenv("HOME"); home && home[0] != '\0')
                return std::filesystem::path(home) / ".local" / "share" / "Limitless";
#endif
            return {};
        }

        std::filesystem::path ResolveEditorUserDataRoot()
        {
            const std::string userDataPath = PlatformDetection::GetUserDataPath();
            std::filesystem::path root = userDataPath.empty()
                ? ResolveFallbackUserDataPath()
                : std::filesystem::path(userDataPath);
            if (root.empty())
                return {};
            return root / "Editor";
        }

        bool ReadWindowStateFromJson(const json& root, EditorLayoutWindowState& outState)
        {
            if (!root.is_object())
                return false;

            outState = EditorLayoutManager::CreateDefaultWindowState();
            outState.ShowScenePanel = root.value("showScenePanel", outState.ShowScenePanel);
            outState.ShowInspectorPanel = root.value("showInspectorPanel", outState.ShowInspectorPanel);
            outState.ShowProjectPanel = root.value("showProjectPanel", outState.ShowProjectPanel);
            outState.ShowSceneView = root.value("showSceneView", outState.ShowSceneView);
            outState.ShowGameView = root.value("showGameView", outState.ShowGameView);
            outState.ShowProjectSettingsWindow = root.value("showProjectSettingsWindow", outState.ShowProjectSettingsWindow);
            outState.ShowBuildSettingsWindow = root.value("showBuildSettingsWindow", outState.ShowBuildSettingsWindow);
            outState.ShowAssetDiagnosticsWindow = root.value("showAssetDiagnosticsWindow", outState.ShowAssetDiagnosticsWindow);
            outState.ShowPhysicsDiagnosticsWindow = root.value("showPhysicsDiagnosticsWindow", outState.ShowPhysicsDiagnosticsWindow);
            outState.ShowConsoleWindow = root.value("showConsoleWindow", outState.ShowConsoleWindow);
            outState.ShowEditorFpsOverlay = root.value("showEditorFpsOverlay", outState.ShowEditorFpsOverlay);
            outState.ShowGizmoToolbar = root.value("showGizmoToolbar", outState.ShowGizmoToolbar);
            outState.ShowPerformancePanel = root.value("showPerformancePanel", outState.ShowPerformancePanel);
            outState.ShowAnimationTimelinePanel = root.value("showAnimationTimelinePanel", outState.ShowAnimationTimelinePanel);
            outState.ShowAnimatorGraphPanel = root.value("showAnimatorGraphPanel", outState.ShowAnimatorGraphPanel);
            outState.ShowTilePalettePanel = root.value("showTilePalettePanel", outState.ShowTilePalettePanel);
            outState.ShowSpriteEditorWindow = root.value("showSpriteEditorWindow", outState.ShowSpriteEditorWindow);
            outState.ShowDemoWindow = root.value("showDemoWindow", outState.ShowDemoWindow);
            return true;
        }

        json WriteWindowStateToJson(const EditorLayoutWindowState& state)
        {
            json root;
            root["showScenePanel"] = state.ShowScenePanel;
            root["showInspectorPanel"] = state.ShowInspectorPanel;
            root["showProjectPanel"] = state.ShowProjectPanel;
            root["showSceneView"] = state.ShowSceneView;
            root["showGameView"] = state.ShowGameView;
            root["showProjectSettingsWindow"] = state.ShowProjectSettingsWindow;
            root["showBuildSettingsWindow"] = state.ShowBuildSettingsWindow;
            root["showAssetDiagnosticsWindow"] = state.ShowAssetDiagnosticsWindow;
            root["showPhysicsDiagnosticsWindow"] = state.ShowPhysicsDiagnosticsWindow;
            root["showConsoleWindow"] = state.ShowConsoleWindow;
            root["showEditorFpsOverlay"] = state.ShowEditorFpsOverlay;
            root["showGizmoToolbar"] = state.ShowGizmoToolbar;
            root["showPerformancePanel"] = state.ShowPerformancePanel;
            root["showAnimationTimelinePanel"] = state.ShowAnimationTimelinePanel;
            root["showAnimatorGraphPanel"] = state.ShowAnimatorGraphPanel;
            root["showTilePalettePanel"] = state.ShowTilePalettePanel;
            root["showSpriteEditorWindow"] = state.ShowSpriteEditorWindow;
            root["showDemoWindow"] = state.ShowDemoWindow;
            return root;
        }

        bool LoadWindowStateFromMetadataFile(const std::filesystem::path& metadataPath, EditorLayoutWindowState& outState)
        {
            try
            {
                std::error_code ec;
                if (!std::filesystem::exists(metadataPath, ec))
                    return false;

                std::ifstream in(metadataPath, std::ios::in | std::ios::binary);
                if (!in.is_open())
                    return false;

                json root;
                in >> root;
                if (!root.is_object())
                    return false;
                if (root.value("version", 0u) != kLayoutMetadataVersion)
                    return false;
                const auto windowStateIt = root.find("windowState");
                if (windowStateIt == root.end())
                    return false;
                return ReadWindowStateFromJson(*windowStateIt, outState);
            }
            catch (...)
            {
                return false;
            }
        }

        bool SaveWindowStateToMetadataFile(const std::filesystem::path& metadataPath,
                                           const std::string& layoutName,
                                           const EditorLayoutWindowState& state)
        {
            try
            {
                std::error_code ec;
                std::filesystem::create_directories(metadataPath.parent_path(), ec);
                if (ec)
                    return false;

                json root;
                root["version"] = kLayoutMetadataVersion;
                root["name"] = layoutName;
                root["windowState"] = WriteWindowStateToJson(state);

                const std::filesystem::path tmpPath = metadataPath.string() + ".tmp";
                {
                    std::ofstream out(tmpPath, std::ios::out | std::ios::binary | std::ios::trunc);
                    if (!out.is_open())
                        return false;
                    out << root.dump(2);
                    out.flush();
                }

                std::filesystem::rename(tmpPath, metadataPath, ec);
                if (ec)
                {
                    ec.clear();
                    std::filesystem::remove(metadataPath, ec);
                    ec.clear();
                    std::filesystem::rename(tmpPath, metadataPath, ec);
                }

                return !ec;
            }
            catch (...)
            {
                return false;
            }
        }

        std::string MakeFilesystemSlug(const std::string& text)
        {
            std::string slug;
            slug.reserve(text.size());
            bool previousWasSeparator = false;
            for (unsigned char ch : text)
            {
                if (std::isalnum(ch))
                {
                    slug.push_back(static_cast<char>(std::tolower(ch)));
                    previousWasSeparator = false;
                    continue;
                }

                if (!previousWasSeparator)
                {
                    slug.push_back('-');
                    previousWasSeparator = true;
                }
            }

            while (!slug.empty() && slug.front() == '-')
                slug.erase(slug.begin());
            while (!slug.empty() && slug.back() == '-')
                slug.pop_back();
            if (slug.empty())
                slug = "layout";
            return slug;
        }
    }

    const std::string& EditorLayoutManager::GetDefaultLayoutName()
    {
        static const std::string s_DefaultLayoutName = "Default";
        return s_DefaultLayoutName;
    }

    EditorLayoutWindowState EditorLayoutManager::CreateDefaultWindowState()
    {
        return {};
    }

    std::string EditorLayoutManager::NormalizeLayoutName(const std::string& rawName)
    {
        std::string name = rawName;
        while (!name.empty() && std::isspace(static_cast<unsigned char>(name.front())))
            name.erase(name.begin());
        while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back())))
            name.pop_back();
        if (name.empty())
            return {};
        if (EqualsIgnoreCaseAscii(name, GetDefaultLayoutName()))
            return GetDefaultLayoutName();
        return name;
    }

    std::filesystem::path EditorLayoutManager::GetProjectWorkingLayoutPath(const std::filesystem::path& projectRoot)
    {
        if (projectRoot.empty())
            return {};
        return projectRoot / "Project" / "Settings" / "EditorLayout.current.ini";
    }

    std::filesystem::path EditorLayoutManager::GetStorageRoot() const
    {
        return ResolveEditorUserDataRoot() / "Layouts";
    }

    std::filesystem::path EditorLayoutManager::GetCustomLayoutsRoot() const
    {
        return GetStorageRoot() / "Saved";
    }

    std::vector<std::pair<EditorLayoutDescriptor, std::filesystem::path>> EditorLayoutManager::ListCustomLayoutsWithDirectories() const
    {
        std::vector<std::pair<EditorLayoutDescriptor, std::filesystem::path>> layouts;
        const std::filesystem::path root = GetCustomLayoutsRoot();
        if (root.empty())
            return layouts;

        std::error_code ec;
        if (!std::filesystem::exists(root, ec) || !std::filesystem::is_directory(root, ec))
            return layouts;

        for (const auto& entry : std::filesystem::directory_iterator(root, ec))
        {
            if (ec)
                break;
            if (!entry.is_directory())
                continue;

            const std::filesystem::path metadataPath = entry.path() / kLayoutMetadataFileName;
            const std::filesystem::path iniPath = entry.path() / kLayoutIniFileName;
            if (!std::filesystem::exists(metadataPath, ec) || !std::filesystem::exists(iniPath, ec))
                continue;

            try
            {
                std::ifstream in(metadataPath, std::ios::in | std::ios::binary);
                if (!in.is_open())
                    continue;

                json rootJson;
                in >> rootJson;
                if (!rootJson.is_object())
                    continue;
                if (rootJson.value("version", 0u) != kLayoutMetadataVersion)
                    continue;
                const std::string name = NormalizeLayoutName(rootJson.value("name", std::string{}));
                if (name.empty() || name == GetDefaultLayoutName())
                    continue;

                const std::filesystem::path layoutDirectory = entry.path();
                layouts.emplace_back(EditorLayoutDescriptor{ name, false, false }, layoutDirectory);
            }
            catch (...)
            {
            }
        }

        std::sort(layouts.begin(), layouts.end(), [](const auto& left, const auto& right) {
            return left.first.Name < right.first.Name;
        });
        return layouts;
    }

    std::vector<EditorLayoutDescriptor> EditorLayoutManager::ListLayouts() const
    {
        std::vector<EditorLayoutDescriptor> layouts;
        layouts.push_back(EditorLayoutDescriptor{ GetDefaultLayoutName(), true, true });
        const auto customLayouts = ListCustomLayoutsWithDirectories();
        for (const auto& [descriptor, _] : customLayouts)
            layouts.push_back(descriptor);
        return layouts;
    }

    bool EditorLayoutManager::TryResolveCustomLayoutDirectory(const std::string& layoutName,
                                                              std::filesystem::path& outDirectory,
                                                              EditorLayoutWindowState* outWindowState) const
    {
        const std::string normalizedName = NormalizeLayoutName(layoutName);
        if (normalizedName.empty() || normalizedName == GetDefaultLayoutName())
            return false;

        const auto layouts = ListCustomLayoutsWithDirectories();
        const auto it = std::find_if(layouts.begin(), layouts.end(), [&](const auto& entry) {
            return entry.first.Name == normalizedName;
        });
        if (it == layouts.end())
            return false;

        outDirectory = it->second;
        if (outWindowState)
            (void)LoadWindowStateFromMetadataFile(outDirectory / kLayoutMetadataFileName, *outWindowState);
        return true;
    }

    std::filesystem::path EditorLayoutManager::AllocateCustomLayoutDirectory(const std::string& layoutName) const
    {
        const std::filesystem::path root = GetCustomLayoutsRoot();
        if (root.empty())
            return {};

        std::error_code ec;
        std::filesystem::create_directories(root, ec);
        if (ec)
            return {};

        const std::string slug = MakeFilesystemSlug(layoutName);
        std::filesystem::path candidate = root / slug;
        int suffix = 2;
        while (std::filesystem::exists(candidate, ec))
        {
            ec.clear();
            candidate = root / (slug + "-" + std::to_string(suffix));
            ++suffix;
        }
        return candidate;
    }

    bool EditorLayoutManager::IsCustomLayoutName(const std::string& layoutName) const
    {
        std::filesystem::path directory;
        return TryResolveCustomLayoutDirectory(layoutName, directory, nullptr);
    }

    bool EditorLayoutManager::SaveCustomLayout(const std::string& layoutName,
                                               const std::filesystem::path& sourceIniPath,
                                               const EditorLayoutWindowState& windowState)
    {
        const std::string normalizedName = NormalizeLayoutName(layoutName);
        if (normalizedName.empty() || normalizedName == GetDefaultLayoutName())
            return false;

        std::error_code ec;
        if (!std::filesystem::exists(sourceIniPath, ec))
            return false;

        std::filesystem::path directory;
        if (!TryResolveCustomLayoutDirectory(normalizedName, directory, nullptr))
            directory = AllocateCustomLayoutDirectory(normalizedName);
        if (directory.empty())
            return false;

        std::filesystem::create_directories(directory, ec);
        if (ec)
            return false;

        const std::filesystem::path destinationIniPath = directory / kLayoutIniFileName;
        std::filesystem::copy_file(sourceIniPath, destinationIniPath, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec)
            return false;

        return SaveWindowStateToMetadataFile(directory / kLayoutMetadataFileName, normalizedName, windowState);
    }

    bool EditorLayoutManager::DeleteCustomLayout(const std::string& layoutName)
    {
        std::filesystem::path directory;
        if (!TryResolveCustomLayoutDirectory(layoutName, directory, nullptr))
            return false;

        std::error_code ec;
        std::filesystem::remove_all(directory, ec);
        return !ec;
    }

    bool EditorLayoutManager::LoadLayoutToPath(const std::string& layoutName,
                                               const std::filesystem::path& defaultLayoutIniPath,
                                               const std::filesystem::path& destinationIniPath,
                                               EditorLayoutWindowState& outWindowState) const
    {
        const std::string normalizedName = NormalizeLayoutName(layoutName);
        std::filesystem::path sourceIniPath;
        outWindowState = CreateDefaultWindowState();

        if (normalizedName.empty() || normalizedName == GetDefaultLayoutName())
        {
            sourceIniPath = defaultLayoutIniPath;
        }
        else
        {
            std::filesystem::path directory;
            if (!TryResolveCustomLayoutDirectory(normalizedName, directory, &outWindowState))
                return false;
            sourceIniPath = directory / kLayoutIniFileName;
        }

        std::error_code ec;
        if (sourceIniPath.empty() || !std::filesystem::exists(sourceIniPath, ec))
            return false;

        std::filesystem::create_directories(destinationIniPath.parent_path(), ec);
        if (ec)
            return false;

        std::filesystem::copy_file(sourceIniPath, destinationIniPath, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec)
            return false;

        if (normalizedName.empty() || normalizedName == GetDefaultLayoutName())
            outWindowState = CreateDefaultWindowState();
        return true;
    }
}
