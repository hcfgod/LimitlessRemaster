#pragma once

#include <string>

namespace Limitless
{
    class EditorUndoService;

    namespace EditorAnimationTimelinePanel
    {
        struct ActivePreview
        {
            std::string ClipAssetKey;
            float PreviewTimeSeconds = 0.0f;
            float ClipDurationSeconds = 1.0f;
            bool IsPlaying = false;
        };

        void Draw(bool& isOpen, const std::string& animationClipAssetKey, EditorUndoService* undoService, bool requestFocus = false);
        bool ApplyPendingChanges(EditorUndoService* undoService);
        bool TryGetActivePreview(ActivePreview& outPreview);
    }
}
