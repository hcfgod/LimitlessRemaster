#include <doctest/doctest.h>

#include "Scene/Components/CoreComponents.h"
#include "Scene/Scene.h"
#include "Undo/EditorUndoService.h"
#include "Undo/EditorTextAssetCommand.h"

namespace
{
    bool IsNullEntity(entt::entity entity)
    {
        return entity == entt::null;
    }

    entt::entity FindEntityByTag(const Limitless::Scene& scene, const std::string& tag)
    {
        const auto& registry = scene.GetRegistry();
        auto view = registry.view<Limitless::TagComponent>();
        for (entt::entity entity : view)
        {
            if (view.get<Limitless::TagComponent>(entity).Tag == tag)
                return entity;
        }
        return entt::null;
    }
}

TEST_SUITE("Editor Undo Redo")
{
    TEST_CASE("Scene mutation command supports undo and redo")
    {
        auto activeScene = std::make_unique<Limitless::Scene>();
        Limitless::EditorUndoService undoService;
        undoService.Initialize(
            [&]() { return activeScene.get(); },
            [&](std::unique_ptr<Limitless::Scene> scene) { activeScene = std::move(scene); },
            [&](std::unique_ptr<Limitless::Scene>& snapshot) {
                if (!snapshot)
                    return false;
                activeScene.swap(snapshot);
                return (activeScene != nullptr);
            });

        CHECK(undoService.ExecuteSceneMutation("Create Player", [](Limitless::Scene& scene) {
            scene.CreateEntity("Player");
            return true;
        }));

        CHECK(undoService.CanUndo());
        CHECK_FALSE(IsNullEntity(FindEntityByTag(*activeScene, "Player")));

        CHECK(undoService.Undo());
        CHECK(IsNullEntity(FindEntityByTag(*activeScene, "Player")));

        CHECK(undoService.Redo());
        CHECK_FALSE(IsNullEntity(FindEntityByTag(*activeScene, "Player")));
    }

    TEST_CASE("Redo stack is cleared after new command")
    {
        auto activeScene = std::make_unique<Limitless::Scene>();
        Limitless::EditorUndoService undoService;
        undoService.Initialize(
            [&]() { return activeScene.get(); },
            [&](std::unique_ptr<Limitless::Scene> scene) { activeScene = std::move(scene); },
            [&](std::unique_ptr<Limitless::Scene>& snapshot) {
                if (!snapshot)
                    return false;
                activeScene.swap(snapshot);
                return (activeScene != nullptr);
            });

        CHECK(undoService.ExecuteSceneMutation("Create A", [](Limitless::Scene& scene) {
            scene.CreateEntity("A");
            return true;
        }));
        CHECK(undoService.ExecuteSceneMutation("Create B", [](Limitless::Scene& scene) {
            scene.CreateEntity("B");
            return true;
        }));

        CHECK(undoService.Undo());
        CHECK(undoService.CanRedo());

        CHECK(undoService.ExecuteSceneMutation("Create C", [](Limitless::Scene& scene) {
            scene.CreateEntity("C");
            return true;
        }));

        CHECK_FALSE(undoService.CanRedo());
        CHECK_FALSE(IsNullEntity(FindEntityByTag(*activeScene, "A")));
        CHECK(IsNullEntity(FindEntityByTag(*activeScene, "B")));
        CHECK_FALSE(IsNullEntity(FindEntityByTag(*activeScene, "C")));
    }

    TEST_CASE("Undo stack enforces maximum command count")
    {
        auto activeScene = std::make_unique<Limitless::Scene>();
        Limitless::EditorUndoService undoService;
        undoService.Initialize(
            [&]() { return activeScene.get(); },
            [&](std::unique_ptr<Limitless::Scene> scene) { activeScene = std::move(scene); },
            [&](std::unique_ptr<Limitless::Scene>& snapshot) {
                if (!snapshot)
                    return false;
                activeScene.swap(snapshot);
                return (activeScene != nullptr);
            });
        undoService.SetMaxUndoCommands(2);

        CHECK(undoService.ExecuteSceneMutation("Create A", [](Limitless::Scene& scene) {
            scene.CreateEntity("A");
            return true;
        }));
        CHECK(undoService.ExecuteSceneMutation("Create B", [](Limitless::Scene& scene) {
            scene.CreateEntity("B");
            return true;
        }));
        CHECK(undoService.ExecuteSceneMutation("Create C", [](Limitless::Scene& scene) {
            scene.CreateEntity("C");
            return true;
        }));

        CHECK(undoService.Undo());
        CHECK(undoService.Undo());
        CHECK_FALSE(undoService.CanUndo());

        CHECK_FALSE(IsNullEntity(FindEntityByTag(*activeScene, "A")));
        CHECK(IsNullEntity(FindEntityByTag(*activeScene, "B")));
        CHECK(IsNullEntity(FindEntityByTag(*activeScene, "C")));
    }

    TEST_CASE("Value mutation command supports undo and redo")
    {
        auto activeScene = std::make_unique<Limitless::Scene>();
        Limitless::EditorUndoService undoService;
        undoService.Initialize(
            [&]() { return activeScene.get(); },
            [&](std::unique_ptr<Limitless::Scene> scene) { activeScene = std::move(scene); },
            [&](std::unique_ptr<Limitless::Scene>& snapshot) {
                if (!snapshot)
                    return false;
                activeScene.swap(snapshot);
                return (activeScene != nullptr);
            });

        const entt::entity entity = activeScene->CreateEntity("Toggle Entity");
        auto* tag = activeScene->GetRegistry().try_get<Limitless::TagComponent>(entity);
        REQUIRE(tag != nullptr);

        const bool beforeEnabled = tag->Enabled;
        const bool afterEnabled = !beforeEnabled;
        tag->Enabled = afterEnabled;

        CHECK(undoService.ExecuteValueMutation<bool>(
            "Toggle Entity Enabled",
            beforeEnabled,
            afterEnabled,
            [&](const bool& value) {
                auto* mutableTag = activeScene->GetRegistry().try_get<Limitless::TagComponent>(entity);
                if (!mutableTag)
                    return false;
                mutableTag->Enabled = value;
                return true;
            }));

        CHECK(undoService.CanUndo());
        CHECK(undoService.Undo());
        CHECK(tag->Enabled == beforeEnabled);
        CHECK(undoService.Redo());
        CHECK(tag->Enabled == afterEnabled);
    }

    TEST_CASE("Value mutation command skips no-op edits")
    {
        auto activeScene = std::make_unique<Limitless::Scene>();
        Limitless::EditorUndoService undoService;
        undoService.Initialize(
            [&]() { return activeScene.get(); },
            [&](std::unique_ptr<Limitless::Scene> scene) { activeScene = std::move(scene); },
            [&](std::unique_ptr<Limitless::Scene>& snapshot) {
                if (!snapshot)
                    return false;
                activeScene.swap(snapshot);
                return (activeScene != nullptr);
            });

        CHECK_FALSE(undoService.ExecuteValueMutation<int32_t>(
            "No-op",
            42,
            42,
            [](const int32_t&) {
                return true;
            }));
        CHECK_FALSE(undoService.CanUndo());
    }

    TEST_CASE("Value mutation command fails gracefully when target is missing")
    {
        auto activeScene = std::make_unique<Limitless::Scene>();
        Limitless::EditorUndoService undoService;
        undoService.Initialize(
            [&]() { return activeScene.get(); },
            [&](std::unique_ptr<Limitless::Scene> scene) { activeScene = std::move(scene); },
            [&](std::unique_ptr<Limitless::Scene>& snapshot) {
                if (!snapshot)
                    return false;
                activeScene.swap(snapshot);
                return (activeScene != nullptr);
            });

        const entt::entity entity = activeScene->CreateEntity("Transient Entity");
        auto* tag = activeScene->GetRegistry().try_get<Limitless::TagComponent>(entity);
        REQUIRE(tag != nullptr);

        const bool beforeEnabled = tag->Enabled;
        const bool afterEnabled = !beforeEnabled;
        tag->Enabled = afterEnabled;

        CHECK(undoService.ExecuteValueMutation<bool>(
            "Toggle Missing Entity",
            beforeEnabled,
            afterEnabled,
            [&](const bool& value) {
                if (!activeScene->IsValid(entity))
                    return false;
                auto* mutableTag = activeScene->GetRegistry().try_get<Limitless::TagComponent>(entity);
                if (!mutableTag)
                    return false;
                mutableTag->Enabled = value;
                return true;
            }));

        activeScene->DestroyEntity(entity);
        CHECK_FALSE(undoService.Undo());
    }

    TEST_CASE("Text asset command supports undo and redo")
    {
        std::string documentText = "Before";
        auto applyText = [&documentText](const std::string& text) {
            documentText = text;
            return true;
        };

        auto command = std::make_unique<Limitless::EditorTextAssetCommand>(
            "Edit Animation Asset",
            "Before",
            "After",
            applyText);

        Limitless::EditorUndoStack stack;
        REQUIRE(stack.Push(std::move(command)));

        // Command application is performed by the caller before push.
        REQUIRE(applyText("After"));
        CHECK(documentText == "After");

        CHECK(stack.Undo());
        CHECK(documentText == "Before");

        CHECK(stack.Redo());
        CHECK(documentText == "After");
    }
}
