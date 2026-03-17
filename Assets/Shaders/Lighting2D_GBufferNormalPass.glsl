#type vertex
#version 330 core

layout(location = 0) in vec2 a_Position;
layout(location = 1) in vec2 a_UV;

out vec2 v_UV;
out vec2 v_BasisX;
out vec2 v_BasisY;

uniform mat4 u_ViewProjection;
uniform mat4 u_Model;
uniform vec2 u_UvMin;
uniform vec2 u_UvMax;

void main()
{
    vec4 worldPosition = u_Model * vec4(a_Position, 0.0, 1.0);
    gl_Position = u_ViewProjection * worldPosition;
    v_UV = mix(u_UvMin, u_UvMax, a_UV);

    mat2 basis = mat2(u_Model);
    v_BasisX = normalize(basis[0]);
    v_BasisY = normalize(basis[1]);
}

#type fragment
#version 330 core

layout(location = 0) out vec4 FragColor;
layout(location = 1) out vec4 FragEntityId;
layout(location = 2) out vec4 FragCasterMask;
layout(location = 3) out vec4 FragCasterEntityId;

in vec2 v_UV;
in vec2 v_BasisX;
in vec2 v_BasisY;

uniform sampler2D u_AlbedoTexture;
uniform sampler2D u_NormalTexture;
uniform vec4 u_Color;
uniform float u_NormalStrength;
uniform int u_ReceiveShadows;
uniform float u_CasterHeightPixels;
uniform float u_CasterHeightEncodeMaxPixels;
uniform vec4 u_CasterEntityId;
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
    float shadowReceiver = 0.0;
    if (u_ReceiveShadows != 0)
    {
        shadowReceiver = 1.0;
    }

    float castShadow = 0.0;
    if (u_CasterEntityId.x > 0.0001 || u_CasterEntityId.y > 0.0001 || u_CasterEntityId.z > 0.0001 || u_CasterEntityId.w > 0.0001)
        castShadow = 1.0;

    float normalizedCasterHeight = 0.0;
    if (castShadow > 0.5 && u_CasterHeightEncodeMaxPixels > 0.0001)
        normalizedCasterHeight = clamp(u_CasterHeightPixels / u_CasterHeightEncodeMaxPixels, 0.0, 1.0);

    float encodedCasterHeight = normalizedCasterHeight * 65535.0;
    float encodedCasterHeightHi = floor(encodedCasterHeight / 256.0);
    float encodedCasterHeightLo = encodedCasterHeight - encodedCasterHeightHi * 256.0;

    FragColor = vec4(encodedNormal, shadowReceiver);
    FragEntityId = u_CasterEntityId;
    FragCasterMask = vec4(encodedCasterHeightHi / 255.0, encodedCasterHeightLo / 255.0, 0.0, albedo.a * castShadow);
    FragCasterEntityId = u_CasterEntityId;
}

