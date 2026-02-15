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

// Multi-texture batching:
// Bind a small fixed set of textures to slots [0..15] and select in-shader via v_TexIndex.
// This avoids flushing the batch on texture changes.
uniform sampler2D u_Textures[16];

void main()
{
    // Note: Some GLSL 330 toolchains/drivers are picky about dynamically indexing sampler arrays.
    // Keep it explicit and branch to a constant index.
    vec4 texColor = vec4(1.0);
    // Slot 0 is reserved for color-only quads. Do not sample a texture for this path.
    if (v_TexIndex == 0) texColor = vec4(1.0);
    else if (v_TexIndex == 1) texColor = texture(u_Textures[1], v_UV);
    else if (v_TexIndex == 2) texColor = texture(u_Textures[2], v_UV);
    else if (v_TexIndex == 3) texColor = texture(u_Textures[3], v_UV);
    else if (v_TexIndex == 4) texColor = texture(u_Textures[4], v_UV);
    else if (v_TexIndex == 5) texColor = texture(u_Textures[5], v_UV);
    else if (v_TexIndex == 6) texColor = texture(u_Textures[6], v_UV);
    else if (v_TexIndex == 7) texColor = texture(u_Textures[7], v_UV);
    else if (v_TexIndex == 8) texColor = texture(u_Textures[8], v_UV);
    else if (v_TexIndex == 9) texColor = texture(u_Textures[9], v_UV);
    else if (v_TexIndex == 10) texColor = texture(u_Textures[10], v_UV);
    else if (v_TexIndex == 11) texColor = texture(u_Textures[11], v_UV);
    else if (v_TexIndex == 12) texColor = texture(u_Textures[12], v_UV);
    else if (v_TexIndex == 13) texColor = texture(u_Textures[13], v_UV);
    else if (v_TexIndex == 14) texColor = texture(u_Textures[14], v_UV);
    else if (v_TexIndex == 15) texColor = texture(u_Textures[15], v_UV);
    else texColor = texture(u_Textures[0], v_UV);

    FragColor = texColor * v_Color;
}

