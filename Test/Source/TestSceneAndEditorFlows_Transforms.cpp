#include "TestSceneAndEditorFlowsShared.h"

using namespace SceneEditorFlowTestSupport;

TEST_SUITE("Scene And Editor Flows")
{
    TEST_CASE("Native scripts can reference other native scripts during OnCreate")
    {
        struct NativeScriptRegistryCleanup final
        {
            ~NativeScriptRegistryCleanup()
            {
                Limitless::NativeScriptRegistry::Clear();
            }
        };
        [[maybe_unused]] NativeScriptRegistryCleanup registryCleanup;

        Limitless::NativeScriptRegistry::Clear();
        Limitless::NativeScriptRegistry::RegisterScript<PrimaryScript>("PrimaryScript");
        Limitless::NativeScriptRegistry::RegisterScript<SecondaryScript>("SecondaryScript");

        Limitless::Scene scene;
        auto& registry = scene.GetRegistry();

        const entt::entity controller = scene.CreateEntity("Controller");
        const entt::entity target = scene.CreateEntity("Target");

        auto& primaryScriptEntry = AttachScriptEntry(scene, controller);
        primaryScriptEntry.ScriptClassName = "PrimaryScript";
        primaryScriptEntry.ExposedProperties["TargetEntity"] = Limitless::ScriptEntityReference{ "Target" };

        auto& selfSecondaryScriptEntry = AttachScriptEntry(scene, controller);
        selfSecondaryScriptEntry.ScriptClassName = "SecondaryScript";

        auto& targetSecondaryScriptEntry = AttachScriptEntry(scene, target);
        targetSecondaryScriptEntry.ScriptClassName = "SecondaryScript";

        scene.Update(1.0f / 60.0f);

        const auto& primaryScriptEntryAfterUpdate = GetScriptEntry(scene, controller, 0);
        REQUIRE(primaryScriptEntryAfterUpdate.RuntimeInstance != nullptr);
        auto* primary = dynamic_cast<PrimaryScript*>(primaryScriptEntryAfterUpdate.RuntimeInstance.get());
        REQUIRE(primary != nullptr);
        if (primary == nullptr)
            return;
        CHECK(primary->FoundSelfInOnCreate);
        CHECK(primary->FoundTargetInOnCreate);
        CHECK(primary->FoundByNameOnSelfInOnCreate);

        auto* selfSecondary = dynamic_cast<SecondaryScript*>(GetScriptEntry(scene, controller, 1).RuntimeInstance.get());
        auto* targetSecondary = dynamic_cast<SecondaryScript*>(GetScriptEntry(scene, target, 0).RuntimeInstance.get());
        REQUIRE(selfSecondary != nullptr);
        if (selfSecondary == nullptr)
            return;
        REQUIRE(targetSecondary != nullptr);
        if (targetSecondary == nullptr)
            return;
        CHECK(selfSecondary->ReceivedPing);
        CHECK(targetSecondary->ReceivedPing);
    }

    TEST_CASE("Depth-batched transform updates keep hierarchy depth and world transforms deterministic")
    {
        Limitless::Scene scene;
        auto& registry = scene.GetRegistry();

        const entt::entity root = scene.CreateEntity("RootDepth");
        const entt::entity childA = scene.CreateEntity("ChildA");
        const entt::entity childB = scene.CreateEntity("ChildB");
        const entt::entity childC = scene.CreateEntity("ChildC");

        REQUIRE(scene.SetParent(childA, root));
        REQUIRE(scene.SetParent(childB, childA));
        REQUIRE(scene.SetParent(childC, childB));

        registry.get<Limitless::TransformComponent>(root).Position = { 1.0f, 0.0f, 0.0f };
        registry.get<Limitless::TransformComponent>(childA).Position = { 2.0f, 0.0f, 0.0f };
        registry.get<Limitless::TransformComponent>(childB).Position = { 3.0f, 0.0f, 0.0f };
        registry.get<Limitless::TransformComponent>(childC).Position = { -1.0f, 0.0f, 0.0f };

        scene.UpdateTransforms();

        const auto& rootHierarchy = registry.get<Limitless::HierarchyComponent>(root);
        const auto& childAHierarchy = registry.get<Limitless::HierarchyComponent>(childA);
        const auto& childBHierarchy = registry.get<Limitless::HierarchyComponent>(childB);
        const auto& childCHierarchy = registry.get<Limitless::HierarchyComponent>(childC);
        CHECK(rootHierarchy.HierarchyDepth == 0);
        CHECK(childAHierarchy.HierarchyDepth == 1);
        CHECK(childBHierarchy.HierarchyDepth == 2);
        CHECK(childCHierarchy.HierarchyDepth == 3);

        CHECK(ExtractWorldPosition(registry.get<Limitless::TransformComponent>(root)).x == doctest::Approx(1.0f));
        CHECK(ExtractWorldPosition(registry.get<Limitless::TransformComponent>(childA)).x == doctest::Approx(3.0f));
        CHECK(ExtractWorldPosition(registry.get<Limitless::TransformComponent>(childB)).x == doctest::Approx(6.0f));
        CHECK(ExtractWorldPosition(registry.get<Limitless::TransformComponent>(childC)).x == doctest::Approx(5.0f));

        for (int iteration = 0; iteration < 3; ++iteration)
        {
            scene.UpdateTransforms();
            CHECK(ExtractWorldPosition(registry.get<Limitless::TransformComponent>(childC)).x == doctest::Approx(5.0f));
        }

        auto& rootTransform = registry.get<Limitless::TransformComponent>(root);
        rootTransform.Position.x = 10.0f;
        scene.MarkTransformDirty(root);
        scene.UpdateTransforms();

        CHECK(ExtractWorldPosition(registry.get<Limitless::TransformComponent>(root)).x == doctest::Approx(10.0f));
        CHECK(ExtractWorldPosition(registry.get<Limitless::TransformComponent>(childA)).x == doctest::Approx(12.0f));
        CHECK(ExtractWorldPosition(registry.get<Limitless::TransformComponent>(childB)).x == doctest::Approx(15.0f));
        CHECK(ExtractWorldPosition(registry.get<Limitless::TransformComponent>(childC)).x == doctest::Approx(14.0f));
    }

    TEST_CASE("Parent dirty propagation updates child world transform in same pass")
    {
        Limitless::Scene scene;
        auto& registry = scene.GetRegistry();

        const entt::entity parent = scene.CreateEntity("PropagationParent");
        const entt::entity child = scene.CreateEntity("PropagationChild");
        REQUIRE(scene.SetParent(child, parent));

        auto& parentTransform = registry.get<Limitless::TransformComponent>(parent);
        auto& childTransform = registry.get<Limitless::TransformComponent>(child);
        parentTransform.Position = { 2.0f, 0.0f, 0.0f };
        childTransform.Position = { 3.0f, 0.0f, 0.0f };

        scene.UpdateTransforms();
        CHECK(ExtractWorldPosition(childTransform).x == doctest::Approx(5.0f));

        parentTransform.Position.x = 7.0f;
        scene.MarkTransformDirty(parent);
        childTransform.LocalDirty = false;
        childTransform.Dirty = false;
        childTransform.RuntimeWorldUpdatedThisFrame = false;

        scene.UpdateTransforms();

        CHECK(ExtractWorldPosition(childTransform).x == doctest::Approx(10.0f));
        CHECK(childTransform.RuntimeWorldUpdatedThisFrame == true);
    }

    TEST_CASE("SetParent with singular parent transform keeps child local transform finite")
    {
        Limitless::Scene scene;
        auto& registry = scene.GetRegistry();

        const entt::entity parent = scene.CreateEntity("SingularParent");
        const entt::entity child = scene.CreateEntity("SingularChild");

        auto& parentTransform = registry.get<Limitless::TransformComponent>(parent);
        parentTransform.Position = { 4.0f, -3.0f, 0.0f };
        parentTransform.Rotation = { 0.0f, 0.0f, 25.0f };
        parentTransform.Scale = { 0.0f, 2.0f, 1.0f };

        auto& childTransform = registry.get<Limitless::TransformComponent>(child);
        childTransform.Position = { 1.0f, 2.0f, 3.0f };
        childTransform.Rotation = { 10.0f, -15.0f, 20.0f };
        childTransform.Scale = { 1.5f, 0.75f, 1.0f };

        scene.UpdateTransforms();
        REQUIRE(scene.SetParent(child, parent));

        const auto& updatedChildTransform = registry.get<Limitless::TransformComponent>(child);
        CHECK(std::isfinite(updatedChildTransform.Position.x));
        CHECK(std::isfinite(updatedChildTransform.Position.y));
        CHECK(std::isfinite(updatedChildTransform.Position.z));
        CHECK(std::isfinite(updatedChildTransform.Rotation.x));
        CHECK(std::isfinite(updatedChildTransform.Rotation.y));
        CHECK(std::isfinite(updatedChildTransform.Rotation.z));
        CHECK(std::isfinite(updatedChildTransform.Scale.x));
        CHECK(std::isfinite(updatedChildTransform.Scale.y));
        CHECK(std::isfinite(updatedChildTransform.Scale.z));
    }
}
