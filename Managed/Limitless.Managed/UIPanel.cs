namespace Limitless.Managed;

public sealed class UIPanel : EntityComponent
{
    internal UIPanel(uint entityHandle)
        : base(entityHandle)
    {
    }

    public Vector4 BackgroundColor
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetUIPanelBackgroundColorIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetUIPanelBackgroundColorIcall(EntityHandle, value);
            }
        }
    }

    public bool UseSpriteTexture
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetUIPanelUseSpriteTextureIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetUIPanelUseSpriteTextureIcall(EntityHandle, value);
            }
        }
    }

    public bool RaycastTarget
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetUIPanelRaycastTargetIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetUIPanelRaycastTargetIcall(EntityHandle, value);
            }
        }
    }
}
