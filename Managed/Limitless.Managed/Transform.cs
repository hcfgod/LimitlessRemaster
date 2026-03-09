namespace Limitless.Managed;

public sealed class Transform : EntityComponent
{
    internal Transform(uint entityHandle)
        : base(entityHandle)
    {
    }

    public Vector3 Position
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetTransformPositionIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetTransformPositionIcall(EntityHandle, value);
            }
        }
    }

    public Vector3 Rotation
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetTransformRotationIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetTransformRotationIcall(EntityHandle, value);
            }
        }
    }

    public Vector3 Scale
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetTransformScaleIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetTransformScaleIcall(EntityHandle, value);
            }
        }
    }
}
