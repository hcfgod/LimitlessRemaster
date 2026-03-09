namespace Limitless.Managed;

public sealed class Camera : EntityComponent
{
    internal Camera(uint entityHandle)
        : base(entityHandle)
    {
    }

    public CameraProjectionType Projection
    {
        get
        {
            unsafe
            {
                return (CameraProjectionType)ScriptBridge.GetCameraProjectionIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetCameraProjectionIcall(EntityHandle, (int)value);
            }
        }
    }

    public bool IsPrimary
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetCameraPrimaryIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetCameraPrimaryIcall(EntityHandle, value);
            }
        }
    }

    public float Zoom
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetCameraZoomIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetCameraZoomIcall(EntityHandle, value);
            }
        }
    }

    public float NearPlane
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetCameraNearPlaneIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetCameraNearPlaneIcall(EntityHandle, value);
            }
        }
    }

    public float FarPlane
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetCameraFarPlaneIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetCameraFarPlaneIcall(EntityHandle, value);
            }
        }
    }

    public float FieldOfViewYDegrees
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetCameraFieldOfViewYDegreesIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetCameraFieldOfViewYDegreesIcall(EntityHandle, value);
            }
        }
    }
}
