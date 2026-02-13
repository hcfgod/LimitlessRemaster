#pragma once

#include "Scripting/ScriptableEntity.h"

namespace Limitless
{
    using NativeScriptCreateFunction = ScriptableEntity* (*)();
    using NativeScriptRegistrationCallback = void (*)(const char* className, NativeScriptCreateFunction createFunction);
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
    LT_SCRIPTCORE_API void LT_RegisterScriptCoreTypes(Limitless::NativeScriptRegistrationCallback registrationCallback);
}
