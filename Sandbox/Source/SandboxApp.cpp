#include "SandboxApp.h"
#include <iostream>

// Define CreateApplication in global namespace as expected by EntryPoint.h
Limitless::Application* CreateApplication()
{
    return new SandboxApp();
}

bool SandboxApp::Initialize()
{
    // Initialize sandbox-specific resources
	std::cout << "SandboxApp has been successfully initialized!" << std::endl;
	std::cout << "Window created with size: " << GetWindow().GetWidth() << "x" << GetWindow().GetHeight() << std::endl;
    return true;
}

void SandboxApp::Shutdown()
{
	std::cout << "SandboxApp is shutting down..." << std::endl;
    // Clean up sandbox-specific resources
} 