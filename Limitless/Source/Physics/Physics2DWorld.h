#pragma once

#include "Physics/Physics2DContactListener.h"
#include "Scene/Components.h"

#include <glm/glm.hpp>
#include <unordered_map>

namespace Limitless
{
    class Scene;

    struct Physics2DWorldSettings
    {
        glm::vec2 Gravity = glm::vec2(0.0f, -9.81f);
        int VelocitySubSteps = 8;
        bool EnableSleep = true;
        bool EnableContinuousCollision = true;
        bool HighContactQualityMode = false;
        int HighContactQualityExtraSubSteps = 4;
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

    struct Physics2DDiagnostics
    {
        int BodyCount = 0;
        int AwakeBodyCount = 0;
        int SleepingBodyCount = 0;
        int ContactPairCount = 0;
        int PenetratingContactPointCount = 0;
        float MaxPenetrationDepth = 0.0f;
    };

    struct Physics2DBodyDiagnostics
    {
        bool IsValid = false;
        bool IsAwake = false;
        int ContactPairCount = 0;
        int PenetratingContactPointCount = 0;
        float MaxPenetrationDepth = 0.0f;
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
        const Physics2DDiagnostics& GetDiagnostics() const { return m_Diagnostics; }
        bool TryGetBodyDiagnostics(entt::entity entity, Physics2DBodyDiagnostics& outDiagnostics) const;

    private:
        void DestroyRuntimeState(Scene& scene);
        void BuildBodiesAndShapes(Scene& scene);
        void BuildJoints(Scene& scene);
        void SyncAuthoringTransformsToBodies(Scene& scene, float fixedDeltaTime);
        bool RequiresRuntimeRebuild(Scene& scene) const;
        void SyncMovedBodiesToTransforms(Scene& scene);
        void SyncBodyContactCounts(Scene& scene);
        void CollectContactEvents();
        void CollectDiagnostics(Scene& scene);
        int ComputeEffectiveSubSteps(Scene& scene) const;

    private:
        Physics2DWorldSettings m_Settings{};
        Physics2DContactListener m_ContactListener;
        Physics2DDiagnostics m_Diagnostics{};
        std::unordered_map<entt::entity, Physics2DBodyDiagnostics> m_BodyDiagnostics;
        bool m_RuntimeBuilt = false;
#ifdef LT_ENABLE_PHYSICS2D
        b2WorldId m_WorldId = b2_nullWorldId;
#endif
    };
}
