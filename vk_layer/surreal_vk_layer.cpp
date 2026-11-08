// ============================================================================
// VK_LAYER_SURREAL_boost  --  AION Surreal Boost Vulkan implicit layer (v1.1)
// ============================================================================
// Sits between ANY Vulkan application (native games, DXVK / VKD3D-Proton via
// Steam Proton) and the GPU driver. It does NOT replace the driver; it boosts
// performance and image quality on the fly:
//
//   * Spatial Upscaler + CAS  : renders games at scaled internal resolution
//                               (SURREAL_SCALE=0.40..0.99, e.g. 0.67 = 720p->1080p)
//                               and upscales to native window/fullscreen size via
//                               an embedded SPIR-V Contrast-Adaptive Sharpening
//                               (CAS) GPU pass (SURREAL_SHARPEN=0.0..1.0).
//                               Can also apply native-res CAS at scale=1.0!
//   * Anisotropic filtering   : raises trilinear texture samplers to N x AF
//                               (SURREAL_AF=2..16)         -> sharper textures
//   * Texture LOD bias        : shifts mip selection        (SURREAL_LOD_BIAS)
//                               negative = sharper, positive = faster
//   * Precise frame pacing    : hybrid sleep/spin limiter   (SURREAL_FPS_CAP)
//                               -> stable frametimes, no tearing/stutter swings
//   * Telemetry               : avg FPS / 1% low / frame-time / GPU name to
//                               /tmp/surreal_layer.log     (SURREAL_LOG=1)
//
// Activation: the layer manifest only enables itself when SURREAL_BOOST=1 is
// set (done by /usr/local/bin/surreal-boost), so no other Vulkan program on the
// system (Xorg tools, Steam UI, browsers) is ever touched.
// ============================================================================

#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>

#include "upscale_shaders_spv.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <time.h>

#define SURREAL_EXPORT __attribute__((visibility("default")))

namespace {

// ----------------------------------------------------------------------------
// Configuration
// ----------------------------------------------------------------------------
struct Config {
    int    af              = 8;      // 0 = leave application untouched, 2..16 = raise to N x (default 8x)
    float  lodBias         = 0.0f;   // added to the application's mipLodBias
    double fpsCap          = 0.0;    // 0 = off
    bool   log             = false;
    float  scale           = 1.0f;   // render scale 0.40..1.00 (1.0 = native resolution)
    bool   scaleLinear     = true;   // upscale filter: linear (smooth) or nearest (crisp pixels)
    float  sharpen         = 0.55f;  // CAS strength 0.0..1.0 (auto-used when upscaling, or at 1.0x if explicit)
    bool   sharpenExplicit = false;  // true if user explicitly configured sharpen
};

Config gCfg;
std::once_flag gCfgOnce;

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

void applyKeyValue(Config& c, const std::string& k, const std::string& v) {
    if (v.empty()) return;
    if (k == "af" || k == "SURREAL_AF")
        c.af = std::max(0, std::min(16, atoi(v.c_str())));
    else if (k == "lod_bias" || k == "SURREAL_LOD_BIAS")
        c.lodBias = std::max(-2.0f, std::min(2.0f, (float)atof(v.c_str())));
    else if (k == "fps_cap" || k == "SURREAL_FPS_CAP")
        c.fpsCap = std::max(0.0, std::min(1000.0, atof(v.c_str())));
    else if (k == "log" || k == "SURREAL_LOG")
        c.log = (v == "1" || v == "true" || v == "yes");
    else if (k == "scale" || k == "SURREAL_SCALE")
        c.scale = std::max(0.40f, std::min(1.0f, (float)atof(v.c_str())));
    else if (k == "scale_filter" || k == "SURREAL_SCALE_FILTER")
        c.scaleLinear = (v != "nearest");
    else if (k == "sharpen" || k == "SURREAL_SHARPEN") {
        c.sharpen = std::max(0.0f, std::min(1.0f, (float)atof(v.c_str())));
        c.sharpenExplicit = true;
    }
}

void loadConfigFile(Config& c, const std::string& path) {
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        std::string s = trim(line);
        if (s.empty() || s[0] == '#') continue;
        size_t eq = s.find('=');
        if (eq == std::string::npos) continue;
        applyKeyValue(c, trim(s.substr(0, eq)), trim(s.substr(eq + 1)));
    }
    fclose(f);
}

void logLine(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void logLine(const char* fmt, ...) {
    if (!gCfg.log) return;
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    FILE* f = fopen("/tmp/surreal_layer.log", "a");
    if (f) {
        fprintf(f, "[surreal-layer] %s\n", buf);
        fclose(f);
    }
}

void loadConfig() {
    std::call_once(gCfgOnce, [] {
        Config c;
        loadConfigFile(c, "/etc/surreal-boost/layer.conf");
        if (const char* home = getenv("HOME"))
            loadConfigFile(c, std::string(home) + "/.config/surreal-boost/layer.conf");
        static const char* keys[] = {
            "SURREAL_AF", "SURREAL_LOD_BIAS", "SURREAL_FPS_CAP", "SURREAL_LOG",
            "SURREAL_SCALE", "SURREAL_SCALE_FILTER", "SURREAL_SHARPEN"
        };
        for (const char* k : keys)
            if (const char* v = getenv(k)) applyKeyValue(c, k, v);
        gCfg = c;
        logLine("config: af=%d lod_bias=%.2f fps_cap=%.1f scale=%.2f (%s) sharpen=%.2f%s",
                gCfg.af, gCfg.lodBias, gCfg.fpsCap, gCfg.scale,
                gCfg.scaleLinear ? "linear" : "nearest", gCfg.sharpen,
                gCfg.sharpenExplicit ? " (explicit)" : "");
    });
}

inline VkExtent2D computeScaledExtent(VkExtent2D realExt, float scale) {
    uint32_t w = std::max(16u, ((uint32_t)std::lround((double)realExt.width  * (double)scale)) & ~1u);
    uint32_t h = std::max(16u, ((uint32_t)std::lround((double)realExt.height * (double)scale)) & ~1u);
    w = std::min(w, realExt.width);
    h = std::min(h, realExt.height);
    return {w, h};
}

// ----------------------------------------------------------------------------
// Dispatch bookkeeping (keyed by the loader's dispatch pointer)
// ----------------------------------------------------------------------------
inline void* dispatchKey(const void* handle) { return *reinterpret_cast<void* const*>(handle); }

struct InstanceData {
    VkInstance                                          instance = VK_NULL_HANDLE;
    PFN_vkGetInstanceProcAddr                           gipa = nullptr;
    PFN_vkDestroyInstance                               destroyInstance = nullptr;
    PFN_vkEnumeratePhysicalDevices                      enumeratePhysicalDevices = nullptr;
    PFN_vkGetPhysicalDeviceProperties                   getPhysicalDeviceProperties = nullptr;
    PFN_vkGetPhysicalDeviceMemoryProperties             getPhysicalDeviceMemoryProperties = nullptr;
    PFN_vkGetPhysicalDeviceFormatProperties             getPhysicalDeviceFormatProperties = nullptr;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties        getPhysicalDeviceQueueFamilyProperties = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR       getSurfaceCaps = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceCapabilities2KHR      getSurfaceCaps2 = nullptr;
};

struct DeviceData {
    VkDevice                                device = VK_NULL_HANDLE;
    VkPhysicalDevice                        physDevice = VK_NULL_HANDLE;
    VkInstance                              instance = VK_NULL_HANDLE;
    PFN_vkGetDeviceProcAddr                 gdpa = nullptr;
    PFN_vkDestroyDevice                     destroyDevice = nullptr;
    PFN_vkDeviceWaitIdle                    deviceWaitIdle = nullptr;
    PFN_vkGetDeviceQueue                    getDeviceQueue = nullptr;
    PFN_vkGetDeviceQueue2                   getDeviceQueue2 = nullptr;
    PFN_vkCreateSampler                     createSampler = nullptr;
    PFN_vkDestroySampler                    destroySampler = nullptr;
    PFN_vkCreateSwapchainKHR                createSwapchain = nullptr;
    PFN_vkDestroySwapchainKHR               destroySwapchain = nullptr;
    PFN_vkGetSwapchainImagesKHR             getSwapchainImages = nullptr;
    PFN_vkQueuePresentKHR                   queuePresent = nullptr;
    PFN_vkQueueSubmit                       queueSubmit = nullptr;

    // Upscaler GPU resource dispatch functions
    PFN_vkCreateImage                       createImage = nullptr;
    PFN_vkDestroyImage                      destroyImage = nullptr;
    PFN_vkGetImageMemoryRequirements        getImageMemoryRequirements = nullptr;
    PFN_vkAllocateMemory                    allocateMemory = nullptr;
    PFN_vkFreeMemory                        freeMemory = nullptr;
    PFN_vkBindImageMemory                   bindImageMemory = nullptr;
    PFN_vkCreateImageView                   createImageView = nullptr;
    PFN_vkDestroyImageView                  destroyImageView = nullptr;
    PFN_vkCreateShaderModule                createShaderModule = nullptr;
    PFN_vkDestroyShaderModule               destroyShaderModule = nullptr;
    PFN_vkCreateDescriptorSetLayout         createDescriptorSetLayout = nullptr;
    PFN_vkDestroyDescriptorSetLayout        destroyDescriptorSetLayout = nullptr;
    PFN_vkCreateDescriptorPool              createDescriptorPool = nullptr;
    PFN_vkDestroyDescriptorPool             destroyDescriptorPool = nullptr;
    PFN_vkAllocateDescriptorSets            allocateDescriptorSets = nullptr;
    PFN_vkUpdateDescriptorSets              updateDescriptorSets = nullptr;
    PFN_vkCreatePipelineLayout              createPipelineLayout = nullptr;
    PFN_vkDestroyPipelineLayout             destroyPipelineLayout = nullptr;
    PFN_vkCreateRenderPass                  createRenderPass = nullptr;
    PFN_vkDestroyRenderPass                 destroyRenderPass = nullptr;
    PFN_vkCreateGraphicsPipelines           createGraphicsPipelines = nullptr;
    PFN_vkDestroyPipeline                   destroyPipeline = nullptr;
    PFN_vkCreateFramebuffer                 createFramebuffer = nullptr;
    PFN_vkDestroyFramebuffer                destroyFramebuffer = nullptr;
    PFN_vkCreateCommandPool                 createCommandPool = nullptr;
    PFN_vkDestroyCommandPool                destroyCommandPool = nullptr;
    PFN_vkAllocateCommandBuffers            allocateCommandBuffers = nullptr;
    PFN_vkBeginCommandBuffer                beginCommandBuffer = nullptr;
    PFN_vkEndCommandBuffer                  endCommandBuffer = nullptr;
    PFN_vkCmdPipelineBarrier                cmdPipelineBarrier = nullptr;
    PFN_vkCmdBeginRenderPass                cmdBeginRenderPass = nullptr;
    PFN_vkCmdEndRenderPass                  cmdEndRenderPass = nullptr;
    PFN_vkCmdBindPipeline                   cmdBindPipeline = nullptr;
    PFN_vkCmdBindDescriptorSets             cmdBindDescriptorSets = nullptr;
    PFN_vkCmdPushConstants                  cmdPushConstants = nullptr;
    PFN_vkCmdDraw                           cmdDraw = nullptr;
    PFN_vkCmdBlitImage                      cmdBlitImage = nullptr;
    PFN_vkCreateSemaphore                   createSemaphore = nullptr;
    PFN_vkDestroySemaphore                  destroySemaphore = nullptr;
    PFN_vkCreateFence                       createFence = nullptr;
    PFN_vkDestroyFence                      destroyFence = nullptr;
    PFN_vkWaitForFences                     waitForFences = nullptr;
    PFN_vkResetFences                       resetFences = nullptr;

    VkPhysicalDeviceMemoryProperties        memProps{};
    std::vector<VkQueueFamilyProperties>    queueProps;
    uint32_t                                defaultQueueFamily = 0;

    bool                                    samplerAnisotropy = false;
    float                                   maxAnisotropy = 1.0f;
    float                                   maxLodBias = 0.0f;
};

struct SyncSlot {
    VkSemaphore sem       = VK_NULL_HANDLE;
    VkFence     fence     = VK_NULL_HANDLE;
    bool        submitted = false;
};

struct SwapchainUpscaler {
    VkDevice                        device = VK_NULL_HANDLE;
    VkSwapchainKHR                  swapchain = VK_NULL_HANDLE;
    VkFormat                        format = VK_FORMAT_UNDEFINED;
    VkExtent2D                      renderExtent{0, 0};
    VkExtent2D                      realExtent{0, 0};
    float                           sharpen = 0.0f;
    bool                            useCasShader = false;

    std::vector<VkImage>            realImages;
    std::vector<VkImage>            virtImages;
    std::vector<VkDeviceMemory>     virtMemories;
    std::vector<VkImageView>        virtViews;
    std::vector<VkImageView>        realViews;
    std::vector<VkFramebuffer>      framebuffers;

    VkSampler                       sampler = VK_NULL_HANDLE;
    VkShaderModule                  vertModule = VK_NULL_HANDLE;
    VkShaderModule                  fragModule = VK_NULL_HANDLE;
    VkDescriptorSetLayout           dsLayout = VK_NULL_HANDLE;
    VkDescriptorPool                dsPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet>    dsSets;
    VkPipelineLayout                pipeLayout = VK_NULL_HANDLE;
    VkRenderPass                    renderPass = VK_NULL_HANDLE;
    VkPipeline                      pipeline = VK_NULL_HANDLE;

    uint32_t                        cmdQueueFamily = UINT32_MAX;
    VkCommandPool                   cmdPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer>    cmdBufs;

    std::vector<SyncSlot>           syncRing;
    size_t                          syncCursor = 0;
};

std::mutex                                          gMapMutex;
std::unordered_map<void*, InstanceData>             gInstances;
std::unordered_map<void*, DeviceData>               gDevices;
std::unordered_map<VkQueue, uint32_t>               gQueueFamilies;

std::mutex                                          gSwapMutex;
std::unordered_map<VkSwapchainKHR, SwapchainUpscaler> gSwapchains;

InstanceData* findInstance(const void* handle) {
    std::lock_guard<std::mutex> l(gMapMutex);
    auto it = gInstances.find(dispatchKey(handle));
    return it == gInstances.end() ? nullptr : &it->second;
}

InstanceData* findInstanceByVkInstance(VkInstance inst) {
    std::lock_guard<std::mutex> l(gMapMutex);
    for (auto& kv : gInstances)
        if (kv.second.instance == inst) return &kv.second;
    return nullptr;
}

DeviceData* findDevice(const void* handle) {
    std::lock_guard<std::mutex> l(gMapMutex);
    auto it = gDevices.find(dispatchKey(handle));
    return it == gDevices.end() ? nullptr : &it->second;
}

uint32_t lookupQueueFamily(VkQueue queue, const DeviceData* d) {
    std::lock_guard<std::mutex> l(gMapMutex);
    auto it = gQueueFamilies.find(queue);
    return it != gQueueFamilies.end() ? it->second : d->defaultQueueFamily;
}

int32_t findMemoryType(const VkPhysicalDeviceMemoryProperties& mp, uint32_t typeBits, VkMemoryPropertyFlags req) {
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        if ((typeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & req) == req)
            return (int32_t)i;
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        if (typeBits & (1u << i))
            return (int32_t)i;
    return -1;
}

// ----------------------------------------------------------------------------
// Frame pacing + telemetry
// ----------------------------------------------------------------------------
inline int64_t nowNs() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

std::mutex           gPaceMutex;
int64_t              gNextFrameNs = 0;
int64_t              gLastPresentNs = 0;
std::vector<float>   gFrameMs;
int64_t              gStatsWindowStartNs = 0;

void sleepUntil(int64_t targetNs) {
    const int64_t spinMargin = 150000;
    int64_t coarse = targetNs - spinMargin;
    if (nowNs() < coarse) {
        timespec ts;
        ts.tv_sec  = coarse / 1000000000LL;
        ts.tv_nsec = coarse % 1000000000LL;
        while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr) == EINTR) {}
    }
    while (nowNs() < targetNs) { /* spin */ }
}

void paceFrame() {
    if (gCfg.fpsCap > 0.0) {
        const int64_t interval = (int64_t)(1.0e9 / gCfg.fpsCap);
        int64_t target;
        {
            std::lock_guard<std::mutex> l(gPaceMutex);
            int64_t now = nowNs();
            if (gNextFrameNs == 0 || now > gNextFrameNs + interval * 2) gNextFrameNs = now;
            target = gNextFrameNs;
            gNextFrameNs += interval;
        }
        if (nowNs() < target) sleepUntil(target);
    }
}

void recordFrame() {
    if (!gCfg.log) return;
    std::lock_guard<std::mutex> l(gPaceMutex);
    int64_t now = nowNs();
    if (gLastPresentNs != 0) gFrameMs.push_back((float)((now - gLastPresentNs) / 1.0e6));
    gLastPresentNs = now;
    if (gStatsWindowStartNs == 0) gStatsWindowStartNs = now;
    if (now - gStatsWindowStartNs >= 2000000000LL && gFrameMs.size() >= 10) {
        std::vector<float> sorted = gFrameMs;
        std::sort(sorted.begin(), sorted.end());
        double sum = 0;
        for (float v : sorted) sum += v;
        double avgMs = sum / sorted.size();
        size_t worstN = std::max<size_t>(1, sorted.size() / 100);
        double worstSum = 0;
        for (size_t i = sorted.size() - worstN; i < sorted.size(); ++i) worstSum += sorted[i];
        double low1 = 1000.0 / (worstSum / worstN);
        logLine("fps avg=%.1f  1%%low=%.1f  frametime avg=%.2f ms  max=%.2f ms  (%zu frames)",
                1000.0 / avgMs, low1, avgMs, (double)sorted.back(), sorted.size());
        gFrameMs.clear();
        gStatsWindowStartNs = now;
    }
}

// ----------------------------------------------------------------------------
// Upscaler teardown & command buffer recording
// ----------------------------------------------------------------------------
void destroyUpscalerResources(DeviceData* d, SwapchainUpscaler& u) {
    if (!d) return;
    VkDevice dev = u.device;
    if (u.cmdPool && d->destroyCommandPool)   d->destroyCommandPool(dev, u.cmdPool, nullptr);
    for (auto& s : u.syncRing) {
        if (s.sem   && d->destroySemaphore)   d->destroySemaphore(dev, s.sem, nullptr);
        if (s.fence && d->destroyFence)       d->destroyFence(dev, s.fence, nullptr);
    }
    for (VkFramebuffer fb : u.framebuffers)
        if (fb && d->destroyFramebuffer)      d->destroyFramebuffer(dev, fb, nullptr);
    if (u.pipeline   && d->destroyPipeline)   d->destroyPipeline(dev, u.pipeline, nullptr);
    if (u.renderPass && d->destroyRenderPass) d->destroyRenderPass(dev, u.renderPass, nullptr);
    if (u.pipeLayout && d->destroyPipelineLayout) d->destroyPipelineLayout(dev, u.pipeLayout, nullptr);
    if (u.dsPool     && d->destroyDescriptorPool) d->destroyDescriptorPool(dev, u.dsPool, nullptr);
    if (u.dsLayout   && d->destroyDescriptorSetLayout) d->destroyDescriptorSetLayout(dev, u.dsLayout, nullptr);
    if (u.vertModule && d->destroyShaderModule) d->destroyShaderModule(dev, u.vertModule, nullptr);
    if (u.fragModule && d->destroyShaderModule) d->destroyShaderModule(dev, u.fragModule, nullptr);
    if (u.sampler    && d->destroySampler)    d->destroySampler(dev, u.sampler, nullptr);
    for (VkImageView iv : u.realViews)
        if (iv && d->destroyImageView)        d->destroyImageView(dev, iv, nullptr);
    for (VkImageView iv : u.virtViews)
        if (iv && d->destroyImageView)        d->destroyImageView(dev, iv, nullptr);
    for (VkImage img : u.virtImages)
        if (img && d->destroyImage)           d->destroyImage(dev, img, nullptr);
    for (VkDeviceMemory mem : u.virtMemories)
        if (mem && d->freeMemory)             d->freeMemory(dev, mem, nullptr);
    u = SwapchainUpscaler{};
}

struct PushConstants {
    float invSrcX;
    float invSrcY;
    float sharpen;
    float pad;
};

bool ensureUpscaleCommandBuffers(DeviceData* d, SwapchainUpscaler& u, uint32_t qFamily) {
    if (u.cmdPool != VK_NULL_HANDLE && u.cmdQueueFamily == qFamily && u.cmdBufs.size() == u.realImages.size())
        return true;

    if (u.cmdPool != VK_NULL_HANDLE) {
        if (d->deviceWaitIdle) d->deviceWaitIdle(u.device);
        d->destroyCommandPool(u.device, u.cmdPool, nullptr);
        u.cmdPool = VK_NULL_HANDLE;
        u.cmdBufs.clear();
    }

    const bool queueSupportsGraphics =
        (qFamily < d->queueProps.size()) && ((d->queueProps[qFamily].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0);
    const bool runShader = u.useCasShader && queueSupportsGraphics && (u.pipeline != VK_NULL_HANDLE);

    VkCommandPoolCreateInfo cpci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cpci.queueFamilyIndex = qFamily;
    if (d->createCommandPool(u.device, &cpci, nullptr, &u.cmdPool) != VK_SUCCESS)
        return false;
    u.cmdQueueFamily = qFamily;

    const uint32_t count = (uint32_t)u.realImages.size();
    u.cmdBufs.resize(count, VK_NULL_HANDLE);
    VkCommandBufferAllocateInfo cbai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cbai.commandPool        = u.cmdPool;
    cbai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = count;
    if (d->allocateCommandBuffers(u.device, &cbai, u.cmdBufs.data()) != VK_SUCCESS)
        return false;

    VkImageSubresourceRange subRange{};
    subRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    subRange.baseMipLevel   = 0;
    subRange.levelCount     = 1;
    subRange.baseArrayLayer = 0;
    subRange.layerCount     = 1;

    PushConstants pc{
        1.0f / (float)std::max(1u, u.renderExtent.width),
        1.0f / (float)std::max(1u, u.renderExtent.height),
        u.sharpen,
        0.0f
    };

    for (uint32_t i = 0; i < count; ++i) {
        VkCommandBuffer cmd = u.cmdBufs[i];
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        bi.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
        if (d->beginCommandBuffer(cmd, &bi) != VK_SUCCESS) return false;

        if (runShader) {
            // 1. Transition virtual image: PRESENT_SRC_KHR -> SHADER_READ_ONLY_OPTIMAL
            VkImageMemoryBarrier bIn{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            bIn.srcAccessMask       = VK_ACCESS_MEMORY_WRITE_BIT;
            bIn.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
            bIn.oldLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            bIn.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            bIn.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            bIn.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            bIn.image               = u.virtImages[i];
            bIn.subresourceRange    = subRange;
            d->cmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                                  0, nullptr, 0, nullptr, 1, &bIn);

            // 2. Render fullscreen CAS upscale pass into real swapchain image
            //    (initialLayout=UNDEFINED -> finalLayout=PRESENT_SRC_KHR)
            VkRenderPassBeginInfo rpbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
            rpbi.renderPass        = u.renderPass;
            rpbi.framebuffer       = u.framebuffers[i];
            rpbi.renderArea.offset = {0, 0};
            rpbi.renderArea.extent = u.realExtent;
            d->cmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
            d->cmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, u.pipeline);
            d->cmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, u.pipeLayout,
                                     0, 1, &u.dsSets[i], 0, nullptr);
            d->cmdPushConstants(cmd, u.pipeLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
            d->cmdDraw(cmd, 3, 1, 0, 0);
            d->cmdEndRenderPass(cmd);

            // 3. Transition virtual image back to PRESENT_SRC_KHR so apps expecting
            //    oldLayout=PRESENT_SRC_KHR on the next frame remain valid.
            VkImageMemoryBarrier bOut{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            bOut.srcAccessMask       = VK_ACCESS_SHADER_READ_BIT;
            bOut.dstAccessMask       = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
            bOut.oldLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            bOut.newLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            bOut.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            bOut.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            bOut.image               = u.virtImages[i];
            bOut.subresourceRange    = subRange;
            d->cmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                  VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0,
                                  0, nullptr, 0, nullptr, 1, &bOut);
        } else {
            // Hardware blit fallback
            VkImageMemoryBarrier pre[2]{};
            pre[0].sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            pre[0].srcAccessMask       = VK_ACCESS_MEMORY_WRITE_BIT;
            pre[0].dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
            pre[0].oldLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            pre[0].newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            pre[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            pre[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            pre[0].image               = u.virtImages[i];
            pre[0].subresourceRange    = subRange;

            pre[1].sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            pre[1].srcAccessMask       = 0;
            pre[1].dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
            pre[1].oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
            pre[1].newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            pre[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            pre[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            pre[1].image               = u.realImages[i];
            pre[1].subresourceRange    = subRange;

            d->cmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                  VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                                  0, nullptr, 0, nullptr, 2, pre);

            VkImageBlit region{};
            region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.srcOffsets[1]  = {(int32_t)u.renderExtent.width, (int32_t)u.renderExtent.height, 1};
            region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.dstOffsets[1]  = {(int32_t)u.realExtent.width, (int32_t)u.realExtent.height, 1};

            d->cmdBlitImage(cmd,
                            u.virtImages[i], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                            u.realImages[i], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            1, &region, gCfg.scaleLinear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST);

            VkImageMemoryBarrier post[2]{};
            post[0].sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            post[0].srcAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
            post[0].dstAccessMask       = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
            post[0].oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            post[0].newLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            post[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            post[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            post[0].image               = u.virtImages[i];
            post[0].subresourceRange    = subRange;

            post[1].sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            post[1].srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
            post[1].dstAccessMask       = 0;
            post[1].oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            post[1].newLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            post[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            post[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            post[1].image               = u.realImages[i];
            post[1].subresourceRange    = subRange;

            d->cmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                  VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0,
                                  0, nullptr, 0, nullptr, 2, post);
        }

        if (d->endCommandBuffer(cmd) != VK_SUCCESS) return false;
    }
    return true;
}

bool buildCasPipeline(DeviceData* d, SwapchainUpscaler& u) {
    VkDevice dev = u.device;
    const uint32_t count = (uint32_t)u.realImages.size();

    VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sci.magFilter        = gCfg.scaleLinear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    sci.minFilter        = gCfg.scaleLinear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    sci.mipmapMode       = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sci.addressModeU     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeV     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeW     = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.minLod           = 0.0f;
    sci.maxLod           = 0.0f;
    if (d->createSampler(dev, &sci, nullptr, &u.sampler) != VK_SUCCESS) return false;

    VkShaderModuleCreateInfo vmci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    vmci.codeSize = sizeof(kUpscaleVertSpv);
    vmci.pCode    = kUpscaleVertSpv;
    if (d->createShaderModule(dev, &vmci, nullptr, &u.vertModule) != VK_SUCCESS) return false;

    VkShaderModuleCreateInfo fmci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    fmci.codeSize = sizeof(kUpscaleFragSpv);
    fmci.pCode    = kUpscaleFragSpv;
    if (d->createShaderModule(dev, &fmci, nullptr, &u.fragModule) != VK_SUCCESS) return false;

    VkDescriptorSetLayoutBinding bind{};
    bind.binding         = 0;
    bind.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bind.descriptorCount = 1;
    bind.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo dslci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dslci.bindingCount = 1;
    dslci.pBindings    = &bind;
    if (d->createDescriptorSetLayout(dev, &dslci, nullptr, &u.dsLayout) != VK_SUCCESS) return false;

    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, count};
    VkDescriptorPoolCreateInfo dpci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpci.maxSets       = count;
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes    = &poolSize;
    if (d->createDescriptorPool(dev, &dpci, nullptr, &u.dsPool) != VK_SUCCESS) return false;

    std::vector<VkDescriptorSetLayout> layouts(count, u.dsLayout);
    u.dsSets.resize(count, VK_NULL_HANDLE);
    VkDescriptorSetAllocateInfo dsai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dsai.descriptorPool     = u.dsPool;
    dsai.descriptorSetCount = count;
    dsai.pSetLayouts        = layouts.data();
    if (d->allocateDescriptorSets(dev, &dsai, u.dsSets.data()) != VK_SUCCESS) return false;

    u.virtViews.resize(count, VK_NULL_HANDLE);
    u.realViews.resize(count, VK_NULL_HANDLE);
    for (uint32_t i = 0; i < count; ++i) {
        VkImageViewCreateInfo ivci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        ivci.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        ivci.format                          = u.format;
        ivci.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        ivci.subresourceRange.baseMipLevel   = 0;
        ivci.subresourceRange.levelCount     = 1;
        ivci.subresourceRange.baseArrayLayer = 0;
        ivci.subresourceRange.layerCount     = 1;

        ivci.image = u.virtImages[i];
        if (d->createImageView(dev, &ivci, nullptr, &u.virtViews[i]) != VK_SUCCESS) return false;

        ivci.image = u.realImages[i];
        if (d->createImageView(dev, &ivci, nullptr, &u.realViews[i]) != VK_SUCCESS) return false;

        VkDescriptorImageInfo dii{};
        dii.sampler     = u.sampler;
        dii.imageView   = u.virtViews[i];
        dii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet wds{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        wds.dstSet          = u.dsSets[i];
        wds.dstBinding      = 0;
        wds.descriptorCount = 1;
        wds.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        wds.pImageInfo      = &dii;
        d->updateDescriptorSets(dev, 1, &wds, 0, nullptr);
    }

    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pcr.offset     = 0;
    pcr.size       = sizeof(PushConstants);

    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount         = 1;
    plci.pSetLayouts            = &u.dsLayout;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges    = &pcr;
    if (d->createPipelineLayout(dev, &plci, nullptr, &u.pipeLayout) != VK_SUCCESS) return false;

    VkAttachmentDescription att{};
    att.format         = u.format;
    att.samples        = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    att.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &colorRef;

    VkSubpassDependency dep{};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = 0;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpci.attachmentCount = 1;
    rpci.pAttachments    = &att;
    rpci.subpassCount    = 1;
    rpci.pSubpasses      = &subpass;
    rpci.dependencyCount = 1;
    rpci.pDependencies   = &dep;
    if (d->createRenderPass(dev, &rpci, nullptr, &u.renderPass) != VK_SUCCESS) return false;

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = u.vertModule;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = u.fragModule;
    stages[1].pName  = "main";

    VkPipelineVertexInputStateCreateInfo   vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport vp{0.0f, 0.0f, (float)u.realExtent.width, (float)u.realExtent.height, 0.0f, 1.0f};
    VkRect2D   sc{{0, 0}, u.realExtent};
    VkPipelineViewportStateCreateInfo vps{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vps.viewportCount = 1;
    vps.pViewports    = &vp;
    vps.scissorCount  = 1;
    vps.pScissors     = &sc;

    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode    = VK_CULL_MODE_NONE;
    rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments    = &cba;

    VkGraphicsPipelineCreateInfo gpci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gpci.stageCount          = 2;
    gpci.pStages             = stages;
    gpci.pVertexInputState   = &vi;
    gpci.pInputAssemblyState = &ia;
    gpci.pViewportState      = &vps;
    gpci.pRasterizationState = &rs;
    gpci.pMultisampleState   = &ms;
    gpci.pColorBlendState    = &cb;
    gpci.layout              = u.pipeLayout;
    gpci.renderPass          = u.renderPass;
    gpci.subpass             = 0;
    if (d->createGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gpci, nullptr, &u.pipeline) != VK_SUCCESS)
        return false;

    u.framebuffers.resize(count, VK_NULL_HANDLE);
    for (uint32_t i = 0; i < count; ++i) {
        VkFramebufferCreateInfo fbci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fbci.renderPass      = u.renderPass;
        fbci.attachmentCount = 1;
        fbci.pAttachments    = &u.realViews[i];
        fbci.width           = u.realExtent.width;
        fbci.height          = u.realExtent.height;
        fbci.layers          = 1;
        if (d->createFramebuffer(dev, &fbci, nullptr, &u.framebuffers[i]) != VK_SUCCESS) return false;
    }
    return true;
}

// ----------------------------------------------------------------------------
// Hooked Vulkan entry points
// ----------------------------------------------------------------------------
int gpuPriorityScore(const VkPhysicalDeviceProperties& p) {
    if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
        return (p.vendorID == 0x10de) ? 0 : 1;   // NVIDIA discrete GPU first
    if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) return 2;
    if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU)    return 3;
    return 4;                                    // CPU (llvmpipe) / other last
}

VKAPI_ATTR VkResult VKAPI_CALL hookEnumeratePhysicalDevices(
    VkInstance instance, uint32_t* pCount, VkPhysicalDevice* pDevices) {
    InstanceData* inst = findInstance(instance);
    if (!inst || !inst->enumeratePhysicalDevices) return VK_ERROR_INITIALIZATION_FAILED;

    if (!pDevices)
        return inst->enumeratePhysicalDevices(instance, pCount, nullptr);

    uint32_t total = 0;
    VkResult r = inst->enumeratePhysicalDevices(instance, &total, nullptr);
    if (r != VK_SUCCESS || total == 0) {
        *pCount = 0;
        return r;
    }

    std::vector<VkPhysicalDevice> all(total);
    r = inst->enumeratePhysicalDevices(instance, &total, all.data());
    if (r != VK_SUCCESS && r != VK_INCOMPLETE) return r;

    if (inst->getPhysicalDeviceProperties && total > 1) {
        std::stable_sort(all.begin(), all.end(), [&](VkPhysicalDevice a, VkPhysicalDevice b) {
            VkPhysicalDeviceProperties pa{}, pb{};
            inst->getPhysicalDeviceProperties(a, &pa);
            inst->getPhysicalDeviceProperties(b, &pb);
            return gpuPriorityScore(pa) < gpuPriorityScore(pb);
        });
    }

    uint32_t copyN = std::min(*pCount, total);
    for (uint32_t i = 0; i < copyN; ++i) pDevices[i] = all[i];
    *pCount = copyN;
    return copyN < total ? VK_INCOMPLETE : VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL hookGetPhysicalDeviceSurfaceCapabilitiesKHR(
    VkPhysicalDevice phys, VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR* caps) {
    InstanceData* inst = findInstance(phys);
    if (!inst || !inst->getSurfaceCaps) return VK_ERROR_INITIALIZATION_FAILED;
    VkResult r = inst->getSurfaceCaps(phys, surface, caps);
    if (r != VK_SUCCESS) return r;

    if (gCfg.scale < 0.999f && caps->currentExtent.width != 0xFFFFFFFFu &&
        caps->currentExtent.width > 0 && caps->currentExtent.height > 0) {
        VkExtent2D s = computeScaledExtent(caps->currentExtent, gCfg.scale);
        caps->currentExtent  = s;
        caps->minImageExtent = {std::min(caps->minImageExtent.width,  s.width),
                                std::min(caps->minImageExtent.height, s.height)};
        caps->maxImageExtent = s;
    }
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL hookGetPhysicalDeviceSurfaceCapabilities2KHR(
    VkPhysicalDevice phys, const VkPhysicalDeviceSurfaceInfo2KHR* info, VkSurfaceCapabilities2KHR* caps2) {
    InstanceData* inst = findInstance(phys);
    if (!inst || !inst->getSurfaceCaps2) return VK_ERROR_INITIALIZATION_FAILED;
    VkResult r = inst->getSurfaceCaps2(phys, info, caps2);
    if (r != VK_SUCCESS) return r;

    VkSurfaceCapabilitiesKHR* c = &caps2->surfaceCapabilities;
    if (gCfg.scale < 0.999f && c->currentExtent.width != 0xFFFFFFFFu &&
        c->currentExtent.width > 0 && c->currentExtent.height > 0) {
        VkExtent2D s = computeScaledExtent(c->currentExtent, gCfg.scale);
        c->currentExtent  = s;
        c->minImageExtent = {std::min(c->minImageExtent.width,  s.width),
                             std::min(c->minImageExtent.height, s.height)};
        c->maxImageExtent = s;
    }
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL hookGetDeviceQueue(VkDevice device, uint32_t qFamily, uint32_t qIndex, VkQueue* pQueue) {
    DeviceData* d = findDevice(device);
    if (!d || !d->getDeviceQueue) return;
    d->getDeviceQueue(device, qFamily, qIndex, pQueue);
    if (pQueue && *pQueue) {
        std::lock_guard<std::mutex> l(gMapMutex);
        gQueueFamilies[*pQueue] = qFamily;
    }
}

VKAPI_ATTR void VKAPI_CALL hookGetDeviceQueue2(VkDevice device, const VkDeviceQueueInfo2* info, VkQueue* pQueue) {
    DeviceData* d = findDevice(device);
    if (!d || !d->getDeviceQueue2) return;
    d->getDeviceQueue2(device, info, pQueue);
    if (pQueue && *pQueue && info) {
        std::lock_guard<std::mutex> l(gMapMutex);
        gQueueFamilies[*pQueue] = info->queueFamilyIndex;
    }
}

bool hasYcbcrConversion(const VkSamplerCreateInfo* ci) {
    for (const VkBaseInStructure* p = reinterpret_cast<const VkBaseInStructure*>(ci->pNext); p; p = p->pNext)
        if (p->sType == VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO) return true;
    return false;
}

VKAPI_ATTR VkResult VKAPI_CALL hookCreateSampler(VkDevice device, const VkSamplerCreateInfo* ci,
                                                 const VkAllocationCallbacks* alloc, VkSampler* out) {
    DeviceData* d = findDevice(device);
    VkSamplerCreateInfo mod = *ci;

    const bool isTexture = ci->minFilter == VK_FILTER_LINEAR &&
                           ci->mipmapMode == VK_SAMPLER_MIPMAP_MODE_LINEAR &&
                           ci->maxLod > 0.25f &&
                           !ci->compareEnable && !ci->unnormalizedCoordinates &&
                           !hasYcbcrConversion(ci);

    if (d && isTexture) {
        bool changed = false;
        if (gCfg.af > 1 && d->samplerAnisotropy) {
            float want = std::min((float)gCfg.af, d->maxAnisotropy);
            float have = ci->anisotropyEnable ? ci->maxAnisotropy : 1.0f;
            if (want > have) {
                mod.anisotropyEnable = VK_TRUE;
                mod.maxAnisotropy    = want;
                changed = true;
            }
        }
        if (gCfg.lodBias != 0.0f) {
            float b = ci->mipLodBias + gCfg.lodBias;
            mod.mipLodBias = std::max(-d->maxLodBias, std::min(d->maxLodBias, b));
            changed = true;
        }
        if (changed) {
            static std::atomic<int> count{0};
            int n = ++count;
            if (n <= 5)
                logLine("sampler #%d boosted: aniso %s/%.0f -> %s/%.0f, lodBias %.2f -> %.2f (maxLod %.1f)", n,
                        ci->anisotropyEnable ? "on" : "off", ci->maxAnisotropy,
                        mod.anisotropyEnable ? "on" : "off", mod.maxAnisotropy,
                        ci->mipLodBias, mod.mipLodBias, ci->maxLod);
        }
    }
    return d->createSampler(device, &mod, alloc, out);
}

VKAPI_ATTR VkResult VKAPI_CALL hookCreateSwapchainKHR(
    VkDevice device, const VkSwapchainCreateInfoKHR* ci,
    const VkAllocationCallbacks* alloc, VkSwapchainKHR* out) {
    DeviceData* d = findDevice(device);
    if (!d || !d->createSwapchain) return VK_ERROR_INITIALIZATION_FAILED;

    InstanceData* inst = findInstanceByVkInstance(d->instance);
    if (!inst || !inst->getSurfaceCaps || ci->imageArrayLayers > 1 ||
        ci->imageExtent.width == 0 || ci->imageExtent.height == 0) {
        return d->createSwapchain(device, ci, alloc, out);
    }

    VkSurfaceCapabilitiesKHR realCaps{};
    if (inst->getSurfaceCaps(d->physDevice, ci->surface, &realCaps) != VK_SUCCESS)
        return d->createSwapchain(device, ci, alloc, out);

    VkExtent2D realExt = (realCaps.currentExtent.width != 0xFFFFFFFFu &&
                          realCaps.currentExtent.width > 0 && realCaps.currentExtent.height > 0)
                             ? realCaps.currentExtent
                             : ci->imageExtent;

    const VkExtent2D renderExt = ci->imageExtent;
    const bool needUpscale = (renderExt.width != realExt.width || renderExt.height != realExt.height);
    const bool wantNativeCas = (!needUpscale && gCfg.sharpenExplicit && gCfg.sharpen > 0.001f);

    if (!needUpscale && !wantNativeCas) {
        logLine("swapchain passthrough: appExtent=%ux%u realCaps=%ux%u (scale=%.2f)",
                renderExt.width, renderExt.height, realExt.width, realExt.height, gCfg.scale);
        return d->createSwapchain(device, ci, alloc, out);
    }

    // Check format capabilities for CAS graphics pipeline vs hardware blit fallback
    VkFormatProperties fmtProps{};
    if (inst->getPhysicalDeviceFormatProperties)
        inst->getPhysicalDeviceFormatProperties(d->physDevice, ci->imageFormat, &fmtProps);

    VkFormatFeatureFlags casReq = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
    if (gCfg.scaleLinear) casReq |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    const bool fmtSupportsCas = (fmtProps.optimalTilingFeatures & casReq) == casReq;
    const bool surfSupportsColor = (realCaps.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) != 0;
    const bool surfSupportsBlit  = (realCaps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) != 0;

    const bool useCas = fmtSupportsCas && surfSupportsColor;
    if (!useCas && !surfSupportsBlit)
        return d->createSwapchain(device, ci, alloc, out);

    VkSwapchainCreateInfoKHR realCi = *ci;
    realCi.imageExtent = realExt;
    realCi.imageUsage  = useCas ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT : VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (surfSupportsBlit)  realCi.imageUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (surfSupportsColor) realCi.imageUsage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    VkResult r = d->createSwapchain(device, &realCi, alloc, out);
    if (r != VK_SUCCESS) return r;

    SwapchainUpscaler u;
    u.device       = device;
    u.swapchain    = *out;
    u.format       = ci->imageFormat;
    u.renderExtent = renderExt;
    u.realExtent   = realExt;
    u.sharpen      = needUpscale ? gCfg.sharpen : (gCfg.sharpenExplicit ? gCfg.sharpen : 0.0f);
    u.useCasShader = useCas;

    uint32_t imgCount = 0;
    if (d->getSwapchainImages(device, *out, &imgCount, nullptr) != VK_SUCCESS || imgCount == 0) {
        d->destroySwapchain(device, *out, alloc);
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    u.realImages.resize(imgCount);
    d->getSwapchainImages(device, *out, &imgCount, u.realImages.data());

    // Forward VK_SWAPCHAIN_CREATE_MUTABLE_FORMAT_BIT_KHR + VkImageFormatListCreateInfo for DXVK/VKD3D
    const VkImageFormatListCreateInfo* fmtList = nullptr;
    for (const VkBaseInStructure* p = reinterpret_cast<const VkBaseInStructure*>(ci->pNext); p; p = p->pNext) {
        if (p->sType == VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO) {
            fmtList = reinterpret_cast<const VkImageFormatListCreateInfo*>(p);
            break;
        }
    }
    VkImageFormatListCreateInfo localFmtList{};
    if (fmtList) {
        localFmtList       = *fmtList;
        localFmtList.pNext = nullptr;
    }

    u.virtImages.resize(imgCount, VK_NULL_HANDLE);
    u.virtMemories.resize(imgCount, VK_NULL_HANDLE);

    for (uint32_t i = 0; i < imgCount; ++i) {
        VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        if (fmtList) ici.pNext = &localFmtList;
        if (ci->flags & VK_SWAPCHAIN_CREATE_MUTABLE_FORMAT_BIT_KHR)
            ici.flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
        ici.imageType             = VK_IMAGE_TYPE_2D;
        ici.format                = ci->imageFormat;
        ici.extent                = {renderExt.width, renderExt.height, 1};
        ici.mipLevels             = 1;
        ici.arrayLayers           = 1;
        ici.samples               = VK_SAMPLE_COUNT_1_BIT;
        ici.tiling                = VK_IMAGE_TILING_OPTIMAL;
        ici.usage                 = ci->imageUsage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        if (useCas) ici.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
        ici.sharingMode           = ci->imageSharingMode;
        ici.queueFamilyIndexCount = ci->queueFamilyIndexCount;
        ici.pQueueFamilyIndices   = ci->pQueueFamilyIndices;
        ici.initialLayout         = VK_IMAGE_LAYOUT_UNDEFINED;

        if (d->createImage(device, &ici, nullptr, &u.virtImages[i]) != VK_SUCCESS) {
            destroyUpscalerResources(d, u);
            d->destroySwapchain(device, *out, alloc);
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;
        }

        VkMemoryRequirements mr{};
        d->getImageMemoryRequirements(device, u.virtImages[i], &mr);
        int32_t memIdx = findMemoryType(d->memProps, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (memIdx < 0) {
            destroyUpscalerResources(d, u);
            d->destroySwapchain(device, *out, alloc);
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;
        }

        VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        mai.allocationSize  = mr.size;
        mai.memoryTypeIndex = (uint32_t)memIdx;
        if (d->allocateMemory(device, &mai, nullptr, &u.virtMemories[i]) != VK_SUCCESS ||
            d->bindImageMemory(device, u.virtImages[i], u.virtMemories[i], 0) != VK_SUCCESS) {
            destroyUpscalerResources(d, u);
            d->destroySwapchain(device, *out, alloc);
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;
        }
    }

    if (useCas && !buildCasPipeline(d, u)) {
        logLine("CAS pipeline build failed on format %d -> falling back to hardware blit", (int)ci->imageFormat);
        u.useCasShader = false;
    }

    const size_t ringCount = std::max<size_t>(12, (size_t)imgCount * 3);
    u.syncRing.resize(ringCount);
    VkSemaphoreCreateInfo semCi{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo     fenCi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    for (size_t i = 0; i < ringCount; ++i) {
        if (d->createSemaphore(device, &semCi, nullptr, &u.syncRing[i].sem) != VK_SUCCESS ||
            d->createFence(device, &fenCi, nullptr, &u.syncRing[i].fence) != VK_SUCCESS) {
            destroyUpscalerResources(d, u);
            d->destroySwapchain(device, *out, alloc);
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
    }

    logLine("swapchain upscaler active: render %ux%u -> display %ux%u (images=%u, cas=%.2f, mode=%s)",
            renderExt.width, renderExt.height, realExt.width, realExt.height, imgCount,
            u.sharpen, u.useCasShader ? "CAS-shader" : "blit");

    std::lock_guard<std::mutex> l(gSwapMutex);
    gSwapchains[*out] = std::move(u);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL hookDestroySwapchainKHR(
    VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks* alloc) {
    DeviceData* d = findDevice(device);
    if (!d) return;

    if (swapchain != VK_NULL_HANDLE) {
        SwapchainUpscaler removed;
        bool had = false;
        {
            std::lock_guard<std::mutex> l(gSwapMutex);
            auto it = gSwapchains.find(swapchain);
            if (it != gSwapchains.end()) {
                removed = std::move(it->second);
                gSwapchains.erase(it);
                had = true;
            }
        }
        if (had) {
            if (d->deviceWaitIdle) d->deviceWaitIdle(device);
            destroyUpscalerResources(d, removed);
        }
    }
    if (d->destroySwapchain) d->destroySwapchain(device, swapchain, alloc);
}

VKAPI_ATTR VkResult VKAPI_CALL hookGetSwapchainImagesKHR(
    VkDevice device, VkSwapchainKHR swapchain, uint32_t* pCount, VkImage* pImages) {
    DeviceData* d = findDevice(device);
    if (!d || !d->getSwapchainImages) return VK_ERROR_INITIALIZATION_FAILED;

    {
        std::lock_guard<std::mutex> l(gSwapMutex);
        auto it = gSwapchains.find(swapchain);
        if (it != gSwapchains.end()) {
            const auto& virt = it->second.virtImages;
            if (!pImages) {
                *pCount = (uint32_t)virt.size();
                return VK_SUCCESS;
            }
            uint32_t n = std::min<uint32_t>(*pCount, (uint32_t)virt.size());
            for (uint32_t i = 0; i < n; ++i) pImages[i] = virt[i];
            *pCount = n;
            return n < virt.size() ? VK_INCOMPLETE : VK_SUCCESS;
        }
    }
    return d->getSwapchainImages(device, swapchain, pCount, pImages);
}

VKAPI_ATTR VkResult VKAPI_CALL hookQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* info) {
    DeviceData* d = findDevice(queue);
    if (!d || !d->queuePresent) return VK_ERROR_INITIALIZATION_FAILED;

    paceFrame();

    // Fast path when no upscaled swapchains are present
    bool anyUpscaled = false;
    {
        std::lock_guard<std::mutex> l(gSwapMutex);
        if (!gSwapchains.empty() && info) {
            for (uint32_t i = 0; i < info->swapchainCount; ++i) {
                if (gSwapchains.find(info->pSwapchains[i]) != gSwapchains.end()) {
                    anyUpscaled = true;
                    break;
                }
            }
        }
    }

    if (!anyUpscaled) {
        VkResult r = d->queuePresent(queue, info);
        recordFrame();
        return r;
    }

    uint32_t qFamily = lookupQueueFamily(queue, d);
    std::vector<VkSemaphore> postUpscaleSems;
    postUpscaleSems.reserve(info->swapchainCount);

    {
        std::lock_guard<std::mutex> l(gSwapMutex);
        bool waitedAppSems = false;
        for (uint32_t i = 0; i < info->swapchainCount; ++i) {
            auto it = gSwapchains.find(info->pSwapchains[i]);
            if (it == gSwapchains.end()) continue;

            SwapchainUpscaler& u = it->second;
            uint32_t imgIdx = info->pImageIndices[i];
            if (imgIdx >= u.realImages.size()) continue;
            if (!ensureUpscaleCommandBuffers(d, u, qFamily)) continue;

            SyncSlot& slot = u.syncRing[u.syncCursor];
            u.syncCursor = (u.syncCursor + 1) % u.syncRing.size();
            if (slot.submitted) {
                d->waitForFences(u.device, 1, &slot.fence, VK_TRUE, UINT64_MAX);
                d->resetFences(u.device, 1, &slot.fence);
                slot.submitted = false;
            }

            uint32_t waitCount = (!waitedAppSems) ? info->waitSemaphoreCount : 0;
            const VkSemaphore* waitSems = (!waitedAppSems) ? info->pWaitSemaphores : nullptr;
            std::vector<VkPipelineStageFlags> waitStages(waitCount, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);

            VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
            si.waitSemaphoreCount   = waitCount;
            si.pWaitSemaphores      = waitSems;
            si.pWaitDstStageMask    = waitCount ? waitStages.data() : nullptr;
            si.commandBufferCount   = 1;
            si.pCommandBuffers      = &u.cmdBufs[imgIdx];
            si.signalSemaphoreCount = 1;
            si.pSignalSemaphores    = &slot.sem;

            if (d->queueSubmit(queue, 1, &si, slot.fence) == VK_SUCCESS) {
                slot.submitted = true;
                waitedAppSems  = true;
                postUpscaleSems.push_back(slot.sem);
            }
        }
    }

    VkPresentInfoKHR mod = *info;
    if (!postUpscaleSems.empty()) {
        mod.waitSemaphoreCount = (uint32_t)postUpscaleSems.size();
        mod.pWaitSemaphores    = postUpscaleSems.data();
    }

    VkResult r = d->queuePresent(queue, &mod);
    recordFrame();
    return r;
}

VKAPI_ATTR void VKAPI_CALL hookDestroyDevice(VkDevice device, const VkAllocationCallbacks* alloc) {
    DeviceData* d = findDevice(device);
    if (d) {
        if (d->deviceWaitIdle) d->deviceWaitIdle(device);
        std::vector<SwapchainUpscaler> leftovers;
        {
            std::lock_guard<std::mutex> l(gSwapMutex);
            for (auto it = gSwapchains.begin(); it != gSwapchains.end();) {
                if (it->second.device == device) {
                    leftovers.push_back(std::move(it->second));
                    it = gSwapchains.erase(it);
                } else {
                    ++it;
                }
            }
        }
        for (auto& u : leftovers) destroyUpscalerResources(d, u);
    }

    void* key = dispatchKey(device);
    PFN_vkDestroyDevice destroy = nullptr;
    {
        std::lock_guard<std::mutex> l(gMapMutex);
        destroy = gDevices[key].destroyDevice;
    }
    if (destroy) destroy(device, alloc);
    std::lock_guard<std::mutex> l(gMapMutex);
    gDevices.erase(key);
}

VKAPI_ATTR void VKAPI_CALL hookDestroyInstance(VkInstance instance, const VkAllocationCallbacks* alloc) {
    void* key = dispatchKey(instance);
    PFN_vkDestroyInstance destroy = nullptr;
    {
        std::lock_guard<std::mutex> l(gMapMutex);
        destroy = gInstances[key].destroyInstance;
    }
    if (destroy) destroy(instance, alloc);
    std::lock_guard<std::mutex> l(gMapMutex);
    gInstances.erase(key);
}

VKAPI_ATTR VkResult VKAPI_CALL hookCreateInstance(const VkInstanceCreateInfo* ci, const VkAllocationCallbacks* alloc,
                                                  VkInstance* pInstance);
VKAPI_ATTR VkResult VKAPI_CALL hookCreateDevice(VkPhysicalDevice phys, const VkDeviceCreateInfo* ci,
                                                const VkAllocationCallbacks* alloc, VkDevice* pDevice);
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL hookGetDeviceProcAddr(VkDevice device, const char* name);
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL hookGetInstanceProcAddr(VkInstance instance, const char* name);

VKAPI_ATTR VkResult VKAPI_CALL hookCreateInstance(const VkInstanceCreateInfo* ci, const VkAllocationCallbacks* alloc,
                                                  VkInstance* pInstance) {
    loadConfig();

    auto* chain = reinterpret_cast<VkLayerInstanceCreateInfo*>(const_cast<void*>(ci->pNext));
    while (chain && !(chain->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO &&
                      chain->function == VK_LAYER_LINK_INFO))
        chain = reinterpret_cast<VkLayerInstanceCreateInfo*>(const_cast<void*>(chain->pNext));
    if (!chain) return VK_ERROR_INITIALIZATION_FAILED;

    PFN_vkGetInstanceProcAddr gipa = chain->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    chain->u.pLayerInfo = chain->u.pLayerInfo->pNext;

    auto createInstance = reinterpret_cast<PFN_vkCreateInstance>(gipa(VK_NULL_HANDLE, "vkCreateInstance"));
    if (!createInstance) return VK_ERROR_INITIALIZATION_FAILED;

    // Fix legacy Vulkan 1.0 apps (e.g. vkmark 2017) passing apiVersion == 0
    VkInstanceCreateInfo modCi = *ci;
    VkApplicationInfo    modApp{};
    if (ci->pApplicationInfo && ci->pApplicationInfo->apiVersion == 0) {
        modApp            = *ci->pApplicationInfo;
        modApp.apiVersion = VK_API_VERSION_1_0;
        modCi.pApplicationInfo = &modApp;
    }

    VkResult r = createInstance(&modCi, alloc, pInstance);
    if (r != VK_SUCCESS) return r;

    InstanceData id;
    id.instance = *pInstance;
    id.gipa     = gipa;
    id.destroyInstance = reinterpret_cast<PFN_vkDestroyInstance>(gipa(*pInstance, "vkDestroyInstance"));
    id.enumeratePhysicalDevices =
        reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(gipa(*pInstance, "vkEnumeratePhysicalDevices"));
    id.getPhysicalDeviceProperties =
        reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(gipa(*pInstance, "vkGetPhysicalDeviceProperties"));
    id.getPhysicalDeviceMemoryProperties =
        reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(gipa(*pInstance, "vkGetPhysicalDeviceMemoryProperties"));
    id.getPhysicalDeviceFormatProperties =
        reinterpret_cast<PFN_vkGetPhysicalDeviceFormatProperties>(gipa(*pInstance, "vkGetPhysicalDeviceFormatProperties"));
    id.getPhysicalDeviceQueueFamilyProperties =
        reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(gipa(*pInstance, "vkGetPhysicalDeviceQueueFamilyProperties"));
    id.getSurfaceCaps =
        reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR>(gipa(*pInstance, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR"));
    id.getSurfaceCaps2 =
        reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceCapabilities2KHR>(gipa(*pInstance, "vkGetPhysicalDeviceSurfaceCapabilities2KHR"));
    {
        std::lock_guard<std::mutex> l(gMapMutex);
        gInstances[dispatchKey(*pInstance)] = id;
    }
    return VK_SUCCESS;
}

#define LOAD_DEV_FN(field, name) \
    dd.field = reinterpret_cast<decltype(dd.field)>(gdpa(*pDevice, name))

VKAPI_ATTR VkResult VKAPI_CALL hookCreateDevice(VkPhysicalDevice phys, const VkDeviceCreateInfo* ci,
                                                const VkAllocationCallbacks* alloc, VkDevice* pDevice) {
    InstanceData* inst = findInstance(phys);
    if (!inst) return VK_ERROR_INITIALIZATION_FAILED;

    auto* chain = reinterpret_cast<VkLayerDeviceCreateInfo*>(const_cast<void*>(ci->pNext));
    while (chain && !(chain->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO &&
                      chain->function == VK_LAYER_LINK_INFO))
        chain = reinterpret_cast<VkLayerDeviceCreateInfo*>(const_cast<void*>(chain->pNext));
    if (!chain) return VK_ERROR_INITIALIZATION_FAILED;

    PFN_vkGetInstanceProcAddr gipa = chain->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr   gdpa = chain->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    chain->u.pLayerInfo = chain->u.pLayerInfo->pNext;

    auto createDevice = reinterpret_cast<PFN_vkCreateDevice>(gipa(inst->instance, "vkCreateDevice"));
    if (!createDevice) return VK_ERROR_INITIALIZATION_FAILED;
    VkResult r = createDevice(phys, ci, alloc, pDevice);
    if (r != VK_SUCCESS) return r;

    DeviceData dd;
    dd.device     = *pDevice;
    dd.physDevice = phys;
    dd.instance   = inst->instance;
    dd.gdpa       = gdpa;

    LOAD_DEV_FN(destroyDevice,              "vkDestroyDevice");
    LOAD_DEV_FN(deviceWaitIdle,             "vkDeviceWaitIdle");
    LOAD_DEV_FN(getDeviceQueue,             "vkGetDeviceQueue");
    LOAD_DEV_FN(getDeviceQueue2,            "vkGetDeviceQueue2");
    LOAD_DEV_FN(createSampler,              "vkCreateSampler");
    LOAD_DEV_FN(destroySampler,             "vkDestroySampler");
    LOAD_DEV_FN(createSwapchain,            "vkCreateSwapchainKHR");
    LOAD_DEV_FN(destroySwapchain,           "vkDestroySwapchainKHR");
    LOAD_DEV_FN(getSwapchainImages,         "vkGetSwapchainImagesKHR");
    LOAD_DEV_FN(queuePresent,               "vkQueuePresentKHR");
    LOAD_DEV_FN(queueSubmit,                "vkQueueSubmit");
    LOAD_DEV_FN(createImage,                "vkCreateImage");
    LOAD_DEV_FN(destroyImage,               "vkDestroyImage");
    LOAD_DEV_FN(getImageMemoryRequirements, "vkGetImageMemoryRequirements");
    LOAD_DEV_FN(allocateMemory,             "vkAllocateMemory");
    LOAD_DEV_FN(freeMemory,                 "vkFreeMemory");
    LOAD_DEV_FN(bindImageMemory,            "vkBindImageMemory");
    LOAD_DEV_FN(createImageView,            "vkCreateImageView");
    LOAD_DEV_FN(destroyImageView,           "vkDestroyImageView");
    LOAD_DEV_FN(createShaderModule,         "vkCreateShaderModule");
    LOAD_DEV_FN(destroyShaderModule,        "vkDestroyShaderModule");
    LOAD_DEV_FN(createDescriptorSetLayout,  "vkCreateDescriptorSetLayout");
    LOAD_DEV_FN(destroyDescriptorSetLayout, "vkDestroyDescriptorSetLayout");
    LOAD_DEV_FN(createDescriptorPool,       "vkCreateDescriptorPool");
    LOAD_DEV_FN(destroyDescriptorPool,      "vkDestroyDescriptorPool");
    LOAD_DEV_FN(allocateDescriptorSets,     "vkAllocateDescriptorSets");
    LOAD_DEV_FN(updateDescriptorSets,       "vkUpdateDescriptorSets");
    LOAD_DEV_FN(createPipelineLayout,       "vkCreatePipelineLayout");
    LOAD_DEV_FN(destroyPipelineLayout,      "vkDestroyPipelineLayout");
    LOAD_DEV_FN(createRenderPass,           "vkCreateRenderPass");
    LOAD_DEV_FN(destroyRenderPass,          "vkDestroyRenderPass");
    LOAD_DEV_FN(createGraphicsPipelines,    "vkCreateGraphicsPipelines");
    LOAD_DEV_FN(destroyPipeline,            "vkDestroyPipeline");
    LOAD_DEV_FN(createFramebuffer,          "vkCreateFramebuffer");
    LOAD_DEV_FN(destroyFramebuffer,         "vkDestroyFramebuffer");
    LOAD_DEV_FN(createCommandPool,          "vkCreateCommandPool");
    LOAD_DEV_FN(destroyCommandPool,         "vkDestroyCommandPool");
    LOAD_DEV_FN(allocateCommandBuffers,     "vkAllocateCommandBuffers");
    LOAD_DEV_FN(beginCommandBuffer,         "vkBeginCommandBuffer");
    LOAD_DEV_FN(endCommandBuffer,           "vkEndCommandBuffer");
    LOAD_DEV_FN(cmdPipelineBarrier,         "vkCmdPipelineBarrier");
    LOAD_DEV_FN(cmdBeginRenderPass,         "vkCmdBeginRenderPass");
    LOAD_DEV_FN(cmdEndRenderPass,           "vkCmdEndRenderPass");
    LOAD_DEV_FN(cmdBindPipeline,            "vkCmdBindPipeline");
    LOAD_DEV_FN(cmdBindDescriptorSets,      "vkCmdBindDescriptorSets");
    LOAD_DEV_FN(cmdPushConstants,           "vkCmdPushConstants");
    LOAD_DEV_FN(cmdDraw,                    "vkCmdDraw");
    LOAD_DEV_FN(cmdBlitImage,               "vkCmdBlitImage");
    LOAD_DEV_FN(createSemaphore,            "vkCreateSemaphore");
    LOAD_DEV_FN(destroySemaphore,           "vkDestroySemaphore");
    LOAD_DEV_FN(createFence,                "vkCreateFence");
    LOAD_DEV_FN(destroyFence,               "vkDestroyFence");
    LOAD_DEV_FN(waitForFences,              "vkWaitForFences");
    LOAD_DEV_FN(resetFences,                "vkResetFences");

    if (ci->queueCreateInfoCount > 0 && ci->pQueueCreateInfos)
        dd.defaultQueueFamily = ci->pQueueCreateInfos[0].queueFamilyIndex;

    if (inst->getPhysicalDeviceMemoryProperties)
        inst->getPhysicalDeviceMemoryProperties(phys, &dd.memProps);

    if (inst->getPhysicalDeviceQueueFamilyProperties) {
        uint32_t qCount = 0;
        inst->getPhysicalDeviceQueueFamilyProperties(phys, &qCount, nullptr);
        dd.queueProps.resize(qCount);
        if (qCount > 0) inst->getPhysicalDeviceQueueFamilyProperties(phys, &qCount, dd.queueProps.data());
    }

    bool aniso = ci->pEnabledFeatures && ci->pEnabledFeatures->samplerAnisotropy;
    for (const VkBaseInStructure* p = reinterpret_cast<const VkBaseInStructure*>(ci->pNext); p; p = p->pNext)
        if (p->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2)
            aniso = aniso || reinterpret_cast<const VkPhysicalDeviceFeatures2*>(p)->features.samplerAnisotropy;
    dd.samplerAnisotropy = aniso;

    if (inst->getPhysicalDeviceProperties) {
        VkPhysicalDeviceProperties props{};
        inst->getPhysicalDeviceProperties(phys, &props);
        dd.maxAnisotropy = props.limits.maxSamplerAnisotropy;
        dd.maxLodBias    = props.limits.maxSamplerLodBias;
        logLine("device: %s  (aniso feature=%s, maxAniso=%.0f, maxLodBias=%.1f)", props.deviceName,
                aniso ? "on" : "off", dd.maxAnisotropy, dd.maxLodBias);
    }
    {
        std::lock_guard<std::mutex> l(gMapMutex);
        gDevices[dispatchKey(*pDevice)] = std::move(dd);
    }
    return VK_SUCCESS;
}

#undef LOAD_DEV_FN

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL hookGetDeviceProcAddr(VkDevice device, const char* name) {
    if (!strcmp(name, "vkGetDeviceProcAddr"))     return reinterpret_cast<PFN_vkVoidFunction>(hookGetDeviceProcAddr);
    if (!strcmp(name, "vkDestroyDevice"))         return reinterpret_cast<PFN_vkVoidFunction>(hookDestroyDevice);
    if (!strcmp(name, "vkGetDeviceQueue"))        return reinterpret_cast<PFN_vkVoidFunction>(hookGetDeviceQueue);
    if (!strcmp(name, "vkGetDeviceQueue2"))       return reinterpret_cast<PFN_vkVoidFunction>(hookGetDeviceQueue2);
    if (!strcmp(name, "vkCreateSampler"))         return reinterpret_cast<PFN_vkVoidFunction>(hookCreateSampler);
    if (!strcmp(name, "vkCreateSwapchainKHR"))    return reinterpret_cast<PFN_vkVoidFunction>(hookCreateSwapchainKHR);
    if (!strcmp(name, "vkDestroySwapchainKHR"))   return reinterpret_cast<PFN_vkVoidFunction>(hookDestroySwapchainKHR);
    if (!strcmp(name, "vkGetSwapchainImagesKHR")) return reinterpret_cast<PFN_vkVoidFunction>(hookGetSwapchainImagesKHR);
    if (!strcmp(name, "vkQueuePresentKHR"))       return reinterpret_cast<PFN_vkVoidFunction>(hookQueuePresentKHR);
    DeviceData* d = findDevice(device);
    return (d && d->gdpa) ? d->gdpa(device, name) : nullptr;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL hookGetInstanceProcAddr(VkInstance instance, const char* name) {
    if (!strcmp(name, "vkGetInstanceProcAddr")) return reinterpret_cast<PFN_vkVoidFunction>(hookGetInstanceProcAddr);
    if (!strcmp(name, "vkCreateInstance"))      return reinterpret_cast<PFN_vkVoidFunction>(hookCreateInstance);
    if (!strcmp(name, "vkDestroyInstance"))     return reinterpret_cast<PFN_vkVoidFunction>(hookDestroyInstance);
    if (!strcmp(name, "vkCreateDevice"))        return reinterpret_cast<PFN_vkVoidFunction>(hookCreateDevice);
    if (!strcmp(name, "vkGetDeviceProcAddr"))   return reinterpret_cast<PFN_vkVoidFunction>(hookGetDeviceProcAddr);
    if (!strcmp(name, "vkDestroyDevice"))       return reinterpret_cast<PFN_vkVoidFunction>(hookDestroyDevice);
    if (!strcmp(name, "vkGetDeviceQueue"))      return reinterpret_cast<PFN_vkVoidFunction>(hookGetDeviceQueue);
    if (!strcmp(name, "vkGetDeviceQueue2"))     return reinterpret_cast<PFN_vkVoidFunction>(hookGetDeviceQueue2);
    if (!strcmp(name, "vkCreateSampler"))       return reinterpret_cast<PFN_vkVoidFunction>(hookCreateSampler);
    if (!strcmp(name, "vkCreateSwapchainKHR"))  return reinterpret_cast<PFN_vkVoidFunction>(hookCreateSwapchainKHR);
    if (!strcmp(name, "vkDestroySwapchainKHR")) return reinterpret_cast<PFN_vkVoidFunction>(hookDestroySwapchainKHR);
    if (!strcmp(name, "vkGetSwapchainImagesKHR")) return reinterpret_cast<PFN_vkVoidFunction>(hookGetSwapchainImagesKHR);
    if (!strcmp(name, "vkQueuePresentKHR"))     return reinterpret_cast<PFN_vkVoidFunction>(hookQueuePresentKHR);

    if (!strcmp(name, "vkEnumeratePhysicalDevices"))
        return reinterpret_cast<PFN_vkVoidFunction>(hookEnumeratePhysicalDevices);

    if (!instance) {
        if (!strcmp(name, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR"))
            return reinterpret_cast<PFN_vkVoidFunction>(hookGetPhysicalDeviceSurfaceCapabilitiesKHR);
        if (!strcmp(name, "vkGetPhysicalDeviceSurfaceCapabilities2KHR"))
            return reinterpret_cast<PFN_vkVoidFunction>(hookGetPhysicalDeviceSurfaceCapabilities2KHR);
        return nullptr;
    }
    InstanceData* i = findInstance(instance);
    if (!i || !i->gipa) return nullptr;

    if (!strcmp(name, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR"))
        return i->getSurfaceCaps
                   ? reinterpret_cast<PFN_vkVoidFunction>(hookGetPhysicalDeviceSurfaceCapabilitiesKHR)
                   : nullptr;
    if (!strcmp(name, "vkGetPhysicalDeviceSurfaceCapabilities2KHR"))
        return i->getSurfaceCaps2
                   ? reinterpret_cast<PFN_vkVoidFunction>(hookGetPhysicalDeviceSurfaceCapabilities2KHR)
                   : nullptr;

    return i->gipa(instance, name);
}

}  // namespace

// ----------------------------------------------------------------------------
// Loader <-> layer negotiation (interface version 2)
// ----------------------------------------------------------------------------
extern "C" SURREAL_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface* iface) {
    if (!iface || iface->sType != LAYER_NEGOTIATE_INTERFACE_STRUCT) return VK_ERROR_INITIALIZATION_FAILED;
    if (iface->loaderLayerInterfaceVersion >= 2) {
        iface->loaderLayerInterfaceVersion = 2;
        iface->pfnGetInstanceProcAddr       = hookGetInstanceProcAddr;
        iface->pfnGetDeviceProcAddr         = hookGetDeviceProcAddr;
        iface->pfnGetPhysicalDeviceProcAddr = nullptr;
    }
    return VK_SUCCESS;
}
