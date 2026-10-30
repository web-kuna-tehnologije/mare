// mare::engine::ShaderLibrary implementation (see shader_library.hh).

#include "shader_library.hh"
#include "shaders.h"

extern "C" {
#include "../core/diag.h"
}

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace mare::engine {
namespace {

bool read_file(const std::string &path, std::string &out) {
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? (size_t)n : 0);
    size_t got = n > 0 ? fread(&out[0], 1, (size_t)n, f) : 0;
    fclose(f);
    out.resize(got);
    return true;
}

bool file_exists(const std::string &path) {
    FILE *f = fopen(path.c_str(), "rb");
    if (f) fclose(f);
    return f != nullptr;
}

} // namespace

ShaderLibrary &ShaderLibrary::instance() {
    static ShaderLibrary lib;
    return lib;
}

void ShaderLibrary::resolve_root() {
    if (!root_.empty()) return;
    if (const char *env = getenv("MARE_SHADERS")) { root_ = env; return; }
    // the tracked shader sources live in game/assets/shaders; probe upward from wherever the
    // game was started (repo root, a build dir, a staged copy) - same scheme as asset_root()
    static const char *candidates[] = {
        "game/assets/shaders", "../game/assets/shaders", "../../game/assets/shaders",
        "../../../game/assets/shaders", "../../../../game/assets/shaders", "assets/shaders",
    };
    for (const char *c : candidates)
        if (file_exists(std::string(c) + "/common/color.glsl")) { root_ = c; return; }
    root_ = "game/assets/shaders";   // the fatal in compose() will name the full path
}

const char *ShaderLibrary::source(const char *name) {
    for (const auto &e : cache_)
        if (e->name == name) return e->text.c_str();
    resolve_root();
    std::vector<std::string> stack;
    auto e = std::make_unique<Entry>();
    e->name = name;
    e->text = compose(name, stack);
    cache_.push_back(std::move(e));
    return cache_.back()->text.c_str();
}

// Load one file and splice its #include "x" lines (recursive; a file repeated in one shader -
// e.g. common/waves.glsl pulled by two chunks - is spliced only on its first appearance).
std::string ShaderLibrary::compose(const std::string &name, std::vector<std::string> &stack) {
    for (const auto &s : stack)
        if (s == name) return "";                     // already spliced into this shader
    stack.push_back(name);

    std::string path = root_ + "/" + name, text;
    if (!read_file(path, text))
        diag_log(LOG_FATAL, "shader", "shader_library.cc:0", "shader file missing: %s (root %s, MARE_SHADERS overrides)", path.c_str(), root_.c_str());

    std::string out;
    out.reserve(text.size());
    size_t pos = 0;
    while (pos < text.size()) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) eol = text.size(); else ++eol;
        std::string line = text.substr(pos, eol - pos);
        pos = eol;
        const char *p = line.c_str();
        while (*p == ' ' || *p == '\t') ++p;
        if (!strncmp(p, "#include", 8)) {
            const char *q0 = strchr(p + 8, '"');
            const char *q1 = q0 ? strchr(q0 + 1, '"') : nullptr;
            if (!q0 || !q1)
                diag_log(LOG_FATAL, "shader", "shader_library.cc:0", "malformed #include in %s: %s", name.c_str(), line.c_str());
            out += compose(std::string(q0 + 1, q1), stack);
        } else {
            out += line;
        }
    }
    return out;
}

} // namespace mare::engine

// C ABI (shaders.h) for the unported gfx modules
extern "C" const char *shader_source(const char *name) {
    return mare::engine::ShaderLibrary::instance().source(name);
}
