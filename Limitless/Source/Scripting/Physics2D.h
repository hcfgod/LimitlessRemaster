#pragma once

#include "EnTT/entt.hpp"

#include <glm/glm.hpp>

#include <cstdint>

namespace Limitless
{
    struct RaycastHit2D
    {
        bool HasHit = false;
        entt::entity Entity = entt::null;
        glm::vec2 Point = glm::vec2(0.0f);
        glm::vec2 Normal = glm::vec2(0.0f, 1.0f);
        float Fraction = 0.0f;
    };

    using Physics2DRaycastBridgeCallback = bool (*)(
        float originX,
        float originY,
        float directionX,
        float directionY,
        float maxDistance,
        uint64_t collisionMask,
        RaycastHit2D* outHit);

    class Physics2D final
    {
    public:
        Physics2D() = delete;

        static void SetRaycastBridgeCallback(Physics2DRaycastBridgeCallback callback);

        static RaycastHit2D Raycast(const glm::vec2& origin,
                                    const glm::vec2& direction,
                                    float maxDistance = 1000.0f,
                                    uint64_t collisionMask = ~0ull);

        static bool Raycast(const glm::vec2& origin,
                            const glm::vec2& direction,
                            float maxDistance,
                            RaycastHit2D& outHit,
                            uint64_t collisionMask = ~0ull);
    };
}
