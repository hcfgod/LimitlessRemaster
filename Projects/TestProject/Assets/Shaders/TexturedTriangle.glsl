#type vertex
#version 330 core

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec2 a_UV;

uniform mat4 u_ViewProjection;
uniform mat4 u_Model;

out vec2 v_UV;

void main()
{
    v_UV = a_UV;
    gl_Position = u_ViewProjection * u_Model * vec4(a_Position, 1.0);
}

#type fragment
#version 330 core

in vec2 v_UV;
out vec4 FragColor;

uniform sampler2D u_Texture;

void main()
{
    FragColor = texture(u_Texture, v_UV);
}

