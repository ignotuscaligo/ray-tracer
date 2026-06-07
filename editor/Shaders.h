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

// Flat per-vertex-colored line shader for the ground grid and origin gnomon.
// Each vertex carries its own RGB color (so a single draw call can mix the
// neutral grid lines, the emphasized origin axes, and the red/green/blue gnomon
// arms). No lighting — these are unlit overlay primitives.
inline const char* kLineVertexShader = R"GLSL(
#version 150 core

in vec3 aPosition;
in vec3 aColor;

uniform mat4 uView;
uniform mat4 uProjection;

out vec3 vColor;

void main()
{
    vColor = aColor;
    gl_Position = uProjection * uView * vec4(aPosition, 1.0);
}
)GLSL";

inline const char* kLineFragmentShader = R"GLSL(
#version 150 core

in vec3 vColor;
out vec4 fragColor;

void main()
{
    fragColor = vec4(vColor, 1.0);
}
)GLSL";

// Fullscreen textured-quad shader for the RENDER OVERLAY. When a path-traced
// render (or in-progress progressive snapshot) is shown, the editor draws the
// render texture as a screen-filling quad INTO the viewport FBO, on top of the
// live GL scene. Drawing into the FBO (rather than as an ImGui image over it)
// means screenshot(target=viewport), which reads the FBO, captures the overlay
// too. The vertex shader emits clip-space coordinates directly from a unit quad
// (no view/projection), and passes UVs; the render texture is sampled flipped to
// match the renderer's image orientation.
inline const char* kOverlayVertexShader = R"GLSL(
#version 150 core

in vec2 aPosition;   // clip-space [-1,1]
in vec2 aTexCoord;   // [0,1]

out vec2 vTexCoord;

void main()
{
    vTexCoord = aTexCoord;
    gl_Position = vec4(aPosition, 0.0, 1.0);
}
)GLSL";

inline const char* kOverlayFragmentShader = R"GLSL(
#version 150 core

in vec2 vTexCoord;
uniform sampler2D uTexture;
out vec4 fragColor;

void main()
{
    fragColor = vec4(texture(uTexture, vTexCoord).rgb, 1.0);
}
)GLSL";
