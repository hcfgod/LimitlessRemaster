#include "EditorInspectorPanel.h"

#include "EditorAssetNaming.h"
#include "EditorInspectorPanelAssetInspectors.h"
#include "EditorInspectorPanelEntityComponents.h"
#include "Undo/EditorUndoService.h"
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
#include <string_view>
#include <unordered_set>
#include <vector>
#include <nlohmann/json.hpp>

#ifdef LT_PLATFORM_WINDOWS
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#else
    #include <sys/wait.h>
#endif

namespace Limitless::EditorInspectorPanel
{
    namespace
    {
        // Asset inspector implementations moved to EditorInspectorPanelAssetInspectors.cpp
        constexpr const char* kSceneEntityPayload = "SCENE_ENTITY";

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

        bool LooksLikePrefabAssetKey(const std::string& value)
        {
            constexpr std::string_view prefabExtension = ".prefab.json";
            if (value.size() >= prefabExtension.size() &&
                value.compare(value.size() - prefabExtension.size(), prefabExtension.size(), prefabExtension) == 0)
            {
                return true;
            }

            const auto record = Assets::AssetDatabase::GetInstance().FindByKey(value);
            return record.IsSuccess() && record.GetValue().Type == Assets::AssetType::Prefab;
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

        constexpr size_t kNativeScriptEditorBufferSize = 256 * 1024;

        struct NativeScriptAuthoringState
        {
            bool EditorWindowOpen = false;
            bool FocusEditorWindowRequested = false;
            bool ShowDebugInfo = false;
            bool SelectHeaderTabRequested = false;
            bool SelectSourceTabRequested = false;
            bool HeaderDirty = false;
            bool SourceDirty = false;
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
            std::unique_ptr<std::thread> BuildThread;

            ~NativeScriptAuthoringState()
            {
                if (BuildThread && BuildThread->joinable())
                    BuildThread->join();
            }
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

        int RunBuildScriptBlocking(const std::filesystem::path& projectRoot, const std::string& configuration, const std::string& platform)
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
            const std::string scriptCommand =
                "cd \"" + projectRoot.string() + "\" && bash \"Scripts/build-scriptcore-unix.sh\" --config \"" + configuration + "\" --platform \"" + platform + "\"";

            const int systemResult = std::system(scriptCommand.c_str());
            if (systemResult == -1)
                return 1;

            if (WIFEXITED(systemResult))
                return WEXITSTATUS(systemResult);

            return systemResult;
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
            state.BuildThread = std::make_unique<std::thread>([&state, root = engineRoot.value(), configuration, platform]() {
                const int exitCode = RunBuildScriptBlocking(root, configuration, platform);
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
            if (typeName == "Limitless::Entity" || typeName == "Entity")
            {
                ScriptEntityReference value{};
                if (!initializer.empty())
                {
                    if (initializer == "{}" ||
                        initializer == "Entity{}" ||
                        initializer == "Limitless::Entity{}" ||
                        initializer == "Entity()" ||
                        initializer == "Limitless::Entity()")
                    {
                        value.Tag.clear();
                    }
                    else if (initializer.size() >= 2 && initializer.front() == '"' && initializer.back() == '"')
                    {
                        const std::string parsedValue = initializer.substr(1, initializer.size() - 2);
                        if (LooksLikePrefabAssetKey(parsedValue))
                            value.PrefabAssetKey = parsedValue;
                        else
                            value.Tag = parsedValue;
                    }
                    else
                    {
                        return false;
                    }
                }
                outValue = value;
                return true;
            }
            if (typeName == "Limitless::Prefab" ||
                typeName == "Prefab" ||
                typeName == "Limitless::ScriptPrefabReference" ||
                typeName == "ScriptPrefabReference")
            {
                Prefab value{};
                if (!initializer.empty())
                {
                    if (initializer == "{}" ||
                        initializer == "Prefab{}" ||
                        initializer == "Limitless::Prefab{}" ||
                        initializer == "Prefab()" ||
                        initializer == "Limitless::Prefab()" ||
                        initializer == "ScriptPrefabReference{}" ||
                        initializer == "Limitless::ScriptPrefabReference{}" ||
                        initializer == "ScriptPrefabReference()" ||
                        initializer == "Limitless::ScriptPrefabReference()")
                    {
                        value.AssetKey.clear();
                    }
                    else if (initializer.size() >= 2 && initializer.front() == '"' && initializer.back() == '"')
                    {
                        value.AssetKey = initializer.substr(1, initializer.size() - 2);
                    }
                    else
                    {
                        return false;
                    }
                }
                outValue = std::move(value);
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
            // Limitless::Entity TargetEntity;
            // Limitless::Entity EnemyPrefab = "Assets/Prefabs/Enemy.prefab.json";
            // Limitless::Prefab LegacyPrefab = "Assets/Prefabs/Enemy.prefab.json";
            const std::regex fieldPattern(
                R"(^\s*(?:const\s+)?(?:static\s+)?(float|int32_t|int|bool|glm::vec3|std::string|Limitless::Entity|Entity|Limitless::Prefab|Prefab|Limitless::ScriptPrefabReference|ScriptPrefabReference)\s+([A-Za-z_][A-Za-z0-9_]*)\s*(?:=\s*([^;]+))?\s*;\s*$)");

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
                R"LT(GetExposed(Float|Integer|Boolean|Vector3|String|Entity|Prefab)\s*\(\s*"([A-Za-z_][A-Za-z0-9_]*)"\s*,\s*([^)]+)\))LT");

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
                else if (functionSuffix == "Entity")
                {
                    ScriptEntityReference value{};
                    if (fallbackExpression == "{}" || fallbackExpression == "Entity{}" || fallbackExpression == "Limitless::Entity{}")
                    {
                        parsed = true;
                    }
                    else if (fallbackExpression.size() >= 2 && fallbackExpression.front() == '"' && fallbackExpression.back() == '"')
                    {
                        const std::string parsedValue = fallbackExpression.substr(1, fallbackExpression.size() - 2);
                        if (LooksLikePrefabAssetKey(parsedValue))
                            value.PrefabAssetKey = parsedValue;
                        else
                            value.Tag = parsedValue;
                        parsed = true;
                    }

                    if (parsed)
                        fieldDefinition.DefaultValue = std::move(value);
                }
                else if (functionSuffix == "Prefab")
                {
                    Prefab value{};
                    if (fallbackExpression == "{}" ||
                        fallbackExpression == "Prefab{}" ||
                        fallbackExpression == "Limitless::Prefab{}" ||
                        fallbackExpression == "ScriptPrefabReference{}" ||
                        fallbackExpression == "Limitless::ScriptPrefabReference{}")
                    {
                        parsed = true;
                    }
                    else if (fallbackExpression.size() >= 2 && fallbackExpression.front() == '"' && fallbackExpression.back() == '"')
                    {
                        value.AssetKey = fallbackExpression.substr(1, fallbackExpression.size() - 2);
                        parsed = true;
                    }

                    if (parsed)
                        fieldDefinition.DefaultValue = std::move(value);
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
                {
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
                    else if (std::holds_alternative<Prefab>(field.DefaultValue))
                    {
                        if (const auto* entityReference = std::get_if<ScriptEntityReference>(&found->second))
                        {
                            Prefab migratedReference{};
                            migratedReference.AssetKey = entityReference->PrefabAssetKey;
                            found->second = std::move(migratedReference);
                            continue;
                        }
                    }

                    found->second = field.DefaultValue;
                }
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
            state.HeaderDirty = false;
            state.SourceDirty = false;
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
                "    float RotationSpeed = 90.0f;\n"
                "    Limitless::Entity TargetEntity;  // Assign via Inspector drag-drop or dropdown\n\n"
                "protected:\n"
                "    LT_BEGIN_AUTO_EXPOSED_FIELD_SYNC()\n"
                "        LT_AUTO_EXPOSED_FIELD(RotationSpeed)\n"
                "        LT_AUTO_EXPOSED_FIELD(TargetEntity)\n"
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
                "    // GetComponent -- throws if missing\n"
                "    auto& transform = GetComponent<Limitless::TransformComponent>();\n"
                "    transform.Rotation.z += RotationSpeed * deltaTime;\n"
                "    if (transform.Rotation.z > 360.0f)\n"
                "        transform.Rotation.z -= 360.0f;\n\n"
                "    // TryGetComponent -- null-safe, returns nullptr if missing\n"
                "    if (auto* targetTransform = TargetEntity.TryGetComponent<Limitless::TransformComponent>())\n"
                "    {\n"
                "        // Access the target entity's transform safely\n"
                "        (void)targetTransform->Position;\n"
                "    }\n"
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
            const bool hasUnsavedChanges = state.HeaderDirty || state.SourceDirty;
            const std::string windowTitle = hasUnsavedChanges
                ? "Native Script Editor*"
                : "Native Script Editor";
            if (!ImGui::Begin(windowTitle.c_str(), &state.EditorWindowOpen))
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

            const auto saveEditorFiles = [&](bool forceBuildAfterSave) -> bool
            {
                std::string saveError;
                const bool headerSaved = SaveBufferToTextFile(state.HeaderPath, state.HeaderBuffer, saveError);
                const bool sourceSaved = SaveBufferToTextFile(state.SourcePath, state.SourceBuffer, saveError);
                if (!(headerSaved && sourceSaved))
                {
                    state.StatusMessage = saveError;
                    state.StatusIsError = true;
                    return false;
                }

                state.HeaderDirty = false;
                state.SourceDirty = false;

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

                const bool shouldBuildNow = canBuild && (forceBuildAfterSave || state.AutoBuildAfterSave);
                if (shouldBuildNow)
                    (void)TriggerNativeScriptsBuild(state);

                return canBuild;
            };

            const auto reloadEditorFiles = [&]() -> bool
            {
                std::string reloadError;
                const bool headerLoaded = LoadTextFileIntoBuffer(state.HeaderPath, state.HeaderBuffer, reloadError);
                const bool sourceLoaded = LoadTextFileIntoBuffer(state.SourcePath, state.SourceBuffer, reloadError);
                if (headerLoaded && sourceLoaded)
                {
                    state.HeaderDirty = false;
                    state.SourceDirty = false;
                    state.StatusMessage = "Reloaded script files from disk.";
                    state.StatusIsError = false;
                    return true;
                }

                state.StatusMessage = reloadError;
                state.StatusIsError = true;
                return false;
            };

            const ImGuiIO& io = ImGui::GetIO();
            const bool shortcutModifierDown = io.KeyCtrl || io.KeySuper;
            const bool editorWindowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
            const bool saveShortcutPressed = editorWindowFocused && shortcutModifierDown && ImGui::IsKeyPressed(ImGuiKey_S, false);
            const bool buildShortcutPressed = editorWindowFocused && shortcutModifierDown && ImGui::IsKeyPressed(ImGuiKey_B, false);
            const bool reloadShortcutPressed = editorWindowFocused && shortcutModifierDown && ImGui::IsKeyPressed(ImGuiKey_R, false);

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
            ImGui::TextDisabled("Shortcuts: Ctrl+S Save, Ctrl+B Save+Build, Ctrl+R Reload");
            ImGui::Checkbox("Auto Build On Save", &state.AutoBuildAfterSave);
            ImGui::SameLine();
            ImGui::TextDisabled("Unsaved: %s", hasUnsavedChanges ? "Yes" : "No");
            if (state.BuildInProgress.load(std::memory_order_relaxed))
                ImGui::TextDisabled("Build in progress...");

            if (ImGui::Button("Save Files", ImVec2(140.0f, 0.0f)))
                (void)saveEditorFiles(false);
            ImGui::SameLine();
            if (ImGui::Button("Reload From Disk", ImVec2(160.0f, 0.0f)))
                (void)reloadEditorFiles();
            ImGui::SameLine();
            ImGui::BeginDisabled(state.BuildInProgress.load(std::memory_order_relaxed));
            if (ImGui::Button("Save + Build", ImVec2(150.0f, 0.0f)))
                (void)saveEditorFiles(true);
            ImGui::EndDisabled();

            if (saveShortcutPressed)
                (void)saveEditorFiles(false);
            if (reloadShortcutPressed)
                (void)reloadEditorFiles();
            if (!state.BuildInProgress.load(std::memory_order_relaxed) && buildShortcutPressed)
                (void)saveEditorFiles(true);

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

                const std::string headerTabLabel = state.HeaderDirty ? "Header (.h)*" : "Header (.h)";
                const std::string sourceTabLabel = state.SourceDirty ? "Source (.cpp)*" : "Source (.cpp)";
                const float editorHeight = std::max(240.0f, ImGui::GetContentRegionAvail().y - 12.0f);

                if (ImGui::BeginTabItem(headerTabLabel.c_str(), nullptr, headerTabFlags))
                {
                    ImGui::InputTextMultiline("##NativeScriptHeaderEditor",
                                              state.HeaderBuffer.data(),
                                              state.HeaderBuffer.size(),
                                              ImVec2(-1.0f, editorHeight),
                                              ImGuiInputTextFlags_AllowTabInput);
                    if (ImGui::IsItemEdited())
                        state.HeaderDirty = true;
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem(sourceTabLabel.c_str(), nullptr, sourceTabFlags))
                {
                    ImGui::InputTextMultiline("##NativeScriptSourceEditor",
                                              state.SourceBuffer.data(),
                                              state.SourceBuffer.size(),
                                              ImVec2(-1.0f, editorHeight),
                                              ImGuiInputTextFlags_AllowTabInput);
                    if (ImGui::IsItemEdited())
                        state.SourceDirty = true;
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
              std::string& selectedNativeScriptAssetKey,
              std::string& selectedPrefabAssetKey,
              std::string& selectedTilesetAssetKey,
              std::string& selectedAudioMixerAssetKey,
              std::string& selectedInputActionsAssetKey,
              std::string& selectedAnimationClipAssetKey,
              std::string& selectedAnimatorControllerAssetKey,
              EditorUndoService* undoService)
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

        if (scene && selectedEntity != entt::null && scene->IsValid(selectedEntity))
        {
            selectedInputActionsAssetKey.clear();
            selectedAudioMixerAssetKey.clear();
            selectedMaterialAssetKey.clear();
            selectedTextureAssetKey.clear();
            cachedTextureAsset.reset();
            cachedMaterialAsset.reset();
            selectedNativeScriptAssetKey.clear();
            selectedPrefabAssetKey.clear();
            selectedTilesetAssetKey.clear();
            selectedAnimationClipAssetKey.clear();
            selectedAnimatorControllerAssetKey.clear();
        }

        if (!selectedInputActionsAssetKey.empty())
        {
            DrawInputActionsAssetInspector(selectedInputActionsAssetKey);
        }
        else if (!selectedAnimationClipAssetKey.empty())
        {
            DrawAnimationClipAssetInspector(selectedAnimationClipAssetKey);
        }
        else if (!selectedAnimatorControllerAssetKey.empty())
        {
            DrawAnimatorControllerAssetInspector(selectedAnimatorControllerAssetKey);
        }
        else if (!selectedAudioMixerAssetKey.empty())
        {
            DrawAudioMixerAssetInspector(selectedAudioMixerAssetKey);
        }
        else if (!selectedMaterialAssetKey.empty())
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
        else if (!selectedPrefabAssetKey.empty())
        {
            DrawPrefabAssetInspector(selectedPrefabAssetKey);
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
            PendingEntityComponentRemovals pendingRemovals{};
            bool removeNativeScriptComponent = false;
            DrawStandardEntityComponentSections(
                scene,
                registry,
                selectedEntity,
                texturePayloadId,
                audioPayloadId,
                materialPayloadId,
                fontPayloadId,
                pendingRemovals,
                undoService);

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
                                            if (undoService)
                                            {
                                                (void)undoService->ExecuteSceneMutation("Change Script Class", [&](Scene& mutableScene) {
                                                    auto* mutableNativeScript = mutableScene.GetRegistry().try_get<NativeScriptComponent>(selectedEntity);
                                                    if (!mutableNativeScript || scriptIndex >= mutableNativeScript->Scripts.size())
                                                        return false;
                                                    auto& mutableEntry = mutableNativeScript->Scripts[scriptIndex];
                                                    mutableEntry.ScriptClassName = scriptName;
                                                    mutableEntry.ScriptAssetRelativePath.clear();
                                                    mutableEntry.ExposedProperties.clear();
                                                    mutableEntry.RuntimeInitialized = false;
                                                    mutableEntry.RuntimeInstance.reset();
                                                    return true;
                                                });
                                            }
                                            else
                                            {
                                                scriptEntry.ScriptClassName = scriptName;
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

                                const bool selectedClassCompiled =
                                    scriptEntry.ScriptClassName.empty() || NativeScriptRegistry::HasScript(scriptEntry.ScriptClassName);
                                if (!selectedClassCompiled)
                                {
                                    ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f),
                                                       "Selected script is discovered in Assets but not compiled yet.");
                                    if (ImGui::Button("Build ScriptCore From Project Scripts"))
                                        (void)TriggerNativeScriptsBuild(nativeScriptAuthoringState);
                                }

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
                                    ImGui::TextDisabled("Supported public field types: float, int/int32_t, bool, glm::vec3, std::string, Limitless::Entity, Limitless::Prefab.");
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
                                        if (auto* floatValue = std::get_if<float>(&propertyValue))
                                        {
                                            ImGui::DragFloat(propertyName.c_str(), floatValue, 0.1f);
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
                                            ImGui::DragInt(propertyName.c_str(), integerValue, 1.0f);
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
                                            ImGui::Checkbox(propertyName.c_str(), booleanValue);
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
                                            ImGui::DragFloat3(propertyName.c_str(), &vectorValue->x, 0.1f);
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
                                            if (ImGui::InputText(propertyName.c_str(), textBuffer.data(), textBuffer.size()))
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
                                            if (ImGui::BeginCombo(propertyName.c_str(), previewLabel.c_str()))
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
                                            if (ImGui::BeginCombo(propertyName.c_str(), previewLabel.c_str()))
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

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::Button("Add Component", ImVec2(-1.0f, 0.0f)))
                ImGui::OpenPopup("AddComponentPopup");

            if (ImGui::BeginPopup("AddComponentPopup"))
            {
                const bool hasSpriteComponent = registry.all_of<SpriteComponent>(selectedEntity);
                const bool hasCameraComponent = registry.all_of<CameraComponent>(selectedEntity);
                const bool hasAudioListener2DComponent = registry.all_of<AudioListener2DComponent>(selectedEntity);
                const bool hasAudioSourceComponent = registry.all_of<AudioSourceComponent>(selectedEntity);
                const bool hasNativeScriptComponent = registry.all_of<NativeScriptComponent>(selectedEntity);
                const bool hasAnimatorComponent = registry.all_of<AnimatorComponent>(selectedEntity);
                const bool hasAnimationEventReceiverComponent = registry.all_of<AnimationEventReceiverComponent>(selectedEntity);
                const bool hasRigidbody2DComponent = registry.all_of<Rigidbody2DComponent>(selectedEntity);
                const bool hasBoxCollider2DComponent = registry.all_of<BoxCollider2DComponent>(selectedEntity);
                const bool hasCircleCollider2DComponent = registry.all_of<CircleCollider2DComponent>(selectedEntity);
                const bool hasJoint2DComponent = registry.all_of<Joint2DComponent>(selectedEntity);
                const bool hasDirectionalLight2DComponent = registry.all_of<DirectionalLight2DComponent>(selectedEntity);
                const bool hasPointLight2DComponent = registry.all_of<PointLight2DComponent>(selectedEntity);
                const bool hasShadowOccluder2DComponent = registry.all_of<ShadowOccluder2DComponent>(selectedEntity);
                const bool hasGrid2DComponent = registry.all_of<Grid2DComponent>(selectedEntity);
                const bool hasTilemapLayerComponent = registry.all_of<TilemapLayerComponent>(selectedEntity);
                const bool hasParticleEmitterComponent = registry.all_of<ParticleEmitterComponent>(selectedEntity);
                const bool hasCanvasComponent = registry.all_of<CanvasComponent>(selectedEntity);
                const bool hasRectTransformComponent = registry.all_of<RectTransformComponent>(selectedEntity);
                const bool hasUIImageComponent = registry.all_of<UIImageComponent>(selectedEntity);
                const bool hasUIPanelComponent = registry.all_of<UIPanelComponent>(selectedEntity);
                const bool hasUITextComponent = registry.all_of<UITextComponent>(selectedEntity);
                const bool hasUIButtonComponent = registry.all_of<UIButtonComponent>(selectedEntity);
                const bool hasUISliderComponent = registry.all_of<UISliderComponent>(selectedEntity);

                if (hasCanvasComponent)
                    ImGui::BeginDisabled();

                if (ImGui::MenuItem("Canvas"))
                {
                    if (undoService)
                        (void)undoService->ExecuteSceneMutation("Add Canvas Component", [&](Scene& mutableScene) {
                            auto& mutableRegistry = mutableScene.GetRegistry();
                            mutableRegistry.emplace<CanvasComponent>(selectedEntity);
                            if (!mutableRegistry.all_of<RectTransformComponent>(selectedEntity))
                                mutableRegistry.emplace<RectTransformComponent>(selectedEntity);
                            return true;
                        });
                    else {
                        registry.emplace<CanvasComponent>(selectedEntity);
                        if (!registry.all_of<RectTransformComponent>(selectedEntity))
                            registry.emplace<RectTransformComponent>(selectedEntity);
                    }
                }

                if (hasCanvasComponent)
                    ImGui::EndDisabled();

                if (hasRectTransformComponent)
                    ImGui::BeginDisabled();

                if (ImGui::MenuItem("RectTransform"))
                {
                    if (undoService)
                        (void)undoService->ExecuteSceneMutation("Add RectTransform Component", [&](Scene& mutableScene) {
                            mutableScene.GetRegistry().emplace<RectTransformComponent>(selectedEntity);
                            return true;
                        });
                    else
                        registry.emplace<RectTransformComponent>(selectedEntity);
                }

                if (hasRectTransformComponent)
                    ImGui::EndDisabled();

                if (hasUIImageComponent)
                    ImGui::BeginDisabled();

                if (ImGui::MenuItem("UI Image"))
                {
                    if (undoService)
                        (void)undoService->ExecuteSceneMutation("Add UIImage Component", [&](Scene& mutableScene) {
                            auto& mutableRegistry = mutableScene.GetRegistry();
                            mutableRegistry.emplace<UIImageComponent>(selectedEntity);
                            if (!mutableRegistry.all_of<RectTransformComponent>(selectedEntity))
                                mutableRegistry.emplace<RectTransformComponent>(selectedEntity);
                            if (!mutableRegistry.all_of<SpriteComponent>(selectedEntity))
                                mutableRegistry.emplace<SpriteComponent>(selectedEntity);
                            return true;
                        });
                    else {
                        registry.emplace<UIImageComponent>(selectedEntity);
                        if (!registry.all_of<RectTransformComponent>(selectedEntity))
                            registry.emplace<RectTransformComponent>(selectedEntity);
                        if (!registry.all_of<SpriteComponent>(selectedEntity))
                            registry.emplace<SpriteComponent>(selectedEntity);
                    }
                }

                if (hasUIImageComponent)
                    ImGui::EndDisabled();

                if (hasUIPanelComponent)
                    ImGui::BeginDisabled();

                if (ImGui::MenuItem("UI Panel"))
                {
                    if (undoService)
                        (void)undoService->ExecuteSceneMutation("Add UIPanel Component", [&](Scene& mutableScene) {
                            auto& mutableRegistry = mutableScene.GetRegistry();
                            auto& panel = mutableRegistry.emplace<UIPanelComponent>(selectedEntity);
                            if (!mutableRegistry.all_of<RectTransformComponent>(selectedEntity))
                                mutableRegistry.emplace<RectTransformComponent>(selectedEntity);
                            if (!mutableRegistry.all_of<SpriteComponent>(selectedEntity))
                            {
                                auto& sprite = mutableRegistry.emplace<SpriteComponent>(selectedEntity);
                                sprite.Color = panel.BackgroundColor;
                            }
                            return true;
                        });
                    else
                    {
                        auto& panel = registry.emplace<UIPanelComponent>(selectedEntity);
                        if (!registry.all_of<RectTransformComponent>(selectedEntity))
                            registry.emplace<RectTransformComponent>(selectedEntity);
                        if (!registry.all_of<SpriteComponent>(selectedEntity))
                        {
                            auto& sprite = registry.emplace<SpriteComponent>(selectedEntity);
                            sprite.Color = panel.BackgroundColor;
                        }
                    }
                }

                if (hasUIPanelComponent)
                    ImGui::EndDisabled();

                if (hasUITextComponent)
                    ImGui::BeginDisabled();

                if (ImGui::MenuItem("UI Text"))
                {
                    if (undoService)
                        (void)undoService->ExecuteSceneMutation("Add UIText Component", [&](Scene& mutableScene) {
                            auto& mutableRegistry = mutableScene.GetRegistry();
                            auto& uiText = mutableRegistry.emplace<UITextComponent>(selectedEntity);
                            uiText.FontFilePath = "Assets/Fonts/Default.ttf";
                            if (!mutableRegistry.all_of<RectTransformComponent>(selectedEntity))
                                mutableRegistry.emplace<RectTransformComponent>(selectedEntity);
                            return true;
                        });
                    else
                    {
                        auto& uiText = registry.emplace<UITextComponent>(selectedEntity);
                        uiText.FontFilePath = "Assets/Fonts/Default.ttf";
                        if (!registry.all_of<RectTransformComponent>(selectedEntity))
                            registry.emplace<RectTransformComponent>(selectedEntity);
                    }
                }

                if (hasUITextComponent)
                    ImGui::EndDisabled();

                if (hasUIButtonComponent)
                    ImGui::BeginDisabled();

                if (ImGui::MenuItem("UI Button"))
                {
                    if (undoService)
                        (void)undoService->ExecuteSceneMutation("Add UIButton Component", [&](Scene& mutableScene) {
                            auto& mutableRegistry = mutableScene.GetRegistry();
                            auto& button = mutableRegistry.emplace<UIButtonComponent>(selectedEntity);
                            if (!mutableRegistry.all_of<UIImageComponent>(selectedEntity))
                                mutableRegistry.emplace<UIImageComponent>(selectedEntity);
                            if (!mutableRegistry.all_of<RectTransformComponent>(selectedEntity))
                                mutableRegistry.emplace<RectTransformComponent>(selectedEntity);
                            if (!mutableRegistry.all_of<SpriteComponent>(selectedEntity))
                            {
                                auto& sprite = mutableRegistry.emplace<SpriteComponent>(selectedEntity);
                                sprite.Color = glm::vec4(0.82f, 0.82f, 0.82f, 1.0f);
                            }
                            if (auto* sprite = mutableRegistry.try_get<SpriteComponent>(selectedEntity))
                            {
                                button.NormalColor = sprite->Color;
                                button.HoveredColor = glm::clamp(sprite->Color * glm::vec4(1.12f, 1.12f, 1.12f, 1.0f), glm::vec4(0.0f), glm::vec4(1.0f));
                                button.PressedColor = glm::clamp(sprite->Color * glm::vec4(0.85f, 0.85f, 0.85f, 1.0f), glm::vec4(0.0f), glm::vec4(1.0f));
                                button.DisabledColor = glm::clamp(sprite->Color * glm::vec4(0.55f, 0.55f, 0.55f, 1.0f), glm::vec4(0.0f), glm::vec4(1.0f));
                            }
                            return true;
                        });
                    else {
                        auto& button = registry.emplace<UIButtonComponent>(selectedEntity);
                        if (!registry.all_of<UIImageComponent>(selectedEntity))
                            registry.emplace<UIImageComponent>(selectedEntity);
                        if (!registry.all_of<RectTransformComponent>(selectedEntity))
                            registry.emplace<RectTransformComponent>(selectedEntity);
                        if (!registry.all_of<SpriteComponent>(selectedEntity))
                        {
                            auto& sprite = registry.emplace<SpriteComponent>(selectedEntity);
                            sprite.Color = glm::vec4(0.82f, 0.82f, 0.82f, 1.0f);
                        }
                        if (auto* sprite = registry.try_get<SpriteComponent>(selectedEntity))
                        {
                            button.NormalColor = sprite->Color;
                            button.HoveredColor = glm::clamp(sprite->Color * glm::vec4(1.12f, 1.12f, 1.12f, 1.0f), glm::vec4(0.0f), glm::vec4(1.0f));
                            button.PressedColor = glm::clamp(sprite->Color * glm::vec4(0.85f, 0.85f, 0.85f, 1.0f), glm::vec4(0.0f), glm::vec4(1.0f));
                            button.DisabledColor = glm::clamp(sprite->Color * glm::vec4(0.55f, 0.55f, 0.55f, 1.0f), glm::vec4(0.0f), glm::vec4(1.0f));
                        }
                    }
                }

                if (hasUIButtonComponent)
                    ImGui::EndDisabled();

                if (hasUISliderComponent)
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

                if (hasUISliderComponent)
                    ImGui::EndDisabled();

                if (hasSpriteComponent)
                    ImGui::BeginDisabled();

                if (ImGui::MenuItem("Sprite Component"))
                {
                    if (undoService)
                        (void)undoService->ExecuteSceneMutation("Add Sprite Component", [&](Scene& mutableScene) {
                            mutableScene.GetRegistry().emplace<SpriteComponent>(selectedEntity);
                            return true;
                        });
                    else
                        registry.emplace<SpriteComponent>(selectedEntity);
                }

                if (hasSpriteComponent)
                    ImGui::EndDisabled();

                if (hasCameraComponent)
                    ImGui::BeginDisabled();

                if (ImGui::MenuItem("Camera Component"))
                {
                    if (undoService)
                    {
                        (void)undoService->ExecuteSceneMutation("Add Camera Component", [&](Scene& mutableScene) {
                            auto& mutableRegistry = mutableScene.GetRegistry();
                            auto& camera = mutableRegistry.emplace<CameraComponent>(selectedEntity);
                            camera.IsPrimary = true;
                            ClearPrimaryFlagFromOtherCameras(mutableRegistry, selectedEntity);
                            return true;
                        });
                    }
                    else
                    {
                        auto& camera = registry.emplace<CameraComponent>(selectedEntity);
                        camera.IsPrimary = true;
                        ClearPrimaryFlagFromOtherCameras(registry, selectedEntity);
                    }
                }

                if (hasCameraComponent)
                    ImGui::EndDisabled();

                if (hasAudioListener2DComponent)
                    ImGui::BeginDisabled();

                if (ImGui::MenuItem("Audio Listener 2D"))
                {
                    if (undoService)
                        (void)undoService->ExecuteSceneMutation("Add Audio Listener 2D Component", [&](Scene& mutableScene) {
                            mutableScene.GetRegistry().emplace<AudioListener2DComponent>(selectedEntity);
                            return true;
                        });
                    else
                        registry.emplace<AudioListener2DComponent>(selectedEntity);
                }

                if (hasAudioListener2DComponent)
                    ImGui::EndDisabled();

                if (hasAudioSourceComponent)
                    ImGui::BeginDisabled();

                if (ImGui::MenuItem("Audio Source"))
                {
                    if (undoService)
                        (void)undoService->ExecuteSceneMutation("Add Audio Source Component", [&](Scene& mutableScene) {
                            mutableScene.GetRegistry().emplace<AudioSourceComponent>(selectedEntity);
                            return true;
                        });
                    else
                        registry.emplace<AudioSourceComponent>(selectedEntity);
                }

                if (hasAudioSourceComponent)
                    ImGui::EndDisabled();

                if (hasNativeScriptComponent)
                    ImGui::BeginDisabled();

                if (ImGui::MenuItem("Native Script"))
                {
                    if (undoService)
                        (void)undoService->ExecuteSceneMutation("Add Native Script Component", [&](Scene& mutableScene) {
                            mutableScene.GetRegistry().emplace<NativeScriptComponent>(selectedEntity);
                            return true;
                        });
                    else
                        registry.emplace<NativeScriptComponent>(selectedEntity);
                }

                if (hasNativeScriptComponent)
                    ImGui::EndDisabled();

                if (hasAnimatorComponent)
                    ImGui::BeginDisabled();

                if (ImGui::MenuItem("Animator"))
                {
                    if (undoService)
                        (void)undoService->ExecuteSceneMutation("Add Animator Component", [&](Scene& mutableScene) {
                            mutableScene.GetRegistry().emplace<AnimatorComponent>(selectedEntity);
                            return true;
                        });
                    else
                        registry.emplace<AnimatorComponent>(selectedEntity);
                }

                if (hasAnimatorComponent)
                    ImGui::EndDisabled();

                if (hasAnimationEventReceiverComponent)
                    ImGui::BeginDisabled();

                if (ImGui::MenuItem("Animation Event Receiver"))
                {
                    if (undoService)
                        (void)undoService->ExecuteSceneMutation("Add Animation Event Receiver Component", [&](Scene& mutableScene) {
                            mutableScene.GetRegistry().emplace<AnimationEventReceiverComponent>(selectedEntity);
                            return true;
                        });
                    else
                        registry.emplace<AnimationEventReceiverComponent>(selectedEntity);
                }

                if (hasAnimationEventReceiverComponent)
                    ImGui::EndDisabled();

                if (hasRigidbody2DComponent)
                    ImGui::BeginDisabled();

                if (ImGui::MenuItem("Rigidbody 2D"))
                {
                    if (undoService)
                        (void)undoService->ExecuteSceneMutation("Add Rigidbody2D Component", [&](Scene& mutableScene) {
                            mutableScene.GetRegistry().emplace<Rigidbody2DComponent>(selectedEntity);
                            return true;
                        });
                    else
                        registry.emplace<Rigidbody2DComponent>(selectedEntity);
                }

                if (hasRigidbody2DComponent)
                    ImGui::EndDisabled();

                if (hasBoxCollider2DComponent)
                    ImGui::BeginDisabled();

                if (ImGui::MenuItem("Box Collider 2D"))
                {
                    if (undoService)
                        (void)undoService->ExecuteSceneMutation("Add BoxCollider2D Component", [&](Scene& mutableScene) {
                            mutableScene.GetRegistry().emplace<BoxCollider2DComponent>(selectedEntity);
                            return true;
                        });
                    else
                        registry.emplace<BoxCollider2DComponent>(selectedEntity);
                }

                if (hasBoxCollider2DComponent)
                    ImGui::EndDisabled();

                if (hasCircleCollider2DComponent)
                    ImGui::BeginDisabled();

                if (ImGui::MenuItem("Circle Collider 2D"))
                {
                    if (undoService)
                        (void)undoService->ExecuteSceneMutation("Add CircleCollider2D Component", [&](Scene& mutableScene) {
                            mutableScene.GetRegistry().emplace<CircleCollider2DComponent>(selectedEntity);
                            return true;
                        });
                    else
                        registry.emplace<CircleCollider2DComponent>(selectedEntity);
                }

                if (hasCircleCollider2DComponent)
                    ImGui::EndDisabled();

                if (hasJoint2DComponent)
                    ImGui::BeginDisabled();

                if (ImGui::MenuItem("Joint 2D"))
                {
                    if (undoService)
                        (void)undoService->ExecuteSceneMutation("Add Joint2D Component", [&](Scene& mutableScene) {
                            mutableScene.GetRegistry().emplace<Joint2DComponent>(selectedEntity);
                            return true;
                        });
                    else
                        registry.emplace<Joint2DComponent>(selectedEntity);
                }

                if (hasJoint2DComponent)
                    ImGui::EndDisabled();

                if (hasDirectionalLight2DComponent)
                    ImGui::BeginDisabled();

                if (ImGui::MenuItem("Directional Light 2D"))
                {
                    if (undoService)
                        (void)undoService->ExecuteSceneMutation("Add DirectionalLight2D Component", [&](Scene& mutableScene) {
                            mutableScene.GetRegistry().emplace<DirectionalLight2DComponent>(selectedEntity);
                            return true;
                        });
                    else
                        registry.emplace<DirectionalLight2DComponent>(selectedEntity);
                }

                if (hasDirectionalLight2DComponent)
                    ImGui::EndDisabled();

                if (hasPointLight2DComponent)
                    ImGui::BeginDisabled();

                if (ImGui::MenuItem("Point Light 2D"))
                {
                    if (undoService)
                        (void)undoService->ExecuteSceneMutation("Add PointLight2D Component", [&](Scene& mutableScene) {
                            mutableScene.GetRegistry().emplace<PointLight2DComponent>(selectedEntity);
                            return true;
                        });
                    else
                        registry.emplace<PointLight2DComponent>(selectedEntity);
                }

                if (hasPointLight2DComponent)
                    ImGui::EndDisabled();

                if (hasShadowOccluder2DComponent)
                    ImGui::BeginDisabled();

                if (ImGui::MenuItem("Shadow Occluder 2D"))
                {
                    if (undoService)
                        (void)undoService->ExecuteSceneMutation("Add ShadowOccluder2D Component", [&](Scene& mutableScene) {
                            mutableScene.GetRegistry().emplace<ShadowOccluder2DComponent>(selectedEntity);
                            return true;
                        });
                    else
                        registry.emplace<ShadowOccluder2DComponent>(selectedEntity);
                }

                if (hasShadowOccluder2DComponent)
                    ImGui::EndDisabled();

                if (hasGrid2DComponent)
                    ImGui::BeginDisabled();

                if (ImGui::MenuItem("Grid 2D"))
                {
                    if (undoService)
                    {
                        (void)undoService->ExecuteSceneMutation("Add Grid2D Component", [&](Scene& mutableScene) {
                            mutableScene.GetRegistry().emplace<Grid2DComponent>(selectedEntity);
                            return true;
                        });
                    }
                    else
                    {
                        registry.emplace<Grid2DComponent>(selectedEntity);
                    }
                }

                if (hasGrid2DComponent)
                    ImGui::EndDisabled();

                if (hasTilemapLayerComponent)
                    ImGui::BeginDisabled();

                if (ImGui::MenuItem("Tilemap Layer"))
                {
                    if (undoService)
                    {
                        (void)undoService->ExecuteSceneMutation("Add TilemapLayer Component", [&](Scene& mutableScene) {
                            auto& layer = mutableScene.GetRegistry().emplace<TilemapLayerComponent>(selectedEntity);
                            layer.EnsureStorage();
                            return true;
                        });
                    }
                    else
                    {
                        auto& layer = registry.emplace<TilemapLayerComponent>(selectedEntity);
                        layer.EnsureStorage();
                    }
                }

                if (hasTilemapLayerComponent)
                    ImGui::EndDisabled();

                if (hasParticleEmitterComponent)
                    ImGui::BeginDisabled();

                if (ImGui::MenuItem("Particle Emitter"))
                {
                    if (undoService)
                        (void)undoService->ExecuteSceneMutation("Add Particle Emitter Component", [&](Scene& mutableScene) {
                            mutableScene.GetRegistry().emplace<ParticleEmitterComponent>(selectedEntity);
                            return true;
                        });
                    else
                        registry.emplace<ParticleEmitterComponent>(selectedEntity);
                }

                if (hasParticleEmitterComponent)
                    ImGui::EndDisabled();

                ImGui::EndPopup();
            }

            if (pendingRemovals.RemoveCanvasComponent)
            {
                if (undoService)
                {
                    (void)undoService->ExecuteSceneMutation("Remove Canvas Component", [&](Scene& mutableScene) {
                        mutableScene.GetRegistry().remove<CanvasComponent>(selectedEntity);
                        return true;
                    });
                }
                else
                {
                    registry.remove<CanvasComponent>(selectedEntity);
                }
            }

            if (pendingRemovals.RemoveRectTransformComponent)
            {
                if (undoService)
                {
                    (void)undoService->ExecuteSceneMutation("Remove RectTransform Component", [&](Scene& mutableScene) {
                        mutableScene.GetRegistry().remove<RectTransformComponent>(selectedEntity);
                        return true;
                    });
                }
                else
                {
                    registry.remove<RectTransformComponent>(selectedEntity);
                }
            }

            if (pendingRemovals.RemoveUIImageComponent)
            {
                if (undoService)
                {
                    (void)undoService->ExecuteSceneMutation("Remove UIImage Component", [&](Scene& mutableScene) {
                        mutableScene.GetRegistry().remove<UIImageComponent>(selectedEntity);
                        return true;
                    });
                }
                else
                {
                    registry.remove<UIImageComponent>(selectedEntity);
                }
            }

            if (pendingRemovals.RemoveUIPanelComponent)
            {
                if (undoService)
                {
                    (void)undoService->ExecuteSceneMutation("Remove UIPanel Component", [&](Scene& mutableScene) {
                        mutableScene.GetRegistry().remove<UIPanelComponent>(selectedEntity);
                        return true;
                    });
                }
                else
                {
                    registry.remove<UIPanelComponent>(selectedEntity);
                }
            }

            if (pendingRemovals.RemoveUITextComponent)
            {
                if (undoService)
                {
                    (void)undoService->ExecuteSceneMutation("Remove UIText Component", [&](Scene& mutableScene) {
                        mutableScene.GetRegistry().remove<UITextComponent>(selectedEntity);
                        return true;
                    });
                }
                else
                {
                    registry.remove<UITextComponent>(selectedEntity);
                }
            }

            if (pendingRemovals.RemoveUIButtonComponent)
            {
                if (undoService)
                {
                    (void)undoService->ExecuteSceneMutation("Remove UIButton Component", [&](Scene& mutableScene) {
                        mutableScene.GetRegistry().remove<UIButtonComponent>(selectedEntity);
                        return true;
                    });
                }
                else
                {
                    registry.remove<UIButtonComponent>(selectedEntity);
                }
            }

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

            if (pendingRemovals.RemoveSpriteComponent)
            {
                if (undoService)
                {
                    (void)undoService->ExecuteSceneMutation("Remove Sprite Component", [&](Scene& mutableScene) {
                        auto& mutableRegistry = mutableScene.GetRegistry();
                        mutableRegistry.remove<SpriteComponent>(selectedEntity);
                        if (mutableRegistry.all_of<MaterialComponent>(selectedEntity))
                            mutableRegistry.remove<MaterialComponent>(selectedEntity);
                        return true;
                    });
                }
                else
                {
                    registry.remove<SpriteComponent>(selectedEntity);
                    if (registry.all_of<MaterialComponent>(selectedEntity))
                        pendingRemovals.RemoveMaterialComponent = true;
                }
            }

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

            if (pendingRemovals.RemoveCameraComponent)
            {
                if (undoService)
                {
                    (void)undoService->ExecuteSceneMutation("Remove Camera Component", [&](Scene& mutableScene) {
                        mutableScene.GetRegistry().remove<CameraComponent>(selectedEntity);
                        return true;
                    });
                }
                else
                {
                    registry.remove<CameraComponent>(selectedEntity);
                }
            }

            if (pendingRemovals.RemoveAudioListener2DComponent)
            {
                if (undoService)
                {
                    (void)undoService->ExecuteSceneMutation("Remove Audio Listener 2D Component", [&](Scene& mutableScene) {
                        mutableScene.GetRegistry().remove<AudioListener2DComponent>(selectedEntity);
                        return true;
                    });
                }
                else
                {
                    registry.remove<AudioListener2DComponent>(selectedEntity);
                }
            }

            if (pendingRemovals.RemoveAudioSourceComponent)
            {
                if (auto* audioSource = registry.try_get<AudioSourceComponent>(selectedEntity))
                {
                    if (audioSource->RuntimeVoiceId != 0)
                        Audio::AudioEngine::GetInstance().Stop(audioSource->RuntimeVoiceId);
                }
                if (undoService)
                {
                    (void)undoService->ExecuteSceneMutation("Remove Audio Source Component", [&](Scene& mutableScene) {
                        auto& mutableRegistry = mutableScene.GetRegistry();
                        if (auto* mutableAudioSource = mutableRegistry.try_get<AudioSourceComponent>(selectedEntity))
                        {
                            if (mutableAudioSource->RuntimeVoiceId != 0)
                                Audio::AudioEngine::GetInstance().Stop(mutableAudioSource->RuntimeVoiceId);
                        }
                        mutableRegistry.remove<AudioSourceComponent>(selectedEntity);
                        return true;
                    });
                }
                else
                {
                    registry.remove<AudioSourceComponent>(selectedEntity);
                }
            }

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
                if (undoService)
                {
                    (void)undoService->ExecuteSceneMutation("Remove Native Script Component", [&](Scene& mutableScene) {
                        auto& mutableRegistry = mutableScene.GetRegistry();
                        if (auto* mutableNativeScript = mutableRegistry.try_get<NativeScriptComponent>(selectedEntity))
                        {
                            for (auto& scriptEntry : mutableNativeScript->Scripts)
                            {
                                scriptEntry.RuntimeInitialized = false;
                                scriptEntry.RuntimeInstance.reset();
                            }
                        }
                        mutableRegistry.remove<NativeScriptComponent>(selectedEntity);
                        return true;
                    });
                }
                else
                {
                    registry.remove<NativeScriptComponent>(selectedEntity);
                }
            }

            if (pendingRemovals.RemoveAnimatorComponent)
            {
                if (undoService)
                {
                    (void)undoService->ExecuteSceneMutation("Remove Animator Component", [&](Scene& mutableScene) {
                        mutableScene.GetRegistry().remove<AnimatorComponent>(selectedEntity);
                        return true;
                    });
                }
                else
                {
                    registry.remove<AnimatorComponent>(selectedEntity);
                }
            }

            if (pendingRemovals.RemoveAnimationEventReceiverComponent)
            {
                if (undoService)
                {
                    (void)undoService->ExecuteSceneMutation("Remove Animation Event Receiver Component", [&](Scene& mutableScene) {
                        mutableScene.GetRegistry().remove<AnimationEventReceiverComponent>(selectedEntity);
                        return true;
                    });
                }
                else
                {
                    registry.remove<AnimationEventReceiverComponent>(selectedEntity);
                }
            }

            if (pendingRemovals.RemoveRigidbody2DComponent)
            {
                if (undoService)
                {
                    (void)undoService->ExecuteSceneMutation("Remove Rigidbody2D Component", [&](Scene& mutableScene) {
                        mutableScene.GetRegistry().remove<Rigidbody2DComponent>(selectedEntity);
                        return true;
                    });
                }
                else
                {
                    registry.remove<Rigidbody2DComponent>(selectedEntity);
                }
            }

            if (pendingRemovals.RemoveBoxCollider2DComponent)
            {
                if (undoService)
                {
                    (void)undoService->ExecuteSceneMutation("Remove BoxCollider2D Component", [&](Scene& mutableScene) {
                        mutableScene.GetRegistry().remove<BoxCollider2DComponent>(selectedEntity);
                        return true;
                    });
                }
                else
                {
                    registry.remove<BoxCollider2DComponent>(selectedEntity);
                }
            }

            if (pendingRemovals.RemoveCircleCollider2DComponent)
            {
                if (undoService)
                {
                    (void)undoService->ExecuteSceneMutation("Remove CircleCollider2D Component", [&](Scene& mutableScene) {
                        mutableScene.GetRegistry().remove<CircleCollider2DComponent>(selectedEntity);
                        return true;
                    });
                }
                else
                {
                    registry.remove<CircleCollider2DComponent>(selectedEntity);
                }
            }

            if (pendingRemovals.RemoveJoint2DComponent)
            {
                if (undoService)
                {
                    (void)undoService->ExecuteSceneMutation("Remove Joint2D Component", [&](Scene& mutableScene) {
                        mutableScene.GetRegistry().remove<Joint2DComponent>(selectedEntity);
                        return true;
                    });
                }
                else
                {
                    registry.remove<Joint2DComponent>(selectedEntity);
                }
            }

            if (pendingRemovals.RemoveDirectionalLight2DComponent)
            {
                if (undoService)
                {
                    (void)undoService->ExecuteSceneMutation("Remove DirectionalLight2D Component", [&](Scene& mutableScene) {
                        mutableScene.GetRegistry().remove<DirectionalLight2DComponent>(selectedEntity);
                        return true;
                    });
                }
                else
                {
                    registry.remove<DirectionalLight2DComponent>(selectedEntity);
                }
            }

            if (pendingRemovals.RemovePointLight2DComponent)
            {
                if (undoService)
                {
                    (void)undoService->ExecuteSceneMutation("Remove PointLight2D Component", [&](Scene& mutableScene) {
                        mutableScene.GetRegistry().remove<PointLight2DComponent>(selectedEntity);
                        return true;
                    });
                }
                else
                {
                    registry.remove<PointLight2DComponent>(selectedEntity);
                }
            }

            if (pendingRemovals.RemoveShadowOccluder2DComponent)
            {
                if (undoService)
                {
                    (void)undoService->ExecuteSceneMutation("Remove ShadowOccluder2D Component", [&](Scene& mutableScene) {
                        mutableScene.GetRegistry().remove<ShadowOccluder2DComponent>(selectedEntity);
                        return true;
                    });
                }
                else
                {
                    registry.remove<ShadowOccluder2DComponent>(selectedEntity);
                }
            }

            if (pendingRemovals.RemoveParticleEmitterComponent)
            {
                if (undoService)
                {
                    (void)undoService->ExecuteSceneMutation("Remove Particle Emitter Component", [&](Scene& mutableScene) {
                        mutableScene.GetRegistry().remove<ParticleEmitterComponent>(selectedEntity);
                        return true;
                    });
                }
                else
                {
                    registry.remove<ParticleEmitterComponent>(selectedEntity);
                }
            }

            if (pendingRemovals.RemoveGrid2DComponent)
            {
                if (undoService)
                {
                    (void)undoService->ExecuteSceneMutation("Remove Grid2D Component", [&](Scene& mutableScene) {
                        mutableScene.GetRegistry().remove<Grid2DComponent>(selectedEntity);
                        return true;
                    });
                }
                else
                {
                    registry.remove<Grid2DComponent>(selectedEntity);
                }
            }

            if (pendingRemovals.RemoveTilemapLayerComponent)
            {
                if (undoService)
                {
                    (void)undoService->ExecuteSceneMutation("Remove TilemapLayer Component", [&](Scene& mutableScene) {
                        mutableScene.GetRegistry().remove<TilemapLayerComponent>(selectedEntity);
                        return true;
                    });
                }
                else
                {
                    registry.remove<TilemapLayerComponent>(selectedEntity);
                }
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
