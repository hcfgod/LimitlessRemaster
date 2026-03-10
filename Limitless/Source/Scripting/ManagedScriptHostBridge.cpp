#include "Scripting/ManagedScriptHostInternal.h"

#include "Audio/AudioEngine.h"
#include "Core/Debug/Log.h"
#include "Scene/ParticleEmitterSystem.h"
#include "Scene/SceneManager.h"
#include "Scripting/Random.h"

namespace Limitless::ManagedScriptHost
{
    using namespace Internal;

    namespace Internal
    {
        const RuntimeInstance* FindRuntimeInstance(uint64_t instanceId)
        {
            const auto iterator = s_HostState.RuntimeInstances.find(instanceId);
            if (iterator == s_HostState.RuntimeInstances.end())
                return nullptr;
            return &iterator->second;
        }

        const RuntimeInstance* FindActiveRuntimeInstance()
        {
            return FindRuntimeInstance(s_HostState.ActiveRuntimeInstanceId);
        }

        std::string BuildManagedLogPrefix()
        {
            const RuntimeInstance* runtimeInstance = FindActiveRuntimeInstance();
            if (!runtimeInstance)
                return "Managed script";

            return "Managed script '" + runtimeInstance->ClassName + "' on entity " + std::to_string(runtimeInstance->EntityHandle);
        }

        void LogCoralMessage(std::string_view message, Coral::MessageLevel level)
        {
            if (level & Coral::MessageLevel::Error)
            {
                LT_ERROR("Managed scripting (Coral): {}", message);
                return;
            }
            if (level & Coral::MessageLevel::Warning)
            {
                LT_WARN("Managed scripting (Coral): {}", message);
                return;
            }
            if (level & Coral::MessageLevel::Info)
            {
                LT_INFO("Managed scripting (Coral): {}", message);
                return;
            }
            LT_TRACE("Managed scripting (Coral): {}", message);
        }

        void CaptureManagedException(std::string_view message)
        {
            s_HostState.LastManagedExceptionMessage.assign(message.begin(), message.end());
        }

        void RegisterInternalCallBatch(Coral::ManagedAssembly& contractAssembly,
                                       std::initializer_list<InternalCallBinding> bindings)
        {
            for (const InternalCallBinding& binding : bindings)
            {
                if (binding.MethodName == nullptr || binding.Function == nullptr)
                    continue;
                contractAssembly.AddInternalCall(kScriptBridgeTypeName, binding.MethodName, binding.Function);
            }
        }

        void RegisterInternalCalls(Coral::ManagedAssembly& contractAssembly)
        {
            const InternalCallRegistrar registrars[] = {
                &RegisterScenePhysicsInternalCalls,
                &RegisterAudioAnimationInternalCalls,
                &RegisterRenderingUiInternalCalls,
                &RegisterGridPhysicsInternalCalls
            };
            for (InternalCallRegistrar registrar : registrars)
                registrar(contractAssembly);
            contractAssembly.UploadInternalCalls();
        }

    }
}

