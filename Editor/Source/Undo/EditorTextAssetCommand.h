#pragma once

#include "EditorUndoService.h"

#include <functional>
#include <string>

namespace Limitless
{
    class EditorTextAssetCommand final : public IEditorCommand
    {
    public:
        using ApplyTextCallback = std::function<bool(const std::string&)>;

        EditorTextAssetCommand(std::string label,
                               std::string beforeText,
                               std::string afterText,
                               ApplyTextCallback applyTextCallback);

        bool Undo() override;
        bool Redo() override;
        const std::string& GetLabel() const override { return m_Label; }

    private:
        std::string m_Label;
        std::string m_BeforeText;
        std::string m_AfterText;
        ApplyTextCallback m_ApplyTextCallback;
    };
}
