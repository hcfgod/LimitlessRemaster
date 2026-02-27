#include "PrecompiledHeader.h"

#include "EditorLambdaCommand.h"

namespace Limitless
{
    EditorLambdaCommand::EditorLambdaCommand(std::string label, Callback undoCallback, Callback redoCallback)
        : m_Label(std::move(label)),
          m_UndoCallback(std::move(undoCallback)),
          m_RedoCallback(std::move(redoCallback))
    {
    }

    bool EditorLambdaCommand::Undo()
    {
        if (!m_UndoCallback)
            return false;
        return m_UndoCallback();
    }

    bool EditorLambdaCommand::Redo()
    {
        if (!m_RedoCallback)
            return false;
        return m_RedoCallback();
    }
}
