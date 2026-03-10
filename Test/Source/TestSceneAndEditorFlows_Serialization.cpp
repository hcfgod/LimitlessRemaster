#include "TestSceneAndEditorFlowsShared.h"

using namespace SceneEditorFlowTestSupport;

TEST_SUITE("Scene And Editor Flows")
{
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
        sprite.TextureLoadAttempted = true;
        sprite.Color = { 0.25f, 0.5f, 0.75f, 1.0f };
        sprite.SubSpriteIndex = 4;
        sprite.UvMin = { 0.125f, 0.25f };
        sprite.UvMax = { 0.625f, 0.75f };

        auto& material = registry.emplace<Limitless::MaterialComponent>(child);
        material.MaterialKey = "Assets/Materials/Ui/Button.material.json";
        material.MaterialLoadAttempted = true;

        auto& text = registry.emplace<Limitless::UITextComponent>(child);
        text.Text = "Play";
        text.FontFilePath = "Assets/Fonts/Inter-Regular.ttf";
        text.FontSize = 42.0f;
        text.Color = { 1.0f, 0.9f, 0.2f, 1.0f };
        text.FontLoadAttempted = true;

        auto& grid2D = registry.emplace<Limitless::Grid2DComponent>(child);
        grid2D.CellSize = { 1.0f, 1.0f };
        auto& tilemapLayer = registry.emplace<Limitless::TilemapLayerComponent>(child);
        tilemapLayer.GridSize = { 4, 3 };
        tilemapLayer.ResizeGrid(tilemapLayer.GridSize);
        const uint32_t grassTileId = tilemapLayer.GetOrAddTileTableEntry("Assets/Tiles/Grass.tile.json");
        const uint32_t stoneTileId = tilemapLayer.GetOrAddTileTableEntry("Assets/Tiles/Stone.tile.json");
        tilemapLayer.Tiles[0] = grassTileId;
        tilemapLayer.Tiles[1] = stoneTileId;

        auto& camera = registry.emplace<Limitless::CameraComponent>(parent);
        camera.Projection = Limitless::CameraComponent::ProjectionType::Perspective3D;
        camera.IsPrimary = false;
        camera.FieldOfViewYDegrees = 75.0f;
        camera.NearPlane = 0.1f;
        camera.FarPlane = 3000.0f;

        auto& listener = registry.emplace<Limitless::AudioListener2DComponent>(parent);
        listener.Enabled = true;
        listener.UsePrimaryCameraPosition = false;

        auto& listener3D = registry.emplace<Limitless::AudioListener3DComponent>(parent);
        listener3D.Enabled = true;
        listener3D.UsePrimaryCameraTransform = false;
        listener3D.RuntimeHasPreviousWorldPosition = true;
        listener3D.RuntimePreviousWorldPosition = { 3.0f, 4.0f, 5.0f };

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
        audio.Space = Limitless::AudioSourceComponent::PlaybackSpace::Spatial3D;
        audio.MixerGroup = "UI";
        audio.SpatialMinDistance = 0.75f;
        audio.SpatialMaxDistance = 8.25f;
        audio.SpatialRolloffExponent = 2.0f;
        audio.StereoPanStrength = 0.65f;
        audio.SpatialRolloffMode = Limitless::AudioSourceComponent::RolloffMode::Inverse;
        audio.DopplerFactor = 1.35f;
        audio.EnableDirectionalAttenuation = true;
        audio.DirectionalInnerAngleDegrees = 55.0f;
        audio.DirectionalOuterAngleDegrees = 130.0f;
        audio.DirectionalOuterVolume = 0.25f;
        audio.AttenuationCurveKey = "Assets/Audio/Curves/UiNearToFar.curve.json";
        audio.RuntimeVoiceId = 99;
        audio.RuntimePlaybackStarted = true;
        audio.RuntimePlayOnStartConsumed = true;
        audio.RuntimeHasPreviousWorldPosition = true;
        audio.RuntimePreviousWorldPosition = { -2.0f, 1.0f, 0.5f };

        auto& scriptEntry = AttachScriptEntry(scene, child);
        scriptEntry.ScriptClassName = "ButtonScript";
        scriptEntry.ScriptAssetRelativePath = "Gameplay/Ui/ButtonScript";
        scriptEntry.Enabled = true;
        scriptEntry.RuntimeInitialized = true;
        scriptEntry.RuntimeUpdateCount = 123;
        scriptEntry.ExposedProperties["FollowTarget"] = Limitless::ScriptEntityReference{ "Parent" };

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
        REQUIRE(cloneRegistry.all_of<Limitless::UITextComponent>(clonedChild));
        REQUIRE(cloneRegistry.all_of<Limitless::AudioSourceComponent>(clonedChild));
        REQUIRE(clone->GetScriptComponentEntities(clonedChild).size() == 1);
        REQUIRE(cloneRegistry.all_of<Limitless::DirectionalLight2DComponent>(clonedChild));
        REQUIRE(cloneRegistry.all_of<Limitless::PointLight2DComponent>(clonedChild));
        REQUIRE(cloneRegistry.all_of<Limitless::ShadowOccluder2DComponent>(clonedChild));
        REQUIRE(cloneRegistry.all_of<Limitless::Rigidbody2DComponent>(clonedChild));
        REQUIRE(cloneRegistry.all_of<Limitless::BoxCollider2DComponent>(clonedChild));
        REQUIRE(cloneRegistry.all_of<Limitless::CircleCollider2DComponent>(clonedChild));
        REQUIRE(cloneRegistry.all_of<Limitless::Joint2DComponent>(clonedChild));
        REQUIRE(cloneRegistry.all_of<Limitless::Grid2DComponent>(clonedChild));
        REQUIRE(cloneRegistry.all_of<Limitless::TilemapLayerComponent>(clonedChild));
        REQUIRE(cloneRegistry.all_of<Limitless::CameraComponent>(clonedParent));
        REQUIRE(cloneRegistry.all_of<Limitless::AudioListener2DComponent>(clonedParent));
        REQUIRE(cloneRegistry.all_of<Limitless::AudioListener3DComponent>(clonedParent));

        const auto& clonedSprite = cloneRegistry.get<Limitless::SpriteComponent>(clonedChild);
        CHECK(clonedSprite.TextureKey == sprite.TextureKey);
        CHECK(clonedSprite.TextureLoadAttempted == false);
        CHECK(clonedSprite.SubSpriteIndex == sprite.SubSpriteIndex);
        CHECK(clonedSprite.UvMin.x == doctest::Approx(sprite.UvMin.x));
        CHECK(clonedSprite.UvMin.y == doctest::Approx(sprite.UvMin.y));
        CHECK(clonedSprite.UvMax.x == doctest::Approx(sprite.UvMax.x));
        CHECK(clonedSprite.UvMax.y == doctest::Approx(sprite.UvMax.y));

        const auto& clonedMaterial = cloneRegistry.get<Limitless::MaterialComponent>(clonedChild);
        CHECK(clonedMaterial.MaterialKey == material.MaterialKey);
        CHECK(clonedMaterial.MaterialLoadAttempted == false);

        const auto& clonedText = cloneRegistry.get<Limitless::UITextComponent>(clonedChild);
        CHECK(clonedText.Text == text.Text);
        CHECK(clonedText.FontFilePath == text.FontFilePath);
        CHECK(clonedText.FontSize == doctest::Approx(text.FontSize));
        CHECK(clonedText.FontLoadAttempted == false);

        const auto& clonedGrid2D = cloneRegistry.get<Limitless::Grid2DComponent>(clonedChild);
        CHECK(clonedGrid2D.CellSize.x == doctest::Approx(1.0f));
        CHECK(clonedGrid2D.CellSize.y == doctest::Approx(1.0f));
        const auto& clonedTilemapLayer = cloneRegistry.get<Limitless::TilemapLayerComponent>(clonedChild);
        CHECK(clonedTilemapLayer.GridSize.x == 4);
        CHECK(clonedTilemapLayer.GridSize.y == 3);
        REQUIRE(clonedTilemapLayer.TileTable.size() >= 3);
        const uint32_t clonedGrassTileId = FindTileIdByAssetKey(clonedTilemapLayer, "Assets/Tiles/Grass.tile.json");
        const uint32_t clonedStoneTileId = FindTileIdByAssetKey(clonedTilemapLayer, "Assets/Tiles/Stone.tile.json");
        REQUIRE(clonedGrassTileId != 0u);
        REQUIRE(clonedStoneTileId != 0u);
        CHECK(clonedGrassTileId != clonedStoneTileId);
        CHECK(clonedTilemapLayer.Tiles[0] == clonedGrassTileId);
        CHECK(clonedTilemapLayer.Tiles[1] == clonedStoneTileId);

        const auto& clonedAudio = cloneRegistry.get<Limitless::AudioSourceComponent>(clonedChild);
        CHECK(clonedAudio.AudioClipKey == audio.AudioClipKey);
        CHECK(clonedAudio.Volume == doctest::Approx(audio.Volume));
        CHECK(clonedAudio.Space == Limitless::AudioSourceComponent::PlaybackSpace::Spatial3D);
        CHECK(clonedAudio.MixerGroup == "UI");
        CHECK(clonedAudio.SpatialMinDistance == doctest::Approx(0.75f));
        CHECK(clonedAudio.SpatialMaxDistance == doctest::Approx(8.25f));
        CHECK(clonedAudio.SpatialRolloffExponent == doctest::Approx(2.0f));
        CHECK(clonedAudio.StereoPanStrength == doctest::Approx(0.65f));
        CHECK(clonedAudio.SpatialRolloffMode == Limitless::AudioSourceComponent::RolloffMode::Inverse);
        CHECK(clonedAudio.DopplerFactor == doctest::Approx(1.35f));
        CHECK(clonedAudio.EnableDirectionalAttenuation == true);
        CHECK(clonedAudio.DirectionalInnerAngleDegrees == doctest::Approx(55.0f));
        CHECK(clonedAudio.DirectionalOuterAngleDegrees == doctest::Approx(130.0f));
        CHECK(clonedAudio.DirectionalOuterVolume == doctest::Approx(0.25f));
        CHECK(clonedAudio.AttenuationCurveKey == "Assets/Audio/Curves/UiNearToFar.curve.json");
        CHECK(clonedAudio.RuntimeVoiceId == 0);
        CHECK(clonedAudio.RuntimePlaybackStarted == false);
        CHECK(clonedAudio.RuntimePlayOnStartConsumed == false);
        CHECK(clonedAudio.RuntimeHasPreviousWorldPosition == false);
        CHECK(clonedAudio.RuntimePreviousWorldPosition.x == doctest::Approx(0.0f));
        CHECK(clonedAudio.RuntimePreviousWorldPosition.y == doctest::Approx(0.0f));
        CHECK(clonedAudio.RuntimePreviousWorldPosition.z == doctest::Approx(0.0f));

        const auto& clonedListener = cloneRegistry.get<Limitless::AudioListener2DComponent>(clonedParent);
        CHECK(clonedListener.Enabled == true);
        CHECK(clonedListener.UsePrimaryCameraPosition == false);

        const auto& clonedListener3D = cloneRegistry.get<Limitless::AudioListener3DComponent>(clonedParent);
        CHECK(clonedListener3D.Enabled == true);
        CHECK(clonedListener3D.UsePrimaryCameraTransform == false);
        CHECK(clonedListener3D.RuntimeHasPreviousWorldPosition == false);
        CHECK(clonedListener3D.RuntimePreviousWorldPosition.x == doctest::Approx(0.0f));
        CHECK(clonedListener3D.RuntimePreviousWorldPosition.y == doctest::Approx(0.0f));
        CHECK(clonedListener3D.RuntimePreviousWorldPosition.z == doctest::Approx(0.0f));

        const auto& clonedScript = GetScriptEntry(*clone, clonedChild, 0);
        CHECK(clonedScript.ScriptClassName == "ButtonScript");
        REQUIRE(clonedScript.ExposedProperties.contains("FollowTarget"));
        const auto* clonedFollowTarget = std::get_if<Limitless::ScriptEntityReference>(&clonedScript.ExposedProperties.at("FollowTarget"));
        REQUIRE(clonedFollowTarget != nullptr);
        CHECK(clonedFollowTarget->Tag == "Parent");
        CHECK(clonedScript.RuntimeInitialized == false);
        CHECK(clonedScript.RuntimeUpdateCount == 0);

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

    TEST_CASE("InstantiatePrefab preserves sprite sub-sprite authored fields")
    {
        Limitless::Scene prefabScene;
        auto& prefabRegistry = prefabScene.GetRegistry();

        const entt::entity prefabRoot = prefabScene.CreateEntity("PrefabRoot");
        auto& prefabSprite = prefabRegistry.emplace<Limitless::SpriteComponent>(prefabRoot);
        prefabSprite.TextureKey = "Assets/Textures/Characters/Player.png";
        prefabSprite.SubSpriteIndex = 2;
        prefabSprite.UvMin = { 0.2f, 0.3f };
        prefabSprite.UvMax = { 0.7f, 0.9f };

        const std::filesystem::path prefabPath = MakeTempScenePath("SpriteSubSprite.prefab.json");
        std::error_code errorCode;
        std::filesystem::create_directories(prefabPath.parent_path(), errorCode);
        REQUIRE(!errorCode);

        const auto saveResult = prefabScene.SaveToFile(prefabPath);
        REQUIRE(saveResult.IsSuccess());

        Limitless::Scene destinationScene;
        const entt::entity instantiatedRoot = destinationScene.InstantiatePrefab(prefabPath.string());
        REQUIRE_FALSE(IsNullEntity(instantiatedRoot));
        REQUIRE(destinationScene.GetRegistry().all_of<Limitless::SpriteComponent>(instantiatedRoot));

        const auto& instantiatedSprite = destinationScene.GetRegistry().get<Limitless::SpriteComponent>(instantiatedRoot);
        CHECK(instantiatedSprite.TextureKey == prefabSprite.TextureKey);
        CHECK(instantiatedSprite.SubSpriteIndex == prefabSprite.SubSpriteIndex);
        CHECK(instantiatedSprite.UvMin.x == doctest::Approx(prefabSprite.UvMin.x));
        CHECK(instantiatedSprite.UvMin.y == doctest::Approx(prefabSprite.UvMin.y));
        CHECK(instantiatedSprite.UvMax.x == doctest::Approx(prefabSprite.UvMax.x));
        CHECK(instantiatedSprite.UvMax.y == doctest::Approx(prefabSprite.UvMax.y));

        std::filesystem::remove(prefabPath, errorCode);
    }

    TEST_CASE("InstantiatePrefab under singular parent transform keeps instantiated transform finite")
    {
        Limitless::Scene prefabScene;
        auto& prefabRegistry = prefabScene.GetRegistry();

        const entt::entity prefabRoot = prefabScene.CreateEntity("PrefabRootSingularParent");
        auto& prefabRootTransform = prefabRegistry.get<Limitless::TransformComponent>(prefabRoot);
        prefabRootTransform.Position = { 2.5f, -1.25f, 0.0f };
        prefabRootTransform.Rotation = { 0.0f, 0.0f, 45.0f };
        prefabRootTransform.Scale = { 1.25f, 0.8f, 1.0f };

        const std::filesystem::path prefabPath = MakeTempScenePath("SingularParentInstantiate.prefab.json");
        std::error_code errorCode;
        std::filesystem::create_directories(prefabPath.parent_path(), errorCode);
        REQUIRE(!errorCode);

        const auto saveResult = prefabScene.SaveToFile(prefabPath);
        REQUIRE(saveResult.IsSuccess());

        Limitless::Scene destinationScene;
        auto& destinationRegistry = destinationScene.GetRegistry();
        const entt::entity parent = destinationScene.CreateEntity("SingularParent");
        auto& parentTransform = destinationRegistry.get<Limitless::TransformComponent>(parent);
        parentTransform.Position = { -3.0f, 6.0f, 0.0f };
        parentTransform.Scale = { 0.0f, 1.0f, 1.0f };

        const entt::entity instantiatedRoot = destinationScene.InstantiatePrefab(prefabPath.string(), parent);
        REQUIRE_FALSE(IsNullEntity(instantiatedRoot));

        const auto& instantiatedTransform = destinationRegistry.get<Limitless::TransformComponent>(instantiatedRoot);
        CHECK(std::isfinite(instantiatedTransform.Position.x));
        CHECK(std::isfinite(instantiatedTransform.Position.y));
        CHECK(std::isfinite(instantiatedTransform.Position.z));
        CHECK(std::isfinite(instantiatedTransform.Rotation.x));
        CHECK(std::isfinite(instantiatedTransform.Rotation.y));
        CHECK(std::isfinite(instantiatedTransform.Rotation.z));
        CHECK(std::isfinite(instantiatedTransform.Scale.x));
        CHECK(std::isfinite(instantiatedTransform.Scale.y));
        CHECK(std::isfinite(instantiatedTransform.Scale.z));

        std::filesystem::remove(prefabPath, errorCode);
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

        auto& text = registry.emplace<Limitless::UITextComponent>(hud);
        text.Text = "Score: 999";
        text.FontFilePath = "Assets/Fonts/ScoreFont.ttf";
        text.FontSize = 24.0f;

        auto& sprite = registry.emplace<Limitless::SpriteComponent>(root);
        sprite.TextureKey = "Assets/Textures/Backgrounds/Stage01.png";

        auto& grid2D = registry.emplace<Limitless::Grid2DComponent>(root);
        grid2D.CellSize = { 0.5f, 0.5f };
        auto& tilemapLayer = registry.emplace<Limitless::TilemapLayerComponent>(root);
        tilemapLayer.GridSize = { 3, 2 };
        tilemapLayer.ResizeGrid(tilemapLayer.GridSize);
        tilemapLayer.CollisionEnabled = true;
        const uint32_t cityTileA = tilemapLayer.GetOrAddTileTableEntry("Assets/Tiles/CityTiles_0.tile.json");
        const uint32_t cityTileB = tilemapLayer.GetOrAddTileTableEntry("Assets/Tiles/CityTiles_1.tile.json");
        tilemapLayer.Tiles[0] = cityTileA;
        tilemapLayer.Tiles[1] = cityTileB;
        tilemapLayer.Tiles[3] = cityTileB;
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

        auto& listener3D = registry.emplace<Limitless::AudioListener3DComponent>(root);
        listener3D.Enabled = true;
        listener3D.UsePrimaryCameraTransform = false;

        auto& audioSource = registry.emplace<Limitless::AudioSourceComponent>(hud);
        audioSource.AudioClipKey = "Assets/Audio/Music/MainTheme.wav";
        audioSource.Volume = 0.85f;
        audioSource.PlayOnStart = true;
        audioSource.Loop = true;
        audioSource.Muted = false;
        audioSource.Space = Limitless::AudioSourceComponent::PlaybackSpace::Spatial3D;
        audioSource.MixerGroup = "Music";
        audioSource.SpatialMinDistance = 1.5f;
        audioSource.SpatialMaxDistance = 24.0f;
        audioSource.SpatialRolloffExponent = 1.35f;
        audioSource.StereoPanStrength = 0.8f;
        audioSource.SpatialRolloffMode = Limitless::AudioSourceComponent::RolloffMode::SmoothStep;
        audioSource.DopplerFactor = 1.2f;
        audioSource.EnableDirectionalAttenuation = true;
        audioSource.DirectionalInnerAngleDegrees = 60.0f;
        audioSource.DirectionalOuterAngleDegrees = 180.0f;
        audioSource.DirectionalOuterVolume = 0.4f;
        audioSource.AttenuationCurveKey = "Assets/Audio/Curves/MusicDistance.curve.json";
        audioSource.RuntimeVoiceId = 99;
        audioSource.RuntimePlaybackStarted = true;
        audioSource.RuntimePlayOnStartConsumed = true;
        audioSource.RuntimeHasPreviousWorldPosition = true;
        audioSource.RuntimePreviousWorldPosition = { -2.0f, 1.0f, 0.5f };

        auto& scriptEntry = AttachScriptEntry(scene, hud);
        scriptEntry.ScriptClassName = "HudScript";
        scriptEntry.ScriptAssetRelativePath = "Gameplay/Ui/HudScript";
        scriptEntry.ExposedProperties["FollowTarget"] = Limitless::ScriptEntityReference{ "Root" };
        scriptEntry.ExposedProperties["DisplayName"] = std::string("HudLabel");
        scriptEntry.ExposedProperties["EnemyPrefab"] = Limitless::Prefab{ "Assets/Prefabs/Enemies/BasicEnemy.prefab.json" };

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

        const auto& loadedText = loadedRegistry.get<Limitless::UITextComponent>(loadedHud);
        CHECK(loadedText.Text == "Score: 999");
        CHECK(loadedText.FontFilePath == "Assets/Fonts/ScoreFont.ttf");

        REQUIRE(loadedRegistry.all_of<Limitless::PrefabInstanceComponent>(loadedRoot));
        const auto& loadedPrefabInstance = loadedRegistry.get<Limitless::PrefabInstanceComponent>(loadedRoot);
        CHECK(loadedPrefabInstance.PrefabAssetKey == "Assets/Prefabs/Ui/Hud.prefab.json");

        REQUIRE(loadedRegistry.all_of<Limitless::Grid2DComponent>(loadedRoot));
        REQUIRE(loadedRegistry.all_of<Limitless::TilemapLayerComponent>(loadedRoot));
        const auto& loadedGrid2D = loadedRegistry.get<Limitless::Grid2DComponent>(loadedRoot);
        CHECK(loadedGrid2D.CellSize.x == doctest::Approx(0.5f));
        CHECK(loadedGrid2D.CellSize.y == doctest::Approx(0.5f));
        const auto& loadedTilemapLayer = loadedRegistry.get<Limitless::TilemapLayerComponent>(loadedRoot);
        CHECK(loadedTilemapLayer.GridSize.x == 3);
        CHECK(loadedTilemapLayer.GridSize.y == 2);
        CHECK(loadedTilemapLayer.CollisionEnabled == true);
        REQUIRE(loadedTilemapLayer.TileTable.size() >= 3);
        const uint32_t loadedCityTileAId = FindTileIdByAssetKey(loadedTilemapLayer, "Assets/Tiles/CityTiles_0.tile.json");
        const uint32_t loadedCityTileBId = FindTileIdByAssetKey(loadedTilemapLayer, "Assets/Tiles/CityTiles_1.tile.json");
        REQUIRE(loadedCityTileAId != 0u);
        REQUIRE(loadedCityTileBId != 0u);
        CHECK(loadedCityTileAId != loadedCityTileBId);
        CHECK(loadedTilemapLayer.Tiles[0] == loadedCityTileAId);
        CHECK(loadedTilemapLayer.Tiles[1] == loadedCityTileBId);
        CHECK(loadedTilemapLayer.Tiles[3] == loadedCityTileBId);

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

        REQUIRE(loadedRegistry.all_of<Limitless::AudioListener3DComponent>(loadedRoot));
        const auto& loadedListener3D = loadedRegistry.get<Limitless::AudioListener3DComponent>(loadedRoot);
        CHECK(loadedListener3D.Enabled == true);
        CHECK(loadedListener3D.UsePrimaryCameraTransform == false);
        CHECK(loadedListener3D.RuntimeHasPreviousWorldPosition == false);

        REQUIRE(loadedRegistry.all_of<Limitless::AudioSourceComponent>(loadedHud));
        const auto& loadedAudioSource = loadedRegistry.get<Limitless::AudioSourceComponent>(loadedHud);
        CHECK(loadedAudioSource.AudioClipKey == "Assets/Audio/Music/MainTheme.wav");
        CHECK(loadedAudioSource.Volume == doctest::Approx(0.85f));
        CHECK(loadedAudioSource.PlayOnStart == true);
        CHECK(loadedAudioSource.Loop == true);
        CHECK(loadedAudioSource.Muted == false);
        CHECK(loadedAudioSource.Space == Limitless::AudioSourceComponent::PlaybackSpace::Spatial3D);
        CHECK(loadedAudioSource.MixerGroup == "Music");
        CHECK(loadedAudioSource.SpatialMinDistance == doctest::Approx(1.5f));
        CHECK(loadedAudioSource.SpatialMaxDistance == doctest::Approx(24.0f));
        CHECK(loadedAudioSource.SpatialRolloffExponent == doctest::Approx(1.35f));
        CHECK(loadedAudioSource.StereoPanStrength == doctest::Approx(0.8f));
        CHECK(loadedAudioSource.SpatialRolloffMode == Limitless::AudioSourceComponent::RolloffMode::SmoothStep);
        CHECK(loadedAudioSource.DopplerFactor == doctest::Approx(1.2f));
        CHECK(loadedAudioSource.EnableDirectionalAttenuation == true);
        CHECK(loadedAudioSource.DirectionalInnerAngleDegrees == doctest::Approx(60.0f));
        CHECK(loadedAudioSource.DirectionalOuterAngleDegrees == doctest::Approx(180.0f));
        CHECK(loadedAudioSource.DirectionalOuterVolume == doctest::Approx(0.4f));
        CHECK(loadedAudioSource.AttenuationCurveKey == "Assets/Audio/Curves/MusicDistance.curve.json");
        CHECK(loadedAudioSource.RuntimeVoiceId == 0);
        CHECK(loadedAudioSource.RuntimePlaybackStarted == false);
        CHECK(loadedAudioSource.RuntimePlayOnStartConsumed == false);
        CHECK(loadedAudioSource.RuntimeHasPreviousWorldPosition == false);

        REQUIRE(loadedScene.GetScriptComponentEntities(loadedHud).size() == 1);
        const auto& loadedScript = GetScriptEntry(loadedScene, loadedHud, 0);
        REQUIRE(loadedScript.ExposedProperties.contains("FollowTarget"));
        const auto* loadedFollowTarget = std::get_if<Limitless::ScriptEntityReference>(&loadedScript.ExposedProperties.at("FollowTarget"));
        REQUIRE(loadedFollowTarget != nullptr);
        CHECK(loadedFollowTarget->Tag == "Root");
        REQUIRE(loadedScript.ExposedProperties.contains("EnemyPrefab"));
        const auto* loadedEnemyPrefab = std::get_if<Limitless::Prefab>(&loadedScript.ExposedProperties.at("EnemyPrefab"));
        REQUIRE(loadedEnemyPrefab != nullptr);
        CHECK(loadedEnemyPrefab->AssetKey == "Assets/Prefabs/Enemies/BasicEnemy.prefab.json");

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

        auto& text = registry.emplace<Limitless::UITextComponent>(player);
        text.Text = "Player";

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
        REQUIRE(loadedRegistry.all_of<Limitless::SpriteComponent, Limitless::UITextComponent>(loadedPlayer));

        const auto& loadedTransform = loadedRegistry.get<Limitless::TransformComponent>(loadedPlayer);
        CHECK(loadedTransform.Position.x == doctest::Approx(1.0f));
        CHECK(loadedTransform.Position.y == doctest::Approx(2.0f));
        CHECK(loadedTransform.Position.z == doctest::Approx(3.0f));

        const auto& loadedSprite = loadedRegistry.get<Limitless::SpriteComponent>(loadedPlayer);
        CHECK(loadedSprite.TextureKey == "Assets/Textures/Characters/Player.png");

        const auto& loadedText = loadedRegistry.get<Limitless::UITextComponent>(loadedPlayer);
        CHECK(loadedText.Text == "Player");

        std::error_code errorCode;
        std::filesystem::remove(scenePath, errorCode);
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
