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

// Selection-outline shader. Used by the stencil-buffer outline pass: the
// selected object is re-drawn slightly enlarged (the model matrix is scaled
// about the object's center on the CPU) and shaded a single flat color, only
// where the stencil buffer is unmarked — producing a silhouette edge around
// the object. Shares the aPosition attribute layout of the mesh shader so the
// same VAOs draw with it.
inline const char* kOutlineVertexShader = R"GLSL(
#version 150 core

in vec3 aPosition;
in vec3 aNormal;  // declared so the mesh VAO's attrib layout matches; unused

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

void main()
{
    gl_Position = uProjection * uView * uModel * vec4(aPosition, 1.0);
}
)GLSL";

inline const char* kOutlineFragmentShader = R"GLSL(
#version 150 core

uniform vec3 uOutlineColor;
out vec4 fragColor;

void main()
{
    fragColor = vec4(uOutlineColor, 1.0);
}
)GLSL";

// Screen-space selection-outline EDGE shader. Pairs with the outline mask pass:
// the selected object is first rendered as a white silhouette into a single-
// channel mask texture. This fullscreen pass samples that mask and paints the
// outline color into the viewport wherever a fragment is OUTSIDE the silhouette
// but within `uThickness` texels of an inside fragment — a uniform-width ring
// that hugs the silhouette regardless of object shape (thin meshes, spheres,
// walls all get the same crisp outline). Fragments inside the silhouette and far
// outside it are discarded so the object's own shaded color shows through.
inline const char* kOutlineEdgeFragmentShader = R"GLSL(
#version 150 core

in vec2 vTexCoord;
uniform sampler2D uMask;     // R = 1 inside the selected silhouette, 0 outside
uniform vec2 uTexel;         // 1/maskWidth, 1/maskHeight
uniform float uThickness;    // outline half-width in texels
uniform vec3 uOutlineColor;

out vec4 fragColor;

void main()
{
    // The fullscreen quad's V is flipped (it's shared with the render-texture
    // overlay, whose source is top-left-origin). The mask, by contrast, is
    // rendered in the same bottom-left-origin space as the viewport FBO, so
    // un-flip V here to sample the mask texel under each screen fragment.
    vec2 maskUv = vec2(vTexCoord.x, 1.0 - vTexCoord.y);
    float here = texture(uMask, maskUv).r;
    if (here > 0.5)
    {
        discard;  // inside the object: keep its shaded color
    }
    // Outside: outline if any neighbor within uThickness is inside the silhouette.
    float maxN = 0.0;
    int r = int(uThickness);
    for (int dy = -r; dy <= r; ++dy)
    {
        for (int dx = -r; dx <= r; ++dx)
        {
            vec2 uv = maskUv + vec2(float(dx), float(dy)) * uTexel;
            maxN = max(maxN, texture(uMask, uv).r);
        }
    }
    if (maxN > 0.5)
    {
        fragColor = vec4(uOutlineColor, 1.0);
    }
    else
    {
        discard;
    }
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
