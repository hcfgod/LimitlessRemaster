#include "EditorViewportPanelShared.h"

#include "Graphics/Camera/Camera.h"
#include "Graphics/Framebuffer.h"
#include "Graphics/NativeRenderHandles.h"
#include "Scene/Scene.h"
#include "Scene/SceneRenderer.h"
#include "imgui/imgui.h"

namespace Limitless::EditorViewportPanel::Internal
{
    void DrawGameViewWindow(ViewportPanelContext& context)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        if (context.ShowGameView)
        {
            if (context.FocusGameViewRequested)
            {
                ImGui::SetNextWindowFocus();
                context.FocusGameViewRequested = false;
            }
            const bool gameWindowVisible = ImGui::Begin("Game View", &context.ShowGameView);

            context.GameViewFocused = ImGui::IsWindowFocused();
            context.GameViewHovered = ImGui::IsWindowHovered();
            const bool skipGameRender = !gameWindowVisible || ImGui::IsWindowCollapsed();
            const ImVec2 gameViewSize = ImGui::GetContentRegionAvail();
            const uint32_t gameWidth = SanitizeViewportDimension(gameViewSize.x);
            const uint32_t gameHeight = SanitizeViewportDimension(gameViewSize.y);

            if (!skipGameRender && gameWidth > 0 && gameHeight > 0)
            {
                context.EnsureGameViewFramebuffer(gameWidth, gameHeight);
                context.GameViewWidthPixels = gameWidth;
                context.GameViewHeightPixels = gameHeight;

                if (context.GameViewCamera)
                    context.GameViewCamera->SetViewportSize(gameWidth, gameHeight);

                const bool isSceneLoading = context.SceneContext && context.SceneContext->GetLoadState() == Scene::LoadState::Loading;
                if (context.GameViewCamera && context.GameViewFramebuffer && !isSceneLoading)
                {
                    if (context.RenderGameView)
                        context.RenderGameView(*context.GameViewCamera, context.GameViewFramebuffer, gameWidth, gameHeight);
                    else if (context.SceneContext)
                        SceneRenderer::RenderToViewport(*context.SceneContext, *context.GameViewCamera, context.GameViewFramebuffer, gameWidth, gameHeight);
                }

                if (context.GameViewFramebuffer && context.GameViewFramebuffer->GetColorAttachment())
                {
                    ImGui::Image(
                        static_cast<ImTextureID>(GetTextureNativeHandle(context.GameViewFramebuffer->GetColorAttachment())),
                        ImVec2(static_cast<float>(gameWidth), static_cast<float>(gameHeight)),
                        ImVec2(0, 1),
                        ImVec2(1, 0));
                    const ImVec2 gameRectMin = ImGui::GetItemRectMin();
                    const ImVec2 gameRectMax = ImGui::GetItemRectMax();
                    context.GameViewRectValid = true;
                    context.GameViewRectMinPixels = glm::vec2(gameRectMin.x, gameRectMin.y);
                    context.GameViewRectMaxPixels = glm::vec2(gameRectMax.x, gameRectMax.y);

                    const ImVec2 minPos = gameRectMin;
                    const ImVec2 maxPos = gameRectMax;
                    SceneRenderer::SetUiInputViewportRectPixels(
                        minPos.x,
                        minPos.y,
                        maxPos.x - minPos.x,
                        maxPos.y - minPos.y,
                        true);
                    const bool gameLoadingToastDrawn = DrawLoadingOverlay(context.SceneContext, minPos, maxPos);
                    if (!gameLoadingToastDrawn && context.ShowMissingGameplayCameraOverlay)
                    {
                        ImDrawList* drawList = ImGui::GetWindowDrawList();
                        drawList->AddRectFilled(minPos, maxPos, IM_COL32(0, 0, 0, 180));

                        const char* text = "Game View: No active gameplay camera.\nAdd a Camera Component to an entity and set it as Primary.";
                        const ImVec2 textSize = ImGui::CalcTextSize(text);
                        const ImVec2 center((minPos.x + maxPos.x) * 0.5f, (minPos.y + maxPos.y) * 0.5f);
                        drawList->AddText(ImVec2(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f), IM_COL32(255, 200, 120, 255), text);
                    }
                }
            }

            ImGui::End();
        }
        else
        {
            context.FocusGameViewRequested = false;
        }
        ImGui::PopStyleVar();
    }
}
