#include <doctest/doctest.h>

#include "Core/ConfigManager.h"
#include "Core/Concurrency/JobSystem.h"
#include "Project/ProjectSettings.h"
#include "Scene/Scene.h"
#include "Scene/SceneManager.h"
#include "Scene/SceneSystemScheduler.h"
#include "Scripting/NativeScriptRegistry.h"

#include <atomic>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>

#include <nlohmann/json.hpp>

namespace
{
    class SecondaryScript final : public Limitless::ScriptableEntity
    {
    public:
        bool ReceivedPing = false;

        void Ping()
        {
            ReceivedPing = true;
        }
    };

    class PrimaryScript final : public Limitless::ScriptableEntity
    {
    public:
        Limitless::Entity TargetEntity;
        bool FoundSelfInOnCreate = false;
        bool FoundTargetInOnCreate = false;
        bool FoundByNameOnSelfInOnCreate = false;

    protected:
        LT_BEGIN_AUTO_EXPOSED_FIELD_SYNC()
            LT_AUTO_EXPOSED_FIELD(TargetEntity)
        LT_END_AUTO_EXPOSED_FIELD_SYNC()

        void OnCreate() override
        {
            auto* selfScript = GetScript<SecondaryScript>();
            auto* targetScript = GetScript<SecondaryScript>(TargetEntity);
            auto* selfScriptByName = GetScript("SecondaryScript");

            FoundSelfInOnCreate = selfScript != nullptr;
            FoundTargetInOnCreate = targetScript != nullptr;
            FoundByNameOnSelfInOnCreate = selfScriptByName != nullptr;

            if (selfScript)
                selfScript->Ping();
            if (targetScript)
                targetScript->Ping();
        }
    };

    class ParallelSpawnScript final : public Limitless::ScriptableEntity
    {
    public:
        bool Spawned = false;

    protected:
        void OnUpdate(float /*deltaTime*/) override
        {
            if (Spawned)
                return;

            CreateEntity("ParallelSpawned");
            Spawned = true;
        }
    };

    class ParallelSpawnThenDestroyScript final : public Limitless::ScriptableEntity
    {
    public:
        bool ReceivedNonNullDeferredHandle = false;
        bool Executed = false;

    protected:
        void OnUpdate(float /*deltaTime*/) override
        {
            if (Executed)
                return;

            const entt::entity spawnedEntity = CreateEntityHandle("ParallelSpawnedThenDestroyed");
            ReceivedNonNullDeferredHandle = (spawnedEntity != entt::null);
            DestroyEntity(spawnedEntity);
            Executed = true;
        }
    };

    class ParallelDestroyScript final : public Limitless::ScriptableEntity
    {
    public:
        Limitless::Entity TargetEntity;
        bool Destroyed = false;

    protected:
        LT_BEGIN_AUTO_EXPOSED_FIELD_SYNC()
            LT_AUTO_EXPOSED_FIELD(TargetEntity)
        LT_END_AUTO_EXPOSED_FIELD_SYNC()

        void OnUpdate(float /*deltaTime*/) override
        {
            if (Destroyed || !TargetEntity)
                return;

            DestroyEntity(TargetEntity);
            Destroyed = true;
        }
    };

    class ParallelTransformMutateScript final : public Limitless::ScriptableEntity
    {
    protected:
        void OnUpdate(float /*deltaTime*/) override
        {
            auto& transform = GetComponent<Limitless::TransformComponent>();
            transform.Position.x += 1.0f;
        }
    };

    class ParallelRigidbodyMutateScript final : public Limitless::ScriptableEntity
    {
    protected:
        void OnUpdate(float /*deltaTime*/) override
        {
            auto& rigidbody = GetComponent<Limitless::Rigidbody2DComponent>();
            rigidbody.SetLinearVelocityX(3.5f);
        }
    };

    class ParallelHierarchyMutateScript final : public Limitless::ScriptableEntity
    {
    protected:
        void OnUpdate(float /*deltaTime*/) override
        {
            auto& hierarchy = GetComponent<Limitless::HierarchyComponent>();
            hierarchy.SiblingOrder += 1;
        }
    };

    class ParallelBoxColliderMutateScript final : public Limitless::ScriptableEntity
    {
    protected:
        void OnUpdate(float /*deltaTime*/) override
        {
            auto& boxCollider = GetComponent<Limitless::BoxCollider2DComponent>();
            boxCollider.Friction = 0.42f;
        }
    };

    class ParallelCircleColliderMutateScript final : public Limitless::ScriptableEntity
    {
    protected:
        void OnUpdate(float /*deltaTime*/) override
        {
            auto& circleCollider = GetComponent<Limitless::CircleCollider2DComponent>();
            circleCollider.Radius = 1.25f;
        }
    };

    class ParallelJointMutateScript final : public Limitless::ScriptableEntity
    {
    protected:
        void OnUpdate(float /*deltaTime*/) override
        {
            auto& joint = GetComponent<Limitless::Joint2DComponent>();
            joint.EnableMotor = true;
            joint.MotorSpeed = 2.0f;
        }
    };

    class ParallelSpriteMutateScript final : public Limitless::ScriptableEntity
    {
    protected:
        void OnUpdate(float /*deltaTime*/) override
        {
            auto& sprite = GetComponent<Limitless::SpriteComponent>();
            sprite.RenderOrder += 1;
        }
    };

    class ParallelAudioMutateScript final : public Limitless::ScriptableEntity
    {
    protected:
        void OnUpdate(float /*deltaTime*/) override
        {
            auto& audioSource = GetComponent<Limitless::AudioSourceComponent>();
            audioSource.Volume = 0.33f;
        }
    };

    class ParallelTagMutateScript final : public Limitless::ScriptableEntity
    {
    protected:
        void OnUpdate(float /*deltaTime*/) override
        {
            auto& tag = GetComponent<Limitless::TagComponent>();
            tag.Tag = "RetaggedByParallelScript";
        }
    };

    class ParallelAnimatorMutateScript final : public Limitless::ScriptableEntity
    {
    protected:
        void OnUpdate(float /*deltaTime*/) override
        {
            auto& animator = GetComponent<Limitless::AnimatorComponent>();
            animator.PlaybackSpeed += 0.5f;
        }
    };

    class ParallelParticleEmitterMutateScript final : public Limitless::ScriptableEntity
    {
    protected:
        void OnUpdate(float /*deltaTime*/) override
        {
            auto& particleEmitter = GetComponent<Limitless::ParticleEmitterComponent>();
            particleEmitter.SpawnRate += 5.0f;
        }
    };

    class ParallelHelperDeclaredAccessScript final : public Limitless::ScriptableEntity
    {
    public:
        LT_DECLARE_SCRIPT_ACCESS(
            LT_SCRIPT_ACCESS_MASK(Limitless::ScriptAccess::Transform),
            LT_SCRIPT_ACCESS_MASK(Limitless::ScriptAccess::Transform))

    protected:
        void OnUpdate(float /*deltaTime*/) override
        {
            auto& transform = GetComponent<Limitless::TransformComponent>();
            transform.Position.x += 2.0f;
        }
    };

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

    uint32_t FindTileIdByAssetKey(const Limitless::TilemapLayerComponent& layer, const std::string& assetKey)
    {
        const auto it = std::find(layer.TileTable.begin(), layer.TileTable.end(), assetKey);
        if (it == layer.TileTable.end())
            return 0u;
        return static_cast<uint32_t>(std::distance(layer.TileTable.begin(), it));
    }

    glm::vec3 ExtractWorldPosition(const Limitless::TransformComponent& transform)
    {
        return glm::vec3(transform.WorldTransform[3]);
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

        auto& controllerScripts = registry.emplace<Limitless::NativeScriptComponent>(controller);
        controllerScripts.Scripts.emplace_back();
        controllerScripts.Scripts[0].ScriptClassName = "PrimaryScript";
        controllerScripts.Scripts[0].ExposedProperties["TargetEntity"] = Limitless::ScriptEntityReference{ "Target" };

        // Intentionally place SecondaryScript after PrimaryScript to verify
        // references resolve during OnCreate regardless of list order.
        controllerScripts.Scripts.emplace_back();
        controllerScripts.Scripts[1].ScriptClassName = "SecondaryScript";

        auto& targetScripts = registry.emplace<Limitless::NativeScriptComponent>(target);
        targetScripts.Scripts.emplace_back();
        targetScripts.Scripts[0].ScriptClassName = "SecondaryScript";

        scene.Update(1.0f / 60.0f);

        REQUIRE(controllerScripts.Scripts[0].RuntimeInstance != nullptr);
        auto* primary = dynamic_cast<PrimaryScript*>(controllerScripts.Scripts[0].RuntimeInstance.get());
        REQUIRE(primary != nullptr);
        CHECK(primary->FoundSelfInOnCreate);
        CHECK(primary->FoundTargetInOnCreate);
        CHECK(primary->FoundByNameOnSelfInOnCreate);

        auto* selfSecondary = dynamic_cast<SecondaryScript*>(controllerScripts.Scripts[1].RuntimeInstance.get());
        auto* targetSecondary = dynamic_cast<SecondaryScript*>(targetScripts.Scripts[0].RuntimeInstance.get());
        REQUIRE(selfSecondary != nullptr);
        REQUIRE(targetSecondary != nullptr);
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

        auto& scripts = registry.emplace<Limitless::NativeScriptComponent>(scriptHost);
        scripts.Scripts.emplace_back();
        scripts.Scripts[0].ScriptClassName = "ParallelSpawnScript";
        scripts.Scripts[0].ExecutionPolicy = Limitless::ScriptExecutionPolicy::ParallelSafe;
        scripts.Scripts[0].DeclaredWriteAccessMask = Limitless::ToAccessMask(Limitless::SceneSystemAccessComponent::Transform);

        scripts.Scripts.emplace_back();
        scripts.Scripts[1].ScriptClassName = "ParallelDestroyScript";
        scripts.Scripts[1].ExecutionPolicy = Limitless::ScriptExecutionPolicy::ParallelSafe;
        scripts.Scripts[1].DeclaredWriteAccessMask = Limitless::ToAccessMask(Limitless::SceneSystemAccessComponent::Transform);
        scripts.Scripts[1].ExposedProperties["TargetEntity"] = Limitless::ScriptEntityReference{ "Victim" };

        scripts.Scripts.emplace_back();
        scripts.Scripts[2].ScriptClassName = "ParallelSpawnThenDestroyScript";
        scripts.Scripts[2].ExecutionPolicy = Limitless::ScriptExecutionPolicy::ParallelSafe;
        scripts.Scripts[2].DeclaredWriteAccessMask = Limitless::ToAccessMask(Limitless::SceneSystemAccessComponent::Transform);

        scene.Update(1.0f / 60.0f);

        CHECK_FALSE(IsNullEntity(FindEntityByTag(scene, "ParallelSpawned")));
        CHECK(IsNullEntity(FindEntityByTag(scene, "ParallelSpawnedThenDestroyed")));
        CHECK(scene.IsValid(victim) == false);
        REQUIRE(scripts.Scripts[0].RuntimeInstance != nullptr);
        REQUIRE(scripts.Scripts[1].RuntimeInstance != nullptr);
        REQUIRE(scripts.Scripts[2].RuntimeInstance != nullptr);
        auto* spawnThenDestroyScript = dynamic_cast<ParallelSpawnThenDestroyScript*>(scripts.Scripts[2].RuntimeInstance.get());
        REQUIRE(spawnThenDestroyScript != nullptr);
        CHECK(spawnThenDestroyScript->ReceivedNonNullDeferredHandle);
        CHECK(scripts.Scripts[0].RuntimeUpdateCount >= 1);
        CHECK(scripts.Scripts[1].RuntimeUpdateCount >= 1);
        CHECK(scripts.Scripts[2].RuntimeUpdateCount >= 1);
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
        auto& scripts = registry.emplace<Limitless::NativeScriptComponent>(scriptHost);
        scripts.Scripts.emplace_back();
        auto& scriptEntry = scripts.Scripts.back();
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
        auto& scripts = registry.emplace<Limitless::NativeScriptComponent>(scriptHost);
        scripts.Scripts.emplace_back();
        auto& scriptEntry = scripts.Scripts.back();
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
        auto& scripts = registry.emplace<Limitless::NativeScriptComponent>(scriptHost);
        scripts.Scripts.emplace_back();
        auto& scriptEntry = scripts.Scripts.back();
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
        auto& scripts = registry.emplace<Limitless::NativeScriptComponent>(scriptHost);
        scripts.Scripts.emplace_back();
        auto& scriptEntry = scripts.Scripts.back();
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
        auto& scripts = registry.emplace<Limitless::NativeScriptComponent>(scriptHost);
        scripts.Scripts.emplace_back();
        auto& scriptEntry = scripts.Scripts.back();
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
        auto& scripts = registry.emplace<Limitless::NativeScriptComponent>(scriptHost);
        scripts.Scripts.emplace_back();
        auto& scriptEntry = scripts.Scripts.back();
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
        auto& scripts = registry.emplace<Limitless::NativeScriptComponent>(scriptHost);
        scripts.Scripts.emplace_back();
        auto& scriptEntry = scripts.Scripts.back();
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
        auto& scripts = registry.emplace<Limitless::NativeScriptComponent>(scriptHost);
        scripts.Scripts.emplace_back();
        auto& scriptEntry = scripts.Scripts.back();
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
        auto& scripts = registry.emplace<Limitless::NativeScriptComponent>(scriptHost);
        scripts.Scripts.emplace_back();
        auto& scriptEntry = scripts.Scripts.back();
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
        auto& scripts = registry.emplace<Limitless::NativeScriptComponent>(scriptHost);
        scripts.Scripts.emplace_back();
        auto& scriptEntry = scripts.Scripts.back();
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
        auto& scripts = registry.emplace<Limitless::NativeScriptComponent>(scriptHost);
        scripts.Scripts.emplace_back();
        auto& scriptEntry = scripts.Scripts.back();
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
        auto& scripts = registry.emplace<Limitless::NativeScriptComponent>(scriptHost);
        scripts.Scripts.emplace_back();
        auto& scriptEntry = scripts.Scripts.back();
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

        auto& text = registry.emplace<Limitless::UITextComponent>(child);
        text.Text = "Play";
        text.FontFilePath = "Assets/Fonts/Inter-Regular.ttf";
        text.FontSize = 42.0f;
        text.Color = { 1.0f, 0.9f, 0.2f, 1.0f };
        text.FontLoadAttempted = true; // runtime-only behavior should reset in clone

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
        REQUIRE(cloneRegistry.all_of<Limitless::UITextComponent>(clonedChild));
        REQUIRE(cloneRegistry.all_of<Limitless::AudioSourceComponent>(clonedChild));
        REQUIRE(cloneRegistry.all_of<Limitless::NativeScriptComponent>(clonedChild));
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

        const auto& clonedSprite = cloneRegistry.get<Limitless::SpriteComponent>(clonedChild);
        CHECK(clonedSprite.TextureKey == sprite.TextureKey);
        CHECK(clonedSprite.TextureLoadAttempted == false);

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
        nativeScripts.Scripts[0].ExposedProperties["EnemyPrefab"] = Limitless::Prefab{ "Assets/Prefabs/Enemies/BasicEnemy.prefab.json" };

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
        REQUIRE(loadedNativeScripts.Scripts[0].ExposedProperties.contains("EnemyPrefab"));
        const auto* loadedEnemyPrefab = std::get_if<Limitless::Prefab>(&loadedNativeScripts.Scripts[0].ExposedProperties.at("EnemyPrefab"));
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
        authoredSettings.ShadowAlphaCutoff = 0.62f;
        authoredSettings.ShadowSegmentSnapPixels = 1.0f;
        authoredSettings.EnableHighAngularVelocityShadowFreeze = true;
        authoredSettings.ShadowFreezeAngularVelocityDegreesPerSecond = 240.0f;
        authoredSettings.ShadowFreezeFrameCount = 3;
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
        CHECK(loadedSettings.ShadowAlphaCutoff == doctest::Approx(0.62f));
        CHECK(loadedSettings.ShadowSegmentSnapPixels == doctest::Approx(1.0f));
        CHECK(loadedSettings.EnableHighAngularVelocityShadowFreeze == true);
        CHECK(loadedSettings.ShadowFreezeAngularVelocityDegreesPerSecond == doctest::Approx(240.0f));
        CHECK(loadedSettings.ShadowFreezeFrameCount == 3);
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
                { "shadowAlphaCutoff", 2.0f },
                { "shadowSegmentSnapPixels", -2.0f },
                { "enableHighAngularVelocityShadowFreeze", true },
                { "shadowFreezeAngularVelocityDegreesPerSecond", -20.0f },
                { "shadowFreezeFrameCount", 0 },
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
        CHECK(clampedSettings.ShadowAlphaCutoff == doctest::Approx(1.0f));
        CHECK(clampedSettings.ShadowSegmentSnapPixels == doctest::Approx(0.0f));
        CHECK(clampedSettings.EnableHighAngularVelocityShadowFreeze == true);
        CHECK(clampedSettings.ShadowFreezeAngularVelocityDegreesPerSecond == doctest::Approx(1.0f));
        CHECK(clampedSettings.ShadowFreezeFrameCount == 1);
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
