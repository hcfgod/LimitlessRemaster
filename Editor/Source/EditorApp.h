#pragma once

#define LT_ENABLE_ENTRYPOINT
#include "Limitless.h"

namespace Limitless
{
    class EditorApp : public Application
    {
    public:
        EditorApp() = default;
        ~EditorApp() override = default;

        bool Initialize() override;
        void Shutdown() override;
    };
}
