#pragma once

#include <string>
#include <vector>

// The editor's in-memory scene model.
//
// KEY DESIGN DECISION: the editor's project format IS the renderer's scene JSON
// (the same format `ray-tracer <scene>.json` reads — see SceneLoader.cpp for the
// schema: Camera, AreaLight/OmniLight, CornellBox objects, SphereVolume/
// MeshVolume with $center/$radius/$material, materials with $type Lambertian/
// Mirror/Glass, and a render config). So this model is structured to map cleanly
// onto that JSON when save/load and object insertion land in later waves.
//
// Wave 1 scope: an EMPTY scene only. The object/light/material/camera vectors
// below are intentionally declared but empty — they are the seams the next waves
// fill in (object insertion populates `objects`; save serializes the whole
// struct to the renderer JSON; render-from-view reads `cameras`). No GL state
// lives here; this is pure data so it can be unit-tested and serialized without
// a GL context, mirroring the MeshData/RasterMesh split elsewhere in the editor.
class Scene
{
public:
    // ----- placeholders for later waves (kept empty in Wave 1) --------------
    //
    // These deliberately use plain strings/vectors rather than concrete renderer
    // types so the editor target stays free of a hard dependency on the loader's
    // object hierarchy until insertion/serialization actually needs it. When
    // those waves land, replace these with structs that round-trip the JSON
    // ($center/$radius/$material, $type, etc.).

    // Object/volume entries (SphereVolume, MeshVolume, CornellBox, ...).
    struct ObjectPlaceholder
    {
        std::string typeName;  // e.g. "SphereVolume"
    };
    // Light entries (AreaLight, OmniLight).
    struct LightPlaceholder
    {
        std::string typeName;
    };

    std::vector<ObjectPlaceholder> objects;
    std::vector<LightPlaceholder> lights;

    // A human-facing name for the project, shown in the title bar / save dialog.
    std::string name = "untitled";

    // True once content has been added or the scene has been loaded from disk.
    // A freshly-`reset()` scene is empty/clean. Later waves flip this on edits so
    // "Save" can warn about unsaved changes.
    bool dirty = false;

    // Path the scene was loaded from / last saved to. Empty for a New File that
    // has never been saved. Later waves use this for Save vs. Save As.
    std::string path;

    // Initialize an empty scene (the File > New action). Clears all content and
    // returns the model to its pristine, ready-to-edit state.
    void reset()
    {
        objects.clear();
        lights.clear();
        name = "untitled";
        path.clear();
        dirty = false;
        m_initialized = true;
    }

    // True once New (or a load) has run. The editor shows the empty-scene
    // viewport only after a scene exists, so a just-launched editor with no
    // active scene can present a neutral/empty state.
    bool isInitialized() const { return m_initialized; }

    bool isEmpty() const { return objects.empty() && lights.empty(); }

private:
    bool m_initialized = false;
};
