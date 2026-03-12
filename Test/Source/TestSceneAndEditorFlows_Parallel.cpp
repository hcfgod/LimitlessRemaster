#include "TestSceneAndEditorFlowsShared.h"

using namespace SceneEditorFlowTestSupport;

TEST_SUITE("Scene And Editor Flows")
{
    TEST_CASE("Parallel-safe scripts defer structural mutations and apply them in structural phase")
    {
        struct NativeScriptRegistryCleanup final
        {
            ~NativeScriptRegistryCleanup()
            {
                Limitless::NativeScriptRegistry::Clear();
            }
        };
        [[maybe_unused]] NativeScriptRegistryCleanup registryCleanup;

        auto& config = Limitless::ConfigManager::GetInstance();
        const bool previousEnableParallelScripts = config.GetValue<bool>("ecs.mt.enable_parallel_scripts", true);
        const bool previousDeferStructuralMutations = config.GetValue<bool>("ecs.mt.defer_structural_mutations", true);
        const bool previousRequireAccessDeclarations = config.GetValue<bool>("ecs.mt.require_parallel_script_access_declarations", true);
        const bool previousWarnImplicitAccess = config.GetValue<bool>("ecs.mt.warn_implicit_parallel_script_access", true);

        struct ConfigRestore final
        {
            Limitless::ConfigManager& Config;
            bool EnableParallelScripts;
            bool DeferStructuralMutations;
            bool RequireAccessDeclarations;
            bool WarnImplicitAccess;

            ~ConfigRestore()
            {
                Config.SetValue("ecs.mt.enable_parallel_scripts", EnableParallelScripts);
                Config.SetValue("ecs.mt.defer_structural_mutations", DeferStructuralMutations);
                Config.SetValue("ecs.mt.require_parallel_script_access_declarations", RequireAccessDeclarations);
                Config.SetValue("ecs.mt.warn_implicit_parallel_script_access", WarnImplicitAccess);
            }
        } configRestore{
            config,
            previousEnableParallelScripts,
            previousDeferStructuralMutations,
            previousRequireAccessDeclarations,
            previousWarnImplicitAccess
        };

        config.SetValue("ecs.mt.enable_parallel_scripts", true);
        config.SetValue("ecs.mt.defer_structural_mutations", true);
        config.SetValue("ecs.mt.require_parallel_script_access_declarations", true);
        config.SetValue("ecs.mt.warn_implicit_parallel_script_access", false);

        Limitless::NativeScriptRegistry::Clear();
        Limitless::NativeScriptRegistry::RegisterScript<ParallelSpawnScript>("ParallelSpawnScript");
        Limitless::NativeScriptRegistry::RegisterScript<ParallelDestroyScript>("ParallelDestroyScript");
        Limitless::NativeScriptRegistry::RegisterScript<ParallelSpawnThenDestroyScript>("ParallelSpawnThenDestroyScript");

        Limitless::Scene scene;
        auto& registry = scene.GetRegistry();

        const entt::entity scriptHost = scene.CreateEntity("ScriptHost");
        const entt::entity victim = scene.CreateEntity("Victim");

        auto& spawnScriptEntry = AttachScriptEntry(scene, scriptHost);
        spawnScriptEntry.ScriptClassName = "ParallelSpawnScript";
        spawnScriptEntry.ExecutionPolicy = Limitless::ScriptExecutionPolicy::ParallelSafe;
        spawnScriptEntry.DeclaredWriteAccessMask = Limitless::ToAccessMask(Limitless::SceneSystemAccessComponent::Transform);

        auto& destroyScriptEntry = AttachScriptEntry(scene, scriptHost);
        destroyScriptEntry.ScriptClassName = "ParallelDestroyScript";
        destroyScriptEntry.ExecutionPolicy = Limitless::ScriptExecutionPolicy::ParallelSafe;
        destroyScriptEntry.DeclaredWriteAccessMask = Limitless::ToAccessMask(Limitless::SceneSystemAccessComponent::Transform);
        destroyScriptEntry.ExposedProperties["TargetEntity"] = Limitless::ScriptEntityReference{ "Victim" };

        auto& spawnThenDestroyScriptEntry = AttachScriptEntry(scene, scriptHost);
        spawnThenDestroyScriptEntry.ScriptClassName = "ParallelSpawnThenDestroyScript";
        spawnThenDestroyScriptEntry.ExecutionPolicy = Limitless::ScriptExecutionPolicy::ParallelSafe;
        spawnThenDestroyScriptEntry.DeclaredWriteAccessMask = Limitless::ToAccessMask(Limitless::SceneSystemAccessComponent::Transform);

        scene.Update(1.0f / 60.0f);

        CHECK_FALSE(IsNullEntity(FindEntityByTag(scene, "ParallelSpawned")));
        CHECK(IsNullEntity(FindEntityByTag(scene, "ParallelSpawnedThenDestroyed")));
        CHECK(scene.IsValid(victim) == false);
        const auto& spawnScriptEntryAfterUpdate = GetScriptEntry(scene, scriptHost, 0);
        const auto& destroyScriptEntryAfterUpdate = GetScriptEntry(scene, scriptHost, 1);
        const auto& spawnThenDestroyScriptEntryAfterUpdate = GetScriptEntry(scene, scriptHost, 2);
        REQUIRE(spawnScriptEntryAfterUpdate.RuntimeInstance != nullptr);
        REQUIRE(destroyScriptEntryAfterUpdate.RuntimeInstance != nullptr);
        REQUIRE(spawnThenDestroyScriptEntryAfterUpdate.RuntimeInstance != nullptr);
        auto* spawnThenDestroyScript = dynamic_cast<ParallelSpawnThenDestroyScript*>(spawnThenDestroyScriptEntryAfterUpdate.RuntimeInstance.get());
        REQUIRE(spawnThenDestroyScript != nullptr);
        if (spawnThenDestroyScript == nullptr)
            return;
        CHECK(spawnThenDestroyScript->ReceivedNonNullDeferredHandle);
        CHECK(spawnScriptEntryAfterUpdate.RuntimeUpdateCount >= 1);
        CHECK(destroyScriptEntryAfterUpdate.RuntimeUpdateCount >= 1);
        CHECK(spawnThenDestroyScriptEntryAfterUpdate.RuntimeUpdateCount >= 1);
    }

    TEST_CASE("Deferred structural mutation flush respects budget and carries work to later flushes")
    {
        auto& config = Limitless::ConfigManager::GetInstance();
        const uint32_t previousFlushBudget = config.GetValue<uint32_t>(
            "ecs.mt.deferred_structural_mutation_flush_budget",
            1024u);

        struct ConfigRestore final
        {
            Limitless::ConfigManager& Config;
            uint32_t FlushBudget;

            ~ConfigRestore()
            {
                Config.SetValue("ecs.mt.deferred_structural_mutation_flush_budget", FlushBudget);
            }
        } configRestore{
            config,
            previousFlushBudget
        };

        constexpr uint32_t kFlushBudget = 8;
        constexpr int kTotalCascadeMutations = 37;
        config.SetValue("ecs.mt.deferred_structural_mutation_flush_budget", kFlushBudget);

        Limitless::Scene scene;
        int appliedMutations = 0;
        std::function<void(Limitless::Scene&)> cascadingMutation;
        cascadingMutation = [&](Limitless::Scene& mutableScene) {
            ++appliedMutations;
            if (appliedMutations < kTotalCascadeMutations)
                (void)mutableScene.EnqueueDeferredStructuralMutation(cascadingMutation, "CascadingBudgetStressMutation");
        };

        (void)scene.EnqueueDeferredStructuralMutation(cascadingMutation, "CascadingBudgetStressSeedMutation");

        scene.FlushDeferredStructuralMutations();
        CHECK(appliedMutations == static_cast<int>(kFlushBudget));

        int flushCalls = 1;
        constexpr int kMaxFlushCalls = 64;
        while (appliedMutations < kTotalCascadeMutations && flushCalls < kMaxFlushCalls)
        {
            scene.FlushDeferredStructuralMutations();
            ++flushCalls;
        }

        CHECK(appliedMutations == kTotalCascadeMutations);
        CHECK(flushCalls > 1);
        CHECK(flushCalls <= kMaxFlushCalls);
    }

    TEST_CASE("Parallel-safe transform writes without declared write mask are flagged")
    {
        struct NativeScriptRegistryCleanup final
        {
            ~NativeScriptRegistryCleanup()
            {
                Limitless::NativeScriptRegistry::Clear();
            }
        };
        [[maybe_unused]] NativeScriptRegistryCleanup registryCleanup;

        auto& config = Limitless::ConfigManager::GetInstance();
        const bool previousEnableParallelScripts = config.GetValue<bool>("ecs.mt.enable_parallel_scripts", true);
        const bool previousRequireAccessDeclarations = config.GetValue<bool>("ecs.mt.require_parallel_script_access_declarations", true);
        const bool previousWarnImplicitAccess = config.GetValue<bool>("ecs.mt.warn_implicit_parallel_script_access", true);
        const bool previousValidateAccessMasks = config.GetValue<bool>("ecs.mt.validate_parallel_script_access_masks", true);
        const bool previousWarnAccessMaskMismatch = config.GetValue<bool>("ecs.mt.warn_parallel_script_access_mismatch", true);

        struct ConfigRestore final
        {
            Limitless::ConfigManager& Config;
            bool EnableParallelScripts;
            bool RequireAccessDeclarations;
            bool WarnImplicitAccess;
            bool ValidateAccessMasks;
            bool WarnAccessMaskMismatch;

            ~ConfigRestore()
            {
                Config.SetValue("ecs.mt.enable_parallel_scripts", EnableParallelScripts);
                Config.SetValue("ecs.mt.require_parallel_script_access_declarations", RequireAccessDeclarations);
                Config.SetValue("ecs.mt.warn_implicit_parallel_script_access", WarnImplicitAccess);
                Config.SetValue("ecs.mt.validate_parallel_script_access_masks", ValidateAccessMasks);
                Config.SetValue("ecs.mt.warn_parallel_script_access_mismatch", WarnAccessMaskMismatch);
            }
        } configRestore{
            config,
            previousEnableParallelScripts,
            previousRequireAccessDeclarations,
            previousWarnImplicitAccess,
            previousValidateAccessMasks,
            previousWarnAccessMaskMismatch
        };

        config.SetValue("ecs.mt.enable_parallel_scripts", true);
        config.SetValue("ecs.mt.require_parallel_script_access_declarations", true);
        config.SetValue("ecs.mt.warn_implicit_parallel_script_access", false);
        config.SetValue("ecs.mt.validate_parallel_script_access_masks", true);
        config.SetValue("ecs.mt.warn_parallel_script_access_mismatch", true);

        Limitless::NativeScriptRegistry::Clear();
        Limitless::NativeScriptRegistry::RegisterScript<ParallelTransformMutateScript>("ParallelTransformMutateScript");

        Limitless::Scene scene;
        auto& registry = scene.GetRegistry();
        const entt::entity scriptHost = scene.CreateEntity("ParallelMaskMismatchHost");
        auto& scriptEntry = AttachScriptEntry(scene, scriptHost);
        scriptEntry.ScriptClassName = "ParallelTransformMutateScript";
        scriptEntry.ExecutionPolicy = Limitless::ScriptExecutionPolicy::ParallelSafe;
        scriptEntry.DeclaredReadAccessMask = Limitless::ToAccessMask(Limitless::SceneSystemAccessComponent::Transform);
        scriptEntry.DeclaredWriteAccessMask = 0;

        auto& transform = registry.get<Limitless::TransformComponent>(scriptHost);
        const float initialX = transform.Position.x;

        scene.Update(1.0f / 60.0f);

        CHECK(transform.Position.x > initialX);
        CHECK(scriptEntry.RuntimeWarnedAccessMaskMismatch == true);
        CHECK(scriptEntry.RuntimeUpdateCount >= 1);
    }

    TEST_CASE("Parallel-safe rigidbody writes without declared write mask are flagged")
    {
        struct NativeScriptRegistryCleanup final
        {
            ~NativeScriptRegistryCleanup()
            {
                Limitless::NativeScriptRegistry::Clear();
            }
        };
        [[maybe_unused]] NativeScriptRegistryCleanup registryCleanup;

        auto& config = Limitless::ConfigManager::GetInstance();
        const bool previousEnableParallelScripts = config.GetValue<bool>("ecs.mt.enable_parallel_scripts", true);
        const bool previousRequireAccessDeclarations = config.GetValue<bool>("ecs.mt.require_parallel_script_access_declarations", true);
        const bool previousWarnImplicitAccess = config.GetValue<bool>("ecs.mt.warn_implicit_parallel_script_access", true);
        const bool previousValidateAccessMasks = config.GetValue<bool>("ecs.mt.validate_parallel_script_access_masks", true);
        const bool previousWarnAccessMaskMismatch = config.GetValue<bool>("ecs.mt.warn_parallel_script_access_mismatch", true);

        struct ConfigRestore final
        {
            Limitless::ConfigManager& Config;
            bool EnableParallelScripts;
            bool RequireAccessDeclarations;
            bool WarnImplicitAccess;
            bool ValidateAccessMasks;
            bool WarnAccessMaskMismatch;

            ~ConfigRestore()
            {
                Config.SetValue("ecs.mt.enable_parallel_scripts", EnableParallelScripts);
                Config.SetValue("ecs.mt.require_parallel_script_access_declarations", RequireAccessDeclarations);
                Config.SetValue("ecs.mt.warn_implicit_parallel_script_access", WarnImplicitAccess);
                Config.SetValue("ecs.mt.validate_parallel_script_access_masks", ValidateAccessMasks);
                Config.SetValue("ecs.mt.warn_parallel_script_access_mismatch", WarnAccessMaskMismatch);
            }
        } configRestore{
            config,
            previousEnableParallelScripts,
            previousRequireAccessDeclarations,
            previousWarnImplicitAccess,
            previousValidateAccessMasks,
            previousWarnAccessMaskMismatch
        };

        config.SetValue("ecs.mt.enable_parallel_scripts", true);
        config.SetValue("ecs.mt.require_parallel_script_access_declarations", true);
        config.SetValue("ecs.mt.warn_implicit_parallel_script_access", false);
        config.SetValue("ecs.mt.validate_parallel_script_access_masks", true);
        config.SetValue("ecs.mt.warn_parallel_script_access_mismatch", true);

        Limitless::NativeScriptRegistry::Clear();
        Limitless::NativeScriptRegistry::RegisterScript<ParallelRigidbodyMutateScript>("ParallelRigidbodyMutateScript");

        Limitless::Scene scene;
        auto& registry = scene.GetRegistry();
        const entt::entity scriptHost = scene.CreateEntity("ParallelRigidbodyMaskMismatchHost");
        auto& scriptEntry = AttachScriptEntry(scene, scriptHost);
        scriptEntry.ScriptClassName = "ParallelRigidbodyMutateScript";
        scriptEntry.ExecutionPolicy = Limitless::ScriptExecutionPolicy::ParallelSafe;
        scriptEntry.DeclaredReadAccessMask = Limitless::ToAccessMask(Limitless::SceneSystemAccessComponent::Transform);
        scriptEntry.DeclaredWriteAccessMask = 0;
        registry.emplace<Limitless::Rigidbody2DComponent>(scriptHost);

        scene.Update(1.0f / 60.0f);

        const auto& rigidbody = registry.get<Limitless::Rigidbody2DComponent>(scriptHost);
        CHECK(rigidbody.RuntimeHasPendingLinearVelocityX == true);
        CHECK(rigidbody.RuntimePendingLinearVelocityX == doctest::Approx(3.5f));
        CHECK(scriptEntry.RuntimeWarnedAccessMaskMismatch == true);
        CHECK(scriptEntry.RuntimeUpdateCount >= 1);
    }

    TEST_CASE("Parallel-safe hierarchy writes without declared write mask are flagged")
    {
        struct NativeScriptRegistryCleanup final
        {
            ~NativeScriptRegistryCleanup()
            {
                Limitless::NativeScriptRegistry::Clear();
            }
        };
        [[maybe_unused]] NativeScriptRegistryCleanup registryCleanup;

        auto& config = Limitless::ConfigManager::GetInstance();
        const bool previousEnableParallelScripts = config.GetValue<bool>("ecs.mt.enable_parallel_scripts", true);
        const bool previousRequireAccessDeclarations = config.GetValue<bool>("ecs.mt.require_parallel_script_access_declarations", true);
        const bool previousWarnImplicitAccess = config.GetValue<bool>("ecs.mt.warn_implicit_parallel_script_access", true);
        const bool previousValidateAccessMasks = config.GetValue<bool>("ecs.mt.validate_parallel_script_access_masks", true);
        const bool previousWarnAccessMaskMismatch = config.GetValue<bool>("ecs.mt.warn_parallel_script_access_mismatch", true);

        struct ConfigRestore final
        {
            Limitless::ConfigManager& Config;
            bool EnableParallelScripts;
            bool RequireAccessDeclarations;
            bool WarnImplicitAccess;
            bool ValidateAccessMasks;
            bool WarnAccessMaskMismatch;

            ~ConfigRestore()
            {
                Config.SetValue("ecs.mt.enable_parallel_scripts", EnableParallelScripts);
                Config.SetValue("ecs.mt.require_parallel_script_access_declarations", RequireAccessDeclarations);
                Config.SetValue("ecs.mt.warn_implicit_parallel_script_access", WarnImplicitAccess);
                Config.SetValue("ecs.mt.validate_parallel_script_access_masks", ValidateAccessMasks);
                Config.SetValue("ecs.mt.warn_parallel_script_access_mismatch", WarnAccessMaskMismatch);
            }
        } configRestore{
            config,
            previousEnableParallelScripts,
            previousRequireAccessDeclarations,
            previousWarnImplicitAccess,
            previousValidateAccessMasks,
            previousWarnAccessMaskMismatch
        };

        config.SetValue("ecs.mt.enable_parallel_scripts", true);
        config.SetValue("ecs.mt.require_parallel_script_access_declarations", true);
        config.SetValue("ecs.mt.warn_implicit_parallel_script_access", false);
        config.SetValue("ecs.mt.validate_parallel_script_access_masks", true);
        config.SetValue("ecs.mt.warn_parallel_script_access_mismatch", true);

        Limitless::NativeScriptRegistry::Clear();
        Limitless::NativeScriptRegistry::RegisterScript<ParallelHierarchyMutateScript>("ParallelHierarchyMutateScript");

        Limitless::Scene scene;
        auto& registry = scene.GetRegistry();
        const entt::entity scriptHost = scene.CreateEntity("ParallelHierarchyMaskMismatchHost");
        auto& scriptEntry = AttachScriptEntry(scene, scriptHost);
        scriptEntry.ScriptClassName = "ParallelHierarchyMutateScript";
        scriptEntry.ExecutionPolicy = Limitless::ScriptExecutionPolicy::ParallelSafe;
        scriptEntry.DeclaredReadAccessMask = Limitless::ToAccessMask(Limitless::SceneSystemAccessComponent::Transform);
        scriptEntry.DeclaredWriteAccessMask = 0;

        const int32_t initialSiblingOrder = registry.get<Limitless::HierarchyComponent>(scriptHost).SiblingOrder;
        scene.Update(1.0f / 60.0f);

        CHECK(registry.get<Limitless::HierarchyComponent>(scriptHost).SiblingOrder == initialSiblingOrder + 1);
        CHECK(scriptEntry.RuntimeWarnedAccessMaskMismatch == true);
        CHECK(scriptEntry.RuntimeUpdateCount >= 1);
    }

    TEST_CASE("Parallel-safe BoxCollider2D writes without declared write mask are flagged")
    {
        struct NativeScriptRegistryCleanup final
        {
            ~NativeScriptRegistryCleanup()
            {
                Limitless::NativeScriptRegistry::Clear();
            }
        };
        [[maybe_unused]] NativeScriptRegistryCleanup registryCleanup;

        auto& config = Limitless::ConfigManager::GetInstance();
        const bool previousEnableParallelScripts = config.GetValue<bool>("ecs.mt.enable_parallel_scripts", true);
        const bool previousRequireAccessDeclarations = config.GetValue<bool>("ecs.mt.require_parallel_script_access_declarations", true);
        const bool previousWarnImplicitAccess = config.GetValue<bool>("ecs.mt.warn_implicit_parallel_script_access", true);
        const bool previousValidateAccessMasks = config.GetValue<bool>("ecs.mt.validate_parallel_script_access_masks", true);
        const bool previousWarnAccessMaskMismatch = config.GetValue<bool>("ecs.mt.warn_parallel_script_access_mismatch", true);

        struct ConfigRestore final
        {
            Limitless::ConfigManager& Config;
            bool EnableParallelScripts;
            bool RequireAccessDeclarations;
            bool WarnImplicitAccess;
            bool ValidateAccessMasks;
            bool WarnAccessMaskMismatch;

            ~ConfigRestore()
            {
                Config.SetValue("ecs.mt.enable_parallel_scripts", EnableParallelScripts);
                Config.SetValue("ecs.mt.require_parallel_script_access_declarations", RequireAccessDeclarations);
                Config.SetValue("ecs.mt.warn_implicit_parallel_script_access", WarnImplicitAccess);
                Config.SetValue("ecs.mt.validate_parallel_script_access_masks", ValidateAccessMasks);
                Config.SetValue("ecs.mt.warn_parallel_script_access_mismatch", WarnAccessMaskMismatch);
            }
        } configRestore{
            config,
            previousEnableParallelScripts,
            previousRequireAccessDeclarations,
            previousWarnImplicitAccess,
            previousValidateAccessMasks,
            previousWarnAccessMaskMismatch
        };

        config.SetValue("ecs.mt.enable_parallel_scripts", true);
        config.SetValue("ecs.mt.require_parallel_script_access_declarations", true);
        config.SetValue("ecs.mt.warn_implicit_parallel_script_access", false);
        config.SetValue("ecs.mt.validate_parallel_script_access_masks", true);
        config.SetValue("ecs.mt.warn_parallel_script_access_mismatch", true);

        Limitless::NativeScriptRegistry::Clear();
        Limitless::NativeScriptRegistry::RegisterScript<ParallelBoxColliderMutateScript>("ParallelBoxColliderMutateScript");

        Limitless::Scene scene;
        auto& registry = scene.GetRegistry();
        const entt::entity scriptHost = scene.CreateEntity("ParallelBoxColliderMaskMismatchHost");
        auto& scriptEntry = AttachScriptEntry(scene, scriptHost);
        scriptEntry.ScriptClassName = "ParallelBoxColliderMutateScript";
        scriptEntry.ExecutionPolicy = Limitless::ScriptExecutionPolicy::ParallelSafe;
        scriptEntry.DeclaredReadAccessMask = Limitless::ToAccessMask(Limitless::SceneSystemAccessComponent::Transform);
        scriptEntry.DeclaredWriteAccessMask = 0;
        registry.emplace<Limitless::BoxCollider2DComponent>(scriptHost);

        scene.Update(1.0f / 60.0f);

        const auto& boxCollider = registry.get<Limitless::BoxCollider2DComponent>(scriptHost);
        CHECK(boxCollider.Friction == doctest::Approx(0.42f));
        CHECK(scriptEntry.RuntimeWarnedAccessMaskMismatch == true);
        CHECK(scriptEntry.RuntimeUpdateCount >= 1);
    }

    TEST_CASE("Parallel-safe CircleCollider2D writes without declared write mask are flagged")
    {
        struct NativeScriptRegistryCleanup final
        {
            ~NativeScriptRegistryCleanup()
            {
                Limitless::NativeScriptRegistry::Clear();
            }
        };
        [[maybe_unused]] NativeScriptRegistryCleanup registryCleanup;

        auto& config = Limitless::ConfigManager::GetInstance();
        const bool previousEnableParallelScripts = config.GetValue<bool>("ecs.mt.enable_parallel_scripts", true);
        const bool previousRequireAccessDeclarations = config.GetValue<bool>("ecs.mt.require_parallel_script_access_declarations", true);
        const bool previousWarnImplicitAccess = config.GetValue<bool>("ecs.mt.warn_implicit_parallel_script_access", true);
        const bool previousValidateAccessMasks = config.GetValue<bool>("ecs.mt.validate_parallel_script_access_masks", true);
        const bool previousWarnAccessMaskMismatch = config.GetValue<bool>("ecs.mt.warn_parallel_script_access_mismatch", true);

        struct ConfigRestore final
        {
            Limitless::ConfigManager& Config;
            bool EnableParallelScripts;
            bool RequireAccessDeclarations;
            bool WarnImplicitAccess;
            bool ValidateAccessMasks;
            bool WarnAccessMaskMismatch;

            ~ConfigRestore()
            {
                Config.SetValue("ecs.mt.enable_parallel_scripts", EnableParallelScripts);
                Config.SetValue("ecs.mt.require_parallel_script_access_declarations", RequireAccessDeclarations);
                Config.SetValue("ecs.mt.warn_implicit_parallel_script_access", WarnImplicitAccess);
                Config.SetValue("ecs.mt.validate_parallel_script_access_masks", ValidateAccessMasks);
                Config.SetValue("ecs.mt.warn_parallel_script_access_mismatch", WarnAccessMaskMismatch);
            }
        } configRestore{
            config,
            previousEnableParallelScripts,
            previousRequireAccessDeclarations,
            previousWarnImplicitAccess,
            previousValidateAccessMasks,
            previousWarnAccessMaskMismatch
        };

        config.SetValue("ecs.mt.enable_parallel_scripts", true);
        config.SetValue("ecs.mt.require_parallel_script_access_declarations", true);
        config.SetValue("ecs.mt.warn_implicit_parallel_script_access", false);
        config.SetValue("ecs.mt.validate_parallel_script_access_masks", true);
        config.SetValue("ecs.mt.warn_parallel_script_access_mismatch", true);

        Limitless::NativeScriptRegistry::Clear();
        Limitless::NativeScriptRegistry::RegisterScript<ParallelCircleColliderMutateScript>("ParallelCircleColliderMutateScript");

        Limitless::Scene scene;
        auto& registry = scene.GetRegistry();
        const entt::entity scriptHost = scene.CreateEntity("ParallelCircleColliderMaskMismatchHost");
        auto& scriptEntry = AttachScriptEntry(scene, scriptHost);
        scriptEntry.ScriptClassName = "ParallelCircleColliderMutateScript";
        scriptEntry.ExecutionPolicy = Limitless::ScriptExecutionPolicy::ParallelSafe;
        scriptEntry.DeclaredReadAccessMask = Limitless::ToAccessMask(Limitless::SceneSystemAccessComponent::Transform);
        scriptEntry.DeclaredWriteAccessMask = 0;
        registry.emplace<Limitless::CircleCollider2DComponent>(scriptHost);

        scene.Update(1.0f / 60.0f);

        const auto& circleCollider = registry.get<Limitless::CircleCollider2DComponent>(scriptHost);
        CHECK(circleCollider.Radius == doctest::Approx(1.25f));
        CHECK(scriptEntry.RuntimeWarnedAccessMaskMismatch == true);
        CHECK(scriptEntry.RuntimeUpdateCount >= 1);
    }

    TEST_CASE("Parallel-safe Joint2D writes without declared write mask are flagged")
    {
        struct NativeScriptRegistryCleanup final
        {
            ~NativeScriptRegistryCleanup()
            {
                Limitless::NativeScriptRegistry::Clear();
            }
        };
        [[maybe_unused]] NativeScriptRegistryCleanup registryCleanup;

        auto& config = Limitless::ConfigManager::GetInstance();
        const bool previousEnableParallelScripts = config.GetValue<bool>("ecs.mt.enable_parallel_scripts", true);
        const bool previousRequireAccessDeclarations = config.GetValue<bool>("ecs.mt.require_parallel_script_access_declarations", true);
        const bool previousWarnImplicitAccess = config.GetValue<bool>("ecs.mt.warn_implicit_parallel_script_access", true);
        const bool previousValidateAccessMasks = config.GetValue<bool>("ecs.mt.validate_parallel_script_access_masks", true);
        const bool previousWarnAccessMaskMismatch = config.GetValue<bool>("ecs.mt.warn_parallel_script_access_mismatch", true);

        struct ConfigRestore final
        {
            Limitless::ConfigManager& Config;
            bool EnableParallelScripts;
            bool RequireAccessDeclarations;
            bool WarnImplicitAccess;
            bool ValidateAccessMasks;
            bool WarnAccessMaskMismatch;

            ~ConfigRestore()
            {
                Config.SetValue("ecs.mt.enable_parallel_scripts", EnableParallelScripts);
                Config.SetValue("ecs.mt.require_parallel_script_access_declarations", RequireAccessDeclarations);
                Config.SetValue("ecs.mt.warn_implicit_parallel_script_access", WarnImplicitAccess);
                Config.SetValue("ecs.mt.validate_parallel_script_access_masks", ValidateAccessMasks);
                Config.SetValue("ecs.mt.warn_parallel_script_access_mismatch", WarnAccessMaskMismatch);
            }
        } configRestore{
            config,
            previousEnableParallelScripts,
            previousRequireAccessDeclarations,
            previousWarnImplicitAccess,
            previousValidateAccessMasks,
            previousWarnAccessMaskMismatch
        };

        config.SetValue("ecs.mt.enable_parallel_scripts", true);
        config.SetValue("ecs.mt.require_parallel_script_access_declarations", true);
        config.SetValue("ecs.mt.warn_implicit_parallel_script_access", false);
        config.SetValue("ecs.mt.validate_parallel_script_access_masks", true);
        config.SetValue("ecs.mt.warn_parallel_script_access_mismatch", true);

        Limitless::NativeScriptRegistry::Clear();
        Limitless::NativeScriptRegistry::RegisterScript<ParallelJointMutateScript>("ParallelJointMutateScript");

        Limitless::Scene scene;
        auto& registry = scene.GetRegistry();
        const entt::entity scriptHost = scene.CreateEntity("ParallelJointMaskMismatchHost");
        auto& scriptEntry = AttachScriptEntry(scene, scriptHost);
        scriptEntry.ScriptClassName = "ParallelJointMutateScript";
        scriptEntry.ExecutionPolicy = Limitless::ScriptExecutionPolicy::ParallelSafe;
        scriptEntry.DeclaredReadAccessMask = Limitless::ToAccessMask(Limitless::SceneSystemAccessComponent::Transform);
        scriptEntry.DeclaredWriteAccessMask = 0;
        registry.emplace<Limitless::Joint2DComponent>(scriptHost);

        scene.Update(1.0f / 60.0f);

        const auto& joint = registry.get<Limitless::Joint2DComponent>(scriptHost);
        CHECK(joint.EnableMotor == true);
        CHECK(joint.MotorSpeed == doctest::Approx(2.0f));
        CHECK(scriptEntry.RuntimeWarnedAccessMaskMismatch == true);
        CHECK(scriptEntry.RuntimeUpdateCount >= 1);
    }

    TEST_CASE("Parallel-safe Rendering2D writes without declared write mask are flagged")
    {
        struct NativeScriptRegistryCleanup final
        {
            ~NativeScriptRegistryCleanup()
            {
                Limitless::NativeScriptRegistry::Clear();
            }
        };
        [[maybe_unused]] NativeScriptRegistryCleanup registryCleanup;

        auto& config = Limitless::ConfigManager::GetInstance();
        const bool previousEnableParallelScripts = config.GetValue<bool>("ecs.mt.enable_parallel_scripts", true);
        const bool previousRequireAccessDeclarations = config.GetValue<bool>("ecs.mt.require_parallel_script_access_declarations", true);
        const bool previousWarnImplicitAccess = config.GetValue<bool>("ecs.mt.warn_implicit_parallel_script_access", true);
        const bool previousValidateAccessMasks = config.GetValue<bool>("ecs.mt.validate_parallel_script_access_masks", true);
        const bool previousWarnAccessMaskMismatch = config.GetValue<bool>("ecs.mt.warn_parallel_script_access_mismatch", true);

        struct ConfigRestore final
        {
            Limitless::ConfigManager& Config;
            bool EnableParallelScripts;
            bool RequireAccessDeclarations;
            bool WarnImplicitAccess;
            bool ValidateAccessMasks;
            bool WarnAccessMaskMismatch;

            ~ConfigRestore()
            {
                Config.SetValue("ecs.mt.enable_parallel_scripts", EnableParallelScripts);
                Config.SetValue("ecs.mt.require_parallel_script_access_declarations", RequireAccessDeclarations);
                Config.SetValue("ecs.mt.warn_implicit_parallel_script_access", WarnImplicitAccess);
                Config.SetValue("ecs.mt.validate_parallel_script_access_masks", ValidateAccessMasks);
                Config.SetValue("ecs.mt.warn_parallel_script_access_mismatch", WarnAccessMaskMismatch);
            }
        } configRestore{
            config,
            previousEnableParallelScripts,
            previousRequireAccessDeclarations,
            previousWarnImplicitAccess,
            previousValidateAccessMasks,
            previousWarnAccessMaskMismatch
        };

        config.SetValue("ecs.mt.enable_parallel_scripts", true);
        config.SetValue("ecs.mt.require_parallel_script_access_declarations", true);
        config.SetValue("ecs.mt.warn_implicit_parallel_script_access", false);
        config.SetValue("ecs.mt.validate_parallel_script_access_masks", true);
        config.SetValue("ecs.mt.warn_parallel_script_access_mismatch", true);

        Limitless::NativeScriptRegistry::Clear();
        Limitless::NativeScriptRegistry::RegisterScript<ParallelSpriteMutateScript>("ParallelSpriteMutateScript");

        Limitless::Scene scene;
        auto& registry = scene.GetRegistry();
        const entt::entity scriptHost = scene.CreateEntity("ParallelRenderingMaskMismatchHost");
        auto& scriptEntry = AttachScriptEntry(scene, scriptHost);
        scriptEntry.ScriptClassName = "ParallelSpriteMutateScript";
        scriptEntry.ExecutionPolicy = Limitless::ScriptExecutionPolicy::ParallelSafe;
        scriptEntry.DeclaredReadAccessMask = Limitless::ToAccessMask(Limitless::SceneSystemAccessComponent::Transform);
        scriptEntry.DeclaredWriteAccessMask = 0;
        registry.emplace<Limitless::SpriteComponent>(scriptHost);

        const int32_t initialRenderOrder = registry.get<Limitless::SpriteComponent>(scriptHost).RenderOrder;
        scene.Update(1.0f / 60.0f);

        CHECK(registry.get<Limitless::SpriteComponent>(scriptHost).RenderOrder == initialRenderOrder + 1);
        CHECK(scriptEntry.RuntimeWarnedAccessMaskMismatch == true);
        CHECK(scriptEntry.RuntimeUpdateCount >= 1);
    }

    TEST_CASE("Parallel-safe Audio writes without declared write mask are flagged")
    {
        struct NativeScriptRegistryCleanup final
        {
            ~NativeScriptRegistryCleanup()
            {
                Limitless::NativeScriptRegistry::Clear();
            }
        };
        [[maybe_unused]] NativeScriptRegistryCleanup registryCleanup;

        auto& config = Limitless::ConfigManager::GetInstance();
        const bool previousEnableParallelScripts = config.GetValue<bool>("ecs.mt.enable_parallel_scripts", true);
        const bool previousRequireAccessDeclarations = config.GetValue<bool>("ecs.mt.require_parallel_script_access_declarations", true);
        const bool previousWarnImplicitAccess = config.GetValue<bool>("ecs.mt.warn_implicit_parallel_script_access", true);
        const bool previousValidateAccessMasks = config.GetValue<bool>("ecs.mt.validate_parallel_script_access_masks", true);
        const bool previousWarnAccessMaskMismatch = config.GetValue<bool>("ecs.mt.warn_parallel_script_access_mismatch", true);

        struct ConfigRestore final
        {
            Limitless::ConfigManager& Config;
            bool EnableParallelScripts;
            bool RequireAccessDeclarations;
            bool WarnImplicitAccess;
            bool ValidateAccessMasks;
            bool WarnAccessMaskMismatch;

            ~ConfigRestore()
            {
                Config.SetValue("ecs.mt.enable_parallel_scripts", EnableParallelScripts);
                Config.SetValue("ecs.mt.require_parallel_script_access_declarations", RequireAccessDeclarations);
                Config.SetValue("ecs.mt.warn_implicit_parallel_script_access", WarnImplicitAccess);
                Config.SetValue("ecs.mt.validate_parallel_script_access_masks", ValidateAccessMasks);
                Config.SetValue("ecs.mt.warn_parallel_script_access_mismatch", WarnAccessMaskMismatch);
            }
        } configRestore{
            config,
            previousEnableParallelScripts,
            previousRequireAccessDeclarations,
            previousWarnImplicitAccess,
            previousValidateAccessMasks,
            previousWarnAccessMaskMismatch
        };

        config.SetValue("ecs.mt.enable_parallel_scripts", true);
        config.SetValue("ecs.mt.require_parallel_script_access_declarations", true);
        config.SetValue("ecs.mt.warn_implicit_parallel_script_access", false);
        config.SetValue("ecs.mt.validate_parallel_script_access_masks", true);
        config.SetValue("ecs.mt.warn_parallel_script_access_mismatch", true);

        Limitless::NativeScriptRegistry::Clear();
        Limitless::NativeScriptRegistry::RegisterScript<ParallelAudioMutateScript>("ParallelAudioMutateScript");

        Limitless::Scene scene;
        auto& registry = scene.GetRegistry();
        const entt::entity scriptHost = scene.CreateEntity("ParallelAudioMaskMismatchHost");
        auto& scriptEntry = AttachScriptEntry(scene, scriptHost);
        scriptEntry.ScriptClassName = "ParallelAudioMutateScript";
        scriptEntry.ExecutionPolicy = Limitless::ScriptExecutionPolicy::ParallelSafe;
        scriptEntry.DeclaredReadAccessMask = Limitless::ToAccessMask(Limitless::SceneSystemAccessComponent::Transform);
        scriptEntry.DeclaredWriteAccessMask = 0;
        registry.emplace<Limitless::AudioSourceComponent>(scriptHost);

        scene.Update(1.0f / 60.0f);

        CHECK(registry.get<Limitless::AudioSourceComponent>(scriptHost).Volume == doctest::Approx(0.33f));
        CHECK(scriptEntry.RuntimeWarnedAccessMaskMismatch == true);
        CHECK(scriptEntry.RuntimeUpdateCount >= 1);
    }

    TEST_CASE("Parallel-safe Metadata writes without declared write mask are flagged")
    {
        struct NativeScriptRegistryCleanup final
        {
            ~NativeScriptRegistryCleanup()
            {
                Limitless::NativeScriptRegistry::Clear();
            }
        };
        [[maybe_unused]] NativeScriptRegistryCleanup registryCleanup;

        auto& config = Limitless::ConfigManager::GetInstance();
        const bool previousEnableParallelScripts = config.GetValue<bool>("ecs.mt.enable_parallel_scripts", true);
        const bool previousRequireAccessDeclarations = config.GetValue<bool>("ecs.mt.require_parallel_script_access_declarations", true);
        const bool previousWarnImplicitAccess = config.GetValue<bool>("ecs.mt.warn_implicit_parallel_script_access", true);
        const bool previousValidateAccessMasks = config.GetValue<bool>("ecs.mt.validate_parallel_script_access_masks", true);
        const bool previousWarnAccessMaskMismatch = config.GetValue<bool>("ecs.mt.warn_parallel_script_access_mismatch", true);

        struct ConfigRestore final
        {
            Limitless::ConfigManager& Config;
            bool EnableParallelScripts;
            bool RequireAccessDeclarations;
            bool WarnImplicitAccess;
            bool ValidateAccessMasks;
            bool WarnAccessMaskMismatch;

            ~ConfigRestore()
            {
                Config.SetValue("ecs.mt.enable_parallel_scripts", EnableParallelScripts);
                Config.SetValue("ecs.mt.require_parallel_script_access_declarations", RequireAccessDeclarations);
                Config.SetValue("ecs.mt.warn_implicit_parallel_script_access", WarnImplicitAccess);
                Config.SetValue("ecs.mt.validate_parallel_script_access_masks", ValidateAccessMasks);
                Config.SetValue("ecs.mt.warn_parallel_script_access_mismatch", WarnAccessMaskMismatch);
            }
        } configRestore{
            config,
            previousEnableParallelScripts,
            previousRequireAccessDeclarations,
            previousWarnImplicitAccess,
            previousValidateAccessMasks,
            previousWarnAccessMaskMismatch
        };

        config.SetValue("ecs.mt.enable_parallel_scripts", true);
        config.SetValue("ecs.mt.require_parallel_script_access_declarations", true);
        config.SetValue("ecs.mt.warn_implicit_parallel_script_access", false);
        config.SetValue("ecs.mt.validate_parallel_script_access_masks", true);
        config.SetValue("ecs.mt.warn_parallel_script_access_mismatch", true);

        Limitless::NativeScriptRegistry::Clear();
        Limitless::NativeScriptRegistry::RegisterScript<ParallelTagMutateScript>("ParallelTagMutateScript");

        Limitless::Scene scene;
        auto& registry = scene.GetRegistry();
        const entt::entity scriptHost = scene.CreateEntity("ParallelMetadataMaskMismatchHost");
        auto& scriptEntry = AttachScriptEntry(scene, scriptHost);
        scriptEntry.ScriptClassName = "ParallelTagMutateScript";
        scriptEntry.ExecutionPolicy = Limitless::ScriptExecutionPolicy::ParallelSafe;
        scriptEntry.DeclaredReadAccessMask = Limitless::ToAccessMask(Limitless::SceneSystemAccessComponent::Transform);
        scriptEntry.DeclaredWriteAccessMask = 0;

        scene.Update(1.0f / 60.0f);

        CHECK(registry.get<Limitless::TagComponent>(scriptHost).Tag == "RetaggedByParallelScript");
        CHECK(scriptEntry.RuntimeWarnedAccessMaskMismatch == true);
        CHECK(scriptEntry.RuntimeUpdateCount >= 1);
    }

    TEST_CASE("Parallel-safe Animator writes without declared write mask are flagged")
    {
        struct NativeScriptRegistryCleanup final
        {
            ~NativeScriptRegistryCleanup()
            {
                Limitless::NativeScriptRegistry::Clear();
            }
        };
        [[maybe_unused]] NativeScriptRegistryCleanup registryCleanup;

        auto& config = Limitless::ConfigManager::GetInstance();
        const bool previousEnableParallelScripts = config.GetValue<bool>("ecs.mt.enable_parallel_scripts", true);
        const bool previousRequireAccessDeclarations = config.GetValue<bool>("ecs.mt.require_parallel_script_access_declarations", true);
        const bool previousWarnImplicitAccess = config.GetValue<bool>("ecs.mt.warn_implicit_parallel_script_access", true);
        const bool previousValidateAccessMasks = config.GetValue<bool>("ecs.mt.validate_parallel_script_access_masks", true);
        const bool previousWarnAccessMaskMismatch = config.GetValue<bool>("ecs.mt.warn_parallel_script_access_mismatch", true);

        struct ConfigRestore final
        {
            Limitless::ConfigManager& Config;
            bool EnableParallelScripts;
            bool RequireAccessDeclarations;
            bool WarnImplicitAccess;
            bool ValidateAccessMasks;
            bool WarnAccessMaskMismatch;

            ~ConfigRestore()
            {
                Config.SetValue("ecs.mt.enable_parallel_scripts", EnableParallelScripts);
                Config.SetValue("ecs.mt.require_parallel_script_access_declarations", RequireAccessDeclarations);
                Config.SetValue("ecs.mt.warn_implicit_parallel_script_access", WarnImplicitAccess);
                Config.SetValue("ecs.mt.validate_parallel_script_access_masks", ValidateAccessMasks);
                Config.SetValue("ecs.mt.warn_parallel_script_access_mismatch", WarnAccessMaskMismatch);
            }
        } configRestore{
            config,
            previousEnableParallelScripts,
            previousRequireAccessDeclarations,
            previousWarnImplicitAccess,
            previousValidateAccessMasks,
            previousWarnAccessMaskMismatch
        };

        config.SetValue("ecs.mt.enable_parallel_scripts", true);
        config.SetValue("ecs.mt.require_parallel_script_access_declarations", true);
        config.SetValue("ecs.mt.warn_implicit_parallel_script_access", false);
        config.SetValue("ecs.mt.validate_parallel_script_access_masks", true);
        config.SetValue("ecs.mt.warn_parallel_script_access_mismatch", true);

        Limitless::NativeScriptRegistry::Clear();
        Limitless::NativeScriptRegistry::RegisterScript<ParallelAnimatorMutateScript>("ParallelAnimatorMutateScript");

        Limitless::Scene scene;
        auto& registry = scene.GetRegistry();
        const entt::entity scriptHost = scene.CreateEntity("ParallelAnimatorMaskMismatchHost");
        auto& scriptEntry = AttachScriptEntry(scene, scriptHost);
        scriptEntry.ScriptClassName = "ParallelAnimatorMutateScript";
        scriptEntry.ExecutionPolicy = Limitless::ScriptExecutionPolicy::ParallelSafe;
        scriptEntry.DeclaredReadAccessMask = Limitless::ToAccessMask(Limitless::SceneSystemAccessComponent::Transform);
        scriptEntry.DeclaredWriteAccessMask = 0;
        registry.emplace<Limitless::AnimatorComponent>(scriptHost);

        scene.Update(1.0f / 60.0f);

        CHECK(registry.get<Limitless::AnimatorComponent>(scriptHost).PlaybackSpeed == doctest::Approx(1.5f));
        CHECK(scriptEntry.RuntimeWarnedAccessMaskMismatch == true);
        CHECK(scriptEntry.RuntimeUpdateCount >= 1);
    }

    TEST_CASE("Parallel-safe ParticleEmitter writes without declared write mask are flagged")
    {
        struct NativeScriptRegistryCleanup final
        {
            ~NativeScriptRegistryCleanup()
            {
                Limitless::NativeScriptRegistry::Clear();
            }
        };
        [[maybe_unused]] NativeScriptRegistryCleanup registryCleanup;

        auto& config = Limitless::ConfigManager::GetInstance();
        const bool previousEnableParallelScripts = config.GetValue<bool>("ecs.mt.enable_parallel_scripts", true);
        const bool previousRequireAccessDeclarations = config.GetValue<bool>("ecs.mt.require_parallel_script_access_declarations", true);
        const bool previousWarnImplicitAccess = config.GetValue<bool>("ecs.mt.warn_implicit_parallel_script_access", true);
        const bool previousValidateAccessMasks = config.GetValue<bool>("ecs.mt.validate_parallel_script_access_masks", true);
        const bool previousWarnAccessMaskMismatch = config.GetValue<bool>("ecs.mt.warn_parallel_script_access_mismatch", true);

        struct ConfigRestore final
        {
            Limitless::ConfigManager& Config;
            bool EnableParallelScripts;
            bool RequireAccessDeclarations;
            bool WarnImplicitAccess;
            bool ValidateAccessMasks;
            bool WarnAccessMaskMismatch;

            ~ConfigRestore()
            {
                Config.SetValue("ecs.mt.enable_parallel_scripts", EnableParallelScripts);
                Config.SetValue("ecs.mt.require_parallel_script_access_declarations", RequireAccessDeclarations);
                Config.SetValue("ecs.mt.warn_implicit_parallel_script_access", WarnImplicitAccess);
                Config.SetValue("ecs.mt.validate_parallel_script_access_masks", ValidateAccessMasks);
                Config.SetValue("ecs.mt.warn_parallel_script_access_mismatch", WarnAccessMaskMismatch);
            }
        } configRestore{
            config,
            previousEnableParallelScripts,
            previousRequireAccessDeclarations,
            previousWarnImplicitAccess,
            previousValidateAccessMasks,
            previousWarnAccessMaskMismatch
        };

        config.SetValue("ecs.mt.enable_parallel_scripts", true);
        config.SetValue("ecs.mt.require_parallel_script_access_declarations", true);
        config.SetValue("ecs.mt.warn_implicit_parallel_script_access", false);
        config.SetValue("ecs.mt.validate_parallel_script_access_masks", true);
        config.SetValue("ecs.mt.warn_parallel_script_access_mismatch", true);

        Limitless::NativeScriptRegistry::Clear();
        Limitless::NativeScriptRegistry::RegisterScript<ParallelParticleEmitterMutateScript>("ParallelParticleEmitterMutateScript");

        Limitless::Scene scene;
        auto& registry = scene.GetRegistry();
        const entt::entity scriptHost = scene.CreateEntity("ParallelParticleEmitterMaskMismatchHost");
        auto& scriptEntry = AttachScriptEntry(scene, scriptHost);
        scriptEntry.ScriptClassName = "ParallelParticleEmitterMutateScript";
        scriptEntry.ExecutionPolicy = Limitless::ScriptExecutionPolicy::ParallelSafe;
        scriptEntry.DeclaredReadAccessMask = Limitless::ToAccessMask(Limitless::SceneSystemAccessComponent::Transform);
        scriptEntry.DeclaredWriteAccessMask = 0;
        registry.emplace<Limitless::ParticleEmitterComponent>(scriptHost);

        scene.Update(1.0f / 60.0f);

        CHECK(registry.get<Limitless::ParticleEmitterComponent>(scriptHost).SpawnRate == doctest::Approx(15.0f));
        CHECK(scriptEntry.RuntimeWarnedAccessMaskMismatch == true);
        CHECK(scriptEntry.RuntimeUpdateCount >= 1);
    }

    TEST_CASE("Parallel-safe Tilemap writes on update without declared write mask are flagged")
    {
        struct NativeScriptRegistryCleanup final
        {
            ~NativeScriptRegistryCleanup()
            {
                Limitless::NativeScriptRegistry::Clear();
            }
        };
        [[maybe_unused]] NativeScriptRegistryCleanup registryCleanup;

        auto& config = Limitless::ConfigManager::GetInstance();
        const bool previousEnableParallelScripts = config.GetValue<bool>("ecs.mt.enable_parallel_scripts", true);
        const bool previousRequireAccessDeclarations = config.GetValue<bool>("ecs.mt.require_parallel_script_access_declarations", true);
        const bool previousWarnImplicitAccess = config.GetValue<bool>("ecs.mt.warn_implicit_parallel_script_access", true);
        const bool previousValidateAccessMasks = config.GetValue<bool>("ecs.mt.validate_parallel_script_access_masks", true);
        const bool previousWarnAccessMaskMismatch = config.GetValue<bool>("ecs.mt.warn_parallel_script_access_mismatch", true);

        struct ConfigRestore final
        {
            Limitless::ConfigManager& Config;
            bool EnableParallelScripts;
            bool RequireAccessDeclarations;
            bool WarnImplicitAccess;
            bool ValidateAccessMasks;
            bool WarnAccessMaskMismatch;

            ~ConfigRestore()
            {
                Config.SetValue("ecs.mt.enable_parallel_scripts", EnableParallelScripts);
                Config.SetValue("ecs.mt.require_parallel_script_access_declarations", RequireAccessDeclarations);
                Config.SetValue("ecs.mt.warn_implicit_parallel_script_access", WarnImplicitAccess);
                Config.SetValue("ecs.mt.validate_parallel_script_access_masks", ValidateAccessMasks);
                Config.SetValue("ecs.mt.warn_parallel_script_access_mismatch", WarnAccessMaskMismatch);
            }
        } configRestore{
            config,
            previousEnableParallelScripts,
            previousRequireAccessDeclarations,
            previousWarnImplicitAccess,
            previousValidateAccessMasks,
            previousWarnAccessMaskMismatch
        };

        config.SetValue("ecs.mt.enable_parallel_scripts", true);
        config.SetValue("ecs.mt.require_parallel_script_access_declarations", true);
        config.SetValue("ecs.mt.warn_implicit_parallel_script_access", false);
        config.SetValue("ecs.mt.validate_parallel_script_access_masks", true);
        config.SetValue("ecs.mt.warn_parallel_script_access_mismatch", true);

        Limitless::NativeScriptRegistry::Clear();
        Limitless::NativeScriptRegistry::RegisterScript<ParallelTilemapMutateScript>("ParallelTilemapMutateScript");

        Limitless::Scene scene;
        auto& registry = scene.GetRegistry();
        const entt::entity scriptHost = scene.CreateEntity("ParallelTilemapMaskMismatchHost");
        auto& scriptEntry = AttachScriptEntry(scene, scriptHost);
        scriptEntry.ScriptClassName = "ParallelTilemapMutateScript";
        scriptEntry.ExecutionPolicy = Limitless::ScriptExecutionPolicy::ParallelSafe;
        scriptEntry.DeclaredReadAccessMask = Limitless::ToAccessMask(Limitless::SceneSystemAccessComponent::Transform);
        scriptEntry.DeclaredWriteAccessMask = 0;
        auto& grid2D = registry.emplace<Limitless::Grid2DComponent>(scriptHost);
        grid2D.GridSize = { 4, 2 };
        auto& tilemapLayer = registry.emplace<Limitless::TilemapLayerComponent>(scriptHost);
        Limitless::EnsureTilemapLayerStorage(grid2D, tilemapLayer);
        const uint32_t tileBeforeUpdate = tilemapLayer.Tiles[1];

        scene.Update(1.0f / 60.0f);

        CHECK(tilemapLayer.Tiles[1] != tileBeforeUpdate);
        CHECK(scriptEntry.RuntimeWarnedAccessMaskMismatch == true);
        CHECK(scriptEntry.RuntimeUpdateCount >= 1);
    }

    TEST_CASE("Parallel-safe Tilemap writes on fixed update without declared write mask are flagged")
    {
        struct NativeScriptRegistryCleanup final
        {
            ~NativeScriptRegistryCleanup()
            {
                Limitless::NativeScriptRegistry::Clear();
            }
        };
        [[maybe_unused]] NativeScriptRegistryCleanup registryCleanup;

        auto& config = Limitless::ConfigManager::GetInstance();
        const bool previousEnableParallelScripts = config.GetValue<bool>("ecs.mt.enable_parallel_scripts", true);
        const bool previousRequireAccessDeclarations = config.GetValue<bool>("ecs.mt.require_parallel_script_access_declarations", true);
        const bool previousWarnImplicitAccess = config.GetValue<bool>("ecs.mt.warn_implicit_parallel_script_access", true);
        const bool previousValidateAccessMasks = config.GetValue<bool>("ecs.mt.validate_parallel_script_access_masks", true);
        const bool previousWarnAccessMaskMismatch = config.GetValue<bool>("ecs.mt.warn_parallel_script_access_mismatch", true);

        struct ConfigRestore final
        {
            Limitless::ConfigManager& Config;
            bool EnableParallelScripts;
            bool RequireAccessDeclarations;
            bool WarnImplicitAccess;
            bool ValidateAccessMasks;
            bool WarnAccessMaskMismatch;

            ~ConfigRestore()
            {
                Config.SetValue("ecs.mt.enable_parallel_scripts", EnableParallelScripts);
                Config.SetValue("ecs.mt.require_parallel_script_access_declarations", RequireAccessDeclarations);
                Config.SetValue("ecs.mt.warn_implicit_parallel_script_access", WarnImplicitAccess);
                Config.SetValue("ecs.mt.validate_parallel_script_access_masks", ValidateAccessMasks);
                Config.SetValue("ecs.mt.warn_parallel_script_access_mismatch", WarnAccessMaskMismatch);
            }
        } configRestore{
            config,
            previousEnableParallelScripts,
            previousRequireAccessDeclarations,
            previousWarnImplicitAccess,
            previousValidateAccessMasks,
            previousWarnAccessMaskMismatch
        };

        config.SetValue("ecs.mt.enable_parallel_scripts", true);
        config.SetValue("ecs.mt.require_parallel_script_access_declarations", true);
        config.SetValue("ecs.mt.warn_implicit_parallel_script_access", false);
        config.SetValue("ecs.mt.validate_parallel_script_access_masks", true);
        config.SetValue("ecs.mt.warn_parallel_script_access_mismatch", true);

        Limitless::NativeScriptRegistry::Clear();
        Limitless::NativeScriptRegistry::RegisterScript<ParallelTilemapFixedMutateScript>("ParallelTilemapFixedMutateScript");

        Limitless::Scene scene;
        auto& registry = scene.GetRegistry();
        const entt::entity scriptHost = scene.CreateEntity("ParallelTilemapFixedMaskMismatchHost");
        auto& scriptEntry = AttachScriptEntry(scene, scriptHost);
        scriptEntry.ScriptClassName = "ParallelTilemapFixedMutateScript";
        scriptEntry.ExecutionPolicy = Limitless::ScriptExecutionPolicy::ParallelSafe;
        scriptEntry.DeclaredReadAccessMask = Limitless::ToAccessMask(Limitless::SceneSystemAccessComponent::Transform);
        scriptEntry.DeclaredWriteAccessMask = 0;
        auto& grid2D = registry.emplace<Limitless::Grid2DComponent>(scriptHost);
        grid2D.GridSize = { 4, 2 };
        auto& tilemapLayer = registry.emplace<Limitless::TilemapLayerComponent>(scriptHost);
        Limitless::EnsureTilemapLayerStorage(grid2D, tilemapLayer);
        const uint32_t tileBeforeFixedUpdate = tilemapLayer.Tiles[1];

        scene.FixedUpdate(1.0f / 60.0f);

        CHECK(tilemapLayer.Tiles[1] != tileBeforeFixedUpdate);
        CHECK(scriptEntry.RuntimeWarnedAccessMaskMismatch == true);
        CHECK(scriptEntry.RuntimeInstance != nullptr);
    }

    TEST_CASE("Parallel-safe mutable scene registry access is blocked")
    {
        struct NativeScriptRegistryCleanup final
        {
            ~NativeScriptRegistryCleanup()
            {
                Limitless::NativeScriptRegistry::Clear();
            }
        };
        [[maybe_unused]] NativeScriptRegistryCleanup registryCleanup;

        auto& config = Limitless::ConfigManager::GetInstance();
        const bool previousEnableParallelScripts = config.GetValue<bool>("ecs.mt.enable_parallel_scripts", true);
        const bool previousRequireAccessDeclarations = config.GetValue<bool>("ecs.mt.require_parallel_script_access_declarations", true);
        const bool previousWarnImplicitAccess = config.GetValue<bool>("ecs.mt.warn_implicit_parallel_script_access", true);
        const bool previousValidateMutableRegistryAccess = config.GetValue<bool>("ecs.mt.validate_mutable_registry_access", true);

        struct ConfigRestore final
        {
            Limitless::ConfigManager& Config;
            bool EnableParallelScripts;
            bool RequireAccessDeclarations;
            bool WarnImplicitAccess;
            bool ValidateMutableRegistryAccess;

            ~ConfigRestore()
            {
                Config.SetValue("ecs.mt.enable_parallel_scripts", EnableParallelScripts);
                Config.SetValue("ecs.mt.require_parallel_script_access_declarations", RequireAccessDeclarations);
                Config.SetValue("ecs.mt.warn_implicit_parallel_script_access", WarnImplicitAccess);
                Config.SetValue("ecs.mt.validate_mutable_registry_access", ValidateMutableRegistryAccess);
            }
        } configRestore{
            config,
            previousEnableParallelScripts,
            previousRequireAccessDeclarations,
            previousWarnImplicitAccess,
            previousValidateMutableRegistryAccess
        };

        config.SetValue("ecs.mt.enable_parallel_scripts", true);
        config.SetValue("ecs.mt.require_parallel_script_access_declarations", true);
        config.SetValue("ecs.mt.warn_implicit_parallel_script_access", false);
        config.SetValue("ecs.mt.validate_mutable_registry_access", true);

        Limitless::NativeScriptRegistry::Clear();
        Limitless::NativeScriptRegistry::RegisterScript<ParallelMutableRegistryAccessScript>("ParallelMutableRegistryAccessScript");

        Limitless::Scene scene;
        auto& registry = scene.GetRegistry();
        const entt::entity scriptHost = scene.CreateEntity("ParallelMutableRegistryAccessHost");
        auto& scriptEntry = AttachScriptEntry(scene, scriptHost);
        scriptEntry.ScriptClassName = "ParallelMutableRegistryAccessScript";
        scriptEntry.ExecutionPolicy = Limitless::ScriptExecutionPolicy::ParallelSafe;
        scriptEntry.DeclaredReadAccessMask = Limitless::ToAccessMask(Limitless::SceneSystemAccessComponent::Transform);
        scriptEntry.DeclaredWriteAccessMask = Limitless::ToAccessMask(Limitless::SceneSystemAccessComponent::Transform);

        auto& transform = registry.get<Limitless::TransformComponent>(scriptHost);
        const float initialX = transform.Position.x;

        scene.Update(1.0f / 60.0f);

        CHECK(transform.Position.x == doctest::Approx(initialX));
        CHECK(scriptEntry.RuntimeInstance == nullptr);
        CHECK(scriptEntry.RuntimeInitialized == false);
        CHECK(scriptEntry.RuntimeUpdateCount == 0);
    }

    TEST_CASE("Parallel-safe scripts can declare access masks with helper macros")
    {
        struct NativeScriptRegistryCleanup final
        {
            ~NativeScriptRegistryCleanup()
            {
                Limitless::NativeScriptRegistry::Clear();
            }
        };
        [[maybe_unused]] NativeScriptRegistryCleanup registryCleanup;

        auto& config = Limitless::ConfigManager::GetInstance();
        const bool previousEnableParallelScripts = config.GetValue<bool>("ecs.mt.enable_parallel_scripts", true);
        const bool previousRequireAccessDeclarations = config.GetValue<bool>("ecs.mt.require_parallel_script_access_declarations", true);
        const bool previousWarnImplicitAccess = config.GetValue<bool>("ecs.mt.warn_implicit_parallel_script_access", true);
        const bool previousValidateAccessMasks = config.GetValue<bool>("ecs.mt.validate_parallel_script_access_masks", true);
        const bool previousWarnAccessMaskMismatch = config.GetValue<bool>("ecs.mt.warn_parallel_script_access_mismatch", true);

        struct ConfigRestore final
        {
            Limitless::ConfigManager& Config;
            bool EnableParallelScripts;
            bool RequireAccessDeclarations;
            bool WarnImplicitAccess;
            bool ValidateAccessMasks;
            bool WarnAccessMaskMismatch;

            ~ConfigRestore()
            {
                Config.SetValue("ecs.mt.enable_parallel_scripts", EnableParallelScripts);
                Config.SetValue("ecs.mt.require_parallel_script_access_declarations", RequireAccessDeclarations);
                Config.SetValue("ecs.mt.warn_implicit_parallel_script_access", WarnImplicitAccess);
                Config.SetValue("ecs.mt.validate_parallel_script_access_masks", ValidateAccessMasks);
                Config.SetValue("ecs.mt.warn_parallel_script_access_mismatch", WarnAccessMaskMismatch);
            }
        } configRestore{
            config,
            previousEnableParallelScripts,
            previousRequireAccessDeclarations,
            previousWarnImplicitAccess,
            previousValidateAccessMasks,
            previousWarnAccessMaskMismatch
        };

        config.SetValue("ecs.mt.enable_parallel_scripts", true);
        config.SetValue("ecs.mt.require_parallel_script_access_declarations", true);
        config.SetValue("ecs.mt.warn_implicit_parallel_script_access", true);
        config.SetValue("ecs.mt.validate_parallel_script_access_masks", true);
        config.SetValue("ecs.mt.warn_parallel_script_access_mismatch", true);

        Limitless::NativeScriptRegistry::Clear();
        Limitless::NativeScriptRegistry::RegisterScript<ParallelHelperDeclaredAccessScript>("ParallelHelperDeclaredAccessScript");

        Limitless::Scene scene;
        auto& registry = scene.GetRegistry();
        const entt::entity scriptHost = scene.CreateEntity("ParallelHelperDeclaredAccessHost");
        auto& scriptEntry = AttachScriptEntry(scene, scriptHost);
        scriptEntry.ScriptClassName = "ParallelHelperDeclaredAccessScript";
        scriptEntry.ExecutionPolicy = Limitless::ScriptExecutionPolicy::ParallelSafe;
        scriptEntry.DeclaredReadAccessMask = 0;
        scriptEntry.DeclaredWriteAccessMask = 0;

        auto& transform = registry.get<Limitless::TransformComponent>(scriptHost);
        const float initialX = transform.Position.x;
        scene.Update(1.0f / 60.0f);

        CHECK(transform.Position.x == doctest::Approx(initialX + 2.0f));
        CHECK(scriptEntry.DeclaredReadAccessMask == Limitless::ScriptAccess::Transform);
        CHECK(scriptEntry.DeclaredWriteAccessMask == Limitless::ScriptAccess::Transform);
        CHECK(scriptEntry.RuntimeWarnedMissingAccessDeclaration == false);
        CHECK(scriptEntry.RuntimeWarnedAccessMaskMismatch == false);
        CHECK(scriptEntry.RuntimeUpdateCount >= 1);
    }

    TEST_CASE("Deferred structural mutation queue preserves all commands under concurrent enqueue")
    {
        Limitless::Scene scene;

        constexpr size_t kProducerCount = 4;
        constexpr size_t kCommandsPerProducer = 128;
        constexpr size_t kTotalCommands = kProducerCount * kCommandsPerProducer;

        std::mutex appliedOrderMutex;
        std::vector<uint64_t> appliedOrder;
        appliedOrder.reserve(kTotalCommands);
        std::atomic<uint64_t> commandToken{ 0 };
        std::atomic<bool> enqueueFailed{ false };

        std::vector<std::thread> producers;
        producers.reserve(kProducerCount);
        for (size_t producerIndex = 0; producerIndex < kProducerCount; ++producerIndex)
        {
            (void)producerIndex;
            producers.emplace_back([&]() {
                for (size_t commandIndex = 0; commandIndex < kCommandsPerProducer; ++commandIndex)
                {
                    (void)commandIndex;
                    const uint64_t token = commandToken.fetch_add(1, std::memory_order_relaxed);
                    const bool enqueued = scene.EnqueueDeferredStructuralMutation(
                        [&appliedOrder, &appliedOrderMutex, token](Limitless::Scene&) {
                            std::lock_guard<std::mutex> lock(appliedOrderMutex);
                            appliedOrder.push_back(token);
                        },
                        "TestConcurrentDeferredMutation");
                    if (!enqueued)
                        enqueueFailed.store(true, std::memory_order_relaxed);
                }
            });
        }

        for (auto& producer : producers)
            producer.join();

        CHECK(enqueueFailed.load(std::memory_order_relaxed) == false);
        scene.FlushDeferredStructuralMutations();

        REQUIRE(appliedOrder.size() == kTotalCommands);
        std::unordered_set<uint64_t> uniqueTokens(appliedOrder.begin(), appliedOrder.end());
        CHECK(uniqueTokens.size() == kTotalCommands);
        for (uint64_t token = 0; token < static_cast<uint64_t>(kTotalCommands); ++token)
            CHECK(uniqueTokens.contains(token));
    }

    TEST_CASE("Deferred structural mutation flush preserves enqueue order for single producer")
    {
        Limitless::Scene scene;

        std::vector<int> appliedOrder;
        constexpr int kCommandCount = 32;
        for (int command = 0; command < kCommandCount; ++command)
        {
            const bool enqueued = scene.EnqueueDeferredStructuralMutation(
                [&appliedOrder, command](Limitless::Scene&) {
                    appliedOrder.push_back(command);
                },
                "TestSingleProducerDeferredMutation");
            REQUIRE(enqueued);
        }

        scene.FlushDeferredStructuralMutations();
        REQUIRE(appliedOrder.size() == static_cast<size_t>(kCommandCount));
        for (int command = 0; command < kCommandCount; ++command)
            CHECK(appliedOrder[static_cast<size_t>(command)] == command);
    }

    TEST_CASE("SceneSystemScheduler isolates serial systems from parallel candidates")
    {
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

        std::atomic<bool> serialRunning{ false };
        std::atomic<bool> overlapDetected{ false };
        std::atomic<int> executedCount{ 0 };

        std::vector<Limitless::ScheduledSceneSystem> systems;
        systems.reserve(3);

        Limitless::ScheduledSceneSystem serialSystem{};
        serialSystem.Name = "Serial";
        serialSystem.AllowParallel = false;
        serialSystem.Execute = [&]() {
            serialRunning.store(true, std::memory_order_release);
            std::this_thread::sleep_for(std::chrono::milliseconds(60));
            executedCount.fetch_add(1, std::memory_order_relaxed);
            serialRunning.store(false, std::memory_order_release);
        };
        systems.push_back(std::move(serialSystem));

        auto makeParallelProbeSystem = [&](const char* name) {
            Limitless::ScheduledSceneSystem system{};
            system.Name = name;
            system.AllowParallel = true;
            system.Execute = [&]() {
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(120);
                while (!serialRunning.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline)
                {
                    std::this_thread::yield();
                }
                if (serialRunning.load(std::memory_order_acquire))
                    overlapDetected.store(true, std::memory_order_relaxed);
                executedCount.fetch_add(1, std::memory_order_relaxed);
            };
            return system;
        };

        systems.push_back(makeParallelProbeSystem("ParallelA"));
        systems.push_back(makeParallelProbeSystem("ParallelB"));

        Limitless::SceneSystemScheduler::Run(jobSystem, systems);

        CHECK(executedCount.load(std::memory_order_relaxed) == 3);
        CHECK(overlapDetected.load(std::memory_order_relaxed) == false);
    }
}
