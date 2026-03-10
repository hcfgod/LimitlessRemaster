#include "TestSceneAndEditorFlowsShared.h"

using namespace SceneEditorFlowTestSupport;

TEST_SUITE("Scene And Editor Flows")
{
    TEST_CASE("Runtime contact callbacks dispatch in deterministic pair order")
    {
        struct NativeScriptRegistryCleanup final
        {
            ~NativeScriptRegistryCleanup()
            {
                Limitless::NativeScriptRegistry::Clear();
            }
        };
        [[maybe_unused]] NativeScriptRegistryCleanup registryCleanup;

        const bool wasExecutionBlocked = Limitless::NativeScriptRegistry::IsExecutionBlocked();
        struct NativeScriptExecutionBlockedRestore final
        {
            bool PreviousBlocked = false;
            ~NativeScriptExecutionBlockedRestore()
            {
                Limitless::NativeScriptRegistry::SetExecutionBlocked(PreviousBlocked);
            }
        } executionBlockedRestore{ wasExecutionBlocked };

        Limitless::NativeScriptRegistry::SetExecutionBlocked(false);
        Limitless::NativeScriptRegistry::Clear();
        Limitless::NativeScriptRegistry::RegisterScript<ContactOrderRecordingScript>("ContactOrderRecordingScript");

        Limitless::Scene scene;
        auto& registry = scene.GetRegistry();
        Limitless::Physics2DWorldSettings settings = scene.GetPhysics2DSettings();
        settings.WorldCount = 1;
        settings.Gravity = { 0.0f, 0.0f };
        scene.SetPhysics2DSettings(settings);

        const auto attachScript = [&](entt::entity entity) {
            auto& scriptEntry = AttachScriptEntry(scene, entity);
            scriptEntry.ScriptClassName = "ContactOrderRecordingScript";
        };

        const entt::entity triggerB = scene.CreateEntity("TriggerB");
        const entt::entity visitor = scene.CreateEntity("Visitor");
        const entt::entity triggerA = scene.CreateEntity("TriggerA");

        attachScript(triggerA);
        attachScript(triggerB);
        attachScript(visitor);

        auto& visitorTransform = registry.get<Limitless::TransformComponent>(visitor);
        visitorTransform.Position = { 10.0f, 0.0f, 0.0f };
        auto& visitorBody = registry.emplace<Limitless::Rigidbody2DComponent>(visitor);
        visitorBody.Type = Limitless::Rigidbody2DComponent::BodyType::Dynamic;
        auto& visitorCollider = registry.emplace<Limitless::BoxCollider2DComponent>(visitor);
        visitorCollider.Size = { 5.0f, 5.0f };

        auto& triggerATransform = registry.get<Limitless::TransformComponent>(triggerA);
        triggerATransform.Position = { -1.0f, 0.0f, 0.0f };
        auto& triggerABody = registry.emplace<Limitless::Rigidbody2DComponent>(triggerA);
        triggerABody.Type = Limitless::Rigidbody2DComponent::BodyType::Static;
        auto& triggerACollider = registry.emplace<Limitless::BoxCollider2DComponent>(triggerA);
        triggerACollider.Size = { 0.8f, 0.8f };
        triggerACollider.IsSensor = false;

        auto& triggerBTransform = registry.get<Limitless::TransformComponent>(triggerB);
        triggerBTransform.Position = { 1.0f, 0.0f, 0.0f };
        auto& triggerBBody = registry.emplace<Limitless::Rigidbody2DComponent>(triggerB);
        triggerBBody.Type = Limitless::Rigidbody2DComponent::BodyType::Static;
        auto& triggerBCollider = registry.emplace<Limitless::BoxCollider2DComponent>(triggerB);
        triggerBCollider.Size = { 0.8f, 0.8f };
        triggerBCollider.IsSensor = false;

        constexpr float kFixedStep = 1.0f / 60.0f;
        scene.Update(kFixedStep);
        scene.StepPhysics2D(kFixedStep);
        ContactOrderRecordingScript::ResetEvents();

        visitorTransform.Position = { 0.0f, 0.0f, 0.0f };
        scene.MarkTransformDirty(visitor);
        constexpr size_t kExpectedEventCount = 4;
        constexpr int kMaxEventSettleSteps = 3;
        for (int stepIndex = 0; stepIndex < kMaxEventSettleSteps; ++stepIndex)
        {
            scene.StepPhysics2D(kFixedStep);
            const std::vector<std::string> stepEvents = ContactOrderRecordingScript::GetEventsSnapshot();
            if (stepEvents.size() >= kExpectedEventCount)
                break;
        }

        auto makePairKey = [](entt::entity entityA, entt::entity entityB) {
            Limitless::RuntimeContactPairKey key{};
            key.EntityA = std::min(static_cast<uint32_t>(entityA), static_cast<uint32_t>(entityB));
            key.EntityB = std::max(static_cast<uint32_t>(entityA), static_cast<uint32_t>(entityB));
            key.WorldSlot = 0;
            key.IsSensor = false;
            return key;
        };

        std::vector<Limitless::RuntimeContactPairKey> expectedPairs;
        expectedPairs.push_back(makePairKey(triggerA, visitor));
        expectedPairs.push_back(makePairKey(triggerB, visitor));
        std::sort(expectedPairs.begin(), expectedPairs.end(), [](const Limitless::RuntimeContactPairKey& left,
                                                                 const Limitless::RuntimeContactPairKey& right) {
            if (left.WorldSlot != right.WorldSlot)
                return left.WorldSlot < right.WorldSlot;
            if (left.IsSensor != right.IsSensor)
                return left.IsSensor < right.IsSensor;
            if (left.EntityA != right.EntityA)
                return left.EntityA < right.EntityA;
            return left.EntityB < right.EntityB;
        });

        auto getTagFor = [&](entt::entity entity) -> std::string {
            return registry.get<Limitless::TagComponent>(entity).Tag;
        };

        std::vector<std::string> expectedOrder;
        expectedOrder.reserve(expectedPairs.size() * 2);
        for (const auto& pair : expectedPairs)
        {
            const entt::entity entityA = static_cast<entt::entity>(pair.EntityA);
            const entt::entity entityB = static_cast<entt::entity>(pair.EntityB);
            expectedOrder.push_back(getTagFor(entityA) + "->" + getTagFor(entityB));
            expectedOrder.push_back(getTagFor(entityB) + "->" + getTagFor(entityA));
        }

        const std::vector<std::string> actualOrder = ContactOrderRecordingScript::GetEventsSnapshot();
        REQUIRE(actualOrder.size() == expectedOrder.size());
        for (size_t eventIndex = 0; eventIndex < expectedOrder.size(); ++eventIndex)
            CHECK(actualOrder[eventIndex] == expectedOrder[eventIndex]);
    }

    TEST_CASE("Parallel physics world stepping matches sequential stepping for isolated worlds")
    {
        auto& config = Limitless::ConfigManager::GetInstance();
        const bool previousParallelWorldStepping = config.GetValue<bool>("ecs.mt.enable_parallel_physics_world_step", true);
        struct ConfigRestore final
        {
            Limitless::ConfigManager& Config;
            bool PreviousParallelWorldStepping;
            ~ConfigRestore()
            {
                Config.SetValue("ecs.mt.enable_parallel_physics_world_step", PreviousParallelWorldStepping);
            }
        } configRestore{ config, previousParallelWorldStepping };

        auto& jobSystem = Limitless::Concurrency::GetJobSystem();
        const bool wasJobSystemInitialized = jobSystem.IsInitialized();
        if (!wasJobSystemInitialized)
            jobSystem.Initialize(2);
        struct JobSystemRestore final
        {
            Limitless::Concurrency::JobSystem& JobSystem;
            bool WasInitialized;
            ~JobSystemRestore()
            {
                if (!WasInitialized && JobSystem.IsInitialized())
                    JobSystem.Shutdown();
            }
        } jobSystemRestore{ jobSystem, wasJobSystemInitialized };

        Limitless::Scene baseScene;
        auto& baseRegistry = baseScene.GetRegistry();

        Limitless::Physics2DWorldSettings settings = baseScene.GetPhysics2DSettings();
        settings.WorldCount = 2;
        settings.Gravity = { 0.0f, -9.81f };
        settings.VelocitySubSteps = 8;
        baseScene.SetPhysics2DSettings(settings);

        const entt::entity worldZeroBody = baseScene.CreateEntity("WorldZeroBody");
        auto& worldZeroTransform = baseRegistry.get<Limitless::TransformComponent>(worldZeroBody);
        worldZeroTransform.Position = { -1.0f, 5.0f, 0.0f };
        auto& worldZeroRigidbody = baseRegistry.emplace<Limitless::Rigidbody2DComponent>(worldZeroBody);
        worldZeroRigidbody.Type = Limitless::Rigidbody2DComponent::BodyType::Dynamic;
        worldZeroRigidbody.PhysicsWorldSlot = 0;
        baseRegistry.emplace<Limitless::BoxCollider2DComponent>(worldZeroBody);

        const entt::entity worldOneBody = baseScene.CreateEntity("WorldOneBody");
        auto& worldOneTransform = baseRegistry.get<Limitless::TransformComponent>(worldOneBody);
        worldOneTransform.Position = { 1.0f, 6.0f, 0.0f };
        auto& worldOneRigidbody = baseRegistry.emplace<Limitless::Rigidbody2DComponent>(worldOneBody);
        worldOneRigidbody.Type = Limitless::Rigidbody2DComponent::BodyType::Dynamic;
        worldOneRigidbody.PhysicsWorldSlot = 1;
        baseRegistry.emplace<Limitless::BoxCollider2DComponent>(worldOneBody);

        auto sequentialScene = baseScene.Clone();
        auto parallelScene = baseScene.Clone();
        REQUIRE(sequentialScene != nullptr);
        REQUIRE(parallelScene != nullptr);

        constexpr float kFixedStep = 1.0f / 60.0f;
        constexpr int kStepCount = 24;

        config.SetValue("ecs.mt.enable_parallel_physics_world_step", false);
        for (int stepIndex = 0; stepIndex < kStepCount; ++stepIndex)
            sequentialScene->StepPhysics2D(kFixedStep);

        config.SetValue("ecs.mt.enable_parallel_physics_world_step", true);
        for (int stepIndex = 0; stepIndex < kStepCount; ++stepIndex)
            parallelScene->StepPhysics2D(kFixedStep);

        const entt::entity sequentialWorldZeroBody = FindEntityByTag(*sequentialScene, "WorldZeroBody");
        const entt::entity sequentialWorldOneBody = FindEntityByTag(*sequentialScene, "WorldOneBody");
        const entt::entity parallelWorldZeroBody = FindEntityByTag(*parallelScene, "WorldZeroBody");
        const entt::entity parallelWorldOneBody = FindEntityByTag(*parallelScene, "WorldOneBody");
        REQUIRE_FALSE(IsNullEntity(sequentialWorldZeroBody));
        REQUIRE_FALSE(IsNullEntity(sequentialWorldOneBody));
        REQUIRE_FALSE(IsNullEntity(parallelWorldZeroBody));
        REQUIRE_FALSE(IsNullEntity(parallelWorldOneBody));

        const auto& sequentialRegistry = sequentialScene->GetRegistry();
        const auto& parallelRegistry = parallelScene->GetRegistry();
        const float sequentialWorldZeroY = sequentialRegistry.get<Limitless::TransformComponent>(sequentialWorldZeroBody).Position.y;
        const float sequentialWorldOneY = sequentialRegistry.get<Limitless::TransformComponent>(sequentialWorldOneBody).Position.y;
        const float parallelWorldZeroY = parallelRegistry.get<Limitless::TransformComponent>(parallelWorldZeroBody).Position.y;
        const float parallelWorldOneY = parallelRegistry.get<Limitless::TransformComponent>(parallelWorldOneBody).Position.y;

        CHECK(std::isfinite(sequentialWorldZeroY));
        CHECK(std::isfinite(sequentialWorldOneY));
        CHECK(std::isfinite(parallelWorldZeroY));
        CHECK(std::isfinite(parallelWorldOneY));
        CHECK(parallelWorldZeroY == doctest::Approx(sequentialWorldZeroY).epsilon(0.0001f));
        CHECK(parallelWorldOneY == doctest::Approx(sequentialWorldOneY).epsilon(0.0001f));
    }

    TEST_CASE("DestroyEntity keeps unrelated runtime bodies alive in same physics world slot")
    {
        Limitless::Scene scene;
        auto& registry = scene.GetRegistry();

        Limitless::Physics2DWorldSettings settings = scene.GetPhysics2DSettings();
        settings.WorldCount = 2;
        settings.Gravity = { 0.0f, -9.81f };
        scene.SetPhysics2DSettings(settings);

        const entt::entity survivor = scene.CreateEntity("Survivor");
        auto& survivorTransform = registry.get<Limitless::TransformComponent>(survivor);
        survivorTransform.Position = { -0.5f, 3.0f, 0.0f };
        auto& survivorBody = registry.emplace<Limitless::Rigidbody2DComponent>(survivor);
        survivorBody.Type = Limitless::Rigidbody2DComponent::BodyType::Dynamic;
        survivorBody.PhysicsWorldSlot = 0;
        registry.emplace<Limitless::BoxCollider2DComponent>(survivor);

        const entt::entity victim = scene.CreateEntity("Victim");
        auto& victimTransform = registry.get<Limitless::TransformComponent>(victim);
        victimTransform.Position = { 0.75f, 4.0f, 0.0f };
        auto& victimBody = registry.emplace<Limitless::Rigidbody2DComponent>(victim);
        victimBody.Type = Limitless::Rigidbody2DComponent::BodyType::Dynamic;
        victimBody.PhysicsWorldSlot = 0;
        registry.emplace<Limitless::BoxCollider2DComponent>(victim);

        scene.StepPhysics2D(1.0f / 60.0f);

        CHECK(registry.get<Limitless::Rigidbody2DComponent>(survivor).RuntimeBodyCreated == true);
        CHECK(registry.get<Limitless::Rigidbody2DComponent>(victim).RuntimeBodyCreated == true);

        CHECK_NOTHROW(scene.DestroyEntity(victim));
        CHECK(scene.IsValid(survivor));
        CHECK_FALSE(scene.IsValid(victim));

        const auto& survivorAfterDestroy = registry.get<Limitless::Rigidbody2DComponent>(survivor);
        CHECK(survivorAfterDestroy.RuntimeBodyCreated == true);
        CHECK(survivorAfterDestroy.RuntimeWorldSlot == 0);
    }

    TEST_CASE("DestroyEntity does not throw when script OnDestroy throws")
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
        Limitless::NativeScriptRegistry::RegisterScript<ThrowingOnDestroyScript>("ThrowingOnDestroyScript");
        ThrowingOnDestroyScript::ResetCounters();

        Limitless::Scene scene;
        auto& registry = scene.GetRegistry();
        const entt::entity entity = scene.CreateEntity("ThrowingOnDestroyEntity");

        auto& scriptEntry = AttachScriptEntry(scene, entity);
        scriptEntry.ScriptClassName = "ThrowingOnDestroyScript";

        scene.Update(1.0f / 60.0f);
        REQUIRE(scriptEntry.RuntimeInstance != nullptr);
        REQUIRE(scriptEntry.RuntimeInitialized == true);

        CHECK_NOTHROW(scene.DestroyEntity(entity));
        CHECK_FALSE(scene.IsValid(entity));
        CHECK(ThrowingOnDestroyScript::DestroyCallCount.load(std::memory_order_relaxed) >= 1);
    }

    TEST_CASE("Scene destructor does not throw when script OnDestroy throws")
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
        Limitless::NativeScriptRegistry::RegisterScript<ThrowingOnDestroyScript>("ThrowingOnDestroyScript");
        ThrowingOnDestroyScript::ResetCounters();

        auto scene = std::make_unique<Limitless::Scene>();
        auto& registry = scene->GetRegistry();
        const entt::entity entity = scene->CreateEntity("ThrowingOnDestroyOnSceneDestruct");

        auto& scriptEntry = AttachScriptEntry(*scene, entity);
        scriptEntry.ScriptClassName = "ThrowingOnDestroyScript";

        scene->Update(1.0f / 60.0f);
        REQUIRE(scriptEntry.RuntimeInstance != nullptr);
        REQUIRE(scriptEntry.RuntimeInitialized == true);

        CHECK_NOTHROW(scene.reset());
        CHECK(ThrowingOnDestroyScript::DestroyCallCount.load(std::memory_order_relaxed) >= 1);
    }

    TEST_CASE("Scene destructor tolerates script OnDestroy structural mutation")
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
        Limitless::NativeScriptRegistry::RegisterScript<DestroySelfOnDestroyScript>("DestroySelfOnDestroyScript");
        DestroySelfOnDestroyScript::ResetCounters();

        auto scene = std::make_unique<Limitless::Scene>();
        auto& registry = scene->GetRegistry();
        const entt::entity entity = scene->CreateEntity("DestroySelfOnDestroyEntity");
        auto& scriptEntry = AttachScriptEntry(*scene, entity);
        scriptEntry.ScriptClassName = "DestroySelfOnDestroyScript";

        scene->Update(1.0f / 60.0f);
        REQUIRE(scriptEntry.RuntimeInstance != nullptr);
        REQUIRE(scriptEntry.RuntimeInitialized == true);

        CHECK_NOTHROW(scene.reset());
        CHECK(DestroySelfOnDestroyScript::DestroyCallCount.load(std::memory_order_relaxed) >= 1);
    }
}
