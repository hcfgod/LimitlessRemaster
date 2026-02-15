#include "Physics/Physics2DContactListener.h"

namespace Limitless
{
    void Physics2DContactListener::Clear()
    {
        m_Events.clear();
    }

    void Physics2DContactListener::PushBegin(entt::entity entityA, entt::entity entityB, bool isSensor)
    {
        m_Events.push_back(Physics2DContactEvent{ entityA, entityB, true, isSensor });
    }

    void Physics2DContactListener::PushEnd(entt::entity entityA, entt::entity entityB, bool isSensor)
    {
        m_Events.push_back(Physics2DContactEvent{ entityA, entityB, false, isSensor });
    }
}
