#include "ScriptCoreModuleRuntime.h"
#include "IncrementalScriptCompiler.h"
#include "Scripting/ManagedScriptHost.h"

#include "Core/Debug/Log.h"
#include "Core/Input/InputSystem.h"
#include "Platform/Platform.h"
#include "Physics/Physics2DQueries.h"
#include "Project/BuildSettings.h"
#include "Project/ProjectManager.h"
#include "Scene/Scene.h"
#include "Scene/SceneManager.h"
#include "Scripting/Physics2D.h"
#include "Scripting/NativeScriptRegistry.h"
#include "Scripting/ScriptCoreApi.h"
#include "Scripting/Debug.h"
#include "Scripting/Input.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace Limitless::ScriptCoreModuleRuntime
{
    namespace
    {
        using RegisterScriptCoreTypesFunction = void (*)(NativeScriptRegistrationCallback registrationCallback);
        using GetScriptCoreAbiVersionFunction = uint32_t (*)();
        using SetSceneTransitionBridgeFunction = void (*)(SceneTransitionBridgeCallback callback);
        using SetInputActionAxis1DBridgeFunction = void (*)(InputActionAxis1DBridgeCallback callback);
        using SetInputActionAxis2DBridgeFunction = void (*)(InputActionAxis2DBridgeCallback callback);
        using SetInputActionExistsBridgeFunction = void (*)(InputActionExistsBridgeCallback callback);
        using SetInputActionPressedBridgeFunction = void (*)(InputActionPressedBridgeCallback callback);
        using SetInputActionTriggerBridgeFunction = void (*)(InputActionTriggerBridgeCallback callback);
        using SetPhysics2DRaycastBridgeFunction = void (*)(Physics2DRaycastBridgeCallback callback);
        using SetScriptLogBridgeFunction = void (*)(ScriptLogBridgeCallback callback);
        using SetScriptCreateEntityBridgeFunction = void (*)(ScriptCreateEntityBridgeCallback callback);
        using SetScriptDestroyEntityBridgeFunction = void (*)(ScriptDestroyEntityBridgeCallback callback);
        using SetScriptInstantiatePrefabBridgeFunction = void (*)(ScriptInstantiatePrefabBridgeCallback callback);
        using SetScriptResolveEntityReferenceBridgeFunction = void (*)(ScriptResolveEntityReferenceBridgeCallback callback);
        using SetScriptContactEntityHandlesBridgeFunction = void (*)(ScriptGetContactEntityHandlesBridgeCallback callback);

        struct RuntimeState final
        {
            bool Initialized = false;
            void* LibraryHandle = nullptr;
            std::filesystem::path SourceLibraryPath;
            std::filesystem::path LoadedLibraryPath;
            std::filesystem::file_time_type LastWriteTime{};
            std::filesystem::path LastRejectedSourcePath;
            std::filesystem::file_time_type LastRejectedSourceWriteTime{};
            uint64_t ReloadCounter = 0;
            std::chrono::steady_clock::time_point LastPollTime{};
        };

        struct ManagedRuntimeState final
        {
            std::filesystem::path SourceManagedDirectory;
            std::filesystem::path SourceManifestPath;
            std::filesystem::file_time_type LastWriteTime{};
            std::filesystem::path LastRejectedSourcePath;
            std::filesystem::file_time_type LastRejectedSourceWriteTime{};
            std::unordered_set<std::string> LoggedRejectedValidationFailures;
        };

        struct GameplayInputRoutingState final
        {
            bool GameViewFocused = false;
            bool GameViewHovered = false;
            bool UiWantsMouseCapture = false;
            bool UiWantsKeyboardCapture = false;
        };

        RuntimeState s_RuntimeState;
        ManagedRuntimeState s_ManagedRuntimeState;
        GameplayInputRoutingState s_GameplayInputRoutingState;

        void ResetManagedRuntimeState()
        {
            s_ManagedRuntimeState = {};
        }

        void LogRejectedManagedPayloadCandidateOnce(const std::filesystem::path& candidatePath, const std::string& validationError)
        {
#if defined(LT_CONFIG_DEBUG)
            if (validationError.empty())
                return;

            const std::string failureKey = candidatePath.lexically_normal().string() + "\n" + validationError;
            if (!s_ManagedRuntimeState.LoggedRejectedValidationFailures.emplace(failureKey).second)
                return;

            LT_INFO("Managed scripting: rejected candidate '{}' ({})", candidatePath.string(), validationError);
#else
            (void)candidatePath;
            (void)validationError;
#endif
        }

        bool ShouldSuppressGameplayInput()
        {
            const bool gameViewInputTargeted =
                s_GameplayInputRoutingState.GameViewFocused ||
                s_GameplayInputRoutingState.GameViewHovered;
            if (!gameViewInputTargeted)
                return true;

            if (s_GameplayInputRoutingState.UiWantsMouseCapture &&
                !s_GameplayInputRoutingState.GameViewHovered)
            {
                return true;
            }

            if (s_GameplayInputRoutingState.UiWantsKeyboardCapture &&
                !s_GameplayInputRoutingState.GameViewFocused)
            {
                return true;
            }

            Scene* activeScene = Physics2DQueries::GetActiveSceneForScriptQueries();
            if (activeScene && activeScene->IsUiPointerOverInteractiveElement())
                return true;

            return false;
        }

        std::string GetScriptCoreLibraryFileName()
        {
#if defined(LT_PLATFORM_WINDOWS)
            return "ScriptCore.dll";
#elif defined(LT_PLATFORM_MACOS)
            return "libScriptCore.dylib";
#else
            return "libScriptCore.so";
#endif
        }

        std::string GetRuntimeLoadedLibraryFileName(uint64_t reloadCounter)
        {
#if defined(LT_PLATFORM_WINDOWS)
            return "ScriptCore.RuntimeLoaded." + std::to_string(reloadCounter) + ".dll";
#elif defined(LT_PLATFORM_MACOS)
            return "libScriptCore.RuntimeLoaded." + std::to_string(reloadCounter) + ".dylib";
#else
            return "libScriptCore.RuntimeLoaded." + std::to_string(reloadCounter) + ".so";
#endif
        }

        std::optional<std::filesystem::path> FindEngineWorkspaceRoot()
        {
            if (const char* toolchainRoot = std::getenv("LIMITLESS_TOOLCHAIN_ROOT"); toolchainRoot && toolchainRoot[0] != '\0')
            {
                std::error_code errorCode;
                std::filesystem::path candidate(toolchainRoot);
                if (std::filesystem::exists(candidate / "Scripts", errorCode))
                    return candidate;
            }

            if (const char* engineRoot = std::getenv("LIMITLESS_ENGINE_ROOT"); engineRoot && engineRoot[0] != '\0')
            {
                std::error_code errorCode;
                std::filesystem::path candidate(engineRoot);
                if (std::filesystem::exists(candidate / "Scripts", errorCode))
                    return candidate;
            }

            const std::string executablePath = PlatformDetection::GetExecutablePath();
            if (!executablePath.empty())
            {
                std::error_code errorCode;
                const std::filesystem::path executableDirectory = std::filesystem::path(executablePath).parent_path();
                const std::filesystem::path embeddedToolchain = executableDirectory / "Toolchain";
                if (std::filesystem::exists(embeddedToolchain / "Scripts", errorCode))
                    return embeddedToolchain;
            }

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

        std::optional<std::filesystem::path> GetOpenProjectRoot()
        {
            const auto& projectManager = Project::ProjectManager::GetInstance();
            if (!projectManager.HasOpenProject())
                return std::nullopt;
            return projectManager.GetProjectRoot();
        }

        std::optional<std::filesystem::path> GetConfiguredBuildRoot()
        {
            const auto projectRoot = GetOpenProjectRoot();
            if (!projectRoot.has_value())
                return std::nullopt;

            const auto buildSettingsResult = Project::LoadBuildSettings(projectRoot.value());
            if (!buildSettingsResult.IsSuccess())
                return std::nullopt;

            const auto& settings = buildSettingsResult.GetValue();
            if (settings.EngineRootOverride.empty())
                return std::nullopt;

            std::error_code errorCode;
            std::filesystem::path configuredRoot = std::filesystem::weakly_canonical(settings.EngineRootOverride, errorCode);
            if (errorCode)
                configuredRoot = std::filesystem::path(settings.EngineRootOverride);
            if (std::filesystem::exists(configuredRoot / "Scripts", errorCode))
                return configuredRoot;
            return std::nullopt;
        }

        std::filesystem::path BuildConfigOutputFolderFor(const std::string& configName)
        {
            std::string architectureName = "x64";
            std::string platformToken = "x64";
#if defined(LT_ARCHITECTURE_ARM64)
            architectureName = "arm64";
            platformToken = "ARM64";
#endif

            std::string systemToken = "linux";
#if defined(LT_PLATFORM_WINDOWS)
            systemToken = "windows";
#elif defined(LT_PLATFORM_MACOS)
            systemToken = "macosx";
#endif

            return std::filesystem::path("Build")
                / (configName + "_" + architectureName + "-" + systemToken + "-" + platformToken)
                / "Editor";
        }

        std::filesystem::path BuildConfigOutputFolder()
        {
            std::string configName = "debug";
#if defined(LT_CONFIG_RELEASE)
            configName = "release";
#elif defined(LT_CONFIG_DIST)
            configName = "dist";
#endif
            return BuildConfigOutputFolderFor(configName);
        }

        std::filesystem::path BuildDistConfigOutputFolder()
        {
            return BuildConfigOutputFolderFor("dist");
        }

        std::vector<std::filesystem::path> BuildScriptCoreLibraryCandidates()
        {
            std::vector<std::filesystem::path> candidates;
            auto addCandidate = [&](const std::filesystem::path& candidatePath) {
                if (candidatePath.empty())
                    return;
                if (std::find(candidates.begin(), candidates.end(), candidatePath) != candidates.end())
                    return;
                candidates.push_back(candidatePath);
            };

            const std::string scriptCoreLibraryFileName = GetScriptCoreLibraryFileName();
            const std::filesystem::path currentConfigOutput = BuildConfigOutputFolder();
            const std::filesystem::path distConfigOutput = BuildDistConfigOutputFolder();
            const std::filesystem::path currentConfigPlatformFolderName = currentConfigOutput.parent_path().filename();
            const std::filesystem::path distConfigPlatformFolderName = distConfigOutput.parent_path().filename();
            if (const auto projectRoot = GetOpenProjectRoot(); projectRoot.has_value())
            {
                // Project-local ScriptCore staging path uses the config-platform folder name.
                addCandidate(projectRoot.value() / "Build" / "ScriptCore" / currentConfigPlatformFolderName / scriptCoreLibraryFileName);
                addCandidate(projectRoot.value() / "Build" / "ScriptCore" / distConfigPlatformFolderName / scriptCoreLibraryFileName);
            }

            const std::string executablePath = PlatformDetection::GetExecutablePath();
            if (!executablePath.empty())
            {
                const std::filesystem::path executableDirectory = std::filesystem::path(executablePath).parent_path();
                if (!executableDirectory.empty())
                    addCandidate(executableDirectory / scriptCoreLibraryFileName);
            }

            if (const auto configuredBuildRoot = GetConfiguredBuildRoot(); configuredBuildRoot.has_value())
            {
                addCandidate(configuredBuildRoot.value() / currentConfigOutput / scriptCoreLibraryFileName);
                addCandidate(configuredBuildRoot.value() / distConfigOutput / scriptCoreLibraryFileName);
            }

            if (const auto engineRoot = FindEngineWorkspaceRoot(); engineRoot.has_value())
            {
                addCandidate(engineRoot.value() / currentConfigOutput / scriptCoreLibraryFileName);
                addCandidate(engineRoot.value() / distConfigOutput / scriptCoreLibraryFileName);
            }

            return candidates;
        }

        void LogScriptCoreLibraryCandidates(const std::vector<std::filesystem::path>& candidates)
        {
#if defined(LT_CONFIG_DEBUG)
            if (candidates.empty())
            {
                LT_INFO("ScriptCore runtime: no candidate library paths were generated.");
                return;
            }

            LT_INFO("ScriptCore runtime: probing {} candidate path(s).", candidates.size());
            for (size_t index = 0; index < candidates.size(); ++index)
            {
                LT_INFO("ScriptCore runtime candidate [{}]: '{}'", index, candidates[index].string());
            }
#else
            (void)candidates;
#endif
        }

        std::vector<std::filesystem::path> BuildManagedDirectoryCandidates()
        {
            std::vector<std::filesystem::path> candidates;
            auto addCandidate = [&](const std::filesystem::path& candidatePath) {
                if (candidatePath.empty())
                    return;
                if (std::find(candidates.begin(), candidates.end(), candidatePath) != candidates.end())
                    return;
                candidates.push_back(candidatePath);
            };

            const std::filesystem::path currentConfigOutput = BuildConfigOutputFolder();
            const std::filesystem::path distConfigOutput = BuildDistConfigOutputFolder();
            const std::filesystem::path currentConfigPlatformFolderName = currentConfigOutput.parent_path().filename();
            const std::filesystem::path distConfigPlatformFolderName = distConfigOutput.parent_path().filename();
            if (const auto projectRoot = GetOpenProjectRoot(); projectRoot.has_value())
            {
                addCandidate(projectRoot.value() / "Build" / "ScriptCore" / currentConfigPlatformFolderName / "Managed");
                addCandidate(projectRoot.value() / "Build" / "ScriptCore" / distConfigPlatformFolderName / "Managed");
            }

            if (const std::string executablePath = PlatformDetection::GetExecutablePath(); !executablePath.empty())
            {
                const std::filesystem::path executableDirectory = std::filesystem::path(executablePath).parent_path();
                if (!executableDirectory.empty())
                    addCandidate(executableDirectory / "Managed");
            }

            if (const auto configuredBuildRoot = GetConfiguredBuildRoot(); configuredBuildRoot.has_value())
            {
                addCandidate(configuredBuildRoot.value() / currentConfigOutput / "Managed");
                addCandidate(configuredBuildRoot.value() / distConfigOutput / "Managed");
            }

            if (const auto engineRoot = FindEngineWorkspaceRoot(); engineRoot.has_value())
            {
                addCandidate(engineRoot.value() / currentConfigOutput / "Managed");
                addCandidate(engineRoot.value() / distConfigOutput / "Managed");
            }

            return candidates;
        }

        void LogManagedDirectoryCandidates(const std::vector<std::filesystem::path>& candidates)
        {
#if defined(LT_CONFIG_DEBUG)
            if (candidates.empty())
            {
                LT_INFO("Managed scripting: no candidate payload directories were generated for the editor.");
                return;
            }

            LT_INFO("Managed scripting: probing {} editor managed payload candidate(s).", candidates.size());
            for (size_t index = 0; index < candidates.size(); ++index)
                LT_INFO("Managed scripting candidate [{}]: '{}'", index, candidates[index].string());
#else
            (void)candidates;
#endif
        }

        bool SelectManagedPayloadCandidate(const std::vector<std::filesystem::path>& candidatePaths,
                                           std::filesystem::path& outSourceDirectory,
                                           std::filesystem::path& outManifestPath,
                                           std::filesystem::file_time_type& outWriteTime,
                                           bool& outHasWriteTime)
        {
            outSourceDirectory.clear();
            outManifestPath.clear();
            outWriteTime = {};
            outHasWriteTime = false;

            for (const auto& candidatePath : candidatePaths)
            {
                std::string validationError;
                if (!ManagedScriptPayload::ValidatePayloadDirectory(candidatePath, nullptr, &validationError))
                {
                    LogRejectedManagedPayloadCandidateOnce(candidatePath, validationError);
                    continue;
                }

                outSourceDirectory = candidatePath;
                outManifestPath = ManagedScriptPayload::GetPayloadManifestPath(candidatePath);

                std::error_code timeError;
                outWriteTime = std::filesystem::last_write_time(outManifestPath, timeError);
                outHasWriteTime = !timeError;
                return true;
            }

            return false;
        }

        bool ReloadManagedScriptHost(const std::filesystem::path& sourceDirectory)
        {
            ManagedScriptHost::Shutdown();
            ResetManagedRuntimeState();

            if (!ManagedScriptHost::Initialize(sourceDirectory))
                return false;

            const auto& snapshot = ManagedScriptHost::GetSnapshot();
            s_ManagedRuntimeState.SourceManagedDirectory = snapshot.ManagedDirectory;
            s_ManagedRuntimeState.SourceManifestPath = ManagedScriptPayload::GetPayloadManifestPath(snapshot.ManagedDirectory);

            std::error_code timeError;
            s_ManagedRuntimeState.LastWriteTime = std::filesystem::last_write_time(s_ManagedRuntimeState.SourceManifestPath, timeError);
            if (timeError)
                s_ManagedRuntimeState.LastWriteTime = {};

            LT_INFO("Managed scripting: loaded editor payload from '{}' (shadow '{}', apiVersion={}).",
                    snapshot.ManagedDirectory.string(),
                    snapshot.LoadedManagedDirectory.string(),
                    snapshot.PayloadApiVersion);
            return true;
        }

        void InitializeManagedScriptHost()
        {
            const auto candidatePaths = BuildManagedDirectoryCandidates();
            LogManagedDirectoryCandidates(candidatePaths);

            std::filesystem::path sourceDirectory;
            std::filesystem::path manifestPath;
            std::filesystem::file_time_type writeTime{};
            bool hasWriteTime = false;
            if (SelectManagedPayloadCandidate(candidatePaths, sourceDirectory, manifestPath, writeTime, hasWriteTime))
            {
                if (ReloadManagedScriptHost(sourceDirectory))
                {
                    s_ManagedRuntimeState.SourceManagedDirectory = sourceDirectory;
                    s_ManagedRuntimeState.SourceManifestPath = manifestPath;
                    if (hasWriteTime)
                        s_ManagedRuntimeState.LastWriteTime = writeTime;
                    return;
                }
            }

            LT_INFO("Managed scripting: no managed discovery payload found for the editor yet.");
        }

        void UpdateManagedScriptHost()
        {
            std::filesystem::path sourceDirectory;
            std::filesystem::path manifestPath;
            std::filesystem::file_time_type writeTime{};
            bool hasWriteTime = false;
            if (!SelectManagedPayloadCandidate(BuildManagedDirectoryCandidates(), sourceDirectory, manifestPath, writeTime, hasWriteTime))
                return;

            auto markSourceRejected = [&](const std::filesystem::path& rejectedPath) {
                if (!hasWriteTime)
                    return;
                s_ManagedRuntimeState.LastRejectedSourcePath = rejectedPath;
                s_ManagedRuntimeState.LastRejectedSourceWriteTime = writeTime;
            };

            auto clearRejectedSource = [&]() {
                s_ManagedRuntimeState.LastRejectedSourcePath.clear();
                s_ManagedRuntimeState.LastRejectedSourceWriteTime = {};
            };

            const bool sourceKnownRejected =
                hasWriteTime &&
                s_ManagedRuntimeState.LastRejectedSourcePath == sourceDirectory &&
                s_ManagedRuntimeState.LastRejectedSourceWriteTime == writeTime;

            if (s_ManagedRuntimeState.SourceManagedDirectory != sourceDirectory)
            {
                if (sourceKnownRejected)
                    return;

                if (ReloadManagedScriptHost(sourceDirectory))
                {
                    clearRejectedSource();
                    s_ManagedRuntimeState.SourceManagedDirectory = sourceDirectory;
                    s_ManagedRuntimeState.SourceManifestPath = manifestPath;
                    if (hasWriteTime)
                        s_ManagedRuntimeState.LastWriteTime = writeTime;
                }
                else
                {
                    markSourceRejected(sourceDirectory);
                }
                return;
            }

            if (!ManagedScriptHost::IsInitialized())
            {
                if (ReloadManagedScriptHost(sourceDirectory))
                {
                    clearRejectedSource();
                    s_ManagedRuntimeState.SourceManagedDirectory = sourceDirectory;
                    s_ManagedRuntimeState.SourceManifestPath = manifestPath;
                    if (hasWriteTime)
                        s_ManagedRuntimeState.LastWriteTime = writeTime;
                }
                return;
            }

            if (!hasWriteTime)
                return;

            if (writeTime != s_ManagedRuntimeState.LastWriteTime)
            {
                if (sourceKnownRejected)
                    return;

                if (ReloadManagedScriptHost(sourceDirectory))
                {
                    clearRejectedSource();
                    s_ManagedRuntimeState.SourceManagedDirectory = sourceDirectory;
                    s_ManagedRuntimeState.SourceManifestPath = manifestPath;
                    s_ManagedRuntimeState.LastWriteTime = writeTime;
                }
                else
                {
                    markSourceRejected(sourceDirectory);
                }
            }
        }

        void ResetRuntimeScriptRegistry()
        {
            NativeScriptRegistry::Clear();
        }

        void RegisterScriptFromModule(const char* className, NativeScriptCreateFunction createFunction)
        {
            if (!className || className[0] == '\0' || !createFunction)
                return;
            NativeScriptRegistry::RegisterScript(className, createFunction);
        }

        bool ForwardSceneTransitionToHost(SceneTransitionType transitionType, const char* sceneIdentifier, LoadSceneMode loadSceneMode)
        {
            switch (transitionType)
            {
                case SceneTransitionType::LoadByAssetKey:
                {
                    if (!sceneIdentifier)
                        return false;
                    return SceneManager::LoadScene(sceneIdentifier, loadSceneMode);
                }
                case SceneTransitionType::ReloadCurrentScene:
                    return SceneManager::ReloadCurrentScene();
                case SceneTransitionType::SetActiveSceneByAssetKey:
                    if (!sceneIdentifier)
                        return false;
                    return SceneManager::SetActiveScene(sceneIdentifier);
                case SceneTransitionType::UnloadByAssetKey:
                    if (!sceneIdentifier)
                        return false;
                    return SceneManager::UnloadScene(sceneIdentifier);
            }

            return false;
        }

        float ForwardInputActionAxis1DToHost(const char* mapName, const char* actionName)
        {
            if (ShouldSuppressGameplayInput())
                return 0.0f;
            const std::string_view safeMapName = mapName ? std::string_view(mapName) : std::string_view();
            const std::string_view safeActionName = actionName ? std::string_view(actionName) : std::string_view();
            return InputSystem::GetInstance().ReadActionAxis1D(safeMapName, safeActionName);
        }

        glm::vec2 ForwardInputActionAxis2DToHost(const char* mapName, const char* actionName)
        {
            if (ShouldSuppressGameplayInput())
                return glm::vec2(0.0f);
            const std::string_view safeMapName = mapName ? std::string_view(mapName) : std::string_view();
            const std::string_view safeActionName = actionName ? std::string_view(actionName) : std::string_view();
            return InputSystem::GetInstance().ReadActionAxis2D(safeMapName, safeActionName);
        }

        glm::vec2 ForwardInputMousePositionToHost()
        {
            if (ShouldSuppressGameplayInput())
                return glm::vec2(0.0f);
            return InputSystem::GetInstance().GetMousePosition();
        }

        glm::vec2 ForwardInputMouseDeltaToHost()
        {
            if (ShouldSuppressGameplayInput())
                return glm::vec2(0.0f);
            return InputSystem::GetInstance().GetMouseDelta();
        }

        glm::vec2 ForwardInputMouseWheelDeltaToHost()
        {
            if (ShouldSuppressGameplayInput())
                return glm::vec2(0.0f);
            return InputSystem::GetInstance().GetMouseWheelDelta();
        }

        bool ForwardInputMouseButtonToHost(uint8_t button)
        {
            if (ShouldSuppressGameplayInput())
                return false;
            return InputSystem::GetInstance().IsMouseButtonDown(button);
        }

        bool ForwardInputMouseButtonDownToHost(uint8_t button)
        {
            if (ShouldSuppressGameplayInput())
                return false;
            return InputSystem::GetInstance().WasMouseButtonPressedThisFrame(button);
        }

        bool ForwardInputMouseButtonUpToHost(uint8_t button)
        {
            if (ShouldSuppressGameplayInput())
                return false;
            return InputSystem::GetInstance().WasMouseButtonReleasedThisFrame(button);
        }

        bool ForwardInputActionPressedToHost(const char* mapName, const char* actionName, float deadzone)
        {
            if (ShouldSuppressGameplayInput())
                return false;
            const std::string_view safeMapName = mapName ? std::string_view(mapName) : std::string_view();
            const std::string_view safeActionName = actionName ? std::string_view(actionName) : std::string_view();
            return InputSystem::GetInstance().IsActionPressed(safeMapName, safeActionName, deadzone);
        }

        bool ForwardInputActionExistsToHost(const char* mapName, const char* actionName)
        {
            const std::string_view safeMapName = mapName ? std::string_view(mapName) : std::string_view();
            const std::string_view safeActionName = actionName ? std::string_view(actionName) : std::string_view();
            return InputSystem::GetInstance().HasAction(safeMapName, safeActionName);
        }

        bool ForwardInputActionStartedToHost(const char* mapName, const char* actionName)
        {
            if (ShouldSuppressGameplayInput())
                return false;
            const std::string_view safeMapName = mapName ? std::string_view(mapName) : std::string_view();
            const std::string_view safeActionName = actionName ? std::string_view(actionName) : std::string_view();
            return InputSystem::GetInstance().WasActionStartedThisFrame(safeMapName, safeActionName);
        }

        bool ForwardInputActionPerformedToHost(const char* mapName, const char* actionName)
        {
            if (ShouldSuppressGameplayInput())
                return false;
            const std::string_view safeMapName = mapName ? std::string_view(mapName) : std::string_view();
            const std::string_view safeActionName = actionName ? std::string_view(actionName) : std::string_view();
            return InputSystem::GetInstance().WasActionPerformedThisFrame(safeMapName, safeActionName);
        }

        bool ForwardInputActionCanceledToHost(const char* mapName, const char* actionName)
        {
            if (ShouldSuppressGameplayInput())
                return false;
            const std::string_view safeMapName = mapName ? std::string_view(mapName) : std::string_view();
            const std::string_view safeActionName = actionName ? std::string_view(actionName) : std::string_view();
            return InputSystem::GetInstance().WasActionCanceledThisFrame(safeMapName, safeActionName);
        }

        bool ForwardInputActionButtonToHost(const char* mapName, const char* actionName)
        {
            if (ShouldSuppressGameplayInput())
                return false;
            const std::string_view safeMapName = mapName ? std::string_view(mapName) : std::string_view();
            const std::string_view safeActionName = actionName ? std::string_view(actionName) : std::string_view();
            return InputSystem::GetInstance().ReadActionButton(safeMapName, safeActionName);
        }

        bool ForwardPhysics2DRaycastToHost(float originX,
                                           float originY,
                                           float directionX,
                                           float directionY,
                                           float maxDistance,
                                           uint64_t collisionMask,
                                           RaycastHit2D* outHit)
        {
            if (!outHit)
                return false;

            *outHit = RaycastHit2D{};
            Scene* scene = Physics2DQueries::GetActiveSceneForScriptQueries();
            if (!scene)
                return false;

            const Physics2DRaycastHit nativeHit = Physics2DQueries::RaycastClosest(
                scene,
                glm::vec2(originX, originY),
                glm::vec2(directionX, directionY),
                maxDistance,
                collisionMask);
            if (!nativeHit.HasHit)
                return false;

            outHit->HasHit = true;
            outHit->Entity = nativeHit.Entity;
            outHit->Point = nativeHit.Point;
            outHit->Normal = nativeHit.Normal;
            outHit->Fraction = nativeHit.Fraction;
            return true;
        }

        void ForwardScriptLogToHost(ScriptLogSeverity severity, const char* message)
        {
            const std::string_view safeMessage = message ? std::string_view(message) : std::string_view();
            switch (severity)
            {
                case ScriptLogSeverity::Info:
                    LT_INFO("[Script] {}", safeMessage);
                    break;
                case ScriptLogSeverity::Warning:
                    LT_WARN("[Script] {}", safeMessage);
                    break;
                case ScriptLogSeverity::Error:
                    LT_ERROR("[Script] {}", safeMessage);
                    break;
            }
        }

        entt::entity ForwardScriptCreateEntityToHost(const char* name)
        {
            Scene* scene = Physics2DQueries::GetActiveSceneForScriptQueries();
            if (!scene)
                return entt::null;
            if (!name || name[0] == '\0')
                return scene->CreateEntity("Entity");
            return scene->CreateEntity(name);
        }

        void ForwardScriptDestroyEntityToHost(entt::entity entity)
        {
            Scene* scene = Physics2DQueries::GetActiveSceneForScriptQueries();
            if (!scene)
                return;
            scene->DestroyEntity(entity);
        }

        entt::entity ForwardScriptInstantiatePrefabToHost(const char* prefabAssetKey, entt::entity parentEntity)
        {
            Scene* scene = Physics2DQueries::GetActiveSceneForScriptQueries();
            if (!scene || !prefabAssetKey || prefabAssetKey[0] == '\0')
                return entt::null;
            return scene->InstantiatePrefab(prefabAssetKey, parentEntity);
        }

        entt::entity ForwardScriptResolveEntityReferenceToHost(entt::entity entity)
        {
            Scene* scene = Physics2DQueries::GetActiveSceneForScriptQueries();
            if (!scene || entity == entt::null)
                return entity;
            return scene->ResolveEntityReference(entity);
        }

        uint32_t ForwardScriptContactEntityHandlesToHost(entt::entity entity,
                                                         bool includeSensorContacts,
                                                         entt::entity* outHandles,
                                                         uint32_t capacity)
        {
            Scene* scene = Physics2DQueries::GetActiveSceneForScriptQueries();
            if (!scene || entity == entt::null)
                return 0u;

            auto& registry = scene->GetRegistry();
            if (!registry.valid(entity))
                return 0u;

            const Physics2DContactListener* contacts = scene->GetPhysics2DContactEventsForEntity(entity);
            if (!contacts)
                return 0u;

            std::vector<entt::entity> uniqueContacts;
            std::unordered_set<entt::entity> seenContacts;
            const auto& events = contacts->GetEvents();
            for (const auto& eventData : events)
            {
                if (!includeSensorContacts && eventData.IsSensor)
                    continue;

                entt::entity other = entt::null;
                if (eventData.EntityA == entity)
                    other = eventData.EntityB;
                else if (eventData.EntityB == entity)
                    other = eventData.EntityA;

                if (other == entt::null || !registry.valid(other))
                    continue;
                if (seenContacts.insert(other).second)
                    uniqueContacts.push_back(other);
            }

            const uint32_t totalCount = static_cast<uint32_t>(uniqueContacts.size());
            if (!outHandles || capacity == 0u)
                return totalCount;

            const uint32_t toWrite = std::min(totalCount, capacity);
            for (uint32_t index = 0; index < toWrite; ++index)
                outHandles[index] = uniqueContacts[static_cast<size_t>(index)];
            return toWrite;
        }

        bool ForwardScriptParallelExecutionStateToHost()
        {
            return Scene::IsCurrentThreadParallelScriptExecution();
        }

        template<typename TCallback>
        bool ConnectOptionalScriptCoreBridge(void* libraryHandle, const char* exportName, TCallback callback)
        {
            if (!libraryHandle || !exportName || exportName[0] == '\0')
                return false;

            using SetterFunction = void (*)(TCallback callbackValue);
            auto setter = reinterpret_cast<SetterFunction>(PlatformUtils::GetProcAddress(libraryHandle, exportName));
            if (!setter)
                return false;

            setter(callback);
            return true;
        }

        void BindLoadedScriptCoreHostBridges(void* libraryHandle)
        {
            ConnectOptionalScriptCoreBridge<SceneTransitionBridgeCallback>(libraryHandle, "LT_SetSceneTransitionBridge", &ForwardSceneTransitionToHost);
            ConnectOptionalScriptCoreBridge<InputActionAxis1DBridgeCallback>(libraryHandle, "LT_SetInputActionAxis1DBridge", &ForwardInputActionAxis1DToHost);
            ConnectOptionalScriptCoreBridge<InputActionAxis2DBridgeCallback>(libraryHandle, "LT_SetInputActionAxis2DBridge", &ForwardInputActionAxis2DToHost);
            ConnectOptionalScriptCoreBridge<InputMouseVector2BridgeCallback>(libraryHandle, "LT_SetInputMousePositionBridge", &ForwardInputMousePositionToHost);
            ConnectOptionalScriptCoreBridge<InputMouseVector2BridgeCallback>(libraryHandle, "LT_SetInputMouseDeltaBridge", &ForwardInputMouseDeltaToHost);
            ConnectOptionalScriptCoreBridge<InputMouseVector2BridgeCallback>(libraryHandle, "LT_SetInputMouseWheelDeltaBridge", &ForwardInputMouseWheelDeltaToHost);
            ConnectOptionalScriptCoreBridge<InputMouseButtonBridgeCallback>(libraryHandle, "LT_SetInputMouseButtonBridge", &ForwardInputMouseButtonToHost);
            ConnectOptionalScriptCoreBridge<InputMouseButtonBridgeCallback>(libraryHandle, "LT_SetInputMouseButtonDownBridge", &ForwardInputMouseButtonDownToHost);
            ConnectOptionalScriptCoreBridge<InputMouseButtonBridgeCallback>(libraryHandle, "LT_SetInputMouseButtonUpBridge", &ForwardInputMouseButtonUpToHost);
            ConnectOptionalScriptCoreBridge<InputActionPressedBridgeCallback>(libraryHandle, "LT_SetInputActionPressedBridge", &ForwardInputActionPressedToHost);
            ConnectOptionalScriptCoreBridge<InputActionExistsBridgeCallback>(libraryHandle, "LT_SetInputActionExistsBridge", &ForwardInputActionExistsToHost);
            ConnectOptionalScriptCoreBridge<InputActionTriggerBridgeCallback>(libraryHandle, "LT_SetInputActionStartedBridge", &ForwardInputActionStartedToHost);
            ConnectOptionalScriptCoreBridge<InputActionTriggerBridgeCallback>(libraryHandle, "LT_SetInputActionPerformedBridge", &ForwardInputActionPerformedToHost);
            ConnectOptionalScriptCoreBridge<InputActionTriggerBridgeCallback>(libraryHandle, "LT_SetInputActionCanceledBridge", &ForwardInputActionCanceledToHost);
            ConnectOptionalScriptCoreBridge<InputActionTriggerBridgeCallback>(libraryHandle, "LT_SetInputActionButtonBridge", &ForwardInputActionButtonToHost);
            ConnectOptionalScriptCoreBridge<Physics2DRaycastBridgeCallback>(libraryHandle, "LT_SetPhysics2DRaycastBridge", &ForwardPhysics2DRaycastToHost);
            ConnectOptionalScriptCoreBridge<ScriptLogBridgeCallback>(libraryHandle, "LT_SetScriptLogBridge", &ForwardScriptLogToHost);
            ConnectOptionalScriptCoreBridge<ScriptCreateEntityBridgeCallback>(libraryHandle, "LT_SetScriptCreateEntityBridge", &ForwardScriptCreateEntityToHost);
            ConnectOptionalScriptCoreBridge<ScriptDestroyEntityBridgeCallback>(libraryHandle, "LT_SetScriptDestroyEntityBridge", &ForwardScriptDestroyEntityToHost);
            ConnectOptionalScriptCoreBridge<ScriptInstantiatePrefabBridgeCallback>(libraryHandle, "LT_SetScriptInstantiatePrefabBridge", &ForwardScriptInstantiatePrefabToHost);
            ConnectOptionalScriptCoreBridge<ScriptResolveEntityReferenceBridgeCallback>(libraryHandle, "LT_SetScriptResolveEntityReferenceBridge", &ForwardScriptResolveEntityReferenceToHost);
            ConnectOptionalScriptCoreBridge<ScriptGetContactEntityHandlesBridgeCallback>(libraryHandle, "LT_SetScriptContactEntityHandlesBridge", &ForwardScriptContactEntityHandlesToHost);
            ConnectOptionalScriptCoreBridge<InputActionTriggerBridgeCallback>(libraryHandle, "LT_SetInputButtonDownBridge", &ForwardInputActionStartedToHost);
            ConnectOptionalScriptCoreBridge<InputActionTriggerBridgeCallback>(libraryHandle, "LT_SetInputButtonBridge", &ForwardInputActionButtonToHost);
        }

        void UnbindLoadedScriptCoreHostBridges(void* libraryHandle)
        {
            ConnectOptionalScriptCoreBridge<SceneTransitionBridgeCallback>(libraryHandle, "LT_SetSceneTransitionBridge", nullptr);
            ConnectOptionalScriptCoreBridge<InputActionAxis1DBridgeCallback>(libraryHandle, "LT_SetInputActionAxis1DBridge", nullptr);
            ConnectOptionalScriptCoreBridge<InputActionAxis2DBridgeCallback>(libraryHandle, "LT_SetInputActionAxis2DBridge", nullptr);
            ConnectOptionalScriptCoreBridge<InputMouseVector2BridgeCallback>(libraryHandle, "LT_SetInputMousePositionBridge", nullptr);
            ConnectOptionalScriptCoreBridge<InputMouseVector2BridgeCallback>(libraryHandle, "LT_SetInputMouseDeltaBridge", nullptr);
            ConnectOptionalScriptCoreBridge<InputMouseVector2BridgeCallback>(libraryHandle, "LT_SetInputMouseWheelDeltaBridge", nullptr);
            ConnectOptionalScriptCoreBridge<InputMouseButtonBridgeCallback>(libraryHandle, "LT_SetInputMouseButtonBridge", nullptr);
            ConnectOptionalScriptCoreBridge<InputMouseButtonBridgeCallback>(libraryHandle, "LT_SetInputMouseButtonDownBridge", nullptr);
            ConnectOptionalScriptCoreBridge<InputMouseButtonBridgeCallback>(libraryHandle, "LT_SetInputMouseButtonUpBridge", nullptr);
            ConnectOptionalScriptCoreBridge<InputActionPressedBridgeCallback>(libraryHandle, "LT_SetInputActionPressedBridge", nullptr);
            ConnectOptionalScriptCoreBridge<InputActionExistsBridgeCallback>(libraryHandle, "LT_SetInputActionExistsBridge", nullptr);
            ConnectOptionalScriptCoreBridge<InputActionTriggerBridgeCallback>(libraryHandle, "LT_SetInputActionStartedBridge", nullptr);
            ConnectOptionalScriptCoreBridge<InputActionTriggerBridgeCallback>(libraryHandle, "LT_SetInputActionPerformedBridge", nullptr);
            ConnectOptionalScriptCoreBridge<InputActionTriggerBridgeCallback>(libraryHandle, "LT_SetInputActionCanceledBridge", nullptr);
            ConnectOptionalScriptCoreBridge<InputActionTriggerBridgeCallback>(libraryHandle, "LT_SetInputActionButtonBridge", nullptr);
            ConnectOptionalScriptCoreBridge<Physics2DRaycastBridgeCallback>(libraryHandle, "LT_SetPhysics2DRaycastBridge", nullptr);
            ConnectOptionalScriptCoreBridge<ScriptLogBridgeCallback>(libraryHandle, "LT_SetScriptLogBridge", nullptr);
            ConnectOptionalScriptCoreBridge<ScriptCreateEntityBridgeCallback>(libraryHandle, "LT_SetScriptCreateEntityBridge", nullptr);
            ConnectOptionalScriptCoreBridge<ScriptDestroyEntityBridgeCallback>(libraryHandle, "LT_SetScriptDestroyEntityBridge", nullptr);
            ConnectOptionalScriptCoreBridge<ScriptInstantiatePrefabBridgeCallback>(libraryHandle, "LT_SetScriptInstantiatePrefabBridge", nullptr);
            ConnectOptionalScriptCoreBridge<ScriptResolveEntityReferenceBridgeCallback>(libraryHandle, "LT_SetScriptResolveEntityReferenceBridge", nullptr);
            ConnectOptionalScriptCoreBridge<ScriptGetContactEntityHandlesBridgeCallback>(libraryHandle, "LT_SetScriptContactEntityHandlesBridge", nullptr);
            ConnectOptionalScriptCoreBridge<InputActionTriggerBridgeCallback>(libraryHandle, "LT_SetInputButtonDownBridge", nullptr);
            ConnectOptionalScriptCoreBridge<InputActionTriggerBridgeCallback>(libraryHandle, "LT_SetInputButtonBridge", nullptr);
        }

        void BindNativeScriptHostBridges()
        {
            ScriptableEntity::SetCreateEntityBridgeCallback(&ForwardScriptCreateEntityToHost);
            ScriptableEntity::SetDestroyEntityBridgeCallback(&ForwardScriptDestroyEntityToHost);
            ScriptableEntity::SetInstantiatePrefabBridgeCallback(&ForwardScriptInstantiatePrefabToHost);
            ScriptableEntity::SetResolveEntityReferenceBridgeCallback(&ForwardScriptResolveEntityReferenceToHost);
            ScriptableEntity::SetContactEntityHandlesBridgeCallback(&ForwardScriptContactEntityHandlesToHost);
            ScriptableEntity::SetParallelScriptExecutionBridgeCallback(&ForwardScriptParallelExecutionStateToHost);
            Entity::SetDestroyBridgeCallback(&ForwardScriptDestroyEntityToHost);
            Entity::SetParallelExecutionBridgeCallback(&ForwardScriptParallelExecutionStateToHost);
        }

        void UnbindNativeScriptHostBridges()
        {
            ScriptableEntity::SetCreateEntityBridgeCallback(nullptr);
            ScriptableEntity::SetDestroyEntityBridgeCallback(nullptr);
            ScriptableEntity::SetInstantiatePrefabBridgeCallback(nullptr);
            ScriptableEntity::SetResolveEntityReferenceBridgeCallback(nullptr);
            ScriptableEntity::SetContactEntityHandlesBridgeCallback(nullptr);
            ScriptableEntity::SetParallelScriptExecutionBridgeCallback(nullptr);
            Entity::SetDestroyBridgeCallback(nullptr);
            Entity::SetParallelExecutionBridgeCallback(nullptr);
        }

        bool ReloadScriptCoreModule(const std::filesystem::path& libraryPath)
        {
            std::error_code errorCode;
            const auto sourceTimestamp = std::filesystem::last_write_time(libraryPath, errorCode);
            if (errorCode)
            {
                LT_WARN("ScriptCore runtime: failed to read timestamp for '{}'.", libraryPath.string());
                return false;
            }

            const std::filesystem::path stagedLibraryPath = libraryPath.parent_path()
                / GetRuntimeLoadedLibraryFileName(++s_RuntimeState.ReloadCounter);

            std::filesystem::copy_file(
                libraryPath,
                stagedLibraryPath,
                std::filesystem::copy_options::overwrite_existing,
                errorCode);
            if (errorCode)
            {
                LT_WARN("ScriptCore runtime: failed to stage '{}': {}", libraryPath.string(), errorCode.message());
                return false;
            }

            void* loadedLibraryHandle = PlatformUtils::LoadLibrary(stagedLibraryPath.string());
            if (!loadedLibraryHandle)
            {
                LT_WARN("ScriptCore runtime: failed to load staged library '{}'.", stagedLibraryPath.string());
                std::filesystem::remove(stagedLibraryPath, errorCode);
                (void)errorCode;
                return false;
            }

            const auto getAbiVersionFunction = reinterpret_cast<GetScriptCoreAbiVersionFunction>(
                PlatformUtils::GetProcAddress(loadedLibraryHandle, "LT_GetScriptCoreAbiVersion"));
            if (!getAbiVersionFunction)
            {
                LT_WARN("ScriptCore runtime: missing LT_GetScriptCoreAbiVersion export in '{}'.",
                        libraryPath.string());
                PlatformUtils::FreeLibrary(loadedLibraryHandle);
                std::filesystem::remove(stagedLibraryPath, errorCode);
                (void)errorCode;
                return false;
            }

            const uint32_t reportedAbiVersion = getAbiVersionFunction();
            if (reportedAbiVersion != kScriptCoreAbiVersion)
            {
                LT_WARN("ScriptCore runtime: ABI mismatch for '{}'. expected={}, got={}. Rebuild project scripts/ScriptCore for this engine revision.",
                        libraryPath.string(),
                        kScriptCoreAbiVersion,
                        reportedAbiVersion);
                PlatformUtils::FreeLibrary(loadedLibraryHandle);
                std::filesystem::remove(stagedLibraryPath, errorCode);
                (void)errorCode;
                return false;
            }

            const auto registerFunction = reinterpret_cast<RegisterScriptCoreTypesFunction>(
                PlatformUtils::GetProcAddress(loadedLibraryHandle, "LT_RegisterScriptCoreTypes"));
            if (!registerFunction)
            {
                LT_WARN("ScriptCore runtime: missing LT_RegisterScriptCoreTypes export in '{}'.",
                        libraryPath.string());
                PlatformUtils::FreeLibrary(loadedLibraryHandle);
                std::filesystem::remove(stagedLibraryPath, errorCode);
                (void)errorCode;
                return false;
            }

            BindLoadedScriptCoreHostBridges(loadedLibraryHandle);
            if (ConnectOptionalScriptCoreBridge<ScriptLogBridgeCallback>(loadedLibraryHandle, "LT_SetScriptLogBridge", &ForwardScriptLogToHost))
            {
                LT_INFO("ScriptCore runtime: script log bridge connected.");
            }
            else
            {
                LT_WARN("ScriptCore runtime: LT_SetScriptLogBridge export missing; script logs will be suppressed.");
            }

            ResetRuntimeScriptRegistry();
            registerFunction(&RegisterScriptFromModule);
            const auto registeredScripts = NativeScriptRegistry::GetRegisteredScriptNames();

            void* previousLibraryHandle = s_RuntimeState.LibraryHandle;
            const std::filesystem::path previousLoadedLibraryPath = s_RuntimeState.LoadedLibraryPath;

            s_RuntimeState.LibraryHandle = loadedLibraryHandle;
            s_RuntimeState.SourceLibraryPath = libraryPath;
            s_RuntimeState.LoadedLibraryPath = stagedLibraryPath;
            s_RuntimeState.LastWriteTime = sourceTimestamp;

            if (previousLibraryHandle != nullptr)
            {
                UnbindLoadedScriptCoreHostBridges(previousLibraryHandle);
                PlatformUtils::FreeLibrary(previousLibraryHandle);
            }
            if (!previousLoadedLibraryPath.empty())
            {
                std::error_code removeError;
                std::filesystem::remove(previousLoadedLibraryPath, removeError);
                (void)removeError;
            }

            LT_INFO("ScriptCore runtime: loaded scripts from '{}' (registered={} script(s)).",
                    libraryPath.string(),
                    registeredScripts.size());
            return true;
        }

        bool TryReloadScriptCoreFromCandidates(const std::vector<std::filesystem::path>& candidatePaths)
        {
            for (const auto& candidatePath : candidatePaths)
            {
                std::error_code existsError;
                if (!std::filesystem::exists(candidatePath, existsError))
                    continue;
                if (existsError)
                    continue;

                if (ReloadScriptCoreModule(candidatePath))
                    return true;
            }
            return false;
        }
    }

    void SetGameplayInputRoutingState(bool gameViewFocused,
                                      bool gameViewHovered,
                                      bool uiWantsMouseCapture,
                                      bool uiWantsKeyboardCapture)
    {
        s_GameplayInputRoutingState.GameViewFocused = gameViewFocused;
        s_GameplayInputRoutingState.GameViewHovered = gameViewHovered;
        s_GameplayInputRoutingState.UiWantsMouseCapture = uiWantsMouseCapture;
        s_GameplayInputRoutingState.UiWantsKeyboardCapture = uiWantsKeyboardCapture;
    }

    void Initialize()
    {
        if (s_RuntimeState.Initialized)
            return;

        ResetRuntimeScriptRegistry();
        BindNativeScriptHostBridges();
        s_RuntimeState.Initialized = true;
        s_RuntimeState.LastPollTime = std::chrono::steady_clock::now();

        const auto candidatePaths = BuildScriptCoreLibraryCandidates();
        LogScriptCoreLibraryCandidates(candidatePaths);
        const bool scriptCoreLoaded = TryReloadScriptCoreFromCandidates(candidatePaths);

        InitializeManagedScriptHost();

        if (!scriptCoreLoaded)
            LT_INFO("ScriptCore runtime: ScriptCore module not found yet, using built-in scripts only.");
    }

    void Shutdown()
    {
        if (!s_RuntimeState.Initialized)
            return;

        ManagedScriptHost::Shutdown();
        ResetManagedRuntimeState();

        if (s_RuntimeState.LibraryHandle != nullptr)
        {
            UnbindLoadedScriptCoreHostBridges(s_RuntimeState.LibraryHandle);
            PlatformUtils::FreeLibrary(s_RuntimeState.LibraryHandle);
            s_RuntimeState.LibraryHandle = nullptr;
        }

        if (!s_RuntimeState.LoadedLibraryPath.empty())
        {
            std::error_code removeError;
            std::filesystem::remove(s_RuntimeState.LoadedLibraryPath, removeError);
            (void)removeError;
            s_RuntimeState.LoadedLibraryPath.clear();
        }

        s_RuntimeState.SourceLibraryPath.clear();
        s_RuntimeState.LastWriteTime = {};
        s_RuntimeState.LastRejectedSourcePath.clear();
        s_RuntimeState.LastRejectedSourceWriteTime = {};
        UnbindNativeScriptHostBridges();
        s_RuntimeState.Initialized = false;
    }

    void Update(EditorPlayModeState playModeState)
    {
        if (!s_RuntimeState.Initialized)
            return;

        // Avoid module reload while runtime scripts may be executing.
        if (playModeState != EditorPlayModeState::Edit)
            return;

        const auto now = std::chrono::steady_clock::now();
        if (now - s_RuntimeState.LastPollTime < std::chrono::milliseconds(500))
            return;
        s_RuntimeState.LastPollTime = now;

        UpdateManagedScriptHost();

        // Always prefer the highest-priority available candidate. This allows
        // switching from fallback ScriptCore (e.g. next to Editor.exe) to the
        // freshly built project-local ScriptCore as soon as it appears.
        std::filesystem::path sourcePath;
        for (const auto& candidatePath : BuildScriptCoreLibraryCandidates())
        {
            std::error_code existsError;
            if (std::filesystem::exists(candidatePath, existsError))
            {
                sourcePath = candidatePath;
                break;
            }
        }

        if (sourcePath.empty())
            return;

        std::error_code timeError;
        const auto writeTime = std::filesystem::last_write_time(sourcePath, timeError);
        const bool hasWriteTime = !timeError;

        auto markSourceRejected = [&](const std::filesystem::path& rejectedPath) {
            if (!hasWriteTime)
                return;
            s_RuntimeState.LastRejectedSourcePath = rejectedPath;
            s_RuntimeState.LastRejectedSourceWriteTime = writeTime;
        };

        auto clearRejectedSource = [&]() {
            s_RuntimeState.LastRejectedSourcePath.clear();
            s_RuntimeState.LastRejectedSourceWriteTime = {};
        };

        const bool sourceKnownRejected =
            hasWriteTime &&
            s_RuntimeState.LastRejectedSourcePath == sourcePath &&
            s_RuntimeState.LastRejectedSourceWriteTime == writeTime;

        if (s_RuntimeState.SourceLibraryPath != sourcePath)
        {
            if (sourceKnownRejected)
                return;

            if (ReloadScriptCoreModule(sourcePath))
                clearRejectedSource();
            else
                markSourceRejected(sourcePath);
            return;
        }

        if (s_RuntimeState.LibraryHandle == nullptr)
        {
            if (TryReloadScriptCoreFromCandidates(BuildScriptCoreLibraryCandidates()))
                clearRejectedSource();
            return;
        }

        if (!hasWriteTime)
            return;

        if (writeTime != s_RuntimeState.LastWriteTime)
        {
            if (sourceKnownRejected)
                return;

            if (ReloadScriptCoreModule(sourcePath))
                clearRejectedSource();
            else
                markSourceRejected(sourcePath);
        }
    }

    void InitializeIncrementalCompiler()
    {
        const auto engineRoot = FindEngineWorkspaceRoot();
        const auto projectRoot = GetOpenProjectRoot();
        if (!engineRoot.has_value() || !projectRoot.has_value())
        {
            LT_INFO("ScriptCoreModuleRuntime: Cannot initialize incremental compiler — missing engine or project root.");
            return;
        }

        auto& compiler = IncrementalScriptCompiler::GetInstance();
        compiler.Initialize(engineRoot.value(), projectRoot.value());
    }

    void ShutdownIncrementalCompiler()
    {
        IncrementalScriptCompiler::GetInstance().Shutdown();
    }

    void SetAutoRecompileOnSave(bool enabled)
    {
        IncrementalScriptCompiler::GetInstance().SetAutoRecompileEnabled(enabled);
    }

    bool IsAutoRecompileOnSave()
    {
        return IncrementalScriptCompiler::GetInstance().IsAutoRecompileEnabled();
    }

    bool HasPendingScriptFileChanges()
    {
        return IncrementalScriptCompiler::GetInstance().HasPendingScriptChanges();
    }
}
