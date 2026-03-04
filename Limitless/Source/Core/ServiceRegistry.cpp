#include "ServiceRegistry.h"

namespace Limitless
{
    static ServiceRegistry s_FallbackRegistry;
    static ServiceRegistry* s_GlobalRegistry = &s_FallbackRegistry;

    ServiceRegistry& GetServices()
    {
        return *s_GlobalRegistry;
    }

    void SetGlobalServiceRegistry(ServiceRegistry* registry)
    {
        s_GlobalRegistry = registry ? registry : &s_FallbackRegistry;
    }
}
