#include "Physics/Physics2DQueries.h"

#include "Scene/Scene.h"
#include "Scene/SceneCollection.h"

namespace Limitless::Physics2DQueries
{
    namespace
    {
        Scene* s_ActiveSceneForScriptQueries = nullptr;
        SceneCollection* s_ActiveSceneCollectionForScriptQueries = nullptr;
        SceneRoleMask s_ActiveSceneCollectionQueryRoles = ToSceneRoleMask(SceneRole::ScriptQueryTarget);
    }

    void SetActiveSceneForScriptQueries(Scene* scene)
    {
        s_ActiveSceneForScriptQueries = scene;
    }

    void SetActiveSceneCollectionForScriptQueries(SceneCollection* collection, SceneRoleMask requiredRoles)
    {
        s_ActiveSceneCollectionForScriptQueries = collection;
        s_ActiveSceneCollectionQueryRoles = requiredRoles;
    }

    Scene* GetActiveSceneForScriptQueries()
    {
        if (s_ActiveSceneForScriptQueries)
            return s_ActiveSceneForScriptQueries;

        if (s_ActiveSceneCollectionForScriptQueries)
            return s_ActiveSceneCollectionForScriptQueries->FindFirstSceneWithRoles(s_ActiveSceneCollectionQueryRoles);

        return s_ActiveSceneForScriptQueries;
    }

    Physics2DRaycastHit RaycastClosest(Scene* scene,
                                       const glm::vec2& origin,
                                       const glm::vec2& direction,
                                       float maxDistance,
                                       uint64_t collisionMask)
    {
        if (!scene)
        {
            scene = GetActiveSceneForScriptQueries();
            if (!scene)
                return {};
        }
        return scene->RaycastClosestAcrossPhysicsWorlds(origin, direction, maxDistance, collisionMask);
    }
}
