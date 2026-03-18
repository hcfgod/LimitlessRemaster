#pragma once

#include <string>

namespace Limitless
{
    namespace EditorInputActionsPanel
    {
        void Draw(bool& isOpen, const std::string& inputActionsAssetKey, bool requestFocus = false);
        void ResetState();
    }
}
