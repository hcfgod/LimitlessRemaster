using System.Runtime.InteropServices;

namespace Limitless.Managed;

[StructLayout(LayoutKind.Sequential)]
public struct Vector2
{
    public float X;
    public float Y;

    public Vector2(float x, float y)
    {
        X = x;
        Y = y;
    }

    public static Vector2 Zero => new(0.0f, 0.0f);
    public static Vector2 One => new(1.0f, 1.0f);

    public static Vector2 operator +(Vector2 left, Vector2 right)
    {
        return new(left.X + right.X, left.Y + right.Y);
    }

    public static Vector2 operator -(Vector2 left, Vector2 right)
    {
        return new(left.X - right.X, left.Y - right.Y);
    }

    public static Vector2 operator *(Vector2 value, float scalar)
    {
        return new(value.X * scalar, value.Y * scalar);
    }

    public static Vector2 operator /(Vector2 value, float scalar)
    {
        return new(value.X / scalar, value.Y / scalar);
    }

    public override string ToString()
    {
        return $"({X}, {Y})";
    }
}
