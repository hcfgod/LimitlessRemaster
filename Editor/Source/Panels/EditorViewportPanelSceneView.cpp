#include "EditorViewportPanelShared.h"

#include "Assets/AssetLoadProgress.h"
#include "Assets/LoadingScreen.h"
#include "Graphics/Camera/Camera.h"
#include "Graphics/Framebuffer.h"
#include "Graphics/NativeRenderHandles.h"
#include "Graphics/Renderer2D.h"
#include "Scene/Scene.h"
#include "Scene/SceneRenderer.h"
#include "imgui/imgui.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

namespace Limitless::EditorViewportPanel::Internal
{
    namespace
    {
        void DrawSceneViewMissingCameraOverlay()
        {
            const ImVec2 minPos = ImGui::GetItemRectMin();
            const ImVec2 maxPos = ImGui::GetItemRectMax();
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(minPos, maxPos, IM_COL32(0, 0, 0, 180));

            const char* text = "Scene View: Editor camera is unavailable.";
            const ImVec2 textSize = ImGui::CalcTextSize(text);
            const ImVec2 center((minPos.x + maxPos.x) * 0.5f, (minPos.y + maxPos.y) * 0.5f);
            drawList->AddText(ImVec2(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f), IM_COL32(255, 200, 120, 255), text);
        }

        void DrawSceneViewFpsOverlay()
        {
            const ImVec2 minPos = ImGui::GetItemRectMin();
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            struct FpsOverlayHistory
            {
                std::array<float, 180> FrameTimesMs{};
                size_t NextIndex = 0;
                size_t SampleCount = 0;
            };
            static FpsOverlayHistory fpsHistory{};

            const float deltaTimeMs = std::max(0.0f, ImGui::GetIO().DeltaTime * 1000.0f);
            if (deltaTimeMs > 0.0f)
            {
                fpsHistory.FrameTimesMs[fpsHistory.NextIndex] = deltaTimeMs;
                fpsHistory.NextIndex = (fpsHistory.NextIndex + 1) % fpsHistory.FrameTimesMs.size();
                fpsHistory.SampleCount = std::min(fpsHistory.SampleCount + 1, fpsHistory.FrameTimesMs.size());
            }

            float minFrameMs = 0.0f;
            float maxFrameMs = 0.0f;
            float avgFrameMs = 0.0f;
            if (fpsHistory.SampleCount > 0)
            {
                minFrameMs = std::numeric_limits<float>::max();
                for (size_t sampleIndex = 0; sampleIndex < fpsHistory.SampleCount; ++sampleIndex)
                {
                    const size_t readIndex =
                        (fpsHistory.NextIndex + fpsHistory.FrameTimesMs.size() - fpsHistory.SampleCount + sampleIndex) %
                        fpsHistory.FrameTimesMs.size();
                    const float sampleMs = fpsHistory.FrameTimesMs[readIndex];
                    minFrameMs = std::min(minFrameMs, sampleMs);
                    maxFrameMs = std::max(maxFrameMs, sampleMs);
                    avgFrameMs += sampleMs;
                }
                avgFrameMs /= static_cast<float>(fpsHistory.SampleCount);
            }

            const float fps = (deltaTimeMs > 0.01f) ? (1000.0f / deltaTimeMs) : ImGui::GetIO().Framerate;
            const ImU32 statusColor = (deltaTimeMs <= 16.67f)
                ? IM_COL32(120, 255, 130, 255)
                : ((deltaTimeMs <= 33.33f) ? IM_COL32(255, 220, 100, 255) : IM_COL32(255, 120, 120, 255));

            const ImVec2 panelMin(minPos.x + 10.0f, minPos.y + 10.0f);
            const ImVec2 panelMax(panelMin.x + 280.0f, panelMin.y + 130.0f);
            drawList->AddRectFilled(panelMin, panelMax, IM_COL32(0, 0, 0, 165), 4.0f);
            drawList->AddRect(panelMin, panelMax, IM_COL32(255, 255, 255, 32), 4.0f);

            char titleBuffer[96]{};
            std::snprintf(titleBuffer, sizeof(titleBuffer), "FPS %d", static_cast<int>(std::round(fps)));
            drawList->AddText(ImVec2(panelMin.x + 8.0f, panelMin.y + 6.0f), statusColor, titleBuffer);

            char frameAvgBuffer[160]{};
            std::snprintf(frameAvgBuffer,
                          sizeof(frameAvgBuffer),
                          "Frame %.2f ms | Avg %.2f ms",
                          deltaTimeMs,
                          avgFrameMs);
            drawList->AddText(ImVec2(panelMin.x + 8.0f, panelMin.y + 24.0f), IM_COL32(215, 230, 255, 255), frameAvgBuffer);

            char minMaxBuffer[160]{};
            std::snprintf(minMaxBuffer,
                          sizeof(minMaxBuffer),
                          "Min %.2f ms | Max %.2f ms",
                          minFrameMs,
                          maxFrameMs);
            drawList->AddText(ImVec2(panelMin.x + 8.0f, panelMin.y + 40.0f), IM_COL32(215, 230, 255, 255), minMaxBuffer);

            const ImVec2 graphMin(panelMin.x + 8.0f, panelMin.y + 58.0f);
            const ImVec2 graphMax(panelMax.x - 8.0f, panelMax.y - 8.0f);
            drawList->AddRectFilled(graphMin, graphMax, IM_COL32(20, 24, 30, 220), 3.0f);
            drawList->AddRect(graphMin, graphMax, IM_COL32(255, 255, 255, 20), 3.0f);

            const float graphHeight = graphMax.y - graphMin.y;
            const float graphWidth = graphMax.x - graphMin.x;
            const float graphMaxMs = std::max(50.0f, maxFrameMs * 1.2f);
            auto msToY = [&](float milliseconds) {
                const float normalized = std::clamp(milliseconds / graphMaxMs, 0.0f, 1.0f);
                return graphMax.y - normalized * graphHeight;
            };

            const float y60 = msToY(16.67f);
            const float y30 = msToY(33.33f);
            drawList->AddLine(ImVec2(graphMin.x, y60), ImVec2(graphMax.x, y60), IM_COL32(110, 255, 120, 70), 1.0f);
            drawList->AddLine(ImVec2(graphMin.x, y30), ImVec2(graphMax.x, y30), IM_COL32(255, 220, 90, 70), 1.0f);

            if (fpsHistory.SampleCount >= 2)
            {
                const size_t baseIndex = (fpsHistory.NextIndex + fpsHistory.FrameTimesMs.size() - fpsHistory.SampleCount) % fpsHistory.FrameTimesMs.size();
                for (size_t pointIndex = 1; pointIndex < fpsHistory.SampleCount; ++pointIndex)
                {
                    const size_t sampleIndexA = (baseIndex + pointIndex - 1) % fpsHistory.FrameTimesMs.size();
                    const size_t sampleIndexB = (baseIndex + pointIndex) % fpsHistory.FrameTimesMs.size();
                    const float sampleA = fpsHistory.FrameTimesMs[sampleIndexA];
                    const float sampleB = fpsHistory.FrameTimesMs[sampleIndexB];

                    const float xA = graphMin.x + (static_cast<float>(pointIndex - 1) / static_cast<float>(fpsHistory.SampleCount - 1)) * graphWidth;
                    const float xB = graphMin.x + (static_cast<float>(pointIndex) / static_cast<float>(fpsHistory.SampleCount - 1)) * graphWidth;
                    const float yA = msToY(sampleA);
                    const float yB = msToY(sampleB);

                    drawList->AddLine(ImVec2(xA, yA), ImVec2(xB, yB), IM_COL32(120, 200, 255, 220), 1.8f);
                }

                drawList->AddCircleFilled(ImVec2(graphMax.x, msToY(deltaTimeMs)), 2.5f, statusColor);
            }
        }
    }

    bool DrawLoadingOverlay(Scene* scene, const ImVec2& minPos, const ImVec2& maxPos)
    {
        const LoadingScreen::Context ctx = LoadingScreen::BuildContext(
            scene, Renderer2D::Default().IsShaderReady(), Renderer2D::GetDefaultShaderKey());
        const LoadingScreen::State state = LoadingScreen::GetState(ctx);

        static bool s_LoadingSessionActive = false;
        static bool s_LoadingToastVisible = false;
        static double s_LoadingVisibleWindowStartTime = 0.0;
        static double s_LoadingToastHideDeadline = 0.0;
        static std::string s_LastLoadingStatusText = "Loading...";
        static float s_LastLoadingProgressValue = 0.0f;
        constexpr double kLoadingToastDelaySeconds = 0.2;
        constexpr double kLoadingToastHoldSeconds = 0.8;
        const double nowSeconds = ImGui::GetTime();

        if (state.IsLoading)
        {
            if (!s_LoadingSessionActive)
            {
                s_LoadingSessionActive = true;
                s_LoadingVisibleWindowStartTime = nowSeconds;
                s_LoadingToastVisible = false;
            }

            s_LastLoadingStatusText = state.StatusText.empty() ? "Loading..." : state.StatusText;
            s_LastLoadingProgressValue = std::clamp(state.Progress, 0.0f, 1.0f);

            if (!s_LoadingToastVisible &&
                (nowSeconds - s_LoadingVisibleWindowStartTime) >= kLoadingToastDelaySeconds)
            {
                s_LoadingToastVisible = true;
            }
        }
        else
        {
            if (s_LoadingSessionActive)
            {
                s_LoadingSessionActive = false;
                s_LoadingToastVisible = true;
                std::string completionStatus = s_LastLoadingStatusText;
                if (completionStatus.empty())
                    completionStatus = "Loading";
                if (completionStatus.size() >= 3 &&
                    completionStatus.substr(completionStatus.size() - 3) == "...")
                {
                    completionStatus = completionStatus.substr(0, completionStatus.size() - 3);
                }
                s_LastLoadingStatusText = completionStatus + " complete";
                s_LastLoadingProgressValue = 1.0f;
                s_LoadingToastHideDeadline = nowSeconds + kLoadingToastHoldSeconds;
            }

            if (s_LoadingToastVisible && nowSeconds >= s_LoadingToastHideDeadline)
            {
                s_LoadingToastVisible = false;
            }
        }

        if (!s_LoadingToastVisible)
            return false;

        ImDrawList* drawList = ImGui::GetWindowDrawList();

        const char* loadingText = s_LastLoadingStatusText.c_str();
        const float progressValue = std::clamp(s_LastLoadingProgressValue, 0.0f, 1.0f);
        const ImVec2 textSize = ImGui::CalcTextSize(loadingText);

        const float margin = 12.0f;
        const float panelWidth = std::max(260.0f, textSize.x + 28.0f);
        const float panelHeight = 54.0f;

        ImVec2 panelMin(maxPos.x - panelWidth - margin, maxPos.y - panelHeight - margin);
        panelMin.x = std::max(panelMin.x, minPos.x + margin);
        panelMin.y = std::max(panelMin.y, minPos.y + margin);
        ImVec2 panelMax(panelMin.x + panelWidth, panelMin.y + panelHeight);
        panelMax.x = std::min(panelMax.x, maxPos.x - margin);
        panelMax.y = std::min(panelMax.y, maxPos.y - margin);

        drawList->AddRectFilled(panelMin, panelMax, IM_COL32(24, 24, 28, 215), 6.0f);
        drawList->AddRect(panelMin, panelMax, IM_COL32(255, 255, 255, 32), 6.0f);

        drawList->AddText(ImVec2(panelMin.x + 12.0f, panelMin.y + 9.0f),
                          IM_COL32(235, 235, 240, 255),
                          loadingText);

        const float barHeight = 6.0f;
        const ImVec2 barMin(panelMin.x + 12.0f, panelMax.y - 14.0f);
        const ImVec2 barMax(panelMax.x - 12.0f, barMin.y + barHeight);
        drawList->AddRectFilled(barMin, barMax, IM_COL32(58, 58, 64, 230), 3.0f);
        const ImVec2 fillMax(barMin.x + (barMax.x - barMin.x) * progressValue, barMax.y);
        drawList->AddRectFilled(barMin, fillMax, IM_COL32(95, 160, 245, 255), 3.0f);
        return true;
    }

    void DrawSceneViewWindow(ViewportPanelContext& context)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        if (context.ShowSceneView)
        {
            if (context.FocusSceneViewRequested)
            {
                ImGui::SetNextWindowFocus();
                context.FocusSceneViewRequested = false;
            }
            const bool sceneWindowVisible = ImGui::Begin("Scene View", &context.ShowSceneView);

            context.SceneViewFocused = ImGui::IsWindowFocused();
            context.SceneViewHovered = ImGui::IsWindowHovered();
            const bool skipSceneRender = !sceneWindowVisible || ImGui::IsWindowCollapsed();
            const ImVec2 sceneViewSize = ImGui::GetContentRegionAvail();
            const uint32_t sceneWidth = SanitizeViewportDimension(sceneViewSize.x);
            const uint32_t sceneHeight = SanitizeViewportDimension(sceneViewSize.y);

            if (!skipSceneRender && sceneWidth > 0 && sceneHeight > 0)
            {
                context.EnsureSceneViewFramebuffer(sceneWidth, sceneHeight);
                context.SceneViewWidthPixels = sceneWidth;
                context.SceneViewHeightPixels = sceneHeight;

                if (context.SceneViewCamera)
                    context.SceneViewCamera->SetViewportSize(sceneWidth, sceneHeight);

                const bool isSceneLoading = context.SceneContext && context.SceneContext->GetLoadState() == Scene::LoadState::Loading;
                const uint32_t previousActiveCullingMask = SceneRenderer::GetActiveCullingMask();
                SceneRenderer::SetActiveCullingMask(~0u);
                if (context.SceneViewCamera && context.SceneContext && context.SceneViewFramebuffer && !isSceneLoading)
                    SceneRenderer::RenderToViewport(*context.SceneContext, *context.SceneViewCamera, context.SceneViewFramebuffer, sceneWidth, sceneHeight);
                SceneRenderer::SetActiveCullingMask(previousActiveCullingMask);

                if (context.SceneViewFramebuffer && context.SceneViewFramebuffer->GetColorAttachment())
                {
                    ImGui::Image(
                        static_cast<ImTextureID>(GetTextureNativeHandle(context.SceneViewFramebuffer->GetColorAttachment())),
                        ImVec2(static_cast<float>(sceneWidth), static_cast<float>(sceneHeight)),
                        ImVec2(0, 1),
                        ImVec2(1, 0));
                    const ImVec2 sceneRectMin = ImGui::GetItemRectMin();
                    const ImVec2 sceneRectMax = ImGui::GetItemRectMax();
                    context.SceneViewRectValid = true;
                    context.SceneViewRectMinPixels = glm::vec2(sceneRectMin.x, sceneRectMin.y);
                    context.SceneViewRectMaxPixels = glm::vec2(sceneRectMax.x, sceneRectMax.y);

                    if (context.SceneContext && context.SceneViewCamera && !isSceneLoading)
                    {
                        const ImVec2 viewportMin = sceneRectMin;
                        const ImVec2 viewportMax = sceneRectMax;
                        ImDrawList* drawList = ImGui::GetWindowDrawList();
                        const bool physicsOverlayCapturedInput = DrawSelectedPhysicsOverlays(drawList,
                                                                                             *context.SceneContext,
                                                                                             *context.SceneViewCamera,
                                                                                             context.SelectedEntity,
                                                                                             viewportMin,
                                                                                             viewportMax,
                                                                                             static_cast<float>(sceneWidth),
                                                                                             static_cast<float>(sceneHeight),
                                                                                             context.PlayModeState,
                                                                                             context.UndoService);
                        bool tilemapCapturedInput = false;
                        if (context.TilemapState)
                        {
                            auto& reg = context.SceneContext->GetRegistry();
                            entt::entity gridEntity = context.TilemapState->ActiveGridEntity;
                            entt::entity layerEntity = context.TilemapState->ActiveLayerEntity;

                            const bool preferredTargetsValid =
                                gridEntity != entt::null &&
                                layerEntity != entt::null &&
                                context.SceneContext->IsValid(gridEntity) &&
                                context.SceneContext->IsValid(layerEntity) &&
                                reg.all_of<Grid2DComponent>(gridEntity) &&
                                reg.all_of<TilemapLayerComponent>(layerEntity);

                            if (!preferredTargetsValid && reg.all_of<Grid2DComponent>(context.SelectedEntity))
                            {
                                gridEntity = context.SelectedEntity;
                                for (entt::entity child : context.SceneContext->GetChildren(context.SelectedEntity))
                                {
                                    if (reg.all_of<TilemapLayerComponent>(child))
                                    {
                                        layerEntity = child;
                                        break;
                                    }
                                }
                            }
                            else if (!preferredTargetsValid && reg.all_of<TilemapLayerComponent>(context.SelectedEntity))
                            {
                                layerEntity = context.SelectedEntity;
                                entt::entity parent = context.SceneContext->GetParent(context.SelectedEntity);
                                if (parent != entt::null && context.SceneContext->IsValid(parent) && reg.all_of<Grid2DComponent>(parent))
                                    gridEntity = parent;
                            }

                            if (gridEntity != entt::null && layerEntity != entt::null)
                            {
                                tilemapCapturedInput = DrawAndHandleGrid2DEditing(drawList,
                                    *context.SceneContext,
                                    *context.SceneViewCamera,
                                    gridEntity,
                                    layerEntity,
                                    viewportMin,
                                    viewportMax,
                                    static_cast<float>(sceneWidth),
                                    static_cast<float>(sceneHeight),
                                    context.PlayModeState,
                                    context.UndoService,
                                    *context.TilemapState,
                                    std::string{});
                            }
                        }

                        if (tilemapCapturedInput && context.GizmoState)
                            context.GizmoState->BoxSelectActive = false;

                        if (context.ScenePanelState)
                        {
                            for (entt::entity entity : context.ScenePanelState->MultiSelectedEntities)
                            {
                                const ImU32 highlightColor = (entity == context.SelectedEntity)
                                    ? IM_COL32(255, 180, 50, 220)
                                    : IM_COL32(100, 180, 255, 180);
                                DrawSelectionHighlight(drawList, *context.SceneContext, *context.SceneViewCamera, entity,
                                                       viewportMin, static_cast<float>(sceneWidth), static_cast<float>(sceneHeight),
                                                       highlightColor);
                            }
                        }
                        else if (context.SelectedEntity != entt::null)
                        {
                            DrawSelectionHighlight(drawList, *context.SceneContext, *context.SceneViewCamera, context.SelectedEntity,
                                                   viewportMin, static_cast<float>(sceneWidth), static_cast<float>(sceneHeight),
                                                   IM_COL32(255, 180, 50, 220));
                        }

                        bool gizmoCapturedInput = physicsOverlayCapturedInput;
                        if (context.GizmoState && !physicsOverlayCapturedInput)
                        {
                            static const std::vector<entt::entity> kEmptyEntities;
                            const std::vector<entt::entity>& multiEntities = context.ScenePanelState
                                ? context.ScenePanelState->MultiSelectedEntities
                                : kEmptyEntities;
                            gizmoCapturedInput = DrawAndHandleTransformGizmos(drawList,
                                *context.SceneContext,
                                *context.SceneViewCamera,
                                context.SelectedEntity,
                                multiEntities,
                                viewportMin,
                                viewportMax,
                                static_cast<float>(sceneWidth),
                                static_cast<float>(sceneHeight),
                                context.PlayModeState,
                                context.UndoService,
                                *context.GizmoState) || gizmoCapturedInput;
                        }

                        if (!gizmoCapturedInput && !tilemapCapturedInput && context.SceneContext && context.SceneViewCamera)
                        {
                            HandleSceneViewPicking(*context.SceneContext, *context.SceneViewCamera, context.SelectedEntity, context.ScenePanelState,
                                                   viewportMin, viewportMax,
                                                   static_cast<float>(sceneWidth), static_cast<float>(sceneHeight),
                                                   context.SceneViewHovered, context.GizmoState);
                        }

                        if (!gizmoCapturedInput && !tilemapCapturedInput && context.SceneContext && context.SceneViewCamera)
                        {
                            HandleBoxSelection(drawList, *context.SceneContext, *context.SceneViewCamera, context.SelectedEntity, context.ScenePanelState,
                                               viewportMin, viewportMax,
                                               static_cast<float>(sceneWidth), static_cast<float>(sceneHeight),
                                               context.SceneViewHovered, context.GizmoState);
                        }

                        if (context.GizmoState && context.ShowGizmoToolbar)
                        {
                            const ImVec2 toolbarViewportMin = context.ShowFpsOverlay
                                ? ImVec2(viewportMin.x, viewportMin.y + 145.0f)
                                : viewportMin;
                            DrawGizmoToolbar(drawList, toolbarViewportMin, viewportMax, *context.GizmoState);
                        }
                    }

                    if (context.GizmoState)
                        HandleGizmoKeyboardShortcuts(*context.GizmoState, context.SceneViewFocused || context.SceneViewHovered);

                    HandleSceneViewDragDrop(context, sceneRectMin, sceneRectMax);

                    const bool sceneLoadingToastDrawn = DrawLoadingOverlay(context.SceneContext, ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
                    if (!sceneLoadingToastDrawn && !context.SceneViewCamera)
                        DrawSceneViewMissingCameraOverlay();

                    if (context.ShowFpsOverlay)
                        DrawSceneViewFpsOverlay();
                }
            }

            ImGui::End();

            if (!context.PendingDroppedSceneAssetKey.empty() && context.OnSceneDropped)
            {
                context.OnSceneDropped(context.PendingDroppedSceneAssetKey);
                context.SceneContext = nullptr;
            }
        }
        else
        {
            context.FocusSceneViewRequested = false;
        }
        ImGui::PopStyleVar();
    }
}
