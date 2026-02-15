#pragma once

#include "EnTT/entt.hpp"

#include <vector>

namespace Limitless
{
    struct Physics2DContactEvent
    {
        entt::entity EntityA = entt::null;
        entt::entity EntityB = entt::null;
        bool IsBegin = true;
        bool IsSensor = false;
    };

    class Physics2DContactListener
    {
    public:
        void Clear();
        void PushBegin(entt::entity entityA, entt::entity entityB, bool isSensor);
        void PushEnd(entt::entity entityA, entt::entity entityB, bool isSensor);

        const std::vector<Physics2DContactEvent>& GetEvents() const { return m_Events; }

    private:
        std::vector<Physics2DContactEvent> m_Events;
    };
}
