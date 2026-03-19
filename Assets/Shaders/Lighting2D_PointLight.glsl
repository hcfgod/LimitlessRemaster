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

uniform vec2 u_ViewportSize;

uniform vec3 u_LightColor;
uniform float u_LightIntensity;
uniform vec2 u_LightPosition;
uniform float u_LightRadius;
uniform float u_LightFalloff;

uniform int u_UseShadows;
uniform float u_ShadowStrength;
uniform float u_ShadowSoftness;
uniform int u_ShadowSamples;
uniform float u_ShadowBias;
uniform float u_ShadowAlphaCutoff;
uniform float u_ShadowSegmentSnapPixels;
uniform int u_ShadowSegmentCount;

const int MAX_SHADOW_SEGMENTS = 128;
uniform vec4 u_ShadowSegments[MAX_SHADOW_SEGMENTS];
uniform vec4 u_ShadowSegmentCasterIds[MAX_SHADOW_SEGMENTS];

float Cross2D(vec2 a, vec2 b)
{
    return a.x * b.y - a.y * b.x;
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

bool RaymarchAlphaOcclusion(vec2 sampleOrigin, vec2 sampleEnd, vec4 fragmentCasterId)
{
    vec2 ray = sampleEnd - sampleOrigin;
    float rayLength = length(ray);
    if (rayLength <= 0.0001)
        return false;

    const int MAX_ALPHA_STEPS = 64;
    int stepCount = min(MAX_ALPHA_STEPS, max(1, int(rayLength / 2.0)));
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

float ComputeShadowFactor(vec2 fragmentScreenPosition, vec4 fragmentCasterId)
{
    if (u_UseShadows == 0 || u_ShadowStrength <= 0.001)
        return 1.0;

    float snapPixels = max(u_ShadowSegmentSnapPixels, 0.0);
    if (snapPixels > 0.0001)
        fragmentScreenPosition = round(fragmentScreenPosition / snapPixels) * snapPixels;

    vec2 stableLightPosition = u_LightPosition;
    if (snapPixels > 0.0001)
        stableLightPosition = round(stableLightPosition / snapPixels) * snapPixels;

    vec2 toFragment = fragmentScreenPosition - stableLightPosition;
    float distanceToFragment = length(toFragment);
    if (distanceToFragment <= 0.0001)
        return 1.0;

    vec2 directionToFragment = toFragment / distanceToFragment;
    vec2 perpendicular = vec2(-directionToFragment.y, directionToFragment.x);
    int samples = max(u_ShadowSamples, 1);
    float weightedBlocked = 0.0;
    float totalWeight = 0.0;
    float softness = max(u_ShadowSoftness, 0.0);
    float halfSpread = max(softness * 0.5, 0.0001);

    // Defer the alpha raymarch until actually needed. Using a single ray from
    // the centered light position avoids the artifact where each soft-shadow
    // sample independently resolves the full caster silhouette from slightly
    // different perpendicular positions, producing N shifted copies of the shadow.
    vec2 alphaRayEnd = fragmentScreenPosition - directionToFragment * max(u_ShadowBias, 0.0);
    if (snapPixels > 0.0001)
        alphaRayEnd = round(alphaRayEnd / snapPixels) * snapPixels;
    bool alphaOccludedChecked = false;
    bool alphaOccluded = false;

    for (int sampleIndex = 0; sampleIndex < samples; ++sampleIndex)
    {
        float phase = (float(sampleIndex) + 0.5) / float(samples);
        float sampleOffset = (phase - 0.5) * softness;
        float t = clamp(abs(sampleOffset) / halfSpread, 0.0, 1.0);
        float sampleWeight = 1.0 - smoothstep(0.0, 1.0, t);
        sampleWeight = max(sampleWeight, 0.02);
        vec2 sampleLightPosition = stableLightPosition + perpendicular * sampleOffset;
        vec2 sampleRayEnd = fragmentScreenPosition - directionToFragment * max(u_ShadowBias, 0.0);
        if (snapPixels > 0.0001)
        {
            sampleLightPosition = round(sampleLightPosition / snapPixels) * snapPixels;
            sampleRayEnd = round(sampleRayEnd / snapPixels) * snapPixels;
        }

        // Check segment intersections first (cheap — no texture fetches).
        bool occluded = false;
        int segmentCount = min(u_ShadowSegmentCount, MAX_SHADOW_SEGMENTS);
        for (int segmentIndex = 0; !occluded && segmentIndex < segmentCount; ++segmentIndex)
        {
            if ((fragmentCasterId.x > 0.0001 || fragmentCasterId.y > 0.0001 || fragmentCasterId.z > 0.0001 || fragmentCasterId.w > 0.0001) &&
                all(lessThan(abs(u_ShadowSegmentCasterIds[segmentIndex] - fragmentCasterId), vec4(0.001))))
            {
                continue;
            }
            vec4 segment = u_ShadowSegments[segmentIndex];
            vec2 edge = segment.zw - segment.xy;
            float edgeLength = length(edge);
            if (edgeLength <= 0.0001)
                continue;
            vec2 outwardNormal = vec2(edge.y, -edge.x) / edgeLength;
            vec2 rayDirection = normalize(sampleRayEnd - sampleLightPosition);
            if (dot(outwardNormal, rayDirection) >= 0.0)
                continue;
            if (IntersectSegment(sampleLightPosition, sampleRayEnd, segment.xy, segment.zw))
            {
                occluded = true;
                break;
            }
        }

        // Only run expensive alpha raymarch if segments didn't find occlusion.
        // Cache the result so it runs at most once across all samples.
        if (!occluded)
        {
            if (!alphaOccludedChecked)
            {
                alphaOccluded = RaymarchAlphaOcclusion(stableLightPosition, alphaRayEnd, fragmentCasterId);
                alphaOccludedChecked = true;
            }
            occluded = alphaOccluded;
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
    // Ignore near-transparent texels to reduce alpha-edge shimmer on fast camera motion.
    if (albedo.a <= 0.01)
    {
        FragColor = vec4(0.0);
        return;
    }

    vec2 fragmentScreenPosition = gl_FragCoord.xy;
    vec2 toLight = u_LightPosition - fragmentScreenPosition;
    float distanceToLight = length(toLight);
    if (distanceToLight >= u_LightRadius)
    {
        FragColor = vec4(0.0);
        return;
    }

    vec4 normalSample = texture(u_NormalTexture, v_UV);
    vec4 fragmentCasterId = texelFetch(u_EntityIdTexture, ivec2(gl_FragCoord.xy), 0);

    // Read GBuffer normal for future normal-mapped bump lighting support.
    // For 2D sprites, N dot L is disabled by default: point lights radiate
    // evenly from their center. Artists who assign a real normal map to a
    // material will get per-pixel bump shading once the feature is enabled.
    float ndotl = 1.0;

    float attenuation = 1.0 - clamp(distanceToLight / max(u_LightRadius, 0.0001), 0.0, 1.0);
    attenuation = pow(attenuation, max(u_LightFalloff, 0.1));

    float shadowAlphaCutoff = clamp(u_ShadowAlphaCutoff, 0.0, 1.0);
    float shadowReceiver = clamp(normalSample.a, 0.0, 1.0);
    // Soft receiver weight from GBuffer prevents edge threshold popping.
    float computedShadowFactor = (shadowReceiver > 0.001 && albedo.a >= max(0.01, shadowAlphaCutoff - 0.12)) ? ComputeShadowFactor(fragmentScreenPosition, fragmentCasterId) : 1.0;
    float shadowFactor = mix(1.0, computedShadowFactor, shadowReceiver);

    vec3 lighting = u_LightColor * (u_LightIntensity * ndotl * attenuation * shadowFactor);
    FragColor = vec4(lighting, 1.0);
}

