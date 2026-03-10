#include "Scripting/ManagedScriptHostInternal.h"

#include "Audio/AudioEngine.h"
#include "Scene/ParticleEmitterSystem.h"

#include <algorithm>

namespace Limitless::ManagedScriptHost
{
    using namespace Internal;

    namespace Internal
    {
        LT_MANAGED_COMPONENT_HAS(HasAudioListener2DComponentIcall, TryGetManagedAudioListener2DComponent);
        LT_MANAGED_COMPONENT_GET(GetAudioListener2DEnabledIcall, bool, TryGetManagedAudioListener2DComponent, component->Enabled, true);
        LT_MANAGED_COMPONENT_SET(SetAudioListener2DEnabledIcall, bool, TryGetManagedAudioListener2DComponent, component->Enabled = value;);
        LT_MANAGED_COMPONENT_GET(GetAudioListener2DUsePrimaryCameraPositionIcall, bool, TryGetManagedAudioListener2DComponent, component->UsePrimaryCameraPosition, true);
        LT_MANAGED_COMPONENT_SET(SetAudioListener2DUsePrimaryCameraPositionIcall, bool, TryGetManagedAudioListener2DComponent, component->UsePrimaryCameraPosition = value;);

        LT_MANAGED_COMPONENT_HAS(HasAudioListener3DComponentIcall, TryGetManagedAudioListener3DComponent);
        LT_MANAGED_COMPONENT_GET(GetAudioListener3DEnabledIcall, bool, TryGetManagedAudioListener3DComponent, component->Enabled, true);
        LT_MANAGED_COMPONENT_SET(SetAudioListener3DEnabledIcall, bool, TryGetManagedAudioListener3DComponent, component->Enabled = value;);
        LT_MANAGED_COMPONENT_GET(GetAudioListener3DUsePrimaryCameraTransformIcall, bool, TryGetManagedAudioListener3DComponent, component->UsePrimaryCameraTransform, true);
        LT_MANAGED_COMPONENT_SET(SetAudioListener3DUsePrimaryCameraTransformIcall, bool, TryGetManagedAudioListener3DComponent, component->UsePrimaryCameraTransform = value;);

        LT_MANAGED_COMPONENT_HAS(HasAudioSourceComponentIcall, TryGetManagedAudioSourceComponent);
        LT_MANAGED_COMPONENT_GET(GetAudioSourceClipKeyIcall, Coral::String, TryGetManagedAudioSourceComponent, Coral::String::New(component->AudioClipKey), Coral::String::New(""));
        LT_MANAGED_COMPONENT_SET(SetAudioSourceClipKeyIcall, Coral::String, TryGetManagedAudioSourceComponent, component->AudioClipKey = ToUtf8Borrowed(value););
        LT_MANAGED_COMPONENT_GET(GetAudioSourceVolumeIcall, float, TryGetManagedAudioSourceComponent, component->Volume, 1.0f);
        LT_MANAGED_COMPONENT_SET(SetAudioSourceVolumeIcall, float, TryGetManagedAudioSourceComponent, component->Volume = std::max(0.0f, value););
        LT_MANAGED_COMPONENT_GET(GetAudioSourcePitchIcall, float, TryGetManagedAudioSourceComponent, component->Pitch, 1.0f);
        LT_MANAGED_COMPONENT_SET(SetAudioSourcePitchIcall, float, TryGetManagedAudioSourceComponent, component->Pitch = std::max(0.01f, value););
        LT_MANAGED_COMPONENT_GET(GetAudioSourcePlayOnStartIcall, bool, TryGetManagedAudioSourceComponent, component->PlayOnStart, true);
        LT_MANAGED_COMPONENT_SET(SetAudioSourcePlayOnStartIcall, bool, TryGetManagedAudioSourceComponent, component->PlayOnStart = value;);
        LT_MANAGED_COMPONENT_GET(GetAudioSourceLoopIcall, bool, TryGetManagedAudioSourceComponent, component->Loop, false);
        LT_MANAGED_COMPONENT_SET(SetAudioSourceLoopIcall, bool, TryGetManagedAudioSourceComponent, component->Loop = value;);
        LT_MANAGED_COMPONENT_GET(GetAudioSourceMutedIcall, bool, TryGetManagedAudioSourceComponent, component->Muted, false);
        LT_MANAGED_COMPONENT_SET(SetAudioSourceMutedIcall, bool, TryGetManagedAudioSourceComponent, component->Muted = value;);
        LT_MANAGED_COMPONENT_GET(GetAudioSourcePlaybackSpaceIcall, int, TryGetManagedAudioSourceComponent, static_cast<int>(component->Space), static_cast<int>(AudioSourceComponent::PlaybackSpace::Global));
        LT_MANAGED_COMPONENT_SET(SetAudioSourcePlaybackSpaceIcall, int, TryGetManagedAudioSourceComponent, component->Space = static_cast<AudioSourceComponent::PlaybackSpace>(value););
        LT_MANAGED_COMPONENT_GET(GetAudioSourceMixerGroupIcall, Coral::String, TryGetManagedAudioSourceComponent, Coral::String::New(component->MixerGroup), Coral::String::New("SFX"));
        LT_MANAGED_COMPONENT_SET(SetAudioSourceMixerGroupIcall, Coral::String, TryGetManagedAudioSourceComponent,
            component->MixerGroup = ToUtf8Borrowed(value);
            if (component->MixerGroup.empty())
                component->MixerGroup = "SFX";);
        LT_MANAGED_COMPONENT_GET(GetAudioSourceSpatialMinDistanceIcall, float, TryGetManagedAudioSourceComponent, component->SpatialMinDistance, 1.0f);
        LT_MANAGED_COMPONENT_SET(SetAudioSourceSpatialMinDistanceIcall, float, TryGetManagedAudioSourceComponent,
            component->SpatialMinDistance = std::max(0.001f, value);
            component->SpatialMaxDistance = std::max(component->SpatialMinDistance, component->SpatialMaxDistance););
        LT_MANAGED_COMPONENT_GET(GetAudioSourceSpatialMaxDistanceIcall, float, TryGetManagedAudioSourceComponent, component->SpatialMaxDistance, 20.0f);
        LT_MANAGED_COMPONENT_SET(SetAudioSourceSpatialMaxDistanceIcall, float, TryGetManagedAudioSourceComponent, component->SpatialMaxDistance = std::max(component->SpatialMinDistance, value););
        LT_MANAGED_COMPONENT_GET(GetAudioSourceSpatialRolloffExponentIcall, float, TryGetManagedAudioSourceComponent, component->SpatialRolloffExponent, 1.0f);
        LT_MANAGED_COMPONENT_SET(SetAudioSourceSpatialRolloffExponentIcall, float, TryGetManagedAudioSourceComponent, component->SpatialRolloffExponent = std::max(0.01f, value););
        LT_MANAGED_COMPONENT_GET(GetAudioSourceStereoPanStrengthIcall, float, TryGetManagedAudioSourceComponent, component->StereoPanStrength, 1.0f);
        LT_MANAGED_COMPONENT_SET(SetAudioSourceStereoPanStrengthIcall, float, TryGetManagedAudioSourceComponent, component->StereoPanStrength = std::clamp(value, 0.0f, 1.0f););
        LT_MANAGED_COMPONENT_GET(GetAudioSourceSpatialRolloffModeIcall, int, TryGetManagedAudioSourceComponent, static_cast<int>(component->SpatialRolloffMode), static_cast<int>(AudioSourceComponent::RolloffMode::Linear));
        LT_MANAGED_COMPONENT_SET(SetAudioSourceSpatialRolloffModeIcall, int, TryGetManagedAudioSourceComponent, component->SpatialRolloffMode = static_cast<AudioSourceComponent::RolloffMode>(value););
        LT_MANAGED_COMPONENT_GET(GetAudioSourceDopplerFactorIcall, float, TryGetManagedAudioSourceComponent, component->DopplerFactor, 1.0f);
        LT_MANAGED_COMPONENT_SET(SetAudioSourceDopplerFactorIcall, float, TryGetManagedAudioSourceComponent, component->DopplerFactor = std::max(0.0f, value););
        LT_MANAGED_COMPONENT_GET(GetAudioSourceEnableDirectionalAttenuationIcall, bool, TryGetManagedAudioSourceComponent, component->EnableDirectionalAttenuation, false);
        LT_MANAGED_COMPONENT_SET(SetAudioSourceEnableDirectionalAttenuationIcall, bool, TryGetManagedAudioSourceComponent, component->EnableDirectionalAttenuation = value;);

        float ManagedGetAudioSourceDirectionalInnerAngleDegreesIcall(uint32_t entityHandle)
        {
            const auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle);
            return audioSource ? audioSource->DirectionalInnerAngleDegrees : 360.0f;
        }

        void ManagedSetAudioSourceDirectionalInnerAngleDegreesIcall(uint32_t entityHandle, float value)
        {
            if (auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle))
                audioSource->DirectionalInnerAngleDegrees = std::max(0.0f, value);
        }

        float ManagedGetAudioSourceDirectionalOuterAngleDegreesIcall(uint32_t entityHandle)
        {
            const auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle);
            return audioSource ? audioSource->DirectionalOuterAngleDegrees : 360.0f;
        }

        void ManagedSetAudioSourceDirectionalOuterAngleDegreesIcall(uint32_t entityHandle, float value)
        {
            if (auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle))
                audioSource->DirectionalOuterAngleDegrees = std::max(0.0f, value);
        }

        float ManagedGetAudioSourceDirectionalOuterVolumeIcall(uint32_t entityHandle)
        {
            const auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle);
            return audioSource ? audioSource->DirectionalOuterVolume : 1.0f;
        }

        void ManagedSetAudioSourceDirectionalOuterVolumeIcall(uint32_t entityHandle, float value)
        {
            if (auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle))
                audioSource->DirectionalOuterVolume = std::clamp(value, 0.0f, 1.0f);
        }

        Coral::String ManagedGetAudioSourceAttenuationCurveKeyIcall(uint32_t entityHandle)
        {
            const auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle);
            return audioSource ? Coral::String::New(audioSource->AttenuationCurveKey) : Coral::String::New("");
        }

        void ManagedSetAudioSourceAttenuationCurveKeyIcall(uint32_t entityHandle, Coral::String value)
        {
            if (auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle))
                audioSource->AttenuationCurveKey = ToUtf8Borrowed(value);
        }

        bool ManagedGetAudioSourceIsPlayingIcall(uint32_t entityHandle)
        {
            const auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle);
            return audioSource != nullptr &&
                audioSource->RuntimeVoiceId != 0 &&
                Audio::AudioEngine::GetInstance().IsVoiceActive(audioSource->RuntimeVoiceId);
        }

        bool ManagedRequestAudioSourcePlayIcall(uint32_t entityHandle)
        {
            auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle);
            if (audioSource == nullptr || audioSource->AudioClipKey.empty())
                return false;

            if (audioSource->RuntimeVoiceId != 0)
                Audio::AudioEngine::GetInstance().Stop(audioSource->RuntimeVoiceId);

            audioSource->RuntimeVoiceId = 0;
            audioSource->RuntimePlaybackStarted = false;
            audioSource->RuntimePlayRequested = true;
            return true;
        }

        void ManagedStopAudioSourceIcall(uint32_t entityHandle)
        {
            if (auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle))
            {
                if (audioSource->RuntimeVoiceId != 0)
                    Audio::AudioEngine::GetInstance().Stop(audioSource->RuntimeVoiceId);
                audioSource->RuntimeVoiceId = 0;
                audioSource->RuntimePlaybackStarted = false;
                audioSource->RuntimePlayRequested = false;
                audioSource->RuntimePlayOnStartConsumed = true;
            }
        }

        bool ManagedHasAnimatorComponentIcall(uint32_t entityHandle)
        {
            return TryGetManagedAnimatorComponent(entityHandle) != nullptr;
        }

        Coral::String ManagedGetAnimatorControllerKeyIcall(uint32_t entityHandle)
        {
            const auto* animator = TryGetManagedAnimatorComponent(entityHandle);
            return animator ? Coral::String::New(animator->ControllerKey) : Coral::String::New("");
        }

        void ManagedSetAnimatorControllerKeyIcall(uint32_t entityHandle, Coral::String value)
        {
            if (auto* animator = TryGetManagedAnimatorComponent(entityHandle))
            {
                animator->ControllerKey = ToUtf8Borrowed(value);
                animator->CachedController.reset();
                animator->ControllerLoadAttempted = false;
                animator->RuntimeInitialized = false;
            }
        }

        Coral::String ManagedGetAnimatorDefaultClipKeyIcall(uint32_t entityHandle)
        {
            const auto* animator = TryGetManagedAnimatorComponent(entityHandle);
            return animator ? Coral::String::New(animator->DefaultClipKey) : Coral::String::New("");
        }

        void ManagedSetAnimatorDefaultClipKeyIcall(uint32_t entityHandle, Coral::String value)
        {
            if (auto* animator = TryGetManagedAnimatorComponent(entityHandle))
            {
                animator->DefaultClipKey = ToUtf8Borrowed(value);
                animator->CachedDefaultClip.reset();
                animator->DefaultClipLoadAttempted = false;
            }
        }

        float ManagedGetAnimatorPlaybackSpeedIcall(uint32_t entityHandle)
        {
            const auto* animator = TryGetManagedAnimatorComponent(entityHandle);
            return animator ? animator->PlaybackSpeed : 1.0f;
        }

        void ManagedSetAnimatorPlaybackSpeedIcall(uint32_t entityHandle, float value)
        {
            if (auto* animator = TryGetManagedAnimatorComponent(entityHandle))
                animator->PlaybackSpeed = value;
        }

        bool ManagedGetAnimatorEnabledIcall(uint32_t entityHandle)
        {
            const auto* animator = TryGetManagedAnimatorComponent(entityHandle);
            return animator ? animator->Enabled : true;
        }

        void ManagedSetAnimatorEnabledIcall(uint32_t entityHandle, bool value)
        {
            if (auto* animator = TryGetManagedAnimatorComponent(entityHandle))
                animator->Enabled = value;
        }

        bool ManagedGetAnimatorApplyToSpriteIcall(uint32_t entityHandle)
        {
            const auto* animator = TryGetManagedAnimatorComponent(entityHandle);
            return animator ? animator->ApplyToSprite : true;
        }

        void ManagedSetAnimatorApplyToSpriteIcall(uint32_t entityHandle, bool value)
        {
            if (auto* animator = TryGetManagedAnimatorComponent(entityHandle))
                animator->ApplyToSprite = value;
        }

        bool ManagedGetAnimatorApplyToTransformIcall(uint32_t entityHandle)
        {
            const auto* animator = TryGetManagedAnimatorComponent(entityHandle);
            return animator ? animator->ApplyToTransform : true;
        }

        void ManagedSetAnimatorApplyToTransformIcall(uint32_t entityHandle, bool value)
        {
            if (auto* animator = TryGetManagedAnimatorComponent(entityHandle))
                animator->ApplyToTransform = value;
        }

        bool ManagedGetAnimatorAutoPlayIcall(uint32_t entityHandle)
        {
            const auto* animator = TryGetManagedAnimatorComponent(entityHandle);
            return animator ? animator->AutoPlay : true;
        }

        void ManagedSetAnimatorAutoPlayIcall(uint32_t entityHandle, bool value)
        {
            if (auto* animator = TryGetManagedAnimatorComponent(entityHandle))
                animator->AutoPlay = value;
        }

        bool ManagedPlayAnimatorStateIcall(uint32_t entityHandle, Coral::String stateName, bool restartIfSameState)
        {
            auto* animator = TryGetManagedAnimatorComponent(entityHandle);
            if (animator == nullptr)
                return false;

            const std::string requestedState = ToUtf8Borrowed(stateName);
            if (requestedState.empty())
                return false;
            if (!restartIfSameState && animator->RuntimeCurrentStateName == requestedState)
                return true;

            animator->RuntimeCurrentStateName = requestedState;
            animator->RuntimeCurrentClipKey.clear();
            animator->RuntimeStateTimeSeconds = 0.0f;
            animator->RuntimePreviousStateTimeSeconds = 0.0f;
            animator->RuntimeCurrentStateDurationSeconds = 1.0f;
            animator->RuntimeInitialized = true;
            return true;
        }

        bool ManagedPlayAnimatorClipIcall(uint32_t entityHandle, Coral::String clipKey, bool restartIfSameClip)
        {
            auto* animator = TryGetManagedAnimatorComponent(entityHandle);
            if (animator == nullptr)
                return false;

            const std::string requestedClip = ToUtf8Borrowed(clipKey);
            if (requestedClip.empty())
                return false;
            if (!restartIfSameClip && animator->RuntimeCurrentClipKey == requestedClip)
                return true;

            animator->RuntimeCurrentStateName.clear();
            animator->RuntimeCurrentClipKey = requestedClip;
            animator->RuntimeStateTimeSeconds = 0.0f;
            animator->RuntimePreviousStateTimeSeconds = 0.0f;
            animator->RuntimeCurrentStateDurationSeconds = 1.0f;
            animator->RuntimeInitialized = true;
            return true;
        }

        bool ManagedSetAnimatorBoolParameterIcall(uint32_t entityHandle, Coral::String parameterName, bool value)
        {
            auto* animator = TryGetManagedAnimatorComponent(entityHandle);
            if (animator == nullptr)
                return false;

            const std::string parameter = ToUtf8Borrowed(parameterName);
            if (parameter.empty())
                return false;
            animator->SetBoolParameter(parameter, value);
            return true;
        }

        bool ManagedGetAnimatorBoolParameterIcall(uint32_t entityHandle, Coral::String parameterName, bool fallback)
        {
            const auto* animator = TryGetManagedAnimatorComponent(entityHandle);
            if (animator == nullptr)
                return fallback;

            const std::string parameter = ToUtf8Borrowed(parameterName);
            if (parameter.empty())
                return fallback;
            return animator->GetBoolParameter(parameter, fallback);
        }

        bool ManagedSetAnimatorFloatParameterIcall(uint32_t entityHandle, Coral::String parameterName, float value)
        {
            auto* animator = TryGetManagedAnimatorComponent(entityHandle);
            if (animator == nullptr)
                return false;

            const std::string parameter = ToUtf8Borrowed(parameterName);
            if (parameter.empty())
                return false;
            animator->SetFloatParameter(parameter, value);
            return true;
        }

        float ManagedGetAnimatorFloatParameterIcall(uint32_t entityHandle, Coral::String parameterName, float fallback)
        {
            const auto* animator = TryGetManagedAnimatorComponent(entityHandle);
            if (animator == nullptr)
                return fallback;

            const std::string parameter = ToUtf8Borrowed(parameterName);
            if (parameter.empty())
                return fallback;
            return animator->GetFloatParameter(parameter, fallback);
        }

        bool ManagedSetAnimatorIntegerParameterIcall(uint32_t entityHandle, Coral::String parameterName, int value)
        {
            auto* animator = TryGetManagedAnimatorComponent(entityHandle);
            if (animator == nullptr)
                return false;

            const std::string parameter = ToUtf8Borrowed(parameterName);
            if (parameter.empty())
                return false;
            animator->SetIntegerParameter(parameter, value);
            return true;
        }

        int ManagedGetAnimatorIntegerParameterIcall(uint32_t entityHandle, Coral::String parameterName, int fallback)
        {
            const auto* animator = TryGetManagedAnimatorComponent(entityHandle);
            if (animator == nullptr)
                return fallback;

            const std::string parameter = ToUtf8Borrowed(parameterName);
            if (parameter.empty())
                return fallback;
            return animator->GetIntegerParameter(parameter, fallback);
        }

        bool ManagedSetAnimatorTriggerParameterIcall(uint32_t entityHandle, Coral::String parameterName)
        {
            auto* animator = TryGetManagedAnimatorComponent(entityHandle);
            if (animator == nullptr)
                return false;

            const std::string parameter = ToUtf8Borrowed(parameterName);
            if (parameter.empty())
                return false;
            animator->SetTrigger(parameter);
            return true;
        }

        bool ManagedResetAnimatorTriggerParameterIcall(uint32_t entityHandle, Coral::String parameterName)
        {
            auto* animator = TryGetManagedAnimatorComponent(entityHandle);
            if (animator == nullptr)
                return false;

            const std::string parameter = ToUtf8Borrowed(parameterName);
            if (parameter.empty())
                return false;
            animator->ResetTrigger(parameter);
            return true;
        }

        Coral::String ManagedGetAnimatorCurrentStateNameIcall(uint32_t entityHandle)
        {
            const auto* animator = TryGetManagedAnimatorComponent(entityHandle);
            return animator ? Coral::String::New(animator->RuntimeCurrentStateName) : Coral::String::New("");
        }

        Coral::String ManagedGetAnimatorCurrentClipKeyIcall(uint32_t entityHandle)
        {
            const auto* animator = TryGetManagedAnimatorComponent(entityHandle);
            return animator ? Coral::String::New(animator->RuntimeCurrentClipKey) : Coral::String::New("");
        }

        float ManagedGetAnimatorStateTimeSecondsIcall(uint32_t entityHandle)
        {
            const auto* animator = TryGetManagedAnimatorComponent(entityHandle);
            return animator ? animator->RuntimeStateTimeSeconds : 0.0f;
        }

        float ManagedGetAnimatorCurrentStateDurationSecondsIcall(uint32_t entityHandle)
        {
            const auto* animator = TryGetManagedAnimatorComponent(entityHandle);
            return animator ? animator->RuntimeCurrentStateDurationSeconds : 0.0f;
        }

        LT_MANAGED_COMPONENT_HAS(HasAnimationEventReceiverComponentIcall, TryGetManagedAnimationEventReceiverComponent);
        LT_MANAGED_COMPONENT_GET(GetAnimationEventReceiverEnabledIcall, bool, TryGetManagedAnimationEventReceiverComponent, component->Enabled, true);
        LT_MANAGED_COMPONENT_SET(SetAnimationEventReceiverEnabledIcall, bool, TryGetManagedAnimationEventReceiverComponent, component->Enabled = value;);
        LT_MANAGED_COMPONENT_GET(GetAnimationEventReceiverDispatchedEventCountIcall, int, TryGetManagedAnimationEventReceiverComponent, static_cast<int>(component->RuntimeDispatchedEvents.size()), 0);

        Coral::String ManagedGetAnimationEventReceiverEventNameIcall(uint32_t entityHandle, int index)
        {
            const auto* receiver = TryGetManagedAnimationEventReceiverComponent(entityHandle);
            if (receiver == nullptr || index < 0 || index >= static_cast<int>(receiver->RuntimeDispatchedEvents.size()))
                return Coral::String::New("");
            return Coral::String::New(receiver->RuntimeDispatchedEvents[static_cast<size_t>(index)].Name);
        }

        Coral::String ManagedGetAnimationEventReceiverEventStringPayloadIcall(uint32_t entityHandle, int index)
        {
            const auto* receiver = TryGetManagedAnimationEventReceiverComponent(entityHandle);
            if (receiver == nullptr || index < 0 || index >= static_cast<int>(receiver->RuntimeDispatchedEvents.size()))
                return Coral::String::New("");
            return Coral::String::New(receiver->RuntimeDispatchedEvents[static_cast<size_t>(index)].StringPayload);
        }

        float ManagedGetAnimationEventReceiverEventFloatPayloadIcall(uint32_t entityHandle, int index)
        {
            const auto* receiver = TryGetManagedAnimationEventReceiverComponent(entityHandle);
            if (receiver == nullptr || index < 0 || index >= static_cast<int>(receiver->RuntimeDispatchedEvents.size()))
                return 0.0f;
            return receiver->RuntimeDispatchedEvents[static_cast<size_t>(index)].FloatPayload;
        }

        int ManagedGetAnimationEventReceiverEventIntegerPayloadIcall(uint32_t entityHandle, int index)
        {
            const auto* receiver = TryGetManagedAnimationEventReceiverComponent(entityHandle);
            if (receiver == nullptr || index < 0 || index >= static_cast<int>(receiver->RuntimeDispatchedEvents.size()))
                return 0;
            return receiver->RuntimeDispatchedEvents[static_cast<size_t>(index)].IntegerPayload;
        }

        bool ManagedGetAnimationEventReceiverEventBooleanPayloadIcall(uint32_t entityHandle, int index)
        {
            const auto* receiver = TryGetManagedAnimationEventReceiverComponent(entityHandle);
            if (receiver == nullptr || index < 0 || index >= static_cast<int>(receiver->RuntimeDispatchedEvents.size()))
                return false;
            return receiver->RuntimeDispatchedEvents[static_cast<size_t>(index)].BooleanPayload;
        }

        float ManagedGetAnimationEventReceiverEventTimeSecondsIcall(uint32_t entityHandle, int index)
        {
            const auto* receiver = TryGetManagedAnimationEventReceiverComponent(entityHandle);
            if (receiver == nullptr || index < 0 || index >= static_cast<int>(receiver->RuntimeDispatchedEvents.size()))
                return 0.0f;
            return receiver->RuntimeDispatchedEvents[static_cast<size_t>(index)].TimeSeconds;
        }

        float ManagedGetAnimationEventReceiverEventNormalizedTimeIcall(uint32_t entityHandle, int index)
        {
            const auto* receiver = TryGetManagedAnimationEventReceiverComponent(entityHandle);
            if (receiver == nullptr || index < 0 || index >= static_cast<int>(receiver->RuntimeDispatchedEvents.size()))
                return 0.0f;
            return receiver->RuntimeDispatchedEvents[static_cast<size_t>(index)].NormalizedTime;
        }

        LT_MANAGED_COMPONENT_HAS(HasParticleEmitterComponentIcall, TryGetManagedParticleEmitterComponent);
        LT_MANAGED_COMPONENT_GET(GetParticleEmitterSpawnRateIcall, float, TryGetManagedParticleEmitterComponent, component->SpawnRate, 10.0f);
        LT_MANAGED_COMPONENT_SET(SetParticleEmitterSpawnRateIcall, float, TryGetManagedParticleEmitterComponent, component->SpawnRate = std::max(0.0f, value););
        LT_MANAGED_COMPONENT_GET(GetParticleEmitterLifetimeMinIcall, float, TryGetManagedParticleEmitterComponent, component->LifetimeMin, 1.0f);
        LT_MANAGED_COMPONENT_SET(SetParticleEmitterLifetimeMinIcall, float, TryGetManagedParticleEmitterComponent,
            component->LifetimeMin = std::max(0.0f, value);
            component->LifetimeMax = std::max(component->LifetimeMin, component->LifetimeMax););
        LT_MANAGED_COMPONENT_GET(GetParticleEmitterLifetimeMaxIcall, float, TryGetManagedParticleEmitterComponent, component->LifetimeMax, 2.0f);
        LT_MANAGED_COMPONENT_SET(SetParticleEmitterLifetimeMaxIcall, float, TryGetManagedParticleEmitterComponent, component->LifetimeMax = std::max(component->LifetimeMin, value););
        LT_MANAGED_COMPONENT_GET(GetParticleEmitterLoopingIcall, bool, TryGetManagedParticleEmitterComponent, component->Looping, true);
        LT_MANAGED_COMPONENT_SET(SetParticleEmitterLoopingIcall, bool, TryGetManagedParticleEmitterComponent, component->Looping = value;);
        LT_MANAGED_COMPONENT_GET(GetParticleEmitterDurationIcall, float, TryGetManagedParticleEmitterComponent, component->Duration, 5.0f);
        LT_MANAGED_COMPONENT_SET(SetParticleEmitterDurationIcall, float, TryGetManagedParticleEmitterComponent, component->Duration = std::max(0.0f, value););
        LT_MANAGED_COMPONENT_GET(GetParticleEmitterPlayOnStartIcall, bool, TryGetManagedParticleEmitterComponent, component->PlayOnStart, true);
        LT_MANAGED_COMPONENT_SET(SetParticleEmitterPlayOnStartIcall, bool, TryGetManagedParticleEmitterComponent, component->PlayOnStart = value;);
        LT_MANAGED_COMPONENT_GET(GetParticleEmitterBurstEnabledIcall, bool, TryGetManagedParticleEmitterComponent, component->BurstEnabled, false);
        LT_MANAGED_COMPONENT_SET(SetParticleEmitterBurstEnabledIcall, bool, TryGetManagedParticleEmitterComponent, component->BurstEnabled = value;);
        LT_MANAGED_COMPONENT_GET(GetParticleEmitterBurstCountIcall, int, TryGetManagedParticleEmitterComponent, static_cast<int>(component->BurstCount), 10);
        LT_MANAGED_COMPONENT_SET(SetParticleEmitterBurstCountIcall, int, TryGetManagedParticleEmitterComponent, component->BurstCount = static_cast<uint32_t>(std::clamp(value, 0, static_cast<int>(ParticleEmitterComponent::kMaxParticlesCap))););
        LT_MANAGED_COMPONENT_GET(GetParticleEmitterSpawnOffsetMinIcall, ManagedVector2, TryGetManagedParticleEmitterComponent, ToManagedVector2(component->SpawnOffsetMin), ManagedVector2{});
        LT_MANAGED_COMPONENT_SET(SetParticleEmitterSpawnOffsetMinIcall, ManagedVector2, TryGetManagedParticleEmitterComponent, component->SpawnOffsetMin = ToGlmVector2(value););
        LT_MANAGED_COMPONENT_GET(GetParticleEmitterSpawnOffsetMaxIcall, ManagedVector2, TryGetManagedParticleEmitterComponent, ToManagedVector2(component->SpawnOffsetMax), ManagedVector2{});
        LT_MANAGED_COMPONENT_SET(SetParticleEmitterSpawnOffsetMaxIcall, ManagedVector2, TryGetManagedParticleEmitterComponent, component->SpawnOffsetMax = ToGlmVector2(value););
        LT_MANAGED_COMPONENT_GET(GetParticleEmitterUseRadialSpawnIcall, bool, TryGetManagedParticleEmitterComponent, component->UseRadialSpawn, false);
        LT_MANAGED_COMPONENT_SET(SetParticleEmitterUseRadialSpawnIcall, bool, TryGetManagedParticleEmitterComponent, component->UseRadialSpawn = value;);

        float ManagedGetParticleEmitterSpawnRadiusMinIcall(uint32_t entityHandle)
        {
            const auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            return emitter ? emitter->SpawnRadiusMin : 0.0f;
        }

        void ManagedSetParticleEmitterSpawnRadiusMinIcall(uint32_t entityHandle, float value)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
            {
                emitter->SpawnRadiusMin = std::max(0.0f, value);
                emitter->SpawnRadiusMax = std::max(emitter->SpawnRadiusMin, emitter->SpawnRadiusMax);
            }
        }

        float ManagedGetParticleEmitterSpawnRadiusMaxIcall(uint32_t entityHandle)
        {
            const auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            return emitter ? emitter->SpawnRadiusMax : 0.0f;
        }

        void ManagedSetParticleEmitterSpawnRadiusMaxIcall(uint32_t entityHandle, float value)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
                emitter->SpawnRadiusMax = std::max(emitter->SpawnRadiusMin, value);
        }

        float ManagedGetParticleEmitterSpeedMinIcall(uint32_t entityHandle)
        {
            const auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            return emitter ? emitter->SpeedMin : 50.0f;
        }

        void ManagedSetParticleEmitterSpeedMinIcall(uint32_t entityHandle, float value)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
            {
                emitter->SpeedMin = std::max(0.0f, value);
                emitter->SpeedMax = std::max(emitter->SpeedMin, emitter->SpeedMax);
            }
        }

        float ManagedGetParticleEmitterSpeedMaxIcall(uint32_t entityHandle)
        {
            const auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            return emitter ? emitter->SpeedMax : 100.0f;
        }

        void ManagedSetParticleEmitterSpeedMaxIcall(uint32_t entityHandle, float value)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
                emitter->SpeedMax = std::max(emitter->SpeedMin, value);
        }

        float ManagedGetParticleEmitterAngleMinIcall(uint32_t entityHandle)
        {
            const auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            return emitter ? emitter->AngleMin : 0.0f;
        }

        void ManagedSetParticleEmitterAngleMinIcall(uint32_t entityHandle, float value)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
                emitter->AngleMin = value;
        }

        float ManagedGetParticleEmitterAngleMaxIcall(uint32_t entityHandle)
        {
            const auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            return emitter ? emitter->AngleMax : 360.0f;
        }

        void ManagedSetParticleEmitterAngleMaxIcall(uint32_t entityHandle, float value)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
                emitter->AngleMax = value;
        }

        bool ManagedGetParticleEmitterRadialVelocityIcall(uint32_t entityHandle)
        {
            const auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            return emitter ? emitter->RadialVelocity : false;
        }

        void ManagedSetParticleEmitterRadialVelocityIcall(uint32_t entityHandle, bool value)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
                emitter->RadialVelocity = value;
        }

        float ManagedGetParticleEmitterGravityModifierIcall(uint32_t entityHandle)
        {
            const auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            return emitter ? emitter->GravityModifier : 0.0f;
        }

        void ManagedSetParticleEmitterGravityModifierIcall(uint32_t entityHandle, float value)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
                emitter->GravityModifier = value;
        }

        float ManagedGetParticleEmitterStartSizeMinIcall(uint32_t entityHandle)
        {
            const auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            return emitter ? emitter->StartSizeMin : 1.0f;
        }

        void ManagedSetParticleEmitterStartSizeMinIcall(uint32_t entityHandle, float value)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
            {
                emitter->StartSizeMin = std::max(0.0f, value);
                emitter->StartSizeMax = std::max(emitter->StartSizeMin, emitter->StartSizeMax);
            }
        }

        float ManagedGetParticleEmitterStartSizeMaxIcall(uint32_t entityHandle)
        {
            const auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            return emitter ? emitter->StartSizeMax : 1.0f;
        }

        void ManagedSetParticleEmitterStartSizeMaxIcall(uint32_t entityHandle, float value)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
                emitter->StartSizeMax = std::max(emitter->StartSizeMin, value);
        }

        float ManagedGetParticleEmitterEndSizeIcall(uint32_t entityHandle)
        {
            const auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            return emitter ? emitter->EndSize : 0.0f;
        }

        void ManagedSetParticleEmitterEndSizeIcall(uint32_t entityHandle, float value)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
                emitter->EndSize = std::max(0.0f, value);
        }

        ManagedVector4 ManagedGetParticleEmitterStartColorIcall(uint32_t entityHandle)
        {
            const auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            return emitter ? ToManagedVector4(emitter->StartColor) : ManagedVector4{ 1.0f, 1.0f, 1.0f, 1.0f };
        }

        void ManagedSetParticleEmitterStartColorIcall(uint32_t entityHandle, ManagedVector4 value)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
                emitter->StartColor = ToGlmVector4(value);
        }

        ManagedVector4 ManagedGetParticleEmitterEndColorIcall(uint32_t entityHandle)
        {
            const auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            return emitter ? ToManagedVector4(emitter->EndColor) : ManagedVector4{ 1.0f, 1.0f, 1.0f, 0.0f };
        }

        void ManagedSetParticleEmitterEndColorIcall(uint32_t entityHandle, ManagedVector4 value)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
                emitter->EndColor = ToGlmVector4(value);
        }

        float ManagedGetParticleEmitterStartRotationMinIcall(uint32_t entityHandle)
        {
            const auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            return emitter ? emitter->StartRotationMin : 0.0f;
        }

        void ManagedSetParticleEmitterStartRotationMinIcall(uint32_t entityHandle, float value)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
                emitter->StartRotationMin = value;
        }

        float ManagedGetParticleEmitterStartRotationMaxIcall(uint32_t entityHandle)
        {
            const auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            return emitter ? emitter->StartRotationMax : 0.0f;
        }

        void ManagedSetParticleEmitterStartRotationMaxIcall(uint32_t entityHandle, float value)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
                emitter->StartRotationMax = value;
        }

        float ManagedGetParticleEmitterRotationSpeedMinIcall(uint32_t entityHandle)
        {
            const auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            return emitter ? emitter->RotationSpeedMin : 0.0f;
        }

        void ManagedSetParticleEmitterRotationSpeedMinIcall(uint32_t entityHandle, float value)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
                emitter->RotationSpeedMin = value;
        }

        float ManagedGetParticleEmitterRotationSpeedMaxIcall(uint32_t entityHandle)
        {
            const auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            return emitter ? emitter->RotationSpeedMax : 0.0f;
        }

        void ManagedSetParticleEmitterRotationSpeedMaxIcall(uint32_t entityHandle, float value)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
                emitter->RotationSpeedMax = value;
        }

        Coral::String ManagedGetParticleEmitterTextureKeyIcall(uint32_t entityHandle)
        {
            const auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            return emitter ? Coral::String::New(emitter->TextureKey) : Coral::String::New("");
        }

        void ManagedSetParticleEmitterTextureKeyIcall(uint32_t entityHandle, Coral::String value)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
            {
                emitter->TextureKey = ToUtf8Borrowed(value);
                emitter->CachedTexture.reset();
                emitter->TextureLoadAttempted = false;
            }
        }

        int ManagedGetParticleEmitterMaxParticlesIcall(uint32_t entityHandle)
        {
            const auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            return emitter ? static_cast<int>(emitter->MaxParticles) : 1024;
        }

        void ManagedSetParticleEmitterMaxParticlesIcall(uint32_t entityHandle, int value)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
            {
                emitter->MaxParticles = static_cast<uint32_t>(std::clamp(value, 0, static_cast<int>(ParticleEmitterComponent::kMaxParticlesCap)));
                if (emitter->RuntimeState)
                    emitter->RuntimeState->Allocate(emitter->MaxParticles);
            }
        }

        bool ManagedGetParticleEmitterIsPlayingIcall(uint32_t entityHandle)
        {
            const auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            return emitter ? emitter->Playing : false;
        }

        bool ManagedGetParticleEmitterIsPausedIcall(uint32_t entityHandle)
        {
            const auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            return emitter ? emitter->Paused : false;
        }

        int ManagedGetParticleEmitterAliveParticleCountIcall(uint32_t entityHandle)
        {
            const auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            return (emitter && emitter->RuntimeState) ? static_cast<int>(emitter->RuntimeState->AliveCount) : 0;
        }

        void ManagedPlayParticleEmitterIcall(uint32_t entityHandle)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
                ParticleEmitterPlay(*emitter);
        }

        void ManagedStopParticleEmitterIcall(uint32_t entityHandle, bool clearParticles)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
                ParticleEmitterStop(*emitter, clearParticles);
        }

        void ManagedPauseParticleEmitterIcall(uint32_t entityHandle)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
                ParticleEmitterPause(*emitter);
        }

        void ManagedResumeParticleEmitterIcall(uint32_t entityHandle)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
                ParticleEmitterResume(*emitter);
        }

        void ManagedEmitParticleEmitterIcall(uint32_t entityHandle, int count)
        {
            auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            if (emitter == nullptr || count <= 0)
                return;

            glm::vec2 worldPosition(0.0f);
            if (s_HostState.ActiveScene != nullptr)
            {
                const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
                if (entity != entt::null)
                {
                    const glm::mat4 worldTransform = s_HostState.ActiveScene->GetWorldTransformMatrix(entity);
                    worldPosition = glm::vec2(worldTransform[3][0], worldTransform[3][1]);
                }
            }

            ParticleEmitterEmit(*emitter, static_cast<uint32_t>(count), worldPosition);
        }

        void RegisterAudioAnimationInternalCalls(Coral::ManagedAssembly& contractAssembly)
        {
            RegisterInternalCallBatch(contractAssembly, {
                LT_MANAGED_INTERNAL_CALL(HasAudioListener2DComponentIcall),
                LT_MANAGED_INTERNAL_CALL(GetAudioListener2DEnabledIcall),
                LT_MANAGED_INTERNAL_CALL(SetAudioListener2DEnabledIcall),
                LT_MANAGED_INTERNAL_CALL(GetAudioListener2DUsePrimaryCameraPositionIcall),
                LT_MANAGED_INTERNAL_CALL(SetAudioListener2DUsePrimaryCameraPositionIcall),
                LT_MANAGED_INTERNAL_CALL(HasAudioListener3DComponentIcall),
                LT_MANAGED_INTERNAL_CALL(GetAudioListener3DEnabledIcall),
                LT_MANAGED_INTERNAL_CALL(SetAudioListener3DEnabledIcall),
                LT_MANAGED_INTERNAL_CALL(GetAudioListener3DUsePrimaryCameraTransformIcall),
                LT_MANAGED_INTERNAL_CALL(SetAudioListener3DUsePrimaryCameraTransformIcall),
                LT_MANAGED_INTERNAL_CALL(HasAudioSourceComponentIcall),
                LT_MANAGED_INTERNAL_CALL(GetAudioSourceClipKeyIcall),
                LT_MANAGED_INTERNAL_CALL(SetAudioSourceClipKeyIcall),
                LT_MANAGED_INTERNAL_CALL(GetAudioSourceVolumeIcall),
                LT_MANAGED_INTERNAL_CALL(SetAudioSourceVolumeIcall),
                LT_MANAGED_INTERNAL_CALL(GetAudioSourcePitchIcall),
                LT_MANAGED_INTERNAL_CALL(SetAudioSourcePitchIcall),
                LT_MANAGED_INTERNAL_CALL(GetAudioSourcePlayOnStartIcall),
                LT_MANAGED_INTERNAL_CALL(SetAudioSourcePlayOnStartIcall),
                LT_MANAGED_INTERNAL_CALL(GetAudioSourceLoopIcall),
                LT_MANAGED_INTERNAL_CALL(SetAudioSourceLoopIcall),
                LT_MANAGED_INTERNAL_CALL(GetAudioSourceMutedIcall),
                LT_MANAGED_INTERNAL_CALL(SetAudioSourceMutedIcall),
                LT_MANAGED_INTERNAL_CALL(GetAudioSourcePlaybackSpaceIcall),
                LT_MANAGED_INTERNAL_CALL(SetAudioSourcePlaybackSpaceIcall),
                LT_MANAGED_INTERNAL_CALL(GetAudioSourceMixerGroupIcall),
                LT_MANAGED_INTERNAL_CALL(SetAudioSourceMixerGroupIcall),
                LT_MANAGED_INTERNAL_CALL(GetAudioSourceSpatialMinDistanceIcall),
                LT_MANAGED_INTERNAL_CALL(SetAudioSourceSpatialMinDistanceIcall),
                LT_MANAGED_INTERNAL_CALL(GetAudioSourceSpatialMaxDistanceIcall),
                LT_MANAGED_INTERNAL_CALL(SetAudioSourceSpatialMaxDistanceIcall),
                LT_MANAGED_INTERNAL_CALL(GetAudioSourceSpatialRolloffExponentIcall),
                LT_MANAGED_INTERNAL_CALL(SetAudioSourceSpatialRolloffExponentIcall),
                LT_MANAGED_INTERNAL_CALL(GetAudioSourceStereoPanStrengthIcall),
                LT_MANAGED_INTERNAL_CALL(SetAudioSourceStereoPanStrengthIcall),
                LT_MANAGED_INTERNAL_CALL(GetAudioSourceSpatialRolloffModeIcall),
                LT_MANAGED_INTERNAL_CALL(SetAudioSourceSpatialRolloffModeIcall),
                LT_MANAGED_INTERNAL_CALL(GetAudioSourceDopplerFactorIcall),
                LT_MANAGED_INTERNAL_CALL(SetAudioSourceDopplerFactorIcall),
                LT_MANAGED_INTERNAL_CALL(GetAudioSourceEnableDirectionalAttenuationIcall),
                LT_MANAGED_INTERNAL_CALL(SetAudioSourceEnableDirectionalAttenuationIcall),
                LT_MANAGED_INTERNAL_CALL(GetAudioSourceDirectionalInnerAngleDegreesIcall),
                LT_MANAGED_INTERNAL_CALL(SetAudioSourceDirectionalInnerAngleDegreesIcall),
                LT_MANAGED_INTERNAL_CALL(GetAudioSourceDirectionalOuterAngleDegreesIcall),
                LT_MANAGED_INTERNAL_CALL(SetAudioSourceDirectionalOuterAngleDegreesIcall),
                LT_MANAGED_INTERNAL_CALL(GetAudioSourceDirectionalOuterVolumeIcall),
                LT_MANAGED_INTERNAL_CALL(SetAudioSourceDirectionalOuterVolumeIcall),
                LT_MANAGED_INTERNAL_CALL(GetAudioSourceAttenuationCurveKeyIcall),
                LT_MANAGED_INTERNAL_CALL(SetAudioSourceAttenuationCurveKeyIcall),
                LT_MANAGED_INTERNAL_CALL(GetAudioSourceIsPlayingIcall),
                LT_MANAGED_INTERNAL_CALL(RequestAudioSourcePlayIcall),
                LT_MANAGED_INTERNAL_CALL(StopAudioSourceIcall),
                LT_MANAGED_INTERNAL_CALL(HasAnimatorComponentIcall),
                LT_MANAGED_INTERNAL_CALL(GetAnimatorControllerKeyIcall),
                LT_MANAGED_INTERNAL_CALL(SetAnimatorControllerKeyIcall),
                LT_MANAGED_INTERNAL_CALL(GetAnimatorDefaultClipKeyIcall),
                LT_MANAGED_INTERNAL_CALL(SetAnimatorDefaultClipKeyIcall),
                LT_MANAGED_INTERNAL_CALL(GetAnimatorPlaybackSpeedIcall),
                LT_MANAGED_INTERNAL_CALL(SetAnimatorPlaybackSpeedIcall),
                LT_MANAGED_INTERNAL_CALL(GetAnimatorEnabledIcall),
                LT_MANAGED_INTERNAL_CALL(SetAnimatorEnabledIcall),
                LT_MANAGED_INTERNAL_CALL(GetAnimatorApplyToSpriteIcall),
                LT_MANAGED_INTERNAL_CALL(SetAnimatorApplyToSpriteIcall),
                LT_MANAGED_INTERNAL_CALL(GetAnimatorApplyToTransformIcall),
                LT_MANAGED_INTERNAL_CALL(SetAnimatorApplyToTransformIcall),
                LT_MANAGED_INTERNAL_CALL(GetAnimatorAutoPlayIcall),
                LT_MANAGED_INTERNAL_CALL(SetAnimatorAutoPlayIcall),
                LT_MANAGED_INTERNAL_CALL(PlayAnimatorStateIcall),
                LT_MANAGED_INTERNAL_CALL(PlayAnimatorClipIcall),
                LT_MANAGED_INTERNAL_CALL(SetAnimatorBoolParameterIcall),
                LT_MANAGED_INTERNAL_CALL(GetAnimatorBoolParameterIcall),
                LT_MANAGED_INTERNAL_CALL(SetAnimatorFloatParameterIcall),
                LT_MANAGED_INTERNAL_CALL(GetAnimatorFloatParameterIcall),
                LT_MANAGED_INTERNAL_CALL(SetAnimatorIntegerParameterIcall),
                LT_MANAGED_INTERNAL_CALL(GetAnimatorIntegerParameterIcall),
                LT_MANAGED_INTERNAL_CALL(SetAnimatorTriggerParameterIcall),
                LT_MANAGED_INTERNAL_CALL(ResetAnimatorTriggerParameterIcall),
                LT_MANAGED_INTERNAL_CALL(GetAnimatorCurrentStateNameIcall),
                LT_MANAGED_INTERNAL_CALL(GetAnimatorCurrentClipKeyIcall),
                LT_MANAGED_INTERNAL_CALL(GetAnimatorStateTimeSecondsIcall),
                LT_MANAGED_INTERNAL_CALL(GetAnimatorCurrentStateDurationSecondsIcall),
                LT_MANAGED_INTERNAL_CALL(HasAnimationEventReceiverComponentIcall),
                LT_MANAGED_INTERNAL_CALL(GetAnimationEventReceiverEnabledIcall),
                LT_MANAGED_INTERNAL_CALL(SetAnimationEventReceiverEnabledIcall),
                LT_MANAGED_INTERNAL_CALL(GetAnimationEventReceiverDispatchedEventCountIcall),
                LT_MANAGED_INTERNAL_CALL(GetAnimationEventReceiverEventNameIcall),
                LT_MANAGED_INTERNAL_CALL(GetAnimationEventReceiverEventStringPayloadIcall),
                LT_MANAGED_INTERNAL_CALL(GetAnimationEventReceiverEventFloatPayloadIcall),
                LT_MANAGED_INTERNAL_CALL(GetAnimationEventReceiverEventIntegerPayloadIcall),
                LT_MANAGED_INTERNAL_CALL(GetAnimationEventReceiverEventBooleanPayloadIcall),
                LT_MANAGED_INTERNAL_CALL(GetAnimationEventReceiverEventTimeSecondsIcall),
                LT_MANAGED_INTERNAL_CALL(GetAnimationEventReceiverEventNormalizedTimeIcall),
                LT_MANAGED_INTERNAL_CALL(HasParticleEmitterComponentIcall),
                LT_MANAGED_INTERNAL_CALL(GetParticleEmitterSpawnRateIcall),
                LT_MANAGED_INTERNAL_CALL(SetParticleEmitterSpawnRateIcall),
                LT_MANAGED_INTERNAL_CALL(GetParticleEmitterLifetimeMinIcall),
                LT_MANAGED_INTERNAL_CALL(SetParticleEmitterLifetimeMinIcall),
                LT_MANAGED_INTERNAL_CALL(GetParticleEmitterLifetimeMaxIcall),
                LT_MANAGED_INTERNAL_CALL(SetParticleEmitterLifetimeMaxIcall),
                LT_MANAGED_INTERNAL_CALL(GetParticleEmitterLoopingIcall),
                LT_MANAGED_INTERNAL_CALL(SetParticleEmitterLoopingIcall),
                LT_MANAGED_INTERNAL_CALL(GetParticleEmitterDurationIcall),
                LT_MANAGED_INTERNAL_CALL(SetParticleEmitterDurationIcall),
                LT_MANAGED_INTERNAL_CALL(GetParticleEmitterPlayOnStartIcall),
                LT_MANAGED_INTERNAL_CALL(SetParticleEmitterPlayOnStartIcall),
                LT_MANAGED_INTERNAL_CALL(GetParticleEmitterBurstEnabledIcall),
                LT_MANAGED_INTERNAL_CALL(SetParticleEmitterBurstEnabledIcall),
                LT_MANAGED_INTERNAL_CALL(GetParticleEmitterBurstCountIcall),
                LT_MANAGED_INTERNAL_CALL(SetParticleEmitterBurstCountIcall),
                LT_MANAGED_INTERNAL_CALL(GetParticleEmitterSpawnOffsetMinIcall),
                LT_MANAGED_INTERNAL_CALL(SetParticleEmitterSpawnOffsetMinIcall),
                LT_MANAGED_INTERNAL_CALL(GetParticleEmitterSpawnOffsetMaxIcall),
                LT_MANAGED_INTERNAL_CALL(SetParticleEmitterSpawnOffsetMaxIcall),
                LT_MANAGED_INTERNAL_CALL(GetParticleEmitterUseRadialSpawnIcall),
                LT_MANAGED_INTERNAL_CALL(SetParticleEmitterUseRadialSpawnIcall),
                LT_MANAGED_INTERNAL_CALL(GetParticleEmitterSpawnRadiusMinIcall),
                LT_MANAGED_INTERNAL_CALL(SetParticleEmitterSpawnRadiusMinIcall),
                LT_MANAGED_INTERNAL_CALL(GetParticleEmitterSpawnRadiusMaxIcall),
                LT_MANAGED_INTERNAL_CALL(SetParticleEmitterSpawnRadiusMaxIcall),
                LT_MANAGED_INTERNAL_CALL(GetParticleEmitterSpeedMinIcall),
                LT_MANAGED_INTERNAL_CALL(SetParticleEmitterSpeedMinIcall),
                LT_MANAGED_INTERNAL_CALL(GetParticleEmitterSpeedMaxIcall),
                LT_MANAGED_INTERNAL_CALL(SetParticleEmitterSpeedMaxIcall),
                LT_MANAGED_INTERNAL_CALL(GetParticleEmitterAngleMinIcall),
                LT_MANAGED_INTERNAL_CALL(SetParticleEmitterAngleMinIcall),
                LT_MANAGED_INTERNAL_CALL(GetParticleEmitterAngleMaxIcall),
                LT_MANAGED_INTERNAL_CALL(SetParticleEmitterAngleMaxIcall),
                LT_MANAGED_INTERNAL_CALL(GetParticleEmitterRadialVelocityIcall),
                LT_MANAGED_INTERNAL_CALL(SetParticleEmitterRadialVelocityIcall),
                LT_MANAGED_INTERNAL_CALL(GetParticleEmitterGravityModifierIcall),
                LT_MANAGED_INTERNAL_CALL(SetParticleEmitterGravityModifierIcall),
                LT_MANAGED_INTERNAL_CALL(GetParticleEmitterStartSizeMinIcall),
                LT_MANAGED_INTERNAL_CALL(SetParticleEmitterStartSizeMinIcall),
                LT_MANAGED_INTERNAL_CALL(GetParticleEmitterStartSizeMaxIcall),
                LT_MANAGED_INTERNAL_CALL(SetParticleEmitterStartSizeMaxIcall),
                LT_MANAGED_INTERNAL_CALL(GetParticleEmitterEndSizeIcall),
                LT_MANAGED_INTERNAL_CALL(SetParticleEmitterEndSizeIcall),
                LT_MANAGED_INTERNAL_CALL(GetParticleEmitterStartColorIcall),
                LT_MANAGED_INTERNAL_CALL(SetParticleEmitterStartColorIcall),
                LT_MANAGED_INTERNAL_CALL(GetParticleEmitterEndColorIcall),
                LT_MANAGED_INTERNAL_CALL(SetParticleEmitterEndColorIcall),
                LT_MANAGED_INTERNAL_CALL(GetParticleEmitterStartRotationMinIcall),
                LT_MANAGED_INTERNAL_CALL(SetParticleEmitterStartRotationMinIcall),
                LT_MANAGED_INTERNAL_CALL(GetParticleEmitterStartRotationMaxIcall),
                LT_MANAGED_INTERNAL_CALL(SetParticleEmitterStartRotationMaxIcall),
                LT_MANAGED_INTERNAL_CALL(GetParticleEmitterRotationSpeedMinIcall),
                LT_MANAGED_INTERNAL_CALL(SetParticleEmitterRotationSpeedMinIcall),
                LT_MANAGED_INTERNAL_CALL(GetParticleEmitterRotationSpeedMaxIcall),
                LT_MANAGED_INTERNAL_CALL(SetParticleEmitterRotationSpeedMaxIcall),
                LT_MANAGED_INTERNAL_CALL(GetParticleEmitterTextureKeyIcall),
                LT_MANAGED_INTERNAL_CALL(SetParticleEmitterTextureKeyIcall),
                LT_MANAGED_INTERNAL_CALL(GetParticleEmitterMaxParticlesIcall),
                LT_MANAGED_INTERNAL_CALL(SetParticleEmitterMaxParticlesIcall),
                LT_MANAGED_INTERNAL_CALL(GetParticleEmitterIsPlayingIcall),
                LT_MANAGED_INTERNAL_CALL(GetParticleEmitterIsPausedIcall),
                LT_MANAGED_INTERNAL_CALL(GetParticleEmitterAliveParticleCountIcall),
                LT_MANAGED_INTERNAL_CALL(PlayParticleEmitterIcall),
                LT_MANAGED_INTERNAL_CALL(StopParticleEmitterIcall),
                LT_MANAGED_INTERNAL_CALL(PauseParticleEmitterIcall),
                LT_MANAGED_INTERNAL_CALL(ResumeParticleEmitterIcall),
                LT_MANAGED_INTERNAL_CALL(EmitParticleEmitterIcall)
            });
        }
    }
}
