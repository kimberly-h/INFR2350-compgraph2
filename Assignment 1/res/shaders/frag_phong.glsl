#version 410

//Referenced from Richard Pazzi, Computer Graphics: Year 2 Sem 1, Lecture 5

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec2 inUV;

uniform sampler2D s_Diffuse;
uniform sampler2D s_Specular;

uniform vec3  u_AmbientCol;
uniform float u_AmbientStrength;

uniform vec3  u_LightPos;
uniform vec3  u_LightCol;
uniform float u_AmbientLightStrength;
uniform float u_SpecularLightStrength;
uniform float u_Shininess;
uniform float u_TextureMix;
uniform vec3  u_CamPos;

uniform float u_LightAttenuationConstant;
uniform float u_LightAttenuationLinear;
uniform float u_LightAttenuationQuadratic;

uniform float u_toonShading;

out vec4 frag_color;

//Toon Shading
const int bands = 5;
const float scaling = 2.0/bands;


void main() {
    // Lecture 5
    vec3 ambient = ((u_AmbientLightStrength * u_LightCol) + (u_AmbientCol * u_AmbientStrength));

    // Diffuse
    vec3 N = normalize(inNormal);
    vec3 lightDir = normalize(u_LightPos - inPos);

    float dif = max(dot(N, lightDir), 0.0);
    vec3 diffuse = dif * u_LightCol;// add diffuse intensity

    //Toon Shading
    if(u_toonShading == 0)
    {
        diffuse = floor(diffuse * bands) * scaling; 
    }

    //Attenuation
    float dist = length(u_LightPos - inPos);
    float attenuation = 1.0f / (
        u_LightAttenuationConstant + 
        u_LightAttenuationLinear * dist +
        u_LightAttenuationQuadratic * dist * dist); // (dist*dist)

    // Specular
    vec3 camPos = u_CamPos;//Pass this as a uniform from your C++ code
    float specularStrength = 1.0; // this can be a uniform
    vec3 camDir = normalize(camPos - inPos);
    vec3 reflectDir = reflect(-lightDir, N);

    float texSpec = texture(s_Specular, inUV).x;
    float spec = pow(max(dot(camDir, reflectDir), 0.0), u_Shininess); // Shininess coefficient (can be a uniform)
    vec3 specular = u_SpecularLightStrength *texSpec * spec * u_LightCol; // Can also use a specular color

    vec4 textureColor1 = texture(s_Diffuse, inUV);
    vec4 textureColor = mix(textureColor1, textureColor1, u_TextureMix);

    vec3 result = ((ambient + diffuse + specular)* attenuation) * inColor * textureColor.rgb;

    frag_color = vec4(result, textureColor.a);
}