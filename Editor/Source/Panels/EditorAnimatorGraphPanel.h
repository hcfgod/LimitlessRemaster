#pragma once

#include <string>

namespace Limitless
{
    class EditorUndoService;

    namespace EditorAnimatorGraphPanel
    {
        void Draw(bool& isOpen, const std::string& animatorControllerAssetKey, EditorUndoService* undoService, bool requestFocus = false);
        bool ApplyPendingChanges(EditorUndoService* undoService);
    }
}
