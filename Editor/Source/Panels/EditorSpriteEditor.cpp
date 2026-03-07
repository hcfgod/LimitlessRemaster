#include "EditorSpriteEditor.h"

#include "EditorPanelStyle.h"
#include "Assets/AssetManager.h"
#include "Assets/SpriteImportSettings.h"
#include "Assets/TextureAssetImporter.h"
#include "Core/Debug/Log.h"
#include "Graphics/Texture.h"
#include "imgui/imgui.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>

namespace Limitless::EditorSpriteEditor
{
    // -------------------------------------------------------------------------
    // Constants
    // -------------------------------------------------------------------------

    static constexpr float kMinZoom = 0.25f;
    static constexpr float kMaxZoom = 16.0f;
    static constexpr float kZoomStep = 0.15f;
    static constexpr float kGridLineAlpha = 0.45f;
    static constexpr float kSubSpriteHighlightAlpha = 0.35f;
    static constexpr ImVec4 kGridLineColor = ImVec4(0.0f, 1.0f, 1.0f, kGridLineAlpha);
    static constexpr ImVec4 kHoveredRectColor = ImVec4(1.0f, 0.85f, 0.0f, kSubSpriteHighlightAlpha);
    static constexpr ImVec4 kSubSpriteRectColor = ImVec4(0.2f, 0.8f, 0.2f, 0.55f);
    static constexpr ImVec4 kCheckerColorA = ImVec4(0.20f, 0.20f, 0.20f, 1.0f);
    static constexpr ImVec4 kCheckerColorB = ImVec4(0.30f, 0.30f, 0.30f, 1.0f);

    // -------------------------------------------------------------------------
    // Helpers
    // -------------------------------------------------------------------------

    static ImVec2 TextureToScreen(const glm::vec2& texCoord,
                                  const ImVec2& canvasOrigin,
                                  const glm::vec2& offset,
                                  float zoom)
    {
        return ImVec2(
            canvasOrigin.x + (texCoord.x + offset.x) * zoom,
            canvasOrigin.y + (texCoord.y + offset.y) * zoom);
    }

    static glm::vec2 ScreenToTexture(const ImVec2& screenPos,
                                     const ImVec2& canvasOrigin,
                                     const glm::vec2& offset,
                                     float zoom)
    {
        return glm::vec2(
            (screenPos.x - canvasOrigin.x) / zoom - offset.x,
            (screenPos.y - canvasOrigin.y) / zoom - offset.y);
    }

    static void DrawCheckerBackground(ImDrawList* drawList,
                                      const ImVec2& pMin, const ImVec2& pMax,
                                      float checkerSize)
    {
        const ImU32 colA = ImGui::ColorConvertFloat4ToU32(kCheckerColorA);
        const ImU32 colB = ImGui::ColorConvertFloat4ToU32(kCheckerColorB);

        const int cols = static_cast<int>(std::ceil((pMax.x - pMin.x) / checkerSize));
        const int rows = static_cast<int>(std::ceil((pMax.y - pMin.y) / checkerSize));

        for (int r = 0; r < rows; ++r)
        {
            for (int c = 0; c < cols; ++c)
            {
                const float x0 = pMin.x + c * checkerSize;
                const float y0 = pMin.y + r * checkerSize;
                const float x1 = std::min(x0 + checkerSize, pMax.x);
                const float y1 = std::min(y0 + checkerSize, pMax.y);
                const ImU32 col = ((r + c) % 2 == 0) ? colA : colB;
                drawList->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), col);
            }
        }
    }

    // -------------------------------------------------------------------------
    // Slice generation
    // -------------------------------------------------------------------------

    static std::vector<Assets::SpriteSubRect> GenerateSliceRects(
        const SpriteEditorState& state,
        uint32_t textureWidth, uint32_t textureHeight)
    {
        std::vector<Assets::SpriteSubRect> results;

        glm::ivec2 cellSize = state.SliceCellSize;
        const glm::ivec2 margin = glm::ivec2(std::max(0, state.SliceMargin.x), std::max(0, state.SliceMargin.y));
        const glm::ivec2 spacing = glm::ivec2(std::max(0, state.SliceSpacing.x), std::max(0, state.SliceSpacing.y));

        if (state.CurrentSliceType == SpriteEditorState::SliceType::GridByCellCount)
        {
            const int cols = std::max(1, state.SliceCellCount.x);
            const int rows = std::max(1, state.SliceCellCount.y);
            const int usableW = static_cast<int>(textureWidth) - 2 * margin.x - (cols - 1) * spacing.x;
            const int usableH = static_cast<int>(textureHeight) - 2 * margin.y - (rows - 1) * spacing.y;
            cellSize = glm::ivec2(std::max(1, usableW / cols), std::max(1, usableH / rows));
        }
        else
        {
            cellSize = glm::ivec2(std::max(1, cellSize.x), std::max(1, cellSize.y));
        }

        const int texW = static_cast<int>(textureWidth);
        const int texH = static_cast<int>(textureHeight);
        int index = 0;

        for (int y = margin.y; y + cellSize.y <= texH; y += cellSize.y + spacing.y)
        {
            for (int x = margin.x; x + cellSize.x <= texW; x += cellSize.x + spacing.x)
            {
                Assets::SpriteSubRect sub;
                const std::string baseName = std::filesystem::path(state.TextureAssetKey).stem().string();
                sub.Name = baseName + "_" + std::to_string(index);
                sub.RectPixels = glm::ivec4(x, y, cellSize.x, cellSize.y);
                results.push_back(std::move(sub));
                ++index;
            }
        }

        return results;
    }

    // -------------------------------------------------------------------------
    // Public API
    // -------------------------------------------------------------------------

    void Open(SpriteEditorState& state, const std::string& textureAssetKey)
    {
        state.Open = true;
        state.TextureAssetKey = textureAssetKey;
        state.CachedTexture.reset();
        state.SettingsLoaded = false;
        state.CanvasOffset = glm::vec2(0.0f);
        state.CanvasZoom = 1.0f;
        state.HoveredSubSpriteIndex = -1;
    }

    void Draw(SpriteEditorState& state)
    {
        if (!state.Open)
            return;

        ImGui::SetNextWindowSize(ImVec2(900, 650), ImGuiCond_FirstUseEver);
        EditorPanelStyle::PushPanelVisualStyle();
        if (!ImGui::Begin("Sprite Editor", &state.Open))
        {
            ImGui::End();
            EditorPanelStyle::PopPanelVisualStyle();
            return;
        }

        // Load texture if needed.
        if (!state.CachedTexture || state.CachedTexture->GetKey() != state.TextureAssetKey)
            state.CachedTexture = Assets::AssetManager::LoadBlocking<Assets::TextureAsset>(state.TextureAssetKey);

        if (!state.CachedTexture || !state.CachedTexture->GetTexture())
        {
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Failed to load texture: %s", state.TextureAssetKey.c_str());
            ImGui::End();
            EditorPanelStyle::PopPanelVisualStyle();
            return;
        }

        // Load import settings once.
        if (!state.SettingsLoaded)
        {
            state.ImportSettings = Assets::LoadSpriteImportSettings(state.TextureAssetKey);
            state.SettingsLoaded = true;

            // Initialize slice cell size from existing sub-sprites if available.
            if (!state.ImportSettings.SubSprites.empty())
            {
                const auto& first = state.ImportSettings.SubSprites[0];
                state.SliceCellSize = glm::ivec2(first.RectPixels.z, first.RectPixels.w);
            }
        }

        const auto* texture = state.CachedTexture->GetTexture().get();
        const uint32_t texW = texture->GetWidth();
        const uint32_t texH = texture->GetHeight();

        // =====================================================================
        // Layout: left sidebar (slice tool) + right canvas
        // =====================================================================

        const float sidebarWidth = 260.0f;

        // --- Sidebar ---
        ImGui::BeginChild("##SpriteEditorSidebar", ImVec2(sidebarWidth, 0), true);
        {
            ImGui::Text("Slice Tool");
            ImGui::Separator();
            ImGui::Spacing();

            const char* sliceTypeNames[] = { "Grid By Cell Size", "Grid By Cell Count" };
            int sliceTypeIndex = static_cast<int>(state.CurrentSliceType);
            if (ImGui::Combo("Type", &sliceTypeIndex, sliceTypeNames, 2))
                state.CurrentSliceType = static_cast<SpriteEditorState::SliceType>(sliceTypeIndex);

            ImGui::Spacing();

            if (state.CurrentSliceType == SpriteEditorState::SliceType::GridByCellSize)
            {
                ImGui::DragInt2("Cell Size", &state.SliceCellSize.x, 1.0f, 1, 4096);
                state.SliceCellSize = glm::max(state.SliceCellSize, glm::ivec2(1));
            }
            else
            {
                ImGui::DragInt2("Cell Count", &state.SliceCellCount.x, 1.0f, 1, 512);
                state.SliceCellCount = glm::max(state.SliceCellCount, glm::ivec2(1));

                // Show computed cell size.
                const int cols = std::max(1, state.SliceCellCount.x);
                const int rows = std::max(1, state.SliceCellCount.y);
                const int margin2x = std::max(0, state.SliceMargin.x) * 2;
                const int margin2y = std::max(0, state.SliceMargin.y) * 2;
                const int spacingTotalX = std::max(0, state.SliceSpacing.x) * (cols - 1);
                const int spacingTotalY = std::max(0, state.SliceSpacing.y) * (rows - 1);
                const int computedW = std::max(1, (static_cast<int>(texW) - margin2x - spacingTotalX) / cols);
                const int computedH = std::max(1, (static_cast<int>(texH) - margin2y - spacingTotalY) / rows);
                ImGui::TextDisabled("Computed: %d x %d px", computedW, computedH);
            }

            ImGui::DragInt2("Margin", &state.SliceMargin.x, 1.0f, 0, 512);
            state.SliceMargin = glm::max(state.SliceMargin, glm::ivec2(0));

            ImGui::DragInt2("Spacing", &state.SliceSpacing.x, 1.0f, 0, 512);
            state.SliceSpacing = glm::max(state.SliceSpacing, glm::ivec2(0));

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::Button("Slice", ImVec2(-1, 0)))
            {
                state.ImportSettings.SubSprites = GenerateSliceRects(state, texW, texH);
            }

            ImGui::Spacing();

            if (!state.ImportSettings.SubSprites.empty())
            {
                ImGui::TextDisabled("%zu sub-sprites", state.ImportSettings.SubSprites.size());
                ImGui::Spacing();

                if (ImGui::Button("Apply", ImVec2(-1, 0)))
                {
                    state.ImportSettings.Mode = Assets::SpriteImportSettings::SpriteMode::Multiple;
                    const auto result = Assets::SaveSpriteImportSettings(state.TextureAssetKey, state.ImportSettings);
                    if (result.IsSuccess())
                        LT_CORE_INFO("Sprite Editor: saved {} sub-sprites for '{}'",
                                     state.ImportSettings.SubSprites.size(), state.TextureAssetKey);
                    else
                        LT_CORE_ERROR("Sprite Editor: failed to save: {}", result.GetError().GetErrorMessage());
                }
                ImGui::SameLine();
                if (ImGui::Button("Clear", ImVec2(-1, 0)))
                {
                    state.ImportSettings.SubSprites.clear();
                }
            }

            // Sub-sprite list.
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Text("Sub-Sprites");
            ImGui::Spacing();

            ImGui::BeginChild("##SubSpriteList", ImVec2(0, 0), false);
            for (size_t i = 0; i < state.ImportSettings.SubSprites.size(); ++i)
            {
                const auto& sub = state.ImportSettings.SubSprites[i];
                const bool isHovered = (state.HoveredSubSpriteIndex == static_cast<int>(i));
                if (isHovered)
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.85f, 0, 1));

                ImGui::Text("[%zu] %s (%d,%d %dx%d)",
                            i, sub.Name.c_str(),
                            sub.RectPixels.x, sub.RectPixels.y,
                            sub.RectPixels.z, sub.RectPixels.w);

                if (isHovered)
                    ImGui::PopStyleColor();
            }
            ImGui::EndChild();
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // --- Canvas ---
        ImGui::BeginChild("##SpriteEditorCanvas", ImVec2(0, 0), true,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        {
            const ImVec2 canvasSize = ImGui::GetContentRegionAvail();
            const ImVec2 canvasPos = ImGui::GetCursorScreenPos();

            // Handle zoom (scroll wheel).
            if (ImGui::IsWindowHovered())
            {
                const float wheel = ImGui::GetIO().MouseWheel;
                if (std::abs(wheel) > 0.01f)
                {
                    const float prevZoom = state.CanvasZoom;
                    state.CanvasZoom *= (1.0f + wheel * kZoomStep);
                    state.CanvasZoom = std::clamp(state.CanvasZoom, kMinZoom, kMaxZoom);

                    // Zoom toward mouse cursor: adjust offset so the point under cursor stays fixed.
                    const ImVec2 mousePos = ImGui::GetIO().MousePos;
                    const glm::vec2 mouseTexBefore = ScreenToTexture(mousePos, canvasPos, state.CanvasOffset, prevZoom);
                    const glm::vec2 mouseTexAfter = ScreenToTexture(mousePos, canvasPos, state.CanvasOffset, state.CanvasZoom);
                    state.CanvasOffset.x += (mouseTexAfter.x - mouseTexBefore.x);
                    state.CanvasOffset.y += (mouseTexAfter.y - mouseTexBefore.y);
                }
            }

            // Handle pan (middle mouse drag or right mouse drag).
            if (ImGui::IsWindowHovered() &&
                (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) || ImGui::IsMouseDragging(ImGuiMouseButton_Right)))
            {
                const ImVec2 delta = ImGui::GetIO().MouseDelta;
                state.CanvasOffset.x += delta.x / state.CanvasZoom;
                state.CanvasOffset.y += delta.y / state.CanvasZoom;
            }

            ImDrawList* drawList = ImGui::GetWindowDrawList();

            // Clip to canvas area.
            drawList->PushClipRect(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), true);

            // Texture rect in screen space.
            const ImVec2 texScreenMin = TextureToScreen(glm::vec2(0, 0), canvasPos, state.CanvasOffset, state.CanvasZoom);
            const ImVec2 texScreenMax = TextureToScreen(
                glm::vec2(static_cast<float>(texW), static_cast<float>(texH)),
                canvasPos, state.CanvasOffset, state.CanvasZoom);

            // Checkerboard background behind texture.
            const float checkerSize = std::max(8.0f, 16.0f * state.CanvasZoom);
            DrawCheckerBackground(drawList, texScreenMin, texScreenMax, checkerSize);

            // Draw the texture (OpenGL UV flip: bottom-left origin).
            drawList->AddImage(
                (ImTextureID)(void*)(uintptr_t)texture->GetRendererID(),
                texScreenMin, texScreenMax,
                ImVec2(0, 1), ImVec2(1, 0));

            // Draw texture border.
            drawList->AddRect(texScreenMin, texScreenMax, IM_COL32(180, 180, 180, 200));

            // Draw sub-sprite rects.
            state.HoveredSubSpriteIndex = -1;
            const ImVec2 mousePos = ImGui::GetIO().MousePos;

            for (size_t i = 0; i < state.ImportSettings.SubSprites.size(); ++i)
            {
                const auto& sub = state.ImportSettings.SubSprites[i];
                const ImVec2 rectMin = TextureToScreen(
                    glm::vec2(sub.RectPixels.x, sub.RectPixels.y),
                    canvasPos, state.CanvasOffset, state.CanvasZoom);
                const ImVec2 rectMax = TextureToScreen(
                    glm::vec2(sub.RectPixels.x + sub.RectPixels.z, sub.RectPixels.y + sub.RectPixels.w),
                    canvasPos, state.CanvasOffset, state.CanvasZoom);

                // Hit test for hover.
                const bool hovered = (mousePos.x >= rectMin.x && mousePos.x <= rectMax.x &&
                                      mousePos.y >= rectMin.y && mousePos.y <= rectMax.y);

                if (hovered)
                {
                    state.HoveredSubSpriteIndex = static_cast<int>(i);
                    drawList->AddRectFilled(rectMin, rectMax, ImGui::ColorConvertFloat4ToU32(kHoveredRectColor));
                }

                drawList->AddRect(rectMin, rectMax, ImGui::ColorConvertFloat4ToU32(kSubSpriteRectColor), 0.0f, 0, 1.5f);
            }

            // Draw grid overlay when no sub-sprites (preview of current slice settings).
            if (state.ImportSettings.SubSprites.empty())
            {
                const auto previewRects = GenerateSliceRects(state, texW, texH);
                const ImU32 gridColor = ImGui::ColorConvertFloat4ToU32(kGridLineColor);

                for (const auto& sub : previewRects)
                {
                    const ImVec2 rectMin = TextureToScreen(
                        glm::vec2(sub.RectPixels.x, sub.RectPixels.y),
                        canvasPos, state.CanvasOffset, state.CanvasZoom);
                    const ImVec2 rectMax = TextureToScreen(
                        glm::vec2(sub.RectPixels.x + sub.RectPixels.z, sub.RectPixels.y + sub.RectPixels.w),
                        canvasPos, state.CanvasOffset, state.CanvasZoom);
                    drawList->AddRect(rectMin, rectMax, gridColor, 0.0f, 0, 1.0f);
                }
            }

            // Tooltip for hovered sub-sprite.
            if (state.HoveredSubSpriteIndex >= 0 &&
                state.HoveredSubSpriteIndex < static_cast<int>(state.ImportSettings.SubSprites.size()))
            {
                const auto& hovered = state.ImportSettings.SubSprites[state.HoveredSubSpriteIndex];
                ImGui::SetTooltip("[%d] %s\n%d x %d at (%d, %d)",
                                  state.HoveredSubSpriteIndex, hovered.Name.c_str(),
                                  hovered.RectPixels.z, hovered.RectPixels.w,
                                  hovered.RectPixels.x, hovered.RectPixels.y);
            }

            drawList->PopClipRect();

            // Zoom info overlay.
            const ImVec2 overlayPos(canvasPos.x + 8, canvasPos.y + canvasSize.y - 22);
            drawList->AddText(overlayPos, IM_COL32(200, 200, 200, 180),
                              (std::to_string(static_cast<int>(state.CanvasZoom * 100.0f)) + "%").c_str());
        }
        ImGui::EndChild();

        ImGui::End();
        EditorPanelStyle::PopPanelVisualStyle();
    }
}
