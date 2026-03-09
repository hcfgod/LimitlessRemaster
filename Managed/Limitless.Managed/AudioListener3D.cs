namespace Limitless.Managed;

public sealed class AudioListener3D : EntityComponent
{
    internal AudioListener3D(uint entityHandle)
        : base(entityHandle)
    {
    }

    public bool Enabled
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetAudioListener3DEnabledIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetAudioListener3DEnabledIcall(EntityHandle, value);
            }
        }
    }

    public bool UsePrimaryCameraTransform
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetAudioListener3DUsePrimaryCameraTransformIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetAudioListener3DUsePrimaryCameraTransformIcall(EntityHandle, value);
            }
        }
    }
}
