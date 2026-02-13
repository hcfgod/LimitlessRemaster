#pragma once

#include "Scripting/ScriptCoreApi.h"

#include <string>
#include <vector>

namespace Limitless::ScriptCore
{
    struct ScriptRegistration final
    {
        std::string ClassName;
        NativeScriptCreateFunction CreateFunction = nullptr;
    };

    void AddRegistration(const ScriptRegistration& registration);
    const std::vector<ScriptRegistration>& GetRegistrations();
}

#define LT_SCRIPTCORE_CONCAT_IMPL(a, b) a##b
#define LT_SCRIPTCORE_CONCAT(a, b) LT_SCRIPTCORE_CONCAT_IMPL(a, b)

#define LT_REGISTER_SCRIPTCORE_SCRIPT(ScriptType) \
    namespace \
    { \
        ::Limitless::ScriptableEntity* LT_SCRIPTCORE_CONCAT(LT_CreateScriptInstance_, __LINE__)() \
        { \
            return new ScriptType(); \
        } \
        struct LT_SCRIPTCORE_CONCAT(LT_ScriptRegistrar_, __LINE__) \
        { \
            LT_SCRIPTCORE_CONCAT(LT_ScriptRegistrar_, __LINE__)() \
            { \
                ::Limitless::ScriptCore::AddRegistration({ #ScriptType, &LT_SCRIPTCORE_CONCAT(LT_CreateScriptInstance_, __LINE__) }); \
            } \
        }; \
        [[maybe_unused]] static const LT_SCRIPTCORE_CONCAT(LT_ScriptRegistrar_, __LINE__) LT_SCRIPTCORE_CONCAT(LT_ScriptRegistrarInstance_, __LINE__); \
    }
