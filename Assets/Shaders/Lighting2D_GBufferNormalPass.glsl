#type vertex
#version 330 core

layout(location = 0) in vec2 a_Position;
layout(location = 1) in vec2 a_UV;

uniform mat4 u_ViewProjection;
uniform mat4 u_Model;

out vec2 v_UV;
out vec2 v_BasisX;
out vec2 v_BasisY;

void main()
{
    v_UV = a_UV;

    vec2 basisX = vec2(u_Model[0].x, u_Model[0].y);
    vec2 basisY = vec2(u_Model[1].x, u_Model[1].y);
    if (length(basisX) < 0.0001) basisX = vec2(1.0, 0.0);
    if (length(basisY) < 0.0001) basisY = vec2(0.0, 1.0);
    v_BasisX = normalize(basisX);
    v_BasisY = normalize(basisY);

    vec4 worldPosition = u_Model * vec4(a_Position, 0.0, 1.0);
    gl_Position = u_ViewProjection * worldPosition;
}

#type fragment
#version 330 core

layout(location = 0) out vec4 FragColor;

in vec2 v_UV;
in vec2 v_BasisX;
in vec2 v_BasisY;

uniform sampler2D u_AlbedoTexture;
uniform sampler2D u_NormalTexture;
uniform vec4 u_Color;
uniform float u_NormalStrength;
uniform int u_ReceiveShadows;
uniform float u_ShadowAlphaCutoff;

void main()
{
    vec4 albedo = texture(u_AlbedoTexture, v_UV) * u_Color;
    // Keep alpha edge rejection aligned with lighting passes for temporal stability.
    if (albedo.a <= 0.01)
        discard;

    vec3 tangentNormal = texture(u_NormalTexture, v_UV).xyz * 2.0 - 1.0;
    tangentNormal.xy *= max(u_NormalStrength, 0.0);

    mat2 basis = mat2(v_BasisX, v_BasisY);
    vec2 worldXY = basis * tangentNormal.xy;
    vec3 worldNormal = normalize(vec3(worldXY, max(0.0001, tangentNormal.z)));
    vec3 encodedNormal = worldNormal * 0.5 + 0.5;

    // A channel carries whether this pixel should receive shadows.
    // For alpha-cutout textures, ignore low-alpha fringe pixels to reduce
    // temporal shimmer while rotating in perspective views.
    float shadowAlphaCutoff = clamp(u_ShadowAlphaCutoff, 0.0, 1.0);
    // Use a soft threshold band to avoid binary popping at alpha-cutout edges.
    float shadowReceiver = 0.0;
    if (u_ReceiveShadows != 0)
    {
        float edgeBand = 0.08;
        shadowReceiver = smoothstep(shadowAlphaCutoff - edgeBand, shadowAlphaCutoff + edgeBand, albedo.a);
    }
    FragColor = vec4(encodedNormal, shadowReceiver);
}

