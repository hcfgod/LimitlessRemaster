namespace Limitless.Managed;

public sealed class UIImage : EntityComponent
{
    internal UIImage(uint entityHandle)
        : base(entityHandle)
    {
    }

    public bool RaycastTarget
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetUIImageRaycastTargetIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetUIImageRaycastTargetIcall(EntityHandle, value);
            }
        }
    }
}
