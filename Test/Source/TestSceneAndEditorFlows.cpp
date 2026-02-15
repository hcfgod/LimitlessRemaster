#include <doctest/doctest.h>

#include "Scene/Scene.h"
#include "Scene/SceneManager.h"

#include <filesystem>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

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

    std::filesystem::path MakeTempScenePath(const std::string& fileName)
    {
        return std::filesystem::temp_directory_path() / "LimitlessRemasterTests" / fileName;
    }
}

TEST_SUITE("Scene And Editor Flows")
{
    TEST_CASE("SceneManager queues load and reload transitions")
    {
        Limitless::SceneManager::ClearPendingSceneTransition();

        CHECK(Limitless::SceneManager::LoadScene("Assets/Scenes/Gameplay.scene.json"));
        CHECK(Limitless::SceneManager::HasPendingSceneTransition());

        const auto loadRequest = Limitless::SceneManager::ConsumePendingSceneTransition();
        REQUIRE(loadRequest.has_value());
        CHECK(loadRequest->Type == Limitless::SceneTransitionType::LoadByAssetKey);
        CHECK(loadRequest->SceneIdentifier == "Assets/Scenes/Gameplay.scene.json");
        CHECK_FALSE(Limitless::SceneManager::HasPendingSceneTransition());

        CHECK(Limitless::SceneManager::ReloadCurrentScene());
        const auto reloadRequest = Limitless::SceneManager::ConsumePendingSceneTransition();
        REQUIRE(reloadRequest.has_value());
        CHECK(reloadRequest->Type == Limitless::SceneTransitionType::ReloadCurrentScene);
        CHECK(reloadRequest->SceneIdentifier.empty());

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

        Limitless::SceneManager::ClearPendingSceneTransition();
    }

    TEST_CASE("SceneManager rejects empty scene key and latest request wins")
    {
        Limitless::SceneManager::ClearPendingSceneTransition();

        CHECK_FALSE(Limitless::SceneManager::LoadScene(""));
        CHECK_FALSE(Limitless::SceneManager::HasPendingSceneTransition());

        CHECK(Limitless::SceneManager::LoadScene("Assets/Scenes/A.scene.json"));
        CHECK(Limitless::SceneManager::LoadScene("Assets/Scenes/B.scene.json"));

        const auto request = Limitless::SceneManager::ConsumePendingSceneTransition();
        REQUIRE(request.has_value());
        CHECK(request->Type == Limitless::SceneTransitionType::LoadByAssetKey);
        CHECK(request->SceneIdentifier == "Assets/Scenes/B.scene.json");

        Limitless::SceneManager::ClearPendingSceneTransition();
    }

    TEST_CASE("Scene clone preserves authored data and resets runtime state")
    {
        Limitless::Scene scene;
        auto& registry = scene.GetRegistry();

        const entt::entity parent = scene.CreateEntity("Parent");
        const entt::entity child = scene.CreateEntity("Child");
        CHECK(scene.SetParent(child, parent));

        auto& parentTransform = registry.get<Limitless::TransformComponent>(parent);
        parentTransform.Position = { 3.0f, 4.0f, 5.0f };
        parentTransform.Rotation = { 10.0f, 20.0f, 30.0f };
        parentTransform.Scale = { 2.0f, 2.0f, 1.0f };

        auto& childTransform = registry.get<Limitless::TransformComponent>(child);
        childTransform.Position = { -2.0f, 8.0f, 1.0f };

        auto& sprite = registry.emplace<Limitless::SpriteComponent>(child);
        sprite.TextureKey = "Assets/Textures/Ui/Button.png";
        sprite.TextureLoadAttempted = true; // runtime-only behavior should reset in clone
        sprite.Color = { 0.25f, 0.5f, 0.75f, 1.0f };

        auto& material = registry.emplace<Limitless::MaterialComponent>(child);
        material.MaterialKey = "Assets/Materials/Ui/Button.material.json";
        material.MaterialLoadAttempted = true; // runtime-only behavior should reset in clone

        auto& text = registry.emplace<Limitless::TextComponent>(child);
        text.Text = "Play";
        text.FontFilePath = "Assets/Fonts/Inter-Regular.ttf";
        text.FontSize = 42.0f;
        text.Color = { 1.0f, 0.9f, 0.2f, 1.0f };
        text.Space = Limitless::TextComponent::RenderSpace::Screen;
        text.Anchor = Limitless::TextComponent::ScreenAnchor::TopRight;
        text.FontLoadAttempted = true; // runtime-only behavior should reset in clone

        auto& camera = registry.emplace<Limitless::CameraComponent>(parent);
        camera.Projection = Limitless::CameraComponent::ProjectionType::Perspective3D;
        camera.IsPrimary = false;
        camera.FieldOfViewYDegrees = 75.0f;
        camera.NearPlane = 0.1f;
        camera.FarPlane = 3000.0f;

        auto& audio = registry.emplace<Limitless::AudioSourceComponent>(child);
        audio.AudioClipKey = "Assets/Audio/Ui/Click.wav";
        audio.Volume = 0.6f;
        audio.PlayOnStart = false;
        audio.Loop = true;
        audio.Muted = true;
        audio.RuntimeVoiceId = 99;
        audio.RuntimePlaybackStarted = true;

        auto& scripts = registry.emplace<Limitless::NativeScriptComponent>(child);
        scripts.Scripts.emplace_back();
        scripts.Scripts[0].ScriptClassName = "ButtonScript";
        scripts.Scripts[0].ScriptAssetRelativePath = "Gameplay/Ui/ButtonScript";
        scripts.Scripts[0].Enabled = true;
        scripts.Scripts[0].RuntimeInitialized = true;
        scripts.Scripts[0].RuntimeUpdateCount = 123;

        auto clone = scene.Clone();
        REQUIRE(clone != nullptr);

        const entt::entity clonedParent = FindEntityByTag(*clone, "Parent");
        const entt::entity clonedChild = FindEntityByTag(*clone, "Child");
        REQUIRE_FALSE(IsNullEntity(clonedParent));
        REQUIRE_FALSE(IsNullEntity(clonedChild));

        const auto& cloneRegistry = clone->GetRegistry();
        REQUIRE(cloneRegistry.all_of<Limitless::TransformComponent>(clonedChild));
        REQUIRE(cloneRegistry.all_of<Limitless::SpriteComponent>(clonedChild));
        REQUIRE(cloneRegistry.all_of<Limitless::MaterialComponent>(clonedChild));
        REQUIRE(cloneRegistry.all_of<Limitless::TextComponent>(clonedChild));
        REQUIRE(cloneRegistry.all_of<Limitless::AudioSourceComponent>(clonedChild));
        REQUIRE(cloneRegistry.all_of<Limitless::NativeScriptComponent>(clonedChild));
        REQUIRE(cloneRegistry.all_of<Limitless::CameraComponent>(clonedParent));

        const auto& clonedSprite = cloneRegistry.get<Limitless::SpriteComponent>(clonedChild);
        CHECK(clonedSprite.TextureKey == sprite.TextureKey);
        CHECK(clonedSprite.TextureLoadAttempted == false);

        const auto& clonedMaterial = cloneRegistry.get<Limitless::MaterialComponent>(clonedChild);
        CHECK(clonedMaterial.MaterialKey == material.MaterialKey);
        CHECK(clonedMaterial.MaterialLoadAttempted == false);

        const auto& clonedText = cloneRegistry.get<Limitless::TextComponent>(clonedChild);
        CHECK(clonedText.Text == text.Text);
        CHECK(clonedText.FontFilePath == text.FontFilePath);
        CHECK(clonedText.FontSize == doctest::Approx(text.FontSize));
        CHECK(clonedText.Space == Limitless::TextComponent::RenderSpace::Screen);
        CHECK(clonedText.Anchor == Limitless::TextComponent::ScreenAnchor::TopRight);
        CHECK(clonedText.FontLoadAttempted == false);

        const auto& clonedAudio = cloneRegistry.get<Limitless::AudioSourceComponent>(clonedChild);
        CHECK(clonedAudio.AudioClipKey == audio.AudioClipKey);
        CHECK(clonedAudio.Volume == doctest::Approx(audio.Volume));
        CHECK(clonedAudio.RuntimeVoiceId == 0);
        CHECK(clonedAudio.RuntimePlaybackStarted == false);

        const auto& clonedScripts = cloneRegistry.get<Limitless::NativeScriptComponent>(clonedChild);
        REQUIRE(clonedScripts.Scripts.size() == 1);
        CHECK(clonedScripts.Scripts[0].ScriptClassName == "ButtonScript");
        CHECK(clonedScripts.Scripts[0].RuntimeInitialized == false);
        CHECK(clonedScripts.Scripts[0].RuntimeUpdateCount == 0);

        CHECK(clone->GetParent(clonedChild) == clonedParent);
    }

    TEST_CASE("Scene save and load round-trips key scene content")
    {
        Limitless::Scene scene;
        auto& registry = scene.GetRegistry();

        const entt::entity root = scene.CreateEntity("Root");
        const entt::entity hud = scene.CreateEntity("HudLabel");
        CHECK(scene.SetParent(hud, root));

        Limitless::Scene::EditorCameraBookmark bookmark{};
        bookmark.Position = { 8.0f, 9.0f, 10.0f };
        bookmark.YawDegrees = -45.0f;
        bookmark.PitchDegrees = 15.0f;
        scene.SetEditorCameraBookmark(bookmark);

        auto& rootTransform = registry.get<Limitless::TransformComponent>(root);
        rootTransform.Position = { 11.0f, 12.0f, 13.0f };

        auto& text = registry.emplace<Limitless::TextComponent>(hud);
        text.Text = "Score: 999";
        text.FontFilePath = "Assets/Fonts/ScoreFont.ttf";
        text.FontSize = 24.0f;
        text.Space = Limitless::TextComponent::RenderSpace::Screen;
        text.Anchor = Limitless::TextComponent::ScreenAnchor::BottomCenter;

        auto& sprite = registry.emplace<Limitless::SpriteComponent>(root);
        sprite.TextureKey = "Assets/Textures/Backgrounds/Stage01.png";

        auto& prefabInstance = registry.emplace<Limitless::PrefabInstanceComponent>(root);
        prefabInstance.PrefabAssetKey = "Assets/Prefabs/Ui/Hud.prefab.json";

        const std::filesystem::path scenePath = MakeTempScenePath("SceneRoundTrip.scene.json");
        const auto saveResult = scene.SaveToFile(scenePath);
        REQUIRE(saveResult.IsSuccess());

        std::ifstream sceneFile(scenePath, std::ios::binary);
        REQUIRE(sceneFile.is_open());
        nlohmann::json rootJson;
        sceneFile >> rootJson;
        REQUIRE(rootJson.is_object());
        CHECK(rootJson.value("Version", -1) == 6);

        const auto loadResult = Limitless::Scene::LoadFromFile(scenePath);
        REQUIRE(loadResult.IsSuccess());
        REQUIRE(loadResult.GetValue() != nullptr);
        const auto& loadedScene = *loadResult.GetValue();
        const auto& loadedRegistry = loadedScene.GetRegistry();

        const entt::entity loadedRoot = FindEntityByTag(loadedScene, "Root");
        const entt::entity loadedHud = FindEntityByTag(loadedScene, "HudLabel");
        REQUIRE_FALSE(IsNullEntity(loadedRoot));
        REQUIRE_FALSE(IsNullEntity(loadedHud));

        CHECK(loadedScene.GetParent(loadedHud) == loadedRoot);

        const auto& loadedText = loadedRegistry.get<Limitless::TextComponent>(loadedHud);
        CHECK(loadedText.Text == "Score: 999");
        CHECK(loadedText.FontFilePath == "Assets/Fonts/ScoreFont.ttf");
        CHECK(loadedText.Space == Limitless::TextComponent::RenderSpace::Screen);
        CHECK(loadedText.Anchor == Limitless::TextComponent::ScreenAnchor::BottomCenter);

        REQUIRE(loadedRegistry.all_of<Limitless::PrefabInstanceComponent>(loadedRoot));
        const auto& loadedPrefabInstance = loadedRegistry.get<Limitless::PrefabInstanceComponent>(loadedRoot);
        CHECK(loadedPrefabInstance.PrefabAssetKey == "Assets/Prefabs/Ui/Hud.prefab.json");

        REQUIRE(loadedScene.GetEditorCameraBookmark().has_value());
        CHECK(loadedScene.GetEditorCameraBookmark()->YawDegrees == doctest::Approx(-45.0f));
        CHECK(loadedScene.GetEditorCameraBookmark()->PitchDegrees == doctest::Approx(15.0f));

        std::error_code errorCode;
        std::filesystem::remove(scenePath, errorCode);
        std::filesystem::remove(std::filesystem::path("Assets/Prefabs/Ui/Hud.prefab.json.meta"), errorCode);
    }

    TEST_CASE("Editor-like flow create entity add component and save scene")
    {
        // Mirrors common editor operations in sequence:
        // create entity, add components through inspector, save scene, load scene.
        Limitless::Scene scene;
        auto& registry = scene.GetRegistry();

        const entt::entity player = scene.CreateEntity("Player");
        REQUIRE_FALSE(IsNullEntity(player));
        REQUIRE(registry.all_of<Limitless::TagComponent, Limitless::TransformComponent>(player));

        auto& transform = registry.get<Limitless::TransformComponent>(player);
        transform.Position = { 1.0f, 2.0f, 3.0f };
        transform.Scale = { 1.5f, 1.5f, 1.0f };

        auto& sprite = registry.emplace<Limitless::SpriteComponent>(player);
        sprite.TextureKey = "Assets/Textures/Characters/Player.png";

        auto& text = registry.emplace<Limitless::TextComponent>(player);
        text.Text = "Player";
        text.Space = Limitless::TextComponent::RenderSpace::World;
        text.Anchor = Limitless::TextComponent::ScreenAnchor::Center;

        const std::filesystem::path scenePath = MakeTempScenePath("EditorEntityFlow.scene.json");
        const auto saveResult = scene.SaveToFile(scenePath);
        REQUIRE(saveResult.IsSuccess());

        const auto loadResult = Limitless::Scene::LoadFromFile(scenePath);
        REQUIRE(loadResult.IsSuccess());
        REQUIRE(loadResult.GetValue() != nullptr);

        const auto& loadedScene = *loadResult.GetValue();
        const auto& loadedRegistry = loadedScene.GetRegistry();
        const entt::entity loadedPlayer = FindEntityByTag(loadedScene, "Player");
        REQUIRE_FALSE(IsNullEntity(loadedPlayer));
        REQUIRE(loadedRegistry.all_of<Limitless::SpriteComponent, Limitless::TextComponent>(loadedPlayer));

        const auto& loadedTransform = loadedRegistry.get<Limitless::TransformComponent>(loadedPlayer);
        CHECK(loadedTransform.Position.x == doctest::Approx(1.0f));
        CHECK(loadedTransform.Position.y == doctest::Approx(2.0f));
        CHECK(loadedTransform.Position.z == doctest::Approx(3.0f));

        const auto& loadedSprite = loadedRegistry.get<Limitless::SpriteComponent>(loadedPlayer);
        CHECK(loadedSprite.TextureKey == "Assets/Textures/Characters/Player.png");

        const auto& loadedText = loadedRegistry.get<Limitless::TextComponent>(loadedPlayer);
        CHECK(loadedText.Text == "Player");
        CHECK(loadedText.Space == Limitless::TextComponent::RenderSpace::World);
        CHECK(loadedText.Anchor == Limitless::TextComponent::ScreenAnchor::Center);

        std::error_code errorCode;
        std::filesystem::remove(scenePath, errorCode);
    }
}
