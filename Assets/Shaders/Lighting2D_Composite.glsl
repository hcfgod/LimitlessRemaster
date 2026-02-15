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
uniform sampler2D u_LightTexture;

void main()
{
    vec4 albedo = texture(u_AlbedoTexture, v_UV);
    vec3 lighting = texture(u_LightTexture, v_UV).rgb;
    FragColor = vec4(albedo.rgb * lighting, albedo.a);
}

