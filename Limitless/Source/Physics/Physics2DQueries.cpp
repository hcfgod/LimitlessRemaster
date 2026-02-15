#include "Physics/Physics2DQueries.h"

#include "Scene/Scene.h"

namespace Limitless::Physics2DQueries
{
    Physics2DRaycastHit RaycastClosest(Scene* scene,
                                       const glm::vec2& origin,
                                       const glm::vec2& direction,
                                       float maxDistance,
                                       uint64_t collisionMask)
    {
        if (!scene)
            return {};
        Physics2DWorld* world = scene->GetPhysics2DWorld();
        if (!world)
            return {};
        return world->RaycastClosest(origin, direction, maxDistance, collisionMask);
    }
}
