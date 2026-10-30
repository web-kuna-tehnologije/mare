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
       STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO = 2, STRUCTURE_TYPE_DEVICE_CREATE_INFO = 3,
       STRUCTURE_TYPE_SUBMIT_INFO = 4, STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO = 5,
       STRUCTURE_TYPE_FENCE_CREATE_INFO = 8, STRUCTURE_TYPE_BUFFER_CREATE_INFO = 12,
       STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO = 39, STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO = 40,
       STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO = 42 };
enum { SUCCESS = 0, QUEUE_GRAPHICS_BIT = 1, MEMORY_HEAP_DEVICE_LOCAL_BIT = 1 };
enum { BUFFER_USAGE_TRANSFER_SRC_BIT = 0x1, BUFFER_USAGE_TRANSFER_DST_BIT = 0x2 };
enum { MEMORY_PROPERTY_DEVICE_LOCAL_BIT = 0x1, MEMORY_PROPERTY_HOST_VISIBLE_BIT = 0x2,
       MEMORY_PROPERTY_HOST_COHERENT_BIT = 0x4 };
enum { SHARING_MODE_EXCLUSIVE = 0, COMMAND_BUFFER_LEVEL_PRIMARY = 0,
       COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT = 0x1, TRUE_ = 1 };
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
typedef struct Buffer_T *Buffer; typedef struct DeviceMemory_T *DeviceMemory;
typedef struct CommandPool_T *CommandPool; typedef struct CommandBuffer_T *CommandBuffer;
typedef struct Fence_T *Fence;
struct BufferCreateInfo { int sType; const void *pNext; Flags flags; DeviceSize size; Flags usage; int sharingMode; unsigned queueFamilyIndexCount; const unsigned *pQueueFamilyIndices; };
struct MemoryRequirements { DeviceSize size; DeviceSize alignment; unsigned memoryTypeBits; };
struct MemoryAllocateInfo { int sType; const void *pNext; DeviceSize allocationSize; unsigned memoryTypeIndex; };
struct CommandPoolCreateInfo { int sType; const void *pNext; Flags flags; unsigned queueFamilyIndex; };
struct CommandBufferAllocateInfo { int sType; const void *pNext; CommandPool commandPool; int level; unsigned commandBufferCount; };
struct CommandBufferBeginInfo { int sType; const void *pNext; Flags flags; const void *pInheritanceInfo; };
struct SubmitInfo { int sType; const void *pNext; unsigned waitSemaphoreCount; const void *pWaitSemaphores; const Flags *pWaitDstStageMask;
                    unsigned commandBufferCount; const CommandBuffer *pCommandBuffers; unsigned signalSemaphoreCount; const void *pSignalSemaphores; };
struct FenceCreateInfo { int sType; const void *pNext; Flags flags; };
struct BufferCopy { DeviceSize srcOffset, dstOffset, size; };
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
typedef PFN_VoidFunction (VKCALL *PFN_GetDeviceProcAddr)(Device, const char *);
typedef int  (VKCALL *PFN_CreateBuffer)(Device, const BufferCreateInfo *, const void *, Buffer *);
typedef void (VKCALL *PFN_DestroyBuffer)(Device, Buffer, const void *);
typedef void (VKCALL *PFN_GetBufferMemoryRequirements)(Device, Buffer, MemoryRequirements *);
typedef int  (VKCALL *PFN_AllocateMemory)(Device, const MemoryAllocateInfo *, const void *, DeviceMemory *);
typedef void (VKCALL *PFN_FreeMemory)(Device, DeviceMemory, const void *);
typedef int  (VKCALL *PFN_BindBufferMemory)(Device, Buffer, DeviceMemory, DeviceSize);
typedef int  (VKCALL *PFN_MapMemory)(Device, DeviceMemory, DeviceSize, DeviceSize, Flags, void **);
typedef void (VKCALL *PFN_UnmapMemory)(Device, DeviceMemory);
typedef int  (VKCALL *PFN_CreateCommandPool)(Device, const CommandPoolCreateInfo *, const void *, CommandPool *);
typedef void (VKCALL *PFN_DestroyCommandPool)(Device, CommandPool, const void *);
typedef int  (VKCALL *PFN_AllocateCommandBuffers)(Device, const CommandBufferAllocateInfo *, CommandBuffer *);
typedef int  (VKCALL *PFN_BeginCommandBuffer)(CommandBuffer, const CommandBufferBeginInfo *);
typedef int  (VKCALL *PFN_EndCommandBuffer)(CommandBuffer);
typedef void (VKCALL *PFN_CmdCopyBuffer)(CommandBuffer, Buffer, Buffer, unsigned, const BufferCopy *);
typedef int  (VKCALL *PFN_QueueSubmit)(Queue, unsigned, const SubmitInfo *, Fence);
typedef int  (VKCALL *PFN_CreateFence)(Device, const FenceCreateInfo *, const void *, Fence *);
typedef void (VKCALL *PFN_DestroyFence)(Device, Fence, const void *);
typedef int  (VKCALL *PFN_WaitForFences)(Device, unsigned, const Fence *, unsigned, unsigned long long);
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
        if (ready) snprintf(buf, sizeof buf, "Vulkan %u.%u %s, %u MB device-local, %u queue families, transfer self test %s (bring-up: drawing via GL) | %s",
                            api_major, api_minor, adapter, vram_mb, nfamilies, selftest_ok ? "passed" : "not run", gl().info());
        else snprintf(buf, sizeof buf, "Vulkan unavailable (%s): drawing via GL | %s", why, gl().info());
        return buf;
    }
    int check_errors(const char *where) override { return gl().check_errors(where); }

    // The memory, command and queue layers, exercised end to end and *checked* (T8 step two).
    //
    // No SPIR-V compiler exists on this machine and no Vulkan SDK is installed, so pipelines
    // and render passes cannot be built yet. What can be built, and proved, is everything
    // underneath them: pick a memory type, create buffers, map one, record a transfer into a
    // command buffer, submit it to the graphics queue, wait on a fence and read the bytes
    // back. If the pattern survives that round trip, the device's allocator, command path and
    // synchronisation are all working - which is the part of the backend the renderer will
    // sit on. It is a few milliseconds at start-up and it reports what it found.
    void selftest() {
        if (!ready || !pGetDeviceProcAddr) return;
        const unsigned N = 64 * 1024;                   // 64 KB is plenty to prove the path
        auto dp = [&](const char *n) { return pGetDeviceProcAddr(device, n); };
        auto pCreateBuffer = (vk::PFN_CreateBuffer)dp("vkCreateBuffer");
        auto pDestroyBuffer = (vk::PFN_DestroyBuffer)dp("vkDestroyBuffer");
        auto pBufReq = (vk::PFN_GetBufferMemoryRequirements)dp("vkGetBufferMemoryRequirements");
        auto pAlloc = (vk::PFN_AllocateMemory)dp("vkAllocateMemory");
        auto pFree = (vk::PFN_FreeMemory)dp("vkFreeMemory");
        auto pBind = (vk::PFN_BindBufferMemory)dp("vkBindBufferMemory");
        auto pMap = (vk::PFN_MapMemory)dp("vkMapMemory");
        auto pUnmap = (vk::PFN_UnmapMemory)dp("vkUnmapMemory");
        auto pCreatePool = (vk::PFN_CreateCommandPool)dp("vkCreateCommandPool");
        auto pDestroyPool = (vk::PFN_DestroyCommandPool)dp("vkDestroyCommandPool");
        auto pAllocCb = (vk::PFN_AllocateCommandBuffers)dp("vkAllocateCommandBuffers");
        auto pBeginCb = (vk::PFN_BeginCommandBuffer)dp("vkBeginCommandBuffer");
        auto pEndCb = (vk::PFN_EndCommandBuffer)dp("vkEndCommandBuffer");
        auto pCopy = (vk::PFN_CmdCopyBuffer)dp("vkCmdCopyBuffer");
        auto pSubmit = (vk::PFN_QueueSubmit)dp("vkQueueSubmit");
        auto pCreateFence = (vk::PFN_CreateFence)dp("vkCreateFence");
        auto pDestroyFence = (vk::PFN_DestroyFence)dp("vkDestroyFence");
        auto pWait = (vk::PFN_WaitForFences)dp("vkWaitForFences");
        if (!pCreateBuffer || !pBufReq || !pAlloc || !pBind || !pMap || !pCreatePool ||
            !pAllocCb || !pBeginCb || !pEndCb || !pCopy || !pSubmit || !pCreateFence || !pWait) {
            LOG_WARN("rhi", "vulkan self test: device entry points missing"); return; }
        vk::PhysicalDeviceMemoryProperties mem; memset(&mem, 0, sizeof mem);
        if (pGetMemProps) pGetMemProps(phys, &mem);
        auto pick = [&](unsigned bits, unsigned want) -> int {
            for (unsigned i = 0; i < mem.memoryTypeCount && i < 32; ++i)
                if ((bits & (1u << i)) && (mem.memoryTypes[i].propertyFlags & want) == want) return (int)i;
            return -1;
        };
        vk::Buffer src = nullptr, dst = nullptr; vk::DeviceMemory msrc = nullptr, mdst = nullptr;
        vk::CommandPool pool = nullptr; vk::CommandBuffer cb = nullptr; vk::Fence fence = nullptr;
        int ok = 0; unsigned mismatch = 0; double ms = 0;
        const unsigned host = vk::MEMORY_PROPERTY_HOST_VISIBLE_BIT | vk::MEMORY_PROPERTY_HOST_COHERENT_BIT;
        do {
            vk::BufferCreateInfo bci = {}; bci.sType = vk::STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bci.size = N; bci.sharingMode = vk::SHARING_MODE_EXCLUSIVE;
            bci.usage = vk::BUFFER_USAGE_TRANSFER_SRC_BIT;
            if (pCreateBuffer(device, &bci, nullptr, &src) != vk::SUCCESS) break;
            bci.usage = vk::BUFFER_USAGE_TRANSFER_DST_BIT;
            if (pCreateBuffer(device, &bci, nullptr, &dst) != vk::SUCCESS) break;
            vk::MemoryRequirements rs = {}, rd = {};
            pBufReq(device, src, &rs); pBufReq(device, dst, &rd);
            int ts = pick(rs.memoryTypeBits, host), td = pick(rd.memoryTypeBits, host);
            if (ts < 0 || td < 0) { LOG_WARN("rhi", "vulkan self test: no host-visible coherent memory type"); break; }
            vk::MemoryAllocateInfo mai = {}; mai.sType = vk::STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            mai.allocationSize = rs.size; mai.memoryTypeIndex = (unsigned)ts;
            if (pAlloc(device, &mai, nullptr, &msrc) != vk::SUCCESS) break;
            mai.allocationSize = rd.size; mai.memoryTypeIndex = (unsigned)td;
            if (pAlloc(device, &mai, nullptr, &mdst) != vk::SUCCESS) break;
            if (pBind(device, src, msrc, 0) != vk::SUCCESS || pBind(device, dst, mdst, 0) != vk::SUCCESS) break;
            unsigned *p = nullptr;
            if (pMap(device, msrc, 0, N, 0, (void **)&p) != vk::SUCCESS || !p) break;
            for (unsigned i = 0; i < N / 4; ++i) p[i] = i * 2654435761u + 0x9E3779B9u;
            if (pUnmap) pUnmap(device, msrc);
            vk::CommandPoolCreateInfo cpi = {}; cpi.sType = vk::STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            cpi.queueFamilyIndex = graphics_family;
            if (pCreatePool(device, &cpi, nullptr, &pool) != vk::SUCCESS) break;
            vk::CommandBufferAllocateInfo cai = {}; cai.sType = vk::STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            cai.commandPool = pool; cai.level = vk::COMMAND_BUFFER_LEVEL_PRIMARY; cai.commandBufferCount = 1;
            if (pAllocCb(device, &cai, &cb) != vk::SUCCESS || !cb) break;
            vk::CommandBufferBeginInfo cbi = {}; cbi.sType = vk::STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            cbi.flags = vk::COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            if (pBeginCb(cb, &cbi) != vk::SUCCESS) break;
            vk::BufferCopy region = { 0, 0, N };
            pCopy(cb, src, dst, 1, &region);
            if (pEndCb(cb) != vk::SUCCESS) break;
            vk::FenceCreateInfo fci = {}; fci.sType = vk::STRUCTURE_TYPE_FENCE_CREATE_INFO;
            if (pCreateFence(device, &fci, nullptr, &fence) != vk::SUCCESS) break;
            vk::SubmitInfo si = {}; si.sType = vk::STRUCTURE_TYPE_SUBMIT_INFO;
            si.commandBufferCount = 1; si.pCommandBuffers = &cb;
            double t0 = diag_now();
            if (pSubmit(queue, 1, &si, fence) != vk::SUCCESS) break;
            if (pWait(device, 1, &fence, vk::TRUE_, 1000000000ull) != vk::SUCCESS) { LOG_WARN("rhi", "vulkan self test: the fence did not signal"); break; }
            ms = (diag_now() - t0) * 1000;
            unsigned *q = nullptr;
            if (pMap(device, mdst, 0, N, 0, (void **)&q) != vk::SUCCESS || !q) break;
            for (unsigned i = 0; i < N / 4; ++i) if (q[i] != i * 2654435761u + 0x9E3779B9u) mismatch++;
            if (pUnmap) pUnmap(device, mdst);
            ok = mismatch == 0;
        } while (0);
        if (fence && pDestroyFence) pDestroyFence(device, fence, nullptr);
        if (pool && pDestroyPool) pDestroyPool(device, pool, nullptr);   // frees its command buffers
        if (src && pDestroyBuffer) pDestroyBuffer(device, src, nullptr);
        if (dst && pDestroyBuffer) pDestroyBuffer(device, dst, nullptr);
        if (msrc && pFree) pFree(device, msrc, nullptr);
        if (mdst && pFree) pFree(device, mdst, nullptr);
        selftest_ok = ok;
        if (ok) LOG_INFO("rhi", "vulkan self test passed: %u KB allocated, mapped, copied on the graphics queue and read back byte for byte (submit to fence %.2f ms). Memory, command buffers, queue submission and fence synchronisation all work; pipelines wait on SPIR-V.", N / 1024, ms);
        else    LOG_WARN("rhi", "vulkan self test failed (%u words differed): the device is up but its transfer path did not round trip", mismatch);
    }

    ~VulkanDevice() override {
        if (device && pDestroyDevice) pDestroyDevice(device, nullptr);
        if (instance && pDestroyInstance) pDestroyInstance(instance, nullptr);
    }

private:
    int ready = 0, attempted = 0, selftest_ok = 0; char why[128] = "not initialised";
    vk::PFN_GetDeviceProcAddr pGetDeviceProcAddr = nullptr;
    vk::PFN_GetPhysicalDeviceMemoryProperties pGetMemProps = nullptr;
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
        pGetMemProps = pMem;
        pGetDeviceProcAddr = (vk::PFN_GetDeviceProcAddr)gipa(instance, "vkGetDeviceProcAddr");
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
        selftest();
    }
};

Device &vulkan_device() { static VulkanDevice d; return d; }

} // namespace mare::engine::rhi
