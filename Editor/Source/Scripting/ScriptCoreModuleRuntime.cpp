#include "ScriptCoreModuleRuntime.h"

#include "Core/Debug/Log.h"
#include "Platform/Platform.h"
#include "Scripting/NativeScriptRegistry.h"
#include "Scripting/ScriptCoreApi.h"

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Limitless::ScriptCoreModuleRuntime
{
    namespace
    {
        using RegisterScriptCoreTypesFunction = void (*)(NativeScriptRegistrationCallback registrationCallback);

        struct RuntimeState final
        {
            bool Initialized = false;
            void* LibraryHandle = nullptr;
            std::filesystem::path SourceLibraryPath;
            std::filesystem::path LoadedLibraryPath;
            std::filesystem::file_time_type LastWriteTime{};
            uint64_t ReloadCounter = 0;
            std::chrono::steady_clock::time_point LastPollTime{};
        };

        RuntimeState s_RuntimeState;

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

        std::filesystem::path BuildConfigOutputFolder()
        {
            std::string configName = "debug";
#if defined(LT_CONFIG_RELEASE)
            configName = "release";
#elif defined(LT_CONFIG_DIST)
            configName = "dist";
#endif

            std::string architectureName = "x64";
            std::string platformToken = "x64";
#if defined(LT_ARCHITECTURE_ARM64)
            architectureName = "arm64";
            platformToken = "ARM64";
#endif

            return std::filesystem::path("Build")
                / (configName + "_" + architectureName + "-windows-" + platformToken)
                / "Editor";
        }

        std::vector<std::filesystem::path> BuildScriptCoreLibraryCandidates()
        {
            std::vector<std::filesystem::path> candidates;

            const std::string executablePath = PlatformDetection::GetExecutablePath();
            if (!executablePath.empty())
            {
                const std::filesystem::path executableDirectory = std::filesystem::path(executablePath).parent_path();
                if (!executableDirectory.empty())
                    candidates.push_back(executableDirectory / "ScriptCore.dll");
            }

            if (const auto engineRoot = FindEngineWorkspaceRoot(); engineRoot.has_value())
            {
                candidates.push_back(engineRoot.value() / BuildConfigOutputFolder() / "ScriptCore.dll");
            }

            return candidates;
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

        bool ReloadScriptCoreModule(const std::filesystem::path& libraryPath)
        {
            ResetRuntimeScriptRegistry();

            if (s_RuntimeState.LibraryHandle != nullptr)
            {
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

            std::error_code errorCode;
            const auto sourceTimestamp = std::filesystem::last_write_time(libraryPath, errorCode);
            if (errorCode)
            {
                LT_WARN("ScriptCore runtime: failed to read timestamp for '{}'.", libraryPath.string());
                return false;
            }

            const std::filesystem::path stagedLibraryPath = libraryPath.parent_path()
                / ("ScriptCore.RuntimeLoaded." + std::to_string(++s_RuntimeState.ReloadCounter) + ".dll");

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

            s_RuntimeState.LibraryHandle = PlatformUtils::LoadLibrary(stagedLibraryPath.string());
            if (!s_RuntimeState.LibraryHandle)
            {
                LT_WARN("ScriptCore runtime: failed to load staged library '{}'.", stagedLibraryPath.string());
                std::filesystem::remove(stagedLibraryPath, errorCode);
                (void)errorCode;
                return false;
            }

            const auto registerFunction = reinterpret_cast<RegisterScriptCoreTypesFunction>(
                PlatformUtils::GetProcAddress(s_RuntimeState.LibraryHandle, "LT_RegisterScriptCoreTypes"));
            if (!registerFunction)
            {
                LT_WARN("ScriptCore runtime: missing LT_RegisterScriptCoreTypes export.");
                PlatformUtils::FreeLibrary(s_RuntimeState.LibraryHandle);
                s_RuntimeState.LibraryHandle = nullptr;
                std::filesystem::remove(stagedLibraryPath, errorCode);
                (void)errorCode;
                return false;
            }

            registerFunction(&RegisterScriptFromModule);
            s_RuntimeState.SourceLibraryPath = libraryPath;
            s_RuntimeState.LoadedLibraryPath = stagedLibraryPath;
            s_RuntimeState.LastWriteTime = sourceTimestamp;
            LT_INFO("ScriptCore runtime: loaded scripts from '{}'.", libraryPath.string());
            return true;
        }
    }

    void Initialize()
    {
        if (s_RuntimeState.Initialized)
            return;

        ResetRuntimeScriptRegistry();
        s_RuntimeState.Initialized = true;
        s_RuntimeState.LastPollTime = std::chrono::steady_clock::now();

        for (const auto& candidatePath : BuildScriptCoreLibraryCandidates())
        {
            std::error_code existsError;
            if (std::filesystem::exists(candidatePath, existsError))
            {
                (void)ReloadScriptCoreModule(candidatePath);
                return;
            }
        }

        LT_INFO("ScriptCore runtime: ScriptCore.dll not found yet, using built-in scripts only.");
    }

    void Shutdown()
    {
        if (!s_RuntimeState.Initialized)
            return;

        if (s_RuntimeState.LibraryHandle != nullptr)
        {
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

        std::filesystem::path sourcePath = s_RuntimeState.SourceLibraryPath;
        if (sourcePath.empty())
        {
            for (const auto& candidatePath : BuildScriptCoreLibraryCandidates())
            {
                std::error_code existsError;
                if (std::filesystem::exists(candidatePath, existsError))
                {
                    sourcePath = candidatePath;
                    break;
                }
            }
        }

        if (sourcePath.empty())
            return;

        std::error_code timeError;
        const auto writeTime = std::filesystem::last_write_time(sourcePath, timeError);
        if (timeError)
            return;

        if (s_RuntimeState.LibraryHandle == nullptr || writeTime != s_RuntimeState.LastWriteTime)
        {
            (void)ReloadScriptCoreModule(sourcePath);
        }
    }
}
