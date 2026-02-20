#pragma once

#include "Assets/SpriteImportSettings.h"
#include "Assets/TextureAsset.h"

#include <glm/glm.hpp>

#include <string>

namespace Limitless::EditorSpriteEditor
{
    /// Persistent state for the Sprite Editor window.
    struct SpriteEditorState
    {
        bool Open = false;
        std::string TextureAssetKey;
        Assets::TextureAsset::Ptr CachedTexture;
        Assets::SpriteImportSettings ImportSettings;
        bool SettingsLoaded = false;

        // Slice tool parameters.
        enum class SliceType : uint8_t
        {
            GridByCellSize  = 0,
            GridByCellCount = 1
        };

        SliceType CurrentSliceType = SliceType::GridByCellSize;
        glm::ivec2 SliceCellSize = glm::ivec2(16, 16);
        glm::ivec2 SliceCellCount = glm::ivec2(4, 4);
        glm::ivec2 SliceMargin = glm::ivec2(0, 0);
        glm::ivec2 SliceSpacing = glm::ivec2(0, 0);

        // Canvas view state.
        glm::vec2 CanvasOffset = glm::vec2(0.0f);
        float CanvasZoom = 1.0f;
        int HoveredSubSpriteIndex = -1;
    };

    /// Open the Sprite Editor for a specific texture.
    void Open(SpriteEditorState& state, const std::string& textureAssetKey);

    /// Draw the Sprite Editor window. Call every frame from EditorLayer::OnRender.
    void Draw(SpriteEditorState& state);
}
