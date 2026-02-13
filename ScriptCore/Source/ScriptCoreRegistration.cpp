#include "ScriptCoreRegistration.h"

namespace Limitless::ScriptCore
{
    namespace
    {
        std::vector<ScriptRegistration>& GetMutableRegistrations()
        {
            static std::vector<ScriptRegistration> registrations;
            return registrations;
        }
    }

    void AddRegistration(const ScriptRegistration& registration)
    {
        if (registration.ClassName.empty() || !registration.CreateFunction)
            return;

        auto& registrations = GetMutableRegistrations();
        for (auto& existingRegistration : registrations)
        {
            if (existingRegistration.ClassName == registration.ClassName)
            {
                existingRegistration = registration;
                return;
            }
        }
        registrations.push_back(registration);
    }

    const std::vector<ScriptRegistration>& GetRegistrations()
    {
        return GetMutableRegistrations();
    }
}
