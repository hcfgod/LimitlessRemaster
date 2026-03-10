#include <doctest/doctest.h>

#include "Scene/SceneManager.h"

TEST_SUITE("Scene Manager Flows")
{
    TEST_CASE("SceneManager queues load, reload, activate, and unload transitions")
    {
        Limitless::SceneManager::ClearPendingSceneTransition();

        CHECK(Limitless::SceneManager::LoadScene("Assets/Scenes/Gameplay.scene.json", Limitless::LoadSceneMode::Additive));
        CHECK(Limitless::SceneManager::SetActiveScene("Assets/Scenes/Gameplay.scene.json"));
        CHECK(Limitless::SceneManager::ReloadCurrentScene());
        CHECK(Limitless::SceneManager::UnloadScene("Assets/Scenes/Gameplay.scene.json"));
        CHECK(Limitless::SceneManager::HasPendingSceneTransition());

        const auto loadRequest = Limitless::SceneManager::ConsumePendingSceneTransition();
        REQUIRE(loadRequest.has_value());
        CHECK(loadRequest->Type == Limitless::SceneTransitionType::LoadByAssetKey);
        CHECK(loadRequest->SceneIdentifier == "Assets/Scenes/Gameplay.scene.json");
        CHECK(loadRequest->LoadMode == Limitless::LoadSceneMode::Additive);

        const auto activateRequest = Limitless::SceneManager::ConsumePendingSceneTransition();
        REQUIRE(activateRequest.has_value());
        CHECK(activateRequest->Type == Limitless::SceneTransitionType::SetActiveSceneByAssetKey);
        CHECK(activateRequest->SceneIdentifier == "Assets/Scenes/Gameplay.scene.json");

        const auto reloadRequest = Limitless::SceneManager::ConsumePendingSceneTransition();
        REQUIRE(reloadRequest.has_value());
        CHECK(reloadRequest->Type == Limitless::SceneTransitionType::ReloadCurrentScene);
        CHECK(reloadRequest->SceneIdentifier.empty());

        const auto unloadRequest = Limitless::SceneManager::ConsumePendingSceneTransition();
        REQUIRE(unloadRequest.has_value());
        CHECK(unloadRequest->Type == Limitless::SceneTransitionType::UnloadByAssetKey);
        CHECK(unloadRequest->SceneIdentifier == "Assets/Scenes/Gameplay.scene.json");
        CHECK_FALSE(Limitless::SceneManager::HasPendingSceneTransition());

        Limitless::SceneManager::ClearPendingSceneTransition();
    }

    TEST_CASE("SceneManager accepts scene key without file extension")
    {
        Limitless::SceneManager::ClearPendingSceneTransition();

        CHECK(Limitless::SceneManager::LoadScene("Assets/Scenes/MainMenu"));
        const auto request = Limitless::SceneManager::ConsumePendingSceneTransition();
        REQUIRE(request.has_value());
        CHECK(request->Type == Limitless::SceneTransitionType::LoadByAssetKey);
        CHECK(request->SceneIdentifier == "Assets/Scenes/MainMenu.scene.json");
        CHECK(request->LoadMode == Limitless::LoadSceneMode::Single);

        Limitless::SceneManager::ClearPendingSceneTransition();
    }

    TEST_CASE("SceneManager prefixes Assets for relative scene paths")
    {
        Limitless::SceneManager::ClearPendingSceneTransition();

        CHECK(Limitless::SceneManager::LoadScene("Scenes/MainMenu"));
        const auto request = Limitless::SceneManager::ConsumePendingSceneTransition();
        REQUIRE(request.has_value());
        CHECK(request->Type == Limitless::SceneTransitionType::LoadByAssetKey);
        CHECK(request->SceneIdentifier == "Assets/Scenes/MainMenu.scene.json");

        Limitless::SceneManager::ClearPendingSceneTransition();
    }

    TEST_CASE("SceneManager accepts scene name without path")
    {
        Limitless::SceneManager::ClearPendingSceneTransition();
        CHECK(Limitless::SceneManager::LoadScene("MainMenu"));
        const auto request = Limitless::SceneManager::ConsumePendingSceneTransition();
        REQUIRE(request.has_value());
        CHECK(request->Type == Limitless::SceneTransitionType::LoadByAssetKey);
        CHECK(request->SceneIdentifier == "MainMenu");
        CHECK(request->LoadMode == Limitless::LoadSceneMode::Single);

        Limitless::SceneManager::ClearPendingSceneTransition();
    }

    TEST_CASE("SceneManager rejects empty scene key and preserves queued request order")
    {
        Limitless::SceneManager::ClearPendingSceneTransition();

        CHECK_FALSE(Limitless::SceneManager::LoadScene(""));
        CHECK_FALSE(Limitless::SceneManager::HasPendingSceneTransition());

        CHECK(Limitless::SceneManager::LoadScene("Assets/Scenes/A.scene.json", Limitless::LoadSceneMode::Single));
        CHECK(Limitless::SceneManager::LoadScene("Assets/Scenes/B.scene.json", Limitless::LoadSceneMode::Additive));

        const auto firstRequest = Limitless::SceneManager::ConsumePendingSceneTransition();
        REQUIRE(firstRequest.has_value());
        CHECK(firstRequest->Type == Limitless::SceneTransitionType::LoadByAssetKey);
        CHECK(firstRequest->SceneIdentifier == "Assets/Scenes/A.scene.json");
        CHECK(firstRequest->LoadMode == Limitless::LoadSceneMode::Single);

        const auto secondRequest = Limitless::SceneManager::ConsumePendingSceneTransition();
        REQUIRE(secondRequest.has_value());
        CHECK(secondRequest->Type == Limitless::SceneTransitionType::LoadByAssetKey);
        CHECK(secondRequest->SceneIdentifier == "Assets/Scenes/B.scene.json");
        CHECK(secondRequest->LoadMode == Limitless::LoadSceneMode::Additive);
        CHECK_FALSE(Limitless::SceneManager::HasPendingSceneTransition());

        Limitless::SceneManager::ClearPendingSceneTransition();
    }
}
