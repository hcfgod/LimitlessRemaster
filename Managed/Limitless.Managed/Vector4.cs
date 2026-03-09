namespace Limitless.Managed;

public struct Vector4
{
    public float X;
    public float Y;
    public float Z;
    public float W;

    public Vector4(float x, float y, float z, float w)
    {
        X = x;
        Y = y;
        Z = z;
        W = w;
    }

    public static Vector4 Zero => new(0.0f, 0.0f, 0.0f, 0.0f);
    public static Vector4 One => new(1.0f, 1.0f, 1.0f, 1.0f);

    public override string ToString()
    {
        return $"({X}, {Y}, {Z}, {W})";
    }
}
