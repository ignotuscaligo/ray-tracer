#pragma once

#include <string>
#include <unordered_map>

// ============================================================================
// LayoutRegistry: a per-frame map of {element_name -> pixel rect} that the app
// rebuilds each frame and the automation port reads via query_layout. This is
// how a driving agent finds where to click: it asks for the rect of a named UI
// element (a menu item, a panel, the viewport, or any registered widget) and
// then injects a click at the rect's center.
//
// Rects are in window/framebuffer pixels with a top-left origin, matching the
// coordinate space injected mouse events use (see InputEvent.h). ImGui's
// GetItemRectMin/Max already report screen-space coordinates relative to the
// main window's top-left, which is the same space, so widget rects can be
// recorded with a direct ImVec2 -> Rect copy.
//
// Threading: the registry is written only on the main/GL thread (during the UI
// build each frame) and read on the main/GL thread (inside the automation
// drain()). No locking is needed because both happen on the same thread; the
// automation worker thread never touches it directly.
// ============================================================================
struct LayoutRect
{
    float x = 0.0f;       // left, window pixels
    float y = 0.0f;       // top, window pixels
    float width = 0.0f;
    float height = 0.0f;

    float centerX() const { return x + width * 0.5f; }
    float centerY() const { return y + height * 0.5f; }
    bool valid() const { return width > 0.0f && height > 0.0f; }
};

class LayoutRegistry
{
public:
    // Clear the registry at the top of each frame, before the UI is built.
    void beginFrame() { m_rects.clear(); }

    // Record a named element's rect. Later writes for the same name overwrite
    // earlier ones (last-one-wins within a frame).
    void record(const std::string& name, const LayoutRect& rect) { m_rects[name] = rect; }

    void record(const std::string& name, float x, float y, float width, float height)
    {
        m_rects[name] = LayoutRect{x, y, width, height};
    }

    // Look up a rect by name. Returns true and fills `out` if present.
    bool find(const std::string& name, LayoutRect& out) const
    {
        auto it = m_rects.find(name);
        if (it == m_rects.end()) return false;
        out = it->second;
        return true;
    }

    const std::unordered_map<std::string, LayoutRect>& all() const { return m_rects; }

private:
    std::unordered_map<std::string, LayoutRect> m_rects;
};
