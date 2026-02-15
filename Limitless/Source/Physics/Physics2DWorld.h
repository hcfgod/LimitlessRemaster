#pragma once

#include "Physics/Physics2DContactListener.h"
#include "Scene/Components.h"

#include <glm/glm.hpp>

namespace Limitless
{
    class Scene;

    struct Physics2DWorldSettings
    {
        glm::vec2 Gravity = glm::vec2(0.0f, -9.81f);
        int VelocitySubSteps = 8;
        bool EnableSleep = true;
        bool EnableContinuousCollision = true;
        float ContactHertz = 90.0f;
        float ContactDampingRatio = 1.0f;
        float ContactPushSpeed = 8.0f;
    };

    struct Physics2DRaycastHit
    {
        bool HasHit = false;
        entt::entity Entity = entt::null;
        glm::vec2 Point = glm::vec2(0.0f);
        glm::vec2 Normal = glm::vec2(0.0f, 1.0f);
        float Fraction = 0.0f;
    };

    class Physics2DWorld
    {
    public:
        Physics2DWorld() = default;
        ~Physics2DWorld();

        void Initialize(const Physics2DWorldSettings& settings);
        void Shutdown(Scene& scene);
        bool IsInitialized() const;

        void RebuildScene(Scene& scene);
        void Step(Scene& scene, float fixedDeltaTime);

        void SetSettings(const Physics2DWorldSettings& settings);
        const Physics2DWorldSettings& GetSettings() const { return m_Settings; }

        Physics2DRaycastHit RaycastClosest(const glm::vec2& origin, const glm::vec2& direction, float maxDistance, uint64_t collisionMask) const;

        const Physics2DContactListener& GetContactListener() const { return m_ContactListener; }

    private:
        void DestroyRuntimeState(Scene& scene);
        void BuildBodiesAndShapes(Scene& scene);
        void BuildJoints(Scene& scene);
        void SyncAuthoringTransformsToBodies(Scene& scene, float fixedDeltaTime);
        bool RequiresRuntimeRebuild(Scene& scene) const;
        void SyncMovedBodiesToTransforms(Scene& scene);
        void CollectContactEvents();

    private:
        Physics2DWorldSettings m_Settings{};
        Physics2DContactListener m_ContactListener;
        bool m_RuntimeBuilt = false;
#ifdef LT_ENABLE_PHYSICS2D
        b2WorldId m_WorldId = b2_nullWorldId;
#endif
    };
}
