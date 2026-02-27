#include "PrecompiledHeader.h"

#include "EditorUndoService.h"
#include "EditorLambdaCommand.h"

#include "Core/Debug/Log.h"

namespace Limitless
{
    namespace
    {
        const std::string kEmptyLabel;

        class SceneSnapshotCommand final : public IEditorCommand
        {
        public:
            SceneSnapshotCommand(std::string label,
                                 std::unique_ptr<Scene> undoSnapshot,
                                 EditorUndoService::SceneSwap sceneSwap)
                : m_Label(std::move(label)),
                  m_OtherSnapshot(std::move(undoSnapshot)),
                  m_SceneSwap(std::move(sceneSwap))
            {
            }

            bool Undo() override
            {
                if (!m_OtherSnapshot || !m_SceneSwap)
                    return false;
                return m_SceneSwap(m_OtherSnapshot);
            }

            bool Redo() override
            {
                if (!m_OtherSnapshot || !m_SceneSwap)
                    return false;
                return m_SceneSwap(m_OtherSnapshot);
            }

            const std::string& GetLabel() const override
            {
                return m_Label;
            }

        private:
            std::string m_Label;
            std::unique_ptr<Scene> m_OtherSnapshot;
            EditorUndoService::SceneSwap m_SceneSwap;
        };
    }

    bool EditorUndoStack::Push(std::unique_ptr<IEditorCommand> command)
    {
        if (!command)
            return false;
        m_UndoCommands.push_back(std::move(command));
        TrimToMaxUndoCommands();
        m_RedoCommands.clear();
        return true;
    }

    bool EditorUndoStack::Undo()
    {
        if (m_UndoCommands.empty())
            return false;

        std::unique_ptr<IEditorCommand> command = std::move(m_UndoCommands.back());
        m_UndoCommands.pop_back();
        if (!command->Undo())
            return false;

        m_RedoCommands.push_back(std::move(command));
        return true;
    }

    bool EditorUndoStack::Redo()
    {
        if (m_RedoCommands.empty())
            return false;

        std::unique_ptr<IEditorCommand> command = std::move(m_RedoCommands.back());
        m_RedoCommands.pop_back();
        if (!command->Redo())
            return false;

        m_UndoCommands.push_back(std::move(command));
        return true;
    }

    void EditorUndoStack::Clear()
    {
        m_UndoCommands.clear();
        m_RedoCommands.clear();
    }

    void EditorUndoStack::SetMaxUndoCommands(size_t maxCommands)
    {
        m_MaxUndoCommands = maxCommands;
        TrimToMaxUndoCommands();
    }

    bool EditorUndoStack::CanUndo() const
    {
        return !m_UndoCommands.empty();
    }

    bool EditorUndoStack::CanRedo() const
    {
        return !m_RedoCommands.empty();
    }

    const std::string& EditorUndoStack::GetUndoLabel() const
    {
        if (m_UndoCommands.empty())
            return kEmptyLabel;
        return m_UndoCommands.back()->GetLabel();
    }

    const std::string& EditorUndoStack::GetRedoLabel() const
    {
        if (m_RedoCommands.empty())
            return kEmptyLabel;
        return m_RedoCommands.back()->GetLabel();
    }

    void EditorUndoStack::TrimToMaxUndoCommands()
    {
        if (m_MaxUndoCommands == 0)
            return;
        if (m_UndoCommands.size() <= m_MaxUndoCommands)
            return;

        const size_t overflowCount = m_UndoCommands.size() - m_MaxUndoCommands;
        m_UndoCommands.erase(m_UndoCommands.begin(), m_UndoCommands.begin() + static_cast<std::ptrdiff_t>(overflowCount));
    }

    void EditorUndoService::Initialize(SceneGetter sceneGetter, SceneSetter sceneSetter, SceneSwap sceneSwap)
    {
        m_SceneGetter = std::move(sceneGetter);
        m_SceneSetter = std::move(sceneSetter);
        m_SceneSwap = std::move(sceneSwap);
    }

    bool EditorUndoService::ExecuteSceneMutation(const std::string& label, const std::function<bool(Scene&)>& mutator)
    {
        if (!mutator)
            return false;

        auto beforeSnapshot = CloneCurrentScene();
        if (!beforeSnapshot)
            return false;

        Scene* scene = m_SceneGetter ? m_SceneGetter() : nullptr;
        if (!scene)
            return false;

        if (!mutator(*scene))
            return false;

        return PushSnapshotCommand(label, std::move(beforeSnapshot));
    }

    bool EditorUndoService::ExecuteCommand(std::unique_ptr<IEditorCommand> command)
    {
        if (!command)
            return false;
        if (!m_UndoStack.Push(std::move(command)))
            return false;
        m_PendingInteractiveSnapshot.reset();
        m_IsDirty = true;
        return true;
    }

    bool EditorUndoService::ExecuteLambdaCommand(const std::string& label,
                                                 std::function<bool()> undoCallback,
                                                 std::function<bool()> redoCallback)
    {
        if (!undoCallback || !redoCallback)
            return false;
        auto command = std::make_unique<EditorLambdaCommand>(label, std::move(undoCallback), std::move(redoCallback));
        return ExecuteCommand(std::move(command));
    }

    void EditorUndoService::BeginInteractiveSceneMutation()
    {
        if (!m_PendingInteractiveSnapshot)
            m_PendingInteractiveSnapshot = CloneCurrentScene();
    }

    bool EditorUndoService::CommitInteractiveSceneMutation(const std::string& label)
    {
        if (!m_PendingInteractiveSnapshot)
            return false;

        auto beforeSnapshot = std::move(m_PendingInteractiveSnapshot);
        m_PendingInteractiveSnapshot.reset();
        return PushSnapshotCommand(label, std::move(beforeSnapshot));
    }

    void EditorUndoService::CancelInteractiveSceneMutation()
    {
        m_PendingInteractiveSnapshot.reset();
    }

    bool EditorUndoService::Undo()
    {
        const bool result = m_UndoStack.Undo();
        if (result)
            m_IsDirty = true;
        return result;
    }

    bool EditorUndoService::Redo()
    {
        const bool result = m_UndoStack.Redo();
        if (result)
            m_IsDirty = true;
        return result;
    }

    void EditorUndoService::Clear()
    {
        m_UndoStack.Clear();
        m_PendingInteractiveSnapshot.reset();
        m_IsDirty = false;
    }

    void EditorUndoService::MarkSaved()
    {
        m_IsDirty = false;
    }

    bool EditorUndoService::PushSnapshotCommand(const std::string& label, std::unique_ptr<Scene> beforeSnapshot)
    {
        if (!beforeSnapshot || !m_SceneSwap)
            return false;

        auto command = std::make_unique<SceneSnapshotCommand>(
            label,
            std::move(beforeSnapshot),
            m_SceneSwap);

        if (!m_UndoStack.Push(std::move(command)))
            return false;

        m_IsDirty = true;
        return true;
    }

    std::unique_ptr<Scene> EditorUndoService::CloneCurrentScene() const
    {
        if (!m_SceneGetter)
            return nullptr;

        Scene* scene = m_SceneGetter();
        if (!scene)
            return nullptr;

        auto cloneResult = scene->Clone();
        if (!cloneResult)
        {
            LT_WARN("Undo snapshot clone failed.");
            return nullptr;
        }

        return cloneResult;
    }
}
