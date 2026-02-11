#include "SandboxApp.h"
#include "TestLayer.h"
#include <iostream>
#include <random>

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

		// Create and push the test layer
		auto testLayer = CreateLayer<TestLayer>();
		PushLayer(testLayer);
		
		LT_INFO("TestLayer pushed to layer stack");

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