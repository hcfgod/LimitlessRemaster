#include "EditorInspectorPanelEntityComponentsShared.h"

#include "EditorAssetNaming.h"
#include "Scene/ParticleEmitterSystem.h"

#include <algorithm>

namespace Limitless::EditorInspectorPanel::Internal
{
    void DrawParticleComponentSections(StandardEntityInspectorContext& context)
    {
        auto& registry = context.Registry;
        const entt::entity selectedEntity = context.SelectedEntity;
        const char* texturePayloadId = context.TexturePayloadId;
        PendingEntityComponentRemovals& pendingRemovals = context.PendingRemovals;
        EditorUndoService* undoService = context.UndoService;
        const std::string_view onlySectionKey = context.OnlySectionKey;
        const std::vector<std::string>* orderedSectionKeys = context.OrderedSectionKeys;

        if (ShouldDrawInspectorSection(onlySectionKey, "ParticleEmitter") && (registry.try_get<ParticleEmitterComponent>(selectedEntity) != nullptr))
        {
            auto* particleEmitter = registry.try_get<ParticleEmitterComponent>(selectedEntity);
            const bool particleOpen = BeginInspectorSectionHeader("Particle Emitter", "ParticleEmitterComponentOptions", "...##ParticleEmitterComponentOptionsButton");
            if (orderedSectionKeys)
                (void)HandleSectionDragDrop("ParticleEmitter", *orderedSectionKeys, "Particle Emitter");

            if (ImGui::BeginPopup("ParticleEmitterComponentOptions"))
            {
                if (ImGui::MenuItem("Remove Component"))
                    pendingRemovals.RemoveParticleEmitterComponent = true;
                ImGui::EndPopup();
            }

            if (particleOpen)
            {
                // -- Playback controls (work in both edit and play mode) --
                {
                    const bool isPlaying = particleEmitter->Playing && !particleEmitter->Paused;
                    const bool isPaused = particleEmitter->Playing && particleEmitter->Paused;

                    if (!particleEmitter->Playing)
                    {
                        if (ImGui::Button("Play##ParticleEmitter"))
                            ParticleEmitterPlay(*particleEmitter);
                    }
                    else
                    {
                        if (ImGui::Button("Stop##ParticleEmitter"))
                            ParticleEmitterStop(*particleEmitter, true);
                    }

                    ImGui::SameLine();
                    if (isPlaying)
                    {
                        if (ImGui::Button("Pause##ParticleEmitter"))
                            ParticleEmitterPause(*particleEmitter);
                    }
                    else if (isPaused)
                    {
                        if (ImGui::Button("Resume##ParticleEmitter"))
                            ParticleEmitterResume(*particleEmitter);
                    }

                    if (particleEmitter->RuntimeState)
                    {
                        ImGui::SameLine();
                        ImGui::Text("Alive: %u", particleEmitter->RuntimeState->AliveCount);
                    }
                }

                ImGui::Separator();

                // -- Emission --
                if (BeginPersistentTreeNode("ParticleEmitter.Emission", "Emission##ParticleEmitter", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    ImGui::TextUnformatted("Spawn Rate");
                    ImGui::DragFloat("##ParticleSpawnRate", &particleEmitter->SpawnRate, 0.5f, 0.0f, 10000.0f);
                    particleEmitter->SpawnRate = std::max(0.0f, particleEmitter->SpawnRate);
                    TrackInteractiveMemberMutation<ParticleEmitterComponent>(
                        undoService, "Edit Particle Spawn Rate", selectedEntity, &ParticleEmitterComponent::SpawnRate, particleEmitter->SpawnRate);

                    ImGui::TextUnformatted("Lifetime Min");
                    ImGui::DragFloat("##ParticleLifetimeMin", &particleEmitter->LifetimeMin, 0.05f, 0.01f, 100.0f);
                    particleEmitter->LifetimeMin = std::max(0.01f, particleEmitter->LifetimeMin);
                    TrackInteractiveMemberMutation<ParticleEmitterComponent>(
                        undoService, "Edit Particle Lifetime Min", selectedEntity, &ParticleEmitterComponent::LifetimeMin, particleEmitter->LifetimeMin);

                    ImGui::TextUnformatted("Lifetime Max");
                    ImGui::DragFloat("##ParticleLifetimeMax", &particleEmitter->LifetimeMax, 0.05f, 0.01f, 100.0f);
                    particleEmitter->LifetimeMax = std::max(particleEmitter->LifetimeMin, particleEmitter->LifetimeMax);
                    TrackInteractiveMemberMutation<ParticleEmitterComponent>(
                        undoService, "Edit Particle Lifetime Max", selectedEntity, &ParticleEmitterComponent::LifetimeMax, particleEmitter->LifetimeMax);

                    ImGui::TextUnformatted("Looping");
                    ImGui::Checkbox("##ParticleLooping", &particleEmitter->Looping);
                    TrackInteractiveMemberMutation<ParticleEmitterComponent>(
                        undoService, "Edit Particle Looping", selectedEntity, &ParticleEmitterComponent::Looping, particleEmitter->Looping);

                    if (!particleEmitter->Looping)
                    {
                        ImGui::TextUnformatted("Duration");
                        ImGui::DragFloat("##ParticleDuration", &particleEmitter->Duration, 0.1f, 0.1f, 600.0f);
                        particleEmitter->Duration = std::max(0.1f, particleEmitter->Duration);
                        TrackInteractiveMemberMutation<ParticleEmitterComponent>(
                            undoService, "Edit Particle Duration", selectedEntity, &ParticleEmitterComponent::Duration, particleEmitter->Duration);
                    }

                    ImGui::TextUnformatted("Play On Start");
                    ImGui::Checkbox("##ParticlePlayOnStart", &particleEmitter->PlayOnStart);
                    TrackInteractiveMemberMutation<ParticleEmitterComponent>(
                        undoService, "Edit Particle Play On Start", selectedEntity, &ParticleEmitterComponent::PlayOnStart, particleEmitter->PlayOnStart);

                    ImGui::TextUnformatted("Burst Enabled");
                    ImGui::Checkbox("##ParticleBurstEnabled", &particleEmitter->BurstEnabled);
                    TrackInteractiveMemberMutation<ParticleEmitterComponent>(
                        undoService, "Edit Particle Burst Enabled", selectedEntity, &ParticleEmitterComponent::BurstEnabled, particleEmitter->BurstEnabled);

                    if (particleEmitter->BurstEnabled)
                    {
                        int burstCount = static_cast<int>(particleEmitter->BurstCount);
                        ImGui::TextUnformatted("Burst Count");
                        ImGui::DragInt("##ParticleBurstCount", &burstCount, 1, 1, static_cast<int>(ParticleEmitterComponent::kMaxParticlesCap));
                        particleEmitter->BurstCount = static_cast<uint32_t>(std::max(1, burstCount));
                        TrackInteractiveMemberMutation<ParticleEmitterComponent>(
                            undoService, "Edit Particle Burst Count", selectedEntity, &ParticleEmitterComponent::BurstCount, particleEmitter->BurstCount);
                    }

                    ImGui::TextUnformatted("Radial Spawn Position");
                    ImGui::Checkbox("##ParticleRadialSpawnPosition", &particleEmitter->UseRadialSpawn);
                    TrackInteractiveMemberMutation<ParticleEmitterComponent>(
                        undoService, "Edit Particle Radial Spawn Position", selectedEntity, &ParticleEmitterComponent::UseRadialSpawn, particleEmitter->UseRadialSpawn);

                    if (particleEmitter->UseRadialSpawn)
                    {
                        ImGui::TextUnformatted("Spawn Radius Min");
                        ImGui::DragFloat("##ParticleSpawnRadiusMin", &particleEmitter->SpawnRadiusMin, 0.01f, 0.0f, 1000.0f);
                        particleEmitter->SpawnRadiusMin = std::max(0.0f, particleEmitter->SpawnRadiusMin);
                        TrackInteractiveMemberMutation<ParticleEmitterComponent>(
                            undoService, "Edit Particle Spawn Radius Min", selectedEntity, &ParticleEmitterComponent::SpawnRadiusMin, particleEmitter->SpawnRadiusMin);

                        ImGui::TextUnformatted("Spawn Radius Max");
                        ImGui::DragFloat("##ParticleSpawnRadiusMax", &particleEmitter->SpawnRadiusMax, 0.01f, 0.0f, 1000.0f);
                        particleEmitter->SpawnRadiusMax = std::max(particleEmitter->SpawnRadiusMin, particleEmitter->SpawnRadiusMax);
                        TrackInteractiveMemberMutation<ParticleEmitterComponent>(
                            undoService, "Edit Particle Spawn Radius Max", selectedEntity, &ParticleEmitterComponent::SpawnRadiusMax, particleEmitter->SpawnRadiusMax);
                    }
                    else
                    {
                        ImGui::TextUnformatted("Spawn Offset Min");
                        EditorPanelStyle::AxisVectorDragState spawnOffsetMinInteractionState{};
                        EditorPanelStyle::DragFloatNWithAxisLabels("##ParticleSpawnOffsetMin", &particleEmitter->SpawnOffsetMin.x, 2, 0.01f, -1000.0f, 1000.0f, "%.3f", 0, &spawnOffsetMinInteractionState);
                        TrackInteractiveVectorMemberMutation<ParticleEmitterComponent>(
                            undoService, "Edit Particle Spawn Offset Min", spawnOffsetMinInteractionState, selectedEntity, &ParticleEmitterComponent::SpawnOffsetMin, particleEmitter->SpawnOffsetMin);

                        ImGui::TextUnformatted("Spawn Offset Max");
                        EditorPanelStyle::AxisVectorDragState spawnOffsetMaxInteractionState{};
                        EditorPanelStyle::DragFloatNWithAxisLabels("##ParticleSpawnOffsetMax", &particleEmitter->SpawnOffsetMax.x, 2, 0.01f, -1000.0f, 1000.0f, "%.3f", 0, &spawnOffsetMaxInteractionState);
                        TrackInteractiveVectorMemberMutation<ParticleEmitterComponent>(
                            undoService, "Edit Particle Spawn Offset Max", spawnOffsetMaxInteractionState, selectedEntity, &ParticleEmitterComponent::SpawnOffsetMax, particleEmitter->SpawnOffsetMax);
                    }

                    ImGui::TreePop();
                }

                // -- Velocity --
                if (BeginPersistentTreeNode("ParticleEmitter.Velocity", "Velocity##ParticleEmitter", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    ImGui::TextUnformatted("Speed Min");
                    ImGui::DragFloat("##ParticleSpeedMin", &particleEmitter->SpeedMin, 0.5f, 0.0f, 10000.0f);
                    particleEmitter->SpeedMin = std::max(0.0f, particleEmitter->SpeedMin);
                    TrackInteractiveMemberMutation<ParticleEmitterComponent>(
                        undoService, "Edit Particle Speed Min", selectedEntity, &ParticleEmitterComponent::SpeedMin, particleEmitter->SpeedMin);

                    ImGui::TextUnformatted("Speed Max");
                    ImGui::DragFloat("##ParticleSpeedMax", &particleEmitter->SpeedMax, 0.5f, 0.0f, 10000.0f);
                    particleEmitter->SpeedMax = std::max(particleEmitter->SpeedMin, particleEmitter->SpeedMax);
                    TrackInteractiveMemberMutation<ParticleEmitterComponent>(
                        undoService, "Edit Particle Speed Max", selectedEntity, &ParticleEmitterComponent::SpeedMax, particleEmitter->SpeedMax);

                    ImGui::TextUnformatted("Angle Min");
                    ImGui::DragFloat("##ParticleAngleMin", &particleEmitter->AngleMin, 1.0f, 0.0f, 360.0f);
                    TrackInteractiveMemberMutation<ParticleEmitterComponent>(
                        undoService, "Edit Particle Angle Min", selectedEntity, &ParticleEmitterComponent::AngleMin, particleEmitter->AngleMin);

                    ImGui::TextUnformatted("Angle Max");
                    ImGui::DragFloat("##ParticleAngleMax", &particleEmitter->AngleMax, 1.0f, 0.0f, 360.0f);
                    TrackInteractiveMemberMutation<ParticleEmitterComponent>(
                        undoService, "Edit Particle Angle Max", selectedEntity, &ParticleEmitterComponent::AngleMax, particleEmitter->AngleMax);

                    ImGui::TextUnformatted("Radial Velocity");
                    ImGui::Checkbox("##ParticleRadialVelocity", &particleEmitter->RadialVelocity);
                    TrackInteractiveMemberMutation<ParticleEmitterComponent>(
                        undoService, "Edit Particle Radial Velocity", selectedEntity, &ParticleEmitterComponent::RadialVelocity, particleEmitter->RadialVelocity);

                    ImGui::TextUnformatted("Gravity Modifier");
                    ImGui::DragFloat("##ParticleGravityModifier", &particleEmitter->GravityModifier, 0.05f, -100.0f, 100.0f);
                    TrackInteractiveMemberMutation<ParticleEmitterComponent>(
                        undoService, "Edit Particle Gravity Modifier", selectedEntity, &ParticleEmitterComponent::GravityModifier, particleEmitter->GravityModifier);

                    ImGui::TreePop();
                }

                // -- Appearance --
                if (BeginPersistentTreeNode("ParticleEmitter.Appearance", "Appearance##ParticleEmitter", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    ImGui::TextUnformatted("Start Size Min");
                    ImGui::DragFloat("##ParticleStartSizeMin", &particleEmitter->StartSizeMin, 0.01f, 0.001f, 100.0f);
                    particleEmitter->StartSizeMin = std::max(0.001f, particleEmitter->StartSizeMin);
                    TrackInteractiveMemberMutation<ParticleEmitterComponent>(
                        undoService, "Edit Particle Start Size Min", selectedEntity, &ParticleEmitterComponent::StartSizeMin, particleEmitter->StartSizeMin);

                    ImGui::TextUnformatted("Start Size Max");
                    ImGui::DragFloat("##ParticleStartSizeMax", &particleEmitter->StartSizeMax, 0.01f, 0.001f, 100.0f);
                    particleEmitter->StartSizeMax = std::max(particleEmitter->StartSizeMin, particleEmitter->StartSizeMax);
                    TrackInteractiveMemberMutation<ParticleEmitterComponent>(
                        undoService, "Edit Particle Start Size Max", selectedEntity, &ParticleEmitterComponent::StartSizeMax, particleEmitter->StartSizeMax);

                    ImGui::TextUnformatted("End Size");
                    ImGui::DragFloat("##ParticleEndSize", &particleEmitter->EndSize, 0.01f, 0.0f, 100.0f);
                    particleEmitter->EndSize = std::max(0.0f, particleEmitter->EndSize);
                    TrackInteractiveMemberMutation<ParticleEmitterComponent>(
                        undoService, "Edit Particle End Size", selectedEntity, &ParticleEmitterComponent::EndSize, particleEmitter->EndSize);

                    ImGui::TextUnformatted("Start Color");
                    ImGui::ColorEdit4("##ParticleStartColor", &particleEmitter->StartColor.r);
                    TrackInteractiveMemberMutation<ParticleEmitterComponent>(
                        undoService, "Edit Particle Start Color", selectedEntity, &ParticleEmitterComponent::StartColor, particleEmitter->StartColor);

                    ImGui::TextUnformatted("End Color");
                    ImGui::ColorEdit4("##ParticleEndColor", &particleEmitter->EndColor.r);
                    TrackInteractiveMemberMutation<ParticleEmitterComponent>(
                        undoService, "Edit Particle End Color", selectedEntity, &ParticleEmitterComponent::EndColor, particleEmitter->EndColor);

                    ImGui::TreePop();
                }

                // -- Rotation --
                if (BeginPersistentTreeNode("ParticleEmitter.Rotation", "Rotation##ParticleEmitter"))
                {
                    ImGui::TextUnformatted("Start Rotation Min");
                    ImGui::DragFloat("##ParticleStartRotationMin", &particleEmitter->StartRotationMin, 1.0f, -360.0f, 360.0f);
                    TrackInteractiveMemberMutation<ParticleEmitterComponent>(
                        undoService, "Edit Particle Start Rotation Min", selectedEntity, &ParticleEmitterComponent::StartRotationMin, particleEmitter->StartRotationMin);

                    ImGui::TextUnformatted("Start Rotation Max");
                    ImGui::DragFloat("##ParticleStartRotationMax", &particleEmitter->StartRotationMax, 1.0f, -360.0f, 360.0f);
                    TrackInteractiveMemberMutation<ParticleEmitterComponent>(
                        undoService, "Edit Particle Start Rotation Max", selectedEntity, &ParticleEmitterComponent::StartRotationMax, particleEmitter->StartRotationMax);

                    ImGui::TextUnformatted("Rotation Speed Min");
                    ImGui::DragFloat("##ParticleRotationSpeedMin", &particleEmitter->RotationSpeedMin, 1.0f, -1000.0f, 1000.0f);
                    TrackInteractiveMemberMutation<ParticleEmitterComponent>(
                        undoService, "Edit Particle Rotation Speed Min", selectedEntity, &ParticleEmitterComponent::RotationSpeedMin, particleEmitter->RotationSpeedMin);

                    ImGui::TextUnformatted("Rotation Speed Max");
                    ImGui::DragFloat("##ParticleRotationSpeedMax", &particleEmitter->RotationSpeedMax, 1.0f, -1000.0f, 1000.0f);
                    TrackInteractiveMemberMutation<ParticleEmitterComponent>(
                        undoService, "Edit Particle Rotation Speed Max", selectedEntity, &ParticleEmitterComponent::RotationSpeedMax, particleEmitter->RotationSpeedMax);

                    ImGui::TreePop();
                }

                // -- Texture --
                if (BeginPersistentTreeNode("ParticleEmitter.Texture", "Texture##ParticleEmitter"))
                {
                    const auto assignParticleTextureKey = [&](const std::string& textureKey) {
                        const std::string beforeTextureKey = particleEmitter->TextureKey;
                        if (beforeTextureKey == textureKey)
                            return;

                        particleEmitter->TextureKey = textureKey;
                        particleEmitter->CachedTexture.reset();
                        particleEmitter->TextureLoadAttempted = false;

                        if (!undoService)
                            return;
                        (void)undoService->ExecuteValueMutation<std::string>(
                            "Edit Particle Emitter Texture",
                            beforeTextureKey,
                            textureKey,
                            [undoService, selectedEntity](const std::string& value) {
                                if (!undoService)
                                    return false;
                                Scene* activeScene = undoService->GetActiveScene();
                                if (!activeScene || !activeScene->IsValid(selectedEntity))
                                    return false;
                                auto* activeEmitter = activeScene->GetRegistry().try_get<ParticleEmitterComponent>(selectedEntity);
                                if (!activeEmitter)
                                    return false;
                                activeEmitter->TextureKey = value;
                                activeEmitter->CachedTexture.reset();
                                activeEmitter->TextureLoadAttempted = false;
                                return true;
                            });
                    };

                    const std::string textureLabel = !particleEmitter->TextureKey.empty()
                        ? EditorAssetNaming::GetAssetDisplayNameFromAssetKey(particleEmitter->TextureKey)
                        : std::string("None (White Quad)");
                    ImGui::Text("Texture");
                    ImGui::Button((textureLabel + "##ParticleEmitterTexture").c_str(),
                                  ImVec2(std::max(60.0f, ImGui::GetContentRegionAvail().x - 90.0f), 0));

                    if (ImGui::BeginDragDropTarget())
                    {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kSubSpritePayloadId))
                        {
                            const char* key = static_cast<const char*>(payload->Data);
                            if (key && key[0])
                            {
                                assignParticleTextureKey(ResolveTextureKeyFromDroppedKey(key));
                            }
                        }
                        else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(texturePayloadId))
                        {
                            const char* key = static_cast<const char*>(payload->Data);
                            if (key && key[0])
                            {
                                assignParticleTextureKey(ResolveTextureKeyFromDroppedKey(key));
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }

                    ImGui::SameLine();
                    if (ImGui::Button("Clear##ParticleEmitterTexture"))
                    {
                        assignParticleTextureKey({});
                    }

                    ImGui::TreePop();
                }

                // -- Limits --
                {
                    int maxParticles = static_cast<int>(particleEmitter->MaxParticles);
                    ImGui::TextUnformatted("Max Particles");
                    ImGui::DragInt("##ParticleMaxParticles", &maxParticles, 16, 1, static_cast<int>(ParticleEmitterComponent::kMaxParticlesCap));
                    particleEmitter->MaxParticles = static_cast<uint32_t>(std::clamp(maxParticles, 1, static_cast<int>(ParticleEmitterComponent::kMaxParticlesCap)));
                    TrackInteractiveMemberMutation<ParticleEmitterComponent>(
                        undoService, "Edit Particle Max Particles", selectedEntity, &ParticleEmitterComponent::MaxParticles, particleEmitter->MaxParticles);
                }

                ImGui::TreePop();
            }
        }
    }
}
