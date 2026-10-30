// mare::engine::rhi::Dx12Device - the Direct3D 12 backend's bring-up (docs/REWRITE_PLAN.md
// T8: "DX12 remains a stub proving the interface is API-neutral").
//
// Built against the Windows SDK headers when they are present, with d3d12.dll and dxgi.dll
// opened at run time, so the game links neither and still starts on a machine without DX12.
// What it does: enumerate adapters through DXGI, prefer the high-performance one, create the
// device, find the highest feature level it supports, create a direct command queue, and
// report all of it. Drawing is delegated to the GL device, exactly as the Vulkan backend does
// until its passes are ported.
//
// This exists to keep the rhi::Device interface honest: if a second and third backend can be
// brought up behind it without the renderer noticing, the abstraction is real rather than a
// GL wrapper wearing a hat.

#include "rhi_device.hh"

#include <cstdio>
#include <cstring>

extern "C" {
#include "../core/diag.h"
}

#if defined(_WIN32) && defined(__has_include)
#  if __has_include(<d3d12.h>) && __has_include(<dxgi1_6.h>)
#    define MARE_DX12_HEADERS 1
#  endif
#endif

#ifdef MARE_DX12_HEADERS
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#endif

namespace mare::engine::rhi {

#ifdef MARE_DX12_HEADERS

class Dx12Device : public DelegatingDevice {
public:
    const char *backend_name() override { return "dx12"; }

    void init() override { gl().init(); if (!attempted_) bring_up(); }

    const char *info() override {
        if (!attempted_) bring_up();
        static char buf[512];
        if (ready_) snprintf(buf, sizeof buf, "D3D12 %s, %u MB dedicated, feature level %u.%u, direct queue ready (bring-up: drawing via GL) | %s",
                             adapter_, vram_mb_, fl_major_, fl_minor_, gl().info());
        else        snprintf(buf, sizeof buf, "D3D12 unavailable (%s): drawing via GL | %s", why_, gl().info());
        return buf;
    }
    int check_errors(const char *where) override { return gl().check_errors(where); }

    ~Dx12Device() override {
        if (queue_) queue_->Release();
        if (device_) device_->Release();
        if (adapter_obj_) adapter_obj_->Release();
        if (factory_) factory_->Release();
    }

private:
    int ready_ = 0, attempted_ = 0;
    char why_[160] = "not initialised";
    char adapter_[160] = "";
    unsigned vram_mb_ = 0, fl_major_ = 0, fl_minor_ = 0;
    IDXGIFactory6 *factory_ = nullptr;
    IDXGIAdapter1 *adapter_obj_ = nullptr;
    ID3D12Device *device_ = nullptr;
    ID3D12CommandQueue *queue_ = nullptr;

    void bring_up() {
        attempted_ = 1;
        HMODULE d3d12 = LoadLibraryA("d3d12.dll"), dxgi = LoadLibraryA("dxgi.dll");
        if (!d3d12 || !dxgi) { snprintf(why_, sizeof why_, "d3d12.dll or dxgi.dll is not present"); LOG_WARN("rhi", "dx12: %s", why_); return; }
        auto create_device = (PFN_D3D12_CREATE_DEVICE)GetProcAddress(d3d12, "D3D12CreateDevice");
        typedef HRESULT (WINAPI *PFN_CreateDXGIFactory2)(UINT, REFIID, void **);
        auto create_factory = (PFN_CreateDXGIFactory2)GetProcAddress(dxgi, "CreateDXGIFactory2");
        if (!create_device || !create_factory) { snprintf(why_, sizeof why_, "entry points missing from the DX12 runtime"); LOG_WARN("rhi", "dx12: %s", why_); return; }

        if (FAILED(create_factory(0, IID_PPV_ARGS(&factory_))) || !factory_) {
            snprintf(why_, sizeof why_, "CreateDXGIFactory2 failed"); LOG_WARN("rhi", "dx12: %s", why_); return; }
        // the same preference the Vulkan backend applies: the discrete GPU where there is one
        if (FAILED(factory_->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter_obj_))))
            factory_->EnumAdapters1(0, &adapter_obj_);
        if (!adapter_obj_) { snprintf(why_, sizeof why_, "no DXGI adapter"); LOG_WARN("rhi", "dx12: %s", why_); return; }

        DXGI_ADAPTER_DESC1 desc = {};
        adapter_obj_->GetDesc1(&desc);
        WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, adapter_, (int)sizeof adapter_ - 1, nullptr, nullptr);
        vram_mb_ = (unsigned)(desc.DedicatedVideoMemory / (1024 * 1024));

        static const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1,
                                                    D3D_FEATURE_LEVEL_12_0, D3D_FEATURE_LEVEL_11_1,
                                                    D3D_FEATURE_LEVEL_11_0 };
        for (D3D_FEATURE_LEVEL lv : levels)
            if (SUCCEEDED(create_device(adapter_obj_, lv, IID_PPV_ARGS(&device_))) && device_) {
                fl_major_ = (unsigned)(lv >> 12) & 0xF; fl_minor_ = (unsigned)(lv >> 8) & 0xF; break;
            }
        if (!device_) { snprintf(why_, sizeof why_, "D3D12CreateDevice failed on %s", adapter_); LOG_WARN("rhi", "dx12: %s", why_); return; }

        D3D12_COMMAND_QUEUE_DESC qd = {};
        qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT; qd.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        if (FAILED(device_->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue_))) || !queue_) {
            snprintf(why_, sizeof why_, "CreateCommandQueue failed"); LOG_WARN("rhi", "dx12: %s", why_); return; }

        ready_ = 1;
        LOG_INFO("rhi", "dx12: %s, %u MB dedicated, feature level %u.%u, direct command queue created; drawing stays on GL (T8: pipelines, descriptor heaps and a swapchain still to come)",
                 adapter_, vram_mb_, fl_major_, fl_minor_);
    }
};

#else  // no Windows SDK headers (or not Windows)

class Dx12Device : public DelegatingDevice {
public:
    const char *backend_name() override { return "dx12"; }
    void init() override { gl().init(); LOG_WARN("rhi", "this build has no Direct3D 12 headers: drawing via GL"); }
    const char *info() override {
        static char buf[384];
        snprintf(buf, sizeof buf, "D3D12 not compiled in (no Windows SDK at build time) | %s", gl().info());
        return buf;
    }
    int check_errors(const char *where) override { return gl().check_errors(where); }
};

#endif

Device &dx12_device() { static Dx12Device d; return d; }

} // namespace mare::engine::rhi
