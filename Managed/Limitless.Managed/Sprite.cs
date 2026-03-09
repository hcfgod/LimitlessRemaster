using Coral.Managed.Interop;

namespace Limitless.Managed;

public sealed class Sprite : EntityComponent
{
    internal Sprite(uint entityHandle)
        : base(entityHandle)
    {
    }

    public string TextureKey
    {
        get
        {
            unsafe
            {
                NativeString nativeValue = ScriptBridge.GetSpriteTextureKeyIcall(EntityHandle);
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
                    ScriptBridge.SetSpriteTextureKeyIcall(EntityHandle, nativeValue);
                }
                finally
                {
                    nativeValue.Dispose();
                }
            }
        }
    }

    public Vector4 Color
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetSpriteColorIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetSpriteColorIcall(EntityHandle, value);
            }
        }
    }

    public Vector2 TilingFactor
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetSpriteTilingFactorIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetSpriteTilingFactorIcall(EntityHandle, value);
            }
        }
    }

    public int RenderOrder
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetSpriteRenderOrderIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetSpriteRenderOrderIcall(EntityHandle, value);
            }
        }
    }

    public bool CastShadows
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetSpriteCastShadowsIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetSpriteCastShadowsIcall(EntityHandle, value);
            }
        }
    }

    public bool ReceiveShadows
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetSpriteReceiveShadowsIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetSpriteReceiveShadowsIcall(EntityHandle, value);
            }
        }
    }

    public int SubSpriteIndex
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetSpriteSubSpriteIndexIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetSpriteSubSpriteIndexIcall(EntityHandle, value);
            }
        }
    }

    public Vector2 UvMin
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetSpriteUvMinIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetSpriteUvMinIcall(EntityHandle, value);
            }
        }
    }

    public Vector2 UvMax
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetSpriteUvMaxIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetSpriteUvMaxIcall(EntityHandle, value);
            }
        }
    }
}
