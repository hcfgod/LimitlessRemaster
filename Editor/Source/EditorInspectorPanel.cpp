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

        constexpr size_t kNativeScriptEditorBufferSize = 256 * 1024;

        struct NativeScriptAuthoringState
        {
            bool EditorWindowOpen = false;
            bool FocusEditorWindowRequested = false;
            bool ShowDebugInfo = false;
            bool SelectHeaderTabRequested = false;
            bool SelectSourceTabRequested = false;
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
            const std::regex fieldPattern(
                R"(^\s*(?:const\s+)?(?:static\s+)?(float|int32_t|int|bool|glm::vec3|std::string)\s+([A-Za-z_][A-Za-z0-9_]*)\s*(?:=\s*([^;]+))?\s*;\s*$)");

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
                R"LT(GetExposed(Float|Integer|Boolean|Vector3|String)\s*\(\s*"([A-Za-z_][A-Za-z0-9_]*)"\s*,\s*([^)]+)\))LT");

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
                    found->second = field.DefaultValue;
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
                "    float RotationSpeed = 90.0f;\n\n"
                "protected:\n"
                "    LT_BEGIN_AUTO_EXPOSED_FIELD_SYNC()\n"
                "        LT_AUTO_EXPOSED_FIELD(RotationSpeed)\n"
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
                "    auto& transform = GetComponent<Limitless::TransformComponent>();\n"
                "    transform.Rotation.z += RotationSpeed * deltaTime;\n"
                "    if (transform.Rotation.z > 360.0f)\n"
                "        transform.Rotation.z -= 360.0f;\n"
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
            if (!ImGui::Begin("Native Script Editor", &state.EditorWindowOpen))
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
            ImGui::Checkbox("Auto Build On Save", &state.AutoBuildAfterSave);
            if (state.BuildInProgress.load(std::memory_order_relaxed))
                ImGui::TextDisabled("Build in progress...");

            if (ImGui::Button("Save Files", ImVec2(140.0f, 0.0f)))
            {
                std::string saveError;
                const bool headerSaved = SaveBufferToTextFile(state.HeaderPath, state.HeaderBuffer, saveError);
                const bool sourceSaved = SaveBufferToTextFile(state.SourcePath, state.SourceBuffer, saveError);
                if (headerSaved && sourceSaved)
                {
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
                    if (state.AutoBuildAfterSave && canBuild)
                        (void)TriggerNativeScriptsBuild(state);
                }
                else
                {
                    state.StatusMessage = saveError;
                    state.StatusIsError = true;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Reload From Disk", ImVec2(160.0f, 0.0f)))
            {
                std::string reloadError;
                const bool headerLoaded = LoadTextFileIntoBuffer(state.HeaderPath, state.HeaderBuffer, reloadError);
                const bool sourceLoaded = LoadTextFileIntoBuffer(state.SourcePath, state.SourceBuffer, reloadError);
                if (headerLoaded && sourceLoaded)
                {
                    state.StatusMessage = "Reloaded script files from disk.";
                    state.StatusIsError = false;
                }
                else
                {
                    state.StatusMessage = reloadError;
                    state.StatusIsError = true;
                }
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(state.BuildInProgress.load(std::memory_order_relaxed));
            if (ImGui::Button("Build Scripts Now", ImVec2(150.0f, 0.0f)))
                (void)TriggerNativeScriptsBuild(state);
            ImGui::EndDisabled();

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

                if (ImGui::BeginTabItem("Header (.h)", nullptr, headerTabFlags))
                {
                    ImGui::InputTextMultiline("##NativeScriptHeaderEditor", state.HeaderBuffer.data(), state.HeaderBuffer.size(), ImVec2(-1.0f, 360.0f));
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Source (.cpp)", nullptr, sourceTabFlags))
                {
                    ImGui::InputTextMultiline("##NativeScriptSourceEditor", state.SourceBuffer.data(), state.SourceBuffer.size(), ImVec2(-1.0f, 360.0f));
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

        if (!selectedMaterialAssetKey.empty())
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
                registry,
                selectedEntity,
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
                                    ImGui::TextDisabled("Selected script is discovered in Assets but not compiled yet. Build ScriptCore.");

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
                                    ImGui::TextDisabled("Supported public field types: float, int/int32_t, bool, glm::vec3, std::string.");
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
                const bool hasAudioSourceComponent = registry.all_of<AudioSourceComponent>(selectedEntity);
                const bool hasTextComponent = registry.all_of<TextComponent>(selectedEntity);
                const bool hasNativeScriptComponent = registry.all_of<NativeScriptComponent>(selectedEntity);
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

                if (hasTextComponent)
                    ImGui::BeginDisabled();

                if (ImGui::MenuItem("Text Component"))
                {
                    if (undoService)
                        (void)undoService->ExecuteSceneMutation("Add Text Component", [&](Scene& mutableScene) {
                            mutableScene.GetRegistry().emplace<TextComponent>(selectedEntity);
                            return true;
                        });
                    else
                        registry.emplace<TextComponent>(selectedEntity);
                }

                if (hasTextComponent)
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

                ImGui::EndPopup();
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

            if (pendingRemovals.RemoveTextComponent)
            {
                if (undoService)
                {
                    (void)undoService->ExecuteSceneMutation("Remove Text Component", [&](Scene& mutableScene) {
                        mutableScene.GetRegistry().remove<TextComponent>(selectedEntity);
                        return true;
                    });
                }
                else
                {
                    registry.remove<TextComponent>(selectedEntity);
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
