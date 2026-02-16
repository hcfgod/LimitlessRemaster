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
uniform int u_ShadowSegmentCount;

const int MAX_SHADOW_SEGMENTS = 128;
uniform vec4 u_ShadowSegments[MAX_SHADOW_SEGMENTS];

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

float ComputeShadowFactor(vec2 fragmentScreenPosition)
{
    if (u_UseShadows == 0 || u_ShadowSegmentCount <= 0 || u_ShadowStrength <= 0.001)
        return 1.0;

    vec2 toFragment = fragmentScreenPosition - u_LightPosition;
    float distanceToFragment = length(toFragment);
    if (distanceToFragment <= 0.0001)
        return 1.0;

    vec2 directionToFragment = toFragment / distanceToFragment;
    vec2 perpendicular = vec2(-directionToFragment.y, directionToFragment.x);
    int samples = max(u_ShadowSamples, 1);
    float weightedBlocked = 0.0;
    float totalWeight = 0.0;
    float softness = max(u_ShadowSoftness, 0.0);
    float halfSoftness = max(softness * 0.5, 0.0001);

    for (int sampleIndex = 0; sampleIndex < samples; ++sampleIndex)
    {
        float phase = (float(sampleIndex) + 0.5) / float(samples);
        float sampleOffset = (phase - 0.5) * softness;
        float sampleWeight = 1.0 - clamp(abs(sampleOffset) / halfSoftness, 0.0, 1.0);
        sampleWeight = max(sampleWeight, 0.01);
        vec2 sampleLightPosition = u_LightPosition + perpendicular * sampleOffset;
        vec2 sampleRayEnd = fragmentScreenPosition - directionToFragment * max(u_ShadowBias, 0.0);

        bool occluded = false;
        int segmentCount = min(u_ShadowSegmentCount, MAX_SHADOW_SEGMENTS);
        for (int segmentIndex = 0; segmentIndex < segmentCount; ++segmentIndex)
        {
            vec4 segment = u_ShadowSegments[segmentIndex];
            if (IntersectSegment(sampleLightPosition, sampleRayEnd, segment.xy, segment.zw))
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
    if (albedo.a <= 0.001 && max(max(albedo.r, albedo.g), albedo.b) <= 0.001)
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

    // Read GBuffer normal for future normal-mapped bump lighting support.
    // For 2D sprites, N dot L is disabled by default: point lights radiate
    // evenly from their center. Artists who assign a real normal map to a
    // material will get per-pixel bump shading once the feature is enabled.
    float ndotl = 1.0;

    float attenuation = 1.0 - clamp(distanceToLight / max(u_LightRadius, 0.0001), 0.0, 1.0);
    attenuation = pow(attenuation, max(u_LightFalloff, 0.1));

    float shadowFactor = normalSample.a > 0.5 ? ComputeShadowFactor(fragmentScreenPosition) : 1.0;

    vec3 lighting = u_LightColor * (u_LightIntensity * ndotl * attenuation * shadowFactor);
    FragColor = vec4(lighting, 1.0);
}

