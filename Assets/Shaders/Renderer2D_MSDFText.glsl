#type vertex
#version 330 core

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec2 a_UV;
layout(location = 2) in vec4 a_Color;
layout(location = 3) in int a_TexIndex;

uniform mat4 u_ViewProjection;
uniform mat4 u_Model;

out vec2 v_UV;
out vec4 v_Color;
flat out int v_TexIndex;

void main()
{
    v_UV = a_UV;
    v_Color = a_Color;
    v_TexIndex = a_TexIndex;
    gl_Position = u_ViewProjection * u_Model * vec4(a_Position, 1.0);
}

#type fragment
#version 330 core

in vec2 v_UV;
in vec4 v_Color;
flat in int v_TexIndex;
out vec4 FragColor;

uniform sampler2D u_Textures[16];

float Median(float r, float g, float b)
{
    return max(min(r, g), min(max(r, g), b));
}

vec4 SampleGlyphAtlas()
{
    if (v_TexIndex == 0) return texture(u_Textures[0], v_UV);
    if (v_TexIndex == 1) return texture(u_Textures[1], v_UV);
    if (v_TexIndex == 2) return texture(u_Textures[2], v_UV);
    if (v_TexIndex == 3) return texture(u_Textures[3], v_UV);
    if (v_TexIndex == 4) return texture(u_Textures[4], v_UV);
    if (v_TexIndex == 5) return texture(u_Textures[5], v_UV);
    if (v_TexIndex == 6) return texture(u_Textures[6], v_UV);
    if (v_TexIndex == 7) return texture(u_Textures[7], v_UV);
    if (v_TexIndex == 8) return texture(u_Textures[8], v_UV);
    if (v_TexIndex == 9) return texture(u_Textures[9], v_UV);
    if (v_TexIndex == 10) return texture(u_Textures[10], v_UV);
    if (v_TexIndex == 11) return texture(u_Textures[11], v_UV);
    if (v_TexIndex == 12) return texture(u_Textures[12], v_UV);
    if (v_TexIndex == 13) return texture(u_Textures[13], v_UV);
    if (v_TexIndex == 14) return texture(u_Textures[14], v_UV);
    if (v_TexIndex == 15) return texture(u_Textures[15], v_UV);
    return texture(u_Textures[0], v_UV);
}

void main()
{
    vec3 msdf = SampleGlyphAtlas().rgb;
    float signedDistance = Median(msdf.r, msdf.g, msdf.b) - 0.5;
    float screenPxDistance = signedDistance / max(fwidth(signedDistance), 0.0001);
    float alpha = clamp(screenPxDistance + 0.5, 0.0, 1.0);
    FragColor = vec4(v_Color.rgb, v_Color.a * alpha);
}
