#pragma once

#define LT_ENABLE_ENTRYPOINT
#include "Limitless.h"

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
		// Demo methods
		void DemonstratePlatformDetection();
		void DemonstrateErrorHandling();
		void DemonstrateAsyncIO();
	};
}