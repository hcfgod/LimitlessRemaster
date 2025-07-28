#pragma once
#include "Application.h"

// This funtion must be defined by the client application
extern Limitless::Application* CreateApplication();

int main(int argc, char** argv)
{
	Limitless::Application* app = CreateApplication();

	if (app)
	{
		app->Run();
		delete app;
	}
	else
	{
		// Handle the case where the application could not be created
		return -1;
	}

	return 0;
}