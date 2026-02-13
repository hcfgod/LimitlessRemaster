#include "ScriptCoreRegistration.h"

extern "C" LT_SCRIPTCORE_API void LT_RegisterScriptCoreTypes(Limitless::NativeScriptRegistrationCallback registrationCallback)
{
    if (!registrationCallback)
        return;

    for (const auto& registration : Limitless::ScriptCore::GetRegistrations())
    {
        registrationCallback(registration.ClassName.c_str(), registration.CreateFunction);
    }
}
