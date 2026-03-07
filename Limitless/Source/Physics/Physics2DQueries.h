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
    }
}
