#pragma once

// GLSL source for the viewport mesh shader. Targets GLSL 1.50 (OpenGL 3.2 core
// profile, the highest the macOS GL stack exposes). The fragment shader does a
// simple two-term Lambert-ish shade: a headlight diffuse term plus a small
// ambient floor, tinted by a uniform base color. This is a preview shade, not a
// physically-based one — the path tracer is the ground truth.

inline const char* kViewportVertexShader = R"GLSL(
#version 150 core

in vec3 aPosition;
in vec3 aNormal;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

out vec3 vNormalWorld;

void main()
{
    // Normals are transformed by the model matrix only (the editor uses a pure
    // identity/rigid model transform, so this is adequate without the inverse
    // transpose).
    vNormalWorld = mat3(uModel) * aNormal;
    gl_Position = uProjection * uView * uModel * vec4(aPosition, 1.0);
}
)GLSL";

inline const char* kViewportFragmentShader = R"GLSL(
#version 150 core

in vec3 vNormalWorld;

uniform vec3 uBaseColor;
uniform vec3 uLightDir;  // direction TO the light, world space, normalized

out vec4 fragColor;

void main()
{
    vec3 n = normalize(vNormalWorld);
    // Two-sided shading so back faces of open meshes aren't black.
    float ndotl = abs(dot(n, normalize(uLightDir)));
    float ambient = 0.18;
    float diffuse = 0.82 * ndotl;
    vec3 color = uBaseColor * (ambient + diffuse);
    fragColor = vec4(color, 1.0);
}
)GLSL";
