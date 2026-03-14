#include "EditorInspectorPanelEntityComponentsShared.h"

#include <algorithm>

namespace Limitless::EditorInspectorPanel::Internal
{
    void DrawPhysics2DComponentSections(StandardEntityInspectorContext& context)
    {
        Scene* scene = context.SceneContext;
        auto& registry = context.Registry;
        const entt::entity selectedEntity = context.SelectedEntity;
        PendingEntityComponentRemovals& pendingRemovals = context.PendingRemovals;
        EditorUndoService* undoService = context.UndoService;
        const std::string_view onlySectionKey = context.OnlySectionKey;
        const std::vector<std::string>* orderedSectionKeys = context.OrderedSectionKeys;

        if (ShouldDrawInspectorSection(onlySectionKey, "Rigidbody2D") && (registry.try_get<Rigidbody2DComponent>(selectedEntity) != nullptr))
        {
            auto* rigidbody2D = registry.try_get<Rigidbody2DComponent>(selectedEntity);
            const bool rigidbodyOpen = BeginInspectorSectionHeader("Rigidbody 2D", "Rigidbody2DComponentOptions", "...##Rigidbody2DComponentOptionsButton");
            if (orderedSectionKeys)
                (void)HandleSectionDragDrop("Rigidbody2D", *orderedSectionKeys, "Rigidbody 2D");

            if (ImGui::BeginPopup("Rigidbody2DComponentOptions"))
            {
                if (ImGui::MenuItem("Remove Component"))
                    pendingRemovals.RemoveRigidbody2DComponent = true;
                ImGui::EndPopup();
            }

            if (rigidbodyOpen)
            {
                int bodyTypeIndex = static_cast<int>(rigidbody2D->Type);
                const char* bodyTypeNames[] = { "Static", "Dynamic", "Kinematic" };
                ImGui::TextUnformatted("Body Type");
                if (ImGui::Combo("##Rigidbody2DBodyType", &bodyTypeIndex, bodyTypeNames, 3))
                    rigidbody2D->Type = static_cast<Rigidbody2DComponent::BodyType>(bodyTypeIndex);
                TrackInteractiveMemberMutation<Rigidbody2DComponent>(
                    undoService, "Edit Rigidbody2D Body Type", selectedEntity, &Rigidbody2DComponent::Type, rigidbody2D->Type);
                if (rigidbody2D->Type == Rigidbody2DComponent::BodyType::Kinematic)
                {
                    ImGui::TextWrapped(
                        "Note: Box2D kinematic bodies do not generate physical collision response against static/kinematic bodies "
                        "(including static tilemap colliders). Use Dynamic + Gravity Scale 0 for moving collidable platforms.");
                }
                const uint16_t sceneWorldCount = scene ? std::max<uint16_t>(1, scene->GetPhysics2DWorldCount()) : 1;
                int physicsWorldSlot = std::min<int>(rigidbody2D->PhysicsWorldSlot, static_cast<int>(sceneWorldCount - 1));
                ImGui::TextUnformatted("Physics World Slot");
                ImGui::DragInt("##Rigidbody2DPhysicsWorldSlot", &physicsWorldSlot, 1.0f, 0, static_cast<int>(sceneWorldCount - 1));
                physicsWorldSlot = std::clamp(physicsWorldSlot, 0, static_cast<int>(sceneWorldCount - 1));
                rigidbody2D->PhysicsWorldSlot = static_cast<uint16_t>(physicsWorldSlot);
                TrackInteractiveMemberMutation<Rigidbody2DComponent>(
                    undoService,
                    "Edit Rigidbody2D Physics World Slot",
                    selectedEntity,
                    &Rigidbody2DComponent::PhysicsWorldSlot,
                    rigidbody2D->PhysicsWorldSlot);

                bool freezeRotation = rigidbody2D->IsRotationLocked();
                ImGui::TextDisabled("Constraints");
                ImGui::TextUnformatted("Freeze Position X");
                ImGui::Checkbox("##Rigidbody2DFreezePositionX", &rigidbody2D->FreezePositionX);
                TrackInteractiveMemberMutation<Rigidbody2DComponent>(
                    undoService, "Edit Rigidbody2D Freeze Position X", selectedEntity, &Rigidbody2DComponent::FreezePositionX, rigidbody2D->FreezePositionX);
                ImGui::TextUnformatted("Freeze Position Y");
                ImGui::Checkbox("##Rigidbody2DFreezePositionY", &rigidbody2D->FreezePositionY);
                TrackInteractiveMemberMutation<Rigidbody2DComponent>(
                    undoService, "Edit Rigidbody2D Freeze Position Y", selectedEntity, &Rigidbody2DComponent::FreezePositionY, rigidbody2D->FreezePositionY);
                ImGui::TextUnformatted("Freeze Rotation");
                if (ImGui::Checkbox("##Rigidbody2DFreezeRotation", &freezeRotation))
                {
                    rigidbody2D->FixedRotation = freezeRotation;
                }
                TrackInteractiveMemberMutation<Rigidbody2DComponent>(
                    undoService, "Edit Rigidbody2D Freeze Rotation", selectedEntity, &Rigidbody2DComponent::FixedRotation, rigidbody2D->FixedRotation);
                ImGui::TextUnformatted("Use CCD");
                ImGui::Checkbox("##Rigidbody2DUseCcd", &rigidbody2D->UseCCD);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    ImGui::SetTooltip("Continuous Collision Detection. Use for fast-moving bodies to reduce tunneling through colliders.");
                TrackInteractiveMemberMutation<Rigidbody2DComponent>(
                    undoService, "Edit Rigidbody2D Use CCD", selectedEntity, &Rigidbody2DComponent::UseCCD, rigidbody2D->UseCCD);
                ImGui::TextUnformatted("Enable Sleep");
                ImGui::Checkbox("##Rigidbody2DEnableSleep", &rigidbody2D->EnableSleep);
                TrackInteractiveMemberMutation<Rigidbody2DComponent>(
                    undoService, "Edit Rigidbody2D Enable Sleep", selectedEntity, &Rigidbody2DComponent::EnableSleep, rigidbody2D->EnableSleep);
                ImGui::TextUnformatted("Start Awake");
                ImGui::Checkbox("##Rigidbody2DStartAwake", &rigidbody2D->StartAwake);
                TrackInteractiveMemberMutation<Rigidbody2DComponent>(
                    undoService, "Edit Rigidbody2D Start Awake", selectedEntity, &Rigidbody2DComponent::StartAwake, rigidbody2D->StartAwake);
                ImGui::TextUnformatted("Interpolate");
                ImGui::Checkbox("##Rigidbody2DInterpolate", &rigidbody2D->Interpolate);
                TrackInteractiveMemberMutation<Rigidbody2DComponent>(
                    undoService, "Edit Rigidbody2D Interpolate", selectedEntity, &Rigidbody2DComponent::Interpolate, rigidbody2D->Interpolate);
                ImGui::TextUnformatted("High Contact Quality");
                ImGui::Checkbox("##Rigidbody2DHighContactQuality", &rigidbody2D->HighContactQuality);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    ImGui::SetTooltip("Applies extra world solver sub-steps when this body is present. Useful for rotating platforms and dense contact stacks.");
                TrackInteractiveMemberMutation<Rigidbody2DComponent>(
                    undoService,
                    "Edit Rigidbody2D High Contact Quality",
                    selectedEntity,
                    &Rigidbody2DComponent::HighContactQuality,
                    rigidbody2D->HighContactQuality);
                ImGui::TextUnformatted("Extra Solver Sub Steps");
                ImGui::DragInt("##Rigidbody2DExtraSolverSubSteps", &rigidbody2D->ExtraSolverSubSteps, 1.0f, 0, 24);
                rigidbody2D->ExtraSolverSubSteps = std::max(0, rigidbody2D->ExtraSolverSubSteps);
                TrackInteractiveMemberMutation<Rigidbody2DComponent>(
                    undoService,
                    "Edit Rigidbody2D Extra Solver Sub Steps",
                    selectedEntity,
                    &Rigidbody2DComponent::ExtraSolverSubSteps,
                    rigidbody2D->ExtraSolverSubSteps);
                ImGui::TextUnformatted("Gravity Scale");
                ImGui::DragFloat("##Rigidbody2DGravityScale", &rigidbody2D->GravityScale, 0.01f, -10.0f, 10.0f);
                TrackInteractiveMemberMutation<Rigidbody2DComponent>(
                    undoService, "Edit Rigidbody2D Gravity Scale", selectedEntity, &Rigidbody2DComponent::GravityScale, rigidbody2D->GravityScale);
                ImGui::TextUnformatted("Linear Damping");
                ImGui::DragFloat("##Rigidbody2DLinearDamping", &rigidbody2D->LinearDamping, 0.01f, 0.0f, 100.0f);
                TrackInteractiveMemberMutation<Rigidbody2DComponent>(
                    undoService, "Edit Rigidbody2D Linear Damping", selectedEntity, &Rigidbody2DComponent::LinearDamping, rigidbody2D->LinearDamping);
                ImGui::TextUnformatted("Angular Damping");
                ImGui::DragFloat("##Rigidbody2DAngularDamping", &rigidbody2D->AngularDamping, 0.01f, 0.0f, 100.0f);
                TrackInteractiveMemberMutation<Rigidbody2DComponent>(
                    undoService, "Edit Rigidbody2D Angular Damping", selectedEntity, &Rigidbody2DComponent::AngularDamping, rigidbody2D->AngularDamping);

                ImGui::Separator();
                ImGui::TextDisabled("Runtime Diagnostics");
                if (!scene)
                {
                    ImGui::TextDisabled("Scene unavailable.");
                }
                else
                {
                    const uint16_t worldSlot = std::min<uint16_t>(rigidbody2D->PhysicsWorldSlot, static_cast<uint16_t>(scene->GetPhysics2DWorldCount() - 1));
                    const Physics2DWorld* physicsWorld = scene->GetPhysics2DWorld(worldSlot);
                    if (!physicsWorld)
                    {
                        ImGui::TextDisabled("Physics world is not initialized.");
                    }
                    else
                    {
                        ImGui::Text("World Slot: %u", static_cast<unsigned>(worldSlot));
                        const Physics2DDiagnostics& worldDiagnostics = physicsWorld->GetDiagnostics();
                        ImGui::Text("Bodies: %d (Awake: %d, Sleeping: %d)",
                                    worldDiagnostics.BodyCount,
                                    worldDiagnostics.AwakeBodyCount,
                                    worldDiagnostics.SleepingBodyCount);
                        ImGui::Text("Contacts: %d | Penetrating Points: %d",
                                    worldDiagnostics.ContactPairCount,
                                    worldDiagnostics.PenetratingContactPointCount);
                        ImGui::Text("Max Penetration Depth: %.5f", worldDiagnostics.MaxPenetrationDepth);

                        Physics2DBodyDiagnostics bodyDiagnostics{};
                        if (physicsWorld->TryGetBodyDiagnostics(selectedEntity, bodyDiagnostics))
                        {
                            ImGui::Text("Body Awake: %s", bodyDiagnostics.IsAwake ? "Yes" : "No");
                            ImGui::Text("Body Contact Pairs: %d", bodyDiagnostics.ContactPairCount);
                            ImGui::Text("Body Penetrating Points: %d", bodyDiagnostics.PenetratingContactPointCount);
                            ImGui::Text("Body Max Penetration: %.5f", bodyDiagnostics.MaxPenetrationDepth);
                        }
                        else
                        {
                            ImGui::TextDisabled("No active runtime body diagnostics for selected entity.");
                        }
                    }
                }

                ImGui::TreePop();
            }
        }

        if (ShouldDrawInspectorSection(onlySectionKey, "BoxCollider2D") && (registry.try_get<BoxCollider2DComponent>(selectedEntity) != nullptr))
        {
            auto* boxCollider2D = registry.try_get<BoxCollider2DComponent>(selectedEntity);
            const bool boxColliderOpen = BeginInspectorSectionHeader("Box Collider 2D", "BoxCollider2DComponentOptions", "...##BoxCollider2DComponentOptionsButton");
            if (orderedSectionKeys)
                (void)HandleSectionDragDrop("BoxCollider2D", *orderedSectionKeys, "Box Collider 2D");

            if (ImGui::BeginPopup("BoxCollider2DComponentOptions"))
            {
                if (ImGui::MenuItem("Remove Component"))
                    pendingRemovals.RemoveBoxCollider2DComponent = true;
                ImGui::EndPopup();
            }

            if (boxColliderOpen)
            {
                ImGui::TextUnformatted("Offset");
                EditorPanelStyle::AxisVectorDragState boxOffsetInteractionState{};
                EditorPanelStyle::DragFloatNWithAxisLabels("##BoxColliderOffset", &boxCollider2D->Offset.x, 2, 0.01f, 0.0f, 0.0f, "%.3f", 0, &boxOffsetInteractionState);
                TrackInteractiveVectorMemberMutation<BoxCollider2DComponent>(
                    undoService, "Edit BoxCollider2D Offset", boxOffsetInteractionState, selectedEntity, &BoxCollider2DComponent::Offset, boxCollider2D->Offset);
                ImGui::TextUnformatted("Size");
                EditorPanelStyle::AxisVectorDragState boxSizeInteractionState{};
                EditorPanelStyle::DragFloatNWithAxisLabels("##BoxColliderSize", &boxCollider2D->Size.x, 2, 0.01f, 0.001f, 1000.0f, "%.3f", 0, &boxSizeInteractionState);
                TrackInteractiveVectorMemberMutation<BoxCollider2DComponent>(
                    undoService, "Edit BoxCollider2D Size", boxSizeInteractionState, selectedEntity, &BoxCollider2DComponent::Size, boxCollider2D->Size);
                ImGui::TextUnformatted("Density");
                ImGui::DragFloat("##BoxColliderDensity", &boxCollider2D->Density, 0.01f, 0.0f, 1000.0f);
                TrackInteractiveMemberMutation<BoxCollider2DComponent>(
                    undoService, "Edit BoxCollider2D Density", selectedEntity, &BoxCollider2DComponent::Density, boxCollider2D->Density);
                ImGui::TextUnformatted("Friction");
                ImGui::DragFloat("##BoxColliderFriction", &boxCollider2D->Friction, 0.01f, 0.0f, 1.0f);
                TrackInteractiveMemberMutation<BoxCollider2DComponent>(
                    undoService, "Edit BoxCollider2D Friction", selectedEntity, &BoxCollider2DComponent::Friction, boxCollider2D->Friction);
                ImGui::TextUnformatted("Restitution");
                ImGui::DragFloat("##BoxColliderRestitution", &boxCollider2D->Restitution, 0.01f, 0.0f, 1.0f);
                TrackInteractiveMemberMutation<BoxCollider2DComponent>(
                    undoService, "Edit BoxCollider2D Restitution", selectedEntity, &BoxCollider2DComponent::Restitution, boxCollider2D->Restitution);
                ImGui::TextUnformatted("Is Sensor");
                ImGui::Checkbox("##BoxColliderIsSensor", &boxCollider2D->IsSensor);
                TrackInteractiveMemberMutation<BoxCollider2DComponent>(
                    undoService, "Edit BoxCollider2D Sensor", selectedEntity, &BoxCollider2DComponent::IsSensor, boxCollider2D->IsSensor);
                ImGui::TextUnformatted("Layer Bits");
                ImGui::InputScalar("##BoxColliderLayerBits", ImGuiDataType_U64, &boxCollider2D->CollisionLayer);
                TrackInteractiveMemberMutation<BoxCollider2DComponent>(
                    undoService, "Edit BoxCollider2D Layer", selectedEntity, &BoxCollider2DComponent::CollisionLayer, boxCollider2D->CollisionLayer);
                ImGui::TextUnformatted("Mask Bits");
                ImGui::InputScalar("##BoxColliderMaskBits", ImGuiDataType_U64, &boxCollider2D->CollisionMask);
                TrackInteractiveMemberMutation<BoxCollider2DComponent>(
                    undoService, "Edit BoxCollider2D Mask", selectedEntity, &BoxCollider2DComponent::CollisionMask, boxCollider2D->CollisionMask);

                ImGui::TreePop();
            }
        }

        if (ShouldDrawInspectorSection(onlySectionKey, "CircleCollider2D") && (registry.try_get<CircleCollider2DComponent>(selectedEntity) != nullptr))
        {
            auto* circleCollider2D = registry.try_get<CircleCollider2DComponent>(selectedEntity);
            const bool circleColliderOpen = BeginInspectorSectionHeader("Circle Collider 2D", "CircleCollider2DComponentOptions", "...##CircleCollider2DComponentOptionsButton");
            if (orderedSectionKeys)
                (void)HandleSectionDragDrop("CircleCollider2D", *orderedSectionKeys, "Circle Collider 2D");

            if (ImGui::BeginPopup("CircleCollider2DComponentOptions"))
            {
                if (ImGui::MenuItem("Remove Component"))
                    pendingRemovals.RemoveCircleCollider2DComponent = true;
                ImGui::EndPopup();
            }

            if (circleColliderOpen)
            {
                ImGui::TextUnformatted("Offset");
                EditorPanelStyle::AxisVectorDragState circleOffsetInteractionState{};
                EditorPanelStyle::DragFloatNWithAxisLabels("##CircleColliderOffset", &circleCollider2D->Offset.x, 2, 0.01f, 0.0f, 0.0f, "%.3f", 0, &circleOffsetInteractionState);
                TrackInteractiveVectorMemberMutation<CircleCollider2DComponent>(
                    undoService, "Edit CircleCollider2D Offset", circleOffsetInteractionState, selectedEntity, &CircleCollider2DComponent::Offset, circleCollider2D->Offset);
                ImGui::TextUnformatted("Radius");
                ImGui::DragFloat("##CircleColliderRadius", &circleCollider2D->Radius, 0.01f, 0.001f, 1000.0f);
                TrackInteractiveMemberMutation<CircleCollider2DComponent>(
                    undoService, "Edit CircleCollider2D Radius", selectedEntity, &CircleCollider2DComponent::Radius, circleCollider2D->Radius);
                ImGui::TextUnformatted("Density");
                ImGui::DragFloat("##CircleColliderDensity", &circleCollider2D->Density, 0.01f, 0.0f, 1000.0f);
                TrackInteractiveMemberMutation<CircleCollider2DComponent>(
                    undoService, "Edit CircleCollider2D Density", selectedEntity, &CircleCollider2DComponent::Density, circleCollider2D->Density);
                ImGui::TextUnformatted("Friction");
                ImGui::DragFloat("##CircleColliderFriction", &circleCollider2D->Friction, 0.01f, 0.0f, 1.0f);
                TrackInteractiveMemberMutation<CircleCollider2DComponent>(
                    undoService, "Edit CircleCollider2D Friction", selectedEntity, &CircleCollider2DComponent::Friction, circleCollider2D->Friction);
                ImGui::TextUnformatted("Restitution");
                ImGui::DragFloat("##CircleColliderRestitution", &circleCollider2D->Restitution, 0.01f, 0.0f, 1.0f);
                TrackInteractiveMemberMutation<CircleCollider2DComponent>(
                    undoService, "Edit CircleCollider2D Restitution", selectedEntity, &CircleCollider2DComponent::Restitution, circleCollider2D->Restitution);
                ImGui::TextUnformatted("Is Sensor");
                ImGui::Checkbox("##CircleColliderIsSensor", &circleCollider2D->IsSensor);
                TrackInteractiveMemberMutation<CircleCollider2DComponent>(
                    undoService, "Edit CircleCollider2D Sensor", selectedEntity, &CircleCollider2DComponent::IsSensor, circleCollider2D->IsSensor);
                ImGui::TextUnformatted("Layer Bits");
                ImGui::InputScalar("##CircleColliderLayerBits", ImGuiDataType_U64, &circleCollider2D->CollisionLayer);
                TrackInteractiveMemberMutation<CircleCollider2DComponent>(
                    undoService, "Edit CircleCollider2D Layer", selectedEntity, &CircleCollider2DComponent::CollisionLayer, circleCollider2D->CollisionLayer);
                ImGui::TextUnformatted("Mask Bits");
                ImGui::InputScalar("##CircleColliderMaskBits", ImGuiDataType_U64, &circleCollider2D->CollisionMask);
                TrackInteractiveMemberMutation<CircleCollider2DComponent>(
                    undoService, "Edit CircleCollider2D Mask", selectedEntity, &CircleCollider2DComponent::CollisionMask, circleCollider2D->CollisionMask);

                ImGui::TreePop();
            }
        }

        if (ShouldDrawInspectorSection(onlySectionKey, "PolygonCollider2D") && (registry.try_get<PolygonCollider2DComponent>(selectedEntity) != nullptr))
        {
            auto* polygonCollider2D = registry.try_get<PolygonCollider2DComponent>(selectedEntity);
            const bool polygonColliderOpen = BeginInspectorSectionHeader("Polygon Collider 2D", "PolygonCollider2DComponentOptions", "...##PolygonCollider2DComponentOptionsButton");
            if (orderedSectionKeys)
                (void)HandleSectionDragDrop("PolygonCollider2D", *orderedSectionKeys, "Polygon Collider 2D");

            if (ImGui::BeginPopup("PolygonCollider2DComponentOptions"))
            {
                if (ImGui::MenuItem("Remove Component"))
                    pendingRemovals.RemovePolygonCollider2DComponent = true;
                ImGui::EndPopup();
            }

            if (polygonColliderOpen)
            {
                ImGui::TextUnformatted("Offset");
                EditorPanelStyle::AxisVectorDragState polygonOffsetInteractionState{};
                EditorPanelStyle::DragFloatNWithAxisLabels("##PolygonColliderOffset", &polygonCollider2D->Offset.x, 2, 0.01f, 0.0f, 0.0f, "%.3f", 0, &polygonOffsetInteractionState);
                TrackInteractiveVectorMemberMutation<PolygonCollider2DComponent>(
                    undoService, "Edit PolygonCollider2D Offset", polygonOffsetInteractionState, selectedEntity, &PolygonCollider2DComponent::Offset, polygonCollider2D->Offset);

                if (ImGui::Button("Add Point##PolygonCollider2D"))
                {
                    if (polygonCollider2D->Points.size() < kPhysics2DPolygonMaxPoints)
                    {
                        const std::vector<glm::vec2> beforePoints = polygonCollider2D->Points;
                        const glm::vec2 newPoint = polygonCollider2D->Points.empty()
                            ? glm::vec2(0.0f)
                            : (polygonCollider2D->Points.back() + glm::vec2(0.5f, 0.0f));
                        polygonCollider2D->Points.push_back(newPoint);
                        const std::vector<glm::vec2> afterPoints = polygonCollider2D->Points;
                        ExecuteVectorMemberMutation<PolygonCollider2DComponent, glm::vec2>(
                            undoService,
                            "Add Polygon Collider Point",
                            selectedEntity,
                            &PolygonCollider2DComponent::Points,
                            beforePoints,
                            afterPoints);
                    }
                }
                if (polygonCollider2D->Points.size() >= kPhysics2DPolygonMaxPoints)
                    ImGui::TextDisabled("Maximum %d points", static_cast<int>(kPhysics2DPolygonMaxPoints));

                int removePolygonPointIndex = -1;
                for (size_t pointIndex = 0; pointIndex < polygonCollider2D->Points.size(); ++pointIndex)
                {
                    ImGui::PushID(static_cast<int>(pointIndex));
                    ImGui::TextUnformatted("Point");
                    EditorPanelStyle::AxisVectorDragState polygonPointInteractionState{};
                    EditorPanelStyle::DragFloatNWithAxisLabels("##PolygonColliderPoint", &polygonCollider2D->Points[pointIndex].x, 2, 0.01f, -10000.0f, 10000.0f, "%.3f", 0, &polygonPointInteractionState);
                    TrackInteractiveVectorValueMutation(
                        undoService,
                        "Edit Polygon Collider Point",
                        polygonPointInteractionState,
                        polygonCollider2D->Points[pointIndex],
                        [undoService, selectedEntity, pointIndex](const glm::vec2& value) {
                            if (!undoService)
                                return false;
                            Scene* activeScene = undoService->GetActiveScene();
                            if (!activeScene || !activeScene->IsValid(selectedEntity))
                                return false;
                            auto* activePolygonCollider2D = activeScene->GetRegistry().try_get<PolygonCollider2DComponent>(selectedEntity);
                            if (!activePolygonCollider2D || pointIndex >= activePolygonCollider2D->Points.size())
                                return false;
                            activePolygonCollider2D->Points[pointIndex] = value;
                            return true;
                        });
                    ImGui::SameLine();
                    if (ImGui::Button("X##PolygonColliderPointRemove"))
                        removePolygonPointIndex = static_cast<int>(pointIndex);
                    ImGui::PopID();
                }

                if (removePolygonPointIndex >= 0 && removePolygonPointIndex < static_cast<int>(polygonCollider2D->Points.size()))
                {
                    const std::vector<glm::vec2> beforePoints = polygonCollider2D->Points;
                    polygonCollider2D->Points.erase(polygonCollider2D->Points.begin() + removePolygonPointIndex);
                    const std::vector<glm::vec2> afterPoints = polygonCollider2D->Points;
                    ExecuteVectorMemberMutation<PolygonCollider2DComponent, glm::vec2>(
                        undoService,
                        "Remove Polygon Collider Point",
                        selectedEntity,
                        &PolygonCollider2DComponent::Points,
                        beforePoints,
                        afterPoints);
                }

                ImGui::TextUnformatted("Density");
                ImGui::DragFloat("##PolygonColliderDensity", &polygonCollider2D->Density, 0.01f, 0.0f, 1000.0f);
                TrackInteractiveMemberMutation<PolygonCollider2DComponent>(
                    undoService, "Edit PolygonCollider2D Density", selectedEntity, &PolygonCollider2DComponent::Density, polygonCollider2D->Density);
                ImGui::TextUnformatted("Friction");
                ImGui::DragFloat("##PolygonColliderFriction", &polygonCollider2D->Friction, 0.01f, 0.0f, 1.0f);
                TrackInteractiveMemberMutation<PolygonCollider2DComponent>(
                    undoService, "Edit PolygonCollider2D Friction", selectedEntity, &PolygonCollider2DComponent::Friction, polygonCollider2D->Friction);
                ImGui::TextUnformatted("Restitution");
                ImGui::DragFloat("##PolygonColliderRestitution", &polygonCollider2D->Restitution, 0.01f, 0.0f, 1.0f);
                TrackInteractiveMemberMutation<PolygonCollider2DComponent>(
                    undoService, "Edit PolygonCollider2D Restitution", selectedEntity, &PolygonCollider2DComponent::Restitution, polygonCollider2D->Restitution);
                ImGui::TextUnformatted("Is Sensor");
                ImGui::Checkbox("##PolygonColliderIsSensor", &polygonCollider2D->IsSensor);
                TrackInteractiveMemberMutation<PolygonCollider2DComponent>(
                    undoService, "Edit PolygonCollider2D Sensor", selectedEntity, &PolygonCollider2DComponent::IsSensor, polygonCollider2D->IsSensor);
                ImGui::TextUnformatted("Layer Bits");
                ImGui::InputScalar("##PolygonColliderLayerBits", ImGuiDataType_U64, &polygonCollider2D->CollisionLayer);
                TrackInteractiveMemberMutation<PolygonCollider2DComponent>(
                    undoService, "Edit PolygonCollider2D Layer", selectedEntity, &PolygonCollider2DComponent::CollisionLayer, polygonCollider2D->CollisionLayer);
                ImGui::TextUnformatted("Mask Bits");
                ImGui::InputScalar("##PolygonColliderMaskBits", ImGuiDataType_U64, &polygonCollider2D->CollisionMask);
                TrackInteractiveMemberMutation<PolygonCollider2DComponent>(
                    undoService, "Edit PolygonCollider2D Mask", selectedEntity, &PolygonCollider2DComponent::CollisionMask, polygonCollider2D->CollisionMask);

                ImGui::TreePop();
            }
        }

        if (ShouldDrawInspectorSection(onlySectionKey, "EdgeCollider2D") && (registry.try_get<EdgeCollider2DComponent>(selectedEntity) != nullptr))
        {
            auto* edgeCollider2D = registry.try_get<EdgeCollider2DComponent>(selectedEntity);
            const bool edgeColliderOpen = BeginInspectorSectionHeader("Edge Collider 2D", "EdgeCollider2DComponentOptions", "...##EdgeCollider2DComponentOptionsButton");
            if (orderedSectionKeys)
                (void)HandleSectionDragDrop("EdgeCollider2D", *orderedSectionKeys, "Edge Collider 2D");

            if (ImGui::BeginPopup("EdgeCollider2DComponentOptions"))
            {
                if (ImGui::MenuItem("Remove Component"))
                    pendingRemovals.RemoveEdgeCollider2DComponent = true;
                ImGui::EndPopup();
            }

            if (edgeColliderOpen)
            {
                ImGui::TextUnformatted("Offset");
                EditorPanelStyle::AxisVectorDragState edgeOffsetInteractionState{};
                EditorPanelStyle::DragFloatNWithAxisLabels("##EdgeColliderOffset", &edgeCollider2D->Offset.x, 2, 0.01f, 0.0f, 0.0f, "%.3f", 0, &edgeOffsetInteractionState);
                TrackInteractiveVectorMemberMutation<EdgeCollider2DComponent>(
                    undoService, "Edit EdgeCollider2D Offset", edgeOffsetInteractionState, selectedEntity, &EdgeCollider2DComponent::Offset, edgeCollider2D->Offset);
                ImGui::TextUnformatted("Point A");
                EditorPanelStyle::AxisVectorDragState edgePointAInteractionState{};
                EditorPanelStyle::DragFloatNWithAxisLabels("##EdgeColliderPointA", &edgeCollider2D->PointA.x, 2, 0.01f, 0.0f, 0.0f, "%.3f", 0, &edgePointAInteractionState);
                TrackInteractiveVectorMemberMutation<EdgeCollider2DComponent>(
                    undoService, "Edit EdgeCollider2D Point A", edgePointAInteractionState, selectedEntity, &EdgeCollider2DComponent::PointA, edgeCollider2D->PointA);
                ImGui::TextUnformatted("Point B");
                EditorPanelStyle::AxisVectorDragState edgePointBInteractionState{};
                EditorPanelStyle::DragFloatNWithAxisLabels("##EdgeColliderPointB", &edgeCollider2D->PointB.x, 2, 0.01f, 0.0f, 0.0f, "%.3f", 0, &edgePointBInteractionState);
                TrackInteractiveVectorMemberMutation<EdgeCollider2DComponent>(
                    undoService, "Edit EdgeCollider2D Point B", edgePointBInteractionState, selectedEntity, &EdgeCollider2DComponent::PointB, edgeCollider2D->PointB);
                ImGui::TextUnformatted("Friction");
                ImGui::DragFloat("##EdgeColliderFriction", &edgeCollider2D->Friction, 0.01f, 0.0f, 1.0f);
                TrackInteractiveMemberMutation<EdgeCollider2DComponent>(
                    undoService, "Edit EdgeCollider2D Friction", selectedEntity, &EdgeCollider2DComponent::Friction, edgeCollider2D->Friction);
                ImGui::TextUnformatted("Restitution");
                ImGui::DragFloat("##EdgeColliderRestitution", &edgeCollider2D->Restitution, 0.01f, 0.0f, 1.0f);
                TrackInteractiveMemberMutation<EdgeCollider2DComponent>(
                    undoService, "Edit EdgeCollider2D Restitution", selectedEntity, &EdgeCollider2DComponent::Restitution, edgeCollider2D->Restitution);
                ImGui::TextUnformatted("Is Sensor");
                ImGui::Checkbox("##EdgeColliderIsSensor", &edgeCollider2D->IsSensor);
                TrackInteractiveMemberMutation<EdgeCollider2DComponent>(
                    undoService, "Edit EdgeCollider2D Sensor", selectedEntity, &EdgeCollider2DComponent::IsSensor, edgeCollider2D->IsSensor);
                ImGui::TextUnformatted("Layer Bits");
                ImGui::InputScalar("##EdgeColliderLayerBits", ImGuiDataType_U64, &edgeCollider2D->CollisionLayer);
                TrackInteractiveMemberMutation<EdgeCollider2DComponent>(
                    undoService, "Edit EdgeCollider2D Layer", selectedEntity, &EdgeCollider2DComponent::CollisionLayer, edgeCollider2D->CollisionLayer);
                ImGui::TextUnformatted("Mask Bits");
                ImGui::InputScalar("##EdgeColliderMaskBits", ImGuiDataType_U64, &edgeCollider2D->CollisionMask);
                TrackInteractiveMemberMutation<EdgeCollider2DComponent>(
                    undoService, "Edit EdgeCollider2D Mask", selectedEntity, &EdgeCollider2DComponent::CollisionMask, edgeCollider2D->CollisionMask);

                ImGui::TreePop();
            }
        }

        if (ShouldDrawInspectorSection(onlySectionKey, "CapsuleCollider2D") && (registry.try_get<CapsuleCollider2DComponent>(selectedEntity) != nullptr))
        {
            auto* capsuleCollider2D = registry.try_get<CapsuleCollider2DComponent>(selectedEntity);
            const bool capsuleColliderOpen = BeginInspectorSectionHeader("Capsule Collider 2D", "CapsuleCollider2DComponentOptions", "...##CapsuleCollider2DComponentOptionsButton");
            if (orderedSectionKeys)
                (void)HandleSectionDragDrop("CapsuleCollider2D", *orderedSectionKeys, "Capsule Collider 2D");

            if (ImGui::BeginPopup("CapsuleCollider2DComponentOptions"))
            {
                if (ImGui::MenuItem("Remove Component"))
                    pendingRemovals.RemoveCapsuleCollider2DComponent = true;
                ImGui::EndPopup();
            }

            if (capsuleColliderOpen)
            {
                ImGui::TextUnformatted("Offset");
                EditorPanelStyle::AxisVectorDragState capsuleOffsetInteractionState{};
                EditorPanelStyle::DragFloatNWithAxisLabels("##CapsuleColliderOffset", &capsuleCollider2D->Offset.x, 2, 0.01f, 0.0f, 0.0f, "%.3f", 0, &capsuleOffsetInteractionState);
                TrackInteractiveVectorMemberMutation<CapsuleCollider2DComponent>(
                    undoService, "Edit CapsuleCollider2D Offset", capsuleOffsetInteractionState, selectedEntity, &CapsuleCollider2DComponent::Offset, capsuleCollider2D->Offset);
                ImGui::TextUnformatted("Size");
                EditorPanelStyle::AxisVectorDragState capsuleSizeInteractionState{};
                EditorPanelStyle::DragFloatNWithAxisLabels("##CapsuleColliderSize", &capsuleCollider2D->Size.x, 2, 0.01f, 0.001f, 1000.0f, "%.3f", 0, &capsuleSizeInteractionState);
                capsuleCollider2D->Size = glm::max(capsuleCollider2D->Size, glm::vec2(0.001f));
                TrackInteractiveVectorMemberMutation<CapsuleCollider2DComponent>(
                    undoService, "Edit CapsuleCollider2D Size", capsuleSizeInteractionState, selectedEntity, &CapsuleCollider2DComponent::Size, capsuleCollider2D->Size);
                int capsuleDirection = capsuleCollider2D->Direction == CapsuleCollider2DComponent::Orientation::Horizontal ? 1 : 0;
                const char* capsuleDirectionNames[] = { "Vertical", "Horizontal" };
                ImGui::TextUnformatted("Direction");
                if (ImGui::Combo("##CapsuleColliderDirection", &capsuleDirection, capsuleDirectionNames, 2))
                {
                    capsuleCollider2D->Direction = capsuleDirection == 1
                        ? CapsuleCollider2DComponent::Orientation::Horizontal
                        : CapsuleCollider2DComponent::Orientation::Vertical;
                }
                TrackInteractiveMemberMutation<CapsuleCollider2DComponent>(
                    undoService, "Edit CapsuleCollider2D Direction", selectedEntity, &CapsuleCollider2DComponent::Direction, capsuleCollider2D->Direction);
                ImGui::TextUnformatted("Density");
                ImGui::DragFloat("##CapsuleColliderDensity", &capsuleCollider2D->Density, 0.01f, 0.0f, 1000.0f);
                TrackInteractiveMemberMutation<CapsuleCollider2DComponent>(
                    undoService, "Edit CapsuleCollider2D Density", selectedEntity, &CapsuleCollider2DComponent::Density, capsuleCollider2D->Density);
                ImGui::TextUnformatted("Friction");
                ImGui::DragFloat("##CapsuleColliderFriction", &capsuleCollider2D->Friction, 0.01f, 0.0f, 1.0f);
                TrackInteractiveMemberMutation<CapsuleCollider2DComponent>(
                    undoService, "Edit CapsuleCollider2D Friction", selectedEntity, &CapsuleCollider2DComponent::Friction, capsuleCollider2D->Friction);
                ImGui::TextUnformatted("Restitution");
                ImGui::DragFloat("##CapsuleColliderRestitution", &capsuleCollider2D->Restitution, 0.01f, 0.0f, 1.0f);
                TrackInteractiveMemberMutation<CapsuleCollider2DComponent>(
                    undoService, "Edit CapsuleCollider2D Restitution", selectedEntity, &CapsuleCollider2DComponent::Restitution, capsuleCollider2D->Restitution);
                ImGui::TextUnformatted("Is Sensor");
                ImGui::Checkbox("##CapsuleColliderIsSensor", &capsuleCollider2D->IsSensor);
                TrackInteractiveMemberMutation<CapsuleCollider2DComponent>(
                    undoService, "Edit CapsuleCollider2D Sensor", selectedEntity, &CapsuleCollider2DComponent::IsSensor, capsuleCollider2D->IsSensor);
                ImGui::TextUnformatted("Layer Bits");
                ImGui::InputScalar("##CapsuleColliderLayerBits", ImGuiDataType_U64, &capsuleCollider2D->CollisionLayer);
                TrackInteractiveMemberMutation<CapsuleCollider2DComponent>(
                    undoService, "Edit CapsuleCollider2D Layer", selectedEntity, &CapsuleCollider2DComponent::CollisionLayer, capsuleCollider2D->CollisionLayer);
                ImGui::TextUnformatted("Mask Bits");
                ImGui::InputScalar("##CapsuleColliderMaskBits", ImGuiDataType_U64, &capsuleCollider2D->CollisionMask);
                TrackInteractiveMemberMutation<CapsuleCollider2DComponent>(
                    undoService, "Edit CapsuleCollider2D Mask", selectedEntity, &CapsuleCollider2DComponent::CollisionMask, capsuleCollider2D->CollisionMask);

                ImGui::TreePop();
            }
        }

        if (ShouldDrawInspectorSection(onlySectionKey, "Joint2D") && (registry.try_get<Joint2DComponent>(selectedEntity) != nullptr))
        {
            auto* joint2D = registry.try_get<Joint2DComponent>(selectedEntity);
            const bool jointOpen = BeginInspectorSectionHeader("Joint 2D", "Joint2DComponentOptions", "...##Joint2DComponentOptionsButton");
            if (orderedSectionKeys)
                (void)HandleSectionDragDrop("Joint2D", *orderedSectionKeys, "Joint 2D");

            if (ImGui::BeginPopup("Joint2DComponentOptions"))
            {
                if (ImGui::MenuItem("Remove Component"))
                    pendingRemovals.RemoveJoint2DComponent = true;
                ImGui::EndPopup();
            }

            if (jointOpen)
            {
                int jointTypeIndex = static_cast<int>(joint2D->Type);
                const char* jointTypeNames[] = { "Distance", "Revolute", "Prismatic" };
                ImGui::TextUnformatted("Type");
                if (ImGui::Combo("##Joint2DType", &jointTypeIndex, jointTypeNames, 3))
                    joint2D->Type = static_cast<Joint2DComponent::JointType>(jointTypeIndex);
                TrackInteractiveMemberMutation<Joint2DComponent>(
                    undoService, "Edit Joint2D Type", selectedEntity, &Joint2DComponent::Type, joint2D->Type);

                int connectedEntityId = (joint2D->ConnectedEntity == entt::null) ? -1 : static_cast<int>(joint2D->ConnectedEntity);
                ImGui::TextUnformatted("Connected Entity ID");
                if (ImGui::InputInt("##Joint2DConnectedEntityId", &connectedEntityId))
                    joint2D->ConnectedEntity = connectedEntityId >= 0 ? static_cast<entt::entity>(connectedEntityId) : entt::null;
                TrackInteractiveMemberMutation<Joint2DComponent>(
                    undoService, "Edit Joint2D Connected Entity", selectedEntity, &Joint2DComponent::ConnectedEntity, joint2D->ConnectedEntity);

                ImGui::TextUnformatted("Collide Connected");
                ImGui::Checkbox("##Joint2DCollideConnected", &joint2D->CollideConnected);
                TrackInteractiveMemberMutation<Joint2DComponent>(
                    undoService, "Edit Joint2D Collide Connected", selectedEntity, &Joint2DComponent::CollideConnected, joint2D->CollideConnected);
                ImGui::TextUnformatted("Anchor A");
                EditorPanelStyle::AxisVectorDragState anchorAInteractionState{};
                EditorPanelStyle::DragFloatNWithAxisLabels("##Joint2DAnchorA", &joint2D->AnchorA.x, 2, 0.01f, 0.0f, 0.0f, "%.3f", 0, &anchorAInteractionState);
                TrackInteractiveVectorMemberMutation<Joint2DComponent>(
                    undoService, "Edit Joint2D Anchor A", anchorAInteractionState, selectedEntity, &Joint2DComponent::AnchorA, joint2D->AnchorA);
                ImGui::TextUnformatted("Anchor B");
                EditorPanelStyle::AxisVectorDragState anchorBInteractionState{};
                EditorPanelStyle::DragFloatNWithAxisLabels("##Joint2DAnchorB", &joint2D->AnchorB.x, 2, 0.01f, 0.0f, 0.0f, "%.3f", 0, &anchorBInteractionState);
                TrackInteractiveVectorMemberMutation<Joint2DComponent>(
                    undoService, "Edit Joint2D Anchor B", anchorBInteractionState, selectedEntity, &Joint2DComponent::AnchorB, joint2D->AnchorB);
                ImGui::TextUnformatted("Axis");
                EditorPanelStyle::AxisVectorDragState axisInteractionState{};
                EditorPanelStyle::DragFloatNWithAxisLabels("##Joint2DAxis", &joint2D->Axis.x, 2, 0.01f, 0.0f, 0.0f, "%.3f", 0, &axisInteractionState);
                TrackInteractiveVectorMemberMutation<Joint2DComponent>(
                    undoService, "Edit Joint2D Axis", axisInteractionState, selectedEntity, &Joint2DComponent::Axis, joint2D->Axis);
                ImGui::TextUnformatted("Enable Limit");
                ImGui::Checkbox("##Joint2DEnableLimit", &joint2D->EnableLimit);
                TrackInteractiveMemberMutation<Joint2DComponent>(
                    undoService, "Edit Joint2D Enable Limit", selectedEntity, &Joint2DComponent::EnableLimit, joint2D->EnableLimit);
                ImGui::TextUnformatted("Limits");
                EditorPanelStyle::AxisVectorDragState limitsInteractionState{};
                EditorPanelStyle::DragFloatNWithAxisLabels("##Joint2DLimits", &joint2D->Limits.x, 2, 0.01f, 0.0f, 0.0f, "%.3f", 0, &limitsInteractionState);
                TrackInteractiveVectorMemberMutation<Joint2DComponent>(
                    undoService, "Edit Joint2D Limits", limitsInteractionState, selectedEntity, &Joint2DComponent::Limits, joint2D->Limits);
                ImGui::TextUnformatted("Enable Motor");
                ImGui::Checkbox("##Joint2DEnableMotor", &joint2D->EnableMotor);
                TrackInteractiveMemberMutation<Joint2DComponent>(
                    undoService, "Edit Joint2D Enable Motor", selectedEntity, &Joint2DComponent::EnableMotor, joint2D->EnableMotor);
                ImGui::TextUnformatted("Motor Speed");
                ImGui::DragFloat("##Joint2DMotorSpeed", &joint2D->MotorSpeed, 0.01f);
                TrackInteractiveMemberMutation<Joint2DComponent>(
                    undoService, "Edit Joint2D Motor Speed", selectedEntity, &Joint2DComponent::MotorSpeed, joint2D->MotorSpeed);
                ImGui::TextUnformatted("Max Motor Force/Torque");
                ImGui::DragFloat("##Joint2DMaxMotorForceOrTorque", &joint2D->MaxMotorForceOrTorque, 0.1f, 0.0f, 100000.0f);
                TrackInteractiveMemberMutation<Joint2DComponent>(
                    undoService, "Edit Joint2D Max Motor", selectedEntity, &Joint2DComponent::MaxMotorForceOrTorque, joint2D->MaxMotorForceOrTorque);
                ImGui::TextUnformatted("Enable Spring");
                ImGui::Checkbox("##Joint2DEnableSpring", &joint2D->EnableSpring);
                TrackInteractiveMemberMutation<Joint2DComponent>(
                    undoService, "Edit Joint2D Enable Spring", selectedEntity, &Joint2DComponent::EnableSpring, joint2D->EnableSpring);
                ImGui::TextUnformatted("Hertz");
                ImGui::DragFloat("##Joint2DHertz", &joint2D->Hertz, 0.1f, 0.0f, 1000.0f);
                TrackInteractiveMemberMutation<Joint2DComponent>(
                    undoService, "Edit Joint2D Hertz", selectedEntity, &Joint2DComponent::Hertz, joint2D->Hertz);
                ImGui::TextUnformatted("Damping Ratio");
                ImGui::DragFloat("##Joint2DDampingRatio", &joint2D->DampingRatio, 0.01f, 0.0f, 10.0f);
                TrackInteractiveMemberMutation<Joint2DComponent>(
                    undoService, "Edit Joint2D Damping", selectedEntity, &Joint2DComponent::DampingRatio, joint2D->DampingRatio);

                ImGui::TreePop();
            }
        }
    }
}
