#include "Scripting/Physics2D.h"

#include "Physics/Physics2DQueries.h"

#include <algorithm>

namespace Limitless
{
    namespace
    {
        Physics2DRaycastBridgeCallback s_RaycastBridgeCallback = nullptr;
    }

    void Physics2D::SetRaycastBridgeCallback(Physics2DRaycastBridgeCallback callback)
    {
        s_RaycastBridgeCallback = callback;
    }

    RaycastHit2D Physics2D::Raycast(const glm::vec2& origin,
                                    const glm::vec2& direction,
                                    float maxDistance,
                                    uint64_t collisionMask)
    {
        RaycastHit2D hit{};
        (void)Raycast(origin, direction, maxDistance, hit, collisionMask);
        return hit;
    }

    bool Physics2D::Raycast(const glm::vec2& origin,
                            const glm::vec2& direction,
                            float maxDistance,
                            RaycastHit2D& outHit,
                            uint64_t collisionMask)
    {
        outHit = RaycastHit2D{};
        const float safeDistance = std::max(0.0f, maxDistance);
        if (safeDistance <= 0.0f)
            return false;

        if (s_RaycastBridgeCallback)
        {
            return s_RaycastBridgeCallback(
                origin.x,
                origin.y,
                direction.x,
                direction.y,
                safeDistance,
                collisionMask,
                &outHit) && outHit.HasHit;
        }

#ifndef SCRIPTCORE_EXPORTS
        Scene* scene = Physics2DQueries::GetActiveSceneForScriptQueries();
        const Physics2DRaycastHit nativeHit = Physics2DQueries::RaycastClosest(
            scene,
            origin,
            direction,
            safeDistance,
            collisionMask);

        if (!nativeHit.HasHit)
            return false;

        outHit.HasHit = true;
        outHit.Entity = nativeHit.Entity;
        outHit.Point = nativeHit.Point;
        outHit.Normal = nativeHit.Normal;
        outHit.Fraction = nativeHit.Fraction;
        return true;
#else
        (void)origin;
        (void)direction;
        (void)collisionMask;
        return false;
#endif
    }
}
