#type vertex
#version 330 core

layout(location = 0) in vec2 a_Position;
layout(location = 1) in vec2 a_UV;

out vec2 v_UV;

void main()
{
    v_UV = a_UV;
    gl_Position = vec4(a_Position * 2.0, 0.0, 1.0);
}

#type fragment
#version 330 core

layout(location = 0) out vec4 FragColor;

in vec2 v_UV;

uniform sampler2D u_AlbedoTexture;
uniform sampler2D u_NormalTexture;
uniform sampler2D u_EntityIdTexture;
uniform sampler2D u_CasterMaskTexture;
uniform sampler2D u_CasterEntityIdTexture;

uniform vec2 u_ViewportSize;

uniform vec3 u_LightColor;
uniform float u_LightIntensity;
uniform vec2 u_LightDirection;
uniform vec2 u_ShadingLightDirection;

uniform int u_UseShadows;
uniform float u_ShadowStrength;
uniform float u_ShadowSoftness;
uniform int u_ShadowSamples;
uniform float u_ShadowDistance;
uniform float u_ShadowBias;
uniform float u_ShadowAlphaCutoff;
uniform float u_CasterHeightEncodeMaxPixels;
uniform float u_ShadowSegmentSnapPixels;
uniform int u_ClampShadowToViewport;
uniform int u_ShadowSegmentCount;

const int MAX_SHADOW_SEGMENTS = 128;
uniform vec4 u_ShadowSegments[MAX_SHADOW_SEGMENTS];
uniform vec4 u_ShadowSegmentCasterIds[MAX_SHADOW_SEGMENTS];
uniform int u_ShadowSegmentFlags[MAX_SHADOW_SEGMENTS];

float Cross2D(vec2 a, vec2 b)
{
    return a.x * b.y - a.y * b.x;
}

float DecodeCasterHeightPixels(vec4 casterMaskSample)
{
    if (u_CasterHeightEncodeMaxPixels <= 0.0001)
        return 0.0;

    float hi = floor(casterMaskSample.r * 255.0 + 0.5);
    float lo = floor(casterMaskSample.g * 255.0 + 0.5);
    float normalized = (hi * 256.0 + lo) / 65535.0;
    return normalized * u_CasterHeightEncodeMaxPixels;
}

bool IntersectSegment(vec2 p0, vec2 p1, vec2 q0, vec2 q1)
{
    vec2 r = p1 - p0;
    vec2 s = q1 - q0;
    float denominator = Cross2D(r, s);
    if (abs(denominator) < 0.0001)
        return false;

    vec2 qp = q0 - p0;
    float t = Cross2D(qp, s) / denominator;
    float u = Cross2D(qp, r) / denominator;
    return (t >= 0.0 && t <= 1.0 && u >= 0.0 && u <= 1.0);
}

bool IsScreenPosInsideViewport(vec2 screenPos)
{
    return screenPos.x >= 0.0 &&
           screenPos.y >= 0.0 &&
           screenPos.x < u_ViewportSize.x &&
           screenPos.y < u_ViewportSize.y;
}

bool ComputeViewportExitPhase(vec2 sampleOrigin, vec2 sampleEnd, out float exitPhase)
{
    if (!IsScreenPosInsideViewport(sampleOrigin))
        return false;

    vec2 delta = sampleEnd - sampleOrigin;
    exitPhase = 1.0;
    bool hasBoundary = false;

    if (delta.x > 0.0001)
    {
        exitPhase = min(exitPhase, (u_ViewportSize.x - sampleOrigin.x) / delta.x);
        hasBoundary = true;
    }
    else if (delta.x < -0.0001)
    {
        exitPhase = min(exitPhase, (0.0 - sampleOrigin.x) / delta.x);
        hasBoundary = true;
    }

    if (delta.y > 0.0001)
    {
        exitPhase = min(exitPhase, (u_ViewportSize.y - sampleOrigin.y) / delta.y);
        hasBoundary = true;
    }
    else if (delta.y < -0.0001)
    {
        exitPhase = min(exitPhase, (0.0 - sampleOrigin.y) / delta.y);
        hasBoundary = true;
    }

    return hasBoundary && exitPhase >= 0.0 && exitPhase < 1.0;
}

bool IsAlphaOccluderAtScreenPos(vec2 screenPos, vec4 fragmentCasterId)
{
    ivec2 texel = ivec2(floor(screenPos));
    if (texel.x < 0 || texel.y < 0 ||
        texel.x >= int(u_ViewportSize.x) || texel.y >= int(u_ViewportSize.y))
    {
        return false;
    }

    vec4 albedoSample = texelFetch(u_AlbedoTexture, texel, 0);
    if (albedoSample.a < max(0.01, u_ShadowAlphaCutoff))
        return false;

    vec4 casterId = texelFetch(u_EntityIdTexture, texel, 0);
    if (casterId.x <= 0.0001 && casterId.y <= 0.0001 && casterId.z <= 0.0001 && casterId.w <= 0.0001)
        return false;

    if ((fragmentCasterId.x > 0.0001 || fragmentCasterId.y > 0.0001 || fragmentCasterId.z > 0.0001 || fragmentCasterId.w > 0.0001) &&
        all(lessThan(abs(casterId - fragmentCasterId), vec4(0.001))))
    {
        return false;
    }

    return true;
}

bool IsAlphaOccluderAtScreenPosProjected(vec2 screenPos,
                                         float projectedDistance,
                                         float matchTolerance,
                                         vec4 fragmentCasterId)
{
    ivec2 texel = ivec2(floor(screenPos));
    if (texel.x < 0 || texel.y < 0 ||
        texel.x >= int(u_ViewportSize.x) || texel.y >= int(u_ViewportSize.y))
    {
        return false;
    }

    vec4 casterMaskSample = texelFetch(u_CasterMaskTexture, texel, 0);
    if (casterMaskSample.a < max(0.01, u_ShadowAlphaCutoff))
        return false;

    vec4 casterId = texelFetch(u_CasterEntityIdTexture, texel, 0);
    if (casterId.x <= 0.0001 && casterId.y <= 0.0001 && casterId.z <= 0.0001 && casterId.w <= 0.0001)
        return false;

    if ((fragmentCasterId.x > 0.0001 || fragmentCasterId.y > 0.0001 || fragmentCasterId.z > 0.0001 || fragmentCasterId.w > 0.0001) &&
        all(lessThan(abs(casterId - fragmentCasterId), vec4(0.001))))
    {
        return false;
    }

    float casterHeightPixels = DecodeCasterHeightPixels(casterMaskSample);
    if (abs(projectedDistance - casterHeightPixels) > matchTolerance)
        return false;

    return true;
}

bool RaymarchAlphaOcclusion(vec2 sampleOrigin, vec2 sampleEnd, vec4 fragmentCasterId)
{
    vec2 ray = sampleEnd - sampleOrigin;
    float rayLength = length(ray);
    if (rayLength <= 0.0001)
        return false;

    const int MAX_ALPHA_STEPS = 96;
    int stepCount = min(MAX_ALPHA_STEPS, max(1, int(rayLength / 1.5)));
    for (int stepIndex = 0; stepIndex < MAX_ALPHA_STEPS; ++stepIndex)
    {
        if (stepIndex >= stepCount)
            break;

        float phase = (float(stepIndex) + 0.5) / float(stepCount);
        vec2 samplePos = mix(sampleOrigin, sampleEnd, phase);
        if (IsAlphaOccluderAtScreenPos(samplePos, fragmentCasterId))
            return true;
    }

    return false;
}

bool ProjectedAlphaOcclusion(vec2 fragmentScreenPosition,
                             vec2 rayDirection,
                             float shadowDistance,
                             float shadowBias,
                             float sampleOffset,
                             float snapPixels,
                             vec4 fragmentCasterId)
{
    vec2 perpendicular = vec2(-rayDirection.y, rayDirection.x);
    float biasDistance = max(shadowBias, 0.0);
    const int MAX_PROJECTED_STEPS = 256;
    float maxDistance = min(max(shadowDistance + u_CasterHeightEncodeMaxPixels, 1.0), float(MAX_PROJECTED_STEPS) * 1.5);
    if (maxDistance <= biasDistance + 0.0001)
        return false;

    vec2 receiverSamplePosition = fragmentScreenPosition + perpendicular * sampleOffset;
    if (snapPixels > 0.0001)
        receiverSamplePosition = round(receiverSamplePosition / snapPixels) * snapPixels;

    int stepCount = min(MAX_PROJECTED_STEPS, max(1, int((maxDistance - biasDistance) / 1.5)));
    float stepDistance = (maxDistance - biasDistance) / float(stepCount);
    float matchTolerance = max(max(1.5, u_ShadowSoftness * 0.35 + 0.75), stepDistance * 0.85);
    for (int stepIndex = 0; stepIndex < MAX_PROJECTED_STEPS; ++stepIndex)
    {
        if (stepIndex >= stepCount)
            break;

        float phase = (float(stepIndex) + 0.5) / float(stepCount);
        float sampleDistance = mix(biasDistance, maxDistance, phase);
        vec2 samplePos = receiverSamplePosition + rayDirection * sampleDistance;
        if (snapPixels > 0.0001)
            samplePos = round(samplePos / snapPixels) * snapPixels;
        float projectedDistance = dot(samplePos - receiverSamplePosition, rayDirection);
        if (IsAlphaOccluderAtScreenPosProjected(samplePos, projectedDistance, matchTolerance, fragmentCasterId))
            return true;
    }

    return false;
}

float ComputeShadowFactor(vec2 fragmentScreenPosition, vec2 rayDirection, vec4 fragmentCasterId)
{
    if (u_UseShadows == 0 || u_ShadowStrength <= 0.001)
        return 1.0;

    float snapPixels = max(u_ShadowSegmentSnapPixels, 0.0);
    if (snapPixels > 0.0001)
        fragmentScreenPosition = round(fragmentScreenPosition / snapPixels) * snapPixels;

    int samples = max(u_ShadowSamples, 1);
    float weightedBlocked = 0.0;
    float totalWeight = 0.0;
    float softness = max(u_ShadowSoftness, 0.0);
    float halfSpread = max(softness * 0.5, 0.0001);
    float effectiveShadowDistance = max(u_ShadowDistance, 1.0);

    for (int sampleIndex = 0; sampleIndex < samples; ++sampleIndex)
    {
        float phase = (float(sampleIndex) + 0.5) / float(samples);
        float sampleOffset = (phase - 0.5) * softness;
        float t = clamp(abs(sampleOffset) / halfSpread, 0.0, 1.0);
        float sampleWeight = 1.0 - smoothstep(0.0, 1.0, t);
        sampleWeight = max(sampleWeight, 0.02);
        vec2 perpendicular = vec2(-rayDirection.y, rayDirection.x);
        vec2 sampleOrigin = fragmentScreenPosition + rayDirection * max(u_ShadowBias, 0.0) + perpendicular * sampleOffset;
        vec2 sampleEnd = sampleOrigin + rayDirection * effectiveShadowDistance;
        if (snapPixels > 0.0001)
        {
            sampleOrigin = round(sampleOrigin / snapPixels) * snapPixels;
            sampleEnd = round(sampleEnd / snapPixels) * snapPixels;
        }
        if (u_ClampShadowToViewport != 0 && !IsScreenPosInsideViewport(sampleOrigin))
            continue;
        float sampleShadowDistance = effectiveShadowDistance;
        bool sampleEndInsideViewport = IsScreenPosInsideViewport(sampleEnd);
        float viewportExitPhase = 1.0;
        bool hasOffscreenRayPortion = !sampleEndInsideViewport &&
                                      ComputeViewportExitPhase(sampleOrigin, sampleEnd, viewportExitPhase);
        if (u_ClampShadowToViewport != 0 && hasOffscreenRayPortion)
        {
            sampleShadowDistance *= clamp(viewportExitPhase, 0.0, 1.0);
            sampleEnd = mix(sampleOrigin, sampleEnd, viewportExitPhase);
            hasOffscreenRayPortion = false;
        }
        bool alphaOccluded = ProjectedAlphaOcclusion(
            fragmentScreenPosition,
            rayDirection,
            sampleShadowDistance,
            u_ShadowBias,
            sampleOffset,
            snapPixels,
            fragmentCasterId);

        bool occluded = alphaOccluded;
        int segmentCount = min(u_ShadowSegmentCount, MAX_SHADOW_SEGMENTS);
        vec2 offscreenSampleOrigin = sampleOrigin;
        int viewportExitSide = 0;
        if (hasOffscreenRayPortion)
        {
            vec2 viewportExitPoint = mix(sampleOrigin, sampleEnd, viewportExitPhase);
            offscreenSampleOrigin = viewportExitPoint + rayDirection * max(0.5, snapPixels * 0.5);

            float minDistanceToBoundary = abs(viewportExitPoint.x);
            viewportExitSide = 1;

            float candidateDistance = abs(viewportExitPoint.x - u_ViewportSize.x);
            if (candidateDistance < minDistanceToBoundary)
            {
                minDistanceToBoundary = candidateDistance;
                viewportExitSide = 2;
            }

            candidateDistance = abs(viewportExitPoint.y);
            if (candidateDistance < minDistanceToBoundary)
            {
                minDistanceToBoundary = candidateDistance;
                viewportExitSide = 3;
            }

            candidateDistance = abs(viewportExitPoint.y - u_ViewportSize.y);
            if (candidateDistance < minDistanceToBoundary)
                viewportExitSide = 4;
        }
        for (int segmentIndex = 0; !occluded && segmentIndex < segmentCount; ++segmentIndex)
        {
            if ((fragmentCasterId.x > 0.0001 || fragmentCasterId.y > 0.0001 || fragmentCasterId.z > 0.0001 || fragmentCasterId.w > 0.0001) &&
                all(lessThan(abs(u_ShadowSegmentCasterIds[segmentIndex] - fragmentCasterId), vec4(0.001))))
            {
                continue;
            }
            vec4 segment = u_ShadowSegments[segmentIndex];
            bool offscreenOnlySegment = (u_ShadowSegmentFlags[segmentIndex] & 1) != 0;
            vec2 segmentSampleOrigin = sampleOrigin;
            if (offscreenOnlySegment)
            {
                if (!hasOffscreenRayPortion)
                    continue;
                float viewportEdgeThreshold = max(1.0, snapPixels);
                bool segmentOutsideExitSide = false;
                if (viewportExitSide == 1)
                    segmentOutsideExitSide = segment.x <= -viewportEdgeThreshold &&
                                             segment.z <= -viewportEdgeThreshold;
                else if (viewportExitSide == 2)
                    segmentOutsideExitSide = segment.x >= u_ViewportSize.x + viewportEdgeThreshold &&
                                             segment.z >= u_ViewportSize.x + viewportEdgeThreshold;
                else if (viewportExitSide == 3)
                    segmentOutsideExitSide = segment.y <= -viewportEdgeThreshold &&
                                             segment.w <= -viewportEdgeThreshold;
                else if (viewportExitSide == 4)
                    segmentOutsideExitSide = segment.y >= u_ViewportSize.y + viewportEdgeThreshold &&
                                             segment.w >= u_ViewportSize.y + viewportEdgeThreshold;
                if (!segmentOutsideExitSide)
                    continue;
                segmentSampleOrigin = offscreenSampleOrigin;
            }
            vec2 edge = segment.zw - segment.xy;
            float edgeLength = length(edge);
            if (edgeLength <= 0.0001)
                continue;
            float maxSegScreenLen = max(u_ViewportSize.x, u_ViewportSize.y) * 1.5;
            if (edgeLength > maxSegScreenLen)
                continue;
            vec2 outwardNormal = vec2(edge.y, -edge.x) / edgeLength;
            // Ignore back-facing/exit intersections. This greatly reduces
            // self-shadowing artifacts for convex sprite/collider occluders.
            if (dot(outwardNormal, rayDirection) >= 0.0)
                continue;
            if (IntersectSegment(segmentSampleOrigin, sampleEnd, segment.xy, segment.zw))
            {
                occluded = true;
                break;
            }
        }

        if (occluded)
            weightedBlocked += sampleWeight;
        totalWeight += sampleWeight;
    }

    float occlusion = weightedBlocked / max(totalWeight, 0.0001);
    return clamp(1.0 - occlusion * clamp(u_ShadowStrength, 0.0, 1.0), 0.0, 1.0);
}

void main()
{
    vec4 albedo = texture(u_AlbedoTexture, v_UV);
    if (albedo.a <= 0.01)
    {
        FragColor = vec4(0.0);
        return;
    }

    vec4 normalSample = texture(u_NormalTexture, v_UV);
    vec4 fragmentCasterId = texelFetch(u_EntityIdTexture, ivec2(gl_FragCoord.xy), 0);

    vec3 normal = normalize(normalSample.xyz * 2.0 - 1.0);
    vec2 shadingDirection = u_ShadingLightDirection;
    if (length(shadingDirection) < 0.0001)
        shadingDirection = vec2(0.0, -1.0);
    else
        shadingDirection = normalize(shadingDirection);

    // Use world-space XY light azimuth and a small positive Z lift so
    // flat sprites keep readable lighting while still reacting to rotation.
    vec3 lightVector = normalize(vec3(-shadingDirection, 0.45));
    float ndotl = max(dot(normal, lightVector), 0.0);

    // Flat/default normals have near-zero XY and otherwise produce an almost
    // constant result. Provide a gentle fallback so directional rotation is
    // still visible without authored normal maps.
    float flatness = 1.0 - clamp(length(normal.xy), 0.0, 1.0);
    float azimuthTerm = 0.35 + 0.65 * max(dot(-shadingDirection, vec2(0.0, -1.0)), 0.0);
    ndotl = mix(ndotl, azimuthTerm, flatness * 0.35);

    vec2 fragmentScreenPosition = gl_FragCoord.xy;
    vec2 shadowRayDir = -u_LightDirection;
    float shadowRayLength = length(shadowRayDir);
    if (shadowRayLength < 0.0001)
        shadowRayDir = vec2(0.0, 1.0);
    else
        shadowRayDir /= shadowRayLength;

    float shadowAlphaCutoff = clamp(u_ShadowAlphaCutoff, 0.0, 1.0);
    float shadowReceiver = clamp(normalSample.a, 0.0, 1.0);
    float computedShadowFactor = (albedo.a >= max(0.01, shadowAlphaCutoff - 0.12)) ? ComputeShadowFactor(fragmentScreenPosition, shadowRayDir, fragmentCasterId) : 1.0;
    float shadowFactor = mix(1.0, computedShadowFactor, shadowReceiver);

    vec3 lighting = u_LightColor * (u_LightIntensity * ndotl * shadowFactor);
    FragColor = vec4(lighting, 1.0);
}
