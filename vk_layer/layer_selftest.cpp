// Headless self-test for VK_LAYER_SURREAL_boost: creates a device on the NVIDIA GPU with
// samplerAnisotropy enabled (like a real game) and creates several samplers through the layer.
// Run:  SURREAL_BOOST=1 SURREAL_LOG=1 SURREAL_AF=16 SURREAL_LOD_BIAS=-0.25 ./layer_selftest
#include <vulkan/vulkan.h>
#include <cstdio>
#include <cstring>
#include <vector>

int main() {
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    VkInstance inst;
    if (vkCreateInstance(&ici, nullptr, &inst) != VK_SUCCESS) { puts("FAIL: vkCreateInstance"); return 1; }

    uint32_t n = 0;
    vkEnumeratePhysicalDevices(inst, &n, nullptr);
    std::vector<VkPhysicalDevice> devs(n);
    vkEnumeratePhysicalDevices(inst, &n, devs.data());
    VkPhysicalDevice phys = VK_NULL_HANDLE;
    for (auto d : devs) {
        VkPhysicalDeviceProperties p;
        vkGetPhysicalDeviceProperties(d, &p);
        if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) { phys = d; printf("using %s\n", p.deviceName); break; }
    }
    if (!phys) { puts("FAIL: no discrete GPU"); return 1; }

    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = 0; qci.queueCount = 1; qci.pQueuePriorities = &prio;
    VkPhysicalDeviceFeatures feat{};
    feat.samplerAnisotropy = VK_TRUE;
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount = 1; dci.pQueueCreateInfos = &qci; dci.pEnabledFeatures = &feat;
    VkDevice dev;
    if (vkCreateDevice(phys, &dci, nullptr, &dev) != VK_SUCCESS) { puts("FAIL: vkCreateDevice"); return 1; }

    auto make = [&](const char* label, VkFilter minF, VkSamplerMipmapMode mm, float maxLod, VkBool32 cmp, VkBool32 aniso, float maxAniso) {
        VkSamplerCreateInfo s{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        s.magFilter = VK_FILTER_LINEAR; s.minFilter = minF; s.mipmapMode = mm;
        s.addressModeU = s.addressModeV = s.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        s.maxLod = maxLod; s.compareEnable = cmp; s.anisotropyEnable = aniso; s.maxAnisotropy = maxAniso;
        VkSampler out;
        VkResult r = vkCreateSampler(dev, &s, nullptr, &out);
        printf("%-34s -> %s\n", label, r == VK_SUCCESS ? "created OK" : "FAILED");
        if (r == VK_SUCCESS) vkDestroySampler(dev, out, nullptr);
        return r == VK_SUCCESS ? 0 : 1;
    };
    int bad = 0;
    bad += make("texture sampler (trilinear, no AF)", VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR, 12.0f, VK_FALSE, VK_FALSE, 1.0f);
    bad += make("texture sampler (game wants AF 4)",  VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR, 12.0f, VK_FALSE, VK_TRUE, 4.0f);
    bad += make("post-process (maxLod 0) [untouched]", VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST, 0.0f, VK_FALSE, VK_FALSE, 1.0f);
    bad += make("shadow compare [untouched]",         VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR, 12.0f, VK_TRUE, VK_FALSE, 1.0f);

    vkDestroyDevice(dev, nullptr);
    vkDestroyInstance(inst, nullptr);
    puts(bad ? "SELFTEST FAILED" : "SELFTEST OK (clean create/destroy)");
    return bad;
}

