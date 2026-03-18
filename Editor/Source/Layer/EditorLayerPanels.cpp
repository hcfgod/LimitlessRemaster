#include "PrecompiledHeader.h"
#include "EditorLayer.h"

#include "Audio/AudioEngine.h"
#include "Assets/AnimatorControllerAsset.h"
#include "Core/Debug/Log.h"
#include "Core/PerformanceMonitor.h"
#include "EditorAssetNaming.h"
#include "EditorInspectorPanelAssetInspectors.h"
#include "EditorPanelStyle.h"
#include "Graphics/Lighting2DRenderer.h"
#include "Graphics/Renderer.h"
#include "Scene/Components/CoreComponents.h"
#include "Scene/Components/ScriptingComponents.h"
#include "imgui/imgui.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

namespace Limitless
{
    namespace
    {
        constexpr const char* kAssetTexturePayload = "ASSET_TEXTURE";
        constexpr const char* kAssetAudioPayload = "ASSET_AUDIO";
        constexpr const char* kAssetMovePayload = "ASSET_MOVE";
        constexpr const char* kAssetScenePayload = "ASSET_SCENE";
        constexpr const char* kAssetMaterialPayload = "ASSET_MATERIAL";
        constexpr const char* kAssetPrefabPayload = "ASSET_PREFAB";
        constexpr const char* kAssetShaderPayload = "ASSET_SHADER";
        constexpr const char* kAssetFontPayload = "ASSET_FONT";
        constexpr const char* kDefaultSceneFileName = "SampleScene.scene.json";

        bool IsPrefabAssetKey(const std::string& assetKey)
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

        std::string SceneDisplayNameFromFileName(const std::string& fileName)
        {
            return EditorAssetNaming::GetAssetDisplayNameFromFileName(fileName);
        }
    }

    void EditorLayer::DrawPhysicsDiagnosticsPanel()
    {
        if (!m_ShowPhysicsDiagnosticsWindow)
            return;

        EditorPanelStyle::PushPanelVisualStyle();
        if (!ImGui::Begin("Physics 2D Diagnostics", &m_ShowPhysicsDiagnosticsWindow))
        {
            ImGui::End();
            EditorPanelStyle::PopPanelVisualStyle();
            return;
        }

        if (!m_Scene)
        {
            ImGui::TextDisabled("No active scene.");
            ImGui::End();
            EditorPanelStyle::PopPanelVisualStyle();
            return;
        }

        {
            Physics2DWorldSettings scenePhysicsSettings = m_Scene->GetPhysics2DSettings();
            int worldCount = std::clamp<int>(scenePhysicsSettings.WorldCount, 1, 16);
            ImGui::TextDisabled("Scene Physics");
            if (ImGui::SliderInt("World Count", &worldCount, 1, 16))
            {
                scenePhysicsSettings.WorldCount = static_cast<uint16_t>(worldCount);
                m_Scene->SetPhysics2DSettings(scenePhysicsSettings);
                if (m_EditSceneStored && m_EditSceneStored.get() != m_Scene.get())
                {
                    Physics2DWorldSettings editSceneSettings = m_EditSceneStored->GetPhysics2DSettings();
                    editSceneSettings.WorldCount = scenePhysicsSettings.WorldCount;
                    m_EditSceneStored->SetPhysics2DSettings(editSceneSettings);
                }
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("Allocates independent Box2D worlds for this scene. Rigidbody2D Physics World Slot selects the world per body.");
            ImGui::Separator();
        }

        const uint16_t worldCount = std::max<uint16_t>(1, m_Scene->GetPhysics2DWorldCount());
        Physics2DDiagnostics diagnostics{};
        bool hasAnyWorld = false;
        for (uint16_t worldSlot = 0; worldSlot < worldCount; ++worldSlot)
        {
            const Physics2DWorld* physicsWorld = m_Scene->GetPhysics2DWorld(worldSlot);
            if (!physicsWorld)
                continue;
            hasAnyWorld = true;
            const Physics2DDiagnostics& worldDiagnostics = physicsWorld->GetDiagnostics();
            diagnostics.BodyCount += worldDiagnostics.BodyCount;
            diagnostics.AwakeBodyCount += worldDiagnostics.AwakeBodyCount;
            diagnostics.SleepingBodyCount += worldDiagnostics.SleepingBodyCount;
            diagnostics.ContactPairCount += worldDiagnostics.ContactPairCount;
            diagnostics.PenetratingContactPointCount += worldDiagnostics.PenetratingContactPointCount;
            diagnostics.MaxPenetrationDepth = std::max(diagnostics.MaxPenetrationDepth, worldDiagnostics.MaxPenetrationDepth);
        }
        if (hasAnyWorld)
        {
            constexpr float kRecentPeakHoldDurationSeconds = 0.35f;
            const float frameDeltaSeconds = std::max(0.0f, ImGui::GetIO().DeltaTime);

            if (diagnostics.ContactPairCount > 0 || diagnostics.PenetratingContactPointCount > 0 || diagnostics.MaxPenetrationDepth > 0.0f)
            {
                m_PhysicsDiagnosticsRecentPeakContactPairs =
                    std::max(m_PhysicsDiagnosticsRecentPeakContactPairs, diagnostics.ContactPairCount);
                m_PhysicsDiagnosticsRecentPeakPenetratingPoints =
                    std::max(m_PhysicsDiagnosticsRecentPeakPenetratingPoints, diagnostics.PenetratingContactPointCount);
                m_PhysicsDiagnosticsRecentPeakMaxPenetrationDepth =
                    std::max(m_PhysicsDiagnosticsRecentPeakMaxPenetrationDepth, diagnostics.MaxPenetrationDepth);
                m_PhysicsDiagnosticsRecentPeakHoldSeconds = kRecentPeakHoldDurationSeconds;
            }
            else if (m_PhysicsDiagnosticsRecentPeakHoldSeconds > 0.0f)
            {
                m_PhysicsDiagnosticsRecentPeakHoldSeconds =
                    std::max(0.0f, m_PhysicsDiagnosticsRecentPeakHoldSeconds - frameDeltaSeconds);
                if (m_PhysicsDiagnosticsRecentPeakHoldSeconds <= 0.0f)
                {
                    m_PhysicsDiagnosticsRecentPeakContactPairs = diagnostics.ContactPairCount;
                    m_PhysicsDiagnosticsRecentPeakPenetratingPoints = diagnostics.PenetratingContactPointCount;
                    m_PhysicsDiagnosticsRecentPeakMaxPenetrationDepth = diagnostics.MaxPenetrationDepth;
                }
            }

            ImGui::Text("Bodies: %d (Awake: %d, Sleeping: %d)", diagnostics.BodyCount, diagnostics.AwakeBodyCount, diagnostics.SleepingBodyCount);
            ImGui::Text("Contact Pairs: %d", diagnostics.ContactPairCount);
            ImGui::Text("Penetrating Points: %d", diagnostics.PenetratingContactPointCount);
            ImGui::Text("Max Penetration Depth: %.5f", diagnostics.MaxPenetrationDepth);
            ImGui::TextDisabled("Recent Peak (%.2fs): contacts=%d, penetrating=%d, maxDepth=%.5f",
                                kRecentPeakHoldDurationSeconds,
                                m_PhysicsDiagnosticsRecentPeakContactPairs,
                                m_PhysicsDiagnosticsRecentPeakPenetratingPoints,
                                m_PhysicsDiagnosticsRecentPeakMaxPenetrationDepth);
            ImGui::Separator();

            if (m_SelectedEntity != entt::null)
            {
                Physics2DBodyDiagnostics bodyDiagnostics{};
                if (m_Scene->TryGetPhysics2DBodyDiagnostics(m_SelectedEntity, bodyDiagnostics))
                {
                    ImGui::TextDisabled("Selected Body");
                    ImGui::Text("Awake: %s", bodyDiagnostics.IsAwake ? "Yes" : "No");
                    ImGui::Text("Contact Pairs: %d", bodyDiagnostics.ContactPairCount);
                    ImGui::Text("Penetrating Points: %d", bodyDiagnostics.PenetratingContactPointCount);
                    ImGui::Text("Max Penetration Depth: %.5f", bodyDiagnostics.MaxPenetrationDepth);
                }
                else
                {
                    ImGui::TextDisabled("Selected entity has no active Rigidbody2D diagnostics.");
                }
            }
            else
            {
                ImGui::TextDisabled("Select an entity to inspect per-body sleep/contact state.");
            }

            ImGui::TextWrapped("Tip: if contacts and penetration are stable but motion appears jittery, it is usually render sampling rather than solver instability.");
            ImGui::Separator();
        }
        else
        {
            ImGui::TextDisabled("Physics world is not initialized yet.");
            ImGui::Separator();
        }

        const Lighting2DDiagnostics& lightingDiagnostics = Lighting2DRenderer::Default().GetDiagnostics();
        ImGui::TextDisabled("Lighting 2D");
        ImGui::Text("Path Active: %s", lightingDiagnostics.UsingLightingPath ? "Yes" : "No");
        ImGui::Text("Directional Lights: %u", lightingDiagnostics.DirectionalLightsRendered);
        ImGui::Text("Point Lights: %u", lightingDiagnostics.PointLightsRendered);
        ImGui::Text("Shadow Occluders: %u", lightingDiagnostics.ShadowOccluderCount);
        ImGui::Text("Shadow Segments: %u", lightingDiagnostics.ShadowSegmentCount);
        ImGui::Text("CPU Build: %.3f ms | Submit: %.3f ms",
                    lightingDiagnostics.CpuBuildTimeMs,
                    lightingDiagnostics.CpuSubmitTimeMs);
        ImGui::End();
        EditorPanelStyle::PopPanelVisualStyle();
    }

    void EditorLayer::DrawPerformancePanel()
    {
        if (!m_ShowPerformancePanel)
            return;

        EditorPanelStyle::PushPanelVisualStyle();
        if (!ImGui::Begin("Performance", &m_ShowPerformancePanel))
        {
            ImGui::End();
            EditorPanelStyle::PopPanelVisualStyle();
            return;
        }

        auto& monitor = PerformanceMonitor::GetInstance();
        if (!monitor.IsInitialized())
        {
            ImGui::TextDisabled("Performance monitor not initialized.");
            ImGui::End();
            EditorPanelStyle::PopPanelVisualStyle();
            return;
        }

        const PerformanceMetrics metrics = monitor.CollectMetrics();

        ImGui::TextDisabled("Frame");
        ImGui::Text("Frame time: %.2f ms (avg %.2f ms)", metrics.frameTime, metrics.frameTimeAvg);
        ImGui::Text("FPS: %.1f (avg %.1f)", metrics.fps, metrics.fpsAvg);
        ImGui::Text("Frame count: %u", metrics.frameCount);
        ImGui::Separator();

        ImGui::TextDisabled("CPU");
        ImGui::Text("Usage: %.1f%% (avg %.1f%%)", metrics.cpuUsage, metrics.cpuUsageAvg);
        ImGui::Text("Cores: %u", metrics.cpuCoreCount);
        ImGui::Separator();

        ImGui::TextDisabled("GPU");
        ImGui::Text("Memory: %.1f%%", metrics.gpuMemoryUsage);
        if (metrics.gpuMemoryTotalBytes > 0)
        {
            const double usedMB = static_cast<double>(metrics.gpuMemoryUsedBytes) / (1024.0 * 1024.0);
            const double totalMB = static_cast<double>(metrics.gpuMemoryTotalBytes) / (1024.0 * 1024.0);
            ImGui::Text("VRAM: %.1f MB / %.1f MB", usedMB, totalMB);
        }
        else
        {
            ImGui::TextDisabled("VRAM: (OpenGL driver did not report)");
        }
        if (metrics.gpuUsage > 0.0)
            ImGui::Text("Usage: %.1f%%", metrics.gpuUsage);
        if (metrics.gpuTemperature > 0.0)
            ImGui::Text("Temperature: %.0f C", metrics.gpuTemperature);
        const auto resourceStats = Renderer::GetInstance().GetLastFrameResourceQueueStatistics();
        ImGui::Text("Primary queue processed: %u  approx size: %u",
            resourceStats.PrimaryProcessedLastFrame,
            resourceStats.PrimaryApproxSize);
        ImGui::Text("Shared queue processed: %u  approx size: %u",
            resourceStats.SharedProcessedLastFrame,
            resourceStats.SharedApproxSize);
        ImGui::Text("Pending retirements: %u", resourceStats.PendingRetirementCount);
        ImGui::Separator();

        ImGui::TextDisabled("Process memory");
        const double currentMB = static_cast<double>(metrics.currentMemory) / (1024.0 * 1024.0);
        const double peakMB = static_cast<double>(metrics.peakMemory) / (1024.0 * 1024.0);
        ImGui::Text("Current: %.1f MB", currentMB);
        ImGui::Text("Peak: %.1f MB", peakMB);
        ImGui::Text("Allocations: %u", metrics.allocationCount);

        ImGui::End();
        EditorPanelStyle::PopPanelVisualStyle();
    }

    void EditorLayer::DrawConsolePanel()
    {
        if (!m_ShowConsoleWindow)
            return;

        EditorPanelStyle::PushPanelVisualStyle();
        if (!ImGui::Begin("Console", &m_ShowConsoleWindow))
        {
            ImGui::End();
            EditorPanelStyle::PopPanelVisualStyle();
            return;
        }

        const std::vector<LogMessageEntry> messages = Log::GetRecentMessages();
        int scriptInfoCount = 0;
        int scriptWarningCount = 0;
        int scriptErrorCount = 0;
        for (const LogMessageEntry& entry : messages)
        {
            if (!entry.Message.starts_with("[Script]"))
                continue;

            if (entry.Level >= spdlog::level::err)
                ++scriptErrorCount;
            else if (entry.Level == spdlog::level::warn)
                ++scriptWarningCount;
            else
                ++scriptInfoCount;
        }

        if (ImGui::Button("Clear"))
        {
            Log::ClearRecentMessages();
            m_ConsoleSelectedEntryText.clear();
            m_ConsoleSelectedMessageText.clear();
        }
        ImGui::SameLine();
        ImGui::Checkbox("Auto-scroll", &m_ConsoleAutoScroll);
        ImGui::SameLine();
        ImGui::Checkbox("Scripts", &m_ConsoleShowScriptLogs);
        ImGui::SameLine();
        ImGui::Checkbox("Engine", &m_ConsoleShowEngineLogs);
        ImGui::SameLine();
        ImGui::Checkbox("Info", &m_ConsoleShowInfo);
        ImGui::SameLine();
        ImGui::Checkbox("Warnings", &m_ConsoleShowWarnings);
        ImGui::SameLine();
        ImGui::Checkbox("Errors", &m_ConsoleShowErrors);
        ImGui::SameLine();
        ImGui::TextDisabled("Script Severity I:%d W:%d E:%d", scriptInfoCount, scriptWarningCount, scriptErrorCount);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(260.0f);
        ImGui::InputTextWithHint("##ConsoleSearch", "Search logs...", m_ConsoleSearchBuffer.data(), m_ConsoleSearchBuffer.size());
        ImGui::SameLine();
        bool copyVisibleRequested = ImGui::Button("Copy Visible");
        {
            static constexpr int kLogLevelCount = 7;
            static constexpr const char* kLogLevelLabels[] = { "Trace", "Debug", "Info", "Warn", "Error", "Critical", "Off" };
            static constexpr spdlog::level::level_enum kLogLevelValues[] = {
                spdlog::level::trace,
                spdlog::level::debug,
                spdlog::level::info,
                spdlog::level::warn,
                spdlog::level::err,
                spdlog::level::critical,
                spdlog::level::off
            };

            auto levelToIndex = [](spdlog::level::level_enum level) -> int
            {
                for (int index = 0; index < kLogLevelCount; ++index)
                {
                    if (kLogLevelValues[index] == level)
                        return index;
                }
                return 2; // Info
            };

            int coreLevelIndex = levelToIndex(Log::GetCoreLogLevel());
            int appLevelIndex = levelToIndex(Log::GetClientLogLevel());
            const bool hasCoreLogger = static_cast<bool>(Log::GetCoreLogger());

            ImGui::SetNextItemWidth(120.0f);
            ImGui::BeginDisabled(!hasCoreLogger);
            if (ImGui::Combo("Core Level", &coreLevelIndex, kLogLevelLabels, kLogLevelCount))
                Log::SetCoreLogLevel(kLogLevelValues[coreLevelIndex], true);
            ImGui::EndDisabled();
            if (!hasCoreLogger && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("Core logger is disabled in this build configuration.");

            ImGui::SameLine();
            ImGui::SetNextItemWidth(120.0f);
            if (ImGui::Combo("App Level", &appLevelIndex, kLogLevelLabels, kLogLevelCount))
                Log::SetClientLogLevel(kLogLevelValues[appLevelIndex], true);
        }

        const std::string searchText = m_ConsoleSearchBuffer.data();
        const auto shouldDisplayEntry = [&](const LogMessageEntry& entry) -> bool
        {
            const bool isScriptLog = entry.Message.starts_with("[Script]");
            const bool isWarning = entry.Level == spdlog::level::warn;
            const bool isError = entry.Level >= spdlog::level::err;
            const bool isInfo = !isWarning && !isError;

            if ((isScriptLog && !m_ConsoleShowScriptLogs) || (!isScriptLog && !m_ConsoleShowEngineLogs))
                return false;
            if ((isInfo && !m_ConsoleShowInfo) || (isWarning && !m_ConsoleShowWarnings) || (isError && !m_ConsoleShowErrors))
                return false;

            if (!searchText.empty())
            {
                const bool loggerMatch = entry.LoggerName.find(searchText) != std::string::npos;
                const bool messageMatch = entry.Message.find(searchText) != std::string::npos;
                if (!loggerMatch && !messageMatch)
                    return false;
            }

            return true;
        };

        std::vector<const LogMessageEntry*> visibleEntries;
        visibleEntries.reserve(messages.size());
        for (const LogMessageEntry& entry : messages)
        {
            if (shouldDisplayEntry(entry))
                visibleEntries.push_back(&entry);
        }

        if (copyVisibleRequested)
        {
            auto levelToLabel = [](spdlog::level::level_enum level) -> const char*
            {
                if (level >= spdlog::level::critical) return "Critical";
                if (level >= spdlog::level::err) return "Error";
                if (level >= spdlog::level::warn) return "Warning";
                if (level >= spdlog::level::info) return "Info";
                if (level >= spdlog::level::debug) return "Debug";
                return "Trace";
            };

            std::string clipboardText;
            for (const LogMessageEntry* entry : visibleEntries)
            {
                if (!entry)
                    continue;
                clipboardText += "[" + entry->LoggerName + "] ";
                clipboardText += "[" + std::string(levelToLabel(entry->Level)) + "] ";
                clipboardText += entry->Message;
                clipboardText.push_back('\n');
            }
            ImGui::SetClipboardText(clipboardText.c_str());
        }
        ImGui::Separator();

        if (ImGui::BeginChild("ConsoleEntries"))
        {
            const bool shouldScrollToBottom = m_ConsoleAutoScroll && (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 8.0f);
            for (size_t entryIndex = 0; entryIndex < visibleEntries.size(); ++entryIndex)
            {
                const LogMessageEntry* entry = visibleEntries[entryIndex];
                if (!entry)
                    continue;

                const bool isWarning = entry->Level == spdlog::level::warn;
                const bool isError = entry->Level >= spdlog::level::err;
                const std::string lineText = "[" + entry->LoggerName + "] " + entry->Message;
                const bool isSelected = (lineText == m_ConsoleSelectedEntryText);

                const ImVec4 textColor = isError
                    ? ImVec4(1.0f, 0.38f, 0.38f, 1.0f)
                    : (isWarning ? ImVec4(1.0f, 0.85f, 0.35f, 1.0f) : ImGui::GetStyleColorVec4(ImGuiCol_Text));

                ImGui::PushStyleColor(ImGuiCol_Text, textColor);
                const std::string selectableLabel = lineText + "##ConsoleEntry_" + std::to_string(entryIndex);
                if (ImGui::Selectable(selectableLabel.c_str(), isSelected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick))
                {
                    m_ConsoleSelectedEntryText = lineText;
                    m_ConsoleSelectedMessageText = entry->Message;
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                        ImGui::SetClipboardText(entry->Message.c_str());
                }
                if (ImGui::BeginPopupContextItem())
                {
                    if (ImGui::MenuItem("Copy"))
                        ImGui::SetClipboardText(lineText.c_str());
                    if (ImGui::MenuItem("Copy Message"))
                        ImGui::SetClipboardText(entry->Message.c_str());
                    ImGui::EndPopup();
                }
                ImGui::PopStyleColor();
            }

            const ImGuiIO& io = ImGui::GetIO();
            if (!ImGui::IsAnyItemActive() &&
                !m_ConsoleSelectedMessageText.empty() &&
                (io.KeyCtrl || io.KeySuper) &&
                ImGui::IsKeyPressed(ImGuiKey_C, false))
            {
                ImGui::SetClipboardText(m_ConsoleSelectedMessageText.c_str());
            }

            if (shouldScrollToBottom)
                ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();

        ImGui::End();
        EditorPanelStyle::PopPanelVisualStyle();
    }

    void EditorLayer::DrawViewportPanel()
    {
        Camera* sceneViewCamera = m_CameraManager.GetCamera(m_EditorCameraId);
        bool missingGameplayCamera = false;
        Camera* gameViewCamera = ResolveGameViewCamera(m_GameViewWidthPixels, m_GameViewHeightPixels, missingGameplayCamera);

        EditorViewportPanel::Draw(
            m_SceneViewWidthPixels,
            m_SceneViewHeightPixels,
            m_SceneViewFramebuffer,
            m_ShowSceneView,
            m_SceneViewFocused,
            m_SceneViewHovered,
            m_SceneViewRectValid,
            m_SceneViewRectMinPixels,
            m_SceneViewRectMaxPixels,
            m_GameViewWidthPixels,
            m_GameViewHeightPixels,
            m_GameViewFramebuffer,
            m_ShowGameView,
            m_GameViewFocused,
            m_GameViewHovered,
            m_GameViewRectValid,
            m_GameViewRectMinPixels,
            m_GameViewRectMaxPixels,
            m_FocusSceneViewOnPlayExit,
            m_FocusGameViewOnPlayEnter,
            m_EditorCameraController.get(),
            sceneViewCamera,
            gameViewCamera,
            m_Scene.get(),
            [this](Camera& camera, const std::shared_ptr<Framebuffer>& framebuffer, uint32_t width, uint32_t height) {
                RenderLoadedGameScenes(camera, framebuffer, width, height);
            },
            m_PlayModeState,
            [this](uint32_t width, uint32_t height) { EnsureSceneViewFramebuffer(width, height); },
            [this](uint32_t width, uint32_t height) { EnsureGameViewFramebuffer(width, height); },
            kAssetScenePayload,
            [this](const std::string& assetKey) { LoadSceneFromAssetKey(assetKey); },
            kAssetPrefabPayload,
            [this](const std::string& prefabAssetKey, const glm::vec3& worldPosition) {
                (void)InstantiatePrefabAtWorldPosition(prefabAssetKey, worldPosition);
            },
            m_SelectedEntity,
            &m_EditorUndoService,
            kAssetMovePayload,
            kAssetMaterialPayload,
            m_SelectedTextureAssetKey,
            m_CachedTextureAsset,
            m_SelectedMaterialAssetKey,
            m_CachedMaterialAsset,
            m_SelectedNativeScriptAssetKey,
            m_ShowEditorFpsOverlay,
            &m_TilemapEditorState,
            missingGameplayCamera,
            &m_TransformGizmoState,
            &m_ScenePanelState,
            m_ShowGizmoToolbar);
    }

    void EditorLayer::DrawScenePanel()
    {
        if (!m_ShowScenePanel)
            return;

        std::string sceneRootDisplayName = SceneDisplayNameFromFileName(kDefaultSceneFileName);
        if (!m_CurrentSceneAssetKey.empty())
        {
            const std::filesystem::path sceneAssetPath(m_CurrentSceneAssetKey);
            sceneRootDisplayName = SceneDisplayNameFromFileName(sceneAssetPath.filename().string());
            if (IsPrefabAssetKey(m_CurrentSceneAssetKey))
                sceneRootDisplayName = "Prefab: " + sceneRootDisplayName;
        }

        EditorScenePanel::Draw(
            m_Scene.get(),
            m_ShowScenePanel,
            m_ScenePanelState,
            m_SelectedEntity,
            m_SelectedTextureAssetKey,
            m_CachedTextureAsset,
            m_SelectedMaterialAssetKey,
            m_CachedMaterialAsset,
            m_SelectedNativeScriptAssetKey,
            m_SelectedPrefabAssetKey,
            m_SelectedTilesetAssetKey,
            m_SelectedAudioMixerAssetKey,
            m_SelectedInputActionsAssetKey,
            kAssetMovePayload,
            kAssetMaterialPayload,
            kAssetPrefabPayload,
            sceneRootDisplayName,
            &m_EditorUndoService,
            [this](const std::string& prefabAssetKey, entt::entity parentEntity) { return InstantiatePrefabAtParent(prefabAssetKey, parentEntity); },
            [this](entt::entity entity) { return CreatePrefabFromEntity(entity); },
            [this](entt::entity entity) { return ApplyPrefabFromEntity(entity); },
            [this](entt::entity entity) { return RevertPrefabEntity(entity); },
            [this](entt::entity entity) { return UnpackPrefabEntity(entity); });
    }

    void EditorLayer::DrawInspectorPanel()
    {
        if (!m_ShowInspectorPanel)
            return;

        m_InspectorInstanceState.IsOpen = m_ShowInspectorPanel;
        EditorInspectorPanel::DrawInstance(
            "Inspector",
            m_InspectorInstanceState,
            m_Scene.get(),
            m_CurrentSceneAssetKey,
            m_SelectedEntity,
            kAssetTexturePayload,
            m_SelectedTextureAssetKey,
            m_CachedTextureAsset,
            kAssetAudioPayload,
            kAssetMaterialPayload,
            kAssetShaderPayload,
            kAssetFontPayload,
            m_MaterialPreviewCache,
            m_SelectedMaterialAssetKey,
            m_CachedMaterialAsset,
            m_SelectedNativeScriptAssetKey,
            m_SelectedPrefabAssetKey,
            m_SelectedTilesetAssetKey,
            m_SelectedAudioMixerAssetKey,
            m_SelectedInputActionsAssetKey,
            m_SelectedAnimationClipAssetKey,
            m_SelectedAnimatorControllerAssetKey,
            &m_EditorUndoService);
        m_ShowInspectorPanel = m_InspectorInstanceState.IsOpen;

        DrawAdditionalInspectorPanels();
    }

    void EditorLayer::DrawAdditionalInspectorPanels()
    {
        for (auto& additional : m_AdditionalInspectors)
        {
            if (!additional.State.IsOpen)
                continue;

            EditorInspectorPanel::DrawInstance(
                additional.WindowName.c_str(),
                additional.State,
                m_Scene.get(),
                m_CurrentSceneAssetKey,
                m_SelectedEntity,
                kAssetTexturePayload,
                m_SelectedTextureAssetKey,
                m_CachedTextureAsset,
                kAssetAudioPayload,
                kAssetMaterialPayload,
                kAssetShaderPayload,
                kAssetFontPayload,
                m_MaterialPreviewCache,
                m_SelectedMaterialAssetKey,
                m_CachedMaterialAsset,
                m_SelectedNativeScriptAssetKey,
                m_SelectedPrefabAssetKey,
                m_SelectedTilesetAssetKey,
                m_SelectedAudioMixerAssetKey,
                m_SelectedInputActionsAssetKey,
                m_SelectedAnimationClipAssetKey,
                m_SelectedAnimatorControllerAssetKey,
                &m_EditorUndoService);
        }

        // Remove closed instances.
        m_AdditionalInspectors.erase(
            std::remove_if(m_AdditionalInspectors.begin(), m_AdditionalInspectors.end(),
                [](const AdditionalInspectorInstance& instance) { return !instance.State.IsOpen; }),
            m_AdditionalInspectors.end());
    }

    void EditorLayer::SpawnAdditionalInspectorPanel()
    {
        AdditionalInspectorInstance instance;
        instance.WindowName = "Inspector##" + std::to_string(m_NextInspectorInstanceId++);
        instance.State.IsOpen = true;
        m_AdditionalInspectors.push_back(std::move(instance));
    }

    void EditorLayer::SpawnAdditionalProjectPanel()
    {
        AdditionalProjectPanelInstance instance;
        instance.WindowName = "Project##" + std::to_string(m_NextProjectPanelInstanceId++);
        instance.IsOpen = true;
        instance.State.GridScale = m_ProjectPanelState.GridScale;
        m_AdditionalProjectPanels.push_back(std::move(instance));
    }

    void EditorLayer::DrawAnimationTimelinePanel()
    {
        EditorAnimationTimelinePanel::Draw(
            m_ShowAnimationTimelinePanel,
            m_SelectedAnimationClipAssetKey,
            &m_EditorUndoService,
            m_ProjectPanelState.RequestFocusAnimationClipEditor);
    }

    void EditorLayer::ApplyAnimationTimelinePreviewToSelectedEntity()
    {
        if (m_PlayModeState != EditorPlayModeState::Edit || !m_Scene || !m_Scene->IsReady())
            return;

        // Always undo ALL previously-applied additive offsets first.
        // This ensures stale offsets on previously-previewed entities (e.g. after
        // entity selection change or panel close) are cleaned up every frame.
        RestoreAnimationPreviewTransforms();

        if (!m_ShowAnimationTimelinePanel)
            return;

        EditorAnimationTimelinePanel::ActivePreview preview{};
        if (!EditorAnimationTimelinePanel::TryGetActivePreview(preview) || preview.ClipAssetKey.empty())
            return;

        auto& registry = m_Scene->GetRegistry();

        if (m_SelectedEntity != entt::null && m_Scene->IsValid(m_SelectedEntity))
        {
            auto* animator = registry.try_get<AnimatorComponent>(m_SelectedEntity);
            if (animator && animator->Enabled)
            {
                (void)m_Scene->PreviewAnimationClipOnEntity(m_SelectedEntity, preview.ClipAssetKey, preview.PreviewTimeSeconds);
                return;
            }
        }

        auto animatorView = registry.view<AnimatorComponent>();
        for (entt::entity entity : animatorView)
        {
            auto& animator = animatorView.get<AnimatorComponent>(entity);
            if (!animator.Enabled)
                continue;

            if (animator.DefaultClipKey == preview.ClipAssetKey ||
                animator.RuntimeCurrentClipKey == preview.ClipAssetKey)
            {
                (void)m_Scene->PreviewAnimationClipOnEntity(entity, preview.ClipAssetKey, preview.PreviewTimeSeconds);
                continue;
            }

            if (!animator.ControllerKey.empty())
            {
                if (!animator.CachedController)
                    animator.CachedController = Assets::AnimatorControllerAsset::LoadBlocking(animator.ControllerKey);
                if (animator.CachedController)
                {
                    for (const auto& state : animator.CachedController->GetData().States)
                    {
                        if (state.ClipKey == preview.ClipAssetKey)
                        {
                            (void)m_Scene->PreviewAnimationClipOnEntity(entity, preview.ClipAssetKey, preview.PreviewTimeSeconds);
                            break;
                        }
                    }
                }
            }
        }
    }

    void EditorLayer::RestoreAnimationPreviewTransforms()
    {
        if (!m_Scene)
            return;

        auto& registry = m_Scene->GetRegistry();
        auto view = registry.view<AnimatorComponent, TransformComponent>();
        for (entt::entity entity : view)
        {
            auto& animator = view.get<AnimatorComponent>(entity);
            auto& transform = view.get<TransformComponent>(entity);

            const bool hasOffset =
                animator.RuntimeAppliedPositionOffset != glm::vec3(0.0f) ||
                animator.RuntimeAppliedScaleOffset    != glm::vec3(0.0f) ||
                animator.RuntimeAppliedRotationOffset != glm::vec3(0.0f);

            if (!hasOffset)
                continue;

            transform.Position -= animator.RuntimeAppliedPositionOffset;
            transform.Scale    -= animator.RuntimeAppliedScaleOffset;
            transform.Rotation -= animator.RuntimeAppliedRotationOffset;

            animator.RuntimeAppliedPositionOffset = glm::vec3(0.0f);
            animator.RuntimeAppliedScaleOffset    = glm::vec3(0.0f);
            animator.RuntimeAppliedRotationOffset = glm::vec3(0.0f);

            m_Scene->MarkTransformDirty(entity);
        }
    }

    void EditorLayer::DrawAnimatorGraphPanel()
    {
        EditorAnimatorGraphPanel::Draw(
            m_ShowAnimatorGraphPanel,
            m_SelectedAnimatorControllerAssetKey,
            &m_EditorUndoService,
            m_ProjectPanelState.RequestFocusAnimatorControllerEditor);
    }

    void EditorLayer::DrawInputActionsPanel()
    {
        EditorInputActionsPanel::Draw(
            m_ShowInputActionsPanel,
            m_SelectedInputActionsAssetKey,
            m_ProjectPanelState.RequestFocusInputActionsEditor);
    }

    void EditorLayer::DrawSpriteEditorPanel()
    {
        // Poll the inspector for a pending "Open Sprite Editor" request.
        const std::string& pendingKey = EditorInspectorPanel::GetPendingSpriteEditorRequest();
        if (!pendingKey.empty())
        {
            EditorSpriteEditor::Open(m_SpriteEditorState, pendingKey);
            EditorInspectorPanel::ClearPendingSpriteEditorRequest();
        }

        EditorSpriteEditor::Draw(m_SpriteEditorState);
    }

    void EditorLayer::DrawTilePalettePanelFrame()
    {
        EditorTilePalettePanel::DrawTilePalettePanel(
            m_TilePaletteState,
            m_Scene.get(),
            m_SelectedEntity,
            m_TilemapEditorState,
            &m_EditorUndoService);
    }
}

