#include "Physics/Physics2DQueries.h"

#include "Scene/Scene.h"

namespace Limitless::Physics2DQueries
{
    namespace
    {
        Scene* s_ActiveSceneForScriptQueries = nullptr;
    }

    void SetActiveSceneForScriptQueries(Scene* scene)
    {
        s_ActiveSceneForScriptQueries = scene;
    }

    Scene* GetActiveSceneForScriptQueries()
    {
        return s_ActiveSceneForScriptQueries;
    }

    Physics2DRaycastHit RaycastClosest(Scene* scene,
                                       const glm::vec2& origin,
                                       const glm::vec2& direction,
                                       float maxDistance,
                                       uint64_t collisionMask)
    {
        if (!scene)
            return {};
        return scene->RaycastClosestAcrossPhysicsWorlds(origin, direction, maxDistance, collisionMask);
    }
}
