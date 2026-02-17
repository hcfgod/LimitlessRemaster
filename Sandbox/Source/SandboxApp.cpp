#include "SandboxApp.h"
#include "TestLayer.h"
#include "GameLayer.h"

#include "Platform/Platform.h"

#include <filesystem>
#include <iostream>

namespace Limitless
{
	SandboxApp::SandboxApp()
	{
		LT_INFO("SandboxApp Constructor");
	}

	SandboxApp::~SandboxApp()
	{
		LT_INFO("SandboxApp Destructor");
	}

	bool SandboxApp::Initialize()
	{
		LT_INFO("SandboxApp Initialize");

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
		else
		{
			LT_INFO("Dev sandbox mode. Starting TestLayer.");
			auto testLayer = CreateLayer<TestLayer>();
			PushLayer(testLayer);
		}

		return true;
	}

	void SandboxApp::Shutdown()
	{
		LT_INFO("SandboxApp Shutdown");
	}
}

// Define the CreateApplication function that the entry point expects
std::unique_ptr<Limitless::Application> CreateApplication()
{
	return std::make_unique<Limitless::SandboxApp>();
}
