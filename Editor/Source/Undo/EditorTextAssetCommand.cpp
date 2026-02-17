#include "PrecompiledHeader.h"

#include "EditorTextAssetCommand.h"

namespace Limitless
{
    EditorTextAssetCommand::EditorTextAssetCommand(std::string label,
                                                   std::string beforeText,
                                                   std::string afterText,
                                                   ApplyTextCallback applyTextCallback)
        : m_Label(std::move(label)),
          m_BeforeText(std::move(beforeText)),
          m_AfterText(std::move(afterText)),
          m_ApplyTextCallback(std::move(applyTextCallback))
    {
    }

    bool EditorTextAssetCommand::Undo()
    {
        if (!m_ApplyTextCallback)
            return false;
        return m_ApplyTextCallback(m_BeforeText);
    }

    bool EditorTextAssetCommand::Redo()
    {
        if (!m_ApplyTextCallback)
            return false;
        return m_ApplyTextCallback(m_AfterText);
    }
}
