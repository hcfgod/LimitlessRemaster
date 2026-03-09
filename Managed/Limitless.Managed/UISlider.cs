using Coral.Managed.Interop;

namespace Limitless.Managed;

public sealed class UISlider : EntityComponent
{
    internal UISlider(uint entityHandle)
        : base(entityHandle)
    {
    }

    public bool Interactable
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetUISliderInteractableIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetUISliderInteractableIcall(EntityHandle, value);
            }
        }
    }

    public float MinValue
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetUISliderMinValueIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetUISliderMinValueIcall(EntityHandle, value);
            }
        }
    }

    public float MaxValue
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetUISliderMaxValueIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetUISliderMaxValueIcall(EntityHandle, value);
            }
        }
    }

    public float Value
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetUISliderValueIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetUISliderValueIcall(EntityHandle, value);
            }
        }
    }

    public Vector4 BackgroundColor
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetUISliderBackgroundColorIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetUISliderBackgroundColorIcall(EntityHandle, value);
            }
        }
    }

    public Vector4 FillColor
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetUISliderFillColorIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetUISliderFillColorIcall(EntityHandle, value);
            }
        }
    }

    public Vector4 HandleColor
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetUISliderHandleColorIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetUISliderHandleColorIcall(EntityHandle, value);
            }
        }
    }

    public float HandleWidth
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetUISliderHandleWidthIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetUISliderHandleWidthIcall(EntityHandle, value);
            }
        }
    }

    public float HandleHeightMultiplier
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetUISliderHandleHeightMultiplierIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetUISliderHandleHeightMultiplierIcall(EntityHandle, value);
            }
        }
    }

    public bool ShowHandle
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetUISliderShowHandleIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetUISliderShowHandleIcall(EntityHandle, value);
            }
        }
    }

    public bool IsDragging
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetUISliderRuntimeDraggingIcall(EntityHandle);
            }
        }
    }

    public string OnValueChangedEvent
    {
        get
        {
            unsafe
            {
                NativeString nativeValue = ScriptBridge.GetUISliderOnValueChangedEventIcall(EntityHandle);
                try
                {
                    return nativeValue.ToString() ?? string.Empty;
                }
                finally
                {
                    nativeValue.Dispose();
                }
            }
        }
        set
        {
            unsafe
            {
                NativeString nativeValue = value ?? string.Empty;
                try
                {
                    ScriptBridge.SetUISliderOnValueChangedEventIcall(EntityHandle, nativeValue);
                }
                finally
                {
                    nativeValue.Dispose();
                }
            }
        }
    }
}
