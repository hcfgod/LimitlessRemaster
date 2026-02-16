#pragma once

#include "Physics/Physics2DWorld.h"

#include <glm/glm.hpp>

namespace Limitless
{
    class Scene;

    namespace Physics2DQueries
    {
        void SetActiveSceneForScriptQueries(Scene* scene);
        Scene* GetActiveSceneForScriptQueries();

        Physics2DRaycastHit RaycastClosest(Scene* scene,
                                           const glm::vec2& origin,
                                           const glm::vec2& direction,
                                           float maxDistance = 1000.0f,
                                           uint64_t collisionMask = ~0ull);
    }
}
