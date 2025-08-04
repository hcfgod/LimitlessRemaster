#include "SandboxApp.h"
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
		
		// Demonstrate the new Layer System
		DemonstrateLayerSystem();
		
		return true;
	}

	void SandboxApp::Shutdown()
	{
		LT_INFO("SandboxApp Shutdown");
		
		// Remove layers from the layer stack
		if (m_UIOverlay) PopOverlay(m_UIOverlay);
		if (m_DebugOverlay) PopOverlay(m_DebugOverlay);
		if (m_GameLayer) PopLayer(m_GameLayer);
		if (m_BackgroundLayer) PopLayer(m_BackgroundLayer);
	}

	void SandboxApp::DemonstrateLayerSystem()
	{
		LT_INFO("=== Layer System Demo ===");
		
		// Create layers using the helper function
		m_BackgroundLayer = CreateLayer<BackgroundLayer>();
		m_GameLayer = CreateLayer<GameLayer>();
		m_DebugOverlay = CreateLayer<DebugOverlay>();
		m_UIOverlay = CreateLayer<UIOverlay>();
		
		// Add layers to the layer stack
		// Regular layers (bottom to top rendering order)
		PushLayer(m_BackgroundLayer);
		PushLayer(m_GameLayer);
		
		// Overlays (rendered on top, processed first for events)
		PushOverlay(m_DebugOverlay);
		PushOverlay(m_UIOverlay);
		
		// Get layer stack statistics
		auto stats = GetLayerStack().GetStats();
		LT_INFO("Layer Stack created with {} total layers ({} regular, {} overlays)", 
		        stats.totalLayers, stats.regularLayers, stats.overlays);
		
		LT_INFO("Layer System demonstration initialized");
	}
}

// Define the CreateApplication function that the entry point expects
Limitless::Application* CreateApplication()
{
	return new Limitless::SandboxApp();
}