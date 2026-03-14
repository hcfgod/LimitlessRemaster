#include "EditorInspectorPanelEntityComponentsShared.h"

#include "EditorAssetNaming.h"
#include "Assets/AudioClipAsset.h"
#include "Audio/SceneAudioSystem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Limitless::EditorInspectorPanel::Internal
{
    void DrawAudioComponentSections(StandardEntityInspectorContext& context)
    {
        Scene* scene = context.SceneContext;
        auto& registry = context.Registry;
        const entt::entity selectedEntity = context.SelectedEntity;
        const char* audioPayloadId = context.AudioPayloadId;
        PendingEntityComponentRemovals& pendingRemovals = context.PendingRemovals;
        EditorUndoService* undoService = context.UndoService;
        const std::string_view onlySectionKey = context.OnlySectionKey;
        const std::vector<std::string>* orderedSectionKeys = context.OrderedSectionKeys;

        if (ShouldDrawInspectorSection(onlySectionKey, "AudioListener2D") && (registry.try_get<AudioListener2DComponent>(selectedEntity) != nullptr))
        {
            auto* audioListener = registry.try_get<AudioListener2DComponent>(selectedEntity);
            const bool listenerOpen = BeginInspectorSectionHeader("Audio Listener 2D", "AudioListener2DComponentOptions", "...##AudioListener2DComponentOptionsButton");
            if (orderedSectionKeys)
                (void)HandleSectionDragDrop("AudioListener2D", *orderedSectionKeys, "Audio Listener 2D");

            if (ImGui::BeginPopup("AudioListener2DComponentOptions"))
            {
                if (ImGui::MenuItem("Remove Component"))
                    pendingRemovals.RemoveAudioListener2DComponent = true;
                ImGui::EndPopup();
            }

            if (listenerOpen)
            {
                ImGui::TextUnformatted("Enabled");
                ImGui::Checkbox("##AudioListenerEnabled", &audioListener->Enabled);
                TrackInteractiveMemberMutation<AudioListener2DComponent>(
                    undoService, "Edit Audio Listener 2D Enabled", selectedEntity, &AudioListener2DComponent::Enabled, audioListener->Enabled);
                ImGui::TextUnformatted("Use Primary Camera Position");
                ImGui::Checkbox("##AudioListenerUsePrimaryCameraPosition", &audioListener->UsePrimaryCameraPosition);
                TrackInteractiveMemberMutation<AudioListener2DComponent>(
                    undoService,
                    "Edit Audio Listener 2D Camera Follow",
                    selectedEntity,
                    &AudioListener2DComponent::UsePrimaryCameraPosition,
                    audioListener->UsePrimaryCameraPosition);
                ImGui::TextDisabled("When enabled, spatial 2D sources attenuate and pan relative to this listener.");
                ImGui::TreePop();
            }
        }

        if (ShouldDrawInspectorSection(onlySectionKey, "AudioListener3D") && (registry.try_get<AudioListener3DComponent>(selectedEntity) != nullptr))
        {
            auto* audioListener3D = registry.try_get<AudioListener3DComponent>(selectedEntity);
            const bool listener3DOpen = BeginInspectorSectionHeader("Audio Listener 3D", "AudioListener3DComponentOptions", "...##AudioListener3DComponentOptionsButton");
            if (orderedSectionKeys)
                (void)HandleSectionDragDrop("AudioListener3D", *orderedSectionKeys, "Audio Listener 3D");

            if (ImGui::BeginPopup("AudioListener3DComponentOptions"))
            {
                if (ImGui::MenuItem("Remove Component"))
                    pendingRemovals.RemoveAudioListener3DComponent = true;
                ImGui::EndPopup();
            }

            if (listener3DOpen)
            {
                ImGui::TextUnformatted("Enabled");
                ImGui::Checkbox("##AudioListener3DEnabled", &audioListener3D->Enabled);
                TrackInteractiveMemberMutation<AudioListener3DComponent>(
                    undoService, "Edit Audio Listener 3D Enabled", selectedEntity, &AudioListener3DComponent::Enabled, audioListener3D->Enabled);
                ImGui::TextUnformatted("Use Primary Camera Transform");
                ImGui::Checkbox("##AudioListener3DUsePrimaryCameraTransform", &audioListener3D->UsePrimaryCameraTransform);
                TrackInteractiveMemberMutation<AudioListener3DComponent>(
                    undoService,
                    "Edit Audio Listener 3D Camera Follow",
                    selectedEntity,
                    &AudioListener3DComponent::UsePrimaryCameraTransform,
                    audioListener3D->UsePrimaryCameraTransform);
                ImGui::TextDisabled("When enabled, spatial 3D sources use this listener or the primary camera transform.");
                ImGui::TreePop();
            }
        }

        if (ShouldDrawInspectorSection(onlySectionKey, "AudioSource") && (registry.try_get<AudioSourceComponent>(selectedEntity) != nullptr))
        {
            auto* audioSource = registry.try_get<AudioSourceComponent>(selectedEntity);
            const bool audioOpen = BeginInspectorSectionHeader("Audio Source", "AudioSourceComponentOptions", "...##AudioSourceComponentOptionsButton");
            if (orderedSectionKeys)
                (void)HandleSectionDragDrop("AudioSource", *orderedSectionKeys, "Audio Source");

            if (ImGui::BeginPopup("AudioSourceComponentOptions"))
            {
                if (ImGui::MenuItem("Remove Component"))
                    pendingRemovals.RemoveAudioSourceComponent = true;
                ImGui::EndPopup();
            }

            if (audioOpen)
            {
                auto assignAudioClipKey = [&](const std::string& key) {
                    if (key == audioSource->AudioClipKey)
                        return;
                    if (audioSource->RuntimeVoiceId != 0)
                        Audio::AudioEngine::GetInstance().Stop(audioSource->RuntimeVoiceId);
                    audioSource->AudioClipKey = key;
                    audioSource->RuntimeVoiceId = 0;
                    audioSource->RuntimePlaybackStarted = false;
                    audioSource->RuntimePlayOnStartConsumed = false;
                    audioSource->RuntimeHasPreviousWorldPosition = false;
                    audioSource->RuntimePreviousWorldPosition = glm::vec3(0.0f);
                };

                const std::string clipLabel = audioSource->AudioClipKey.empty()
                    ? std::string("None")
                    : EditorAssetNaming::GetAssetDisplayNameFromAssetKey(audioSource->AudioClipKey);

                ImGui::Text("Clip");
                ImGui::Button((clipLabel + "##AudioClip").c_str(),
                              ImVec2(std::max(60.0f, ImGui::GetContentRegionAvail().x - 90.0f), 0.0f));
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(audioPayloadId))
                    {
                        const char* key = static_cast<const char*>(payload->Data);
                        if (key && key[0])
                            assignAudioClipKey(key);
                    }
                    ImGui::EndDragDropTarget();
                }

                ImGui::SameLine();
                if (ImGui::Button("...##AudioClipPicker"))
                    ImGui::OpenPopup("AudioClipPickerPopup");

                if (ImGui::BeginPopup("AudioClipPickerPopup"))
                {
                    if (ImGui::Selectable("None##AudioClipPickerNone"))
                    {
                        assignAudioClipKey({});
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::Separator();

                    const std::vector<std::string> audioClipKeys = BuildAudioClipPickerKeys();
                    for (const auto& key : audioClipKeys)
                    {
                        const bool isSelected = (audioSource->AudioClipKey == key);
                        const std::string display = EditorAssetNaming::GetAssetDisplayNameFromAssetKey(key);
                        if (ImGui::Selectable((display + "##AudioClipPicker_" + key).c_str(), isSelected))
                        {
                            assignAudioClipKey(key);
                            ImGui::CloseCurrentPopup();
                        }
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                            ImGui::SetTooltip("%s", key.c_str());
                    }
                    ImGui::EndPopup();
                }

                if (!audioSource->AudioClipKey.empty())
                {
                    ImGui::SameLine();
                    if (ImGui::Button("Clear##AudioClip"))
                        assignAudioClipKey({});
                }

                ImGui::TextUnformatted("Play On Start");
                ImGui::Checkbox("##AudioSourcePlayOnStart", &audioSource->PlayOnStart);
                TrackInteractiveMemberMutation<AudioSourceComponent>(
                    undoService, "Edit Audio Play On Start", selectedEntity, &AudioSourceComponent::PlayOnStart, audioSource->PlayOnStart);
                ImGui::TextUnformatted("Loop");
                ImGui::Checkbox("##AudioSourceLoop", &audioSource->Loop);
                TrackInteractiveMemberMutation<AudioSourceComponent>(
                    undoService, "Edit Audio Loop", selectedEntity, &AudioSourceComponent::Loop, audioSource->Loop);
                ImGui::TextUnformatted("Muted");
                ImGui::Checkbox("##AudioSourceMuted", &audioSource->Muted);
                TrackInteractiveMemberMutation<AudioSourceComponent>(
                    undoService, "Edit Audio Muted", selectedEntity, &AudioSourceComponent::Muted, audioSource->Muted);
                ImGui::TextUnformatted("Volume");
                ImGui::SliderFloat("##AudioSourceVolume", &audioSource->Volume, 0.0f, 2.0f, "%.2f");
                TrackInteractiveMemberMutation<AudioSourceComponent>(
                    undoService, "Edit Audio Volume", selectedEntity, &AudioSourceComponent::Volume, audioSource->Volume);
                ImGui::TextUnformatted("Pitch");
                ImGui::SliderFloat("##AudioSourcePitch", &audioSource->Pitch, 0.1f, 4.0f, "%.2f");
                audioSource->Pitch = std::max(0.01f, audioSource->Pitch);
                TrackInteractiveMemberMutation<AudioSourceComponent>(
                    undoService, "Edit Audio Pitch", selectedEntity, &AudioSourceComponent::Pitch, audioSource->Pitch);

                int playbackSpaceIndex = static_cast<int>(audioSource->Space);
                const char* playbackSpaceNames[] = { "Global", "Spatial 2D", "Spatial 3D" };
                ImGui::TextUnformatted("Playback Space");
                if (ImGui::Combo("##AudioSourcePlaybackSpace", &playbackSpaceIndex, playbackSpaceNames, 3))
                    audioSource->Space = static_cast<AudioSourceComponent::PlaybackSpace>(playbackSpaceIndex);
                TrackInteractiveMemberMutation<AudioSourceComponent>(
                    undoService, "Edit Audio Playback Space", selectedEntity, &AudioSourceComponent::Space, audioSource->Space);

                std::array<char, 128> mixerGroupBuffer{};
                std::snprintf(mixerGroupBuffer.data(), mixerGroupBuffer.size(), "%s", audioSource->MixerGroup.c_str());
                ImGui::TextUnformatted("Mixer Group");
                if (ImGui::InputText("##AudioSourceMixerGroup", mixerGroupBuffer.data(), mixerGroupBuffer.size()))
                    audioSource->MixerGroup = mixerGroupBuffer.data();
                if (audioSource->MixerGroup.empty())
                    audioSource->MixerGroup = "SFX";
                TrackInteractiveMemberMutation<AudioSourceComponent>(
                    undoService, "Edit Audio Mixer Group", selectedEntity, &AudioSourceComponent::MixerGroup, audioSource->MixerGroup);

                if (audioSource->Space == AudioSourceComponent::PlaybackSpace::Spatial2D ||
                    audioSource->Space == AudioSourceComponent::PlaybackSpace::Spatial3D)
                {
                    ImGui::TextUnformatted("Min Distance");
                    ImGui::DragFloat("##AudioSourceSpatialMinDistance", &audioSource->SpatialMinDistance, 0.01f, 0.001f, 10000.0f, "%.3f");
                    audioSource->SpatialMinDistance = std::max(0.001f, audioSource->SpatialMinDistance);
                    TrackInteractiveMemberMutation<AudioSourceComponent>(
                        undoService,
                        "Edit Audio Spatial Min Distance",
                        selectedEntity,
                        &AudioSourceComponent::SpatialMinDistance,
                        audioSource->SpatialMinDistance);

                    ImGui::TextUnformatted("Max Distance");
                    ImGui::DragFloat("##AudioSourceSpatialMaxDistance", &audioSource->SpatialMaxDistance, 0.05f, 0.001f, 10000.0f, "%.3f");
                    audioSource->SpatialMaxDistance = std::max(audioSource->SpatialMinDistance, audioSource->SpatialMaxDistance);
                    TrackInteractiveMemberMutation<AudioSourceComponent>(
                        undoService,
                        "Edit Audio Spatial Max Distance",
                        selectedEntity,
                        &AudioSourceComponent::SpatialMaxDistance,
                        audioSource->SpatialMaxDistance);

                    ImGui::TextUnformatted("Rolloff Exponent");
                    ImGui::DragFloat("##AudioSourceSpatialRolloffExponent", &audioSource->SpatialRolloffExponent, 0.01f, 0.01f, 16.0f, "%.2f");
                    audioSource->SpatialRolloffExponent = std::max(0.01f, audioSource->SpatialRolloffExponent);
                    TrackInteractiveMemberMutation<AudioSourceComponent>(
                        undoService,
                        "Edit Audio Spatial Rolloff Exponent",
                        selectedEntity,
                        &AudioSourceComponent::SpatialRolloffExponent,
                        audioSource->SpatialRolloffExponent);

                    int rolloffModeIndex = 0;
                    switch (audioSource->SpatialRolloffMode)
                    {
                        case AudioSourceComponent::RolloffMode::SmoothStep:
                            rolloffModeIndex = 1;
                            break;
                        case AudioSourceComponent::RolloffMode::Inverse:
                            rolloffModeIndex = 2;
                            break;
                        case AudioSourceComponent::RolloffMode::Linear:
                        default:
                            rolloffModeIndex = 0;
                            break;
                    }
                    const char* rolloffModeNames[] = { "Linear", "SmoothStep", "Inverse" };
                    ImGui::TextUnformatted("Rolloff Mode");
                    if (ImGui::Combo("##AudioSourceSpatialRolloffMode", &rolloffModeIndex, rolloffModeNames, 3))
                    {
                        switch (rolloffModeIndex)
                        {
                            case 1:
                                audioSource->SpatialRolloffMode = AudioSourceComponent::RolloffMode::SmoothStep;
                                break;
                            case 2:
                                audioSource->SpatialRolloffMode = AudioSourceComponent::RolloffMode::Inverse;
                                break;
                            case 0:
                            default:
                                audioSource->SpatialRolloffMode = AudioSourceComponent::RolloffMode::Linear;
                                break;
                        }
                    }
                    TrackInteractiveMemberMutation<AudioSourceComponent>(
                        undoService,
                        "Edit Audio Spatial Rolloff Mode",
                        selectedEntity,
                        &AudioSourceComponent::SpatialRolloffMode,
                        audioSource->SpatialRolloffMode);

                    ImGui::TextUnformatted("Stereo Pan Strength");
                    ImGui::SliderFloat("##AudioSourceStereoPanStrength", &audioSource->StereoPanStrength, 0.0f, 1.0f, "%.2f");
                    audioSource->StereoPanStrength = std::clamp(audioSource->StereoPanStrength, 0.0f, 1.0f);
                    TrackInteractiveMemberMutation<AudioSourceComponent>(
                        undoService,
                        "Edit Audio Spatial Pan Strength",
                        selectedEntity,
                        &AudioSourceComponent::StereoPanStrength,
                        audioSource->StereoPanStrength);

                    std::array<char, 256> attenuationCurveBuffer{};
                    std::snprintf(attenuationCurveBuffer.data(), attenuationCurveBuffer.size(), "%s", audioSource->AttenuationCurveKey.c_str());
                    ImGui::TextUnformatted("Attenuation Curve Key");
                    if (ImGui::InputText("##AudioSourceAttenuationCurveKey", attenuationCurveBuffer.data(), attenuationCurveBuffer.size()))
                        audioSource->AttenuationCurveKey = attenuationCurveBuffer.data();
                    TrackInteractiveMemberMutation<AudioSourceComponent>(
                        undoService,
                        "Edit Audio Attenuation Curve Key",
                        selectedEntity,
                        &AudioSourceComponent::AttenuationCurveKey,
                        audioSource->AttenuationCurveKey);

                    if (audioSource->Space == AudioSourceComponent::PlaybackSpace::Spatial3D)
                    {
                        ImGui::TextUnformatted("Doppler Factor");
                        ImGui::DragFloat("##AudioSourceDopplerFactor", &audioSource->DopplerFactor, 0.01f, 0.0f, 8.0f, "%.2f");
                        audioSource->DopplerFactor = std::max(0.0f, audioSource->DopplerFactor);
                        TrackInteractiveMemberMutation<AudioSourceComponent>(
                            undoService,
                            "Edit Audio Doppler Factor",
                            selectedEntity,
                            &AudioSourceComponent::DopplerFactor,
                            audioSource->DopplerFactor);

                        ImGui::TextUnformatted("Enable Directional Attenuation");
                        ImGui::Checkbox("##AudioSourceEnableDirectionalAttenuation", &audioSource->EnableDirectionalAttenuation);
                        TrackInteractiveMemberMutation<AudioSourceComponent>(
                            undoService,
                            "Edit Audio Directional Attenuation Enabled",
                            selectedEntity,
                            &AudioSourceComponent::EnableDirectionalAttenuation,
                            audioSource->EnableDirectionalAttenuation);

                        if (audioSource->EnableDirectionalAttenuation)
                        {
                            ImGui::TextUnformatted("Inner Angle Degrees");
                            ImGui::DragFloat("##AudioSourceDirectionalInnerAngleDegrees", &audioSource->DirectionalInnerAngleDegrees, 0.25f, 0.0f, 360.0f, "%.1f");
                            audioSource->DirectionalInnerAngleDegrees = std::clamp(audioSource->DirectionalInnerAngleDegrees, 0.0f, 360.0f);
                            TrackInteractiveMemberMutation<AudioSourceComponent>(
                                undoService,
                                "Edit Audio Directional Inner Angle",
                                selectedEntity,
                                &AudioSourceComponent::DirectionalInnerAngleDegrees,
                                audioSource->DirectionalInnerAngleDegrees);

                            ImGui::TextUnformatted("Outer Angle Degrees");
                            ImGui::DragFloat("##AudioSourceDirectionalOuterAngleDegrees", &audioSource->DirectionalOuterAngleDegrees, 0.25f, 0.0f, 360.0f, "%.1f");
                            audioSource->DirectionalOuterAngleDegrees = std::clamp(audioSource->DirectionalOuterAngleDegrees, audioSource->DirectionalInnerAngleDegrees, 360.0f);
                            TrackInteractiveMemberMutation<AudioSourceComponent>(
                                undoService,
                                "Edit Audio Directional Outer Angle",
                                selectedEntity,
                                &AudioSourceComponent::DirectionalOuterAngleDegrees,
                                audioSource->DirectionalOuterAngleDegrees);

                            ImGui::TextUnformatted("Outer Volume");
                            ImGui::SliderFloat("##AudioSourceDirectionalOuterVolume", &audioSource->DirectionalOuterVolume, 0.0f, 1.0f, "%.2f");
                            audioSource->DirectionalOuterVolume = std::clamp(audioSource->DirectionalOuterVolume, 0.0f, 1.0f);
                            TrackInteractiveMemberMutation<AudioSourceComponent>(
                                undoService,
                                "Edit Audio Directional Outer Volume",
                                selectedEntity,
                                &AudioSourceComponent::DirectionalOuterVolume,
                                audioSource->DirectionalOuterVolume);
                        }
                    }
                }

                if (audioSource->RuntimeVoiceId != 0 &&
                    !Audio::AudioEngine::GetInstance().IsVoiceActive(audioSource->RuntimeVoiceId))
                {
                    audioSource->RuntimeVoiceId = 0;
                    audioSource->RuntimePlaybackStarted = false;
                }
                const bool isPlaying = (audioSource->RuntimeVoiceId != 0);
                if (isPlaying)
                {
                    if (ImGui::Button("Stop##AudioSourcePreview", ImVec2(120, 0)))
                    {
                        Audio::AudioEngine::GetInstance().Stop(audioSource->RuntimeVoiceId);
                        audioSource->RuntimeVoiceId = 0;
                        audioSource->RuntimePlaybackStarted = false;
                    }
                }
                else
                {
                    if (ImGui::Button("Play##AudioSourcePreview", ImVec2(120, 0)))
                    {
                        if (!audioSource->AudioClipKey.empty())
                        {
                            auto clipAsset = Assets::AudioClipAsset::LoadBlocking(audioSource->AudioClipKey);
                            if (clipAsset && clipAsset->GetClip())
                            {
                                float volume = audioSource->Muted ? 0.0f : std::max(0.0f, audioSource->Volume);
                                float pan = 0.0f;
                                float pitch = std::max(0.01f, audioSource->Pitch);

                                if (scene && scene->IsValid(selectedEntity))
                                {
                                    scene->UpdateTransforms();
                                    const glm::mat4 worldTransform = scene->GetWorldTransformMatrix(selectedEntity);
                                    const glm::vec3 sourcePosition3D(worldTransform[3][0], worldTransform[3][1], worldTransform[3][2]);

                                    if (audioSource->Space == AudioSourceComponent::PlaybackSpace::Spatial2D)
                                    {
                                        const Audio::AudioListenerPositions2D listenerPositions = Audio::CollectAudioListenerPositions2D(*scene);
                                        const Audio::AudioSpatialMix2D spatialMix2D = Audio::ComputeAudioSpatialMix2D(
                                            *audioSource,
                                            glm::vec2(sourcePosition3D.x, sourcePosition3D.y),
                                            listenerPositions);
                                        volume *= spatialMix2D.Gain;
                                        pan = spatialMix2D.Pan;
                                    }
                                    else if (audioSource->Space == AudioSourceComponent::PlaybackSpace::Spatial3D)
                                    {
                                        glm::vec3 sourceForward(-worldTransform[2][0], -worldTransform[2][1], -worldTransform[2][2]);
                                        const float sourceForwardLengthSquared = glm::dot(sourceForward, sourceForward);
                                        if (sourceForwardLengthSquared > 0.000001f)
                                            sourceForward /= std::sqrt(sourceForwardLengthSquared);
                                        else
                                            sourceForward = glm::vec3(0.0f, 0.0f, -1.0f);

                                        const Audio::AudioListenerStates3D listenerStates3D = Audio::CollectAudioListenerStates3D(*scene, 0.0f);
                                        const Audio::AudioSpatialMix3D spatialMix3D = Audio::ComputeAudioSpatialMix3D(
                                            *audioSource,
                                            sourcePosition3D,
                                            sourceForward,
                                            glm::vec3(0.0f),
                                            listenerStates3D);
                                        volume *= spatialMix3D.Gain;
                                        pan = spatialMix3D.Pan;
                                        pitch *= spatialMix3D.PitchMultiplier;
                                    }
                                }

                                pan = std::clamp(pan, -1.0f, 1.0f);
                                pitch = std::max(0.01f, pitch);
                                audioSource->RuntimeVoiceId = Audio::AudioEngine::GetInstance().PlayClip(
                                    clipAsset->GetClip(),
                                    volume,
                                    audioSource->Loop,
                                    audioSource->MixerGroup,
                                    pan,
                                    pitch);
                                audioSource->RuntimePlaybackStarted = (audioSource->RuntimeVoiceId != 0);
                            }
                        }
                    }
                }

                ImGui::TreePop();
            }
        }
    }
}
