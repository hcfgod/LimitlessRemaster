#pragma once

#include "EditorUndoService.h"

#include <functional>
#include <string>

namespace Limitless
{
    class EditorLambdaCommand final : public IEditorCommand
    {
    public:
        using Callback = std::function<bool()>;

        EditorLambdaCommand(std::string label, Callback undoCallback, Callback redoCallback);

        bool Undo() override;
        bool Redo() override;
        const std::string& GetLabel() const override { return m_Label; }

    private:
        std::string m_Label;
        Callback m_UndoCallback;
        Callback m_RedoCallback;
    };
}
