namespace Limitless.Managed;

public sealed class RandomApi
{
    private static readonly RandomApi s_Shared = new();

    public static RandomApi Shared => s_Shared;

    private RandomApi()
    {
    }

    public float Value
    {
        get
        {
            unsafe
            {
                return ScriptBridge.RandomValueIcall();
            }
        }
    }

    public float value => Value;

    public void InitState(int seed)
    {
        unsafe
        {
            ScriptBridge.SetRandomSeedIcall(unchecked((uint)seed));
        }
    }

    public int Range(int minInclusive, int maxExclusive)
    {
        unsafe
        {
            return ScriptBridge.RandomRangeIntIcall(minInclusive, maxExclusive);
        }
    }

    public float Range(float minInclusive, float maxInclusive)
    {
        unsafe
        {
            return ScriptBridge.RandomRangeFloatIcall(minInclusive, maxInclusive);
        }
    }
}
