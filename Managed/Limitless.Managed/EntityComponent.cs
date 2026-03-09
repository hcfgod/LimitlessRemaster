namespace Limitless.Managed;

public abstract class EntityComponent
{
    protected EntityComponent(uint entityHandle)
    {
        EntityHandle = entityHandle;
    }

    public uint EntityHandle { get; }
    public Entity Entity => new(EntityHandle);
}

internal static class EntityComponentResolver
{
    public static bool HasComponent<T>(uint entityHandle) where T : EntityComponent
    {
        unsafe
        {
            if (typeof(T) == typeof(Transform))
                return ScriptBridge.HasTransformComponentIcall(entityHandle);
            if (typeof(T) == typeof(Camera))
                return ScriptBridge.HasCameraComponentIcall(entityHandle);
            if (typeof(T) == typeof(AudioListener2D))
                return ScriptBridge.HasAudioListener2DComponentIcall(entityHandle);
            if (typeof(T) == typeof(AudioListener3D))
                return ScriptBridge.HasAudioListener3DComponentIcall(entityHandle);
            if (typeof(T) == typeof(AudioSource))
                return ScriptBridge.HasAudioSourceComponentIcall(entityHandle);
            if (typeof(T) == typeof(Animator))
                return ScriptBridge.HasAnimatorComponentIcall(entityHandle);
            if (typeof(T) == typeof(AnimationEventReceiver))
                return ScriptBridge.HasAnimationEventReceiverComponentIcall(entityHandle);
            if (typeof(T) == typeof(ParticleEmitter))
                return ScriptBridge.HasParticleEmitterComponentIcall(entityHandle);
            if (typeof(T) == typeof(Grid2D))
                return ScriptBridge.HasGrid2DComponentIcall(entityHandle);
            if (typeof(T) == typeof(TilemapLayer))
                return ScriptBridge.HasTilemapLayerComponentIcall(entityHandle);
            if (typeof(T) == typeof(Sprite))
                return ScriptBridge.HasSpriteComponentIcall(entityHandle);
            if (typeof(T) == typeof(Material))
                return ScriptBridge.HasMaterialComponentIcall(entityHandle);
            if (typeof(T) == typeof(Canvas))
                return ScriptBridge.HasCanvasComponentIcall(entityHandle);
            if (typeof(T) == typeof(RectTransform))
                return ScriptBridge.HasRectTransformComponentIcall(entityHandle);
            if (typeof(T) == typeof(DirectionalLight2D))
                return ScriptBridge.HasDirectionalLight2DComponentIcall(entityHandle);
            if (typeof(T) == typeof(PointLight2D))
                return ScriptBridge.HasPointLight2DComponentIcall(entityHandle);
            if (typeof(T) == typeof(UIImage))
                return ScriptBridge.HasUIImageComponentIcall(entityHandle);
            if (typeof(T) == typeof(UIPanel))
                return ScriptBridge.HasUIPanelComponentIcall(entityHandle);
            if (typeof(T) == typeof(UIText))
                return ScriptBridge.HasUITextComponentIcall(entityHandle);
            if (typeof(T) == typeof(UIButton))
                return ScriptBridge.HasUIButtonComponentIcall(entityHandle);
            if (typeof(T) == typeof(UISlider))
                return ScriptBridge.HasUISliderComponentIcall(entityHandle);
            if (typeof(T) == typeof(BoxCollider2D))
                return ScriptBridge.HasBoxCollider2DComponentIcall(entityHandle);
            if (typeof(T) == typeof(CircleCollider2D))
                return ScriptBridge.HasCircleCollider2DComponentIcall(entityHandle);
            if (typeof(T) == typeof(PolygonCollider2D))
                return ScriptBridge.HasPolygonCollider2DComponentIcall(entityHandle);
            if (typeof(T) == typeof(EdgeCollider2D))
                return ScriptBridge.HasEdgeCollider2DComponentIcall(entityHandle);
            if (typeof(T) == typeof(CapsuleCollider2D))
                return ScriptBridge.HasCapsuleCollider2DComponentIcall(entityHandle);
            if (typeof(T) == typeof(Joint2D))
                return ScriptBridge.HasJoint2DComponentIcall(entityHandle);
            if (typeof(T) == typeof(Rigidbody2D))
                return ScriptBridge.HasRigidbody2DComponentIcall(entityHandle);
        }

        throw new System.NotSupportedException($"Managed component type '{typeof(T).FullName}' is not supported.");
    }

    public static T? GetComponent<T>(uint entityHandle) where T : EntityComponent
    {
        if (!HasComponent<T>(entityHandle))
            return null;

        return CreateComponent<T>(entityHandle);
    }

    public static bool TryGetComponent<T>(uint entityHandle, out T component) where T : EntityComponent
    {
        T? resolvedComponent = GetComponent<T>(entityHandle);
        if (resolvedComponent != null)
        {
            component = resolvedComponent;
            return true;
        }

        component = null!;
        return false;
    }

    private static T CreateComponent<T>(uint entityHandle) where T : EntityComponent
    {
        if (typeof(T) == typeof(Transform))
            return (T)(object)new Transform(entityHandle);
        if (typeof(T) == typeof(Camera))
            return (T)(object)new Camera(entityHandle);
        if (typeof(T) == typeof(AudioListener2D))
            return (T)(object)new AudioListener2D(entityHandle);
        if (typeof(T) == typeof(AudioListener3D))
            return (T)(object)new AudioListener3D(entityHandle);
        if (typeof(T) == typeof(AudioSource))
            return (T)(object)new AudioSource(entityHandle);
        if (typeof(T) == typeof(Animator))
            return (T)(object)new Animator(entityHandle);
        if (typeof(T) == typeof(AnimationEventReceiver))
            return (T)(object)new AnimationEventReceiver(entityHandle);
        if (typeof(T) == typeof(ParticleEmitter))
            return (T)(object)new ParticleEmitter(entityHandle);
        if (typeof(T) == typeof(Grid2D))
            return (T)(object)new Grid2D(entityHandle);
        if (typeof(T) == typeof(TilemapLayer))
            return (T)(object)new TilemapLayer(entityHandle);
        if (typeof(T) == typeof(Sprite))
            return (T)(object)new Sprite(entityHandle);
        if (typeof(T) == typeof(Material))
            return (T)(object)new Material(entityHandle);
        if (typeof(T) == typeof(Canvas))
            return (T)(object)new Canvas(entityHandle);
        if (typeof(T) == typeof(RectTransform))
            return (T)(object)new RectTransform(entityHandle);
        if (typeof(T) == typeof(DirectionalLight2D))
            return (T)(object)new DirectionalLight2D(entityHandle);
        if (typeof(T) == typeof(PointLight2D))
            return (T)(object)new PointLight2D(entityHandle);
        if (typeof(T) == typeof(UIImage))
            return (T)(object)new UIImage(entityHandle);
        if (typeof(T) == typeof(UIPanel))
            return (T)(object)new UIPanel(entityHandle);
        if (typeof(T) == typeof(UIText))
            return (T)(object)new UIText(entityHandle);
        if (typeof(T) == typeof(UIButton))
            return (T)(object)new UIButton(entityHandle);
        if (typeof(T) == typeof(UISlider))
            return (T)(object)new UISlider(entityHandle);
        if (typeof(T) == typeof(BoxCollider2D))
            return (T)(object)new BoxCollider2D(entityHandle);
        if (typeof(T) == typeof(CircleCollider2D))
            return (T)(object)new CircleCollider2D(entityHandle);
        if (typeof(T) == typeof(PolygonCollider2D))
            return (T)(object)new PolygonCollider2D(entityHandle);
        if (typeof(T) == typeof(EdgeCollider2D))
            return (T)(object)new EdgeCollider2D(entityHandle);
        if (typeof(T) == typeof(CapsuleCollider2D))
            return (T)(object)new CapsuleCollider2D(entityHandle);
        if (typeof(T) == typeof(Joint2D))
            return (T)(object)new Joint2D(entityHandle);
        if (typeof(T) == typeof(Rigidbody2D))
            return (T)(object)new Rigidbody2D(entityHandle);

        throw new System.NotSupportedException($"Managed component type '{typeof(T).FullName}' is not supported.");
    }
}
