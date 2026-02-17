#pragma once

#define LT_ENABLE_ENTRYPOINT
#include "Limitless.h"

namespace Limitless
{
    class RuntimeApp : public Application
    {
    public:
        RuntimeApp();
        virtual ~RuntimeApp();

        // Override the virtual methods from Application
        bool Initialize() override;
        void Shutdown() override;
    };
}
