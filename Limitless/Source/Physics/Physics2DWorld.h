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
        uint16_t WorldCount = 1;
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
        explicit Physics2DWorld(uint16_t sceneWorldSlot = 0) : m_SceneWorldSlot(sceneWorldSlot) {}
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
        uint16_t GetSceneWorldSlot() const { return m_SceneWorldSlot; }

        /// Controls whether the expensive per-body diagnostics collection runs
        /// each step. Disable when the diagnostics panel is not visible to save
        /// significant CPU time at high body counts.
        void SetDiagnosticsEnabled(bool enabled) { m_DiagnosticsEnabled = enabled; }
        bool IsDiagnosticsEnabled() const { return m_DiagnosticsEnabled; }

    private:
        void DestroyRuntimeState(Scene& scene);

        /// Creates physics bodies and shapes for entities that don't have them.
        /// Returns the number of new bodies created this call (capped by
        /// kMaxNewBodiesPerStep to avoid allocation pressure).
        int BuildBodiesAndShapes(Scene& scene);

        void BuildJoints(Scene& scene);
        void SyncAuthoringTransformsToBodies(Scene& scene, float fixedDeltaTime);
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
        bool m_DiagnosticsEnabled = true;
        uint16_t m_SceneWorldSlot = 0;

        // Cached effective substep count. Recomputed only when settings or bodies change.
        int m_CachedEffectiveSubSteps = -1;
        bool m_SubStepsCacheDirty = true;

#ifdef LT_ENABLE_PHYSICS2D
        b2WorldId m_WorldId = b2_nullWorldId;
#endif
    };
}
