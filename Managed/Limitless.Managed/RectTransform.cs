namespace Limitless.Managed;

public sealed class RectTransform : EntityComponent
{
    internal RectTransform(uint entityHandle)
        : base(entityHandle)
    {
    }

    public Vector2 AnchorMin
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetRectTransformAnchorMinIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetRectTransformAnchorMinIcall(EntityHandle, value);
            }
        }
    }

    public Vector2 AnchorMax
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetRectTransformAnchorMaxIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetRectTransformAnchorMaxIcall(EntityHandle, value);
            }
        }
    }

    public Vector2 Pivot
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetRectTransformPivotIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetRectTransformPivotIcall(EntityHandle, value);
            }
        }
    }

    public Vector2 SizeDelta
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetRectTransformSizeDeltaIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetRectTransformSizeDeltaIcall(EntityHandle, value);
            }
        }
    }

    public Vector2 AnchoredPosition
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetRectTransformAnchoredPositionIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetRectTransformAnchoredPositionIcall(EntityHandle, value);
            }
        }
    }
}
