using Coral.Managed.Interop;

namespace Limitless.Managed;

public sealed class UIButton : EntityComponent
{
    internal UIButton(uint entityHandle)
        : base(entityHandle)
    {
    }

    public bool Interactable
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetUIButtonInteractableIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetUIButtonInteractableIcall(EntityHandle, value);
            }
        }
    }

    public bool UseStateColors
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetUIButtonUseStateColorsIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetUIButtonUseStateColorsIcall(EntityHandle, value);
            }
        }
    }

    public Vector4 NormalColor
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetUIButtonNormalColorIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetUIButtonNormalColorIcall(EntityHandle, value);
            }
        }
    }

    public Vector4 HoveredColor
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetUIButtonHoveredColorIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetUIButtonHoveredColorIcall(EntityHandle, value);
            }
        }
    }

    public Vector4 PressedColor
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetUIButtonPressedColorIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetUIButtonPressedColorIcall(EntityHandle, value);
            }
        }
    }

    public Vector4 DisabledColor
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetUIButtonDisabledColorIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetUIButtonDisabledColorIcall(EntityHandle, value);
            }
        }
    }

    public bool IsHovered
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetUIButtonIsHoveredIcall(EntityHandle);
            }
        }
    }

    public bool IsPressed
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetUIButtonIsPressedIcall(EntityHandle);
            }
        }
    }

    public string OnClickEvent
    {
        get
        {
            unsafe
            {
                return GetString(ScriptBridge.GetUIButtonOnClickEventIcall);
            }
        }
        set
        {
            unsafe
            {
                SetString(ScriptBridge.SetUIButtonOnClickEventIcall, value);
            }
        }
    }

    public string OnHoverEnterEvent
    {
        get
        {
            unsafe
            {
                return GetString(ScriptBridge.GetUIButtonOnHoverEnterEventIcall);
            }
        }
        set
        {
            unsafe
            {
                SetString(ScriptBridge.SetUIButtonOnHoverEnterEventIcall, value);
            }
        }
    }

    public string OnHoverExitEvent
    {
        get
        {
            unsafe
            {
                return GetString(ScriptBridge.GetUIButtonOnHoverExitEventIcall);
            }
        }
        set
        {
            unsafe
            {
                SetString(ScriptBridge.SetUIButtonOnHoverExitEventIcall, value);
            }
        }
    }

    public string OnPressedEvent
    {
        get
        {
            unsafe
            {
                return GetString(ScriptBridge.GetUIButtonOnPressedEventIcall);
            }
        }
        set
        {
            unsafe
            {
                SetString(ScriptBridge.SetUIButtonOnPressedEventIcall, value);
            }
        }
    }

    private unsafe string GetString(delegate*<uint, NativeString> getter)
    {
        NativeString nativeValue = getter(EntityHandle);
        try
        {
            return nativeValue.ToString() ?? string.Empty;
        }
        finally
        {
            nativeValue.Dispose();
        }
    }

    private unsafe void SetString(delegate*<uint, NativeString, void> setter, string value)
    {
        NativeString nativeValue = value ?? string.Empty;
        try
        {
            setter(EntityHandle, nativeValue);
        }
        finally
        {
            nativeValue.Dispose();
        }
    }
}
