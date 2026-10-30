// mare::engine::rhi::Dx12Device - the DX12 stub of the rewrite plan (T8: "DX12 remains a stub
// proving the interface is API-neutral"). It checks that the runtime is present, says so, and
// hands everything to the GL device. Nothing here draws.

#include "rhi_device.hh"

#include <cstdio>

extern "C" {
#include "../core/diag.h"
}

#ifdef _WIN32
#include <windows.h>
#endif

namespace mare::engine::rhi {

class Dx12Device : public DelegatingDevice {
public:
    const char *backend_name() override { return "dx12"; }
    void init() override {
        gl().init();
#ifdef _WIN32
        HMODULE m = LoadLibraryA("d3d12.dll");
        present = m != nullptr;
        if (m) FreeLibrary(m);
#endif
        LOG_WARN("rhi", "dx12 backend is a stub: runtime %s; drawing via GL", present ? "present" : "absent");
    }
    const char *info() override {
        static char buf[384];
        snprintf(buf, sizeof buf, "DX12 stub (runtime %s, drawing via GL) | %s", present ? "present" : "absent", gl().info());
        return buf;
    }
    int check_errors(const char *where) override { return gl().check_errors(where); }
private:
    int present = 0;
};

Device &dx12_device() { static Dx12Device d; return d; }

} // namespace mare::engine::rhi
