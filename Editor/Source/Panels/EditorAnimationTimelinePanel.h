#pragma once

#include <string>

namespace Limitless
{
    class EditorUndoService;

    namespace EditorAnimationTimelinePanel
    {
        void Draw(bool& isOpen, const std::string& animationClipAssetKey, EditorUndoService* undoService);
        bool ApplyPendingChanges(EditorUndoService* undoService);
    }
}
