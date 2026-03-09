namespace Limitless.Managed;

public sealed class Canvas : EntityComponent
{
    internal Canvas(uint entityHandle)
        : base(entityHandle)
    {
    }

    public CanvasRenderMode RenderMode
    {
        get
        {
            unsafe
            {
                return (CanvasRenderMode)ScriptBridge.GetCanvasRenderModeIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetCanvasRenderModeIcall(EntityHandle, (int)value);
            }
        }
    }

    public int SortOrder
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetCanvasSortOrderIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetCanvasSortOrderIcall(EntityHandle, value);
            }
        }
    }

    public Vector2 ReferenceResolution
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetCanvasReferenceResolutionIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetCanvasReferenceResolutionIcall(EntityHandle, value);
            }
        }
    }
}
