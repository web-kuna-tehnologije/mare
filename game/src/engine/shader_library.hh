// mare::engine::ShaderLibrary - GLSL sources as files (game/assets/shaders/**) instead of
// C string literals.
//
// source(name) loads <root>/<name>, resolves #include "x" lines (paths relative to the
// shaders root, recursive, each file at most once per shader - the shared chunks in
// common/*.glsl exist exactly once on disk), caches the composed text and returns a pointer
// that stays valid for the whole run. The GL backend keeps prepending the #version header
// (rhi_gl.c), so the files carry none.
//
// A missing file or include is fatal with the full path in the message: a shader that
// cannot be found is the same class of error as one that cannot be compiled, and
// rhi_pipeline_make is already fatal on those.
//
// The root is resolved once: MARE_SHADERS overrides; otherwise the first candidate of
// game/assets/shaders probed upward from the working directory (same scheme as asset_root).
//
// C callers (the unported gfx modules) use shader_source() from shaders.h.

#ifndef MARE_ENGINE_SHADER_LIBRARY_HH
#define MARE_ENGINE_SHADER_LIBRARY_HH

#include <memory>
#include <string>
#include <vector>

namespace mare::engine {

class ShaderLibrary {
public:
    static ShaderLibrary &instance();

    // composed GLSL for e.g. "sea/sea.frag"; never NULL (missing files are fatal)
    const char *source(const char *name);

    const char *root() const { return root_.c_str(); }
    int count() const { return (int)cache_.size(); }

private:
    ShaderLibrary() = default;
    void resolve_root();
    std::string compose(const std::string &name, std::vector<std::string> &stack);

    struct Entry { std::string name, text; };
    std::vector<std::unique_ptr<Entry>> cache_;      // pointers into Entry::text stay stable
    std::string root_;
};

} // namespace mare::engine

#endif
