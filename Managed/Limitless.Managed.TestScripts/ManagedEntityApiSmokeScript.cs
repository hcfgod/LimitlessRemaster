using System.Collections;
using Limitless.Managed;

namespace Limitless.Managed.TestScripts;

public sealed class ManagedEntityApiSmokeScript : ScriptableEntity
{
    private Vector3 m_OriginalPosition;
    private bool m_HasTransform;
    private string m_OriginalTag = string.Empty;
    private Coroutine? m_SmokeCoroutine;
    private int m_CoroutineStepCount;
    private int m_LastRandomInt;
    private float m_LastRandomFloat;
    private float m_LastRandomValue;
    private bool m_StopCoroutineSucceeded;
    private bool m_StopCoroutineRunningAfterStop;

    public override void OnCreate()
    {
        LogInfo("EntityApi step 0 start");
        bool entityHasTransform = Entity.HasComponent<Transform>();
        LogInfo($"EntityApi step 1 entityHasTransform={entityHasTransform}");
        Transform? transformFromEntity = Entity.GetComponent<Transform>();
        LogInfo($"EntityApi step 2 entityGetTransform={(transformFromEntity != null)}");
        m_HasTransform = TryGetComponent(out Transform transform);
        LogInfo($"EntityApi step 3 tryGetTransform={m_HasTransform}");
        m_OriginalTag = Entity.Tag;
        LogInfo($"EntityApi step 4 tag='{m_OriginalTag}'");
        m_OriginalPosition = m_HasTransform ? transform.Position : Vector3.Zero;
        bool hasRigidbody2D = TryGetComponent(out Rigidbody2D rigidbody2D);
        LogInfo($"EntityApi step 5 tryGetRigidbody2D={hasRigidbody2D}");
        LogExpandedComponentAccess();
        LogRandomAndCoroutineApi();

        LogInfo($"EntityApi OnCreate handle={Entity.Handle} alive={Entity.IsAlive} tag='{m_OriginalTag}' hasTransform={m_HasTransform} entityHasTransform={entityHasTransform} entityGetTransform={(transformFromEntity != null)} hasRigidbody2D={hasRigidbody2D} position={m_OriginalPosition}");

        if (!string.IsNullOrWhiteSpace(m_OriginalTag))
        {
            Entity found = Entity.FindEntityByTag(m_OriginalTag);
            LogInfo($"EntityApi FindEntityByTag('{m_OriginalTag}') => {found.Handle} alive={found.IsAlive}");
        }

        if (hasRigidbody2D)
            LogInfo($"EntityApi Rigidbody2D bodyType={rigidbody2D.BodyType} velocity={rigidbody2D.LinearVelocity}");

        if (m_HasTransform)
        {
            Transform.Position = new Vector3(m_OriginalPosition.X + 1.0f, m_OriginalPosition.Y, m_OriginalPosition.Z);
            Transform.Rotation = new Vector3(Transform.Rotation.X, Transform.Rotation.Y, Transform.Rotation.Z + 5.0f);
            LogInfo($"EntityApi mutated transform position={Transform.Position} rotation={Transform.Rotation} scale={Transform.Scale}");
        }
    }

    public override void OnDestroy()
    {
        if (m_HasTransform)
            Transform.Position = m_OriginalPosition;

        LogInfo($"EntityApi OnDestroy handle={Entity.Handle} finalTag='{Entity.Tag}' finalPosition={(m_HasTransform ? Transform.Position : Vector3.Zero)} coroutineSteps={m_CoroutineStepCount} coroutineRunning={IsCoroutineRunning(m_SmokeCoroutine)} randomInt={m_LastRandomInt} randomFloat={m_LastRandomFloat:0.###} randomValue={m_LastRandomValue:0.###} stopSucceeded={m_StopCoroutineSucceeded} stopRunningAfterStop={m_StopCoroutineRunningAfterStop}");
    }

    private void LogExpandedComponentAccess()
    {
        LogComponentAccess<AudioListener2D>(nameof(AudioListener2D));
        LogComponentAccess<AudioListener3D>(nameof(AudioListener3D));
        LogComponentAccess<AudioSource>(nameof(AudioSource));
        LogComponentAccess<Animator>(nameof(Animator));
        LogComponentAccess<AnimationEventReceiver>(nameof(AnimationEventReceiver));
        LogComponentAccess<ParticleEmitter>(nameof(ParticleEmitter));
        LogComponentAccess<Grid2D>(nameof(Grid2D));
        LogComponentAccess<TilemapLayer>(nameof(TilemapLayer));

        if (Entity.TryGetComponent(out AudioSource audioSource))
        {
            float volume = audioSource.Volume;
            float pitch = audioSource.Pitch;
            bool muted = audioSource.Muted;
            bool isPlaying = audioSource.IsPlaying;
            audioSource.Volume = volume;
            audioSource.Pitch = pitch;
            audioSource.Muted = muted;
            LogInfo($"EntityApi AudioSource clip='{audioSource.ClipKey}' volume={volume} pitch={pitch} muted={muted} playing={isPlaying} loop={audioSource.Loop} playOnStart={audioSource.PlayOnStart} playbackSpace={audioSource.PlaybackSpace}");
        }

        if (Entity.TryGetComponent(out Animator animator))
        {
            float playbackSpeed = animator.PlaybackSpeed;
            bool enabled = animator.Enabled;
            animator.PlaybackSpeed = playbackSpeed;
            animator.Enabled = enabled;
            LogInfo($"EntityApi Animator controller='{animator.ControllerKey}' defaultClip='{animator.DefaultClipKey}' enabled={enabled} playbackSpeed={playbackSpeed} currentState='{animator.CurrentStateName}' currentClip='{animator.CurrentClipKey}' stateTime={animator.StateTimeSeconds:0.###} duration={animator.CurrentStateDurationSeconds:0.###}");
        }

        if (Entity.TryGetComponent(out AnimationEventReceiver animationEventReceiver))
        {
            int eventCount = animationEventReceiver.EventCount;
            LogInfo($"EntityApi AnimationEventReceiver enabled={animationEventReceiver.Enabled} eventCount={eventCount}");
            if (eventCount > 0)
            {
                AnimationEvent firstEvent = animationEventReceiver.GetEvent(0);
                LogInfo($"EntityApi AnimationEventReceiver firstEvent name='{firstEvent.Name}' string='{firstEvent.StringPayload}' float={firstEvent.FloatPayload:0.###} int={firstEvent.IntegerPayload} bool={firstEvent.BooleanPayload} time={firstEvent.TimeSeconds:0.###} normalized={firstEvent.NormalizedTime:0.###}");
            }
        }

        if (Entity.TryGetComponent(out ParticleEmitter particleEmitter))
        {
            float spawnRate = particleEmitter.SpawnRate;
            int maxParticles = particleEmitter.MaxParticles;
            particleEmitter.SpawnRate = spawnRate;
            particleEmitter.MaxParticles = maxParticles;
            particleEmitter.Emit(0);
            LogInfo($"EntityApi ParticleEmitter playing={particleEmitter.IsPlaying} paused={particleEmitter.IsPaused} alive={particleEmitter.AliveParticleCount} spawnRate={spawnRate} lifetimeMin={particleEmitter.LifetimeMin} lifetimeMax={particleEmitter.LifetimeMax} maxParticles={maxParticles} texture='{particleEmitter.TextureKey}'");
        }

        if (Entity.TryGetComponent(out Grid2D grid2D))
        {
            Vector2 cellSize = grid2D.CellSize;
            Vector2 cellGap = grid2D.CellGap;
            grid2D.CellSize = cellSize;
            grid2D.CellGap = cellGap;
            LogInfo($"EntityApi Grid2D cellSize={cellSize} cellGap={cellGap}");
        }

        if (Entity.TryGetComponent(out TilemapLayer tilemapLayer))
        {
            int width = tilemapLayer.GridWidth;
            int height = tilemapLayer.GridHeight;
            int renderOrder = tilemapLayer.RenderOrder;
            tilemapLayer.RenderOrder = renderOrder;
            tilemapLayer.ResizeGrid(width, height);

            string firstCellAssetKey = string.Empty;
            int firstCellTileId = 0;
            if (tilemapLayer.IsCellInBounds(0, 0))
            {
                firstCellTileId = tilemapLayer.GetTileId(0, 0);
                firstCellAssetKey = tilemapLayer.GetTileAssetKey(0, 0);
                tilemapLayer.SetTileId(0, 0, firstCellTileId);
                tilemapLayer.SetTileAssetKey(0, 0, firstCellAssetKey);
            }

            string firstTileTableEntry = tilemapLayer.TileTableEntryCount > 0
                ? tilemapLayer.GetTileTableEntry(0)
                : string.Empty;
            LogInfo($"EntityApi TilemapLayer size={width}x{height} cellCount={tilemapLayer.CellCount} renderOrder={renderOrder} collision={tilemapLayer.CollisionEnabled} castShadows={tilemapLayer.CastShadows} tableCount={tilemapLayer.TileTableEntryCount} firstTileId={firstCellTileId} firstTileKey='{firstCellAssetKey}' table0='{firstTileTableEntry}'");
        }
    }

    private void LogRandomAndCoroutineApi()
    {
        Random.InitState(1337);
        m_LastRandomInt = Random.Range(2, 7);
        m_LastRandomFloat = Random.Range(0.5f, 1.5f);
        m_LastRandomValue = Random.Value;
        LogInfo($"EntityApi Random int={m_LastRandomInt} float={m_LastRandomFloat:0.###} value={m_LastRandomValue:0.###}");

        m_SmokeCoroutine = StartCoroutine(CoroutineSmoke());
        LogInfo($"EntityApi Coroutine start valid={(m_SmokeCoroutine != null && m_SmokeCoroutine.IsValid)} running={IsCoroutineRunning(m_SmokeCoroutine)}");

        Coroutine stoppedCoroutine = StartCoroutine(StoppedCoroutineSmoke());
        m_StopCoroutineSucceeded = StopCoroutine(stoppedCoroutine);
        m_StopCoroutineRunningAfterStop = IsCoroutineRunning(stoppedCoroutine);
        LogInfo($"EntityApi Coroutine stopImmediate valid={stoppedCoroutine.IsValid} stopped={m_StopCoroutineSucceeded} runningAfterStop={m_StopCoroutineRunningAfterStop}");
    }

    private IEnumerator CoroutineSmoke()
    {
        m_CoroutineStepCount++;
        LogInfo($"EntityApi Coroutine step {m_CoroutineStepCount} yield=null");
        yield return null;

        m_CoroutineStepCount++;
        LogInfo($"EntityApi Coroutine step {m_CoroutineStepCount} yield=WaitForFrames");
        yield return WaitForFrames.NextFrame();

        m_CoroutineStepCount++;
        LogInfo($"EntityApi Coroutine step {m_CoroutineStepCount} yield=WaitForSeconds");
        yield return new WaitForSeconds(0.0f);

        Coroutine childCoroutine = StartCoroutine(CoroutineChildSmoke());
        LogInfo($"EntityApi Coroutine childStart valid={childCoroutine.IsValid} running={IsCoroutineRunning(childCoroutine)}");
        yield return childCoroutine;

        m_CoroutineStepCount++;
        LogInfo($"EntityApi Coroutine step {m_CoroutineStepCount} childCompleted={!IsCoroutineRunning(childCoroutine)}");
    }

    private IEnumerator CoroutineChildSmoke()
    {
        LogInfo("EntityApi Coroutine child step 1");
        yield return null;
        LogInfo("EntityApi Coroutine child step 2");
    }

    private IEnumerator StoppedCoroutineSmoke()
    {
        LogInfo("EntityApi Coroutine stopProbe step 1");
        yield return null;
        LogInfo("EntityApi Coroutine stopProbe step 2");
    }

    private void LogComponentAccess<T>(string label) where T : EntityComponent
    {
        bool hasComponent = Entity.HasComponent<T>();
        T? component = Entity.GetComponent<T>();
        bool tryGetComponent = Entity.TryGetComponent(out T resolvedComponent);
        LogInfo($"EntityApi component {label} has={hasComponent} get={(component != null)} try={tryGetComponent} resolved={(tryGetComponent && resolvedComponent != null)}");
    }
}
