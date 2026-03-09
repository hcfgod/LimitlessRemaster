namespace Limitless.Managed;

public sealed class AudioListener2D : EntityComponent
{
    internal AudioListener2D(uint entityHandle)
        : base(entityHandle)
    {
    }

    public bool Enabled
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetAudioListener2DEnabledIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetAudioListener2DEnabledIcall(EntityHandle, value);
            }
        }
    }

    public bool UsePrimaryCameraPosition
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetAudioListener2DUsePrimaryCameraPositionIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetAudioListener2DUsePrimaryCameraPositionIcall(EntityHandle, value);
            }
        }
    }
}
