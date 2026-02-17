#include <doctest/doctest.h>

#include "Project/ProjectSettings.h"
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

    std::filesystem::path MakeTempProjectRoot(const std::string& folderName)
    {
        return std::filesystem::temp_directory_path() / "LimitlessRemasterTests" / folderName;
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

        auto& tilemap = registry.emplace<Limitless::TilemapComponent>(child);
        tilemap.GridSize = { 4, 3 };
        tilemap.ResizeGrid(tilemap.GridSize);
        tilemap.CellSize = { 1.0f, 1.0f };
        tilemap.TilesetTileSizePixels = { 16, 16 };
        tilemap.TilesetTextureKey = "Assets/Textures/Tiles/Dungeon.png";
        tilemap.TilesetTextureLoadAttempted = true;
        tilemap.Layers[0].Tiles[0] = 1;
        tilemap.Layers[0].Tiles[1] = 2;
        tilemap.Layers[0].PerTileData[1] = 77;

        auto& camera = registry.emplace<Limitless::CameraComponent>(parent);
        camera.Projection = Limitless::CameraComponent::ProjectionType::Perspective3D;
        camera.IsPrimary = false;
        camera.FieldOfViewYDegrees = 75.0f;
        camera.NearPlane = 0.1f;
        camera.FarPlane = 3000.0f;

        auto& listener = registry.emplace<Limitless::AudioListener2DComponent>(parent);
        listener.Enabled = true;
        listener.UsePrimaryCameraPosition = false;

        auto& directionalLight = registry.emplace<Limitless::DirectionalLight2DComponent>(child);
        directionalLight.RuntimeResolvedDirection = { 0.4f, 0.9f };

        auto& pointLight = registry.emplace<Limitless::PointLight2DComponent>(child);
        pointLight.RuntimeViewportPosition = { 100.0f, 200.0f };
        pointLight.RuntimeViewportRadius = 32.0f;

        auto& shadowOccluder = registry.emplace<Limitless::ShadowOccluder2DComponent>(child);
        shadowOccluder.RuntimeResolvedPolygonPoints = { {0.0f, 0.0f}, {1.0f, 0.0f}, {0.5f, 1.0f} };
        shadowOccluder.RuntimeGeometryRevision = 7;

        auto& rigidbody = registry.emplace<Limitless::Rigidbody2DComponent>(child);
        rigidbody.RuntimeBodyCreated = true;
        rigidbody.RuntimePreviousPosition = { 1.0f, 2.0f };
        rigidbody.RuntimePreviousAngleRadians = 0.25f;
        rigidbody.RuntimeRenderPreviousPosition = { 3.0f, 4.0f };
        rigidbody.RuntimeRenderPreviousAngleRadians = 0.5f;
        rigidbody.RuntimeRenderCurrentPosition = { 5.0f, 6.0f };
        rigidbody.RuntimeRenderCurrentAngleRadians = 0.75f;

        auto& boxCollider = registry.emplace<Limitless::BoxCollider2DComponent>(child);
        boxCollider.RuntimeShapeCreated = true;

        auto& circleCollider = registry.emplace<Limitless::CircleCollider2DComponent>(child);
        circleCollider.RuntimeShapeCreated = true;

        auto& joint = registry.emplace<Limitless::Joint2DComponent>(child);
        joint.RuntimeJointCreated = true;
        joint.ConnectedEntity = parent;

        auto& audio = registry.emplace<Limitless::AudioSourceComponent>(child);
        audio.AudioClipKey = "Assets/Audio/Ui/Click.wav";
        audio.Volume = 0.6f;
        audio.PlayOnStart = false;
        audio.Loop = true;
        audio.Muted = true;
        audio.Space = Limitless::AudioSourceComponent::PlaybackSpace::Spatial2D;
        audio.MixerGroup = "UI";
        audio.SpatialMinDistance = 0.75f;
        audio.SpatialMaxDistance = 8.25f;
        audio.SpatialRolloffExponent = 2.0f;
        audio.StereoPanStrength = 0.65f;
        audio.AttenuationCurveKey = "Assets/Audio/Curves/UiNearToFar.curve.json";
        audio.RuntimeVoiceId = 99;
        audio.RuntimePlaybackStarted = true;

        auto& scripts = registry.emplace<Limitless::NativeScriptComponent>(child);
        scripts.Scripts.emplace_back();
        scripts.Scripts[0].ScriptClassName = "ButtonScript";
        scripts.Scripts[0].ScriptAssetRelativePath = "Gameplay/Ui/ButtonScript";
        scripts.Scripts[0].Enabled = true;
        scripts.Scripts[0].RuntimeInitialized = true;
        scripts.Scripts[0].RuntimeUpdateCount = 123;
        scripts.Scripts[0].ExposedProperties["FollowTarget"] = Limitless::ScriptEntityReference{ "Parent" };

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
        REQUIRE(cloneRegistry.all_of<Limitless::DirectionalLight2DComponent>(clonedChild));
        REQUIRE(cloneRegistry.all_of<Limitless::PointLight2DComponent>(clonedChild));
        REQUIRE(cloneRegistry.all_of<Limitless::ShadowOccluder2DComponent>(clonedChild));
        REQUIRE(cloneRegistry.all_of<Limitless::Rigidbody2DComponent>(clonedChild));
        REQUIRE(cloneRegistry.all_of<Limitless::BoxCollider2DComponent>(clonedChild));
        REQUIRE(cloneRegistry.all_of<Limitless::CircleCollider2DComponent>(clonedChild));
        REQUIRE(cloneRegistry.all_of<Limitless::Joint2DComponent>(clonedChild));
        REQUIRE(cloneRegistry.all_of<Limitless::TilemapComponent>(clonedChild));
        REQUIRE(cloneRegistry.all_of<Limitless::CameraComponent>(clonedParent));
        REQUIRE(cloneRegistry.all_of<Limitless::AudioListener2DComponent>(clonedParent));

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

        const auto& clonedTilemap = cloneRegistry.get<Limitless::TilemapComponent>(clonedChild);
        CHECK(clonedTilemap.GridSize.x == 4);
        CHECK(clonedTilemap.GridSize.y == 3);
        CHECK(clonedTilemap.TilesetTextureKey == tilemap.TilesetTextureKey);
        CHECK(clonedTilemap.TilesetTextureLoadAttempted == false);
        REQUIRE(clonedTilemap.Layers.size() >= 1);
        CHECK(clonedTilemap.Layers[0].Tiles[0] == 1);
        CHECK(clonedTilemap.Layers[0].Tiles[1] == 2);
        CHECK(clonedTilemap.Layers[0].PerTileData[1] == 77);

        const auto& clonedAudio = cloneRegistry.get<Limitless::AudioSourceComponent>(clonedChild);
        CHECK(clonedAudio.AudioClipKey == audio.AudioClipKey);
        CHECK(clonedAudio.Volume == doctest::Approx(audio.Volume));
        CHECK(clonedAudio.Space == Limitless::AudioSourceComponent::PlaybackSpace::Spatial2D);
        CHECK(clonedAudio.MixerGroup == "UI");
        CHECK(clonedAudio.SpatialMinDistance == doctest::Approx(0.75f));
        CHECK(clonedAudio.SpatialMaxDistance == doctest::Approx(8.25f));
        CHECK(clonedAudio.SpatialRolloffExponent == doctest::Approx(2.0f));
        CHECK(clonedAudio.StereoPanStrength == doctest::Approx(0.65f));
        CHECK(clonedAudio.AttenuationCurveKey == "Assets/Audio/Curves/UiNearToFar.curve.json");
        CHECK(clonedAudio.RuntimeVoiceId == 0);
        CHECK(clonedAudio.RuntimePlaybackStarted == false);

        const auto& clonedListener = cloneRegistry.get<Limitless::AudioListener2DComponent>(clonedParent);
        CHECK(clonedListener.Enabled == true);
        CHECK(clonedListener.UsePrimaryCameraPosition == false);

        const auto& clonedScripts = cloneRegistry.get<Limitless::NativeScriptComponent>(clonedChild);
        REQUIRE(clonedScripts.Scripts.size() == 1);
        CHECK(clonedScripts.Scripts[0].ScriptClassName == "ButtonScript");
        REQUIRE(clonedScripts.Scripts[0].ExposedProperties.contains("FollowTarget"));
        const auto* clonedFollowTarget = std::get_if<Limitless::ScriptEntityReference>(&clonedScripts.Scripts[0].ExposedProperties.at("FollowTarget"));
        REQUIRE(clonedFollowTarget != nullptr);
        CHECK(clonedFollowTarget->Tag == "Parent");
        CHECK(clonedScripts.Scripts[0].RuntimeInitialized == false);
        CHECK(clonedScripts.Scripts[0].RuntimeUpdateCount == 0);

        const auto& clonedDirectionalLight = cloneRegistry.get<Limitless::DirectionalLight2DComponent>(clonedChild);
        CHECK(clonedDirectionalLight.RuntimeResolvedDirection.x == doctest::Approx(0.0f));
        CHECK(clonedDirectionalLight.RuntimeResolvedDirection.y == doctest::Approx(-1.0f));

        const auto& clonedPointLight = cloneRegistry.get<Limitless::PointLight2DComponent>(clonedChild);
        CHECK(clonedPointLight.RuntimeViewportPosition.x == doctest::Approx(0.0f));
        CHECK(clonedPointLight.RuntimeViewportPosition.y == doctest::Approx(0.0f));
        CHECK(clonedPointLight.RuntimeViewportRadius == doctest::Approx(0.0f));

        const auto& clonedShadowOccluder = cloneRegistry.get<Limitless::ShadowOccluder2DComponent>(clonedChild);
        CHECK(clonedShadowOccluder.RuntimeResolvedPolygonPoints.empty());
        CHECK(clonedShadowOccluder.RuntimeGeometryRevision == 0);

        const auto& clonedRigidbody = cloneRegistry.get<Limitless::Rigidbody2DComponent>(clonedChild);
        CHECK(clonedRigidbody.RuntimeBodyCreated == false);
        CHECK(clonedRigidbody.RuntimePreviousPosition.x == doctest::Approx(0.0f));
        CHECK(clonedRigidbody.RuntimePreviousPosition.y == doctest::Approx(0.0f));
        CHECK(clonedRigidbody.RuntimePreviousAngleRadians == doctest::Approx(0.0f));
        CHECK(clonedRigidbody.RuntimeRenderPreviousPosition.x == doctest::Approx(0.0f));
        CHECK(clonedRigidbody.RuntimeRenderPreviousPosition.y == doctest::Approx(0.0f));
        CHECK(clonedRigidbody.RuntimeRenderPreviousAngleRadians == doctest::Approx(0.0f));
        CHECK(clonedRigidbody.RuntimeRenderCurrentPosition.x == doctest::Approx(0.0f));
        CHECK(clonedRigidbody.RuntimeRenderCurrentPosition.y == doctest::Approx(0.0f));
        CHECK(clonedRigidbody.RuntimeRenderCurrentAngleRadians == doctest::Approx(0.0f));

        const auto& clonedBoxCollider = cloneRegistry.get<Limitless::BoxCollider2DComponent>(clonedChild);
        CHECK(clonedBoxCollider.RuntimeShapeCreated == false);

        const auto& clonedCircleCollider = cloneRegistry.get<Limitless::CircleCollider2DComponent>(clonedChild);
        CHECK(clonedCircleCollider.RuntimeShapeCreated == false);

        const auto& clonedJoint = cloneRegistry.get<Limitless::Joint2DComponent>(clonedChild);
        CHECK(clonedJoint.RuntimeJointCreated == false);
        CHECK(clonedJoint.ConnectedEntity == clonedParent);

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

        auto& tilemap = registry.emplace<Limitless::TilemapComponent>(root);
        tilemap.GridSize = { 3, 2 };
        tilemap.ResizeGrid(tilemap.GridSize);
        tilemap.CellSize = { 0.5f, 0.5f };
        tilemap.TilesetTileSizePixels = { 8, 8 };
        tilemap.TilesetTextureKey = "Assets/Textures/Tiles/CityTiles.png";
        tilemap.AutoTileEnabled = true;
        tilemap.Layers[0].Tiles[0] = 1;
        tilemap.Layers[0].Tiles[1] = 2;
        tilemap.Layers[0].PerTileData[0] = 3;
        tilemap.Layers[1].Tiles[3] = 9;
        tilemap.Layers[1].CollisionEnabled = true;
        auto& tilemapCollider2D = registry.emplace<Limitless::TilemapCollider2DComponent>(root);
        tilemapCollider2D.Enabled = true;
        tilemapCollider2D.MergeAdjacentTiles = true;
        tilemapCollider2D.UseCollisionEnabledLayers = true;
        tilemapCollider2D.LayerIndex = 0;
        tilemapCollider2D.Friction = 0.3f;
        tilemapCollider2D.Restitution = 0.1f;
        tilemapCollider2D.IsSensor = false;
        tilemapCollider2D.CollisionLayer = 0x2ull;
        tilemapCollider2D.CollisionMask = 0xFFFFFFFEull;

        auto& prefabInstance = registry.emplace<Limitless::PrefabInstanceComponent>(root);
        prefabInstance.PrefabAssetKey = "Assets/Prefabs/Ui/Hud.prefab.json";

        auto& directionalLight = registry.emplace<Limitless::DirectionalLight2DComponent>(root);
        directionalLight.Enabled = true;
        directionalLight.Color = { 1.0f, 0.85f, 0.7f };
        directionalLight.Intensity = 2.5f;
        directionalLight.UseEntityRotation = false;
        directionalLight.Direction = glm::normalize(glm::vec2(-0.2f, -1.0f));
        directionalLight.CastShadows = true;
        directionalLight.ShadowStrength = 0.85f;
        directionalLight.ShadowSoftness = 1.6f;
        directionalLight.ShadowSamples = 7;
        directionalLight.ShadowDistance = 18.0f;
        directionalLight.ShadowBias = 0.045f;

        auto& pointLight = registry.emplace<Limitless::PointLight2DComponent>(hud);
        pointLight.Enabled = true;
        pointLight.Color = { 0.35f, 0.7f, 1.0f };
        pointLight.Intensity = 3.0f;
        pointLight.Radius = 6.5f;
        pointLight.Falloff = 2.4f;
        pointLight.CastShadows = true;
        pointLight.ShadowStrength = 0.5f;
        pointLight.ShadowSoftness = 0.65f;
        pointLight.ShadowSamples = 5;
        pointLight.ShadowBias = 0.003f;

        auto& shadowOccluder = registry.emplace<Limitless::ShadowOccluder2DComponent>(root);
        shadowOccluder.Enabled = true;
        shadowOccluder.Source = Limitless::ShadowOccluder2DComponent::SourceMode::ManualPolygon;
        shadowOccluder.Closed = true;
        shadowOccluder.Extrusion = 0.35f;
        shadowOccluder.PolygonPoints = {
            { -0.5f, -0.5f },
            { 0.6f, -0.35f },
            { 0.55f, 0.7f },
            { -0.45f, 0.4f }
        };

        auto& listener = registry.emplace<Limitless::AudioListener2DComponent>(root);
        listener.Enabled = true;
        listener.UsePrimaryCameraPosition = true;

        auto& audioSource = registry.emplace<Limitless::AudioSourceComponent>(hud);
        audioSource.AudioClipKey = "Assets/Audio/Music/MainTheme.wav";
        audioSource.Volume = 0.85f;
        audioSource.PlayOnStart = true;
        audioSource.Loop = true;
        audioSource.Muted = false;
        audioSource.Space = Limitless::AudioSourceComponent::PlaybackSpace::Spatial2D;
        audioSource.MixerGroup = "Music";
        audioSource.SpatialMinDistance = 1.5f;
        audioSource.SpatialMaxDistance = 24.0f;
        audioSource.SpatialRolloffExponent = 1.35f;
        audioSource.StereoPanStrength = 0.8f;
        audioSource.AttenuationCurveKey = "Assets/Audio/Curves/MusicDistance.curve.json";

        auto& nativeScripts = registry.emplace<Limitless::NativeScriptComponent>(hud);
        nativeScripts.Scripts.emplace_back();
        nativeScripts.Scripts[0].ScriptClassName = "HudScript";
        nativeScripts.Scripts[0].ScriptAssetRelativePath = "Gameplay/Ui/HudScript";
        nativeScripts.Scripts[0].ExposedProperties["FollowTarget"] = Limitless::ScriptEntityReference{ "Root" };
        nativeScripts.Scripts[0].ExposedProperties["DisplayName"] = std::string("HudLabel");

        const std::filesystem::path scenePath = MakeTempScenePath("SceneRoundTrip.scene.json");
        const auto saveResult = scene.SaveToFile(scenePath);
        REQUIRE(saveResult.IsSuccess());

        std::ifstream sceneFile(scenePath, std::ios::binary);
        REQUIRE(sceneFile.is_open());
        nlohmann::json rootJson;
        sceneFile >> rootJson;
        REQUIRE(rootJson.is_object());
        CHECK(rootJson.value("Version", -1) == Limitless::kSceneSerializationVersion);

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

        REQUIRE(loadedRegistry.all_of<Limitless::TilemapComponent>(loadedRoot));
        const auto& loadedTilemap = loadedRegistry.get<Limitless::TilemapComponent>(loadedRoot);
        CHECK(loadedTilemap.GridSize.x == 3);
        CHECK(loadedTilemap.GridSize.y == 2);
        CHECK(loadedTilemap.CellSize.x == doctest::Approx(0.5f));
        CHECK(loadedTilemap.CellSize.y == doctest::Approx(0.5f));
        CHECK(loadedTilemap.TilesetTileSizePixels.x == 8);
        CHECK(loadedTilemap.TilesetTileSizePixels.y == 8);
        CHECK(loadedTilemap.TilesetTextureKey == "Assets/Textures/Tiles/CityTiles.png");
        CHECK(loadedTilemap.AutoTileEnabled == true);
        REQUIRE(loadedTilemap.Layers.size() >= 2);
        CHECK(loadedTilemap.Layers[0].Tiles[0] == 1);
        CHECK(loadedTilemap.Layers[0].Tiles[1] == 2);
        CHECK(loadedTilemap.Layers[0].PerTileData[0] == 3);
        CHECK(loadedTilemap.Layers[1].Tiles[3] == 9);
        CHECK(loadedTilemap.Layers[1].CollisionEnabled == true);

        REQUIRE(loadedRegistry.all_of<Limitless::TilemapCollider2DComponent>(loadedRoot));
        const auto& loadedTilemapCollider2D = loadedRegistry.get<Limitless::TilemapCollider2DComponent>(loadedRoot);
        CHECK(loadedTilemapCollider2D.MergeAdjacentTiles == true);
        CHECK(loadedTilemapCollider2D.UseCollisionEnabledLayers == true);
        CHECK(loadedTilemapCollider2D.Friction == doctest::Approx(0.3f));
        CHECK(loadedTilemapCollider2D.Restitution == doctest::Approx(0.1f));
        CHECK(loadedTilemapCollider2D.CollisionLayer == 0x2ull);
        CHECK(loadedTilemapCollider2D.CollisionMask == 0xFFFFFFFEull);

        REQUIRE(loadedRegistry.all_of<Limitless::DirectionalLight2DComponent>(loadedRoot));
        const auto& loadedDirectionalLight = loadedRegistry.get<Limitless::DirectionalLight2DComponent>(loadedRoot);
        CHECK(loadedDirectionalLight.Enabled == true);
        CHECK(loadedDirectionalLight.Intensity == doctest::Approx(2.5f));
        CHECK(loadedDirectionalLight.UseEntityRotation == false);
        CHECK(loadedDirectionalLight.Direction.x == doctest::Approx(directionalLight.Direction.x));
        CHECK(loadedDirectionalLight.Direction.y == doctest::Approx(directionalLight.Direction.y));
        CHECK(loadedDirectionalLight.ShadowSamples == 7);
        CHECK(loadedDirectionalLight.ShadowBias == doctest::Approx(0.045f));

        REQUIRE(loadedRegistry.all_of<Limitless::PointLight2DComponent>(loadedHud));
        const auto& loadedPointLight = loadedRegistry.get<Limitless::PointLight2DComponent>(loadedHud);
        CHECK(loadedPointLight.Intensity == doctest::Approx(3.0f));
        CHECK(loadedPointLight.Radius == doctest::Approx(6.5f));
        CHECK(loadedPointLight.Falloff == doctest::Approx(2.4f));
        CHECK(loadedPointLight.ShadowSamples == 5);

        REQUIRE(loadedRegistry.all_of<Limitless::ShadowOccluder2DComponent>(loadedRoot));
        const auto& loadedShadowOccluder = loadedRegistry.get<Limitless::ShadowOccluder2DComponent>(loadedRoot);
        CHECK(loadedShadowOccluder.Source == Limitless::ShadowOccluder2DComponent::SourceMode::ManualPolygon);
        CHECK(loadedShadowOccluder.Closed == true);
        CHECK(loadedShadowOccluder.Extrusion == doctest::Approx(0.35f));
        REQUIRE(loadedShadowOccluder.PolygonPoints.size() == 4);
        CHECK(loadedShadowOccluder.PolygonPoints[2].x == doctest::Approx(0.55f));
        CHECK(loadedShadowOccluder.PolygonPoints[2].y == doctest::Approx(0.7f));

        REQUIRE(loadedRegistry.all_of<Limitless::AudioListener2DComponent>(loadedRoot));
        const auto& loadedListener = loadedRegistry.get<Limitless::AudioListener2DComponent>(loadedRoot);
        CHECK(loadedListener.Enabled == true);
        CHECK(loadedListener.UsePrimaryCameraPosition == true);

        REQUIRE(loadedRegistry.all_of<Limitless::AudioSourceComponent>(loadedHud));
        const auto& loadedAudioSource = loadedRegistry.get<Limitless::AudioSourceComponent>(loadedHud);
        CHECK(loadedAudioSource.AudioClipKey == "Assets/Audio/Music/MainTheme.wav");
        CHECK(loadedAudioSource.Volume == doctest::Approx(0.85f));
        CHECK(loadedAudioSource.PlayOnStart == true);
        CHECK(loadedAudioSource.Loop == true);
        CHECK(loadedAudioSource.Muted == false);
        CHECK(loadedAudioSource.Space == Limitless::AudioSourceComponent::PlaybackSpace::Spatial2D);
        CHECK(loadedAudioSource.MixerGroup == "Music");
        CHECK(loadedAudioSource.SpatialMinDistance == doctest::Approx(1.5f));
        CHECK(loadedAudioSource.SpatialMaxDistance == doctest::Approx(24.0f));
        CHECK(loadedAudioSource.SpatialRolloffExponent == doctest::Approx(1.35f));
        CHECK(loadedAudioSource.StereoPanStrength == doctest::Approx(0.8f));
        CHECK(loadedAudioSource.AttenuationCurveKey == "Assets/Audio/Curves/MusicDistance.curve.json");
        CHECK(loadedAudioSource.RuntimeVoiceId == 0);
        CHECK(loadedAudioSource.RuntimePlaybackStarted == false);

        REQUIRE(loadedRegistry.all_of<Limitless::NativeScriptComponent>(loadedHud));
        const auto& loadedNativeScripts = loadedRegistry.get<Limitless::NativeScriptComponent>(loadedHud);
        REQUIRE(loadedNativeScripts.Scripts.size() == 1);
        REQUIRE(loadedNativeScripts.Scripts[0].ExposedProperties.contains("FollowTarget"));
        const auto* loadedFollowTarget = std::get_if<Limitless::ScriptEntityReference>(&loadedNativeScripts.Scripts[0].ExposedProperties.at("FollowTarget"));
        REQUIRE(loadedFollowTarget != nullptr);
        CHECK(loadedFollowTarget->Tag == "Root");

        REQUIRE(loadedScene.GetEditorCameraBookmark().has_value());
        CHECK(loadedScene.GetEditorCameraBookmark()->YawDegrees == doctest::Approx(-45.0f));
        CHECK(loadedScene.GetEditorCameraBookmark()->PitchDegrees == doctest::Approx(15.0f));

        std::error_code errorCode;
        std::filesystem::remove(scenePath, errorCode);
        std::filesystem::remove(std::filesystem::path("Assets/Prefabs/Ui/Hud.prefab.json.meta"), errorCode);
        std::filesystem::remove(std::filesystem::path("Assets/Audio/Music/MainTheme.wav.meta"), errorCode);
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

    TEST_CASE("Project lighting settings save-load round-trips and clamps invalid values")
    {
        namespace Project = Limitless::Project;

        const std::filesystem::path projectRoot = MakeTempProjectRoot("LightingProjectSettings");
        std::error_code errorCode;
        std::filesystem::remove_all(projectRoot, errorCode);
        std::filesystem::create_directories(projectRoot, errorCode);
        REQUIRE(!errorCode);

        Project::Lighting2DSettings authoredSettings{};
        authoredSettings.Enabled = true;
        authoredSettings.EnableNormalMaps = true;
        authoredSettings.EnableShadows = true;
        authoredSettings.AmbientColor[0] = 0.2f;
        authoredSettings.AmbientColor[1] = 0.15f;
        authoredSettings.AmbientColor[2] = 0.1f;
        authoredSettings.AmbientIntensity = 0.75f;
        authoredSettings.ShadowQualityLevel = 2;
        authoredSettings.MaxDirectionalLights = 3;
        authoredSettings.MaxPointLights = 24;
        authoredSettings.MaxShadowSegments = 196;
        authoredSettings.ShadowSoftnessScale = 1.35f;
        authoredSettings.DirectionalShadowBiasScale = 1.4f;
        authoredSettings.MaxShadowSamplesPerLight = 14;

        const auto saveResult = Project::SaveLighting2DSettings(projectRoot, authoredSettings);
        REQUIRE(saveResult.IsSuccess());

        const auto loadResult = Project::LoadLighting2DSettings(projectRoot);
        REQUIRE(loadResult.IsSuccess());
        const auto& loadedSettings = loadResult.GetValue();
        CHECK(loadedSettings.Enabled == true);
        CHECK(loadedSettings.EnableNormalMaps == true);
        CHECK(loadedSettings.EnableShadows == true);
        CHECK(loadedSettings.AmbientColor[0] == doctest::Approx(0.2f));
        CHECK(loadedSettings.AmbientColor[1] == doctest::Approx(0.15f));
        CHECK(loadedSettings.AmbientColor[2] == doctest::Approx(0.1f));
        CHECK(loadedSettings.AmbientIntensity == doctest::Approx(0.75f));
        CHECK(loadedSettings.ShadowQualityLevel == 2);
        CHECK(loadedSettings.MaxDirectionalLights == 3);
        CHECK(loadedSettings.MaxPointLights == 24);
        CHECK(loadedSettings.MaxShadowSegments == 196);
        CHECK(loadedSettings.ShadowSoftnessScale == doctest::Approx(1.35f));
        CHECK(loadedSettings.DirectionalShadowBiasScale == doctest::Approx(1.4f));
        CHECK(loadedSettings.MaxShadowSamplesPerLight == 14);

        const std::filesystem::path lightingSettingsPath = Project::GetLighting2DSettingsPath(projectRoot);
        {
            nlohmann::json invalidSettings = {
                { "version", 1 },
                { "enabled", true },
                { "enableNormalMaps", true },
                { "enableShadows", true },
                { "ambientColor", { 1.2f, -0.6f, 0.3f } },
                { "ambientIntensity", -8.0f },
                { "shadowQualityLevel", 99 },
                { "maxDirectionalLights", -5 },
                { "maxPointLights", -12 },
                { "maxShadowSegments", 0 },
                { "shadowSoftnessScale", -3.0f },
                { "directionalShadowBiasScale", -4.0f },
                { "maxShadowSamplesPerLight", 0 }
            };

            std::filesystem::create_directories(lightingSettingsPath.parent_path(), errorCode);
            REQUIRE(!errorCode);
            std::ofstream stream(lightingSettingsPath, std::ios::binary);
            REQUIRE(stream.is_open());
            stream << invalidSettings.dump(2);
        }

        const auto clampedResult = Project::LoadLighting2DSettings(projectRoot);
        REQUIRE(clampedResult.IsSuccess());
        const auto& clampedSettings = clampedResult.GetValue();
        CHECK(clampedSettings.AmbientIntensity == doctest::Approx(0.0f));
        CHECK(clampedSettings.ShadowQualityLevel == 2);
        CHECK(clampedSettings.MaxDirectionalLights == 0);
        CHECK(clampedSettings.MaxPointLights == 0);
        CHECK(clampedSettings.MaxShadowSegments == 1);
        CHECK(clampedSettings.ShadowSoftnessScale == doctest::Approx(0.0f));
        CHECK(clampedSettings.DirectionalShadowBiasScale == doctest::Approx(0.0f));
        CHECK(clampedSettings.MaxShadowSamplesPerLight == 1);

        std::filesystem::remove_all(projectRoot, errorCode);
    }

    TEST_CASE("Animator components clone and serialize authored data")
    {
        Limitless::Scene scene;
        auto& registry = scene.GetRegistry();

        const entt::entity animatedEntity = scene.CreateEntity("Animated");
        auto& animator = registry.emplace<Limitless::AnimatorComponent>(animatedEntity);
        animator.ControllerKey = "Assets/Animation/Character.animcontroller.json";
        animator.DefaultClipKey = "Assets/Animation/Idle.animationclip.json";
        animator.PlaybackSpeed = 1.25f;
        animator.Enabled = true;
        animator.ApplyToSprite = true;
        animator.ApplyToTransform = true;
        animator.AutoPlay = true;
        animator.BoolParameters["Grounded"] = true;
        animator.FloatParameters["Speed"] = 3.5f;
        animator.IntegerParameters["Combo"] = 2;
        animator.TriggerParameters["Fire"] = true;
        animator.RuntimeInitialized = true;
        animator.RuntimeCurrentStateName = "Run";
        animator.RuntimeCurrentClipKey = "Assets/Animation/Run.animationclip.json";
        animator.RuntimeStateTimeSeconds = 0.65f;
        animator.RuntimeCurrentStateDurationSeconds = 0.9f;
        animator.RuntimeSpriteTextureOverrideKey = "Assets/Textures/Character_Run.png";
        animator.RuntimeSpriteTextureOverrideLoadAttempted = true;

        auto& receiver = registry.emplace<Limitless::AnimationEventReceiverComponent>(animatedEntity);
        receiver.Enabled = true;
        receiver.RuntimeDispatchedEvents.push_back({
            "Footstep",
            "Left",
            0.15f,
            1,
            true,
            0.25f,
            0.5f
        });

        auto clone = scene.Clone();
        REQUIRE(clone != nullptr);
        const entt::entity clonedEntity = FindEntityByTag(*clone, "Animated");
        REQUIRE_FALSE(IsNullEntity(clonedEntity));

        const auto& cloneRegistry = clone->GetRegistry();
        REQUIRE(cloneRegistry.all_of<Limitless::AnimatorComponent>(clonedEntity));
        REQUIRE(cloneRegistry.all_of<Limitless::AnimationEventReceiverComponent>(clonedEntity));

        const auto& clonedAnimator = cloneRegistry.get<Limitless::AnimatorComponent>(clonedEntity);
        CHECK(clonedAnimator.ControllerKey == animator.ControllerKey);
        CHECK(clonedAnimator.DefaultClipKey == animator.DefaultClipKey);
        CHECK(clonedAnimator.PlaybackSpeed == doctest::Approx(1.25f));
        CHECK(clonedAnimator.GetBoolParameter("Grounded", false));
        CHECK(clonedAnimator.GetFloatParameter("Speed", 0.0f) == doctest::Approx(3.5f));
        CHECK(clonedAnimator.GetIntegerParameter("Combo", 0) == 2);
        CHECK(clonedAnimator.TriggerParameters.contains("Fire"));
        CHECK_FALSE(clonedAnimator.TriggerParameters.at("Fire"));
        CHECK_FALSE(clonedAnimator.RuntimeInitialized);
        CHECK(clonedAnimator.RuntimeCurrentStateName.empty());
        CHECK(clonedAnimator.RuntimeCurrentClipKey.empty());
        CHECK(clonedAnimator.RuntimeStateTimeSeconds == doctest::Approx(0.0f));
        CHECK(clonedAnimator.RuntimeCurrentStateDurationSeconds == doctest::Approx(1.0f));
        CHECK(clonedAnimator.RuntimeSpriteTextureOverrideKey.empty());
        CHECK_FALSE(clonedAnimator.RuntimeSpriteTextureOverrideLoadAttempted);

        const auto& clonedReceiver = cloneRegistry.get<Limitless::AnimationEventReceiverComponent>(clonedEntity);
        CHECK(clonedReceiver.Enabled);
        CHECK(clonedReceiver.RuntimeDispatchedEvents.empty());

        const std::filesystem::path tempScenePath = MakeTempScenePath("AnimatorSerialization.scene.json");
        std::error_code errorCode;
        std::filesystem::create_directories(tempScenePath.parent_path(), errorCode);
        REQUIRE(!errorCode);

        const auto saveResult = scene.SaveToFile(tempScenePath);
        REQUIRE(saveResult.IsSuccess());

        const auto loadResult = Limitless::Scene::LoadFromFile(tempScenePath);
        REQUIRE(loadResult.IsSuccess());
        REQUIRE(loadResult.GetValue() != nullptr);

        const entt::entity loadedEntity = FindEntityByTag(*loadResult.GetValue(), "Animated");
        REQUIRE_FALSE(IsNullEntity(loadedEntity));
        const auto& loadedRegistry = loadResult.GetValue()->GetRegistry();
        REQUIRE(loadedRegistry.all_of<Limitless::AnimatorComponent>(loadedEntity));
        REQUIRE(loadedRegistry.all_of<Limitless::AnimationEventReceiverComponent>(loadedEntity));

        const auto& loadedAnimator = loadedRegistry.get<Limitless::AnimatorComponent>(loadedEntity);
        CHECK(loadedAnimator.ControllerKey == animator.ControllerKey);
        CHECK(loadedAnimator.DefaultClipKey == animator.DefaultClipKey);
        CHECK(loadedAnimator.PlaybackSpeed == doctest::Approx(1.25f));
        CHECK(loadedAnimator.GetBoolParameter("Grounded", false));
        CHECK(loadedAnimator.GetFloatParameter("Speed", 0.0f) == doctest::Approx(3.5f));
        CHECK(loadedAnimator.GetIntegerParameter("Combo", 0) == 2);
        CHECK(loadedAnimator.TriggerParameters.contains("Fire"));
        CHECK_FALSE(loadedAnimator.TriggerParameters.at("Fire"));

        const auto& loadedReceiver = loadedRegistry.get<Limitless::AnimationEventReceiverComponent>(loadedEntity);
        CHECK(loadedReceiver.Enabled);
        CHECK(loadedReceiver.RuntimeDispatchedEvents.empty());

        std::filesystem::remove(tempScenePath, errorCode);
    }
}
