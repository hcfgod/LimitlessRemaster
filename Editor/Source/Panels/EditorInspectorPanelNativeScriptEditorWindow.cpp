#include "PrecompiledHeader.h"
#include "EditorInspectorPanelNativeScriptEditorShared.h"

#include "Assets/AssetPaths.h"
#include "Core/Debug/Log.h"
#include "EditorPanelStyle.h"
#include "Project/BuildSettings.h"
#include "Scripting/NativeScriptExternalEditor.h"
#include "imgui/imgui.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <optional>

namespace Limitless::EditorInspectorPanel::Internal
{
    bool s_HasPendingNativeScriptEditorSessionRestore = false;
    NativeScriptEditorSessionState s_PendingNativeScriptEditorSessionState;


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

        bool MirrorScriptToGeneratedDirectory(const NativeScriptAuthoringState& state, std::string& outError, bool* outMirroredChange)
        {
            if (outMirroredChange)
                *outMirroredChange = false;

            std::vector<std::filesystem::path> generatedDirectories = GetGeneratedScriptCoreMirrorDirectories();
            if (generatedDirectories.empty())
            {
                if (const auto inferredProjectRoot = InferProjectRootFromScriptSourcePath(state.SourcePath); inferredProjectRoot.has_value())
                    generatedDirectories.push_back(inferredProjectRoot.value() / "Build" / "Generated" / "ScriptCore");
            }

            if (generatedDirectories.empty())
            {
                outError = "Could not locate generated ScriptCore mirror directory.";
                return true;
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

            std::error_code createDirectoriesError;
            for (const std::filesystem::path& generatedDirectory : generatedDirectories)
            {
                const std::filesystem::path generatedHeaderPath = generatedDirectory / relativeMirrorPathWithoutExtension;
                const std::filesystem::path generatedSourcePath = generatedDirectory / relativeMirrorPathWithoutExtension;
                const std::filesystem::path generatedHeaderFile = generatedHeaderPath.string() + ".h";
                const std::filesystem::path generatedSourceFile = generatedSourcePath.string() + ".cpp";

                std::filesystem::create_directories(generatedHeaderFile.parent_path(), createDirectoriesError);
                if (createDirectoriesError)
                {
                    outError = "Failed to create generated ScriptCore mirror directory '" + generatedHeaderFile.parent_path().string() + "': " + createDirectoriesError.message();
                    return false;
                }

                bool headerChanged = false;
                if (!SaveBufferToTextFile(generatedHeaderFile, state.HeaderBuffer, outError, &headerChanged))
                    return false;
                bool sourceChanged = false;
                if (!SaveBufferToTextFile(generatedSourceFile, state.SourceBuffer, outError, &sourceChanged))
                    return false;
                if (outMirroredChange)
                    *outMirroredChange = *outMirroredChange || headerChanged || sourceChanged;
            }
            return true;
        }

        void DrawNativeScriptEditorWindowImpl(NativeScriptAuthoringState& state)
        {
            ConsumeFinishedNativeScriptBuildResult(state, state.EditorWindowOpen);
            DrawNativeScriptBuildToast(state);
            if (!state.EditorWindowOpen)
                return;

            if (state.FocusEditorWindowRequested)
                ImGui::SetNextWindowFocus();
            const bool hasUnsavedChanges = state.HeaderDirty || state.SourceDirty;
            const std::string windowTitle = hasUnsavedChanges
                ? "Native Script Editor*"
                : "Native Script Editor";
            EditorPanelStyle::PushPanelVisualStyle();
            if (!ImGui::Begin(windowTitle.c_str(), &state.EditorWindowOpen))
            {
                state.FocusEditorWindowRequested = false;
                ImGui::End();
                EditorPanelStyle::PopPanelVisualStyle();
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
                bool headerFileChanged = false;
                bool sourceFileChanged = false;
                const bool headerSaved = SaveBufferToTextFile(state.HeaderPath, state.HeaderBuffer, saveError, &headerFileChanged);
                const bool sourceSaved = SaveBufferToTextFile(state.SourcePath, state.SourceBuffer, saveError, &sourceFileChanged);
                if (!(headerSaved && sourceSaved))
                {
                    state.StatusMessage = saveError;
                    state.StatusIsError = true;
                    return false;
                }

                state.HeaderDirty = false;
                state.SourceDirty = false;

                bool mirroredAnyChange = false;
                const bool mirrorSucceeded = MirrorScriptToGeneratedDirectory(state, saveError, &mirroredAnyChange);
                if (!mirrorSucceeded)
                {
                    state.StatusMessage = saveError;
                    state.StatusIsError = true;
                }
                else
                {
                    const bool anySavedChange = headerFileChanged || sourceFileChanged || mirroredAnyChange;
                    if (!anySavedChange)
                    {
                        state.StatusMessage = "No script file changes to save.";
                        state.StatusIsError = false;
                    }
                    else if (!saveError.empty())
                    {
                        state.StatusMessage = "Script files saved. " + saveError + " Build will mirror all scripts.";
                        state.StatusIsError = false;
                    }
                    else
                    {
                        state.StatusMessage = "Script files saved and mirrored to generated build directory.";
                        state.StatusIsError = false;
                    }
                }

                const bool shouldBuildNow =
                    forceBuildAfterSave || (state.AutoBuildAfterSave && (headerFileChanged || sourceFileChanged || mirroredAnyChange));
                if (shouldBuildNow)
                    (void)TriggerNativeScriptsBuild(state);

                return mirrorSucceeded;
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
            ImGui::TextUnformatted("Auto Build On Save");
            ImGui::Checkbox("##AutoBuildOnSave", &state.AutoBuildAfterSave);
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

            if (!state.StatusMessage.empty())
            {
                const ImVec4 statusColor = state.StatusIsError
                    ? ImVec4(1.0f, 0.35f, 0.35f, 1.0f)
                    : ImVec4(0.35f, 1.0f, 0.45f, 1.0f);
                ImGui::TextColored(statusColor, "%s", state.StatusMessage.c_str());
            }

            std::string lastBuildOutputSnapshot;
            {
                std::lock_guard<std::mutex> lock(state.LastBuildOutputMutex);
                lastBuildOutputSnapshot = state.LastBuildOutput;
            }
            if (!lastBuildOutputSnapshot.empty() &&
                ImGui::CollapsingHeader("Last Native Script Build Output", ImGuiTreeNodeFlags_DefaultOpen))
            {
                if (ImGui::Button("Copy Build Output", ImVec2(160.0f, 0.0f)))
                    ImGui::SetClipboardText(lastBuildOutputSnapshot.c_str());

                ImGui::BeginChild("NativeScriptBuildOutput", ImVec2(0.0f, 160.0f), true);
                ImGui::TextUnformatted(lastBuildOutputSnapshot.c_str());
                ImGui::EndChild();
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
            EditorPanelStyle::PopPanelVisualStyle();
        }

    void RestorePendingNativeScriptEditorSession()
    {
        if (!s_HasPendingNativeScriptEditorSessionRestore)
            return;

        auto& state = GetNativeScriptAuthoringState();
        const auto pendingState = s_PendingNativeScriptEditorSessionState;
        s_HasPendingNativeScriptEditorSessionRestore = false;
        s_PendingNativeScriptEditorSessionState = {};
        state.ShowDebugInfo = pendingState.ShowDebugInfo;

        if (pendingState.IsOpen && !pendingState.LastEditedScriptClassName.empty())
        {
            std::string openError;
            if (!OpenNativeScriptEditor(
                pendingState.LastEditedScriptClassName,
                pendingState.LastEditedScriptAssetRelativePath,
                state,
                openError))
            {
                state.StatusMessage = openError;
                state.StatusIsError = true;
                state.EditorWindowOpen = true;
                state.FocusEditorWindowRequested = true;
            }
        }
        else
        {
            state.EditorWindowOpen = false;
        }
    }

    void DrawNativeScriptEditorWindow()
    {
        DrawNativeScriptEditorWindowImpl(GetNativeScriptAuthoringState());
    }

    bool IsNativeScriptBuildInProgress()
    {
        return GetNativeScriptAuthoringState().BuildInProgress.load(std::memory_order_relaxed);
    }

    bool TriggerNativeScriptBuildFromInspector()
    {
        return TriggerNativeScriptsBuild(GetNativeScriptAuthoringState());
    }

    bool HasAnyProjectNativeScriptSourcesForInspector()
    {
        return HasAnyProjectNativeScriptSources();
    }

    std::vector<std::string> DiscoverNativeScriptClassNamesFromProjectAssetsForInspector()
    {
        return DiscoverNativeScriptClassNamesFromProjectAssets();
    }

    std::vector<ProjectNativeScriptInfo> GetAvailableProjectScriptsForInspector()
    {
        return BuildAvailableProjectScripts();
    }

    std::vector<std::string> GetAvailableProjectScriptClassNamesForInspector()
    {
        return BuildAvailableProjectScriptClassNames();
    }

    std::string ResolveRegisteredScriptClassNameForInspector(const std::string& requestedClassName)
    {
        return ResolveRegisteredScriptClassName(requestedClassName);
    }

    bool SynchronizeExposedPropertiesFromScriptForInspector(NativeScriptEntry& nativeScript,
                                                            std::vector<std::string>& outFieldOrder,
                                                            std::string& outError)
    {
        return SynchronizeExposedPropertiesFromScript(nativeScript, outFieldOrder, outError);
    }

    bool GetNativeScriptDebugInfoEnabled()
    {
        return GetNativeScriptAuthoringState().ShowDebugInfo;
    }

    void SetNativeScriptDebugInfoEnabled(bool enabled)
    {
        GetNativeScriptAuthoringState().ShowDebugInfo = enabled;
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
        if (extension != ".h" && extension != ".cpp" && extension != ".cs")
            return false;

        const std::string normalizedKey = assetPath.generic_string();
        constexpr const char* assetsPrefix = "Assets/";
        if (normalizedKey.rfind(assetsPrefix, 0) != 0)
            return false;

        const bool isManagedScript = (extension == ".cs");

        const bool preferHeaderTab = (extension == ".h");
        const bool preferSourceTab = (extension == ".cpp");

        std::filesystem::path relativeWithoutAssets = normalizedKey.substr(std::strlen(assetsPrefix));
        relativeWithoutAssets.replace_extension("");
        const std::string assetRelativePathWithoutExtension = relativeWithoutAssets.generic_string();
        if (assetRelativePathWithoutExtension.empty())
            return false;

        auto& nativeScriptAuthoringState = GetNativeScriptAuthoringState();
        const std::string className = assetPath.stem().string();

        const auto openManagedWithDefaultApplication = [&](const std::string& optionalWarning = {}) -> bool
        {
            const auto resolvedScriptPathResult = Assets::ResolveAssetKeyToPath(normalizedKey);
            if (resolvedScriptPathResult.IsFailure())
            {
                LT_WARN("Managed scripts: failed resolving script asset path '{}' for external open.", normalizedKey);
                return false;
            }

            std::string openError;
            if (!OpenPathInExternalApplication(resolvedScriptPathResult.GetValue(), openError))
            {
                LT_WARN("Managed scripts: {}", openError);
                return false;
            }

            if (!optionalWarning.empty())
                LT_WARN("Managed scripts: {}", optionalWarning);
            LT_INFO("Managed scripts: opened external editor for '{}'.", resolvedScriptPathResult.GetValue().string());
            return true;
        };

        const auto openInternalEditor = [&](const std::string& optionalStatus = {}, bool isError = false) -> bool
        {
            if (isManagedScript)
                return openManagedWithDefaultApplication(optionalStatus);

            std::string openError;
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
            if (!optionalStatus.empty())
            {
                nativeScriptAuthoringState.StatusMessage = optionalStatus;
                nativeScriptAuthoringState.StatusIsError = isError;
            }
            return true;
        };

        std::string scriptEditorMode = Project::ScriptEditorMode::Internal;
        const auto openedProjectRoot = GetOpenedProjectRoot();
        if (openedProjectRoot.has_value())
        {
            const auto buildSettingsResult = Project::LoadBuildSettings(openedProjectRoot.value());
            if (buildSettingsResult.IsSuccess())
            {
                const auto& buildSettings = buildSettingsResult.GetValue();
                scriptEditorMode = NormalizeScriptEditorMode(buildSettings.ScriptEditorMode);
            }
        }

#if defined(LT_PLATFORM_WINDOWS)
        bool useInternalBackend = false;
        if (openedProjectRoot.has_value())
        {
            const auto buildSettingsResult = Project::LoadBuildSettings(openedProjectRoot.value());
            if (buildSettingsResult.IsSuccess())
                useInternalBackend = (buildSettingsResult.GetValue().BuildBackend == Project::BuildBackend::InternalToolchain);
        }
        if (scriptEditorMode == Project::ScriptEditorMode::External)
        {
            if (!openedProjectRoot.has_value())
            {
                return openInternalEditor(
                    isManagedScript
                        ? "External Visual Studio requested, but no project is open. Falling back to the default external application."
                        : "External editor requested, but no project is open. Using built-in editor.",
                    true);
            }

            if (!isManagedScript)
            {
                std::string mirrorError;
                if (nativeScriptAuthoringState.BuildInProgress.load(std::memory_order_relaxed))
                {
                    // Avoid clearing/rebuilding Generated/ScriptCore while an active compile is reading it.
                    LT_WARN("Native scripts: skipping full generated mirror refresh because a build is currently running.");
                }
                else if (!MirrorAllProjectNativeScriptsToGeneratedDirectory(mirrorError))
                {
                    return openInternalEditor(
                        "Could not prepare external script mirror (" + mirrorError + "). Using built-in editor.",
                        true);
                }
            }

            const auto buildRoot = FindEngineWorkspaceRoot();
            if (!buildRoot.has_value())
            {
                return openInternalEditor(
                    isManagedScript
                        ? "External Visual Studio could not locate engine/toolchain root. Falling back to the default external application."
                        : "External editor could not locate engine/toolchain root. Using built-in editor.",
                    true);
            }

            if (useInternalBackend)
            {
                if (!IsInternalToolchainRootCandidate(buildRoot.value()))
                    useInternalBackend = false;
            }
            else if (IsInternalToolchainRootCandidate(buildRoot.value()))
            {
                useInternalBackend = true;
            }

            const auto resolvedScriptPathResult = Assets::ResolveAssetKeyToPath(normalizedKey);
            if (resolvedScriptPathResult.IsFailure())
            {
                return openInternalEditor(
                    isManagedScript
                        ? "Failed resolving script asset path for external Visual Studio launch. Falling back to the default external application."
                        : "Failed resolving script asset path for external editor. Using built-in editor.",
                    true);
            }

            const auto [configuration, platform] = GetBuildConfigurationAndPlatform(openedProjectRoot.value());
            NativeScriptExternalEditor::OpenVisualStudioRequest request;
            request.ProjectRoot = openedProjectRoot.value();
            request.BuildRoot = buildRoot.value();
            if (const auto engineSourceRoot = FindEngineSourceWorkspaceRoot(); engineSourceRoot.has_value())
                request.EngineSourceRoot = engineSourceRoot.value();
            request.TargetScriptPath = resolvedScriptPathResult.GetValue();
            request.Configuration = configuration;
            request.Platform = platform;
            request.UseInternalToolchain = useInternalBackend;

            const auto externalOpenResult = NativeScriptExternalEditor::OpenScriptInVisualStudio(request);
            if (externalOpenResult.Launched)
                return true;

            const std::string warningMessage =
                externalOpenResult.ErrorMessage.empty()
                    ? (isManagedScript ? "External Visual Studio launch failed. Falling back to the default external application."
                                       : "External editor launch failed. Using built-in editor.")
                    : (isManagedScript ? externalOpenResult.ErrorMessage + " Falling back to the default external application."
                                       : externalOpenResult.ErrorMessage + " Falling back to built-in editor.");
            LT_WARN("{}: {}", isManagedScript ? "Managed scripts" : "Native scripts", warningMessage);
            return openInternalEditor(warningMessage, true);
        }
#else
        if (scriptEditorMode == Project::ScriptEditorMode::External)
            return openInternalEditor(
                isManagedScript
                    ? "External Visual Studio mode is only available on Windows. Falling back to the default external application."
                    : "External Visual Studio mode is only available on Windows. Using built-in editor.",
                true);
#endif

        return openInternalEditor();
    }

    bool BuildProjectNativeScripts(std::string* outStatusMessage)
    {
        auto& nativeScriptAuthoringState = GetNativeScriptAuthoringState();
        if (nativeScriptAuthoringState.BuildInProgress.load(std::memory_order_relaxed))
        {
            if (outStatusMessage)
                *outStatusMessage = "Native script build already in progress.";
            return false;
        }

        const bool started = TriggerNativeScriptsBuild(nativeScriptAuthoringState);
        if (outStatusMessage)
        {
            if (!nativeScriptAuthoringState.StatusMessage.empty())
                *outStatusMessage = nativeScriptAuthoringState.StatusMessage;
            else if (started)
                *outStatusMessage = "Building native scripts...";
            else
                *outStatusMessage = "Failed to start native script build.";
        }
        return started;
    }

    bool GetLastNativeScriptBuildFailure(std::string* outStatusMessage)
    {
        auto& state = GetNativeScriptAuthoringState();
        if (!state.HasCompletedBuild || state.LastBuildSucceeded)
            return false;

        if (outStatusMessage)
        {
            if (!state.LastBuildSummary.empty())
                *outStatusMessage = state.LastBuildSummary;
            else
                *outStatusMessage = "Native script build failed.";
        }
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

