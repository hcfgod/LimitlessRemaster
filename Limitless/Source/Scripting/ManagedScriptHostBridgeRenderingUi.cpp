#include "Scripting/ManagedScriptHostInternal.h"

#include <algorithm>

namespace Limitless::ManagedScriptHost
{
    using namespace Internal;

    namespace Internal
    {
        bool ManagedHasSpriteComponentIcall(uint32_t entityHandle)
        {
            return TryGetManagedSpriteComponent(entityHandle) != nullptr;
        }

        Coral::String ManagedGetSpriteTextureKeyIcall(uint32_t entityHandle)
        {
            const auto* sprite = TryGetManagedSpriteComponent(entityHandle);
            return sprite ? Coral::String::New(sprite->TextureKey) : Coral::String::New("");
        }

        void ManagedSetSpriteTextureKeyIcall(uint32_t entityHandle, Coral::String textureKey)
        {
            if (auto* sprite = TryGetManagedSpriteComponent(entityHandle))
                sprite->TextureKey = ToUtf8Borrowed(textureKey);
        }

        ManagedVector4 ManagedGetSpriteColorIcall(uint32_t entityHandle)
        {
            const auto* sprite = TryGetManagedSpriteComponent(entityHandle);
            return sprite ? ToManagedVector4(sprite->Color) : ManagedVector4{ 1.0f, 1.0f, 1.0f, 1.0f };
        }

        void ManagedSetSpriteColorIcall(uint32_t entityHandle, ManagedVector4 value)
        {
            if (auto* sprite = TryGetManagedSpriteComponent(entityHandle))
                sprite->Color = ToGlmVector4(value);
        }

        ManagedVector2 ManagedGetSpriteTilingFactorIcall(uint32_t entityHandle)
        {
            const auto* sprite = TryGetManagedSpriteComponent(entityHandle);
            return sprite ? ToManagedVector2(sprite->TilingFactor) : ManagedVector2{ 1.0f, 1.0f };
        }

        void ManagedSetSpriteTilingFactorIcall(uint32_t entityHandle, ManagedVector2 value)
        {
            if (auto* sprite = TryGetManagedSpriteComponent(entityHandle))
                sprite->TilingFactor = ToGlmVector2(value);
        }

        int ManagedGetSpriteRenderOrderIcall(uint32_t entityHandle)
        {
            const auto* sprite = TryGetManagedSpriteComponent(entityHandle);
            return sprite ? sprite->RenderOrder : 0;
        }

        void ManagedSetSpriteRenderOrderIcall(uint32_t entityHandle, int value)
        {
            if (auto* sprite = TryGetManagedSpriteComponent(entityHandle))
                sprite->RenderOrder = value;
        }

        bool ManagedGetSpriteCastShadowsIcall(uint32_t entityHandle)
        {
            const auto* sprite = TryGetManagedSpriteComponent(entityHandle);
            return sprite ? sprite->CastShadows : true;
        }

        void ManagedSetSpriteCastShadowsIcall(uint32_t entityHandle, bool value)
        {
            if (auto* sprite = TryGetManagedSpriteComponent(entityHandle))
                sprite->CastShadows = value;
        }

        bool ManagedGetSpriteReceiveShadowsIcall(uint32_t entityHandle)
        {
            const auto* sprite = TryGetManagedSpriteComponent(entityHandle);
            return sprite ? sprite->ReceiveShadows : true;
        }

        void ManagedSetSpriteReceiveShadowsIcall(uint32_t entityHandle, bool value)
        {
            if (auto* sprite = TryGetManagedSpriteComponent(entityHandle))
                sprite->ReceiveShadows = value;
        }

        int ManagedGetSpriteSubSpriteIndexIcall(uint32_t entityHandle)
        {
            const auto* sprite = TryGetManagedSpriteComponent(entityHandle);
            return sprite ? sprite->SubSpriteIndex : -1;
        }

        void ManagedSetSpriteSubSpriteIndexIcall(uint32_t entityHandle, int value)
        {
            if (auto* sprite = TryGetManagedSpriteComponent(entityHandle))
                sprite->SubSpriteIndex = value;
        }

        ManagedVector2 ManagedGetSpriteUvMinIcall(uint32_t entityHandle)
        {
            const auto* sprite = TryGetManagedSpriteComponent(entityHandle);
            return sprite ? ToManagedVector2(sprite->UvMin) : ManagedVector2{};
        }

        void ManagedSetSpriteUvMinIcall(uint32_t entityHandle, ManagedVector2 value)
        {
            if (auto* sprite = TryGetManagedSpriteComponent(entityHandle))
                sprite->UvMin = ToGlmVector2(value);
        }

        ManagedVector2 ManagedGetSpriteUvMaxIcall(uint32_t entityHandle)
        {
            const auto* sprite = TryGetManagedSpriteComponent(entityHandle);
            return sprite ? ToManagedVector2(sprite->UvMax) : ManagedVector2{ 1.0f, 1.0f };
        }

        void ManagedSetSpriteUvMaxIcall(uint32_t entityHandle, ManagedVector2 value)
        {
            if (auto* sprite = TryGetManagedSpriteComponent(entityHandle))
                sprite->UvMax = ToGlmVector2(value);
        }

        bool ManagedHasMaterialComponentIcall(uint32_t entityHandle)
        {
            return TryGetManagedMaterialComponent(entityHandle) != nullptr;
        }

        Coral::String ManagedGetMaterialKeyIcall(uint32_t entityHandle)
        {
            const auto* material = TryGetManagedMaterialComponent(entityHandle);
            return material ? Coral::String::New(material->MaterialKey) : Coral::String::New("");
        }

        void ManagedSetMaterialKeyIcall(uint32_t entityHandle, Coral::String materialKey)
        {
            if (auto* material = TryGetManagedMaterialComponent(entityHandle))
                material->MaterialKey = ToUtf8Borrowed(materialKey);
        }

        bool ManagedHasCanvasComponentIcall(uint32_t entityHandle)
        {
            return TryGetManagedCanvasComponent(entityHandle) != nullptr;
        }

        int ManagedGetCanvasRenderModeIcall(uint32_t entityHandle)
        {
            const auto* canvas = TryGetManagedCanvasComponent(entityHandle);
            return canvas ? static_cast<int>(canvas->Mode) : static_cast<int>(CanvasComponent::RenderMode::ScreenSpace);
        }

        void ManagedSetCanvasRenderModeIcall(uint32_t entityHandle, int value)
        {
            if (auto* canvas = TryGetManagedCanvasComponent(entityHandle))
                canvas->Mode = static_cast<CanvasComponent::RenderMode>(value);
        }

        int ManagedGetCanvasSortOrderIcall(uint32_t entityHandle)
        {
            const auto* canvas = TryGetManagedCanvasComponent(entityHandle);
            return canvas ? canvas->SortOrder : 0;
        }

        void ManagedSetCanvasSortOrderIcall(uint32_t entityHandle, int value)
        {
            if (auto* canvas = TryGetManagedCanvasComponent(entityHandle))
                canvas->SortOrder = value;
        }

        ManagedVector2 ManagedGetCanvasReferenceResolutionIcall(uint32_t entityHandle)
        {
            const auto* canvas = TryGetManagedCanvasComponent(entityHandle);
            return canvas ? ToManagedVector2(canvas->ReferenceResolution) : ManagedVector2{ 1920.0f, 1080.0f };
        }

        void ManagedSetCanvasReferenceResolutionIcall(uint32_t entityHandle, ManagedVector2 value)
        {
            if (auto* canvas = TryGetManagedCanvasComponent(entityHandle))
                canvas->ReferenceResolution = ToGlmVector2(value);
        }

        bool ManagedHasRectTransformComponentIcall(uint32_t entityHandle)
        {
            return TryGetManagedRectTransformComponent(entityHandle) != nullptr;
        }

        ManagedVector2 ManagedGetRectTransformAnchorMinIcall(uint32_t entityHandle)
        {
            const auto* rectTransform = TryGetManagedRectTransformComponent(entityHandle);
            return rectTransform ? ToManagedVector2(rectTransform->AnchorMin) : ManagedVector2{ 0.5f, 0.5f };
        }

        void ManagedSetRectTransformAnchorMinIcall(uint32_t entityHandle, ManagedVector2 value)
        {
            if (auto* rectTransform = TryGetManagedRectTransformComponent(entityHandle))
                rectTransform->AnchorMin = ToGlmVector2(value);
        }

        ManagedVector2 ManagedGetRectTransformAnchorMaxIcall(uint32_t entityHandle)
        {
            const auto* rectTransform = TryGetManagedRectTransformComponent(entityHandle);
            return rectTransform ? ToManagedVector2(rectTransform->AnchorMax) : ManagedVector2{ 0.5f, 0.5f };
        }

        void ManagedSetRectTransformAnchorMaxIcall(uint32_t entityHandle, ManagedVector2 value)
        {
            if (auto* rectTransform = TryGetManagedRectTransformComponent(entityHandle))
                rectTransform->AnchorMax = ToGlmVector2(value);
        }

        ManagedVector2 ManagedGetRectTransformPivotIcall(uint32_t entityHandle)
        {
            const auto* rectTransform = TryGetManagedRectTransformComponent(entityHandle);
            return rectTransform ? ToManagedVector2(rectTransform->Pivot) : ManagedVector2{ 0.5f, 0.5f };
        }

        void ManagedSetRectTransformPivotIcall(uint32_t entityHandle, ManagedVector2 value)
        {
            if (auto* rectTransform = TryGetManagedRectTransformComponent(entityHandle))
                rectTransform->Pivot = ToGlmVector2(value);
        }

        ManagedVector2 ManagedGetRectTransformSizeDeltaIcall(uint32_t entityHandle)
        {
            const auto* rectTransform = TryGetManagedRectTransformComponent(entityHandle);
            return rectTransform ? ToManagedVector2(rectTransform->SizeDelta) : ManagedVector2{ 100.0f, 40.0f };
        }

        void ManagedSetRectTransformSizeDeltaIcall(uint32_t entityHandle, ManagedVector2 value)
        {
            if (auto* rectTransform = TryGetManagedRectTransformComponent(entityHandle))
                rectTransform->SizeDelta = ToGlmVector2(value);
        }

        ManagedVector2 ManagedGetRectTransformAnchoredPositionIcall(uint32_t entityHandle)
        {
            const auto* rectTransform = TryGetManagedRectTransformComponent(entityHandle);
            return rectTransform ? ToManagedVector2(rectTransform->AnchoredPosition) : ManagedVector2{};
        }

        void ManagedSetRectTransformAnchoredPositionIcall(uint32_t entityHandle, ManagedVector2 value)
        {
            if (auto* rectTransform = TryGetManagedRectTransformComponent(entityHandle))
                rectTransform->AnchoredPosition = ToGlmVector2(value);
        }

        bool ManagedHasDirectionalLight2DComponentIcall(uint32_t entityHandle)
        {
            return TryGetManagedDirectionalLight2DComponent(entityHandle) != nullptr;
        }

        bool ManagedGetDirectionalLight2DEnabledIcall(uint32_t entityHandle)
        {
            const auto* light = TryGetManagedDirectionalLight2DComponent(entityHandle);
            return light ? light->Enabled : true;
        }

        void ManagedSetDirectionalLight2DEnabledIcall(uint32_t entityHandle, bool value)
        {
            if (auto* light = TryGetManagedDirectionalLight2DComponent(entityHandle))
                light->Enabled = value;
        }

        ManagedVector3 ManagedGetDirectionalLight2DColorIcall(uint32_t entityHandle)
        {
            const auto* light = TryGetManagedDirectionalLight2DComponent(entityHandle);
            return light ? ToManagedVector3(light->Color) : ManagedVector3{ 1.0f, 1.0f, 1.0f };
        }

        void ManagedSetDirectionalLight2DColorIcall(uint32_t entityHandle, ManagedVector3 value)
        {
            if (auto* light = TryGetManagedDirectionalLight2DComponent(entityHandle))
                light->Color = ToGlmVector3(value);
        }

        float ManagedGetDirectionalLight2DIntensityIcall(uint32_t entityHandle)
        {
            const auto* light = TryGetManagedDirectionalLight2DComponent(entityHandle);
            return light ? light->Intensity : 1.0f;
        }

        void ManagedSetDirectionalLight2DIntensityIcall(uint32_t entityHandle, float value)
        {
            if (auto* light = TryGetManagedDirectionalLight2DComponent(entityHandle))
                light->Intensity = value;
        }

        bool ManagedGetDirectionalLight2DUseEntityRotationIcall(uint32_t entityHandle)
        {
            const auto* light = TryGetManagedDirectionalLight2DComponent(entityHandle);
            return light ? light->UseEntityRotation : true;
        }

        void ManagedSetDirectionalLight2DUseEntityRotationIcall(uint32_t entityHandle, bool value)
        {
            if (auto* light = TryGetManagedDirectionalLight2DComponent(entityHandle))
                light->UseEntityRotation = value;
        }

        ManagedVector2 ManagedGetDirectionalLight2DDirectionIcall(uint32_t entityHandle)
        {
            const auto* light = TryGetManagedDirectionalLight2DComponent(entityHandle);
            return light ? ToManagedVector2(light->Direction) : ManagedVector2{ 0.0f, -1.0f };
        }

        void ManagedSetDirectionalLight2DDirectionIcall(uint32_t entityHandle, ManagedVector2 value)
        {
            if (auto* light = TryGetManagedDirectionalLight2DComponent(entityHandle))
                light->Direction = ToGlmVector2(value);
        }

        bool ManagedGetDirectionalLight2DCastShadowsIcall(uint32_t entityHandle)
        {
            const auto* light = TryGetManagedDirectionalLight2DComponent(entityHandle);
            return light ? light->CastShadows : true;
        }

        void ManagedSetDirectionalLight2DCastShadowsIcall(uint32_t entityHandle, bool value)
        {
            if (auto* light = TryGetManagedDirectionalLight2DComponent(entityHandle))
                light->CastShadows = value;
        }

        float ManagedGetDirectionalLight2DShadowStrengthIcall(uint32_t entityHandle)
        {
            const auto* light = TryGetManagedDirectionalLight2DComponent(entityHandle);
            return light ? light->ShadowStrength : 1.0f;
        }

        void ManagedSetDirectionalLight2DShadowStrengthIcall(uint32_t entityHandle, float value)
        {
            if (auto* light = TryGetManagedDirectionalLight2DComponent(entityHandle))
                light->ShadowStrength = value;
        }

        float ManagedGetDirectionalLight2DShadowSoftnessIcall(uint32_t entityHandle)
        {
            const auto* light = TryGetManagedDirectionalLight2DComponent(entityHandle);
            return light ? light->ShadowSoftness : 1.0f;
        }

        void ManagedSetDirectionalLight2DShadowSoftnessIcall(uint32_t entityHandle, float value)
        {
            if (auto* light = TryGetManagedDirectionalLight2DComponent(entityHandle))
                light->ShadowSoftness = value;
        }

        int ManagedGetDirectionalLight2DShadowSamplesIcall(uint32_t entityHandle)
        {
            const auto* light = TryGetManagedDirectionalLight2DComponent(entityHandle);
            return light ? light->ShadowSamples : 8;
        }

        void ManagedSetDirectionalLight2DShadowSamplesIcall(uint32_t entityHandle, int value)
        {
            if (auto* light = TryGetManagedDirectionalLight2DComponent(entityHandle))
                light->ShadowSamples = value;
        }

        float ManagedGetDirectionalLight2DShadowDistanceIcall(uint32_t entityHandle)
        {
            const auto* light = TryGetManagedDirectionalLight2DComponent(entityHandle);
            return light ? light->ShadowDistance : 25.0f;
        }

        void ManagedSetDirectionalLight2DShadowDistanceIcall(uint32_t entityHandle, float value)
        {
            if (auto* light = TryGetManagedDirectionalLight2DComponent(entityHandle))
                light->ShadowDistance = value;
        }

        float ManagedGetDirectionalLight2DShadowBiasIcall(uint32_t entityHandle)
        {
            const auto* light = TryGetManagedDirectionalLight2DComponent(entityHandle);
            return light ? light->ShadowBias : 0.02f;
        }

        void ManagedSetDirectionalLight2DShadowBiasIcall(uint32_t entityHandle, float value)
        {
            if (auto* light = TryGetManagedDirectionalLight2DComponent(entityHandle))
                light->ShadowBias = value;
        }

        bool ManagedHasPointLight2DComponentIcall(uint32_t entityHandle)
        {
            return TryGetManagedPointLight2DComponent(entityHandle) != nullptr;
        }

        bool ManagedGetPointLight2DEnabledIcall(uint32_t entityHandle)
        {
            const auto* light = TryGetManagedPointLight2DComponent(entityHandle);
            return light ? light->Enabled : true;
        }

        void ManagedSetPointLight2DEnabledIcall(uint32_t entityHandle, bool value)
        {
            if (auto* light = TryGetManagedPointLight2DComponent(entityHandle))
                light->Enabled = value;
        }

        ManagedVector3 ManagedGetPointLight2DColorIcall(uint32_t entityHandle)
        {
            const auto* light = TryGetManagedPointLight2DComponent(entityHandle);
            return light ? ToManagedVector3(light->Color) : ManagedVector3{ 1.0f, 1.0f, 1.0f };
        }

        void ManagedSetPointLight2DColorIcall(uint32_t entityHandle, ManagedVector3 value)
        {
            if (auto* light = TryGetManagedPointLight2DComponent(entityHandle))
                light->Color = ToGlmVector3(value);
        }

        float ManagedGetPointLight2DIntensityIcall(uint32_t entityHandle)
        {
            const auto* light = TryGetManagedPointLight2DComponent(entityHandle);
            return light ? light->Intensity : 1.0f;
        }

        void ManagedSetPointLight2DIntensityIcall(uint32_t entityHandle, float value)
        {
            if (auto* light = TryGetManagedPointLight2DComponent(entityHandle))
                light->Intensity = value;
        }

        float ManagedGetPointLight2DRadiusIcall(uint32_t entityHandle)
        {
            const auto* light = TryGetManagedPointLight2DComponent(entityHandle);
            return light ? light->Radius : 5.0f;
        }

        void ManagedSetPointLight2DRadiusIcall(uint32_t entityHandle, float value)
        {
            if (auto* light = TryGetManagedPointLight2DComponent(entityHandle))
                light->Radius = value;
        }

        float ManagedGetPointLight2DFalloffIcall(uint32_t entityHandle)
        {
            const auto* light = TryGetManagedPointLight2DComponent(entityHandle);
            return light ? light->Falloff : 2.0f;
        }

        void ManagedSetPointLight2DFalloffIcall(uint32_t entityHandle, float value)
        {
            if (auto* light = TryGetManagedPointLight2DComponent(entityHandle))
                light->Falloff = value;
        }

        bool ManagedGetPointLight2DCastShadowsIcall(uint32_t entityHandle)
        {
            const auto* light = TryGetManagedPointLight2DComponent(entityHandle);
            return light ? light->CastShadows : true;
        }

        void ManagedSetPointLight2DCastShadowsIcall(uint32_t entityHandle, bool value)
        {
            if (auto* light = TryGetManagedPointLight2DComponent(entityHandle))
                light->CastShadows = value;
        }

        float ManagedGetPointLight2DShadowStrengthIcall(uint32_t entityHandle)
        {
            const auto* light = TryGetManagedPointLight2DComponent(entityHandle);
            return light ? light->ShadowStrength : 1.0f;
        }

        void ManagedSetPointLight2DShadowStrengthIcall(uint32_t entityHandle, float value)
        {
            if (auto* light = TryGetManagedPointLight2DComponent(entityHandle))
                light->ShadowStrength = value;
        }

        float ManagedGetPointLight2DShadowSoftnessIcall(uint32_t entityHandle)
        {
            const auto* light = TryGetManagedPointLight2DComponent(entityHandle);
            return light ? light->ShadowSoftness : 1.0f;
        }

        void ManagedSetPointLight2DShadowSoftnessIcall(uint32_t entityHandle, float value)
        {
            if (auto* light = TryGetManagedPointLight2DComponent(entityHandle))
                light->ShadowSoftness = value;
        }

        int ManagedGetPointLight2DShadowSamplesIcall(uint32_t entityHandle)
        {
            const auto* light = TryGetManagedPointLight2DComponent(entityHandle);
            return light ? light->ShadowSamples : 8;
        }

        void ManagedSetPointLight2DShadowSamplesIcall(uint32_t entityHandle, int value)
        {
            if (auto* light = TryGetManagedPointLight2DComponent(entityHandle))
                light->ShadowSamples = value;
        }

        float ManagedGetPointLight2DShadowBiasIcall(uint32_t entityHandle)
        {
            const auto* light = TryGetManagedPointLight2DComponent(entityHandle);
            return light ? light->ShadowBias : 0.0015f;
        }

        void ManagedSetPointLight2DShadowBiasIcall(uint32_t entityHandle, float value)
        {
            if (auto* light = TryGetManagedPointLight2DComponent(entityHandle))
                light->ShadowBias = value;
        }

        bool ManagedHasUIImageComponentIcall(uint32_t entityHandle)
        {
            return TryGetManagedUIImageComponent(entityHandle) != nullptr;
        }

        bool ManagedGetUIImageRaycastTargetIcall(uint32_t entityHandle)
        {
            const auto* image = TryGetManagedUIImageComponent(entityHandle);
            return image ? image->RaycastTarget : true;
        }

        void ManagedSetUIImageRaycastTargetIcall(uint32_t entityHandle, bool value)
        {
            if (auto* image = TryGetManagedUIImageComponent(entityHandle))
                image->RaycastTarget = value;
        }

        bool ManagedHasUIPanelComponentIcall(uint32_t entityHandle)
        {
            return TryGetManagedUIPanelComponent(entityHandle) != nullptr;
        }

        ManagedVector4 ManagedGetUIPanelBackgroundColorIcall(uint32_t entityHandle)
        {
            const auto* panel = TryGetManagedUIPanelComponent(entityHandle);
            return panel ? ToManagedVector4(panel->BackgroundColor) : ManagedVector4{ 0.12f, 0.12f, 0.12f, 0.9f };
        }

        void ManagedSetUIPanelBackgroundColorIcall(uint32_t entityHandle, ManagedVector4 value)
        {
            if (auto* panel = TryGetManagedUIPanelComponent(entityHandle))
                panel->BackgroundColor = ToGlmVector4(value);
        }

        bool ManagedGetUIPanelUseSpriteTextureIcall(uint32_t entityHandle)
        {
            const auto* panel = TryGetManagedUIPanelComponent(entityHandle);
            return panel ? panel->UseSpriteTexture : false;
        }

        void ManagedSetUIPanelUseSpriteTextureIcall(uint32_t entityHandle, bool value)
        {
            if (auto* panel = TryGetManagedUIPanelComponent(entityHandle))
                panel->UseSpriteTexture = value;
        }

        bool ManagedGetUIPanelRaycastTargetIcall(uint32_t entityHandle)
        {
            const auto* panel = TryGetManagedUIPanelComponent(entityHandle);
            return panel ? panel->RaycastTarget : false;
        }

        void ManagedSetUIPanelRaycastTargetIcall(uint32_t entityHandle, bool value)
        {
            if (auto* panel = TryGetManagedUIPanelComponent(entityHandle))
                panel->RaycastTarget = value;
        }

        bool ManagedHasUITextComponentIcall(uint32_t entityHandle)
        {
            return TryGetManagedUITextComponent(entityHandle) != nullptr;
        }

        Coral::String ManagedGetUITextValueIcall(uint32_t entityHandle)
        {
            const auto* text = TryGetManagedUITextComponent(entityHandle);
            return text ? Coral::String::New(text->Text) : Coral::String::New("");
        }

        void ManagedSetUITextValueIcall(uint32_t entityHandle, Coral::String value)
        {
            if (auto* text = TryGetManagedUITextComponent(entityHandle))
                text->Text = ToUtf8Borrowed(value);
        }

        Coral::String ManagedGetUITextFontFilePathIcall(uint32_t entityHandle)
        {
            const auto* text = TryGetManagedUITextComponent(entityHandle);
            return text ? Coral::String::New(text->FontFilePath) : Coral::String::New("");
        }

        void ManagedSetUITextFontFilePathIcall(uint32_t entityHandle, Coral::String value)
        {
            if (auto* text = TryGetManagedUITextComponent(entityHandle))
                text->FontFilePath = ToUtf8Borrowed(value);
        }

        float ManagedGetUITextFontSizeIcall(uint32_t entityHandle)
        {
            const auto* text = TryGetManagedUITextComponent(entityHandle);
            return text ? text->FontSize : 32.0f;
        }

        void ManagedSetUITextFontSizeIcall(uint32_t entityHandle, float value)
        {
            if (auto* text = TryGetManagedUITextComponent(entityHandle))
                text->FontSize = value;
        }

        ManagedVector4 ManagedGetUITextColorIcall(uint32_t entityHandle)
        {
            const auto* text = TryGetManagedUITextComponent(entityHandle);
            return text ? ToManagedVector4(text->Color) : ManagedVector4{ 1.0f, 1.0f, 1.0f, 1.0f };
        }

        void ManagedSetUITextColorIcall(uint32_t entityHandle, ManagedVector4 value)
        {
            if (auto* text = TryGetManagedUITextComponent(entityHandle))
                text->Color = ToGlmVector4(value);
        }

        bool ManagedGetUITextRaycastTargetIcall(uint32_t entityHandle)
        {
            const auto* text = TryGetManagedUITextComponent(entityHandle);
            return text ? text->RaycastTarget : false;
        }

        void ManagedSetUITextRaycastTargetIcall(uint32_t entityHandle, bool value)
        {
            if (auto* text = TryGetManagedUITextComponent(entityHandle))
                text->RaycastTarget = value;
        }

        bool ManagedHasUIButtonComponentIcall(uint32_t entityHandle)
        {
            return TryGetManagedUIButtonComponent(entityHandle) != nullptr;
        }

        bool ManagedGetUIButtonInteractableIcall(uint32_t entityHandle)
        {
            const auto* button = TryGetManagedUIButtonComponent(entityHandle);
            return button ? button->Interactable : true;
        }

        void ManagedSetUIButtonInteractableIcall(uint32_t entityHandle, bool value)
        {
            if (auto* button = TryGetManagedUIButtonComponent(entityHandle))
                button->Interactable = value;
        }

        bool ManagedGetUIButtonUseStateColorsIcall(uint32_t entityHandle)
        {
            const auto* button = TryGetManagedUIButtonComponent(entityHandle);
            return button ? button->UseStateColors : true;
        }

        void ManagedSetUIButtonUseStateColorsIcall(uint32_t entityHandle, bool value)
        {
            if (auto* button = TryGetManagedUIButtonComponent(entityHandle))
                button->UseStateColors = value;
        }

        ManagedVector4 ManagedGetUIButtonNormalColorIcall(uint32_t entityHandle)
        {
            const auto* button = TryGetManagedUIButtonComponent(entityHandle);
            return button ? ToManagedVector4(button->NormalColor) : ManagedVector4{ 0.82f, 0.82f, 0.82f, 1.0f };
        }

        void ManagedSetUIButtonNormalColorIcall(uint32_t entityHandle, ManagedVector4 value)
        {
            if (auto* button = TryGetManagedUIButtonComponent(entityHandle))
                button->NormalColor = ToGlmVector4(value);
        }

        ManagedVector4 ManagedGetUIButtonHoveredColorIcall(uint32_t entityHandle)
        {
            const auto* button = TryGetManagedUIButtonComponent(entityHandle);
            return button ? ToManagedVector4(button->HoveredColor) : ManagedVector4{ 0.92f, 0.92f, 0.92f, 1.0f };
        }

        void ManagedSetUIButtonHoveredColorIcall(uint32_t entityHandle, ManagedVector4 value)
        {
            if (auto* button = TryGetManagedUIButtonComponent(entityHandle))
                button->HoveredColor = ToGlmVector4(value);
        }

        ManagedVector4 ManagedGetUIButtonPressedColorIcall(uint32_t entityHandle)
        {
            const auto* button = TryGetManagedUIButtonComponent(entityHandle);
            return button ? ToManagedVector4(button->PressedColor) : ManagedVector4{ 0.72f, 0.72f, 0.72f, 1.0f };
        }

        void ManagedSetUIButtonPressedColorIcall(uint32_t entityHandle, ManagedVector4 value)
        {
            if (auto* button = TryGetManagedUIButtonComponent(entityHandle))
                button->PressedColor = ToGlmVector4(value);
        }

        ManagedVector4 ManagedGetUIButtonDisabledColorIcall(uint32_t entityHandle)
        {
            const auto* button = TryGetManagedUIButtonComponent(entityHandle);
            return button ? ToManagedVector4(button->DisabledColor) : ManagedVector4{ 0.45f, 0.45f, 0.45f, 1.0f };
        }

        void ManagedSetUIButtonDisabledColorIcall(uint32_t entityHandle, ManagedVector4 value)
        {
            if (auto* button = TryGetManagedUIButtonComponent(entityHandle))
                button->DisabledColor = ToGlmVector4(value);
        }

        bool ManagedGetUIButtonIsHoveredIcall(uint32_t entityHandle)
        {
            const auto* button = TryGetManagedUIButtonComponent(entityHandle);
            return button ? button->IsHovered : false;
        }

        bool ManagedGetUIButtonIsPressedIcall(uint32_t entityHandle)
        {
            const auto* button = TryGetManagedUIButtonComponent(entityHandle);
            return button ? button->IsPressed : false;
        }

        Coral::String ManagedGetUIButtonOnClickEventIcall(uint32_t entityHandle)
        {
            const auto* button = TryGetManagedUIButtonComponent(entityHandle);
            return button ? Coral::String::New(button->OnClickEvent) : Coral::String::New("");
        }

        void ManagedSetUIButtonOnClickEventIcall(uint32_t entityHandle, Coral::String value)
        {
            if (auto* button = TryGetManagedUIButtonComponent(entityHandle))
                button->OnClickEvent = ToUtf8Borrowed(value);
        }

        Coral::String ManagedGetUIButtonOnHoverEnterEventIcall(uint32_t entityHandle)
        {
            const auto* button = TryGetManagedUIButtonComponent(entityHandle);
            return button ? Coral::String::New(button->OnHoverEnterEvent) : Coral::String::New("");
        }

        void ManagedSetUIButtonOnHoverEnterEventIcall(uint32_t entityHandle, Coral::String value)
        {
            if (auto* button = TryGetManagedUIButtonComponent(entityHandle))
                button->OnHoverEnterEvent = ToUtf8Borrowed(value);
        }

        Coral::String ManagedGetUIButtonOnHoverExitEventIcall(uint32_t entityHandle)
        {
            const auto* button = TryGetManagedUIButtonComponent(entityHandle);
            return button ? Coral::String::New(button->OnHoverExitEvent) : Coral::String::New("");
        }

        void ManagedSetUIButtonOnHoverExitEventIcall(uint32_t entityHandle, Coral::String value)
        {
            if (auto* button = TryGetManagedUIButtonComponent(entityHandle))
                button->OnHoverExitEvent = ToUtf8Borrowed(value);
        }

        Coral::String ManagedGetUIButtonOnPressedEventIcall(uint32_t entityHandle)
        {
            const auto* button = TryGetManagedUIButtonComponent(entityHandle);
            return button ? Coral::String::New(button->OnPressedEvent) : Coral::String::New("");
        }

        void ManagedSetUIButtonOnPressedEventIcall(uint32_t entityHandle, Coral::String value)
        {
            if (auto* button = TryGetManagedUIButtonComponent(entityHandle))
                button->OnPressedEvent = ToUtf8Borrowed(value);
        }

        bool ManagedHasUISliderComponentIcall(uint32_t entityHandle)
        {
            return TryGetManagedUISliderComponent(entityHandle) != nullptr;
        }

        bool ManagedGetUISliderInteractableIcall(uint32_t entityHandle)
        {
            const auto* slider = TryGetManagedUISliderComponent(entityHandle);
            return slider ? slider->Interactable : true;
        }

        void ManagedSetUISliderInteractableIcall(uint32_t entityHandle, bool value)
        {
            if (auto* slider = TryGetManagedUISliderComponent(entityHandle))
                slider->Interactable = value;
        }

        float ManagedGetUISliderMinValueIcall(uint32_t entityHandle)
        {
            const auto* slider = TryGetManagedUISliderComponent(entityHandle);
            return slider ? slider->MinValue : 0.0f;
        }

        void ManagedSetUISliderMinValueIcall(uint32_t entityHandle, float value)
        {
            if (auto* slider = TryGetManagedUISliderComponent(entityHandle))
                slider->MinValue = value;
        }

        float ManagedGetUISliderMaxValueIcall(uint32_t entityHandle)
        {
            const auto* slider = TryGetManagedUISliderComponent(entityHandle);
            return slider ? slider->MaxValue : 1.0f;
        }

        void ManagedSetUISliderMaxValueIcall(uint32_t entityHandle, float value)
        {
            if (auto* slider = TryGetManagedUISliderComponent(entityHandle))
                slider->MaxValue = value;
        }

        float ManagedGetUISliderValueIcall(uint32_t entityHandle)
        {
            const auto* slider = TryGetManagedUISliderComponent(entityHandle);
            return slider ? slider->Value : 0.0f;
        }

        void ManagedSetUISliderValueIcall(uint32_t entityHandle, float value)
        {
            if (auto* slider = TryGetManagedUISliderComponent(entityHandle))
                slider->Value = value;
        }

        ManagedVector4 ManagedGetUISliderBackgroundColorIcall(uint32_t entityHandle)
        {
            const auto* slider = TryGetManagedUISliderComponent(entityHandle);
            return slider ? ToManagedVector4(slider->BackgroundColor) : ManagedVector4{ 0.22f, 0.22f, 0.22f, 1.0f };
        }

        void ManagedSetUISliderBackgroundColorIcall(uint32_t entityHandle, ManagedVector4 value)
        {
            if (auto* slider = TryGetManagedUISliderComponent(entityHandle))
                slider->BackgroundColor = ToGlmVector4(value);
        }

        ManagedVector4 ManagedGetUISliderFillColorIcall(uint32_t entityHandle)
        {
            const auto* slider = TryGetManagedUISliderComponent(entityHandle);
            return slider ? ToManagedVector4(slider->FillColor) : ManagedVector4{ 0.22f, 0.72f, 1.0f, 0.95f };
        }

        void ManagedSetUISliderFillColorIcall(uint32_t entityHandle, ManagedVector4 value)
        {
            if (auto* slider = TryGetManagedUISliderComponent(entityHandle))
                slider->FillColor = ToGlmVector4(value);
        }

        ManagedVector4 ManagedGetUISliderHandleColorIcall(uint32_t entityHandle)
        {
            const auto* slider = TryGetManagedUISliderComponent(entityHandle);
            return slider ? ToManagedVector4(slider->HandleColor) : ManagedVector4{ 0.92f, 0.92f, 0.92f, 1.0f };
        }

        void ManagedSetUISliderHandleColorIcall(uint32_t entityHandle, ManagedVector4 value)
        {
            if (auto* slider = TryGetManagedUISliderComponent(entityHandle))
                slider->HandleColor = ToGlmVector4(value);
        }

        float ManagedGetUISliderHandleWidthIcall(uint32_t entityHandle)
        {
            const auto* slider = TryGetManagedUISliderComponent(entityHandle);
            return slider ? slider->HandleWidth : 16.0f;
        }

        void ManagedSetUISliderHandleWidthIcall(uint32_t entityHandle, float value)
        {
            if (auto* slider = TryGetManagedUISliderComponent(entityHandle))
                slider->HandleWidth = value;
        }

        float ManagedGetUISliderHandleHeightMultiplierIcall(uint32_t entityHandle)
        {
            const auto* slider = TryGetManagedUISliderComponent(entityHandle);
            return slider ? slider->HandleHeightMultiplier : 1.25f;
        }

        void ManagedSetUISliderHandleHeightMultiplierIcall(uint32_t entityHandle, float value)
        {
            if (auto* slider = TryGetManagedUISliderComponent(entityHandle))
                slider->HandleHeightMultiplier = value;
        }

        bool ManagedGetUISliderShowHandleIcall(uint32_t entityHandle)
        {
            const auto* slider = TryGetManagedUISliderComponent(entityHandle);
            return slider ? slider->ShowHandle : true;
        }

        void ManagedSetUISliderShowHandleIcall(uint32_t entityHandle, bool value)
        {
            if (auto* slider = TryGetManagedUISliderComponent(entityHandle))
                slider->ShowHandle = value;
        }

        bool ManagedGetUISliderRuntimeDraggingIcall(uint32_t entityHandle)
        {
            const auto* slider = TryGetManagedUISliderComponent(entityHandle);
            return slider ? slider->RuntimeDragging : false;
        }

        Coral::String ManagedGetUISliderOnValueChangedEventIcall(uint32_t entityHandle)
        {
            const auto* slider = TryGetManagedUISliderComponent(entityHandle);
            return slider ? Coral::String::New(slider->OnValueChangedEvent) : Coral::String::New("");
        }

        void ManagedSetUISliderOnValueChangedEventIcall(uint32_t entityHandle, Coral::String value)
        {
            if (auto* slider = TryGetManagedUISliderComponent(entityHandle))
                slider->OnValueChangedEvent = ToUtf8Borrowed(value);
        }

        void RegisterRenderingUiInternalCalls(Coral::ManagedAssembly& contractAssembly)
        {
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "HasSpriteComponentIcall", reinterpret_cast<void*>(&ManagedHasSpriteComponentIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetSpriteTextureKeyIcall", reinterpret_cast<void*>(&ManagedGetSpriteTextureKeyIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetSpriteTextureKeyIcall", reinterpret_cast<void*>(&ManagedSetSpriteTextureKeyIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetSpriteColorIcall", reinterpret_cast<void*>(&ManagedGetSpriteColorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetSpriteColorIcall", reinterpret_cast<void*>(&ManagedSetSpriteColorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetSpriteTilingFactorIcall", reinterpret_cast<void*>(&ManagedGetSpriteTilingFactorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetSpriteTilingFactorIcall", reinterpret_cast<void*>(&ManagedSetSpriteTilingFactorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetSpriteRenderOrderIcall", reinterpret_cast<void*>(&ManagedGetSpriteRenderOrderIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetSpriteRenderOrderIcall", reinterpret_cast<void*>(&ManagedSetSpriteRenderOrderIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetSpriteCastShadowsIcall", reinterpret_cast<void*>(&ManagedGetSpriteCastShadowsIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetSpriteCastShadowsIcall", reinterpret_cast<void*>(&ManagedSetSpriteCastShadowsIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetSpriteReceiveShadowsIcall", reinterpret_cast<void*>(&ManagedGetSpriteReceiveShadowsIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetSpriteReceiveShadowsIcall", reinterpret_cast<void*>(&ManagedSetSpriteReceiveShadowsIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetSpriteSubSpriteIndexIcall", reinterpret_cast<void*>(&ManagedGetSpriteSubSpriteIndexIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetSpriteSubSpriteIndexIcall", reinterpret_cast<void*>(&ManagedSetSpriteSubSpriteIndexIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetSpriteUvMinIcall", reinterpret_cast<void*>(&ManagedGetSpriteUvMinIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetSpriteUvMinIcall", reinterpret_cast<void*>(&ManagedSetSpriteUvMinIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetSpriteUvMaxIcall", reinterpret_cast<void*>(&ManagedGetSpriteUvMaxIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetSpriteUvMaxIcall", reinterpret_cast<void*>(&ManagedSetSpriteUvMaxIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "HasMaterialComponentIcall", reinterpret_cast<void*>(&ManagedHasMaterialComponentIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetMaterialKeyIcall", reinterpret_cast<void*>(&ManagedGetMaterialKeyIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetMaterialKeyIcall", reinterpret_cast<void*>(&ManagedSetMaterialKeyIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "HasCanvasComponentIcall", reinterpret_cast<void*>(&ManagedHasCanvasComponentIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetCanvasRenderModeIcall", reinterpret_cast<void*>(&ManagedGetCanvasRenderModeIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetCanvasRenderModeIcall", reinterpret_cast<void*>(&ManagedSetCanvasRenderModeIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetCanvasSortOrderIcall", reinterpret_cast<void*>(&ManagedGetCanvasSortOrderIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetCanvasSortOrderIcall", reinterpret_cast<void*>(&ManagedSetCanvasSortOrderIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetCanvasReferenceResolutionIcall", reinterpret_cast<void*>(&ManagedGetCanvasReferenceResolutionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetCanvasReferenceResolutionIcall", reinterpret_cast<void*>(&ManagedSetCanvasReferenceResolutionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "HasRectTransformComponentIcall", reinterpret_cast<void*>(&ManagedHasRectTransformComponentIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetRectTransformAnchorMinIcall", reinterpret_cast<void*>(&ManagedGetRectTransformAnchorMinIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetRectTransformAnchorMinIcall", reinterpret_cast<void*>(&ManagedSetRectTransformAnchorMinIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetRectTransformAnchorMaxIcall", reinterpret_cast<void*>(&ManagedGetRectTransformAnchorMaxIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetRectTransformAnchorMaxIcall", reinterpret_cast<void*>(&ManagedSetRectTransformAnchorMaxIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetRectTransformPivotIcall", reinterpret_cast<void*>(&ManagedGetRectTransformPivotIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetRectTransformPivotIcall", reinterpret_cast<void*>(&ManagedSetRectTransformPivotIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetRectTransformSizeDeltaIcall", reinterpret_cast<void*>(&ManagedGetRectTransformSizeDeltaIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetRectTransformSizeDeltaIcall", reinterpret_cast<void*>(&ManagedSetRectTransformSizeDeltaIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetRectTransformAnchoredPositionIcall", reinterpret_cast<void*>(&ManagedGetRectTransformAnchoredPositionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetRectTransformAnchoredPositionIcall", reinterpret_cast<void*>(&ManagedSetRectTransformAnchoredPositionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "HasDirectionalLight2DComponentIcall", reinterpret_cast<void*>(&ManagedHasDirectionalLight2DComponentIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetDirectionalLight2DEnabledIcall", reinterpret_cast<void*>(&ManagedGetDirectionalLight2DEnabledIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetDirectionalLight2DEnabledIcall", reinterpret_cast<void*>(&ManagedSetDirectionalLight2DEnabledIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetDirectionalLight2DColorIcall", reinterpret_cast<void*>(&ManagedGetDirectionalLight2DColorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetDirectionalLight2DColorIcall", reinterpret_cast<void*>(&ManagedSetDirectionalLight2DColorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetDirectionalLight2DIntensityIcall", reinterpret_cast<void*>(&ManagedGetDirectionalLight2DIntensityIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetDirectionalLight2DIntensityIcall", reinterpret_cast<void*>(&ManagedSetDirectionalLight2DIntensityIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetDirectionalLight2DUseEntityRotationIcall", reinterpret_cast<void*>(&ManagedGetDirectionalLight2DUseEntityRotationIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetDirectionalLight2DUseEntityRotationIcall", reinterpret_cast<void*>(&ManagedSetDirectionalLight2DUseEntityRotationIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetDirectionalLight2DDirectionIcall", reinterpret_cast<void*>(&ManagedGetDirectionalLight2DDirectionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetDirectionalLight2DDirectionIcall", reinterpret_cast<void*>(&ManagedSetDirectionalLight2DDirectionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetDirectionalLight2DCastShadowsIcall", reinterpret_cast<void*>(&ManagedGetDirectionalLight2DCastShadowsIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetDirectionalLight2DCastShadowsIcall", reinterpret_cast<void*>(&ManagedSetDirectionalLight2DCastShadowsIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetDirectionalLight2DShadowStrengthIcall", reinterpret_cast<void*>(&ManagedGetDirectionalLight2DShadowStrengthIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetDirectionalLight2DShadowStrengthIcall", reinterpret_cast<void*>(&ManagedSetDirectionalLight2DShadowStrengthIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetDirectionalLight2DShadowSoftnessIcall", reinterpret_cast<void*>(&ManagedGetDirectionalLight2DShadowSoftnessIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetDirectionalLight2DShadowSoftnessIcall", reinterpret_cast<void*>(&ManagedSetDirectionalLight2DShadowSoftnessIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetDirectionalLight2DShadowSamplesIcall", reinterpret_cast<void*>(&ManagedGetDirectionalLight2DShadowSamplesIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetDirectionalLight2DShadowSamplesIcall", reinterpret_cast<void*>(&ManagedSetDirectionalLight2DShadowSamplesIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetDirectionalLight2DShadowDistanceIcall", reinterpret_cast<void*>(&ManagedGetDirectionalLight2DShadowDistanceIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetDirectionalLight2DShadowDistanceIcall", reinterpret_cast<void*>(&ManagedSetDirectionalLight2DShadowDistanceIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetDirectionalLight2DShadowBiasIcall", reinterpret_cast<void*>(&ManagedGetDirectionalLight2DShadowBiasIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetDirectionalLight2DShadowBiasIcall", reinterpret_cast<void*>(&ManagedSetDirectionalLight2DShadowBiasIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "HasPointLight2DComponentIcall", reinterpret_cast<void*>(&ManagedHasPointLight2DComponentIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetPointLight2DEnabledIcall", reinterpret_cast<void*>(&ManagedGetPointLight2DEnabledIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetPointLight2DEnabledIcall", reinterpret_cast<void*>(&ManagedSetPointLight2DEnabledIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetPointLight2DColorIcall", reinterpret_cast<void*>(&ManagedGetPointLight2DColorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetPointLight2DColorIcall", reinterpret_cast<void*>(&ManagedSetPointLight2DColorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetPointLight2DIntensityIcall", reinterpret_cast<void*>(&ManagedGetPointLight2DIntensityIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetPointLight2DIntensityIcall", reinterpret_cast<void*>(&ManagedSetPointLight2DIntensityIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetPointLight2DRadiusIcall", reinterpret_cast<void*>(&ManagedGetPointLight2DRadiusIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetPointLight2DRadiusIcall", reinterpret_cast<void*>(&ManagedSetPointLight2DRadiusIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetPointLight2DFalloffIcall", reinterpret_cast<void*>(&ManagedGetPointLight2DFalloffIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetPointLight2DFalloffIcall", reinterpret_cast<void*>(&ManagedSetPointLight2DFalloffIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetPointLight2DCastShadowsIcall", reinterpret_cast<void*>(&ManagedGetPointLight2DCastShadowsIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetPointLight2DCastShadowsIcall", reinterpret_cast<void*>(&ManagedSetPointLight2DCastShadowsIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetPointLight2DShadowStrengthIcall", reinterpret_cast<void*>(&ManagedGetPointLight2DShadowStrengthIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetPointLight2DShadowStrengthIcall", reinterpret_cast<void*>(&ManagedSetPointLight2DShadowStrengthIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetPointLight2DShadowSoftnessIcall", reinterpret_cast<void*>(&ManagedGetPointLight2DShadowSoftnessIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetPointLight2DShadowSoftnessIcall", reinterpret_cast<void*>(&ManagedSetPointLight2DShadowSoftnessIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetPointLight2DShadowSamplesIcall", reinterpret_cast<void*>(&ManagedGetPointLight2DShadowSamplesIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetPointLight2DShadowSamplesIcall", reinterpret_cast<void*>(&ManagedSetPointLight2DShadowSamplesIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetPointLight2DShadowBiasIcall", reinterpret_cast<void*>(&ManagedGetPointLight2DShadowBiasIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetPointLight2DShadowBiasIcall", reinterpret_cast<void*>(&ManagedSetPointLight2DShadowBiasIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "HasUIImageComponentIcall", reinterpret_cast<void*>(&ManagedHasUIImageComponentIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetUIImageRaycastTargetIcall", reinterpret_cast<void*>(&ManagedGetUIImageRaycastTargetIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetUIImageRaycastTargetIcall", reinterpret_cast<void*>(&ManagedSetUIImageRaycastTargetIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "HasUIPanelComponentIcall", reinterpret_cast<void*>(&ManagedHasUIPanelComponentIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetUIPanelBackgroundColorIcall", reinterpret_cast<void*>(&ManagedGetUIPanelBackgroundColorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetUIPanelBackgroundColorIcall", reinterpret_cast<void*>(&ManagedSetUIPanelBackgroundColorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetUIPanelUseSpriteTextureIcall", reinterpret_cast<void*>(&ManagedGetUIPanelUseSpriteTextureIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetUIPanelUseSpriteTextureIcall", reinterpret_cast<void*>(&ManagedSetUIPanelUseSpriteTextureIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetUIPanelRaycastTargetIcall", reinterpret_cast<void*>(&ManagedGetUIPanelRaycastTargetIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetUIPanelRaycastTargetIcall", reinterpret_cast<void*>(&ManagedSetUIPanelRaycastTargetIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "HasUITextComponentIcall", reinterpret_cast<void*>(&ManagedHasUITextComponentIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetUITextValueIcall", reinterpret_cast<void*>(&ManagedGetUITextValueIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetUITextValueIcall", reinterpret_cast<void*>(&ManagedSetUITextValueIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetUITextFontFilePathIcall", reinterpret_cast<void*>(&ManagedGetUITextFontFilePathIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetUITextFontFilePathIcall", reinterpret_cast<void*>(&ManagedSetUITextFontFilePathIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetUITextFontSizeIcall", reinterpret_cast<void*>(&ManagedGetUITextFontSizeIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetUITextFontSizeIcall", reinterpret_cast<void*>(&ManagedSetUITextFontSizeIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetUITextColorIcall", reinterpret_cast<void*>(&ManagedGetUITextColorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetUITextColorIcall", reinterpret_cast<void*>(&ManagedSetUITextColorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetUITextRaycastTargetIcall", reinterpret_cast<void*>(&ManagedGetUITextRaycastTargetIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetUITextRaycastTargetIcall", reinterpret_cast<void*>(&ManagedSetUITextRaycastTargetIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "HasUIButtonComponentIcall", reinterpret_cast<void*>(&ManagedHasUIButtonComponentIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetUIButtonInteractableIcall", reinterpret_cast<void*>(&ManagedGetUIButtonInteractableIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetUIButtonInteractableIcall", reinterpret_cast<void*>(&ManagedSetUIButtonInteractableIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetUIButtonUseStateColorsIcall", reinterpret_cast<void*>(&ManagedGetUIButtonUseStateColorsIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetUIButtonUseStateColorsIcall", reinterpret_cast<void*>(&ManagedSetUIButtonUseStateColorsIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetUIButtonNormalColorIcall", reinterpret_cast<void*>(&ManagedGetUIButtonNormalColorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetUIButtonNormalColorIcall", reinterpret_cast<void*>(&ManagedSetUIButtonNormalColorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetUIButtonHoveredColorIcall", reinterpret_cast<void*>(&ManagedGetUIButtonHoveredColorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetUIButtonHoveredColorIcall", reinterpret_cast<void*>(&ManagedSetUIButtonHoveredColorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetUIButtonPressedColorIcall", reinterpret_cast<void*>(&ManagedGetUIButtonPressedColorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetUIButtonPressedColorIcall", reinterpret_cast<void*>(&ManagedSetUIButtonPressedColorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetUIButtonDisabledColorIcall", reinterpret_cast<void*>(&ManagedGetUIButtonDisabledColorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetUIButtonDisabledColorIcall", reinterpret_cast<void*>(&ManagedSetUIButtonDisabledColorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetUIButtonIsHoveredIcall", reinterpret_cast<void*>(&ManagedGetUIButtonIsHoveredIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetUIButtonIsPressedIcall", reinterpret_cast<void*>(&ManagedGetUIButtonIsPressedIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetUIButtonOnClickEventIcall", reinterpret_cast<void*>(&ManagedGetUIButtonOnClickEventIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetUIButtonOnClickEventIcall", reinterpret_cast<void*>(&ManagedSetUIButtonOnClickEventIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetUIButtonOnHoverEnterEventIcall", reinterpret_cast<void*>(&ManagedGetUIButtonOnHoverEnterEventIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetUIButtonOnHoverEnterEventIcall", reinterpret_cast<void*>(&ManagedSetUIButtonOnHoverEnterEventIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetUIButtonOnHoverExitEventIcall", reinterpret_cast<void*>(&ManagedGetUIButtonOnHoverExitEventIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetUIButtonOnHoverExitEventIcall", reinterpret_cast<void*>(&ManagedSetUIButtonOnHoverExitEventIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetUIButtonOnPressedEventIcall", reinterpret_cast<void*>(&ManagedGetUIButtonOnPressedEventIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetUIButtonOnPressedEventIcall", reinterpret_cast<void*>(&ManagedSetUIButtonOnPressedEventIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "HasUISliderComponentIcall", reinterpret_cast<void*>(&ManagedHasUISliderComponentIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetUISliderInteractableIcall", reinterpret_cast<void*>(&ManagedGetUISliderInteractableIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetUISliderInteractableIcall", reinterpret_cast<void*>(&ManagedSetUISliderInteractableIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetUISliderMinValueIcall", reinterpret_cast<void*>(&ManagedGetUISliderMinValueIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetUISliderMinValueIcall", reinterpret_cast<void*>(&ManagedSetUISliderMinValueIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetUISliderMaxValueIcall", reinterpret_cast<void*>(&ManagedGetUISliderMaxValueIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetUISliderMaxValueIcall", reinterpret_cast<void*>(&ManagedSetUISliderMaxValueIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetUISliderValueIcall", reinterpret_cast<void*>(&ManagedGetUISliderValueIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetUISliderValueIcall", reinterpret_cast<void*>(&ManagedSetUISliderValueIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetUISliderBackgroundColorIcall", reinterpret_cast<void*>(&ManagedGetUISliderBackgroundColorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetUISliderBackgroundColorIcall", reinterpret_cast<void*>(&ManagedSetUISliderBackgroundColorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetUISliderFillColorIcall", reinterpret_cast<void*>(&ManagedGetUISliderFillColorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetUISliderFillColorIcall", reinterpret_cast<void*>(&ManagedSetUISliderFillColorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetUISliderHandleColorIcall", reinterpret_cast<void*>(&ManagedGetUISliderHandleColorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetUISliderHandleColorIcall", reinterpret_cast<void*>(&ManagedSetUISliderHandleColorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetUISliderHandleWidthIcall", reinterpret_cast<void*>(&ManagedGetUISliderHandleWidthIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetUISliderHandleWidthIcall", reinterpret_cast<void*>(&ManagedSetUISliderHandleWidthIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetUISliderHandleHeightMultiplierIcall", reinterpret_cast<void*>(&ManagedGetUISliderHandleHeightMultiplierIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetUISliderHandleHeightMultiplierIcall", reinterpret_cast<void*>(&ManagedSetUISliderHandleHeightMultiplierIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetUISliderShowHandleIcall", reinterpret_cast<void*>(&ManagedGetUISliderShowHandleIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetUISliderShowHandleIcall", reinterpret_cast<void*>(&ManagedSetUISliderShowHandleIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetUISliderRuntimeDraggingIcall", reinterpret_cast<void*>(&ManagedGetUISliderRuntimeDraggingIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetUISliderOnValueChangedEventIcall", reinterpret_cast<void*>(&ManagedGetUISliderOnValueChangedEventIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetUISliderOnValueChangedEventIcall", reinterpret_cast<void*>(&ManagedSetUISliderOnValueChangedEventIcall));
        }
    }
}
