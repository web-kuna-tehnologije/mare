// mare::engine::rhi::VulkanDevice - the Vulkan backend's bring-up slice (docs/REWRITE_PLAN.md
// T8, first step). What it does today:
//
//   * loads the Vulkan loader at run time (vulkan-1.dll / libvulkan.so.1) - no SDK, no headers
//     and no link dependency, so a machine without Vulkan simply logs it and the game runs on GL;
//   * creates an instance, picks the physical device (a discrete GPU when there is one), reads
//     its properties and memory heaps, creates a logical device with one graphics queue;
//   * reports all of it through rhi_info() and the log.
//
// What it does not do yet: own a swapchain or draw. Every resource and draw call is delegated
// to the GL device (DelegatingDevice), so `--rhi=vulkan` runs the whole game with Vulkan open
// beside it. The remaining T8 work - SPIR-V for the shader library (a glslang step in the asset
// pipeline), render passes, pipeline objects baked from rhi_pipeline_desc_t, per-pipeline
// uniform blocks, the swapchain over the v3 window - lands behind this same interface.
//
// The handful of Vulkan declarations needed for that are written out here: the core structs
// involved are stable ABI, and keeping the game free of a Vulkan SDK dependency is the point.

#include "rhi_device.hh"

#include <cstring>
#include <cstdlib>
#include <cstdio>

extern "C" {
#include "../core/diag.h"
}

#ifdef _WIN32
#include <windows.h>
#define VKCALL __stdcall
#else
#include <dlfcn.h>
#define VKCALL
#endif

namespace mare::engine::rhi {

// --- the slice of the Vulkan core API this file uses (vulkan_core.h, 1.0 layouts)
namespace vk {
typedef unsigned int Flags; typedef unsigned int Bool32; typedef unsigned long long DeviceSize;
typedef struct Instance_T *Instance; typedef struct PhysicalDevice_T *PhysicalDevice;
typedef struct Device_T *Device; typedef struct Queue_T *Queue;
enum { STRUCTURE_TYPE_APPLICATION_INFO = 0, STRUCTURE_TYPE_INSTANCE_CREATE_INFO = 1,
       STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO = 2, STRUCTURE_TYPE_DEVICE_CREATE_INFO = 3 };
enum { SUCCESS = 0, QUEUE_GRAPHICS_BIT = 1, MEMORY_HEAP_DEVICE_LOCAL_BIT = 1 };
enum { PHYSICAL_DEVICE_TYPE_DISCRETE_GPU = 2 };
struct ApplicationInfo { int sType; const void *pNext; const char *pApplicationName; unsigned applicationVersion; const char *pEngineName; unsigned engineVersion; unsigned apiVersion; };
struct InstanceCreateInfo { int sType; const void *pNext; Flags flags; const ApplicationInfo *pApplicationInfo; unsigned enabledLayerCount; const char *const *ppEnabledLayerNames; unsigned enabledExtensionCount; const char *const *ppEnabledExtensionNames; };
struct QueueFamilyProperties { Flags queueFlags; unsigned queueCount; unsigned timestampValidBits; unsigned minImageTransferGranularity[3]; };
struct DeviceQueueCreateInfo { int sType; const void *pNext; Flags flags; unsigned queueFamilyIndex; unsigned queueCount; const float *pQueuePriorities; };
struct DeviceCreateInfo { int sType; const void *pNext; Flags flags; unsigned queueCreateInfoCount; const DeviceQueueCreateInfo *pQueueCreateInfos; unsigned enabledLayerCount; const char *const *ppEnabledLayerNames; unsigned enabledExtensionCount; const char *const *ppEnabledExtensionNames; const void *pEnabledFeatures; };
struct MemoryType { Flags propertyFlags; unsigned heapIndex; };
struct MemoryHeap { DeviceSize size; Flags flags; unsigned pad_; };
struct PhysicalDeviceMemoryProperties { unsigned memoryTypeCount; MemoryType memoryTypes[32]; unsigned memoryHeapCount; MemoryHeap memoryHeaps[16]; };
// VkPhysicalDeviceProperties is 824 bytes; only its head is read, by offset, from a buffer
// large enough for any 1.x revision (the struct only ever grows through pNext chains)
enum { PROPS_BYTES = 2048, PROPS_OFF_API = 0, PROPS_OFF_DRIVER = 4, PROPS_OFF_VENDOR = 8, PROPS_OFF_DEVICE = 12, PROPS_OFF_TYPE = 16, PROPS_OFF_NAME = 20, PROPS_NAME_LEN = 256 };
typedef void (VKCALL *PFN_VoidFunction)(void);
typedef PFN_VoidFunction (VKCALL *PFN_GetInstanceProcAddr)(Instance, const char *);
typedef int  (VKCALL *PFN_CreateInstance)(const InstanceCreateInfo *, const void *, Instance *);
typedef int  (VKCALL *PFN_EnumerateInstanceVersion)(unsigned *);
typedef int  (VKCALL *PFN_EnumeratePhysicalDevices)(Instance, unsigned *, PhysicalDevice *);
typedef void (VKCALL *PFN_GetPhysicalDeviceProperties)(PhysicalDevice, void *);
typedef void (VKCALL *PFN_GetPhysicalDeviceQueueFamilyProperties)(PhysicalDevice, unsigned *, QueueFamilyProperties *);
typedef void (VKCALL *PFN_GetPhysicalDeviceMemoryProperties)(PhysicalDevice, PhysicalDeviceMemoryProperties *);
typedef int  (VKCALL *PFN_CreateDevice)(PhysicalDevice, const DeviceCreateInfo *, const void *, Device *);
typedef void (VKCALL *PFN_GetDeviceQueue)(Device, unsigned, unsigned, Queue *);
typedef void (VKCALL *PFN_DestroyDevice)(Device, const void *);
typedef void (VKCALL *PFN_DestroyInstance)(Instance, const void *);
static inline unsigned version_major(unsigned v) { return v >> 22; }
static inline unsigned version_minor(unsigned v) { return (v >> 12) & 0x3FF; }
static inline unsigned version_patch(unsigned v) { return v & 0xFFF; }
static inline unsigned make_version(unsigned a, unsigned b, unsigned c) { return (a << 22) | (b << 12) | c; }
} // namespace vk

class VulkanDevice : public DelegatingDevice {
public:
    const char *backend_name() override { return "vulkan"; }

    void init() override {
        gl().init();                       // drawing stays on GL until the pipeline work lands
        if (!attempted) bring_up();
    }
    const char *info() override {
        static char buf[512];
        if (!attempted) bring_up();        // the driver logs rhi_info() before rhi_init: report either way
        if (ready) snprintf(buf, sizeof buf, "Vulkan %u.%u %s, %u MB device-local, %u queue families (bring-up: drawing via GL) | %s",
                            api_major, api_minor, adapter, vram_mb, nfamilies, gl().info());
        else snprintf(buf, sizeof buf, "Vulkan unavailable (%s): drawing via GL | %s", why, gl().info());
        return buf;
    }
    int check_errors(const char *where) override { return gl().check_errors(where); }

    ~VulkanDevice() override {
        if (device && pDestroyDevice) pDestroyDevice(device, nullptr);
        if (instance && pDestroyInstance) pDestroyInstance(instance, nullptr);
    }

private:
    int ready = 0, attempted = 0; char why[128] = "not initialised";
    char adapter[vk::PROPS_NAME_LEN] = "";
    unsigned api_major = 0, api_minor = 0, vram_mb = 0, nfamilies = 0, graphics_family = 0;
    vk::Instance instance = nullptr; vk::PhysicalDevice phys = nullptr; vk::Device device = nullptr; vk::Queue queue = nullptr;
    vk::PFN_DestroyDevice pDestroyDevice = nullptr; vk::PFN_DestroyInstance pDestroyInstance = nullptr;

    static vk::PFN_VoidFunction load_sym(void *lib, const char *name) {
#ifdef _WIN32
        return (vk::PFN_VoidFunction)GetProcAddress((HMODULE)lib, name);
#else
        return (vk::PFN_VoidFunction)dlsym(lib, name);
#endif
    }
    static void *load_lib() {
#ifdef _WIN32
        return (void *)LoadLibraryA("vulkan-1.dll");
#else
        void *l = dlopen("libvulkan.so.1", RTLD_NOW); if (!l) l = dlopen("libvulkan.so", RTLD_NOW); return l;
#endif
    }

    void bring_up() {
        attempted = 1;
        void *lib = load_lib();
        if (!lib) { snprintf(why, sizeof why, "no Vulkan loader on this machine"); LOG_WARN("rhi", "vulkan: %s", why); return; }
        vk::PFN_GetInstanceProcAddr gipa = (vk::PFN_GetInstanceProcAddr)load_sym(lib, "vkGetInstanceProcAddr");
        if (!gipa) { snprintf(why, sizeof why, "loader has no vkGetInstanceProcAddr"); LOG_WARN("rhi", "vulkan: %s", why); return; }
        vk::PFN_CreateInstance pCreateInstance = (vk::PFN_CreateInstance)gipa(nullptr, "vkCreateInstance");
        vk::PFN_EnumerateInstanceVersion pEnumVersion = (vk::PFN_EnumerateInstanceVersion)gipa(nullptr, "vkEnumerateInstanceVersion");
        unsigned loader_version = vk::make_version(1, 0, 0);
        if (pEnumVersion) pEnumVersion(&loader_version);
        vk::ApplicationInfo app = {}; app.sType = vk::STRUCTURE_TYPE_APPLICATION_INFO;
        app.pApplicationName = "Mare Liberum"; app.applicationVersion = vk::make_version(0, 19, 0);
        app.pEngineName = "mare"; app.engineVersion = vk::make_version(0, 19, 0);
        app.apiVersion = vk::version_minor(loader_version) >= 1 ? vk::make_version(1, 1, 0) : vk::make_version(1, 0, 0);
        vk::InstanceCreateInfo ici = {}; ici.sType = vk::STRUCTURE_TYPE_INSTANCE_CREATE_INFO; ici.pApplicationInfo = &app;
        if (!pCreateInstance || pCreateInstance(&ici, nullptr, &instance) != vk::SUCCESS || !instance) {
            snprintf(why, sizeof why, "vkCreateInstance failed"); LOG_WARN("rhi", "vulkan: %s", why); instance = nullptr; return; }
        pDestroyInstance = (vk::PFN_DestroyInstance)gipa(instance, "vkDestroyInstance");
        vk::PFN_EnumeratePhysicalDevices pEnum = (vk::PFN_EnumeratePhysicalDevices)gipa(instance, "vkEnumeratePhysicalDevices");
        vk::PFN_GetPhysicalDeviceProperties pProps = (vk::PFN_GetPhysicalDeviceProperties)gipa(instance, "vkGetPhysicalDeviceProperties");
        vk::PFN_GetPhysicalDeviceQueueFamilyProperties pQueues = (vk::PFN_GetPhysicalDeviceQueueFamilyProperties)gipa(instance, "vkGetPhysicalDeviceQueueFamilyProperties");
        vk::PFN_GetPhysicalDeviceMemoryProperties pMem = (vk::PFN_GetPhysicalDeviceMemoryProperties)gipa(instance, "vkGetPhysicalDeviceMemoryProperties");
        vk::PFN_CreateDevice pCreateDevice = (vk::PFN_CreateDevice)gipa(instance, "vkCreateDevice");
        vk::PFN_GetDeviceQueue pGetQueue = (vk::PFN_GetDeviceQueue)gipa(instance, "vkGetDeviceQueue");
        pDestroyDevice = (vk::PFN_DestroyDevice)gipa(instance, "vkDestroyDevice");
        if (!pEnum || !pProps || !pQueues || !pMem || !pCreateDevice || !pGetQueue) { snprintf(why, sizeof why, "core entry points missing"); LOG_WARN("rhi", "vulkan: %s", why); return; }
        unsigned n = 0; pEnum(instance, &n, nullptr);
        if (!n) { snprintf(why, sizeof why, "no Vulkan-capable GPU"); LOG_WARN("rhi", "vulkan: %s", why); return; }
        if (n > 8) n = 8;
        vk::PhysicalDevice devs[8]; pEnum(instance, &n, devs);
        // prefer a discrete GPU; otherwise the first adapter the loader lists
        unsigned char props[vk::PROPS_BYTES]; int pick = 0;
        for (unsigned i = 0; i < n; ++i) {
            memset(props, 0, sizeof props); pProps(devs[i], props);
            int type; memcpy(&type, props + vk::PROPS_OFF_TYPE, 4);
            if (type == vk::PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) { pick = (int)i; break; }
        }
        phys = devs[pick];
        memset(props, 0, sizeof props); pProps(phys, props);
        unsigned api; memcpy(&api, props + vk::PROPS_OFF_API, 4);
        api_major = vk::version_major(api); api_minor = vk::version_minor(api);
        memcpy(adapter, props + vk::PROPS_OFF_NAME, vk::PROPS_NAME_LEN - 1); adapter[vk::PROPS_NAME_LEN - 1] = 0;
        vk::PhysicalDeviceMemoryProperties mem; memset(&mem, 0, sizeof mem); pMem(phys, &mem);
        unsigned long long local = 0;
        for (unsigned i = 0; i < mem.memoryHeapCount && i < 16; ++i) if (mem.memoryHeaps[i].flags & vk::MEMORY_HEAP_DEVICE_LOCAL_BIT) local += mem.memoryHeaps[i].size;
        vram_mb = (unsigned)(local / (1024 * 1024));
        unsigned nq = 0; pQueues(phys, &nq, nullptr); if (nq > 16) nq = 16;
        vk::QueueFamilyProperties fam[16]; memset(fam, 0, sizeof fam); pQueues(phys, &nq, fam);
        nfamilies = nq; graphics_family = 0xFFFFFFFFu;
        for (unsigned i = 0; i < nq; ++i) if ((fam[i].queueFlags & vk::QUEUE_GRAPHICS_BIT) && fam[i].queueCount) { graphics_family = i; break; }
        if (graphics_family == 0xFFFFFFFFu) { snprintf(why, sizeof why, "%s has no graphics queue", adapter); LOG_WARN("rhi", "vulkan: %s", why); return; }
        float prio = 1.f;
        vk::DeviceQueueCreateInfo qci = {}; qci.sType = vk::STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO; qci.queueFamilyIndex = graphics_family; qci.queueCount = 1; qci.pQueuePriorities = &prio;
        vk::DeviceCreateInfo dci = {}; dci.sType = vk::STRUCTURE_TYPE_DEVICE_CREATE_INFO; dci.queueCreateInfoCount = 1; dci.pQueueCreateInfos = &qci;
        if (pCreateDevice(phys, &dci, nullptr, &device) != vk::SUCCESS || !device) { snprintf(why, sizeof why, "vkCreateDevice failed on %s", adapter); LOG_WARN("rhi", "vulkan: %s", why); device = nullptr; return; }
        pGetQueue(device, graphics_family, 0, &queue);
        ready = 1;
        LOG_INFO("rhi", "vulkan bring-up: %s, API %u.%u.%u, driver loader %u.%u, %u MB device-local, graphics queue family %u of %u, logical device + queue created; drawing stays on GL (T8 pending: SPIR-V, swapchain, pipelines)",
                 adapter, api_major, api_minor, vk::version_patch(api), vk::version_major(loader_version), vk::version_minor(loader_version), vram_mb, graphics_family, nfamilies);
    }
};

Device &vulkan_device() { static VulkanDevice d; return d; }

} // namespace mare::engine::rhi
