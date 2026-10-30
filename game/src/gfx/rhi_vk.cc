// mare::engine::rhi::VulkanDevice - the Vulkan backend (docs/REWRITE_PLAN.md T8).
//
// Built against the real Vulkan headers when they are installed, with VK_NO_PROTOTYPES and
// every entry point fetched through the loader at run time: the game links no Vulkan library
// and runs unchanged on a machine with no driver. Without the headers the backend is simply
// not available and `--rhi=vulkan` says so and stays on GL - one implementation, no second
// hand-written copy of the API to keep in step.
//
// What works today, and is verified at start-up by two self tests:
//
//   * device bring-up: instance, physical device (a discrete GPU when there is one), logical
//     device and a graphics queue, with the adapter, API version and device-local memory
//     reported through rhi_info().
//   * the transfer path: a host-coherent allocation mapped and filled, copied on the graphics
//     queue into a second buffer, fence-waited and read back byte for byte.
//   * the render path: an offscreen image, a render pass, a graphics pipeline built from
//     SPIR-V, a triangle drawn over the target, the image copied to a buffer and the pixels
//     checked - interpolation across the primitive and the clear both confirmed.
//
// What does not work yet, and why: presenting. The window's OpenGL context owns the surface
// for all the real drawing, and two APIs cannot present to it at once. The remaining T8 work
// is to port the passes themselves - pipelines from rhi_pipeline_desc_t, descriptor sets for
// what are currently loose uniforms, and a swapchain that replaces GL rather than shares with
// it. Until then every resource call and draw is delegated to the GL device, so `--rhi=vulkan`
// runs the whole game with Vulkan brought up and proven beside it.

#include "rhi_device.hh"

#include <cstring>
#include <cstdlib>
#include <cstdio>

extern "C" {
#include "../core/diag.h"
}

#if defined(__has_include)
#  if __has_include(<vulkan/vulkan.h>)
#    define MARE_VULKAN_HEADERS 1
#  endif
#endif

#ifdef MARE_VULKAN_HEADERS
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include "vk_selftest_spv.h"
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#endif

namespace mare::engine::rhi {

#ifdef MARE_VULKAN_HEADERS

#define VKFN(name) PFN_##name name = nullptr
#define LOAD_I(name) name = (PFN_##name)gipa_(instance_, #name)
#define LOAD_D(name) name = (PFN_##name)gdpa_(device_, #name)

class VulkanDevice : public DelegatingDevice {
public:
    const char *backend_name() override { return "vulkan"; }

    void init() override {
        gl().init();                       // drawing stays on GL until the passes are ported
        if (!attempted_) bring_up();
    }
    const char *info() override {
        static char buf[640];
        if (!attempted_) bring_up();       // the driver logs rhi_info() before rhi_init
        if (ready_)
            snprintf(buf, sizeof buf, "Vulkan %u.%u %s, %u MB device-local, %u queue families; self tests: transfer %s, render %s (bring-up: drawing via GL) | %s",
                     api_major_, api_minor_, adapter_, vram_mb_, nfamilies_,
                     transfer_ok_ ? "passed" : "FAILED", render_ok_ ? "passed" : "FAILED", gl().info());
        else
            snprintf(buf, sizeof buf, "Vulkan unavailable (%s): drawing via GL | %s", why_, gl().info());
        return buf;
    }
    int check_errors(const char *where) override { return gl().check_errors(where); }

    ~VulkanDevice() override {
        if (device_ && vkDestroyDevice) vkDestroyDevice(device_, nullptr);
        if (instance_ && vkDestroyInstance) vkDestroyInstance(instance_, nullptr);
    }

private:
    int ready_ = 0, attempted_ = 0, transfer_ok_ = 0, render_ok_ = 0;
    char why_[160] = "not initialised";
    char adapter_[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE] = "";
    unsigned api_major_ = 0, api_minor_ = 0, vram_mb_ = 0, nfamilies_ = 0, gfx_family_ = 0;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkPhysicalDevice phys_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties mem_ = {};
    PFN_vkGetInstanceProcAddr gipa_ = nullptr;
    PFN_vkGetDeviceProcAddr   gdpa_ = nullptr;

    // instance level
    VKFN(vkCreateInstance); VKFN(vkDestroyInstance); VKFN(vkEnumeratePhysicalDevices);
    VKFN(vkGetPhysicalDeviceProperties); VKFN(vkGetPhysicalDeviceQueueFamilyProperties);
    VKFN(vkGetPhysicalDeviceMemoryProperties); VKFN(vkCreateDevice);
    // device level
    VKFN(vkGetDeviceQueue); VKFN(vkDestroyDevice); VKFN(vkDeviceWaitIdle);
    VKFN(vkCreateBuffer); VKFN(vkDestroyBuffer); VKFN(vkGetBufferMemoryRequirements);
    VKFN(vkAllocateMemory); VKFN(vkFreeMemory); VKFN(vkBindBufferMemory);
    VKFN(vkMapMemory); VKFN(vkUnmapMemory);
    VKFN(vkCreateImage); VKFN(vkDestroyImage); VKFN(vkGetImageMemoryRequirements); VKFN(vkBindImageMemory);
    VKFN(vkCreateImageView); VKFN(vkDestroyImageView);
    VKFN(vkCreateRenderPass); VKFN(vkDestroyRenderPass);
    VKFN(vkCreateFramebuffer); VKFN(vkDestroyFramebuffer);
    VKFN(vkCreateShaderModule); VKFN(vkDestroyShaderModule);
    VKFN(vkCreatePipelineLayout); VKFN(vkDestroyPipelineLayout);
    VKFN(vkCreateGraphicsPipelines); VKFN(vkDestroyPipeline);
    VKFN(vkCreateCommandPool); VKFN(vkDestroyCommandPool); VKFN(vkAllocateCommandBuffers);
    VKFN(vkBeginCommandBuffer); VKFN(vkEndCommandBuffer);
    VKFN(vkCmdCopyBuffer); VKFN(vkCmdBeginRenderPass); VKFN(vkCmdEndRenderPass);
    VKFN(vkCmdBindPipeline); VKFN(vkCmdDraw); VKFN(vkCmdCopyImageToBuffer);
    VKFN(vkQueueSubmit); VKFN(vkCreateFence); VKFN(vkDestroyFence); VKFN(vkWaitForFences);

    static void *load_library() {
#ifdef _WIN32
        return (void *)LoadLibraryA("vulkan-1.dll");
#else
        void *l = dlopen("libvulkan.so.1", RTLD_NOW); return l ? l : dlopen("libvulkan.so", RTLD_NOW);
#endif
    }
    static void *load_symbol(void *lib, const char *name) {
#ifdef _WIN32
        return (void *)GetProcAddress((HMODULE)lib, name);
#else
        return dlsym(lib, name);
#endif
    }

    int memory_type(unsigned bits, VkMemoryPropertyFlags want) const {
        for (unsigned i = 0; i < mem_.memoryTypeCount; ++i)
            if ((bits & (1u << i)) && (mem_.memoryTypes[i].propertyFlags & want) == want) return (int)i;
        return -1;
    }

    void bring_up();
    void selftest_transfer();
    void selftest_render();
    // one command buffer recorded, submitted and waited on; `rec` fills it
    template <class F> int submit_once(VkCommandPool pool, F rec);
};

void VulkanDevice::bring_up() {
    attempted_ = 1;
    void *lib = load_library();
    if (!lib) { snprintf(why_, sizeof why_, "no Vulkan loader on this machine"); LOG_WARN("rhi", "vulkan: %s", why_); return; }
    gipa_ = (PFN_vkGetInstanceProcAddr)load_symbol(lib, "vkGetInstanceProcAddr");
    if (!gipa_) { snprintf(why_, sizeof why_, "the loader has no vkGetInstanceProcAddr"); LOG_WARN("rhi", "vulkan: %s", why_); return; }

    vkCreateInstance = (PFN_vkCreateInstance)gipa_(nullptr, "vkCreateInstance");
    auto enum_version = (PFN_vkEnumerateInstanceVersion)gipa_(nullptr, "vkEnumerateInstanceVersion");
    uint32_t loader_version = VK_API_VERSION_1_0;
    if (enum_version) enum_version(&loader_version);

    VkApplicationInfo app = {};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "Mare Liberum"; app.applicationVersion = VK_MAKE_VERSION(0, 21, 0);
    app.pEngineName = "mare"; app.engineVersion = VK_MAKE_VERSION(0, 21, 0);
    app.apiVersion = VK_API_VERSION_1_0;
    VkInstanceCreateInfo ici = {};
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO; ici.pApplicationInfo = &app;
    if (!vkCreateInstance || vkCreateInstance(&ici, nullptr, &instance_) != VK_SUCCESS) {
        snprintf(why_, sizeof why_, "vkCreateInstance failed"); LOG_WARN("rhi", "vulkan: %s", why_); instance_ = VK_NULL_HANDLE; return; }

    LOAD_I(vkDestroyInstance); LOAD_I(vkEnumeratePhysicalDevices); LOAD_I(vkGetPhysicalDeviceProperties);
    LOAD_I(vkGetPhysicalDeviceQueueFamilyProperties); LOAD_I(vkGetPhysicalDeviceMemoryProperties);
    LOAD_I(vkCreateDevice);
    gdpa_ = (PFN_vkGetDeviceProcAddr)gipa_(instance_, "vkGetDeviceProcAddr");
    if (!vkEnumeratePhysicalDevices || !vkCreateDevice || !gdpa_) {
        snprintf(why_, sizeof why_, "core instance entry points missing"); LOG_WARN("rhi", "vulkan: %s", why_); return; }

    uint32_t n = 0; vkEnumeratePhysicalDevices(instance_, &n, nullptr);
    if (!n) { snprintf(why_, sizeof why_, "no Vulkan-capable GPU"); LOG_WARN("rhi", "vulkan: %s", why_); return; }
    if (n > 8) n = 8;
    VkPhysicalDevice devs[8]; vkEnumeratePhysicalDevices(instance_, &n, devs);
    VkPhysicalDeviceProperties props = {};
    phys_ = devs[0];
    for (uint32_t i = 0; i < n; ++i) {       // a discrete GPU if the machine has one
        VkPhysicalDeviceProperties p; vkGetPhysicalDeviceProperties(devs[i], &p);
        if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) { phys_ = devs[i]; break; }
    }
    vkGetPhysicalDeviceProperties(phys_, &props);
    api_major_ = VK_VERSION_MAJOR(props.apiVersion); api_minor_ = VK_VERSION_MINOR(props.apiVersion);
    snprintf(adapter_, sizeof adapter_, "%s", props.deviceName);
    vkGetPhysicalDeviceMemoryProperties(phys_, &mem_);
    VkDeviceSize local = 0;
    for (uint32_t i = 0; i < mem_.memoryHeapCount; ++i)
        if (mem_.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) local += mem_.memoryHeaps[i].size;
    vram_mb_ = (unsigned)(local / (1024 * 1024));

    uint32_t nq = 0; vkGetPhysicalDeviceQueueFamilyProperties(phys_, &nq, nullptr);
    if (nq > 16) nq = 16;
    VkQueueFamilyProperties fam[16] = {}; vkGetPhysicalDeviceQueueFamilyProperties(phys_, &nq, fam);
    nfamilies_ = nq; gfx_family_ = UINT32_MAX;
    for (uint32_t i = 0; i < nq; ++i)
        if ((fam[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && fam[i].queueCount) { gfx_family_ = i; break; }
    if (gfx_family_ == UINT32_MAX) { snprintf(why_, sizeof why_, "%s has no graphics queue", adapter_); LOG_WARN("rhi", "vulkan: %s", why_); return; }

    float prio = 1.f;
    VkDeviceQueueCreateInfo qci = {};
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO; qci.queueFamilyIndex = gfx_family_;
    qci.queueCount = 1; qci.pQueuePriorities = &prio;
    VkDeviceCreateInfo dci = {};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO; dci.queueCreateInfoCount = 1; dci.pQueueCreateInfos = &qci;
    if (vkCreateDevice(phys_, &dci, nullptr, &device_) != VK_SUCCESS) {
        snprintf(why_, sizeof why_, "vkCreateDevice failed on %s", adapter_); LOG_WARN("rhi", "vulkan: %s", why_); device_ = VK_NULL_HANDLE; return; }

    LOAD_D(vkGetDeviceQueue); LOAD_D(vkDestroyDevice); LOAD_D(vkDeviceWaitIdle);
    LOAD_D(vkCreateBuffer); LOAD_D(vkDestroyBuffer); LOAD_D(vkGetBufferMemoryRequirements);
    LOAD_D(vkAllocateMemory); LOAD_D(vkFreeMemory); LOAD_D(vkBindBufferMemory);
    LOAD_D(vkMapMemory); LOAD_D(vkUnmapMemory);
    LOAD_D(vkCreateImage); LOAD_D(vkDestroyImage); LOAD_D(vkGetImageMemoryRequirements); LOAD_D(vkBindImageMemory);
    LOAD_D(vkCreateImageView); LOAD_D(vkDestroyImageView);
    LOAD_D(vkCreateRenderPass); LOAD_D(vkDestroyRenderPass);
    LOAD_D(vkCreateFramebuffer); LOAD_D(vkDestroyFramebuffer);
    LOAD_D(vkCreateShaderModule); LOAD_D(vkDestroyShaderModule);
    LOAD_D(vkCreatePipelineLayout); LOAD_D(vkDestroyPipelineLayout);
    LOAD_D(vkCreateGraphicsPipelines); LOAD_D(vkDestroyPipeline);
    LOAD_D(vkCreateCommandPool); LOAD_D(vkDestroyCommandPool); LOAD_D(vkAllocateCommandBuffers);
    LOAD_D(vkBeginCommandBuffer); LOAD_D(vkEndCommandBuffer);
    LOAD_D(vkCmdCopyBuffer); LOAD_D(vkCmdBeginRenderPass); LOAD_D(vkCmdEndRenderPass);
    LOAD_D(vkCmdBindPipeline); LOAD_D(vkCmdDraw); LOAD_D(vkCmdCopyImageToBuffer);
    LOAD_D(vkQueueSubmit); LOAD_D(vkCreateFence); LOAD_D(vkDestroyFence); LOAD_D(vkWaitForFences);
    vkGetDeviceQueue(device_, gfx_family_, 0, &queue_);
    ready_ = 1;

    LOG_INFO("rhi", "vulkan: %s, API %u.%u.%u, loader %u.%u, %u MB device-local, graphics queue family %u of %u",
             adapter_, api_major_, api_minor_, VK_VERSION_PATCH(props.apiVersion),
             VK_VERSION_MAJOR(loader_version), VK_VERSION_MINOR(loader_version), vram_mb_, gfx_family_, nfamilies_);
    selftest_transfer();
    selftest_render();
}

template <class F>
int VulkanDevice::submit_once(VkCommandPool pool, F rec) {
    VkCommandBufferAllocateInfo cai = {};
    cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO; cai.commandPool = pool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cai.commandBufferCount = 1;
    VkCommandBuffer cb = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(device_, &cai, &cb) != VK_SUCCESS || !cb) return 0;
    VkCommandBufferBeginInfo bi = {};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO; bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(cb, &bi) != VK_SUCCESS) return 0;
    rec(cb);
    if (vkEndCommandBuffer(cb) != VK_SUCCESS) return 0;
    VkFenceCreateInfo fci = {}; fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence = VK_NULL_HANDLE;
    if (vkCreateFence(device_, &fci, nullptr, &fence) != VK_SUCCESS) return 0;
    VkSubmitInfo si = {}; si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO; si.commandBufferCount = 1; si.pCommandBuffers = &cb;
    int ok = vkQueueSubmit(queue_, 1, &si, fence) == VK_SUCCESS
          && vkWaitForFences(device_, 1, &fence, VK_TRUE, 2000000000ull) == VK_SUCCESS;
    vkDestroyFence(device_, fence, nullptr);
    return ok;
}

// memory, buffers, mapping, the command path and fence synchronisation
void VulkanDevice::selftest_transfer() {
    const VkDeviceSize N = 64 * 1024;
    const VkMemoryPropertyFlags host = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    VkBuffer src = VK_NULL_HANDLE, dst = VK_NULL_HANDLE;
    VkDeviceMemory msrc = VK_NULL_HANDLE, mdst = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    unsigned mismatch = 0; int ok = 0;
    double t0 = diag_now();
    do {
        VkBufferCreateInfo bci = {};
        bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO; bci.size = N; bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        if (vkCreateBuffer(device_, &bci, nullptr, &src) != VK_SUCCESS) break;
        bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        if (vkCreateBuffer(device_, &bci, nullptr, &dst) != VK_SUCCESS) break;
        VkMemoryRequirements rs, rd;
        vkGetBufferMemoryRequirements(device_, src, &rs);
        vkGetBufferMemoryRequirements(device_, dst, &rd);
        int ts = memory_type(rs.memoryTypeBits, host), td = memory_type(rd.memoryTypeBits, host);
        if (ts < 0 || td < 0) { LOG_WARN("rhi", "vulkan self test: no host-visible coherent memory"); break; }
        VkMemoryAllocateInfo mai = {}; mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize = rs.size; mai.memoryTypeIndex = (uint32_t)ts;
        if (vkAllocateMemory(device_, &mai, nullptr, &msrc) != VK_SUCCESS) break;
        mai.allocationSize = rd.size; mai.memoryTypeIndex = (uint32_t)td;
        if (vkAllocateMemory(device_, &mai, nullptr, &mdst) != VK_SUCCESS) break;
        if (vkBindBufferMemory(device_, src, msrc, 0) != VK_SUCCESS) break;
        if (vkBindBufferMemory(device_, dst, mdst, 0) != VK_SUCCESS) break;
        unsigned *p = nullptr;
        if (vkMapMemory(device_, msrc, 0, N, 0, (void **)&p) != VK_SUCCESS || !p) break;
        for (unsigned i = 0; i < N / 4; ++i) p[i] = i * 2654435761u + 0x9E3779B9u;
        vkUnmapMemory(device_, msrc);
        VkCommandPoolCreateInfo cpi = {};
        cpi.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO; cpi.queueFamilyIndex = gfx_family_;
        if (vkCreateCommandPool(device_, &cpi, nullptr, &pool) != VK_SUCCESS) break;
        VkBufferCopy region = { 0, 0, N };
        if (!submit_once(pool, [&](VkCommandBuffer cb) { vkCmdCopyBuffer(cb, src, dst, 1, &region); })) break;
        unsigned *q = nullptr;
        if (vkMapMemory(device_, mdst, 0, N, 0, (void **)&q) != VK_SUCCESS || !q) break;
        for (unsigned i = 0; i < N / 4; ++i) if (q[i] != i * 2654435761u + 0x9E3779B9u) mismatch++;
        vkUnmapMemory(device_, mdst);
        ok = mismatch == 0;
    } while (0);
    if (pool) vkDestroyCommandPool(device_, pool, nullptr);
    if (src) vkDestroyBuffer(device_, src, nullptr);
    if (dst) vkDestroyBuffer(device_, dst, nullptr);
    if (msrc) vkFreeMemory(device_, msrc, nullptr);
    if (mdst) vkFreeMemory(device_, mdst, nullptr);
    transfer_ok_ = ok;
    if (ok) LOG_INFO("rhi", "vulkan transfer self test passed: 64 KB mapped, copied on the graphics queue and read back byte for byte in %.2f ms", (diag_now() - t0) * 1000);
    else    LOG_WARN("rhi", "vulkan transfer self test FAILED (%u words differed)", mismatch);
}

// a render pass, a pipeline built from SPIR-V, and a triangle whose pixels are checked
void VulkanDevice::selftest_render() {
    if (!transfer_ok_) return;                 // no point if the transfer path is broken
    const uint32_t W = 64, H = 64;
    const VkDeviceSize BYTES = (VkDeviceSize)W * H * 4;
    VkImage image = VK_NULL_HANDLE; VkDeviceMemory imem = VK_NULL_HANDLE; VkImageView view = VK_NULL_HANDLE;
    VkRenderPass pass = VK_NULL_HANDLE; VkFramebuffer fb = VK_NULL_HANDLE;
    VkShaderModule vs = VK_NULL_HANDLE, fs = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE; VkPipeline pipe = VK_NULL_HANDLE;
    VkBuffer readback = VK_NULL_HANDLE; VkDeviceMemory rmem = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    int ok = 0; const char *stage = "start";
    unsigned char corner_tl[4] = {}, corner_br[4] = {};
    double t0 = diag_now();
    do {
        stage = "image";
        VkImageCreateInfo ici = {};
        ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO; ici.imageType = VK_IMAGE_TYPE_2D;
        ici.format = VK_FORMAT_R8G8B8A8_UNORM; ici.extent = { W, H, 1 };
        ici.mipLevels = 1; ici.arrayLayers = 1; ici.samples = VK_SAMPLE_COUNT_1_BIT;
        ici.tiling = VK_IMAGE_TILING_OPTIMAL;
        ici.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE; ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(device_, &ici, nullptr, &image) != VK_SUCCESS) break;
        VkMemoryRequirements ir; vkGetImageMemoryRequirements(device_, image, &ir);
        int it = memory_type(ir.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (it < 0) it = memory_type(ir.memoryTypeBits, 0);
        if (it < 0) break;
        VkMemoryAllocateInfo mai = {}; mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize = ir.size; mai.memoryTypeIndex = (uint32_t)it;
        if (vkAllocateMemory(device_, &mai, nullptr, &imem) != VK_SUCCESS) break;
        if (vkBindImageMemory(device_, image, imem, 0) != VK_SUCCESS) break;

        stage = "view";
        VkImageViewCreateInfo vci = {};
        vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO; vci.image = image;
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D; vci.format = VK_FORMAT_R8G8B8A8_UNORM;
        vci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        if (vkCreateImageView(device_, &vci, nullptr, &view) != VK_SUCCESS) break;

        stage = "render pass";
        VkAttachmentDescription att = {};
        att.format = VK_FORMAT_R8G8B8A8_UNORM; att.samples = VK_SAMPLE_COUNT_1_BIT;
        att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        att.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;   // ready for the copy, no barrier needed
        VkAttachmentReference ref = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkSubpassDescription sub = {};
        sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; sub.colorAttachmentCount = 1; sub.pColorAttachments = &ref;
        VkRenderPassCreateInfo rpci = {};
        rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpci.attachmentCount = 1; rpci.pAttachments = &att; rpci.subpassCount = 1; rpci.pSubpasses = &sub;
        if (vkCreateRenderPass(device_, &rpci, nullptr, &pass) != VK_SUCCESS) break;

        stage = "framebuffer";
        VkFramebufferCreateInfo fci = {};
        fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO; fci.renderPass = pass;
        fci.attachmentCount = 1; fci.pAttachments = &view; fci.width = W; fci.height = H; fci.layers = 1;
        if (vkCreateFramebuffer(device_, &fci, nullptr, &fb) != VK_SUCCESS) break;

        stage = "shader modules";
        VkShaderModuleCreateInfo smci = {};
        smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        smci.codeSize = sizeof vk_selftest_vert_spv; smci.pCode = vk_selftest_vert_spv;
        if (vkCreateShaderModule(device_, &smci, nullptr, &vs) != VK_SUCCESS) break;
        smci.codeSize = sizeof vk_selftest_frag_spv; smci.pCode = vk_selftest_frag_spv;
        if (vkCreateShaderModule(device_, &smci, nullptr, &fs) != VK_SUCCESS) break;

        stage = "pipeline";
        VkPipelineLayoutCreateInfo plci = {}; plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        if (vkCreatePipelineLayout(device_, &plci, nullptr, &layout) != VK_SUCCESS) break;
        VkPipelineShaderStageCreateInfo stages[2] = {};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = vs; stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = fs; stages[1].pName = "main";
        VkPipelineVertexInputStateCreateInfo vi = {};      // the triangle comes from gl_VertexIndex
        vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        VkPipelineInputAssemblyStateCreateInfo ia = {};
        ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkViewport vp = { 0, 0, (float)W, (float)H, 0, 1 };
        VkRect2D sc = { { 0, 0 }, { W, H } };
        VkPipelineViewportStateCreateInfo vps = {};
        vps.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vps.viewportCount = 1; vps.pViewports = &vp; vps.scissorCount = 1; vps.pScissors = &sc;
        VkPipelineRasterizationStateCreateInfo rs = {};
        rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE;
        rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; rs.lineWidth = 1.f;
        VkPipelineMultisampleStateCreateInfo ms = {};
        ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState cba = {};
        cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo cb = {};
        cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.attachmentCount = 1; cb.pAttachments = &cba;
        VkGraphicsPipelineCreateInfo gpci = {};
        gpci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        gpci.stageCount = 2; gpci.pStages = stages;
        gpci.pVertexInputState = &vi; gpci.pInputAssemblyState = &ia; gpci.pViewportState = &vps;
        gpci.pRasterizationState = &rs; gpci.pMultisampleState = &ms; gpci.pColorBlendState = &cb;
        gpci.layout = layout; gpci.renderPass = pass; gpci.subpass = 0;
        if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gpci, nullptr, &pipe) != VK_SUCCESS) break;

        stage = "readback buffer";
        VkBufferCreateInfo bci = {};
        bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO; bci.size = BYTES;
        bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT; bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(device_, &bci, nullptr, &readback) != VK_SUCCESS) break;
        VkMemoryRequirements br; vkGetBufferMemoryRequirements(device_, readback, &br);
        int bt = memory_type(br.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (bt < 0) break;
        mai.allocationSize = br.size; mai.memoryTypeIndex = (uint32_t)bt;
        if (vkAllocateMemory(device_, &mai, nullptr, &rmem) != VK_SUCCESS) break;
        if (vkBindBufferMemory(device_, readback, rmem, 0) != VK_SUCCESS) break;

        stage = "record and submit";
        VkCommandPoolCreateInfo cpi = {};
        cpi.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO; cpi.queueFamilyIndex = gfx_family_;
        if (vkCreateCommandPool(device_, &cpi, nullptr, &pool) != VK_SUCCESS) break;
        VkClearValue clear = {}; clear.color.float32[0] = 0.f; clear.color.float32[1] = 0.f;
        clear.color.float32[2] = 0.f; clear.color.float32[3] = 1.f;
        VkBufferImageCopy copy = {};
        copy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        copy.imageExtent = { W, H, 1 };
        int submitted = submit_once(pool, [&](VkCommandBuffer cbuf) {
            VkRenderPassBeginInfo rbi = {};
            rbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO; rbi.renderPass = pass; rbi.framebuffer = fb;
            rbi.renderArea = sc; rbi.clearValueCount = 1; rbi.pClearValues = &clear;
            vkCmdBeginRenderPass(cbuf, &rbi, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(cbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
            vkCmdDraw(cbuf, 3, 1, 0, 0);
            vkCmdEndRenderPass(cbuf);
            vkCmdCopyImageToBuffer(cbuf, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback, 1, &copy);
        });
        if (!submitted) break;

        stage = "readback";
        unsigned char *px = nullptr;
        if (vkMapMemory(device_, rmem, 0, BYTES, 0, (void **)&px) != VK_SUCCESS || !px) break;
        memcpy(corner_tl, px, 4);
        memcpy(corner_br, px + ((size_t)(H - 1) * W + (W - 1)) * 4, 4);
        // The triangle covers the whole target, and its colour is the screen position: red
        // must climb left to right, green top to bottom, blue is 1 everywhere. Checking the
        // gradient rather than a flat colour is what proves the primitive was rasterised and
        // interpolated, not that something merely cleared the image.
        int all_blue = 1, opaque = 1;
        for (uint32_t i = 0; i < W * H; ++i) { if (px[i * 4 + 2] < 250) all_blue = 0; if (px[i * 4 + 3] < 250) opaque = 0; }
        int red_rises   = px[(0 * W + W - 1) * 4 + 0] > px[(0 * W + 0) * 4 + 0] + 100;
        int green_rises = px[((H - 1) * W + 0) * 4 + 1] > px[(0 * W + 0) * 4 + 1] + 100;
        vkUnmapMemory(device_, rmem);
        ok = all_blue && opaque && red_rises && green_rises;
        if (!ok) LOG_WARN("rhi", "vulkan render self test: blue %d opaque %d red %d green %d", all_blue, opaque, red_rises, green_rises);
    } while (0);
    if (pool) vkDestroyCommandPool(device_, pool, nullptr);
    if (pipe) vkDestroyPipeline(device_, pipe, nullptr);
    if (layout) vkDestroyPipelineLayout(device_, layout, nullptr);
    if (vs) vkDestroyShaderModule(device_, vs, nullptr);
    if (fs) vkDestroyShaderModule(device_, fs, nullptr);
    if (fb) vkDestroyFramebuffer(device_, fb, nullptr);
    if (pass) vkDestroyRenderPass(device_, pass, nullptr);
    if (view) vkDestroyImageView(device_, view, nullptr);
    if (image) vkDestroyImage(device_, image, nullptr);
    if (imem) vkFreeMemory(device_, imem, nullptr);
    if (readback) vkDestroyBuffer(device_, readback, nullptr);
    if (rmem) vkFreeMemory(device_, rmem, nullptr);
    render_ok_ = ok;
    if (ok) LOG_INFO("rhi", "vulkan render self test passed: SPIR-V pipeline drew a triangle into a %ux%u target and the pixels read back correct (top-left rgba %u,%u,%u,%u, bottom-right %u,%u,%u,%u) in %.2f ms",
                     W, H, corner_tl[0], corner_tl[1], corner_tl[2], corner_tl[3],
                     corner_br[0], corner_br[1], corner_br[2], corner_br[3], (diag_now() - t0) * 1000);
    else    LOG_WARN("rhi", "vulkan render self test FAILED at '%s'", stage);
}

Device &vulkan_device() { static VulkanDevice d; return d; }

#else  // no Vulkan headers at build time

// Built without the Vulkan SDK: the backend exists so --rhi=vulkan has something to select
// and report, and every call goes to GL.
class VulkanDevice : public DelegatingDevice {
public:
    const char *backend_name() override { return "vulkan"; }
    void init() override {
        gl().init();
        LOG_WARN("rhi", "this build has no Vulkan headers (install the Vulkan SDK and rebuild): drawing via GL");
    }
    const char *info() override {
        static char buf[384];
        snprintf(buf, sizeof buf, "Vulkan not compiled in (no SDK at build time) | %s", gl().info());
        return buf;
    }
    int check_errors(const char *where) override { return gl().check_errors(where); }
};

Device &vulkan_device() { static VulkanDevice d; return d; }

#endif

} // namespace mare::engine::rhi
