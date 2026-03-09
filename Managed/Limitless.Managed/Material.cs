using Coral.Managed.Interop;

namespace Limitless.Managed;

public sealed class Material : EntityComponent
{
    internal Material(uint entityHandle)
        : base(entityHandle)
    {
    }

    public string MaterialKey
    {
        get
        {
            unsafe
            {
                NativeString nativeValue = ScriptBridge.GetMaterialKeyIcall(EntityHandle);
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
                    ScriptBridge.SetMaterialKeyIcall(EntityHandle, nativeValue);
                }
                finally
                {
                    nativeValue.Dispose();
                }
            }
        }
    }
}
