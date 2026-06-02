#pragma once

// Centralized OpenGL header include. On macOS the system ships the GL 3.2 core
// profile entry points in <OpenGL/gl3.h>; no loader (GLEW/GLAD) is needed
// because the function pointers are resolved statically against the framework.
// GL_SILENCE_DEPRECATION suppresses Apple's "OpenGL is deprecated" warnings.
//
// On other platforms a real loader would be required; this editor targets
// macOS per the project brief, so we keep the include minimal.

#if defined(__APPLE__)
#  ifndef GL_SILENCE_DEPRECATION
#    define GL_SILENCE_DEPRECATION
#  endif
#  include <OpenGL/gl3.h>
#else
#  include <GL/gl.h>
#endif
