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
uniform vec2 u_SceneUvOffset;
uniform vec2 u_SceneUvScale;

void main()
{
    vec2 sceneUv = u_SceneUvOffset + v_UV * u_SceneUvScale;
    vec4 albedo = texture(u_AlbedoTexture, sceneUv);
    if (albedo.a <= 0.01)
    {
        FragColor = vec4(0.0);
        return;
    }
    vec3 lighting = texture(u_LightTexture, sceneUv).rgb;
    FragColor = vec4(albedo.rgb * lighting, albedo.a);
}

