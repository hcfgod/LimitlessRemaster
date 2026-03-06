#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace Limitless
{
    /// Multicast delegate template for C#-style event subscription.
    ///
    /// Usage:
    ///   ScriptEvent<> onClick;               // no-arg event
    ///   ScriptEvent<float> onValueChanged;   // single-arg event
    ///
    ///   auto token = onClick += [](){ ... };  // subscribe
    ///   onClick -= token;                     // unsubscribe
    ///   onClick.Invoke();                     // fire all subscribers
    ///
    /// Copy/assignment produces an empty subscriber list (subscribers are runtime-only state).
    template<typename... Args>
    class ScriptEvent
    {
    public:
        using CallbackType = std::function<void(Args...)>;
        using TokenType = uint64_t;

        ScriptEvent() = default;

        // Copy produces empty subscribers (runtime-only state, never serialized).
        ScriptEvent(const ScriptEvent&) noexcept {}
        ScriptEvent& operator=(const ScriptEvent&) noexcept
        {
            m_Subscribers.clear();
            m_NextToken = 1;
            return *this;
        }

        ScriptEvent(ScriptEvent&&) noexcept = default;
        ScriptEvent& operator=(ScriptEvent&&) noexcept = default;

        /// Subscribe a callback. Returns a token for later unsubscription.
        TokenType operator+=(CallbackType callback)
        {
            const TokenType token = m_NextToken++;
            m_Subscribers.push_back({ token, std::move(callback) });
            return token;
        }

        /// Unsubscribe by token.
        void operator-=(TokenType token)
        {
            m_Subscribers.erase(
                std::remove_if(m_Subscribers.begin(), m_Subscribers.end(),
                    [token](const Subscription& sub) { return sub.Token == token; }),
                m_Subscribers.end());
        }

        /// Fire all subscribers with the given arguments.
        void Invoke(Args... args) const
        {
            // Iterate a copy-of-size so that subscribers added during Invoke
            // are not called in this pass (matches C# event semantics).
            const size_t count = m_Subscribers.size();
            for (size_t i = 0; i < count; ++i)
            {
                if (m_Subscribers[i].Handler)
                    m_Subscribers[i].Handler(args...);
            }
        }

        /// Remove all subscribers.
        void Clear()
        {
            m_Subscribers.clear();
            m_NextToken = 1;
        }

        bool HasSubscribers() const { return !m_Subscribers.empty(); }
        size_t GetSubscriberCount() const { return m_Subscribers.size(); }

    private:
        struct Subscription
        {
            TokenType Token = 0;
            CallbackType Handler;
        };

        std::vector<Subscription> m_Subscribers;
        TokenType m_NextToken = 1;
    };
}
