#pragma once

#include "Physics/Physics2DWorld.h"
#include "Scene/SceneCollection.h"

#include <glm/glm.hpp>

namespace Limitless
{
    class Scene;
    class SceneCollection;

    namespace Physics2DQueries
    {
        void SetActiveSceneForScriptQueries(Scene* scene);
        void SetActiveSceneCollectionForScriptQueries(SceneCollection* collection, SceneRoleMask requiredRoles = ToSceneRoleMask(SceneRole::ScriptQueryTarget));
        Scene* GetActiveSceneForScriptQueries();

        Physics2DRaycastHit RaycastClosest(Scene* scene,
                                           const glm::vec2& origin,
                                           const glm::vec2& direction,
                                           float maxDistance = 1000.0f,
                                           uint64_t collisionMask = ~0ull);

        /// Build a 32-bit layer mask from a single layer index (0-31).
        inline uint32_t LayerToMask(uint8_t layer) { return (layer < 32) ? (1u << layer) : 0u; }
        /// Build a combined layer mask from multiple layer indices.
        inline uint32_t LayersToMask(std::initializer_list<uint8_t> layers)
        {
            uint32_t mask = 0;
            for (uint8_t l : layers)
                if (l < 32) mask |= (1u << l);
            return mask;
        }
    }
}
