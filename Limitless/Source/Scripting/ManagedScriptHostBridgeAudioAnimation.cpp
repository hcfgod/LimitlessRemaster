#include "Scripting/ManagedScriptHostInternal.h"

#include "Audio/AudioEngine.h"
#include "Scene/ParticleEmitterSystem.h"

#include <algorithm>

namespace Limitless::ManagedScriptHost
{
    using namespace Internal;

    namespace Internal
    {
        bool ManagedHasAudioListener2DComponentIcall(uint32_t entityHandle)
        {
            return TryGetManagedAudioListener2DComponent(entityHandle) != nullptr;
        }

        bool ManagedGetAudioListener2DEnabledIcall(uint32_t entityHandle)
        {
            const auto* listener = TryGetManagedAudioListener2DComponent(entityHandle);
            return listener ? listener->Enabled : true;
        }

        void ManagedSetAudioListener2DEnabledIcall(uint32_t entityHandle, bool value)
        {
            if (auto* listener = TryGetManagedAudioListener2DComponent(entityHandle))
                listener->Enabled = value;
        }

        bool ManagedGetAudioListener2DUsePrimaryCameraPositionIcall(uint32_t entityHandle)
        {
            const auto* listener = TryGetManagedAudioListener2DComponent(entityHandle);
            return listener ? listener->UsePrimaryCameraPosition : true;
        }

        void ManagedSetAudioListener2DUsePrimaryCameraPositionIcall(uint32_t entityHandle, bool value)
        {
            if (auto* listener = TryGetManagedAudioListener2DComponent(entityHandle))
                listener->UsePrimaryCameraPosition = value;
        }

        bool ManagedHasAudioListener3DComponentIcall(uint32_t entityHandle)
        {
            return TryGetManagedAudioListener3DComponent(entityHandle) != nullptr;
        }

        bool ManagedGetAudioListener3DEnabledIcall(uint32_t entityHandle)
        {
            const auto* listener = TryGetManagedAudioListener3DComponent(entityHandle);
            return listener ? listener->Enabled : true;
        }

        void ManagedSetAudioListener3DEnabledIcall(uint32_t entityHandle, bool value)
        {
            if (auto* listener = TryGetManagedAudioListener3DComponent(entityHandle))
                listener->Enabled = value;
        }

        bool ManagedGetAudioListener3DUsePrimaryCameraTransformIcall(uint32_t entityHandle)
        {
            const auto* listener = TryGetManagedAudioListener3DComponent(entityHandle);
            return listener ? listener->UsePrimaryCameraTransform : true;
        }

        void ManagedSetAudioListener3DUsePrimaryCameraTransformIcall(uint32_t entityHandle, bool value)
        {
            if (auto* listener = TryGetManagedAudioListener3DComponent(entityHandle))
                listener->UsePrimaryCameraTransform = value;
        }

        bool ManagedHasAudioSourceComponentIcall(uint32_t entityHandle)
        {
            return TryGetManagedAudioSourceComponent(entityHandle) != nullptr;
        }

        Coral::String ManagedGetAudioSourceClipKeyIcall(uint32_t entityHandle)
        {
            const auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle);
            return audioSource ? Coral::String::New(audioSource->AudioClipKey) : Coral::String::New("");
        }

        void ManagedSetAudioSourceClipKeyIcall(uint32_t entityHandle, Coral::String value)
        {
            if (auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle))
                audioSource->AudioClipKey = ToUtf8Borrowed(value);
        }

        float ManagedGetAudioSourceVolumeIcall(uint32_t entityHandle)
        {
            const auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle);
            return audioSource ? audioSource->Volume : 1.0f;
        }

        void ManagedSetAudioSourceVolumeIcall(uint32_t entityHandle, float value)
        {
            if (auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle))
                audioSource->Volume = std::max(0.0f, value);
        }

        float ManagedGetAudioSourcePitchIcall(uint32_t entityHandle)
        {
            const auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle);
            return audioSource ? audioSource->Pitch : 1.0f;
        }

        void ManagedSetAudioSourcePitchIcall(uint32_t entityHandle, float value)
        {
            if (auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle))
                audioSource->Pitch = std::max(0.01f, value);
        }

        bool ManagedGetAudioSourcePlayOnStartIcall(uint32_t entityHandle)
        {
            const auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle);
            return audioSource ? audioSource->PlayOnStart : true;
        }

        void ManagedSetAudioSourcePlayOnStartIcall(uint32_t entityHandle, bool value)
        {
            if (auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle))
                audioSource->PlayOnStart = value;
        }

        bool ManagedGetAudioSourceLoopIcall(uint32_t entityHandle)
        {
            const auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle);
            return audioSource ? audioSource->Loop : false;
        }

        void ManagedSetAudioSourceLoopIcall(uint32_t entityHandle, bool value)
        {
            if (auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle))
                audioSource->Loop = value;
        }

        bool ManagedGetAudioSourceMutedIcall(uint32_t entityHandle)
        {
            const auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle);
            return audioSource ? audioSource->Muted : false;
        }

        void ManagedSetAudioSourceMutedIcall(uint32_t entityHandle, bool value)
        {
            if (auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle))
                audioSource->Muted = value;
        }

        int ManagedGetAudioSourcePlaybackSpaceIcall(uint32_t entityHandle)
        {
            const auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle);
            return audioSource ? static_cast<int>(audioSource->Space) : static_cast<int>(AudioSourceComponent::PlaybackSpace::Global);
        }

        void ManagedSetAudioSourcePlaybackSpaceIcall(uint32_t entityHandle, int value)
        {
            if (auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle))
                audioSource->Space = static_cast<AudioSourceComponent::PlaybackSpace>(value);
        }

        Coral::String ManagedGetAudioSourceMixerGroupIcall(uint32_t entityHandle)
        {
            const auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle);
            return audioSource ? Coral::String::New(audioSource->MixerGroup) : Coral::String::New("SFX");
        }

        void ManagedSetAudioSourceMixerGroupIcall(uint32_t entityHandle, Coral::String value)
        {
            if (auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle))
            {
                audioSource->MixerGroup = ToUtf8Borrowed(value);
                if (audioSource->MixerGroup.empty())
                    audioSource->MixerGroup = "SFX";
            }
        }

        float ManagedGetAudioSourceSpatialMinDistanceIcall(uint32_t entityHandle)
        {
            const auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle);
            return audioSource ? audioSource->SpatialMinDistance : 1.0f;
        }

        void ManagedSetAudioSourceSpatialMinDistanceIcall(uint32_t entityHandle, float value)
        {
            if (auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle))
            {
                audioSource->SpatialMinDistance = std::max(0.001f, value);
                audioSource->SpatialMaxDistance = std::max(audioSource->SpatialMinDistance, audioSource->SpatialMaxDistance);
            }
        }

        float ManagedGetAudioSourceSpatialMaxDistanceIcall(uint32_t entityHandle)
        {
            const auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle);
            return audioSource ? audioSource->SpatialMaxDistance : 20.0f;
        }

        void ManagedSetAudioSourceSpatialMaxDistanceIcall(uint32_t entityHandle, float value)
        {
            if (auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle))
                audioSource->SpatialMaxDistance = std::max(audioSource->SpatialMinDistance, value);
        }

        float ManagedGetAudioSourceSpatialRolloffExponentIcall(uint32_t entityHandle)
        {
            const auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle);
            return audioSource ? audioSource->SpatialRolloffExponent : 1.0f;
        }

        void ManagedSetAudioSourceSpatialRolloffExponentIcall(uint32_t entityHandle, float value)
        {
            if (auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle))
                audioSource->SpatialRolloffExponent = std::max(0.01f, value);
        }

        float ManagedGetAudioSourceStereoPanStrengthIcall(uint32_t entityHandle)
        {
            const auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle);
            return audioSource ? audioSource->StereoPanStrength : 1.0f;
        }

        void ManagedSetAudioSourceStereoPanStrengthIcall(uint32_t entityHandle, float value)
        {
            if (auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle))
                audioSource->StereoPanStrength = std::clamp(value, 0.0f, 1.0f);
        }

        int ManagedGetAudioSourceSpatialRolloffModeIcall(uint32_t entityHandle)
        {
            const auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle);
            return audioSource ? static_cast<int>(audioSource->SpatialRolloffMode) : static_cast<int>(AudioSourceComponent::RolloffMode::Linear);
        }

        void ManagedSetAudioSourceSpatialRolloffModeIcall(uint32_t entityHandle, int value)
        {
            if (auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle))
                audioSource->SpatialRolloffMode = static_cast<AudioSourceComponent::RolloffMode>(value);
        }

        float ManagedGetAudioSourceDopplerFactorIcall(uint32_t entityHandle)
        {
            const auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle);
            return audioSource ? audioSource->DopplerFactor : 1.0f;
        }

        void ManagedSetAudioSourceDopplerFactorIcall(uint32_t entityHandle, float value)
        {
            if (auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle))
                audioSource->DopplerFactor = std::max(0.0f, value);
        }

        bool ManagedGetAudioSourceEnableDirectionalAttenuationIcall(uint32_t entityHandle)
        {
            const auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle);
            return audioSource ? audioSource->EnableDirectionalAttenuation : false;
        }

        void ManagedSetAudioSourceEnableDirectionalAttenuationIcall(uint32_t entityHandle, bool value)
        {
            if (auto* audioSource = TryGetManagedAudioSourceComponent(entityHandle))
                audioSource->EnableDirectionalAttenuation = value;
        }

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

        bool ManagedHasAnimationEventReceiverComponentIcall(uint32_t entityHandle)
        {
            return TryGetManagedAnimationEventReceiverComponent(entityHandle) != nullptr;
        }

        bool ManagedGetAnimationEventReceiverEnabledIcall(uint32_t entityHandle)
        {
            const auto* receiver = TryGetManagedAnimationEventReceiverComponent(entityHandle);
            return receiver ? receiver->Enabled : true;
        }

        void ManagedSetAnimationEventReceiverEnabledIcall(uint32_t entityHandle, bool value)
        {
            if (auto* receiver = TryGetManagedAnimationEventReceiverComponent(entityHandle))
                receiver->Enabled = value;
        }

        int ManagedGetAnimationEventReceiverDispatchedEventCountIcall(uint32_t entityHandle)
        {
            const auto* receiver = TryGetManagedAnimationEventReceiverComponent(entityHandle);
            return receiver ? static_cast<int>(receiver->RuntimeDispatchedEvents.size()) : 0;
        }

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

        bool ManagedHasParticleEmitterComponentIcall(uint32_t entityHandle)
        {
            return TryGetManagedParticleEmitterComponent(entityHandle) != nullptr;
        }

        float ManagedGetParticleEmitterSpawnRateIcall(uint32_t entityHandle)
        {
            const auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            return emitter ? emitter->SpawnRate : 10.0f;
        }

        void ManagedSetParticleEmitterSpawnRateIcall(uint32_t entityHandle, float value)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
                emitter->SpawnRate = std::max(0.0f, value);
        }

        float ManagedGetParticleEmitterLifetimeMinIcall(uint32_t entityHandle)
        {
            const auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            return emitter ? emitter->LifetimeMin : 1.0f;
        }

        void ManagedSetParticleEmitterLifetimeMinIcall(uint32_t entityHandle, float value)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
            {
                emitter->LifetimeMin = std::max(0.0f, value);
                emitter->LifetimeMax = std::max(emitter->LifetimeMin, emitter->LifetimeMax);
            }
        }

        float ManagedGetParticleEmitterLifetimeMaxIcall(uint32_t entityHandle)
        {
            const auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            return emitter ? emitter->LifetimeMax : 2.0f;
        }

        void ManagedSetParticleEmitterLifetimeMaxIcall(uint32_t entityHandle, float value)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
                emitter->LifetimeMax = std::max(emitter->LifetimeMin, value);
        }

        bool ManagedGetParticleEmitterLoopingIcall(uint32_t entityHandle)
        {
            const auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            return emitter ? emitter->Looping : true;
        }

        void ManagedSetParticleEmitterLoopingIcall(uint32_t entityHandle, bool value)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
                emitter->Looping = value;
        }

        float ManagedGetParticleEmitterDurationIcall(uint32_t entityHandle)
        {
            const auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            return emitter ? emitter->Duration : 5.0f;
        }

        void ManagedSetParticleEmitterDurationIcall(uint32_t entityHandle, float value)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
                emitter->Duration = std::max(0.0f, value);
        }

        bool ManagedGetParticleEmitterPlayOnStartIcall(uint32_t entityHandle)
        {
            const auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            return emitter ? emitter->PlayOnStart : true;
        }

        void ManagedSetParticleEmitterPlayOnStartIcall(uint32_t entityHandle, bool value)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
                emitter->PlayOnStart = value;
        }

        bool ManagedGetParticleEmitterBurstEnabledIcall(uint32_t entityHandle)
        {
            const auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            return emitter ? emitter->BurstEnabled : false;
        }

        void ManagedSetParticleEmitterBurstEnabledIcall(uint32_t entityHandle, bool value)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
                emitter->BurstEnabled = value;
        }

        int ManagedGetParticleEmitterBurstCountIcall(uint32_t entityHandle)
        {
            const auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            return emitter ? static_cast<int>(emitter->BurstCount) : 10;
        }

        void ManagedSetParticleEmitterBurstCountIcall(uint32_t entityHandle, int value)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
                emitter->BurstCount = static_cast<uint32_t>(std::clamp(value, 0, static_cast<int>(ParticleEmitterComponent::kMaxParticlesCap)));
        }

        ManagedVector2 ManagedGetParticleEmitterSpawnOffsetMinIcall(uint32_t entityHandle)
        {
            const auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            return emitter ? ToManagedVector2(emitter->SpawnOffsetMin) : ManagedVector2{};
        }

        void ManagedSetParticleEmitterSpawnOffsetMinIcall(uint32_t entityHandle, ManagedVector2 value)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
                emitter->SpawnOffsetMin = ToGlmVector2(value);
        }

        ManagedVector2 ManagedGetParticleEmitterSpawnOffsetMaxIcall(uint32_t entityHandle)
        {
            const auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            return emitter ? ToManagedVector2(emitter->SpawnOffsetMax) : ManagedVector2{};
        }

        void ManagedSetParticleEmitterSpawnOffsetMaxIcall(uint32_t entityHandle, ManagedVector2 value)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
                emitter->SpawnOffsetMax = ToGlmVector2(value);
        }

        bool ManagedGetParticleEmitterUseRadialSpawnIcall(uint32_t entityHandle)
        {
            const auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle);
            return emitter ? emitter->UseRadialSpawn : false;
        }

        void ManagedSetParticleEmitterUseRadialSpawnIcall(uint32_t entityHandle, bool value)
        {
            if (auto* emitter = TryGetManagedParticleEmitterComponent(entityHandle))
                emitter->UseRadialSpawn = value;
        }

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
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "HasAudioListener2DComponentIcall", reinterpret_cast<void*>(&ManagedHasAudioListener2DComponentIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAudioListener2DEnabledIcall", reinterpret_cast<void*>(&ManagedGetAudioListener2DEnabledIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAudioListener2DEnabledIcall", reinterpret_cast<void*>(&ManagedSetAudioListener2DEnabledIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAudioListener2DUsePrimaryCameraPositionIcall", reinterpret_cast<void*>(&ManagedGetAudioListener2DUsePrimaryCameraPositionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAudioListener2DUsePrimaryCameraPositionIcall", reinterpret_cast<void*>(&ManagedSetAudioListener2DUsePrimaryCameraPositionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "HasAudioListener3DComponentIcall", reinterpret_cast<void*>(&ManagedHasAudioListener3DComponentIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAudioListener3DEnabledIcall", reinterpret_cast<void*>(&ManagedGetAudioListener3DEnabledIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAudioListener3DEnabledIcall", reinterpret_cast<void*>(&ManagedSetAudioListener3DEnabledIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAudioListener3DUsePrimaryCameraTransformIcall", reinterpret_cast<void*>(&ManagedGetAudioListener3DUsePrimaryCameraTransformIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAudioListener3DUsePrimaryCameraTransformIcall", reinterpret_cast<void*>(&ManagedSetAudioListener3DUsePrimaryCameraTransformIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "HasAudioSourceComponentIcall", reinterpret_cast<void*>(&ManagedHasAudioSourceComponentIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAudioSourceClipKeyIcall", reinterpret_cast<void*>(&ManagedGetAudioSourceClipKeyIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAudioSourceClipKeyIcall", reinterpret_cast<void*>(&ManagedSetAudioSourceClipKeyIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAudioSourceVolumeIcall", reinterpret_cast<void*>(&ManagedGetAudioSourceVolumeIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAudioSourceVolumeIcall", reinterpret_cast<void*>(&ManagedSetAudioSourceVolumeIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAudioSourcePitchIcall", reinterpret_cast<void*>(&ManagedGetAudioSourcePitchIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAudioSourcePitchIcall", reinterpret_cast<void*>(&ManagedSetAudioSourcePitchIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAudioSourcePlayOnStartIcall", reinterpret_cast<void*>(&ManagedGetAudioSourcePlayOnStartIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAudioSourcePlayOnStartIcall", reinterpret_cast<void*>(&ManagedSetAudioSourcePlayOnStartIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAudioSourceLoopIcall", reinterpret_cast<void*>(&ManagedGetAudioSourceLoopIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAudioSourceLoopIcall", reinterpret_cast<void*>(&ManagedSetAudioSourceLoopIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAudioSourceMutedIcall", reinterpret_cast<void*>(&ManagedGetAudioSourceMutedIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAudioSourceMutedIcall", reinterpret_cast<void*>(&ManagedSetAudioSourceMutedIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAudioSourcePlaybackSpaceIcall", reinterpret_cast<void*>(&ManagedGetAudioSourcePlaybackSpaceIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAudioSourcePlaybackSpaceIcall", reinterpret_cast<void*>(&ManagedSetAudioSourcePlaybackSpaceIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAudioSourceMixerGroupIcall", reinterpret_cast<void*>(&ManagedGetAudioSourceMixerGroupIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAudioSourceMixerGroupIcall", reinterpret_cast<void*>(&ManagedSetAudioSourceMixerGroupIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAudioSourceSpatialMinDistanceIcall", reinterpret_cast<void*>(&ManagedGetAudioSourceSpatialMinDistanceIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAudioSourceSpatialMinDistanceIcall", reinterpret_cast<void*>(&ManagedSetAudioSourceSpatialMinDistanceIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAudioSourceSpatialMaxDistanceIcall", reinterpret_cast<void*>(&ManagedGetAudioSourceSpatialMaxDistanceIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAudioSourceSpatialMaxDistanceIcall", reinterpret_cast<void*>(&ManagedSetAudioSourceSpatialMaxDistanceIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAudioSourceSpatialRolloffExponentIcall", reinterpret_cast<void*>(&ManagedGetAudioSourceSpatialRolloffExponentIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAudioSourceSpatialRolloffExponentIcall", reinterpret_cast<void*>(&ManagedSetAudioSourceSpatialRolloffExponentIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAudioSourceStereoPanStrengthIcall", reinterpret_cast<void*>(&ManagedGetAudioSourceStereoPanStrengthIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAudioSourceStereoPanStrengthIcall", reinterpret_cast<void*>(&ManagedSetAudioSourceStereoPanStrengthIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAudioSourceSpatialRolloffModeIcall", reinterpret_cast<void*>(&ManagedGetAudioSourceSpatialRolloffModeIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAudioSourceSpatialRolloffModeIcall", reinterpret_cast<void*>(&ManagedSetAudioSourceSpatialRolloffModeIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAudioSourceDopplerFactorIcall", reinterpret_cast<void*>(&ManagedGetAudioSourceDopplerFactorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAudioSourceDopplerFactorIcall", reinterpret_cast<void*>(&ManagedSetAudioSourceDopplerFactorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAudioSourceEnableDirectionalAttenuationIcall", reinterpret_cast<void*>(&ManagedGetAudioSourceEnableDirectionalAttenuationIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAudioSourceEnableDirectionalAttenuationIcall", reinterpret_cast<void*>(&ManagedSetAudioSourceEnableDirectionalAttenuationIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAudioSourceDirectionalInnerAngleDegreesIcall", reinterpret_cast<void*>(&ManagedGetAudioSourceDirectionalInnerAngleDegreesIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAudioSourceDirectionalInnerAngleDegreesIcall", reinterpret_cast<void*>(&ManagedSetAudioSourceDirectionalInnerAngleDegreesIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAudioSourceDirectionalOuterAngleDegreesIcall", reinterpret_cast<void*>(&ManagedGetAudioSourceDirectionalOuterAngleDegreesIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAudioSourceDirectionalOuterAngleDegreesIcall", reinterpret_cast<void*>(&ManagedSetAudioSourceDirectionalOuterAngleDegreesIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAudioSourceDirectionalOuterVolumeIcall", reinterpret_cast<void*>(&ManagedGetAudioSourceDirectionalOuterVolumeIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAudioSourceDirectionalOuterVolumeIcall", reinterpret_cast<void*>(&ManagedSetAudioSourceDirectionalOuterVolumeIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAudioSourceAttenuationCurveKeyIcall", reinterpret_cast<void*>(&ManagedGetAudioSourceAttenuationCurveKeyIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAudioSourceAttenuationCurveKeyIcall", reinterpret_cast<void*>(&ManagedSetAudioSourceAttenuationCurveKeyIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAudioSourceIsPlayingIcall", reinterpret_cast<void*>(&ManagedGetAudioSourceIsPlayingIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "RequestAudioSourcePlayIcall", reinterpret_cast<void*>(&ManagedRequestAudioSourcePlayIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "StopAudioSourceIcall", reinterpret_cast<void*>(&ManagedStopAudioSourceIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "HasAnimatorComponentIcall", reinterpret_cast<void*>(&ManagedHasAnimatorComponentIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAnimatorControllerKeyIcall", reinterpret_cast<void*>(&ManagedGetAnimatorControllerKeyIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAnimatorControllerKeyIcall", reinterpret_cast<void*>(&ManagedSetAnimatorControllerKeyIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAnimatorDefaultClipKeyIcall", reinterpret_cast<void*>(&ManagedGetAnimatorDefaultClipKeyIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAnimatorDefaultClipKeyIcall", reinterpret_cast<void*>(&ManagedSetAnimatorDefaultClipKeyIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAnimatorPlaybackSpeedIcall", reinterpret_cast<void*>(&ManagedGetAnimatorPlaybackSpeedIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAnimatorPlaybackSpeedIcall", reinterpret_cast<void*>(&ManagedSetAnimatorPlaybackSpeedIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAnimatorEnabledIcall", reinterpret_cast<void*>(&ManagedGetAnimatorEnabledIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAnimatorEnabledIcall", reinterpret_cast<void*>(&ManagedSetAnimatorEnabledIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAnimatorApplyToSpriteIcall", reinterpret_cast<void*>(&ManagedGetAnimatorApplyToSpriteIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAnimatorApplyToSpriteIcall", reinterpret_cast<void*>(&ManagedSetAnimatorApplyToSpriteIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAnimatorApplyToTransformIcall", reinterpret_cast<void*>(&ManagedGetAnimatorApplyToTransformIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAnimatorApplyToTransformIcall", reinterpret_cast<void*>(&ManagedSetAnimatorApplyToTransformIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAnimatorAutoPlayIcall", reinterpret_cast<void*>(&ManagedGetAnimatorAutoPlayIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAnimatorAutoPlayIcall", reinterpret_cast<void*>(&ManagedSetAnimatorAutoPlayIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "PlayAnimatorStateIcall", reinterpret_cast<void*>(&ManagedPlayAnimatorStateIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "PlayAnimatorClipIcall", reinterpret_cast<void*>(&ManagedPlayAnimatorClipIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAnimatorBoolParameterIcall", reinterpret_cast<void*>(&ManagedSetAnimatorBoolParameterIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAnimatorBoolParameterIcall", reinterpret_cast<void*>(&ManagedGetAnimatorBoolParameterIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAnimatorFloatParameterIcall", reinterpret_cast<void*>(&ManagedSetAnimatorFloatParameterIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAnimatorFloatParameterIcall", reinterpret_cast<void*>(&ManagedGetAnimatorFloatParameterIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAnimatorIntegerParameterIcall", reinterpret_cast<void*>(&ManagedSetAnimatorIntegerParameterIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAnimatorIntegerParameterIcall", reinterpret_cast<void*>(&ManagedGetAnimatorIntegerParameterIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAnimatorTriggerParameterIcall", reinterpret_cast<void*>(&ManagedSetAnimatorTriggerParameterIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "ResetAnimatorTriggerParameterIcall", reinterpret_cast<void*>(&ManagedResetAnimatorTriggerParameterIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAnimatorCurrentStateNameIcall", reinterpret_cast<void*>(&ManagedGetAnimatorCurrentStateNameIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAnimatorCurrentClipKeyIcall", reinterpret_cast<void*>(&ManagedGetAnimatorCurrentClipKeyIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAnimatorStateTimeSecondsIcall", reinterpret_cast<void*>(&ManagedGetAnimatorStateTimeSecondsIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAnimatorCurrentStateDurationSecondsIcall", reinterpret_cast<void*>(&ManagedGetAnimatorCurrentStateDurationSecondsIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "HasAnimationEventReceiverComponentIcall", reinterpret_cast<void*>(&ManagedHasAnimationEventReceiverComponentIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAnimationEventReceiverEnabledIcall", reinterpret_cast<void*>(&ManagedGetAnimationEventReceiverEnabledIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetAnimationEventReceiverEnabledIcall", reinterpret_cast<void*>(&ManagedSetAnimationEventReceiverEnabledIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAnimationEventReceiverDispatchedEventCountIcall", reinterpret_cast<void*>(&ManagedGetAnimationEventReceiverDispatchedEventCountIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAnimationEventReceiverEventNameIcall", reinterpret_cast<void*>(&ManagedGetAnimationEventReceiverEventNameIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAnimationEventReceiverEventStringPayloadIcall", reinterpret_cast<void*>(&ManagedGetAnimationEventReceiverEventStringPayloadIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAnimationEventReceiverEventFloatPayloadIcall", reinterpret_cast<void*>(&ManagedGetAnimationEventReceiverEventFloatPayloadIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAnimationEventReceiverEventIntegerPayloadIcall", reinterpret_cast<void*>(&ManagedGetAnimationEventReceiverEventIntegerPayloadIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAnimationEventReceiverEventBooleanPayloadIcall", reinterpret_cast<void*>(&ManagedGetAnimationEventReceiverEventBooleanPayloadIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAnimationEventReceiverEventTimeSecondsIcall", reinterpret_cast<void*>(&ManagedGetAnimationEventReceiverEventTimeSecondsIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetAnimationEventReceiverEventNormalizedTimeIcall", reinterpret_cast<void*>(&ManagedGetAnimationEventReceiverEventNormalizedTimeIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "HasParticleEmitterComponentIcall", reinterpret_cast<void*>(&ManagedHasParticleEmitterComponentIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParticleEmitterSpawnRateIcall", reinterpret_cast<void*>(&ManagedGetParticleEmitterSpawnRateIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetParticleEmitterSpawnRateIcall", reinterpret_cast<void*>(&ManagedSetParticleEmitterSpawnRateIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParticleEmitterLifetimeMinIcall", reinterpret_cast<void*>(&ManagedGetParticleEmitterLifetimeMinIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetParticleEmitterLifetimeMinIcall", reinterpret_cast<void*>(&ManagedSetParticleEmitterLifetimeMinIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParticleEmitterLifetimeMaxIcall", reinterpret_cast<void*>(&ManagedGetParticleEmitterLifetimeMaxIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetParticleEmitterLifetimeMaxIcall", reinterpret_cast<void*>(&ManagedSetParticleEmitterLifetimeMaxIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParticleEmitterLoopingIcall", reinterpret_cast<void*>(&ManagedGetParticleEmitterLoopingIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetParticleEmitterLoopingIcall", reinterpret_cast<void*>(&ManagedSetParticleEmitterLoopingIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParticleEmitterDurationIcall", reinterpret_cast<void*>(&ManagedGetParticleEmitterDurationIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetParticleEmitterDurationIcall", reinterpret_cast<void*>(&ManagedSetParticleEmitterDurationIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParticleEmitterPlayOnStartIcall", reinterpret_cast<void*>(&ManagedGetParticleEmitterPlayOnStartIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetParticleEmitterPlayOnStartIcall", reinterpret_cast<void*>(&ManagedSetParticleEmitterPlayOnStartIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParticleEmitterBurstEnabledIcall", reinterpret_cast<void*>(&ManagedGetParticleEmitterBurstEnabledIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetParticleEmitterBurstEnabledIcall", reinterpret_cast<void*>(&ManagedSetParticleEmitterBurstEnabledIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParticleEmitterBurstCountIcall", reinterpret_cast<void*>(&ManagedGetParticleEmitterBurstCountIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetParticleEmitterBurstCountIcall", reinterpret_cast<void*>(&ManagedSetParticleEmitterBurstCountIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParticleEmitterSpawnOffsetMinIcall", reinterpret_cast<void*>(&ManagedGetParticleEmitterSpawnOffsetMinIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetParticleEmitterSpawnOffsetMinIcall", reinterpret_cast<void*>(&ManagedSetParticleEmitterSpawnOffsetMinIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParticleEmitterSpawnOffsetMaxIcall", reinterpret_cast<void*>(&ManagedGetParticleEmitterSpawnOffsetMaxIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetParticleEmitterSpawnOffsetMaxIcall", reinterpret_cast<void*>(&ManagedSetParticleEmitterSpawnOffsetMaxIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParticleEmitterUseRadialSpawnIcall", reinterpret_cast<void*>(&ManagedGetParticleEmitterUseRadialSpawnIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetParticleEmitterUseRadialSpawnIcall", reinterpret_cast<void*>(&ManagedSetParticleEmitterUseRadialSpawnIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParticleEmitterSpawnRadiusMinIcall", reinterpret_cast<void*>(&ManagedGetParticleEmitterSpawnRadiusMinIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetParticleEmitterSpawnRadiusMinIcall", reinterpret_cast<void*>(&ManagedSetParticleEmitterSpawnRadiusMinIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParticleEmitterSpawnRadiusMaxIcall", reinterpret_cast<void*>(&ManagedGetParticleEmitterSpawnRadiusMaxIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetParticleEmitterSpawnRadiusMaxIcall", reinterpret_cast<void*>(&ManagedSetParticleEmitterSpawnRadiusMaxIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParticleEmitterSpeedMinIcall", reinterpret_cast<void*>(&ManagedGetParticleEmitterSpeedMinIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetParticleEmitterSpeedMinIcall", reinterpret_cast<void*>(&ManagedSetParticleEmitterSpeedMinIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParticleEmitterSpeedMaxIcall", reinterpret_cast<void*>(&ManagedGetParticleEmitterSpeedMaxIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetParticleEmitterSpeedMaxIcall", reinterpret_cast<void*>(&ManagedSetParticleEmitterSpeedMaxIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParticleEmitterAngleMinIcall", reinterpret_cast<void*>(&ManagedGetParticleEmitterAngleMinIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetParticleEmitterAngleMinIcall", reinterpret_cast<void*>(&ManagedSetParticleEmitterAngleMinIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParticleEmitterAngleMaxIcall", reinterpret_cast<void*>(&ManagedGetParticleEmitterAngleMaxIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetParticleEmitterAngleMaxIcall", reinterpret_cast<void*>(&ManagedSetParticleEmitterAngleMaxIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParticleEmitterRadialVelocityIcall", reinterpret_cast<void*>(&ManagedGetParticleEmitterRadialVelocityIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetParticleEmitterRadialVelocityIcall", reinterpret_cast<void*>(&ManagedSetParticleEmitterRadialVelocityIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParticleEmitterGravityModifierIcall", reinterpret_cast<void*>(&ManagedGetParticleEmitterGravityModifierIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetParticleEmitterGravityModifierIcall", reinterpret_cast<void*>(&ManagedSetParticleEmitterGravityModifierIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParticleEmitterStartSizeMinIcall", reinterpret_cast<void*>(&ManagedGetParticleEmitterStartSizeMinIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetParticleEmitterStartSizeMinIcall", reinterpret_cast<void*>(&ManagedSetParticleEmitterStartSizeMinIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParticleEmitterStartSizeMaxIcall", reinterpret_cast<void*>(&ManagedGetParticleEmitterStartSizeMaxIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetParticleEmitterStartSizeMaxIcall", reinterpret_cast<void*>(&ManagedSetParticleEmitterStartSizeMaxIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParticleEmitterEndSizeIcall", reinterpret_cast<void*>(&ManagedGetParticleEmitterEndSizeIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetParticleEmitterEndSizeIcall", reinterpret_cast<void*>(&ManagedSetParticleEmitterEndSizeIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParticleEmitterStartColorIcall", reinterpret_cast<void*>(&ManagedGetParticleEmitterStartColorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetParticleEmitterStartColorIcall", reinterpret_cast<void*>(&ManagedSetParticleEmitterStartColorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParticleEmitterEndColorIcall", reinterpret_cast<void*>(&ManagedGetParticleEmitterEndColorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetParticleEmitterEndColorIcall", reinterpret_cast<void*>(&ManagedSetParticleEmitterEndColorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParticleEmitterStartRotationMinIcall", reinterpret_cast<void*>(&ManagedGetParticleEmitterStartRotationMinIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetParticleEmitterStartRotationMinIcall", reinterpret_cast<void*>(&ManagedSetParticleEmitterStartRotationMinIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParticleEmitterStartRotationMaxIcall", reinterpret_cast<void*>(&ManagedGetParticleEmitterStartRotationMaxIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetParticleEmitterStartRotationMaxIcall", reinterpret_cast<void*>(&ManagedSetParticleEmitterStartRotationMaxIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParticleEmitterRotationSpeedMinIcall", reinterpret_cast<void*>(&ManagedGetParticleEmitterRotationSpeedMinIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetParticleEmitterRotationSpeedMinIcall", reinterpret_cast<void*>(&ManagedSetParticleEmitterRotationSpeedMinIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParticleEmitterRotationSpeedMaxIcall", reinterpret_cast<void*>(&ManagedGetParticleEmitterRotationSpeedMaxIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetParticleEmitterRotationSpeedMaxIcall", reinterpret_cast<void*>(&ManagedSetParticleEmitterRotationSpeedMaxIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParticleEmitterTextureKeyIcall", reinterpret_cast<void*>(&ManagedGetParticleEmitterTextureKeyIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetParticleEmitterTextureKeyIcall", reinterpret_cast<void*>(&ManagedSetParticleEmitterTextureKeyIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParticleEmitterMaxParticlesIcall", reinterpret_cast<void*>(&ManagedGetParticleEmitterMaxParticlesIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetParticleEmitterMaxParticlesIcall", reinterpret_cast<void*>(&ManagedSetParticleEmitterMaxParticlesIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParticleEmitterIsPlayingIcall", reinterpret_cast<void*>(&ManagedGetParticleEmitterIsPlayingIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParticleEmitterIsPausedIcall", reinterpret_cast<void*>(&ManagedGetParticleEmitterIsPausedIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParticleEmitterAliveParticleCountIcall", reinterpret_cast<void*>(&ManagedGetParticleEmitterAliveParticleCountIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "PlayParticleEmitterIcall", reinterpret_cast<void*>(&ManagedPlayParticleEmitterIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "StopParticleEmitterIcall", reinterpret_cast<void*>(&ManagedStopParticleEmitterIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "PauseParticleEmitterIcall", reinterpret_cast<void*>(&ManagedPauseParticleEmitterIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "ResumeParticleEmitterIcall", reinterpret_cast<void*>(&ManagedResumeParticleEmitterIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "EmitParticleEmitterIcall", reinterpret_cast<void*>(&ManagedEmitParticleEmitterIcall));
        }
    }
}
