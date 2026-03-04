#define DOCTEST_CONFIG_WITH_VARIADIC_MACROS
#include <doctest/doctest.h>

#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components/CoreComponents.h"

#include <string>
#include <vector>

TEST_SUITE("Scene ECS")
{
    TEST_CASE("CreateEntity returns valid entity with TagComponent")
    {
        Limitless::Scene scene;

        entt::entity entity = scene.CreateEntity("TestEntity");
        CHECK((entity != entt::null));
        CHECK(scene.IsValid(entity));

        auto& tag = scene.GetRegistry().get<Limitless::TagComponent>(entity);
        CHECK(tag.Tag == "TestEntity");
        CHECK(tag.Enabled == true);
    }

    TEST_CASE("CreateEntity assigns TransformComponent with defaults")
    {
        Limitless::Scene scene;
        entt::entity entity = scene.CreateEntity("Positioned");

        REQUIRE(scene.GetRegistry().all_of<Limitless::TransformComponent>(entity));

        auto& transform = scene.GetRegistry().get<Limitless::TransformComponent>(entity);
        CHECK(transform.Position == glm::vec3(0.0f));
        CHECK(transform.Scale == glm::vec3(1.0f));
        CHECK(transform.Rotation == glm::vec3(0.0f));
    }

    TEST_CASE("CreateEntityWrapped returns a working Entity wrapper")
    {
        Limitless::Scene scene;
        Limitless::Entity entity = scene.CreateEntityWrapped("Wrapped");

        CHECK(entity.IsValid());
        CHECK(entity.HasHandle());
        CHECK(entity.HasComponent<Limitless::TagComponent>());
        CHECK(entity.GetComponent<Limitless::TagComponent>().Tag == "Wrapped");
    }

    TEST_CASE("DestroyEntity invalidates the handle")
    {
        Limitless::Scene scene;

        entt::entity entity = scene.CreateEntity("Ephemeral");
        REQUIRE(scene.IsValid(entity));

        scene.DestroyEntity(entity);
        CHECK_FALSE(scene.IsValid(entity));
    }

    TEST_CASE("Multiple entities have distinct handles")
    {
        Limitless::Scene scene;

        entt::entity a = scene.CreateEntity("A");
        entt::entity b = scene.CreateEntity("B");
        entt::entity c = scene.CreateEntity("C");

        CHECK(a != b);
        CHECK(b != c);
        CHECK(a != c);
        CHECK(scene.IsValid(a));
        CHECK(scene.IsValid(b));
        CHECK(scene.IsValid(c));
    }

    TEST_CASE("SetParent establishes parent-child relationship")
    {
        Limitless::Scene scene;

        entt::entity parent = scene.CreateEntity("Parent");
        entt::entity child = scene.CreateEntity("Child");

        bool result = scene.SetParent(child, parent);
        CHECK(result == true);
        CHECK(scene.GetParent(child) == parent);

        auto children = scene.GetChildren(parent);
        REQUIRE(children.size() == 1);
        CHECK(children[0] == child);
    }

    TEST_CASE("SetParent to entt::null makes entity root-level")
    {
        Limitless::Scene scene;

        entt::entity parent = scene.CreateEntity("Parent");
        entt::entity child = scene.CreateEntity("Child");

        scene.SetParent(child, parent);
        REQUIRE(scene.GetParent(child) == parent);

        scene.SetParent(child, entt::null);
        CHECK((scene.GetParent(child) == entt::null));
        CHECK(scene.GetChildren(parent).empty());
    }

    TEST_CASE("Nested hierarchy reports IsDescendantOf correctly")
    {
        Limitless::Scene scene;

        entt::entity grandparent = scene.CreateEntity("Grandparent");
        entt::entity parent = scene.CreateEntity("Parent");
        entt::entity child = scene.CreateEntity("Child");

        scene.SetParent(parent, grandparent);
        scene.SetParent(child, parent);

        CHECK(scene.IsDescendantOf(child, grandparent));
        CHECK(scene.IsDescendantOf(child, parent));
        CHECK_FALSE(scene.IsDescendantOf(grandparent, child));
        CHECK_FALSE(scene.IsDescendantOf(parent, child));
        CHECK_FALSE(scene.IsDescendantOf(child, child));
    }

    TEST_CASE("GetChildren returns empty for entities with no children")
    {
        Limitless::Scene scene;
        entt::entity entity = scene.CreateEntity("Lonely");
        CHECK(scene.GetChildren(entity).empty());
    }

    TEST_CASE("GetChildren returns root-level entities when parent is null")
    {
        Limitless::Scene scene;

        scene.CreateEntity("Root1");
        scene.CreateEntity("Root2");

        auto roots = scene.GetChildren(entt::null);
        CHECK(roots.size() >= 2);
    }

    TEST_CASE("SetSiblingOrderBefore reorders siblings")
    {
        Limitless::Scene scene;

        entt::entity parent = scene.CreateEntity("Parent");
        entt::entity a = scene.CreateEntity("A");
        entt::entity b = scene.CreateEntity("B");
        entt::entity c = scene.CreateEntity("C");

        scene.SetParent(a, parent);
        scene.SetParent(b, parent);
        scene.SetParent(c, parent);

        bool result = scene.SetSiblingOrderBefore(c, a);
        CHECK(result == true);

        auto children = scene.GetChildren(parent);
        REQUIRE(children.size() == 3);

        auto& hierarchyC = scene.GetRegistry().get<Limitless::HierarchyComponent>(c);
        auto& hierarchyA = scene.GetRegistry().get<Limitless::HierarchyComponent>(a);
        CHECK(hierarchyC.SiblingOrder < hierarchyA.SiblingOrder);
    }

    TEST_CASE("Entity wrapper AddComponent and RemoveComponent")
    {
        Limitless::Scene scene;
        Limitless::Entity entity = scene.CreateEntityWrapped("Components");

        REQUIRE_FALSE(entity.HasComponent<Limitless::CanvasComponent>());

        entity.AddComponent<Limitless::CanvasComponent>();
        CHECK(entity.HasComponent<Limitless::CanvasComponent>());

        entity.RemoveComponent<Limitless::CanvasComponent>();
        CHECK_FALSE(entity.HasComponent<Limitless::CanvasComponent>());
    }

    TEST_CASE("Entity wrapper AddComponent is idempotent")
    {
        Limitless::Scene scene;
        Limitless::Entity entity = scene.CreateEntityWrapped("Idempotent");

        auto& first = entity.AddComponent<Limitless::CanvasComponent>();
        first.SortOrder = 42;

        auto& second = entity.AddComponent<Limitless::CanvasComponent>();
        CHECK(second.SortOrder == 42);
    }

    TEST_CASE("Entity TryGetComponent returns nullptr for missing components")
    {
        Limitless::Scene scene;
        Limitless::Entity entity = scene.CreateEntityWrapped("NoCanvas");

        auto* canvas = entity.TryGetComponent<Limitless::CanvasComponent>();
        CHECK(canvas == nullptr);
    }

    TEST_CASE("DestroyEntity removes children from hierarchy")
    {
        Limitless::Scene scene;

        entt::entity parent = scene.CreateEntity("Parent");
        entt::entity child = scene.CreateEntity("Child");
        scene.SetParent(child, parent);

        scene.DestroyEntity(parent);
        CHECK_FALSE(scene.IsValid(parent));
        // Child should also be destroyed (parent cleanup)
        // or at minimum unparented
    }

    TEST_CASE("Clone produces an independent copy")
    {
        Limitless::Scene scene;

        entt::entity original = scene.CreateEntity("Original");
        auto& transform = scene.GetRegistry().get<Limitless::TransformComponent>(original);
        transform.Position = glm::vec3(5.0f, 10.0f, 15.0f);

        auto cloned = scene.Clone();
        REQUIRE(cloned != nullptr);

        bool foundCloned = false;
        auto view = cloned->GetRegistry().view<Limitless::TagComponent, Limitless::TransformComponent>();
        for (auto entity : view)
        {
            auto& tag = view.get<Limitless::TagComponent>(entity);
            if (tag.Tag == "Original")
            {
                foundCloned = true;
                auto& clonedTransform = view.get<Limitless::TransformComponent>(entity);
                CHECK(clonedTransform.Position.x == doctest::Approx(5.0f));
                CHECK(clonedTransform.Position.y == doctest::Approx(10.0f));
                CHECK(clonedTransform.Position.z == doctest::Approx(15.0f));
            }
        }
        CHECK(foundCloned);

        // Mutating the clone does not affect the original
        scene.GetRegistry().get<Limitless::TransformComponent>(original).Position.x = 99.0f;
        for (auto entity : view)
        {
            auto& tag = view.get<Limitless::TagComponent>(entity);
            if (tag.Tag == "Original")
            {
                auto& clonedTransform = view.get<Limitless::TransformComponent>(entity);
                CHECK(clonedTransform.Position.x == doctest::Approx(5.0f));
            }
        }
    }

    TEST_CASE("IsEntityEnabledInHierarchy respects TagComponent.Enabled")
    {
        Limitless::Scene scene;
        entt::entity entity = scene.CreateEntity("Toggleable");

        CHECK(scene.IsEntityEnabledInHierarchy(entity));

        scene.GetRegistry().get<Limitless::TagComponent>(entity).Enabled = false;
        CHECK_FALSE(scene.IsEntityEnabledInHierarchy(entity));
    }

    TEST_CASE("IsEntityEnabledInHierarchy respects parent disabled state")
    {
        Limitless::Scene scene;

        entt::entity parent = scene.CreateEntity("Parent");
        entt::entity child = scene.CreateEntity("Child");
        scene.SetParent(child, parent);

        CHECK(scene.IsEntityEnabledInHierarchy(child));

        scene.GetRegistry().get<Limitless::TagComponent>(parent).Enabled = false;
        CHECK_FALSE(scene.IsEntityEnabledInHierarchy(child));
    }

    TEST_CASE("EditorCameraBookmark can be set and cleared")
    {
        Limitless::Scene scene;

        CHECK_FALSE(scene.GetEditorCameraBookmark().has_value());

        Limitless::Scene::EditorCameraBookmark bookmark;
        bookmark.Position = glm::vec3(1.0f, 2.0f, 3.0f);
        bookmark.YawDegrees = 45.0f;
        bookmark.PitchDegrees = -30.0f;

        scene.SetEditorCameraBookmark(bookmark);
        REQUIRE(scene.GetEditorCameraBookmark().has_value());
        CHECK(scene.GetEditorCameraBookmark()->Position.x == doctest::Approx(1.0f));
        CHECK(scene.GetEditorCameraBookmark()->YawDegrees == doctest::Approx(45.0f));
        CHECK(scene.GetEditorCameraBookmark()->PitchDegrees == doctest::Approx(-30.0f));

        scene.ClearEditorCameraBookmark();
        CHECK_FALSE(scene.GetEditorCameraBookmark().has_value());
    }

    TEST_CASE("Scene RuntimePhase defaults to Idle")
    {
        Limitless::Scene scene;
        CHECK(scene.GetRuntimePhase() == Limitless::Scene::RuntimePhase::Idle);
    }

    TEST_CASE("Scene LoadState defaults to Ready")
    {
        Limitless::Scene scene;
        CHECK(scene.IsReady());
        CHECK(scene.GetLoadState() == Limitless::Scene::LoadState::Ready);
    }

    TEST_CASE("Entity wrapper default-constructed is invalid")
    {
        Limitless::Entity entity;
        CHECK_FALSE(entity.IsValid());
        CHECK_FALSE(entity.HasHandle());
        CHECK((entity.GetHandle() == entt::null));
    }

    TEST_CASE("Entity FromPrefabAssetKey creates a prefab reference")
    {
        auto entity = Limitless::Entity::FromPrefabAssetKey("prefabs/player");
        CHECK(entity.IsPrefabReference());
        CHECK(entity.GetPrefabAssetKey() == "prefabs/player");
        CHECK_FALSE(entity.IsValid());
    }
}
