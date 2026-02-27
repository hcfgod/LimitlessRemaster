#pragma once

#include "Scene/Scene.h"

#include <cstddef>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
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
        void SetMaxUndoCommands(size_t maxCommands);
        size_t GetMaxUndoCommands() const { return m_MaxUndoCommands; }

        bool CanUndo() const;
        bool CanRedo() const;
        const std::string& GetUndoLabel() const;
        const std::string& GetRedoLabel() const;

    private:
        void TrimToMaxUndoCommands();

    private:
        std::vector<std::unique_ptr<IEditorCommand>> m_UndoCommands;
        std::vector<std::unique_ptr<IEditorCommand>> m_RedoCommands;
        size_t m_MaxUndoCommands = 128;
    };

    class EditorUndoService
    {
    public:
        using SceneGetter = std::function<Scene*()>;
        using SceneSetter = std::function<void(std::unique_ptr<Scene>)>;
        using SceneSwap = std::function<bool(std::unique_ptr<Scene>&)>;

        void Initialize(SceneGetter sceneGetter, SceneSetter sceneSetter, SceneSwap sceneSwap);

        bool ExecuteSceneMutation(const std::string& label, const std::function<bool(Scene&)>& mutator);
        bool ExecuteCommand(std::unique_ptr<IEditorCommand> command);
        bool ExecuteLambdaCommand(const std::string& label,
                                  std::function<bool()> undoCallback,
                                  std::function<bool()> redoCallback);
        template<typename TValue>
        bool ExecuteValueMutation(const std::string& label,
                                  const TValue& beforeValue,
                                  const TValue& afterValue,
                                  std::function<bool(const TValue&)> applyValue)
        {
            if (!applyValue)
                return false;

            const bool unchanged = [&]() {
                if constexpr (std::is_trivially_copyable_v<TValue>)
                    return std::memcmp(&beforeValue, &afterValue, sizeof(TValue)) == 0;
                else
                    return beforeValue == afterValue;
            }();
            if (unchanged)
                return false;

            return ExecuteLambdaCommand(
                label,
                [applyValue, beforeValue]() { return applyValue(beforeValue); },
                [applyValue, afterValue]() { return applyValue(afterValue); });
        }
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
        Scene* GetActiveScene() const { return m_SceneGetter ? m_SceneGetter() : nullptr; }
        void SetMaxUndoCommands(size_t maxCommands) { m_UndoStack.SetMaxUndoCommands(maxCommands); }
        size_t GetMaxUndoCommands() const { return m_UndoStack.GetMaxUndoCommands(); }

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
