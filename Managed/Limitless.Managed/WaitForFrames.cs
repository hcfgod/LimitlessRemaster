namespace Limitless.Managed;

public readonly struct WaitForFrames
{
    public WaitForFrames(int frameCount)
    {
        FrameCount = frameCount;
    }

    public int FrameCount { get; }

    public static WaitForFrames NextFrame(int frameCount = 1)
    {
        return new WaitForFrames(frameCount);
    }
}
