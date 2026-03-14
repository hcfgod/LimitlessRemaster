#include "EditorInspectorPanelEntityComponentsShared.h"

#include <algorithm>

namespace Limitless::EditorInspectorPanel::Internal
{
    void DrawLighting2DComponentSections(StandardEntityInspectorContext& context)
    {
        auto& registry = context.Registry;
        const entt::entity selectedEntity = context.SelectedEntity;
        PendingEntityComponentRemovals& pendingRemovals = context.PendingRemovals;
        EditorUndoService* undoService = context.UndoService;
        const std::string_view onlySectionKey = context.OnlySectionKey;
        const std::vector<std::string>* orderedSectionKeys = context.OrderedSectionKeys;

        if (ShouldDrawInspectorSection(onlySectionKey, "DirectionalLight2D") && (registry.try_get<DirectionalLight2DComponent>(selectedEntity) != nullptr))
        {
            auto* directionalLight = registry.try_get<DirectionalLight2DComponent>(selectedEntity);
            const bool directionalLightOpen = BeginInspectorSectionHeader("Directional Light 2D", "DirectionalLight2DComponentOptions", "...##DirectionalLight2DComponentOptionsButton");
            if (orderedSectionKeys)
                (void)HandleSectionDragDrop("DirectionalLight2D", *orderedSectionKeys, "Directional Light 2D");

            if (ImGui::BeginPopup("DirectionalLight2DComponentOptions"))
            {
                if (ImGui::MenuItem("Remove Component"))
                    pendingRemovals.RemoveDirectionalLight2DComponent = true;
                ImGui::EndPopup();
            }

            if (directionalLightOpen)
            {
                ImGui::TextUnformatted("Enabled");
                ImGui::Checkbox("##DirectionalLightEnabled", &directionalLight->Enabled);
                TrackInteractiveMemberMutation<DirectionalLight2DComponent>(
                    undoService,
                    "Edit Directional Light Enabled",
                    selectedEntity,
                    &DirectionalLight2DComponent::Enabled,
                    directionalLight->Enabled);
                ImGui::TextUnformatted("Color");
                ImGui::ColorEdit3("##DirectionalLightColor", &directionalLight->Color.r);
                TrackInteractiveMemberMutation<DirectionalLight2DComponent>(
                    undoService, "Edit Directional Light Color", selectedEntity, &DirectionalLight2DComponent::Color, directionalLight->Color);
                ImGui::TextUnformatted("Intensity");
                ImGui::DragFloat("##DirectionalLightIntensity", &directionalLight->Intensity, 0.01f, 0.0f, 100.0f, "%.2f");
                TrackInteractiveMemberMutation<DirectionalLight2DComponent>(
                    undoService, "Edit Directional Light Intensity", selectedEntity, &DirectionalLight2DComponent::Intensity, directionalLight->Intensity);
                ImGui::TextUnformatted("Use Entity Rotation");
                ImGui::Checkbox("##DirectionalLightUseEntityRotation", &directionalLight->UseEntityRotation);
                TrackInteractiveMemberMutation<DirectionalLight2DComponent>(
                    undoService,
                    "Edit Directional Light Rotation Mode",
                    selectedEntity,
                    &DirectionalLight2DComponent::UseEntityRotation,
                    directionalLight->UseEntityRotation);
                if (!directionalLight->UseEntityRotation)
                {
                    ImGui::TextUnformatted("Direction");
                    EditorPanelStyle::AxisVectorDragState lightDirectionInteractionState{};
                    EditorPanelStyle::DragFloatNWithAxisLabels("##DirectionalLightDirection", &directionalLight->Direction.x, 2, 0.01f, -1.0f, 1.0f, "%.3f", 0, &lightDirectionInteractionState);
                    directionalLight->Direction = NormalizeDirectionOrFallback(directionalLight->Direction);
                    TrackInteractiveVectorMemberMutation<DirectionalLight2DComponent>(
                        undoService,
                        "Edit Directional Light Direction",
                        lightDirectionInteractionState,
                        selectedEntity,
                        &DirectionalLight2DComponent::Direction,
                        directionalLight->Direction);
                }

                ImGui::TextUnformatted("Cast Shadows");
                ImGui::Checkbox("##DirectionalLightCastShadows", &directionalLight->CastShadows);
                TrackInteractiveMemberMutation<DirectionalLight2DComponent>(
                    undoService, "Edit Directional Light Cast Shadows", selectedEntity, &DirectionalLight2DComponent::CastShadows, directionalLight->CastShadows);
                ImGui::TextUnformatted("Shadow Strength");
                ImGui::DragFloat("##DirectionalLightShadowStrength", &directionalLight->ShadowStrength, 0.01f, 0.0f, 1.0f, "%.2f");
                directionalLight->ShadowStrength = std::clamp(directionalLight->ShadowStrength, 0.0f, 1.0f);
                TrackInteractiveMemberMutation<DirectionalLight2DComponent>(
                    undoService, "Edit Directional Light Shadow Strength", selectedEntity, &DirectionalLight2DComponent::ShadowStrength, directionalLight->ShadowStrength);
                ImGui::TextUnformatted("Shadow Softness");
                ImGui::DragFloat("##DirectionalLightShadowSoftness", &directionalLight->ShadowSoftness, 0.01f, 0.0f, 256.0f, "%.2f");
                directionalLight->ShadowSoftness = std::max(0.0f, directionalLight->ShadowSoftness);
                TrackInteractiveMemberMutation<DirectionalLight2DComponent>(
                    undoService, "Edit Directional Light Shadow Softness", selectedEntity, &DirectionalLight2DComponent::ShadowSoftness, directionalLight->ShadowSoftness);
                ImGui::TextUnformatted("Shadow Samples");
                ImGui::DragInt("##DirectionalLightShadowSamples", &directionalLight->ShadowSamples, 1.0f, 1, 32);
                directionalLight->ShadowSamples = std::clamp(directionalLight->ShadowSamples, 1, 32);
                TrackInteractiveMemberMutation<DirectionalLight2DComponent>(
                    undoService, "Edit Directional Light Shadow Samples", selectedEntity, &DirectionalLight2DComponent::ShadowSamples, directionalLight->ShadowSamples);
                ImGui::TextDisabled("Directional light intensity is global (no distance falloff).");
                ImGui::TextUnformatted("Shadow Distance");
                ImGui::DragFloat("##DirectionalLightShadowDistance", &directionalLight->ShadowDistance, 0.05f, 0.0f, 10000.0f, "%.2f");
                directionalLight->ShadowDistance = std::max(0.0f, directionalLight->ShadowDistance);
                TrackInteractiveMemberMutation<DirectionalLight2DComponent>(
                    undoService, "Edit Directional Light Shadow Distance", selectedEntity, &DirectionalLight2DComponent::ShadowDistance, directionalLight->ShadowDistance);
                ImGui::TextUnformatted("Shadow Bias");
                ImGui::DragFloat("##DirectionalLightShadowBias", &directionalLight->ShadowBias, 0.0005f, 0.0f, 2.0f, "%.4f");
                directionalLight->ShadowBias = std::max(0.0f, directionalLight->ShadowBias);
                TrackInteractiveMemberMutation<DirectionalLight2DComponent>(
                    undoService, "Edit Directional Light Shadow Bias", selectedEntity, &DirectionalLight2DComponent::ShadowBias, directionalLight->ShadowBias);

                ImGui::TreePop();
            }
        }

        if (ShouldDrawInspectorSection(onlySectionKey, "PointLight2D") && (registry.try_get<PointLight2DComponent>(selectedEntity) != nullptr))
        {
            auto* pointLight = registry.try_get<PointLight2DComponent>(selectedEntity);
            const bool pointLightOpen = BeginInspectorSectionHeader("Point Light 2D", "PointLight2DComponentOptions", "...##PointLight2DComponentOptionsButton");
            if (orderedSectionKeys)
                (void)HandleSectionDragDrop("PointLight2D", *orderedSectionKeys, "Point Light 2D");

            if (ImGui::BeginPopup("PointLight2DComponentOptions"))
            {
                if (ImGui::MenuItem("Remove Component"))
                    pendingRemovals.RemovePointLight2DComponent = true;
                ImGui::EndPopup();
            }

            if (pointLightOpen)
            {
                ImGui::TextUnformatted("Enabled");
                ImGui::Checkbox("##PointLightEnabled", &pointLight->Enabled);
                TrackInteractiveMemberMutation<PointLight2DComponent>(
                    undoService, "Edit Point Light Enabled", selectedEntity, &PointLight2DComponent::Enabled, pointLight->Enabled);
                ImGui::TextUnformatted("Color");
                ImGui::ColorEdit3("##PointLightColor", &pointLight->Color.r);
                TrackInteractiveMemberMutation<PointLight2DComponent>(
                    undoService, "Edit Point Light Color", selectedEntity, &PointLight2DComponent::Color, pointLight->Color);
                ImGui::TextUnformatted("Intensity");
                ImGui::DragFloat("##PointLightIntensity", &pointLight->Intensity, 0.01f, 0.0f, 100.0f, "%.2f");
                TrackInteractiveMemberMutation<PointLight2DComponent>(
                    undoService, "Edit Point Light Intensity", selectedEntity, &PointLight2DComponent::Intensity, pointLight->Intensity);
                ImGui::TextUnformatted("Radius");
                ImGui::DragFloat("##PointLightRadius", &pointLight->Radius, 0.01f, 0.01f, 10000.0f, "%.2f");
                pointLight->Radius = std::max(0.01f, pointLight->Radius);
                TrackInteractiveMemberMutation<PointLight2DComponent>(
                    undoService, "Edit Point Light Radius", selectedEntity, &PointLight2DComponent::Radius, pointLight->Radius);
                ImGui::TextUnformatted("Falloff");
                ImGui::DragFloat("##PointLightFalloff", &pointLight->Falloff, 0.01f, 0.1f, 8.0f, "%.2f");
                pointLight->Falloff = std::max(0.1f, pointLight->Falloff);
                TrackInteractiveMemberMutation<PointLight2DComponent>(
                    undoService, "Edit Point Light Falloff", selectedEntity, &PointLight2DComponent::Falloff, pointLight->Falloff);
                ImGui::TextUnformatted("Cast Shadows");
                ImGui::Checkbox("##PointLightCastShadows", &pointLight->CastShadows);
                TrackInteractiveMemberMutation<PointLight2DComponent>(
                    undoService, "Edit Point Light Cast Shadows", selectedEntity, &PointLight2DComponent::CastShadows, pointLight->CastShadows);
                ImGui::TextUnformatted("Shadow Strength");
                ImGui::DragFloat("##PointLightShadowStrength", &pointLight->ShadowStrength, 0.01f, 0.0f, 1.0f, "%.2f");
                pointLight->ShadowStrength = std::clamp(pointLight->ShadowStrength, 0.0f, 1.0f);
                TrackInteractiveMemberMutation<PointLight2DComponent>(
                    undoService, "Edit Point Light Shadow Strength", selectedEntity, &PointLight2DComponent::ShadowStrength, pointLight->ShadowStrength);
                ImGui::TextUnformatted("Shadow Softness");
                ImGui::DragFloat("##PointLightShadowSoftness", &pointLight->ShadowSoftness, 0.01f, 0.0f, 256.0f, "%.2f");
                pointLight->ShadowSoftness = std::max(0.0f, pointLight->ShadowSoftness);
                TrackInteractiveMemberMutation<PointLight2DComponent>(
                    undoService, "Edit Point Light Shadow Softness", selectedEntity, &PointLight2DComponent::ShadowSoftness, pointLight->ShadowSoftness);
                ImGui::TextUnformatted("Shadow Samples");
                ImGui::DragInt("##PointLightShadowSamples", &pointLight->ShadowSamples, 1.0f, 1, 32);
                pointLight->ShadowSamples = std::clamp(pointLight->ShadowSamples, 1, 32);
                TrackInteractiveMemberMutation<PointLight2DComponent>(
                    undoService, "Edit Point Light Shadow Samples", selectedEntity, &PointLight2DComponent::ShadowSamples, pointLight->ShadowSamples);
                ImGui::TextUnformatted("Shadow Bias");
                ImGui::DragFloat("##PointLightShadowBias", &pointLight->ShadowBias, 0.0001f, 0.0f, 10.0f, "%.4f");
                pointLight->ShadowBias = std::max(0.0f, pointLight->ShadowBias);
                TrackInteractiveMemberMutation<PointLight2DComponent>(
                    undoService, "Edit Point Light Shadow Bias", selectedEntity, &PointLight2DComponent::ShadowBias, pointLight->ShadowBias);

                ImGui::TreePop();
            }
        }

        if (ShouldDrawInspectorSection(onlySectionKey, "ShadowOccluder2D") && (registry.try_get<ShadowOccluder2DComponent>(selectedEntity) != nullptr))
        {
            auto* shadowOccluder = registry.try_get<ShadowOccluder2DComponent>(selectedEntity);
            const bool occluderOpen = BeginInspectorSectionHeader("Shadow Occluder 2D", "ShadowOccluder2DComponentOptions", "...##ShadowOccluder2DComponentOptionsButton");
            if (orderedSectionKeys)
                (void)HandleSectionDragDrop("ShadowOccluder2D", *orderedSectionKeys, "Shadow Occluder 2D");

            if (ImGui::BeginPopup("ShadowOccluder2DComponentOptions"))
            {
                if (ImGui::MenuItem("Remove Component"))
                    pendingRemovals.RemoveShadowOccluder2DComponent = true;
                ImGui::EndPopup();
            }

            if (occluderOpen)
            {
                ImGui::TextUnformatted("Enabled");
                ImGui::Checkbox("##ShadowOccluderEnabled", &shadowOccluder->Enabled);
                TrackInteractiveMemberMutation<ShadowOccluder2DComponent>(
                    undoService, "Edit Shadow Occluder Enabled", selectedEntity, &ShadowOccluder2DComponent::Enabled, shadowOccluder->Enabled);

                int sourceMode = static_cast<int>(shadowOccluder->Source);
                const char* sourceModeNames[] = { "Manual Polygon", "Physics Collider" };
                ImGui::TextUnformatted("Source");
                if (ImGui::Combo("##ShadowOccluderSource", &sourceMode, sourceModeNames, 2))
                    shadowOccluder->Source = static_cast<ShadowOccluder2DComponent::SourceMode>(sourceMode);
                TrackInteractiveMemberMutation<ShadowOccluder2DComponent>(
                    undoService, "Edit Shadow Occluder Source", selectedEntity, &ShadowOccluder2DComponent::Source, shadowOccluder->Source);

                ImGui::TextUnformatted("Closed Polygon");
                ImGui::Checkbox("##ShadowOccluderClosedPolygon", &shadowOccluder->Closed);
                TrackInteractiveMemberMutation<ShadowOccluder2DComponent>(
                    undoService, "Edit Shadow Occluder Closed", selectedEntity, &ShadowOccluder2DComponent::Closed, shadowOccluder->Closed);
                ImGui::TextUnformatted("Extrusion");
                ImGui::DragFloat("##ShadowOccluderExtrusion", &shadowOccluder->Extrusion, 0.01f, 0.0f, 1000.0f, "%.2f");
                shadowOccluder->Extrusion = std::max(0.0f, shadowOccluder->Extrusion);
                TrackInteractiveMemberMutation<ShadowOccluder2DComponent>(
                    undoService, "Edit Shadow Occluder Extrusion", selectedEntity, &ShadowOccluder2DComponent::Extrusion, shadowOccluder->Extrusion);

                if (shadowOccluder->Source == ShadowOccluder2DComponent::SourceMode::ManualPolygon)
                {
                    if (ImGui::Button("Add Point"))
                    {
                        const glm::vec2 newPoint = shadowOccluder->PolygonPoints.empty()
                            ? glm::vec2(0.0f)
                            : (shadowOccluder->PolygonPoints.back() + glm::vec2(0.5f, 0.0f));

                        const std::vector<glm::vec2> beforePoints = shadowOccluder->PolygonPoints;
                        shadowOccluder->PolygonPoints.push_back(newPoint);
                        const std::vector<glm::vec2> afterPoints = shadowOccluder->PolygonPoints;
                        if (undoService)
                        {
                            (void)undoService->ExecuteLambdaCommand(
                                "Add Shadow Occluder Point",
                                [undoService, selectedEntity, beforePoints]() {
                                    Scene* activeScene = undoService ? undoService->GetActiveScene() : nullptr;
                                    if (!activeScene || !activeScene->IsValid(selectedEntity))
                                        return false;
                                    auto* activeOccluder = activeScene->GetRegistry().try_get<ShadowOccluder2DComponent>(selectedEntity);
                                    if (!activeOccluder)
                                        return false;
                                    activeOccluder->PolygonPoints = beforePoints;
                                    return true;
                                },
                                [undoService, selectedEntity, afterPoints]() {
                                    Scene* activeScene = undoService ? undoService->GetActiveScene() : nullptr;
                                    if (!activeScene || !activeScene->IsValid(selectedEntity))
                                        return false;
                                    auto* activeOccluder = activeScene->GetRegistry().try_get<ShadowOccluder2DComponent>(selectedEntity);
                                    if (!activeOccluder)
                                        return false;
                                    activeOccluder->PolygonPoints = afterPoints;
                                    return true;
                                });
                        }
                    }

                    int removePointIndex = -1;
                    for (size_t pointIndex = 0; pointIndex < shadowOccluder->PolygonPoints.size(); ++pointIndex)
                    {
                        ImGui::PushID(static_cast<int>(pointIndex));
                        ImGui::TextUnformatted("Point");
                        EditorPanelStyle::AxisVectorDragState shadowPointInteractionState{};
                        EditorPanelStyle::DragFloatNWithAxisLabels("##Point", &shadowOccluder->PolygonPoints[pointIndex].x, 2, 0.01f, -10000.0f, 10000.0f, "%.3f", 0, &shadowPointInteractionState);
                        TrackInteractiveVectorValueMutation(
                            undoService,
                            "Edit Shadow Occluder Point",
                            shadowPointInteractionState,
                            shadowOccluder->PolygonPoints[pointIndex],
                            [undoService, selectedEntity, pointIndex](const glm::vec2& value) {
                                if (!undoService)
                                    return false;
                                Scene* activeScene = undoService->GetActiveScene();
                                if (!activeScene || !activeScene->IsValid(selectedEntity))
                                    return false;
                                auto* activeOccluder = activeScene->GetRegistry().try_get<ShadowOccluder2DComponent>(selectedEntity);
                                if (!activeOccluder || pointIndex >= activeOccluder->PolygonPoints.size())
                                    return false;
                                activeOccluder->PolygonPoints[pointIndex] = value;
                                return true;
                            });
                        ImGui::SameLine();
                        if (ImGui::Button("X"))
                            removePointIndex = static_cast<int>(pointIndex);
                        ImGui::PopID();
                    }

                    if (removePointIndex >= 0)
                    {
                        const std::vector<glm::vec2> beforePoints = shadowOccluder->PolygonPoints;
                        if (removePointIndex >= 0 && removePointIndex < static_cast<int>(shadowOccluder->PolygonPoints.size()))
                            shadowOccluder->PolygonPoints.erase(shadowOccluder->PolygonPoints.begin() + removePointIndex);
                        const std::vector<glm::vec2> afterPoints = shadowOccluder->PolygonPoints;
                        if (undoService)
                        {
                            (void)undoService->ExecuteLambdaCommand(
                                "Remove Shadow Occluder Point",
                                [undoService, selectedEntity, beforePoints]() {
                                    Scene* activeScene = undoService ? undoService->GetActiveScene() : nullptr;
                                    if (!activeScene || !activeScene->IsValid(selectedEntity))
                                        return false;
                                    auto* activeOccluder = activeScene->GetRegistry().try_get<ShadowOccluder2DComponent>(selectedEntity);
                                    if (!activeOccluder)
                                        return false;
                                    activeOccluder->PolygonPoints = beforePoints;
                                    return true;
                                },
                                [undoService, selectedEntity, afterPoints]() {
                                    Scene* activeScene = undoService ? undoService->GetActiveScene() : nullptr;
                                    if (!activeScene || !activeScene->IsValid(selectedEntity))
                                        return false;
                                    auto* activeOccluder = activeScene->GetRegistry().try_get<ShadowOccluder2DComponent>(selectedEntity);
                                    if (!activeOccluder)
                                        return false;
                                    activeOccluder->PolygonPoints = afterPoints;
                                    return true;
                                });
                        }
                    }
                }
                else
                {
                    ImGui::TextDisabled("Uses Box/Circle Collider2D shape on this entity.");
                }

                ImGui::TreePop();
            }
        }
    }
}
