#pragma once

#include "Scene/Scene.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Limitless
{
    class IEditorCommand
    {
    public:
        virtual ~IEditorCommand() = default;
        virtual bool Undo() = 0;
        virtual bool Redo() = 0;
        virtual const std::string& GetLabel() const = 0;
    };

    class EditorUndoStack
    {
    public:
        bool Push(std::unique_ptr<IEditorCommand> command);
        bool Undo();
        bool Redo();
        void Clear();

        bool CanUndo() const;
        bool CanRedo() const;
        const std::string& GetUndoLabel() const;
        const std::string& GetRedoLabel() const;

    private:
        std::vector<std::unique_ptr<IEditorCommand>> m_UndoCommands;
        std::vector<std::unique_ptr<IEditorCommand>> m_RedoCommands;
    };

    class EditorUndoService
    {
    public:
        using SceneGetter = std::function<Scene*()>;
        using SceneSetter = std::function<void(std::unique_ptr<Scene>)>;
        using SceneSwap = std::function<bool(std::unique_ptr<Scene>&)>;

        void Initialize(SceneGetter sceneGetter, SceneSetter sceneSetter, SceneSwap sceneSwap);

        bool ExecuteSceneMutation(const std::string& label, const std::function<bool(Scene&)>& mutator);
        void BeginInteractiveSceneMutation();
        bool CommitInteractiveSceneMutation(const std::string& label);
        void CancelInteractiveSceneMutation();

        bool Undo();
        bool Redo();
        void Clear();
        void MarkSaved();

        bool CanUndo() const { return m_UndoStack.CanUndo(); }
        bool CanRedo() const { return m_UndoStack.CanRedo(); }
        const std::string& GetUndoLabel() const { return m_UndoStack.GetUndoLabel(); }
        const std::string& GetRedoLabel() const { return m_UndoStack.GetRedoLabel(); }
        bool IsDirty() const { return m_IsDirty; }

    private:
        bool PushSnapshotCommand(const std::string& label, std::unique_ptr<Scene> beforeSnapshot);
        std::unique_ptr<Scene> CloneCurrentScene() const;

    private:
        SceneGetter m_SceneGetter;
        SceneSetter m_SceneSetter;
        SceneSwap m_SceneSwap;
        EditorUndoStack m_UndoStack;
        std::unique_ptr<Scene> m_PendingInteractiveSnapshot;
        bool m_IsDirty = false;
    };
}
