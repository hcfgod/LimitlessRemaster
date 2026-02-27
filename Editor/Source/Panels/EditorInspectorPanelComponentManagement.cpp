#include "EditorInspectorPanelComponentManagement.h"

#include "Undo/EditorUndoService.h"
#include "Audio/AudioEngine.h"
#include "Scene/Scene.h"
#include "imgui/imgui.h"

#include <algorithm>

namespace Limitless::EditorInspectorPanel
{
    namespace
    {
        template<typename ComponentType>
        bool HasComponent(const entt::registry& registry, entt::entity entity)
        {
            return registry.all_of<ComponentType>(entity);
        }

        void ClearPrimaryFlagFromOtherCameras(entt::registry& registry, entt::entity currentEntity)
        {
            auto view = registry.view<CameraComponent>();
            for (entt::entity entity : view)
            {
                if (entity == currentEntity)
                    continue;

                auto& otherCamera = view.get<CameraComponent>(entity);
                otherCamera.IsPrimary = false;
            }
        }
    }

    void DrawAddComponentPopup(Scene* scene,
                               entt::registry& registry,
                               entt::entity selectedEntity,
                               EditorUndoService* undoService)
    {
        if (ImGui::Button("Add Component", ImVec2(-1.0f, 0.0f)))
            ImGui::OpenPopup("AddComponentPopup");

        if (!ImGui::BeginPopup("AddComponentPopup"))
            return;

        if (HasComponent<CanvasComponent>(registry, selectedEntity))
            ImGui::BeginDisabled();

        if (ImGui::MenuItem("Canvas"))
        {
            if (undoService)
                (void)undoService->ExecuteSceneMutation("Add Canvas Component", [&](Scene& mutableScene) {
                    auto& mutableRegistry = mutableScene.GetRegistry();
                    mutableRegistry.emplace<CanvasComponent>(selectedEntity);
                    if (!mutableRegistry.all_of<RectTransformComponent>(selectedEntity))
                        mutableRegistry.emplace<RectTransformComponent>(selectedEntity);
                    return true;
                });
            else {
                registry.emplace<CanvasComponent>(selectedEntity);
                if (!registry.all_of<RectTransformComponent>(selectedEntity))
                    registry.emplace<RectTransformComponent>(selectedEntity);
            }
        }

        if (HasComponent<CanvasComponent>(registry, selectedEntity))
            ImGui::EndDisabled();

        if (HasComponent<RectTransformComponent>(registry, selectedEntity))
            ImGui::BeginDisabled();

        if (ImGui::MenuItem("RectTransform"))
        {
            if (undoService)
                (void)undoService->ExecuteSceneMutation("Add RectTransform Component", [&](Scene& mutableScene) {
                    mutableScene.GetRegistry().emplace<RectTransformComponent>(selectedEntity);
                    return true;
                });
            else
                registry.emplace<RectTransformComponent>(selectedEntity);
        }

        if (HasComponent<RectTransformComponent>(registry, selectedEntity))
            ImGui::EndDisabled();

        if (HasComponent<UIImageComponent>(registry, selectedEntity))
            ImGui::BeginDisabled();

        if (ImGui::MenuItem("UI Image"))
        {
            if (undoService)
                (void)undoService->ExecuteSceneMutation("Add UIImage Component", [&](Scene& mutableScene) {
                    auto& mutableRegistry = mutableScene.GetRegistry();
                    mutableRegistry.emplace<UIImageComponent>(selectedEntity);
                    if (!mutableRegistry.all_of<RectTransformComponent>(selectedEntity))
                        mutableRegistry.emplace<RectTransformComponent>(selectedEntity);
                    if (!mutableRegistry.all_of<SpriteComponent>(selectedEntity))
                        mutableRegistry.emplace<SpriteComponent>(selectedEntity);
                    return true;
                });
            else {
                registry.emplace<UIImageComponent>(selectedEntity);
                if (!registry.all_of<RectTransformComponent>(selectedEntity))
                    registry.emplace<RectTransformComponent>(selectedEntity);
                if (!registry.all_of<SpriteComponent>(selectedEntity))
                    registry.emplace<SpriteComponent>(selectedEntity);
            }
        }

        if (HasComponent<UIImageComponent>(registry, selectedEntity))
            ImGui::EndDisabled();

        if (HasComponent<UIPanelComponent>(registry, selectedEntity))
            ImGui::BeginDisabled();

        if (ImGui::MenuItem("UI Panel"))
        {
            if (undoService)
                (void)undoService->ExecuteSceneMutation("Add UIPanel Component", [&](Scene& mutableScene) {
                    auto& mutableRegistry = mutableScene.GetRegistry();
                    auto& panel = mutableRegistry.emplace<UIPanelComponent>(selectedEntity);
                    if (!mutableRegistry.all_of<RectTransformComponent>(selectedEntity))
                        mutableRegistry.emplace<RectTransformComponent>(selectedEntity);
                    if (!mutableRegistry.all_of<SpriteComponent>(selectedEntity))
                    {
                        auto& sprite = mutableRegistry.emplace<SpriteComponent>(selectedEntity);
                        sprite.Color = panel.BackgroundColor;
                    }
                    return true;
                });
            else
            {
                auto& panel = registry.emplace<UIPanelComponent>(selectedEntity);
                if (!registry.all_of<RectTransformComponent>(selectedEntity))
                    registry.emplace<RectTransformComponent>(selectedEntity);
                if (!registry.all_of<SpriteComponent>(selectedEntity))
                {
                    auto& sprite = registry.emplace<SpriteComponent>(selectedEntity);
                    sprite.Color = panel.BackgroundColor;
                }
            }
        }

        if (HasComponent<UIPanelComponent>(registry, selectedEntity))
            ImGui::EndDisabled();

        if (HasComponent<UITextComponent>(registry, selectedEntity))
            ImGui::BeginDisabled();

        if (ImGui::MenuItem("UI Text"))
        {
            if (undoService)
                (void)undoService->ExecuteSceneMutation("Add UIText Component", [&](Scene& mutableScene) {
                    auto& mutableRegistry = mutableScene.GetRegistry();
                    auto& uiText = mutableRegistry.emplace<UITextComponent>(selectedEntity);
                    uiText.FontFilePath = "Assets/Fonts/Default.ttf";
                    if (!mutableRegistry.all_of<RectTransformComponent>(selectedEntity))
                        mutableRegistry.emplace<RectTransformComponent>(selectedEntity);
                    return true;
                });
            else
            {
                auto& uiText = registry.emplace<UITextComponent>(selectedEntity);
                uiText.FontFilePath = "Assets/Fonts/Default.ttf";
                if (!registry.all_of<RectTransformComponent>(selectedEntity))
                    registry.emplace<RectTransformComponent>(selectedEntity);
            }
        }

        if (HasComponent<UITextComponent>(registry, selectedEntity))
            ImGui::EndDisabled();

        if (HasComponent<UIButtonComponent>(registry, selectedEntity))
            ImGui::BeginDisabled();

        if (ImGui::MenuItem("UI Button"))
        {
            if (undoService)
                (void)undoService->ExecuteSceneMutation("Add UIButton Component", [&](Scene& mutableScene) {
                    auto& mutableRegistry = mutableScene.GetRegistry();
                    auto& button = mutableRegistry.emplace<UIButtonComponent>(selectedEntity);
                    if (!mutableRegistry.all_of<UIImageComponent>(selectedEntity))
                        mutableRegistry.emplace<UIImageComponent>(selectedEntity);
                    if (!mutableRegistry.all_of<RectTransformComponent>(selectedEntity))
                        mutableRegistry.emplace<RectTransformComponent>(selectedEntity);
                    if (!mutableRegistry.all_of<SpriteComponent>(selectedEntity))
                    {
                        auto& sprite = mutableRegistry.emplace<SpriteComponent>(selectedEntity);
                        sprite.Color = glm::vec4(0.82f, 0.82f, 0.82f, 1.0f);
                    }
                    if (auto* sprite = mutableRegistry.try_get<SpriteComponent>(selectedEntity))
                    {
                        button.NormalColor = sprite->Color;
                        button.HoveredColor = glm::clamp(sprite->Color * glm::vec4(1.12f, 1.12f, 1.12f, 1.0f), glm::vec4(0.0f), glm::vec4(1.0f));
                        button.PressedColor = glm::clamp(sprite->Color * glm::vec4(0.85f, 0.85f, 0.85f, 1.0f), glm::vec4(0.0f), glm::vec4(1.0f));
                        button.DisabledColor = glm::clamp(sprite->Color * glm::vec4(0.55f, 0.55f, 0.55f, 1.0f), glm::vec4(0.0f), glm::vec4(1.0f));
                    }
                    return true;
                });
            else {
                auto& button = registry.emplace<UIButtonComponent>(selectedEntity);
                if (!registry.all_of<UIImageComponent>(selectedEntity))
                    registry.emplace<UIImageComponent>(selectedEntity);
                if (!registry.all_of<RectTransformComponent>(selectedEntity))
                    registry.emplace<RectTransformComponent>(selectedEntity);
                if (!registry.all_of<SpriteComponent>(selectedEntity))
                {
                    auto& sprite = registry.emplace<SpriteComponent>(selectedEntity);
                    sprite.Color = glm::vec4(0.82f, 0.82f, 0.82f, 1.0f);
                }
                if (auto* sprite = registry.try_get<SpriteComponent>(selectedEntity))
                {
                    button.NormalColor = sprite->Color;
                    button.HoveredColor = glm::clamp(sprite->Color * glm::vec4(1.12f, 1.12f, 1.12f, 1.0f), glm::vec4(0.0f), glm::vec4(1.0f));
                    button.PressedColor = glm::clamp(sprite->Color * glm::vec4(0.85f, 0.85f, 0.85f, 1.0f), glm::vec4(0.0f), glm::vec4(1.0f));
                    button.DisabledColor = glm::clamp(sprite->Color * glm::vec4(0.55f, 0.55f, 0.55f, 1.0f), glm::vec4(0.0f), glm::vec4(1.0f));
                }
            }
        }

        if (HasComponent<UIButtonComponent>(registry, selectedEntity))
            ImGui::EndDisabled();

        if (HasComponent<UISliderComponent>(registry, selectedEntity))
            ImGui::BeginDisabled();

        if (ImGui::MenuItem("UI Slider"))
        {
            if (undoService)
                (void)undoService->ExecuteSceneMutation("Add UISlider Component", [&](Scene& mutableScene) {
                    auto& mutableRegistry = mutableScene.GetRegistry();
                    auto& slider = mutableRegistry.emplace<UISliderComponent>(selectedEntity);
                    slider.Value = std::clamp(0.5f, slider.MinValue, std::max(slider.MinValue, slider.MaxValue));
                    if (!mutableRegistry.all_of<UIImageComponent>(selectedEntity))
                        mutableRegistry.emplace<UIImageComponent>(selectedEntity);
                    if (!mutableRegistry.all_of<RectTransformComponent>(selectedEntity))
                        mutableRegistry.emplace<RectTransformComponent>(selectedEntity);

                    const float sliderRange = std::max(0.0001f, slider.MaxValue - slider.MinValue);
                    const float sliderNormalized = std::clamp((slider.Value - slider.MinValue) / sliderRange, 0.0f, 1.0f);
                    auto ensureSliderVisualChild = [&](const char* childName,
                                                       const glm::vec4& defaultColor,
                                                       int32_t siblingOrder,
                                                       auto&& initializeRectTransform) {
                        entt::entity childEntity = entt::null;
                        auto childView = mutableRegistry.view<TagComponent, HierarchyComponent>();
                        for (entt::entity candidate : childView)
                        {
                            const auto& hierarchy = childView.get<HierarchyComponent>(candidate);
                            if (hierarchy.Parent != selectedEntity)
                                continue;
                            const auto& tag = childView.get<TagComponent>(candidate);
                            if (tag.Tag == childName)
                            {
                                childEntity = candidate;
                                break;
                            }
                        }

                        bool created = false;
                        if (childEntity == entt::null)
                        {
                            childEntity = mutableScene.CreateEntity(childName);
                            mutableScene.SetParent(childEntity, selectedEntity);
                            created = true;
                        }

                        if (auto* hierarchy = mutableRegistry.try_get<HierarchyComponent>(childEntity))
                            hierarchy->SiblingOrder = siblingOrder;
                        if (!mutableRegistry.all_of<RectTransformComponent>(childEntity))
                            mutableRegistry.emplace<RectTransformComponent>(childEntity);
                        if (!mutableRegistry.all_of<UIImageComponent>(childEntity))
                            mutableRegistry.emplace<UIImageComponent>(childEntity);
                        if (!mutableRegistry.all_of<SpriteComponent>(childEntity))
                        {
                            auto& childSprite = mutableRegistry.emplace<SpriteComponent>(childEntity);
                            childSprite.Color = defaultColor;
                        }

                        if (created)
                        {
                            auto& rect = mutableRegistry.get<RectTransformComponent>(childEntity);
                            initializeRectTransform(rect);
                        }
                    };

                    ensureSliderVisualChild("Slider Background", slider.BackgroundColor, 0, [](RectTransformComponent& rect) {
                        rect.AnchorMin = glm::vec2(0.0f, 0.0f);
                        rect.AnchorMax = glm::vec2(1.0f, 1.0f);
                        rect.Pivot = glm::vec2(0.5f, 0.5f);
                        rect.SizeDelta = glm::vec2(0.0f, 0.0f);
                        rect.AnchoredPosition = glm::vec2(0.0f, 0.0f);
                    });
                    ensureSliderVisualChild("Slider Fill", slider.FillColor, 10, [sliderNormalized](RectTransformComponent& rect) {
                        rect.AnchorMin = glm::vec2(0.0f, 0.0f);
                        rect.AnchorMax = glm::vec2(sliderNormalized, 1.0f);
                        rect.Pivot = glm::vec2(0.5f, 0.5f);
                        rect.SizeDelta = glm::vec2(0.0f, 0.0f);
                        rect.AnchoredPosition = glm::vec2(0.0f, 0.0f);
                    });
                    ensureSliderVisualChild("Slider Handle", slider.HandleColor, 20, [sliderNormalized](RectTransformComponent& rect) {
                        rect.AnchorMin = glm::vec2(sliderNormalized, 0.5f);
                        rect.AnchorMax = glm::vec2(sliderNormalized, 0.5f);
                        rect.Pivot = glm::vec2(0.5f, 0.5f);
                        rect.SizeDelta = glm::vec2(16.0f, 48.0f);
                        rect.AnchoredPosition = glm::vec2(0.0f, 0.0f);
                    });
                    return true;
                });
            else {
                auto& slider = registry.emplace<UISliderComponent>(selectedEntity);
                slider.Value = std::clamp(0.5f, slider.MinValue, std::max(slider.MinValue, slider.MaxValue));
                if (!registry.all_of<UIImageComponent>(selectedEntity))
                    registry.emplace<UIImageComponent>(selectedEntity);
                if (!registry.all_of<RectTransformComponent>(selectedEntity))
                    registry.emplace<RectTransformComponent>(selectedEntity);

                const float sliderRange = std::max(0.0001f, slider.MaxValue - slider.MinValue);
                const float sliderNormalized = std::clamp((slider.Value - slider.MinValue) / sliderRange, 0.0f, 1.0f);
                auto ensureSliderVisualChild = [&](const char* childName,
                                                   const glm::vec4& defaultColor,
                                                   int32_t siblingOrder,
                                                   auto&& initializeRectTransform) {
                    entt::entity childEntity = entt::null;
                    auto childView = registry.view<TagComponent, HierarchyComponent>();
                    for (entt::entity candidate : childView)
                    {
                        const auto& hierarchy = childView.get<HierarchyComponent>(candidate);
                        if (hierarchy.Parent != selectedEntity)
                            continue;
                        const auto& tag = childView.get<TagComponent>(candidate);
                        if (tag.Tag == childName)
                        {
                            childEntity = candidate;
                            break;
                        }
                    }

                    bool created = false;
                    if (childEntity == entt::null)
                    {
                        childEntity = scene->CreateEntity(childName);
                        scene->SetParent(childEntity, selectedEntity);
                        created = true;
                    }

                    if (auto* hierarchy = registry.try_get<HierarchyComponent>(childEntity))
                        hierarchy->SiblingOrder = siblingOrder;
                    if (!registry.all_of<RectTransformComponent>(childEntity))
                        registry.emplace<RectTransformComponent>(childEntity);
                    if (!registry.all_of<UIImageComponent>(childEntity))
                        registry.emplace<UIImageComponent>(childEntity);
                    if (!registry.all_of<SpriteComponent>(childEntity))
                    {
                        auto& childSprite = registry.emplace<SpriteComponent>(childEntity);
                        childSprite.Color = defaultColor;
                    }

                    if (created)
                    {
                        auto& rect = registry.get<RectTransformComponent>(childEntity);
                        initializeRectTransform(rect);
                    }
                };

                ensureSliderVisualChild("Slider Background", slider.BackgroundColor, 0, [](RectTransformComponent& rect) {
                    rect.AnchorMin = glm::vec2(0.0f, 0.0f);
                    rect.AnchorMax = glm::vec2(1.0f, 1.0f);
                    rect.Pivot = glm::vec2(0.5f, 0.5f);
                    rect.SizeDelta = glm::vec2(0.0f, 0.0f);
                    rect.AnchoredPosition = glm::vec2(0.0f, 0.0f);
                });
                ensureSliderVisualChild("Slider Fill", slider.FillColor, 10, [sliderNormalized](RectTransformComponent& rect) {
                    rect.AnchorMin = glm::vec2(0.0f, 0.0f);
                    rect.AnchorMax = glm::vec2(sliderNormalized, 1.0f);
                    rect.Pivot = glm::vec2(0.5f, 0.5f);
                    rect.SizeDelta = glm::vec2(0.0f, 0.0f);
                    rect.AnchoredPosition = glm::vec2(0.0f, 0.0f);
                });
                ensureSliderVisualChild("Slider Handle", slider.HandleColor, 20, [sliderNormalized](RectTransformComponent& rect) {
                    rect.AnchorMin = glm::vec2(sliderNormalized, 0.5f);
                    rect.AnchorMax = glm::vec2(sliderNormalized, 0.5f);
                    rect.Pivot = glm::vec2(0.5f, 0.5f);
                    rect.SizeDelta = glm::vec2(16.0f, 48.0f);
                    rect.AnchoredPosition = glm::vec2(0.0f, 0.0f);
                });
            }
        }

        if (HasComponent<UISliderComponent>(registry, selectedEntity))
            ImGui::EndDisabled();

        if (HasComponent<SpriteComponent>(registry, selectedEntity))
            ImGui::BeginDisabled();

        if (ImGui::MenuItem("Sprite Component"))
        {
            if (undoService)
                (void)undoService->ExecuteSceneMutation("Add Sprite Component", [&](Scene& mutableScene) {
                    mutableScene.GetRegistry().emplace<SpriteComponent>(selectedEntity);
                    return true;
                });
            else
                registry.emplace<SpriteComponent>(selectedEntity);
        }

        if (HasComponent<SpriteComponent>(registry, selectedEntity))
            ImGui::EndDisabled();

        if (HasComponent<CameraComponent>(registry, selectedEntity))
            ImGui::BeginDisabled();

        if (ImGui::MenuItem("Camera Component"))
        {
            if (undoService)
            {
                (void)undoService->ExecuteSceneMutation("Add Camera Component", [&](Scene& mutableScene) {
                    auto& mutableRegistry = mutableScene.GetRegistry();
                    auto& camera = mutableRegistry.emplace<CameraComponent>(selectedEntity);
                    camera.IsPrimary = true;
                    ClearPrimaryFlagFromOtherCameras(mutableRegistry, selectedEntity);
                    return true;
                });
            }
            else
            {
                auto& camera = registry.emplace<CameraComponent>(selectedEntity);
                camera.IsPrimary = true;
                ClearPrimaryFlagFromOtherCameras(registry, selectedEntity);
            }
        }

        if (HasComponent<CameraComponent>(registry, selectedEntity))
            ImGui::EndDisabled();

        if (HasComponent<AudioListener2DComponent>(registry, selectedEntity))
            ImGui::BeginDisabled();

        if (ImGui::MenuItem("Audio Listener 2D"))
        {
            if (undoService)
                (void)undoService->ExecuteSceneMutation("Add Audio Listener 2D Component", [&](Scene& mutableScene) {
                    mutableScene.GetRegistry().emplace<AudioListener2DComponent>(selectedEntity);
                    return true;
                });
            else
                registry.emplace<AudioListener2DComponent>(selectedEntity);
        }

        if (HasComponent<AudioListener2DComponent>(registry, selectedEntity))
            ImGui::EndDisabled();

        if (HasComponent<AudioSourceComponent>(registry, selectedEntity))
            ImGui::BeginDisabled();

        if (ImGui::MenuItem("Audio Source"))
        {
            if (undoService)
                (void)undoService->ExecuteSceneMutation("Add Audio Source Component", [&](Scene& mutableScene) {
                    mutableScene.GetRegistry().emplace<AudioSourceComponent>(selectedEntity);
                    return true;
                });
            else
                registry.emplace<AudioSourceComponent>(selectedEntity);
        }

        if (HasComponent<AudioSourceComponent>(registry, selectedEntity))
            ImGui::EndDisabled();

        if (HasComponent<NativeScriptComponent>(registry, selectedEntity))
            ImGui::BeginDisabled();

        if (ImGui::MenuItem("Native Script"))
        {
            if (undoService)
                (void)undoService->ExecuteSceneMutation("Add Native Script Component", [&](Scene& mutableScene) {
                    mutableScene.GetRegistry().emplace<NativeScriptComponent>(selectedEntity);
                    return true;
                });
            else
                registry.emplace<NativeScriptComponent>(selectedEntity);
        }

        if (HasComponent<NativeScriptComponent>(registry, selectedEntity))
            ImGui::EndDisabled();

        if (HasComponent<AnimatorComponent>(registry, selectedEntity))
            ImGui::BeginDisabled();

        if (ImGui::MenuItem("Animator"))
        {
            if (undoService)
                (void)undoService->ExecuteSceneMutation("Add Animator Component", [&](Scene& mutableScene) {
                    mutableScene.GetRegistry().emplace<AnimatorComponent>(selectedEntity);
                    return true;
                });
            else
                registry.emplace<AnimatorComponent>(selectedEntity);
        }

        if (HasComponent<AnimatorComponent>(registry, selectedEntity))
            ImGui::EndDisabled();

        if (HasComponent<AnimationEventReceiverComponent>(registry, selectedEntity))
            ImGui::BeginDisabled();

        if (ImGui::MenuItem("Animation Event Receiver"))
        {
            if (undoService)
                (void)undoService->ExecuteSceneMutation("Add Animation Event Receiver Component", [&](Scene& mutableScene) {
                    mutableScene.GetRegistry().emplace<AnimationEventReceiverComponent>(selectedEntity);
                    return true;
                });
            else
                registry.emplace<AnimationEventReceiverComponent>(selectedEntity);
        }

        if (HasComponent<AnimationEventReceiverComponent>(registry, selectedEntity))
            ImGui::EndDisabled();

        if (HasComponent<Rigidbody2DComponent>(registry, selectedEntity))
            ImGui::BeginDisabled();

        if (ImGui::MenuItem("Rigidbody 2D"))
        {
            if (undoService)
                (void)undoService->ExecuteSceneMutation("Add Rigidbody2D Component", [&](Scene& mutableScene) {
                    mutableScene.GetRegistry().emplace<Rigidbody2DComponent>(selectedEntity);
                    return true;
                });
            else
                registry.emplace<Rigidbody2DComponent>(selectedEntity);
        }

        if (HasComponent<Rigidbody2DComponent>(registry, selectedEntity))
            ImGui::EndDisabled();

        if (HasComponent<BoxCollider2DComponent>(registry, selectedEntity))
            ImGui::BeginDisabled();

        if (ImGui::MenuItem("Box Collider 2D"))
        {
            if (undoService)
                (void)undoService->ExecuteSceneMutation("Add BoxCollider2D Component", [&](Scene& mutableScene) {
                    mutableScene.GetRegistry().emplace<BoxCollider2DComponent>(selectedEntity);
                    return true;
                });
            else
                registry.emplace<BoxCollider2DComponent>(selectedEntity);
        }

        if (HasComponent<BoxCollider2DComponent>(registry, selectedEntity))
            ImGui::EndDisabled();

        if (HasComponent<CircleCollider2DComponent>(registry, selectedEntity))
            ImGui::BeginDisabled();

        if (ImGui::MenuItem("Circle Collider 2D"))
        {
            if (undoService)
                (void)undoService->ExecuteSceneMutation("Add CircleCollider2D Component", [&](Scene& mutableScene) {
                    mutableScene.GetRegistry().emplace<CircleCollider2DComponent>(selectedEntity);
                    return true;
                });
            else
                registry.emplace<CircleCollider2DComponent>(selectedEntity);
        }

        if (HasComponent<CircleCollider2DComponent>(registry, selectedEntity))
            ImGui::EndDisabled();

        if (HasComponent<Joint2DComponent>(registry, selectedEntity))
            ImGui::BeginDisabled();

        if (ImGui::MenuItem("Joint 2D"))
        {
            if (undoService)
                (void)undoService->ExecuteSceneMutation("Add Joint2D Component", [&](Scene& mutableScene) {
                    mutableScene.GetRegistry().emplace<Joint2DComponent>(selectedEntity);
                    return true;
                });
            else
                registry.emplace<Joint2DComponent>(selectedEntity);
        }

        if (HasComponent<Joint2DComponent>(registry, selectedEntity))
            ImGui::EndDisabled();

        if (HasComponent<DirectionalLight2DComponent>(registry, selectedEntity))
            ImGui::BeginDisabled();

        if (ImGui::MenuItem("Directional Light 2D"))
        {
            if (undoService)
                (void)undoService->ExecuteSceneMutation("Add DirectionalLight2D Component", [&](Scene& mutableScene) {
                    mutableScene.GetRegistry().emplace<DirectionalLight2DComponent>(selectedEntity);
                    return true;
                });
            else
                registry.emplace<DirectionalLight2DComponent>(selectedEntity);
        }

        if (HasComponent<DirectionalLight2DComponent>(registry, selectedEntity))
            ImGui::EndDisabled();

        if (HasComponent<PointLight2DComponent>(registry, selectedEntity))
            ImGui::BeginDisabled();

        if (ImGui::MenuItem("Point Light 2D"))
        {
            if (undoService)
                (void)undoService->ExecuteSceneMutation("Add PointLight2D Component", [&](Scene& mutableScene) {
                    mutableScene.GetRegistry().emplace<PointLight2DComponent>(selectedEntity);
                    return true;
                });
            else
                registry.emplace<PointLight2DComponent>(selectedEntity);
        }

        if (HasComponent<PointLight2DComponent>(registry, selectedEntity))
            ImGui::EndDisabled();

        if (HasComponent<ShadowOccluder2DComponent>(registry, selectedEntity))
            ImGui::BeginDisabled();

        if (ImGui::MenuItem("Shadow Occluder 2D"))
        {
            if (undoService)
                (void)undoService->ExecuteSceneMutation("Add ShadowOccluder2D Component", [&](Scene& mutableScene) {
                    mutableScene.GetRegistry().emplace<ShadowOccluder2DComponent>(selectedEntity);
                    return true;
                });
            else
                registry.emplace<ShadowOccluder2DComponent>(selectedEntity);
        }

        if (HasComponent<ShadowOccluder2DComponent>(registry, selectedEntity))
            ImGui::EndDisabled();

        if (HasComponent<Grid2DComponent>(registry, selectedEntity))
            ImGui::BeginDisabled();

        if (ImGui::MenuItem("Grid 2D"))
        {
            if (undoService)
            {
                (void)undoService->ExecuteSceneMutation("Add Grid2D Component", [&](Scene& mutableScene) {
                    mutableScene.GetRegistry().emplace<Grid2DComponent>(selectedEntity);
                    return true;
                });
            }
            else
            {
                registry.emplace<Grid2DComponent>(selectedEntity);
            }
        }

        if (HasComponent<Grid2DComponent>(registry, selectedEntity))
            ImGui::EndDisabled();

        if (HasComponent<TilemapLayerComponent>(registry, selectedEntity))
            ImGui::BeginDisabled();

        if (ImGui::MenuItem("Tilemap Layer"))
        {
            if (undoService)
            {
                (void)undoService->ExecuteSceneMutation("Add TilemapLayer Component", [&](Scene& mutableScene) {
                    auto& layer = mutableScene.GetRegistry().emplace<TilemapLayerComponent>(selectedEntity);
                    layer.EnsureStorage();
                    return true;
                });
            }
            else
            {
                auto& layer = registry.emplace<TilemapLayerComponent>(selectedEntity);
                layer.EnsureStorage();
            }
        }

        if (HasComponent<TilemapLayerComponent>(registry, selectedEntity))
            ImGui::EndDisabled();

        if (HasComponent<ParticleEmitterComponent>(registry, selectedEntity))
            ImGui::BeginDisabled();

        if (ImGui::MenuItem("Particle Emitter"))
        {
            if (undoService)
                (void)undoService->ExecuteSceneMutation("Add Particle Emitter Component", [&](Scene& mutableScene) {
                    mutableScene.GetRegistry().emplace<ParticleEmitterComponent>(selectedEntity);
                    return true;
                });
            else
                registry.emplace<ParticleEmitterComponent>(selectedEntity);
        }

        if (HasComponent<ParticleEmitterComponent>(registry, selectedEntity))
            ImGui::EndDisabled();

        ImGui::EndPopup();
    }

    void ApplyPendingEntityComponentRemovals(Scene* scene,
                                             entt::registry& registry,
                                             entt::entity selectedEntity,
                                             PendingEntityComponentRemovals& pendingRemovals,
                                             bool removeNativeScriptComponent,
                                             EditorUndoService* undoService)
    {
        (void)scene;

        if (pendingRemovals.RemoveCanvasComponent)
        {
            if (undoService)
            {
                (void)undoService->ExecuteSceneMutation("Remove Canvas Component", [&](Scene& mutableScene) {
                    mutableScene.GetRegistry().remove<CanvasComponent>(selectedEntity);
                    return true;
                });
            }
            else
            {
                registry.remove<CanvasComponent>(selectedEntity);
            }
        }

        if (pendingRemovals.RemoveRectTransformComponent)
        {
            if (undoService)
            {
                (void)undoService->ExecuteSceneMutation("Remove RectTransform Component", [&](Scene& mutableScene) {
                    mutableScene.GetRegistry().remove<RectTransformComponent>(selectedEntity);
                    return true;
                });
            }
            else
            {
                registry.remove<RectTransformComponent>(selectedEntity);
            }
        }

        if (pendingRemovals.RemoveUIImageComponent)
        {
            if (undoService)
            {
                (void)undoService->ExecuteSceneMutation("Remove UIImage Component", [&](Scene& mutableScene) {
                    mutableScene.GetRegistry().remove<UIImageComponent>(selectedEntity);
                    return true;
                });
            }
            else
            {
                registry.remove<UIImageComponent>(selectedEntity);
            }
        }

        if (pendingRemovals.RemoveUIPanelComponent)
        {
            if (undoService)
            {
                (void)undoService->ExecuteSceneMutation("Remove UIPanel Component", [&](Scene& mutableScene) {
                    mutableScene.GetRegistry().remove<UIPanelComponent>(selectedEntity);
                    return true;
                });
            }
            else
            {
                registry.remove<UIPanelComponent>(selectedEntity);
            }
        }

        if (pendingRemovals.RemoveUITextComponent)
        {
            if (undoService)
            {
                (void)undoService->ExecuteSceneMutation("Remove UIText Component", [&](Scene& mutableScene) {
                    mutableScene.GetRegistry().remove<UITextComponent>(selectedEntity);
                    return true;
                });
            }
            else
            {
                registry.remove<UITextComponent>(selectedEntity);
            }
        }

        if (pendingRemovals.RemoveUIButtonComponent)
        {
            if (undoService)
            {
                (void)undoService->ExecuteSceneMutation("Remove UIButton Component", [&](Scene& mutableScene) {
                    mutableScene.GetRegistry().remove<UIButtonComponent>(selectedEntity);
                    return true;
                });
            }
            else
            {
                registry.remove<UIButtonComponent>(selectedEntity);
            }
        }

        if (pendingRemovals.RemoveUISliderComponent)
        {
            if (undoService)
            {
                (void)undoService->ExecuteSceneMutation("Remove UISlider Component", [&](Scene& mutableScene) {
                    mutableScene.GetRegistry().remove<UISliderComponent>(selectedEntity);
                    return true;
                });
            }
            else
            {
                registry.remove<UISliderComponent>(selectedEntity);
            }
        }

        if (pendingRemovals.RemoveSpriteComponent)
        {
            if (undoService)
            {
                (void)undoService->ExecuteSceneMutation("Remove Sprite Component", [&](Scene& mutableScene) {
                    auto& mutableRegistry = mutableScene.GetRegistry();
                    mutableRegistry.remove<SpriteComponent>(selectedEntity);
                    if (mutableRegistry.all_of<MaterialComponent>(selectedEntity))
                        mutableRegistry.remove<MaterialComponent>(selectedEntity);
                    return true;
                });
            }
            else
            {
                registry.remove<SpriteComponent>(selectedEntity);
                if (registry.all_of<MaterialComponent>(selectedEntity))
                    pendingRemovals.RemoveMaterialComponent = true;
            }
        }

        if (pendingRemovals.RemoveMaterialComponent)
        {
            if (undoService)
            {
                (void)undoService->ExecuteSceneMutation("Remove Material Component", [&](Scene& mutableScene) {
                    mutableScene.GetRegistry().remove<MaterialComponent>(selectedEntity);
                    return true;
                });
            }
            else
            {
                registry.remove<MaterialComponent>(selectedEntity);
            }
        }

        if (pendingRemovals.RemoveCameraComponent)
        {
            if (undoService)
            {
                (void)undoService->ExecuteSceneMutation("Remove Camera Component", [&](Scene& mutableScene) {
                    mutableScene.GetRegistry().remove<CameraComponent>(selectedEntity);
                    return true;
                });
            }
            else
            {
                registry.remove<CameraComponent>(selectedEntity);
            }
        }

        if (pendingRemovals.RemoveAudioListener2DComponent)
        {
            if (undoService)
            {
                (void)undoService->ExecuteSceneMutation("Remove Audio Listener 2D Component", [&](Scene& mutableScene) {
                    mutableScene.GetRegistry().remove<AudioListener2DComponent>(selectedEntity);
                    return true;
                });
            }
            else
            {
                registry.remove<AudioListener2DComponent>(selectedEntity);
            }
        }

        if (pendingRemovals.RemoveAudioSourceComponent)
        {
            if (auto* audioSource = registry.try_get<AudioSourceComponent>(selectedEntity))
            {
                if (audioSource->RuntimeVoiceId != 0)
                    Audio::AudioEngine::GetInstance().Stop(audioSource->RuntimeVoiceId);
            }
            if (undoService)
            {
                (void)undoService->ExecuteSceneMutation("Remove Audio Source Component", [&](Scene& mutableScene) {
                    auto& mutableRegistry = mutableScene.GetRegistry();
                    if (auto* mutableAudioSource = mutableRegistry.try_get<AudioSourceComponent>(selectedEntity))
                    {
                        if (mutableAudioSource->RuntimeVoiceId != 0)
                            Audio::AudioEngine::GetInstance().Stop(mutableAudioSource->RuntimeVoiceId);
                    }
                    mutableRegistry.remove<AudioSourceComponent>(selectedEntity);
                    return true;
                });
            }
            else
            {
                registry.remove<AudioSourceComponent>(selectedEntity);
            }
        }

        if (removeNativeScriptComponent)
        {
            if (auto* nativeScript = registry.try_get<NativeScriptComponent>(selectedEntity))
            {
                for (auto& scriptEntry : nativeScript->Scripts)
                {
                    scriptEntry.RuntimeInitialized = false;
                    scriptEntry.RuntimeInstance.reset();
                }
            }
            if (undoService)
            {
                (void)undoService->ExecuteSceneMutation("Remove Native Script Component", [&](Scene& mutableScene) {
                    auto& mutableRegistry = mutableScene.GetRegistry();
                    if (auto* mutableNativeScript = mutableRegistry.try_get<NativeScriptComponent>(selectedEntity))
                    {
                        for (auto& scriptEntry : mutableNativeScript->Scripts)
                        {
                            scriptEntry.RuntimeInitialized = false;
                            scriptEntry.RuntimeInstance.reset();
                        }
                    }
                    mutableRegistry.remove<NativeScriptComponent>(selectedEntity);
                    return true;
                });
            }
            else
            {
                registry.remove<NativeScriptComponent>(selectedEntity);
            }
        }

        if (pendingRemovals.RemoveAnimatorComponent)
        {
            if (undoService)
            {
                (void)undoService->ExecuteSceneMutation("Remove Animator Component", [&](Scene& mutableScene) {
                    mutableScene.GetRegistry().remove<AnimatorComponent>(selectedEntity);
                    return true;
                });
            }
            else
            {
                registry.remove<AnimatorComponent>(selectedEntity);
            }
        }

        if (pendingRemovals.RemoveAnimationEventReceiverComponent)
        {
            if (undoService)
            {
                (void)undoService->ExecuteSceneMutation("Remove Animation Event Receiver Component", [&](Scene& mutableScene) {
                    mutableScene.GetRegistry().remove<AnimationEventReceiverComponent>(selectedEntity);
                    return true;
                });
            }
            else
            {
                registry.remove<AnimationEventReceiverComponent>(selectedEntity);
            }
        }

        if (pendingRemovals.RemoveRigidbody2DComponent)
        {
            if (undoService)
            {
                (void)undoService->ExecuteSceneMutation("Remove Rigidbody2D Component", [&](Scene& mutableScene) {
                    mutableScene.GetRegistry().remove<Rigidbody2DComponent>(selectedEntity);
                    return true;
                });
            }
            else
            {
                registry.remove<Rigidbody2DComponent>(selectedEntity);
            }
        }

        if (pendingRemovals.RemoveBoxCollider2DComponent)
        {
            if (undoService)
            {
                (void)undoService->ExecuteSceneMutation("Remove BoxCollider2D Component", [&](Scene& mutableScene) {
                    mutableScene.GetRegistry().remove<BoxCollider2DComponent>(selectedEntity);
                    return true;
                });
            }
            else
            {
                registry.remove<BoxCollider2DComponent>(selectedEntity);
            }
        }

        if (pendingRemovals.RemoveCircleCollider2DComponent)
        {
            if (undoService)
            {
                (void)undoService->ExecuteSceneMutation("Remove CircleCollider2D Component", [&](Scene& mutableScene) {
                    mutableScene.GetRegistry().remove<CircleCollider2DComponent>(selectedEntity);
                    return true;
                });
            }
            else
            {
                registry.remove<CircleCollider2DComponent>(selectedEntity);
            }
        }

        if (pendingRemovals.RemoveJoint2DComponent)
        {
            if (undoService)
            {
                (void)undoService->ExecuteSceneMutation("Remove Joint2D Component", [&](Scene& mutableScene) {
                    mutableScene.GetRegistry().remove<Joint2DComponent>(selectedEntity);
                    return true;
                });
            }
            else
            {
                registry.remove<Joint2DComponent>(selectedEntity);
            }
        }

        if (pendingRemovals.RemoveDirectionalLight2DComponent)
        {
            if (undoService)
            {
                (void)undoService->ExecuteSceneMutation("Remove DirectionalLight2D Component", [&](Scene& mutableScene) {
                    mutableScene.GetRegistry().remove<DirectionalLight2DComponent>(selectedEntity);
                    return true;
                });
            }
            else
            {
                registry.remove<DirectionalLight2DComponent>(selectedEntity);
            }
        }

        if (pendingRemovals.RemovePointLight2DComponent)
        {
            if (undoService)
            {
                (void)undoService->ExecuteSceneMutation("Remove PointLight2D Component", [&](Scene& mutableScene) {
                    mutableScene.GetRegistry().remove<PointLight2DComponent>(selectedEntity);
                    return true;
                });
            }
            else
            {
                registry.remove<PointLight2DComponent>(selectedEntity);
            }
        }

        if (pendingRemovals.RemoveShadowOccluder2DComponent)
        {
            if (undoService)
            {
                (void)undoService->ExecuteSceneMutation("Remove ShadowOccluder2D Component", [&](Scene& mutableScene) {
                    mutableScene.GetRegistry().remove<ShadowOccluder2DComponent>(selectedEntity);
                    return true;
                });
            }
            else
            {
                registry.remove<ShadowOccluder2DComponent>(selectedEntity);
            }
        }

        if (pendingRemovals.RemoveParticleEmitterComponent)
        {
            if (undoService)
            {
                (void)undoService->ExecuteSceneMutation("Remove Particle Emitter Component", [&](Scene& mutableScene) {
                    mutableScene.GetRegistry().remove<ParticleEmitterComponent>(selectedEntity);
                    return true;
                });
            }
            else
            {
                registry.remove<ParticleEmitterComponent>(selectedEntity);
            }
        }

        if (pendingRemovals.RemoveGrid2DComponent)
        {
            if (undoService)
            {
                (void)undoService->ExecuteSceneMutation("Remove Grid2D Component", [&](Scene& mutableScene) {
                    mutableScene.GetRegistry().remove<Grid2DComponent>(selectedEntity);
                    return true;
                });
            }
            else
            {
                registry.remove<Grid2DComponent>(selectedEntity);
            }
        }

        if (pendingRemovals.RemoveTilemapLayerComponent)
        {
            if (undoService)
            {
                (void)undoService->ExecuteSceneMutation("Remove TilemapLayer Component", [&](Scene& mutableScene) {
                    mutableScene.GetRegistry().remove<TilemapLayerComponent>(selectedEntity);
                    return true;
                });
            }
            else
            {
                registry.remove<TilemapLayerComponent>(selectedEntity);
            }
        }
    }
}
