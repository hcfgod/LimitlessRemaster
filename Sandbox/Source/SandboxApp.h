#pragma once

#define LT_ENABLE_ENTRYPOINT
#include "Limitless.h"

class SandboxApp : public Limitless::Application
{
public:
	SandboxApp()
	{
		// Initialization code for the sandbox application
	}
	virtual ~SandboxApp()
	{
		// Cleanup code for the sandbox application
	}

	// Override the virtual methods from Application
	bool Initialize() override;
	void Shutdown() override;
};