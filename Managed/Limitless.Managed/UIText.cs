using Coral.Managed.Interop;

namespace Limitless.Managed;

public sealed class UIText : EntityComponent
{
    internal UIText(uint entityHandle)
        : base(entityHandle)
    {
    }

    public string Value
    {
        get
        {
            unsafe
            {
                NativeString nativeValue = ScriptBridge.GetUITextValueIcall(EntityHandle);
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
                    ScriptBridge.SetUITextValueIcall(EntityHandle, nativeValue);
                }
                finally
                {
                    nativeValue.Dispose();
                }
            }
        }
    }

    public string FontFilePath
    {
        get
        {
            unsafe
            {
                NativeString nativeValue = ScriptBridge.GetUITextFontFilePathIcall(EntityHandle);
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
                    ScriptBridge.SetUITextFontFilePathIcall(EntityHandle, nativeValue);
                }
                finally
                {
                    nativeValue.Dispose();
                }
            }
        }
    }

    public float FontSize
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetUITextFontSizeIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetUITextFontSizeIcall(EntityHandle, value);
            }
        }
    }

    public Vector4 Color
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetUITextColorIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetUITextColorIcall(EntityHandle, value);
            }
        }
    }

    public bool RaycastTarget
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetUITextRaycastTargetIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetUITextRaycastTargetIcall(EntityHandle, value);
            }
        }
    }
}
