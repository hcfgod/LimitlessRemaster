#pragma once

#include "Scripting/CoroutineTypes.h"

#include <utility>

namespace Limitless
{
    class ScriptableEntity;

    class Coroutine final
    {
    public:
        Coroutine() = delete;

        // Unity-style coroutine controls on a script owner.
        static CoroutineHandle Start(ScriptableEntity& owner, CoroutineRoutine routine);
        static bool Stop(ScriptableEntity& owner, CoroutineHandle coroutineHandle);
        static void StopAll(ScriptableEntity& owner);
        static bool IsRunning(const ScriptableEntity& owner, CoroutineHandle coroutineHandle);

        // Engine-internal runtime pump. Called from Scene::Update.
        static void TickOwner(ScriptableEntity& owner, float deltaTimeSeconds);
    };

    inline CoroutineHandle StartCoroutine(ScriptableEntity& owner, CoroutineRoutine routine)
    {
        return Coroutine::Start(owner, std::move(routine));
    }

    inline bool StopCoroutine(ScriptableEntity& owner, CoroutineHandle coroutineHandle)
    {
        return Coroutine::Stop(owner, coroutineHandle);
    }

    inline void StopAllCoroutines(ScriptableEntity& owner)
    {
        Coroutine::StopAll(owner);
    }

    inline bool IsCoroutineRunning(const ScriptableEntity& owner, CoroutineHandle coroutineHandle)
    {
        return Coroutine::IsRunning(owner, coroutineHandle);
    }
}
