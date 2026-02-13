#pragma once

// Entry point
#ifdef LT_ENABLE_ENTRYPOINT
#include "Core/EntryPoint.h"
#endif

// Core
#include "Core/Application.h"
#include "Core/Error.h"
#include "Core/ConfigManager.h"
#include "Core/EventSystem.h"
#include "Core/Input/InputSystem.h"
#include "Core/Input/InputAction.h"
#include "Core/Debug/Log.h"
#include "Core/Layer.h"
#include "Core/LayerStack.h"
#include "Core/Time.h"

// Platform
#include "Platform/Platform.h"
#include "Platform/Window.h"

// Concurrency systems
#include "Core/Concurrency/LockFreeQueue.h"
#include "Core/Concurrency/AsyncIO.h"
#include "Core/PerformanceMonitor.h"

// Graphics
#include "Graphics/RenderCommand.h"
#include "Graphics/RenderCommandQueue.h"
#include "Graphics/GraphicsEnums.h"
#include "Graphics/Renderer.h"
#include "Graphics/Renderer2D.h"
#include "Graphics/Shader.h"
#include "Graphics/Buffer.h"
#include "Graphics/VertexArray.h"
#include "Graphics/Texture.h"
#include "Graphics/Camera/CameraManager.h"
#include "Graphics/Camera/OrthographicCamera2D.h"
#include "Graphics/Camera/PerspectiveCamera3D.h"

// Scene / ECS
#include "Scene/Components.h"
#include "Scene/Scene.h"
#include "Scripting/ScriptableEntity.h"
#include "Scripting/NativeScriptRegistry.h"

// Project System (Editor/tooling)
#include "Project/ProjectDefinition.h"
#include "Project/ProjectManager.h"
#include "Project/ProjectSettings.h"
#include "Project/BuildTargetsSettings.h"