#include "RuntimeApp.h"
#include "GameLayer.h"

#include "Platform/Platform.h"

#include <filesystem>
#include <iostream>

namespace Limitless
{
    RuntimeApp::RuntimeApp()
    {
        LT_INFO("RuntimeApp Constructor");
    }

    RuntimeApp::~RuntimeApp()
    {
        LT_INFO("RuntimeApp Destructor");
    }

    bool RuntimeApp::Initialize()
    {
        LT_INFO("RuntimeApp Initialize");

        // Detect shipped game mode: if GameBootstrap.json exists next to
        // the executable, launch as a standalone game (no editor, no test layer).
        bool isShippedGame = false;

        const auto& platformInfo = PlatformDetection::GetPlatformInfo();
        if (!platformInfo.executablePath.empty())
        {
            const auto exeDir = std::filesystem::path(platformInfo.executablePath).parent_path();
            if (std::filesystem::exists(exeDir / "GameBootstrap.json"))
                isShippedGame = true;
        }

        if (!isShippedGame && std::filesystem::exists(std::filesystem::current_path() / "GameBootstrap.json"))
            isShippedGame = true;

        if (isShippedGame)
        {
            LT_INFO("Shipped game detected (GameBootstrap.json found). Starting GameLayer.");
            auto gameLayer = CreateLayer<GameLayer>();
            PushLayer(gameLayer);
        }

        return true;
    }

    void RuntimeApp::Shutdown()
    {
        LT_INFO("RuntimeApp Shutdown");
    }
}

// Define the CreateApplication function that the entry point expects
std::unique_ptr<Limitless::Application> CreateApplication()
{
    return std::make_unique<Limitless::RuntimeApp>();
}
