#include <doctest/doctest.h>

#include "Scene/Components.h"
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
