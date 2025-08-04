 #pragma once

#define LT_ENABLE_ENTRYPOINT
#include "Limitless.h"
#include "ExampleLayers.h"

namespace Limitless
{
	class SandboxApp : public Application
	{
	public:
		SandboxApp();
		virtual ~SandboxApp();

		// Override the virtual methods from Application
		bool Initialize() override;
		void Shutdown() override;

	private:
		// Layer references for management
		LayerRef m_BackgroundLayer;
		LayerRef m_GameLayer;
		LayerRef m_DebugOverlay;
		LayerRef m_UIOverlay;

		// Demo methods
		void DemonstrateLayerSystem();
	};
}