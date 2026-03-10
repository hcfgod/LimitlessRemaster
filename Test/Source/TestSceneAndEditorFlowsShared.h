#pragma once

#include <doctest/doctest.h>

#include "Core/ConfigManager.h"
#include "Core/Concurrency/JobSystem.h"
#include "Project/ProjectSettings.h"
#include "Scene/Scene.h"
#include "Scene/SceneSystemScheduler.h"
#include "Scene/Components/AudioComponents.h"
#include "Scene/Components/RenderingComponents.h"
#include "Scene/Components/ScriptingComponents.h"
#include "Scene/Components/TilemapComponents.h"
#include "Scripting/ManagedScriptPayload.h"
#include "Scripting/NativeScriptRegistry.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

namespace SceneEditorFlowTestSupport
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

    class ParallelTilemapMutateScript final : public Limitless::ScriptableEntity
    {
    protected:
        void OnUpdate(float /*deltaTime*/) override
        {
            auto& tilemapLayer = GetComponent<Limitless::TilemapLayerComponent>();
            if (tilemapLayer.Tiles.size() > 1)
                tilemapLayer.Tiles[1] = tilemapLayer.Tiles[1] + 1u;
        }
    };

    class ParallelTilemapFixedMutateScript final : public Limitless::ScriptableEntity
    {
    protected:
        void OnFixedUpdate(float /*fixedDeltaTime*/) override
        {
            auto& tilemapLayer = GetComponent<Limitless::TilemapLayerComponent>();
            if (tilemapLayer.Tiles.size() > 1)
                tilemapLayer.Tiles[1] = tilemapLayer.Tiles[1] + 1u;
        }
    };

    class ParallelMutableRegistryAccessScript final : public Limitless::ScriptableEntity
    {
    protected:
        void OnUpdate(float /*deltaTime*/) override
        {
            auto* scene = GetScene();
            auto& registry = scene->GetRegistry();
            auto& transform = registry.get<Limitless::TransformComponent>(GetEntityHandle());
            transform.Position.x += 1.0f;
        }
    };

    class ContactOrderRecordingScript final : public Limitless::ScriptableEntity
    {
    public:
        inline static std::mutex EventMutex{};
        inline static std::vector<std::string> TriggerEnterEvents{};

        static void ResetEvents()
        {
            std::lock_guard<std::mutex> lock(EventMutex);
            TriggerEnterEvents.clear();
        }

        static std::vector<std::string> GetEventsSnapshot()
        {
            std::lock_guard<std::mutex> lock(EventMutex);
            return TriggerEnterEvents;
        }

    protected:
        void RecordEnterEvent(const Limitless::Entity& other)
        {
            std::string selfTag = "Entity";
            const auto& selfTagComponent = GetComponent<Limitless::TagComponent>();
            if (!selfTagComponent.Tag.empty())
                selfTag = selfTagComponent.Tag;

            std::string otherTag = "Entity";
            if (other)
            {
                if (const auto* otherTagComponent = other.TryGetComponent<Limitless::TagComponent>())
                {
                    if (!otherTagComponent->Tag.empty())
                        otherTag = otherTagComponent->Tag;
                }
            }

            std::lock_guard<std::mutex> lock(EventMutex);
            TriggerEnterEvents.push_back(selfTag + "->" + otherTag);
        }

        void OnTriggerEnter(const Limitless::Entity& other) override
        {
            RecordEnterEvent(other);
        }

        void OnCollisionEnter(const Limitless::Entity& other) override
        {
            RecordEnterEvent(other);
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

    class ThrowingOnDestroyScript final : public Limitless::ScriptableEntity
    {
    public:
        inline static std::atomic<int> DestroyCallCount{ 0 };

        static void ResetCounters()
        {
            DestroyCallCount.store(0, std::memory_order_relaxed);
        }

    protected:
        void OnDestroy() override
        {
            DestroyCallCount.fetch_add(1, std::memory_order_relaxed);
            throw std::runtime_error("ThrowingOnDestroyScript failure");
        }
    };

    class DestroySelfOnDestroyScript final : public Limitless::ScriptableEntity
    {
    public:
        inline static std::atomic<int> DestroyCallCount{ 0 };

        static void ResetCounters()
        {
            DestroyCallCount.store(0, std::memory_order_relaxed);
        }

    protected:
        void OnDestroy() override
        {
            DestroyCallCount.fetch_add(1, std::memory_order_relaxed);
            DestroyEntity(GetEntityHandle());
        }
    };

    inline bool IsNullEntity(entt::entity entity)
    {
        return entity == entt::null;
    }

    inline entt::entity FindEntityByTag(const Limitless::Scene& scene, const std::string& tag)
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

    inline std::filesystem::path MakeTempScenePath(const std::string& fileName)
    {
        return std::filesystem::temp_directory_path() / "LimitlessRemasterTests" / fileName;
    }

    inline std::filesystem::path MakeTempProjectRoot(const std::string& folderName)
    {
        return std::filesystem::temp_directory_path() / "LimitlessRemasterTests" / folderName;
    }

    inline uint32_t FindTileIdByAssetKey(const Limitless::TilemapLayerComponent& layer, const std::string& assetKey)
    {
        const auto it = std::find(layer.TileTable.begin(), layer.TileTable.end(), assetKey);
        if (it == layer.TileTable.end())
            return 0u;
        return static_cast<uint32_t>(std::distance(layer.TileTable.begin(), it));
    }

    inline glm::vec3 ExtractWorldPosition(const Limitless::TransformComponent& transform)
    {
        return glm::vec3(transform.WorldTransform[3]);
    }

    inline Limitless::NativeScriptEntry& AttachScriptEntry(Limitless::Scene& scene, entt::entity owner)
    {
        const entt::entity scriptEntity = scene.AttachScriptComponent(owner);
        return scene.GetRegistry().get<Limitless::ScriptComponent>(scriptEntity).Script;
    }

    inline Limitless::NativeScriptEntry& GetScriptEntry(Limitless::Scene& scene, entt::entity owner, size_t index)
    {
        const auto scriptEntities = scene.GetScriptComponentEntities(owner);
        return scene.GetRegistry().get<Limitless::ScriptComponent>(scriptEntities.at(index)).Script;
    }

    inline const Limitless::NativeScriptEntry& GetScriptEntry(const Limitless::Scene& scene, entt::entity owner, size_t index)
    {
        const auto scriptEntities = scene.GetScriptComponentEntities(owner);
        return scene.GetRegistry().get<Limitless::ScriptComponent>(scriptEntities.at(index)).Script;
    }
}
