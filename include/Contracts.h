#pragma once

// ============================================================================
// Contracts.h — design-by-contract vocabulary for the renderer.
//
// Provides PRECONDITION / POSTCONDITION / INVARIANT / CHECK macros for asserting
// contracts at MODULE BOUNDARIES (scene-file input, public BSDF interface, the
// camera-splat deposit). These are NOT meant to be sprinkled at every internal
// call — that is validation-theater. Put them where bad data crosses an edge.
//
// Activation model (zero release overhead is mandatory — the photon inner loop
// runs billions of times):
//
//   * Active   when RAYTRACER_CONTRACTS_ENABLED == 1. This is the case in debug
//     builds (NDEBUG not defined) OR sanitizer builds (RAYTRACER_SANITIZE set,
//     surfaced to the preprocessor as the RAYTRACER_SANITIZE_BUILD define from
//     CMake). On failure: print "file:line: CONTRACT <kind> failed: <cond>"
//     (plus an optional message) to stderr, then std::abort() so the failure is
//     catchable by the sanitizer/test harness.
//
//   * Inert    in release (NDEBUG defined and no sanitizer). The macros expand to
//     a discarded sizeof-expression so the condition is still parsed/type-checked
//     by the compiler (no "unused variable" warnings) but generates NO code and
//     is NOT evaluated — verified by the release-build assembly having no abort
//     path. The condition's side effects, if any, do NOT run in release, so
//     contract conditions must be PURE (this is the standard assert() rule).
// ============================================================================

// Decide whether contracts are active. CMake defines RAYTRACER_SANITIZE_BUILD on
// sanitizer builds (see CMakeLists.txt); NDEBUG is defined by Release configs.
#if !defined(RAYTRACER_CONTRACTS_ENABLED)
#  if defined(RAYTRACER_SANITIZE_BUILD) || !defined(NDEBUG)
#    define RAYTRACER_CONTRACTS_ENABLED 1
#  else
#    define RAYTRACER_CONTRACTS_ENABLED 0
#  endif
#endif

#if RAYTRACER_CONTRACTS_ENABLED

#include <cstdio>
#include <cstdlib>

namespace contracts
{
// Out-of-line so the macro expansion stays small and the include is header-only.
// `kind` is "PRECONDITION" / "POSTCONDITION" / "INVARIANT" / "CHECK"; `expr` is
// the stringized condition; `message` may be nullptr.
[[noreturn]] inline void fail(const char* kind,
                              const char* expr,
                              const char* file,
                              int line,
                              const char* message) noexcept
{
    if (message != nullptr)
    {
        std::fprintf(stderr,
                     "%s:%d: CONTRACT %s failed: %s  (%s)\n",
                     file, line, kind, expr, message);
    }
    else
    {
        std::fprintf(stderr,
                     "%s:%d: CONTRACT %s failed: %s\n",
                     file, line, kind, expr);
    }
    std::fflush(stderr);
    std::abort();
}
}  // namespace contracts

#define RAYTRACER_CONTRACT_CHECK(kind, cond, message)                       \
    do                                                                      \
    {                                                                       \
        if (!(cond))                                                        \
        {                                                                   \
            ::contracts::fail((kind), #cond, __FILE__, __LINE__, (message));\
        }                                                                   \
    } while (false)

#else  // contracts inert

// Parse + type-check the condition (catches typos / stale references in release
// too) but emit no code and never evaluate it. `sizeof` makes the expression
// unevaluated; the `(void)` discards it. The message arg is referenced the same
// way so an unused-variable message string doesn't warn.
#define RAYTRACER_CONTRACT_CHECK(kind, cond, message) \
    ((void)sizeof((cond) ? 0 : 0), (void)sizeof(message))

#endif  // RAYTRACER_CONTRACTS_ENABLED

// ===== Public vocabulary =====
//
// Two forms each: a bare condition, and a *_MSG form taking an explanatory
// message. Use PRECONDITION at the top of a boundary function (caller's fault),
// POSTCONDITION before returning (callee's guarantee), INVARIANT for a condition
// that must hold mid-computation, CHECK for a general boundary assertion.

#define PRECONDITION(cond)            RAYTRACER_CONTRACT_CHECK("PRECONDITION", (cond), nullptr)
#define PRECONDITION_MSG(cond, msg)   RAYTRACER_CONTRACT_CHECK("PRECONDITION", (cond), (msg))

#define POSTCONDITION(cond)           RAYTRACER_CONTRACT_CHECK("POSTCONDITION", (cond), nullptr)
#define POSTCONDITION_MSG(cond, msg)  RAYTRACER_CONTRACT_CHECK("POSTCONDITION", (cond), (msg))

#define INVARIANT(cond)               RAYTRACER_CONTRACT_CHECK("INVARIANT", (cond), nullptr)
#define INVARIANT_MSG(cond, msg)      RAYTRACER_CONTRACT_CHECK("INVARIANT", (cond), (msg))

// NOTE: named CONTRACT_CHECK, not CHECK, to avoid colliding with Catch2's CHECK
// assertion macro in test translation units.
#define CONTRACT_CHECK(cond)          RAYTRACER_CONTRACT_CHECK("CHECK", (cond), nullptr)
#define CONTRACT_CHECK_MSG(cond, msg) RAYTRACER_CONTRACT_CHECK("CHECK", (cond), (msg))
