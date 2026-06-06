#pragma once

#include <cstdint>

// ============================================================================
// InputEvent: the single input abstraction the whole editor consumes.
//
// There is exactly ONE input path in the app. Two sources feed it:
//   1. The OS layer (GLFW callbacks) TRANSLATES raw GLFW events into InputEvents.
//   2. The debug/automation port INJECTS InputEvents directly.
// Both go through EditorApp::dispatchInputEvent(), which (a) feeds ImGui's IO and
// (b) updates app state (camera nav, future UI handlers). App code must NEVER read
// raw GLFW input directly — it only ever sees InputEvents. This is also the
// cross-platform seam: app logic is decoupled from the OS input source.
//
// Button / key / mods values are GLFW's numeric codes (GLFW_MOUSE_BUTTON_LEFT,
// GLFW_KEY_*, GLFW_MOD_*). Using GLFW's namespace keeps the real-input translation
// a pass-through and lets injected events use the same well-known constants. The
// abstraction is the *event shape* and the *single path*, not a re-encoding of
// every constant.
//
// Coordinates are in window/framebuffer pixels (top-left origin, matching GLFW's
// cursor convention), so an injected click at a rect returned by query_layout
// lands where the agent expects.
// ============================================================================
struct InputEvent
{
    enum class Type
    {
        MouseMove,    // x, y      — cursor position in window pixels
        MouseButton,  // button, down (true=press, false=release), mods
        Scroll,       // dx, dy    — scroll offsets (dy>0 = scroll up)
        Key,          // key, down, mods
        Char,         // codepoint — a translated text character
    };

    Type type;

    // MouseMove
    double x = 0.0;
    double y = 0.0;

    // MouseButton / Key
    int button = 0;  // also used as the key code for Type::Key
    bool down = false;
    int mods = 0;

    // Scroll
    double dx = 0.0;
    double dy = 0.0;

    // Char
    unsigned int codepoint = 0;

    static InputEvent mouseMove(double x, double y)
    {
        InputEvent e;
        e.type = Type::MouseMove;
        e.x = x;
        e.y = y;
        return e;
    }

    static InputEvent mouseButton(int button, bool down, int mods)
    {
        InputEvent e;
        e.type = Type::MouseButton;
        e.button = button;
        e.down = down;
        e.mods = mods;
        return e;
    }

    static InputEvent scroll(double dx, double dy)
    {
        InputEvent e;
        e.type = Type::Scroll;
        e.dx = dx;
        e.dy = dy;
        return e;
    }

    static InputEvent key(int key, bool down, int mods)
    {
        InputEvent e;
        e.type = Type::Key;
        e.button = key;
        e.down = down;
        e.mods = mods;
        return e;
    }

    static InputEvent character(unsigned int codepoint)
    {
        InputEvent e;
        e.type = Type::Char;
        e.codepoint = codepoint;
        return e;
    }
};
