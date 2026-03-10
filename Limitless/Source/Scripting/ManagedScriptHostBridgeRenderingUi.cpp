#include "Scripting/ManagedScriptHostInternal.h"

#include <algorithm>

namespace Limitless::ManagedScriptHost
{
    using namespace Internal;

    namespace Internal
    {
        LT_MANAGED_COMPONENT_HAS(HasSpriteComponentIcall, TryGetManagedSpriteComponent);
        LT_MANAGED_COMPONENT_GET(GetSpriteTextureKeyIcall, Coral::String, TryGetManagedSpriteComponent, Coral::String::New(component->TextureKey), Coral::String::New(""));
        LT_MANAGED_COMPONENT_SET(SetSpriteTextureKeyIcall, Coral::String, TryGetManagedSpriteComponent, component->TextureKey = ToUtf8Borrowed(value););
        LT_MANAGED_COMPONENT_GET(GetSpriteColorIcall, ManagedVector4, TryGetManagedSpriteComponent, ToManagedVector4(component->Color), ManagedVector4{ 1.0f, 1.0f, 1.0f, 1.0f });
        LT_MANAGED_COMPONENT_SET(SetSpriteColorIcall, ManagedVector4, TryGetManagedSpriteComponent, component->Color = ToGlmVector4(value););
        LT_MANAGED_COMPONENT_GET(GetSpriteTilingFactorIcall, ManagedVector2, TryGetManagedSpriteComponent, ToManagedVector2(component->TilingFactor), ManagedVector2{ 1.0f, 1.0f });
        LT_MANAGED_COMPONENT_SET(SetSpriteTilingFactorIcall, ManagedVector2, TryGetManagedSpriteComponent, component->TilingFactor = ToGlmVector2(value););
        LT_MANAGED_COMPONENT_GET(GetSpriteRenderOrderIcall, int, TryGetManagedSpriteComponent, component->RenderOrder, 0);
        LT_MANAGED_COMPONENT_SET(SetSpriteRenderOrderIcall, int, TryGetManagedSpriteComponent, component->RenderOrder = value;);
        LT_MANAGED_COMPONENT_GET(GetSpriteCastShadowsIcall, bool, TryGetManagedSpriteComponent, component->CastShadows, true);
        LT_MANAGED_COMPONENT_SET(SetSpriteCastShadowsIcall, bool, TryGetManagedSpriteComponent, component->CastShadows = value;);
        LT_MANAGED_COMPONENT_GET(GetSpriteReceiveShadowsIcall, bool, TryGetManagedSpriteComponent, component->ReceiveShadows, true);
        LT_MANAGED_COMPONENT_SET(SetSpriteReceiveShadowsIcall, bool, TryGetManagedSpriteComponent, component->ReceiveShadows = value;);
        LT_MANAGED_COMPONENT_GET(GetSpriteSubSpriteIndexIcall, int, TryGetManagedSpriteComponent, component->SubSpriteIndex, -1);
        LT_MANAGED_COMPONENT_SET(SetSpriteSubSpriteIndexIcall, int, TryGetManagedSpriteComponent, component->SubSpriteIndex = value;);
        LT_MANAGED_COMPONENT_GET(GetSpriteUvMinIcall, ManagedVector2, TryGetManagedSpriteComponent, ToManagedVector2(component->UvMin), ManagedVector2{});
        LT_MANAGED_COMPONENT_SET(SetSpriteUvMinIcall, ManagedVector2, TryGetManagedSpriteComponent, component->UvMin = ToGlmVector2(value););
        LT_MANAGED_COMPONENT_GET(GetSpriteUvMaxIcall, ManagedVector2, TryGetManagedSpriteComponent, ToManagedVector2(component->UvMax), ManagedVector2{ 1.0f, 1.0f });
        LT_MANAGED_COMPONENT_SET(SetSpriteUvMaxIcall, ManagedVector2, TryGetManagedSpriteComponent, component->UvMax = ToGlmVector2(value););

        LT_MANAGED_COMPONENT_HAS(HasMaterialComponentIcall, TryGetManagedMaterialComponent);
        LT_MANAGED_COMPONENT_GET(GetMaterialKeyIcall, Coral::String, TryGetManagedMaterialComponent, Coral::String::New(component->MaterialKey), Coral::String::New(""));
        LT_MANAGED_COMPONENT_SET(SetMaterialKeyIcall, Coral::String, TryGetManagedMaterialComponent, component->MaterialKey = ToUtf8Borrowed(value););

        LT_MANAGED_COMPONENT_HAS(HasCanvasComponentIcall, TryGetManagedCanvasComponent);
        LT_MANAGED_COMPONENT_GET(GetCanvasRenderModeIcall, int, TryGetManagedCanvasComponent, static_cast<int>(component->Mode), static_cast<int>(CanvasComponent::RenderMode::ScreenSpace));
        LT_MANAGED_COMPONENT_SET(SetCanvasRenderModeIcall, int, TryGetManagedCanvasComponent, component->Mode = static_cast<CanvasComponent::RenderMode>(value););
        LT_MANAGED_COMPONENT_GET(GetCanvasSortOrderIcall, int, TryGetManagedCanvasComponent, component->SortOrder, 0);
        LT_MANAGED_COMPONENT_SET(SetCanvasSortOrderIcall, int, TryGetManagedCanvasComponent, component->SortOrder = value;);
        LT_MANAGED_COMPONENT_GET(GetCanvasReferenceResolutionIcall, ManagedVector2, TryGetManagedCanvasComponent, ToManagedVector2(component->ReferenceResolution), ManagedVector2{ 1920.0f, 1080.0f });
        LT_MANAGED_COMPONENT_SET(SetCanvasReferenceResolutionIcall, ManagedVector2, TryGetManagedCanvasComponent, component->ReferenceResolution = ToGlmVector2(value););

        LT_MANAGED_COMPONENT_HAS(HasRectTransformComponentIcall, TryGetManagedRectTransformComponent);
        LT_MANAGED_COMPONENT_GET(GetRectTransformAnchorMinIcall, ManagedVector2, TryGetManagedRectTransformComponent, ToManagedVector2(component->AnchorMin), ManagedVector2{ 0.5f, 0.5f });
        LT_MANAGED_COMPONENT_SET(SetRectTransformAnchorMinIcall, ManagedVector2, TryGetManagedRectTransformComponent, component->AnchorMin = ToGlmVector2(value););
        LT_MANAGED_COMPONENT_GET(GetRectTransformAnchorMaxIcall, ManagedVector2, TryGetManagedRectTransformComponent, ToManagedVector2(component->AnchorMax), ManagedVector2{ 0.5f, 0.5f });
        LT_MANAGED_COMPONENT_SET(SetRectTransformAnchorMaxIcall, ManagedVector2, TryGetManagedRectTransformComponent, component->AnchorMax = ToGlmVector2(value););
        LT_MANAGED_COMPONENT_GET(GetRectTransformPivotIcall, ManagedVector2, TryGetManagedRectTransformComponent, ToManagedVector2(component->Pivot), ManagedVector2{ 0.5f, 0.5f });
        LT_MANAGED_COMPONENT_SET(SetRectTransformPivotIcall, ManagedVector2, TryGetManagedRectTransformComponent, component->Pivot = ToGlmVector2(value););
        LT_MANAGED_COMPONENT_GET(GetRectTransformSizeDeltaIcall, ManagedVector2, TryGetManagedRectTransformComponent, ToManagedVector2(component->SizeDelta), ManagedVector2{ 100.0f, 40.0f });
        LT_MANAGED_COMPONENT_SET(SetRectTransformSizeDeltaIcall, ManagedVector2, TryGetManagedRectTransformComponent, component->SizeDelta = ToGlmVector2(value););
        LT_MANAGED_COMPONENT_GET(GetRectTransformAnchoredPositionIcall, ManagedVector2, TryGetManagedRectTransformComponent, ToManagedVector2(component->AnchoredPosition), ManagedVector2{});
        LT_MANAGED_COMPONENT_SET(SetRectTransformAnchoredPositionIcall, ManagedVector2, TryGetManagedRectTransformComponent, component->AnchoredPosition = ToGlmVector2(value););

        LT_MANAGED_COMPONENT_HAS(HasDirectionalLight2DComponentIcall, TryGetManagedDirectionalLight2DComponent);
        LT_MANAGED_COMPONENT_GET(GetDirectionalLight2DEnabledIcall, bool, TryGetManagedDirectionalLight2DComponent, component->Enabled, true);
        LT_MANAGED_COMPONENT_SET(SetDirectionalLight2DEnabledIcall, bool, TryGetManagedDirectionalLight2DComponent, component->Enabled = value;);
        LT_MANAGED_COMPONENT_GET(GetDirectionalLight2DColorIcall, ManagedVector3, TryGetManagedDirectionalLight2DComponent, ToManagedVector3(component->Color), ManagedVector3{ 1.0f, 1.0f, 1.0f });
        LT_MANAGED_COMPONENT_SET(SetDirectionalLight2DColorIcall, ManagedVector3, TryGetManagedDirectionalLight2DComponent, component->Color = ToGlmVector3(value););
        LT_MANAGED_COMPONENT_GET(GetDirectionalLight2DIntensityIcall, float, TryGetManagedDirectionalLight2DComponent, component->Intensity, 1.0f);
        LT_MANAGED_COMPONENT_SET(SetDirectionalLight2DIntensityIcall, float, TryGetManagedDirectionalLight2DComponent, component->Intensity = value;);
        LT_MANAGED_COMPONENT_GET(GetDirectionalLight2DUseEntityRotationIcall, bool, TryGetManagedDirectionalLight2DComponent, component->UseEntityRotation, true);
        LT_MANAGED_COMPONENT_SET(SetDirectionalLight2DUseEntityRotationIcall, bool, TryGetManagedDirectionalLight2DComponent, component->UseEntityRotation = value;);
        LT_MANAGED_COMPONENT_GET(GetDirectionalLight2DDirectionIcall, ManagedVector2, TryGetManagedDirectionalLight2DComponent, ToManagedVector2(component->Direction), ManagedVector2{ 0.0f, -1.0f });
        LT_MANAGED_COMPONENT_SET(SetDirectionalLight2DDirectionIcall, ManagedVector2, TryGetManagedDirectionalLight2DComponent, component->Direction = ToGlmVector2(value););
        LT_MANAGED_COMPONENT_GET(GetDirectionalLight2DCastShadowsIcall, bool, TryGetManagedDirectionalLight2DComponent, component->CastShadows, true);
        LT_MANAGED_COMPONENT_SET(SetDirectionalLight2DCastShadowsIcall, bool, TryGetManagedDirectionalLight2DComponent, component->CastShadows = value;);
        LT_MANAGED_COMPONENT_GET(GetDirectionalLight2DShadowStrengthIcall, float, TryGetManagedDirectionalLight2DComponent, component->ShadowStrength, 1.0f);
        LT_MANAGED_COMPONENT_SET(SetDirectionalLight2DShadowStrengthIcall, float, TryGetManagedDirectionalLight2DComponent, component->ShadowStrength = value;);
        LT_MANAGED_COMPONENT_GET(GetDirectionalLight2DShadowSoftnessIcall, float, TryGetManagedDirectionalLight2DComponent, component->ShadowSoftness, 1.0f);
        LT_MANAGED_COMPONENT_SET(SetDirectionalLight2DShadowSoftnessIcall, float, TryGetManagedDirectionalLight2DComponent, component->ShadowSoftness = value;);
        LT_MANAGED_COMPONENT_GET(GetDirectionalLight2DShadowSamplesIcall, int, TryGetManagedDirectionalLight2DComponent, component->ShadowSamples, 8);
        LT_MANAGED_COMPONENT_SET(SetDirectionalLight2DShadowSamplesIcall, int, TryGetManagedDirectionalLight2DComponent, component->ShadowSamples = value;);
        LT_MANAGED_COMPONENT_GET(GetDirectionalLight2DShadowDistanceIcall, float, TryGetManagedDirectionalLight2DComponent, component->ShadowDistance, 25.0f);
        LT_MANAGED_COMPONENT_SET(SetDirectionalLight2DShadowDistanceIcall, float, TryGetManagedDirectionalLight2DComponent, component->ShadowDistance = value;);
        LT_MANAGED_COMPONENT_GET(GetDirectionalLight2DShadowBiasIcall, float, TryGetManagedDirectionalLight2DComponent, component->ShadowBias, 0.02f);
        LT_MANAGED_COMPONENT_SET(SetDirectionalLight2DShadowBiasIcall, float, TryGetManagedDirectionalLight2DComponent, component->ShadowBias = value;);

        LT_MANAGED_COMPONENT_HAS(HasPointLight2DComponentIcall, TryGetManagedPointLight2DComponent);
        LT_MANAGED_COMPONENT_GET(GetPointLight2DEnabledIcall, bool, TryGetManagedPointLight2DComponent, component->Enabled, true);
        LT_MANAGED_COMPONENT_SET(SetPointLight2DEnabledIcall, bool, TryGetManagedPointLight2DComponent, component->Enabled = value;);
        LT_MANAGED_COMPONENT_GET(GetPointLight2DColorIcall, ManagedVector3, TryGetManagedPointLight2DComponent, ToManagedVector3(component->Color), ManagedVector3{ 1.0f, 1.0f, 1.0f });
        LT_MANAGED_COMPONENT_SET(SetPointLight2DColorIcall, ManagedVector3, TryGetManagedPointLight2DComponent, component->Color = ToGlmVector3(value););
        LT_MANAGED_COMPONENT_GET(GetPointLight2DIntensityIcall, float, TryGetManagedPointLight2DComponent, component->Intensity, 1.0f);
        LT_MANAGED_COMPONENT_SET(SetPointLight2DIntensityIcall, float, TryGetManagedPointLight2DComponent, component->Intensity = value;);
        LT_MANAGED_COMPONENT_GET(GetPointLight2DRadiusIcall, float, TryGetManagedPointLight2DComponent, component->Radius, 5.0f);
        LT_MANAGED_COMPONENT_SET(SetPointLight2DRadiusIcall, float, TryGetManagedPointLight2DComponent, component->Radius = value;);
        LT_MANAGED_COMPONENT_GET(GetPointLight2DFalloffIcall, float, TryGetManagedPointLight2DComponent, component->Falloff, 2.0f);
        LT_MANAGED_COMPONENT_SET(SetPointLight2DFalloffIcall, float, TryGetManagedPointLight2DComponent, component->Falloff = value;);
        LT_MANAGED_COMPONENT_GET(GetPointLight2DCastShadowsIcall, bool, TryGetManagedPointLight2DComponent, component->CastShadows, true);
        LT_MANAGED_COMPONENT_SET(SetPointLight2DCastShadowsIcall, bool, TryGetManagedPointLight2DComponent, component->CastShadows = value;);
        LT_MANAGED_COMPONENT_GET(GetPointLight2DShadowStrengthIcall, float, TryGetManagedPointLight2DComponent, component->ShadowStrength, 1.0f);
        LT_MANAGED_COMPONENT_SET(SetPointLight2DShadowStrengthIcall, float, TryGetManagedPointLight2DComponent, component->ShadowStrength = value;);
        LT_MANAGED_COMPONENT_GET(GetPointLight2DShadowSoftnessIcall, float, TryGetManagedPointLight2DComponent, component->ShadowSoftness, 1.0f);
        LT_MANAGED_COMPONENT_SET(SetPointLight2DShadowSoftnessIcall, float, TryGetManagedPointLight2DComponent, component->ShadowSoftness = value;);
        LT_MANAGED_COMPONENT_GET(GetPointLight2DShadowSamplesIcall, int, TryGetManagedPointLight2DComponent, component->ShadowSamples, 8);
        LT_MANAGED_COMPONENT_SET(SetPointLight2DShadowSamplesIcall, int, TryGetManagedPointLight2DComponent, component->ShadowSamples = value;);
        LT_MANAGED_COMPONENT_GET(GetPointLight2DShadowBiasIcall, float, TryGetManagedPointLight2DComponent, component->ShadowBias, 0.0015f);
        LT_MANAGED_COMPONENT_SET(SetPointLight2DShadowBiasIcall, float, TryGetManagedPointLight2DComponent, component->ShadowBias = value;);

        LT_MANAGED_COMPONENT_HAS(HasUIImageComponentIcall, TryGetManagedUIImageComponent);
        LT_MANAGED_COMPONENT_GET(GetUIImageRaycastTargetIcall, bool, TryGetManagedUIImageComponent, component->RaycastTarget, true);
        LT_MANAGED_COMPONENT_SET(SetUIImageRaycastTargetIcall, bool, TryGetManagedUIImageComponent, component->RaycastTarget = value;);

        LT_MANAGED_COMPONENT_HAS(HasUIPanelComponentIcall, TryGetManagedUIPanelComponent);
        LT_MANAGED_COMPONENT_GET(GetUIPanelBackgroundColorIcall, ManagedVector4, TryGetManagedUIPanelComponent, ToManagedVector4(component->BackgroundColor), ManagedVector4{ 0.12f, 0.12f, 0.12f, 0.9f });
        LT_MANAGED_COMPONENT_SET(SetUIPanelBackgroundColorIcall, ManagedVector4, TryGetManagedUIPanelComponent, component->BackgroundColor = ToGlmVector4(value););
        LT_MANAGED_COMPONENT_GET(GetUIPanelUseSpriteTextureIcall, bool, TryGetManagedUIPanelComponent, component->UseSpriteTexture, false);
        LT_MANAGED_COMPONENT_SET(SetUIPanelUseSpriteTextureIcall, bool, TryGetManagedUIPanelComponent, component->UseSpriteTexture = value;);
        LT_MANAGED_COMPONENT_GET(GetUIPanelRaycastTargetIcall, bool, TryGetManagedUIPanelComponent, component->RaycastTarget, false);
        LT_MANAGED_COMPONENT_SET(SetUIPanelRaycastTargetIcall, bool, TryGetManagedUIPanelComponent, component->RaycastTarget = value;);

        LT_MANAGED_COMPONENT_HAS(HasUITextComponentIcall, TryGetManagedUITextComponent);
        LT_MANAGED_COMPONENT_GET(GetUITextValueIcall, Coral::String, TryGetManagedUITextComponent, Coral::String::New(component->Text), Coral::String::New(""));
        LT_MANAGED_COMPONENT_SET(SetUITextValueIcall, Coral::String, TryGetManagedUITextComponent, component->Text = ToUtf8Borrowed(value););
        LT_MANAGED_COMPONENT_GET(GetUITextFontFilePathIcall, Coral::String, TryGetManagedUITextComponent, Coral::String::New(component->FontFilePath), Coral::String::New(""));
        LT_MANAGED_COMPONENT_SET(SetUITextFontFilePathIcall, Coral::String, TryGetManagedUITextComponent, component->FontFilePath = ToUtf8Borrowed(value););
        LT_MANAGED_COMPONENT_GET(GetUITextFontSizeIcall, float, TryGetManagedUITextComponent, component->FontSize, 32.0f);
        LT_MANAGED_COMPONENT_SET(SetUITextFontSizeIcall, float, TryGetManagedUITextComponent, component->FontSize = value;);
        LT_MANAGED_COMPONENT_GET(GetUITextColorIcall, ManagedVector4, TryGetManagedUITextComponent, ToManagedVector4(component->Color), ManagedVector4{ 1.0f, 1.0f, 1.0f, 1.0f });
        LT_MANAGED_COMPONENT_SET(SetUITextColorIcall, ManagedVector4, TryGetManagedUITextComponent, component->Color = ToGlmVector4(value););
        LT_MANAGED_COMPONENT_GET(GetUITextRaycastTargetIcall, bool, TryGetManagedUITextComponent, component->RaycastTarget, false);
        LT_MANAGED_COMPONENT_SET(SetUITextRaycastTargetIcall, bool, TryGetManagedUITextComponent, component->RaycastTarget = value;);

        LT_MANAGED_COMPONENT_HAS(HasUIButtonComponentIcall, TryGetManagedUIButtonComponent);
        LT_MANAGED_COMPONENT_GET(GetUIButtonInteractableIcall, bool, TryGetManagedUIButtonComponent, component->Interactable, true);
        LT_MANAGED_COMPONENT_SET(SetUIButtonInteractableIcall, bool, TryGetManagedUIButtonComponent, component->Interactable = value;);
        LT_MANAGED_COMPONENT_GET(GetUIButtonUseStateColorsIcall, bool, TryGetManagedUIButtonComponent, component->UseStateColors, true);
        LT_MANAGED_COMPONENT_SET(SetUIButtonUseStateColorsIcall, bool, TryGetManagedUIButtonComponent, component->UseStateColors = value;);
        LT_MANAGED_COMPONENT_GET(GetUIButtonNormalColorIcall, ManagedVector4, TryGetManagedUIButtonComponent, ToManagedVector4(component->NormalColor), ManagedVector4{ 0.82f, 0.82f, 0.82f, 1.0f });
        LT_MANAGED_COMPONENT_SET(SetUIButtonNormalColorIcall, ManagedVector4, TryGetManagedUIButtonComponent, component->NormalColor = ToGlmVector4(value););
        LT_MANAGED_COMPONENT_GET(GetUIButtonHoveredColorIcall, ManagedVector4, TryGetManagedUIButtonComponent, ToManagedVector4(component->HoveredColor), ManagedVector4{ 0.92f, 0.92f, 0.92f, 1.0f });
        LT_MANAGED_COMPONENT_SET(SetUIButtonHoveredColorIcall, ManagedVector4, TryGetManagedUIButtonComponent, component->HoveredColor = ToGlmVector4(value););
        LT_MANAGED_COMPONENT_GET(GetUIButtonPressedColorIcall, ManagedVector4, TryGetManagedUIButtonComponent, ToManagedVector4(component->PressedColor), ManagedVector4{ 0.72f, 0.72f, 0.72f, 1.0f });
        LT_MANAGED_COMPONENT_SET(SetUIButtonPressedColorIcall, ManagedVector4, TryGetManagedUIButtonComponent, component->PressedColor = ToGlmVector4(value););
        LT_MANAGED_COMPONENT_GET(GetUIButtonDisabledColorIcall, ManagedVector4, TryGetManagedUIButtonComponent, ToManagedVector4(component->DisabledColor), ManagedVector4{ 0.45f, 0.45f, 0.45f, 1.0f });
        LT_MANAGED_COMPONENT_SET(SetUIButtonDisabledColorIcall, ManagedVector4, TryGetManagedUIButtonComponent, component->DisabledColor = ToGlmVector4(value););
        LT_MANAGED_COMPONENT_GET(GetUIButtonIsHoveredIcall, bool, TryGetManagedUIButtonComponent, component->IsHovered, false);
        LT_MANAGED_COMPONENT_GET(GetUIButtonIsPressedIcall, bool, TryGetManagedUIButtonComponent, component->IsPressed, false);
        LT_MANAGED_COMPONENT_GET(GetUIButtonOnClickEventIcall, Coral::String, TryGetManagedUIButtonComponent, Coral::String::New(component->OnClickEvent), Coral::String::New(""));
        LT_MANAGED_COMPONENT_SET(SetUIButtonOnClickEventIcall, Coral::String, TryGetManagedUIButtonComponent, component->OnClickEvent = ToUtf8Borrowed(value););
        LT_MANAGED_COMPONENT_GET(GetUIButtonOnHoverEnterEventIcall, Coral::String, TryGetManagedUIButtonComponent, Coral::String::New(component->OnHoverEnterEvent), Coral::String::New(""));
        LT_MANAGED_COMPONENT_SET(SetUIButtonOnHoverEnterEventIcall, Coral::String, TryGetManagedUIButtonComponent, component->OnHoverEnterEvent = ToUtf8Borrowed(value););
        LT_MANAGED_COMPONENT_GET(GetUIButtonOnHoverExitEventIcall, Coral::String, TryGetManagedUIButtonComponent, Coral::String::New(component->OnHoverExitEvent), Coral::String::New(""));
        LT_MANAGED_COMPONENT_SET(SetUIButtonOnHoverExitEventIcall, Coral::String, TryGetManagedUIButtonComponent, component->OnHoverExitEvent = ToUtf8Borrowed(value););
        LT_MANAGED_COMPONENT_GET(GetUIButtonOnPressedEventIcall, Coral::String, TryGetManagedUIButtonComponent, Coral::String::New(component->OnPressedEvent), Coral::String::New(""));
        LT_MANAGED_COMPONENT_SET(SetUIButtonOnPressedEventIcall, Coral::String, TryGetManagedUIButtonComponent, component->OnPressedEvent = ToUtf8Borrowed(value););

        LT_MANAGED_COMPONENT_HAS(HasUISliderComponentIcall, TryGetManagedUISliderComponent);
        LT_MANAGED_COMPONENT_GET(GetUISliderInteractableIcall, bool, TryGetManagedUISliderComponent, component->Interactable, true);
        LT_MANAGED_COMPONENT_SET(SetUISliderInteractableIcall, bool, TryGetManagedUISliderComponent, component->Interactable = value;);
        LT_MANAGED_COMPONENT_GET(GetUISliderMinValueIcall, float, TryGetManagedUISliderComponent, component->MinValue, 0.0f);
        LT_MANAGED_COMPONENT_SET(SetUISliderMinValueIcall, float, TryGetManagedUISliderComponent, component->MinValue = value;);
        LT_MANAGED_COMPONENT_GET(GetUISliderMaxValueIcall, float, TryGetManagedUISliderComponent, component->MaxValue, 1.0f);
        LT_MANAGED_COMPONENT_SET(SetUISliderMaxValueIcall, float, TryGetManagedUISliderComponent, component->MaxValue = value;);
        LT_MANAGED_COMPONENT_GET(GetUISliderValueIcall, float, TryGetManagedUISliderComponent, component->Value, 0.0f);
        LT_MANAGED_COMPONENT_SET(SetUISliderValueIcall, float, TryGetManagedUISliderComponent, component->Value = value;);
        LT_MANAGED_COMPONENT_GET(GetUISliderBackgroundColorIcall, ManagedVector4, TryGetManagedUISliderComponent, ToManagedVector4(component->BackgroundColor), ManagedVector4{ 0.22f, 0.22f, 0.22f, 1.0f });
        LT_MANAGED_COMPONENT_SET(SetUISliderBackgroundColorIcall, ManagedVector4, TryGetManagedUISliderComponent, component->BackgroundColor = ToGlmVector4(value););
        LT_MANAGED_COMPONENT_GET(GetUISliderFillColorIcall, ManagedVector4, TryGetManagedUISliderComponent, ToManagedVector4(component->FillColor), ManagedVector4{ 0.22f, 0.72f, 1.0f, 0.95f });
        LT_MANAGED_COMPONENT_SET(SetUISliderFillColorIcall, ManagedVector4, TryGetManagedUISliderComponent, component->FillColor = ToGlmVector4(value););
        LT_MANAGED_COMPONENT_GET(GetUISliderHandleColorIcall, ManagedVector4, TryGetManagedUISliderComponent, ToManagedVector4(component->HandleColor), ManagedVector4{ 0.92f, 0.92f, 0.92f, 1.0f });
        LT_MANAGED_COMPONENT_SET(SetUISliderHandleColorIcall, ManagedVector4, TryGetManagedUISliderComponent, component->HandleColor = ToGlmVector4(value););
        LT_MANAGED_COMPONENT_GET(GetUISliderHandleWidthIcall, float, TryGetManagedUISliderComponent, component->HandleWidth, 16.0f);
        LT_MANAGED_COMPONENT_SET(SetUISliderHandleWidthIcall, float, TryGetManagedUISliderComponent, component->HandleWidth = value;);
        LT_MANAGED_COMPONENT_GET(GetUISliderHandleHeightMultiplierIcall, float, TryGetManagedUISliderComponent, component->HandleHeightMultiplier, 1.25f);
        LT_MANAGED_COMPONENT_SET(SetUISliderHandleHeightMultiplierIcall, float, TryGetManagedUISliderComponent, component->HandleHeightMultiplier = value;);
        LT_MANAGED_COMPONENT_GET(GetUISliderShowHandleIcall, bool, TryGetManagedUISliderComponent, component->ShowHandle, true);
        LT_MANAGED_COMPONENT_SET(SetUISliderShowHandleIcall, bool, TryGetManagedUISliderComponent, component->ShowHandle = value;);
        LT_MANAGED_COMPONENT_GET(GetUISliderRuntimeDraggingIcall, bool, TryGetManagedUISliderComponent, component->RuntimeDragging, false);
        LT_MANAGED_COMPONENT_GET(GetUISliderOnValueChangedEventIcall, Coral::String, TryGetManagedUISliderComponent, Coral::String::New(component->OnValueChangedEvent), Coral::String::New(""));
        LT_MANAGED_COMPONENT_SET(SetUISliderOnValueChangedEventIcall, Coral::String, TryGetManagedUISliderComponent, component->OnValueChangedEvent = ToUtf8Borrowed(value););

        void RegisterRenderingUiInternalCalls(Coral::ManagedAssembly& contractAssembly)
        {
            RegisterInternalCallBatch(contractAssembly, {
                LT_MANAGED_INTERNAL_CALL(HasSpriteComponentIcall),
                LT_MANAGED_INTERNAL_CALL(GetSpriteTextureKeyIcall),
                LT_MANAGED_INTERNAL_CALL(SetSpriteTextureKeyIcall),
                LT_MANAGED_INTERNAL_CALL(GetSpriteColorIcall),
                LT_MANAGED_INTERNAL_CALL(SetSpriteColorIcall),
                LT_MANAGED_INTERNAL_CALL(GetSpriteTilingFactorIcall),
                LT_MANAGED_INTERNAL_CALL(SetSpriteTilingFactorIcall),
                LT_MANAGED_INTERNAL_CALL(GetSpriteRenderOrderIcall),
                LT_MANAGED_INTERNAL_CALL(SetSpriteRenderOrderIcall),
                LT_MANAGED_INTERNAL_CALL(GetSpriteCastShadowsIcall),
                LT_MANAGED_INTERNAL_CALL(SetSpriteCastShadowsIcall),
                LT_MANAGED_INTERNAL_CALL(GetSpriteReceiveShadowsIcall),
                LT_MANAGED_INTERNAL_CALL(SetSpriteReceiveShadowsIcall),
                LT_MANAGED_INTERNAL_CALL(GetSpriteSubSpriteIndexIcall),
                LT_MANAGED_INTERNAL_CALL(SetSpriteSubSpriteIndexIcall),
                LT_MANAGED_INTERNAL_CALL(GetSpriteUvMinIcall),
                LT_MANAGED_INTERNAL_CALL(SetSpriteUvMinIcall),
                LT_MANAGED_INTERNAL_CALL(GetSpriteUvMaxIcall),
                LT_MANAGED_INTERNAL_CALL(SetSpriteUvMaxIcall),
                LT_MANAGED_INTERNAL_CALL(HasMaterialComponentIcall),
                LT_MANAGED_INTERNAL_CALL(GetMaterialKeyIcall),
                LT_MANAGED_INTERNAL_CALL(SetMaterialKeyIcall),
                LT_MANAGED_INTERNAL_CALL(HasCanvasComponentIcall),
                LT_MANAGED_INTERNAL_CALL(GetCanvasRenderModeIcall),
                LT_MANAGED_INTERNAL_CALL(SetCanvasRenderModeIcall),
                LT_MANAGED_INTERNAL_CALL(GetCanvasSortOrderIcall),
                LT_MANAGED_INTERNAL_CALL(SetCanvasSortOrderIcall),
                LT_MANAGED_INTERNAL_CALL(GetCanvasReferenceResolutionIcall),
                LT_MANAGED_INTERNAL_CALL(SetCanvasReferenceResolutionIcall),
                LT_MANAGED_INTERNAL_CALL(HasRectTransformComponentIcall),
                LT_MANAGED_INTERNAL_CALL(GetRectTransformAnchorMinIcall),
                LT_MANAGED_INTERNAL_CALL(SetRectTransformAnchorMinIcall),
                LT_MANAGED_INTERNAL_CALL(GetRectTransformAnchorMaxIcall),
                LT_MANAGED_INTERNAL_CALL(SetRectTransformAnchorMaxIcall),
                LT_MANAGED_INTERNAL_CALL(GetRectTransformPivotIcall),
                LT_MANAGED_INTERNAL_CALL(SetRectTransformPivotIcall),
                LT_MANAGED_INTERNAL_CALL(GetRectTransformSizeDeltaIcall),
                LT_MANAGED_INTERNAL_CALL(SetRectTransformSizeDeltaIcall),
                LT_MANAGED_INTERNAL_CALL(GetRectTransformAnchoredPositionIcall),
                LT_MANAGED_INTERNAL_CALL(SetRectTransformAnchoredPositionIcall),
                LT_MANAGED_INTERNAL_CALL(HasDirectionalLight2DComponentIcall),
                LT_MANAGED_INTERNAL_CALL(GetDirectionalLight2DEnabledIcall),
                LT_MANAGED_INTERNAL_CALL(SetDirectionalLight2DEnabledIcall),
                LT_MANAGED_INTERNAL_CALL(GetDirectionalLight2DColorIcall),
                LT_MANAGED_INTERNAL_CALL(SetDirectionalLight2DColorIcall),
                LT_MANAGED_INTERNAL_CALL(GetDirectionalLight2DIntensityIcall),
                LT_MANAGED_INTERNAL_CALL(SetDirectionalLight2DIntensityIcall),
                LT_MANAGED_INTERNAL_CALL(GetDirectionalLight2DUseEntityRotationIcall),
                LT_MANAGED_INTERNAL_CALL(SetDirectionalLight2DUseEntityRotationIcall),
                LT_MANAGED_INTERNAL_CALL(GetDirectionalLight2DDirectionIcall),
                LT_MANAGED_INTERNAL_CALL(SetDirectionalLight2DDirectionIcall),
                LT_MANAGED_INTERNAL_CALL(GetDirectionalLight2DCastShadowsIcall),
                LT_MANAGED_INTERNAL_CALL(SetDirectionalLight2DCastShadowsIcall),
                LT_MANAGED_INTERNAL_CALL(GetDirectionalLight2DShadowStrengthIcall),
                LT_MANAGED_INTERNAL_CALL(SetDirectionalLight2DShadowStrengthIcall),
                LT_MANAGED_INTERNAL_CALL(GetDirectionalLight2DShadowSoftnessIcall),
                LT_MANAGED_INTERNAL_CALL(SetDirectionalLight2DShadowSoftnessIcall),
                LT_MANAGED_INTERNAL_CALL(GetDirectionalLight2DShadowSamplesIcall),
                LT_MANAGED_INTERNAL_CALL(SetDirectionalLight2DShadowSamplesIcall),
                LT_MANAGED_INTERNAL_CALL(GetDirectionalLight2DShadowDistanceIcall),
                LT_MANAGED_INTERNAL_CALL(SetDirectionalLight2DShadowDistanceIcall),
                LT_MANAGED_INTERNAL_CALL(GetDirectionalLight2DShadowBiasIcall),
                LT_MANAGED_INTERNAL_CALL(SetDirectionalLight2DShadowBiasIcall),
                LT_MANAGED_INTERNAL_CALL(HasPointLight2DComponentIcall),
                LT_MANAGED_INTERNAL_CALL(GetPointLight2DEnabledIcall),
                LT_MANAGED_INTERNAL_CALL(SetPointLight2DEnabledIcall),
                LT_MANAGED_INTERNAL_CALL(GetPointLight2DColorIcall),
                LT_MANAGED_INTERNAL_CALL(SetPointLight2DColorIcall),
                LT_MANAGED_INTERNAL_CALL(GetPointLight2DIntensityIcall),
                LT_MANAGED_INTERNAL_CALL(SetPointLight2DIntensityIcall),
                LT_MANAGED_INTERNAL_CALL(GetPointLight2DRadiusIcall),
                LT_MANAGED_INTERNAL_CALL(SetPointLight2DRadiusIcall),
                LT_MANAGED_INTERNAL_CALL(GetPointLight2DFalloffIcall),
                LT_MANAGED_INTERNAL_CALL(SetPointLight2DFalloffIcall),
                LT_MANAGED_INTERNAL_CALL(GetPointLight2DCastShadowsIcall),
                LT_MANAGED_INTERNAL_CALL(SetPointLight2DCastShadowsIcall),
                LT_MANAGED_INTERNAL_CALL(GetPointLight2DShadowStrengthIcall),
                LT_MANAGED_INTERNAL_CALL(SetPointLight2DShadowStrengthIcall),
                LT_MANAGED_INTERNAL_CALL(GetPointLight2DShadowSoftnessIcall),
                LT_MANAGED_INTERNAL_CALL(SetPointLight2DShadowSoftnessIcall),
                LT_MANAGED_INTERNAL_CALL(GetPointLight2DShadowSamplesIcall),
                LT_MANAGED_INTERNAL_CALL(SetPointLight2DShadowSamplesIcall),
                LT_MANAGED_INTERNAL_CALL(GetPointLight2DShadowBiasIcall),
                LT_MANAGED_INTERNAL_CALL(SetPointLight2DShadowBiasIcall),
                LT_MANAGED_INTERNAL_CALL(HasUIImageComponentIcall),
                LT_MANAGED_INTERNAL_CALL(GetUIImageRaycastTargetIcall),
                LT_MANAGED_INTERNAL_CALL(SetUIImageRaycastTargetIcall),
                LT_MANAGED_INTERNAL_CALL(HasUIPanelComponentIcall),
                LT_MANAGED_INTERNAL_CALL(GetUIPanelBackgroundColorIcall),
                LT_MANAGED_INTERNAL_CALL(SetUIPanelBackgroundColorIcall),
                LT_MANAGED_INTERNAL_CALL(GetUIPanelUseSpriteTextureIcall),
                LT_MANAGED_INTERNAL_CALL(SetUIPanelUseSpriteTextureIcall),
                LT_MANAGED_INTERNAL_CALL(GetUIPanelRaycastTargetIcall),
                LT_MANAGED_INTERNAL_CALL(SetUIPanelRaycastTargetIcall),
                LT_MANAGED_INTERNAL_CALL(HasUITextComponentIcall),
                LT_MANAGED_INTERNAL_CALL(GetUITextValueIcall),
                LT_MANAGED_INTERNAL_CALL(SetUITextValueIcall),
                LT_MANAGED_INTERNAL_CALL(GetUITextFontFilePathIcall),
                LT_MANAGED_INTERNAL_CALL(SetUITextFontFilePathIcall),
                LT_MANAGED_INTERNAL_CALL(GetUITextFontSizeIcall),
                LT_MANAGED_INTERNAL_CALL(SetUITextFontSizeIcall),
                LT_MANAGED_INTERNAL_CALL(GetUITextColorIcall),
                LT_MANAGED_INTERNAL_CALL(SetUITextColorIcall),
                LT_MANAGED_INTERNAL_CALL(GetUITextRaycastTargetIcall),
                LT_MANAGED_INTERNAL_CALL(SetUITextRaycastTargetIcall),
                LT_MANAGED_INTERNAL_CALL(HasUIButtonComponentIcall),
                LT_MANAGED_INTERNAL_CALL(GetUIButtonInteractableIcall),
                LT_MANAGED_INTERNAL_CALL(SetUIButtonInteractableIcall),
                LT_MANAGED_INTERNAL_CALL(GetUIButtonUseStateColorsIcall),
                LT_MANAGED_INTERNAL_CALL(SetUIButtonUseStateColorsIcall),
                LT_MANAGED_INTERNAL_CALL(GetUIButtonNormalColorIcall),
                LT_MANAGED_INTERNAL_CALL(SetUIButtonNormalColorIcall),
                LT_MANAGED_INTERNAL_CALL(GetUIButtonHoveredColorIcall),
                LT_MANAGED_INTERNAL_CALL(SetUIButtonHoveredColorIcall),
                LT_MANAGED_INTERNAL_CALL(GetUIButtonPressedColorIcall),
                LT_MANAGED_INTERNAL_CALL(SetUIButtonPressedColorIcall),
                LT_MANAGED_INTERNAL_CALL(GetUIButtonDisabledColorIcall),
                LT_MANAGED_INTERNAL_CALL(SetUIButtonDisabledColorIcall),
                LT_MANAGED_INTERNAL_CALL(GetUIButtonIsHoveredIcall),
                LT_MANAGED_INTERNAL_CALL(GetUIButtonIsPressedIcall),
                LT_MANAGED_INTERNAL_CALL(GetUIButtonOnClickEventIcall),
                LT_MANAGED_INTERNAL_CALL(SetUIButtonOnClickEventIcall),
                LT_MANAGED_INTERNAL_CALL(GetUIButtonOnHoverEnterEventIcall),
                LT_MANAGED_INTERNAL_CALL(SetUIButtonOnHoverEnterEventIcall),
                LT_MANAGED_INTERNAL_CALL(GetUIButtonOnHoverExitEventIcall),
                LT_MANAGED_INTERNAL_CALL(SetUIButtonOnHoverExitEventIcall),
                LT_MANAGED_INTERNAL_CALL(GetUIButtonOnPressedEventIcall),
                LT_MANAGED_INTERNAL_CALL(SetUIButtonOnPressedEventIcall),
                LT_MANAGED_INTERNAL_CALL(HasUISliderComponentIcall),
                LT_MANAGED_INTERNAL_CALL(GetUISliderInteractableIcall),
                LT_MANAGED_INTERNAL_CALL(SetUISliderInteractableIcall),
                LT_MANAGED_INTERNAL_CALL(GetUISliderMinValueIcall),
                LT_MANAGED_INTERNAL_CALL(SetUISliderMinValueIcall),
                LT_MANAGED_INTERNAL_CALL(GetUISliderMaxValueIcall),
                LT_MANAGED_INTERNAL_CALL(SetUISliderMaxValueIcall),
                LT_MANAGED_INTERNAL_CALL(GetUISliderValueIcall),
                LT_MANAGED_INTERNAL_CALL(SetUISliderValueIcall),
                LT_MANAGED_INTERNAL_CALL(GetUISliderBackgroundColorIcall),
                LT_MANAGED_INTERNAL_CALL(SetUISliderBackgroundColorIcall),
                LT_MANAGED_INTERNAL_CALL(GetUISliderFillColorIcall),
                LT_MANAGED_INTERNAL_CALL(SetUISliderFillColorIcall),
                LT_MANAGED_INTERNAL_CALL(GetUISliderHandleColorIcall),
                LT_MANAGED_INTERNAL_CALL(SetUISliderHandleColorIcall),
                LT_MANAGED_INTERNAL_CALL(GetUISliderHandleWidthIcall),
                LT_MANAGED_INTERNAL_CALL(SetUISliderHandleWidthIcall),
                LT_MANAGED_INTERNAL_CALL(GetUISliderHandleHeightMultiplierIcall),
                LT_MANAGED_INTERNAL_CALL(SetUISliderHandleHeightMultiplierIcall),
                LT_MANAGED_INTERNAL_CALL(GetUISliderShowHandleIcall),
                LT_MANAGED_INTERNAL_CALL(SetUISliderShowHandleIcall),
                LT_MANAGED_INTERNAL_CALL(GetUISliderRuntimeDraggingIcall),
                LT_MANAGED_INTERNAL_CALL(GetUISliderOnValueChangedEventIcall),
                LT_MANAGED_INTERNAL_CALL(SetUISliderOnValueChangedEventIcall)
            });
        }
    }
}
