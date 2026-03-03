#pragma once

#include "Scripting/ScriptableEntity.h"

#include <cstdint>

namespace Limitless
{
    using NativeScriptCreateFunction = ScriptableEntity* (*)();
    using NativeScriptRegistrationCallback = void (*)(const char* className, NativeScriptCreateFunction createFunction);
    // Bump this whenever ScriptableEntity/ScriptProperty ABI changes.
    constexpr uint32_t kScriptCoreAbiVersion = 4u;
}

#if defined(_WIN32)
    #if defined(SCRIPTCORE_EXPORTS)
        #define LT_SCRIPTCORE_API __declspec(dllexport)
    #else
        #define LT_SCRIPTCORE_API __declspec(dllimport)
    #endif
#else
    #define LT_SCRIPTCORE_API
#endif

extern "C"
{
    LT_SCRIPTCORE_API uint32_t LT_GetScriptCoreAbiVersion();
    LT_SCRIPTCORE_API void LT_RegisterScriptCoreTypes(Limitless::NativeScriptRegistrationCallback registrationCallback);
    LT_SCRIPTCORE_API void LT_SetScriptInstantiatePrefabBridge(Limitless::ScriptInstantiatePrefabBridgeCallback callback);
    LT_SCRIPTCORE_API void LT_SetScriptResolveEntityReferenceBridge(Limitless::ScriptResolveEntityReferenceBridgeCallback callback);
    LT_SCRIPTCORE_API void LT_SetScriptContactEntityHandlesBridge(Limitless::ScriptGetContactEntityHandlesBridgeCallback callback);
}
