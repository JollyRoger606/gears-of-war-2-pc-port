// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Santiago Quiroz Upegui and the gow2-recomp contributors
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_vulkan.h>

#include <vulkan/vulkan.h>

#include <android/log.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstdlib>
#include <string_view>
#include <vector>

namespace {

constexpr const char* kLogTag = "Gow2Shell";
constexpr std::uint8_t kClearR = 0x8C;
constexpr std::uint8_t kClearG = 0x14;
constexpr std::uint8_t kClearB = 0x14;
constexpr std::uint8_t kClearA = 0xFF;
constexpr std::uint32_t kBackgroundDelayMs = 50;
constexpr std::uint32_t kNoVsyncDelayMs = 16;

void logInfo(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void logError(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

void logInfo(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    __android_log_vprint(ANDROID_LOG_INFO, kLogTag, fmt, args);
    va_end(args);
}

void logError(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    __android_log_vprint(ANDROID_LOG_ERROR, kLogTag, fmt, args);
    va_end(args);
}

const char* envOrUnset(const char* name)
{
    const char* value = std::getenv(name);
    return value != nullptr ? value : "(unset)";
}

void logEnvironment(int argc, char* argv[])
{
    logInfo("placeholder started (argc=%d)", argc);
    for (int i = 0; i < argc; ++i) {
        logInfo("argv[%d]=%s", i, argv[i] != nullptr ? argv[i] : "");
    }
    logInfo("HOME=%s", envOrUnset("HOME"));
    logInfo("XDG_DATA_HOME=%s", envOrUnset("XDG_DATA_HOME"));
    logInfo("TMPDIR=%s", envOrUnset("TMPDIR"));
    const long pageSize = sysconf(_SC_PAGESIZE);
    const long onlineCpus = sysconf(_SC_NPROCESSORS_ONLN);
    logInfo("page size=%ld online cpus=%ld", pageSize, onlineCpus);
}

void logVersion()
{
    const int version = SDL_GetVersion();
    logInfo("SDL %d.%d.%d (%s)", SDL_VERSIONNUM_MAJOR(version),
            SDL_VERSIONNUM_MINOR(version), SDL_VERSIONNUM_MICRO(version),
            SDL_GetRevision());
}

const char* deviceTypeName(VkPhysicalDeviceType type)
{
    switch (type) {
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
        return "integrated";
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
        return "discrete";
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
        return "virtual";
    case VK_PHYSICAL_DEVICE_TYPE_CPU:
        return "cpu";
    default:
        return "other";
    }
}

VkInstance createReportInstance(std::uint32_t apiVersion, std::uint32_t extCount,
                                char const* const* extNames)
{
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "gow2-recomp shell";
    appInfo.apiVersion = apiVersion;
    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = extCount;
    createInfo.ppEnabledExtensionNames = extNames;
    VkInstance instance = VK_NULL_HANDLE;
    const VkResult res = vkCreateInstance(&createInfo, nullptr, &instance);
    if (res != VK_SUCCESS) {
        logError("vkCreateInstance failed: VkResult=%d", static_cast<int>(res));
        return VK_NULL_HANDLE;
    }
    return instance;
}

void logDeviceIdentity(std::uint32_t index, const VkPhysicalDeviceProperties& props)
{
    logInfo("GPU %u: %s type=%s vendor=0x%04x device=0x%04x api=%u.%u.%u driverVersion=0x%08x",
            static_cast<unsigned>(index), props.deviceName,
            deviceTypeName(props.deviceType),
            static_cast<unsigned>(props.vendorID), static_cast<unsigned>(props.deviceID),
            static_cast<unsigned>(VK_API_VERSION_MAJOR(props.apiVersion)),
            static_cast<unsigned>(VK_API_VERSION_MINOR(props.apiVersion)),
            static_cast<unsigned>(VK_API_VERSION_PATCH(props.apiVersion)),
            static_cast<unsigned>(props.driverVersion));
}

void logDriver(std::uint32_t index, VkPhysicalDevice device, std::uint32_t deviceApi,
               std::uint32_t instanceApi)
{
    if (deviceApi < VK_API_VERSION_1_2 || instanceApi < VK_API_VERSION_1_1) {
        return;
    }
    VkPhysicalDeviceDriverProperties driverProps{};
    driverProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
    VkPhysicalDeviceProperties2 props2{};
    props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    props2.pNext = &driverProps;
    vkGetPhysicalDeviceProperties2(device, &props2);
    logInfo("GPU %u driver: %s | %s (driverID=%d)", static_cast<unsigned>(index),
            driverProps.driverName, driverProps.driverInfo,
            static_cast<int>(driverProps.driverID));
}

struct FeatureEntry {
    std::string_view name;
    VkBool32 VkPhysicalDeviceFeatures::* member;
};

constexpr std::array<FeatureEntry, 22> kFeatureTable = {{
    {"vertexPipelineStoresAndAtomics", &VkPhysicalDeviceFeatures::vertexPipelineStoresAndAtomics},
    {"fragmentStoresAndAtomics", &VkPhysicalDeviceFeatures::fragmentStoresAndAtomics},
    {"geometryShader", &VkPhysicalDeviceFeatures::geometryShader},
    {"tessellationShader", &VkPhysicalDeviceFeatures::tessellationShader},
    {"textureCompressionBC", &VkPhysicalDeviceFeatures::textureCompressionBC},
    {"textureCompressionETC2", &VkPhysicalDeviceFeatures::textureCompressionETC2},
    {"textureCompressionASTC_LDR", &VkPhysicalDeviceFeatures::textureCompressionASTC_LDR},
    {"shaderStorageImageWriteWithoutFormat",
     &VkPhysicalDeviceFeatures::shaderStorageImageWriteWithoutFormat},
    {"shaderStorageImageReadWithoutFormat",
     &VkPhysicalDeviceFeatures::shaderStorageImageReadWithoutFormat},
    {"independentBlend", &VkPhysicalDeviceFeatures::independentBlend},
    {"dualSrcBlend", &VkPhysicalDeviceFeatures::dualSrcBlend},
    {"logicOp", &VkPhysicalDeviceFeatures::logicOp},
    {"depthClamp", &VkPhysicalDeviceFeatures::depthClamp},
    {"depthBiasClamp", &VkPhysicalDeviceFeatures::depthBiasClamp},
    {"fillModeNonSolid", &VkPhysicalDeviceFeatures::fillModeNonSolid},
    {"fullDrawIndexUint32", &VkPhysicalDeviceFeatures::fullDrawIndexUint32},
    {"imageCubeArray", &VkPhysicalDeviceFeatures::imageCubeArray},
    {"samplerAnisotropy", &VkPhysicalDeviceFeatures::samplerAnisotropy},
    {"shaderClipDistance", &VkPhysicalDeviceFeatures::shaderClipDistance},
    {"shaderCullDistance", &VkPhysicalDeviceFeatures::shaderCullDistance},
    {"multiViewport", &VkPhysicalDeviceFeatures::multiViewport},
    {"occlusionQueryPrecise", &VkPhysicalDeviceFeatures::occlusionQueryPrecise},
}};

void logFeatures(VkPhysicalDevice device)
{
    VkPhysicalDeviceFeatures features{};
    vkGetPhysicalDeviceFeatures(device, &features);
    for (const FeatureEntry& entry : kFeatureTable) {
        const bool enabled = (features.*(entry.member)) != VK_FALSE;
        logInfo("feature %.*s=%s", static_cast<int>(entry.name.size()), entry.name.data(),
                enabled ? "yes" : "no");
    }
}

constexpr std::array<std::string_view, 11> kWantedDeviceExtensions = {{
    "VK_KHR_swapchain",
    "VK_EXT_fragment_shader_interlock",
    "VK_KHR_dynamic_rendering",
    "VK_EXT_shader_stencil_export",
    "VK_EXT_custom_border_color",
    "VK_KHR_timeline_semaphore",
    "VK_KHR_buffer_device_address",
    "VK_EXT_memory_budget",
    "VK_KHR_driver_properties",
    "VK_EXT_shader_demote_to_helper_invocation",
    "VK_KHR_shader_float16_int8",
}};

std::vector<VkExtensionProperties> enumerateDeviceExtensions(VkPhysicalDevice device)
{
    std::uint32_t count = 0;
    VkResult res = vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
    if (res != VK_SUCCESS) {
        logError("vkEnumerateDeviceExtensionProperties failed: VkResult=%d",
                 static_cast<int>(res));
        return {};
    }
    std::vector<VkExtensionProperties> available(count);
    if (count > 0) {
        res = vkEnumerateDeviceExtensionProperties(device, nullptr, &count, available.data());
        if (res != VK_SUCCESS) {
            logError("vkEnumerateDeviceExtensionProperties failed: VkResult=%d",
                     static_cast<int>(res));
            return {};
        }
        available.resize(count);
    }
    return available;
}

bool hasExtension(const std::vector<VkExtensionProperties>& available, std::string_view wanted)
{
    return std::any_of(available.begin(), available.end(),
                       [wanted](const VkExtensionProperties& have) {
                           return std::string_view(have.extensionName) == wanted;
                       });
}

void logExtensions(std::uint32_t index, VkPhysicalDevice device)
{
    const std::vector<VkExtensionProperties> available = enumerateDeviceExtensions(device);
    logInfo("GPU %u: %u device extensions", static_cast<unsigned>(index),
            static_cast<unsigned>(available.size()));
    for (const std::string_view wanted : kWantedDeviceExtensions) {
        logInfo("extension %.*s=%s", static_cast<int>(wanted.size()), wanted.data(),
                hasExtension(available, wanted) ? "present" : "absent");
    }
}

void logHeaps(VkPhysicalDevice device)
{
    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(device, &memProps);
    for (std::uint32_t k = 0; k < memProps.memoryHeapCount; ++k) {
        const unsigned long long mib =
            static_cast<unsigned long long>(memProps.memoryHeaps[k].size / (1024ULL * 1024ULL));
        const bool local =
            (memProps.memoryHeaps[k].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0;
        logInfo("heap %u: %llu MiB%s", static_cast<unsigned>(k), mib,
                local ? " device-local" : "");
    }
}

void logLimits(const VkPhysicalDeviceProperties& props)
{
    logInfo("limits maxImageDimension2D=%u maxStorageBufferRange=%u maxComputeSharedMemorySize=%u maxBoundDescriptorSets=%u",
            static_cast<unsigned>(props.limits.maxImageDimension2D),
            static_cast<unsigned>(props.limits.maxStorageBufferRange),
            static_cast<unsigned>(props.limits.maxComputeSharedMemorySize),
            static_cast<unsigned>(props.limits.maxBoundDescriptorSets));
}

void logQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface)
{
    std::uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    if (count > 0) {
        vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());
    }
    for (std::uint32_t j = 0; j < static_cast<std::uint32_t>(families.size()); ++j) {
        VkBool32 supported = VK_FALSE;
        const VkResult res =
            vkGetPhysicalDeviceSurfaceSupportKHR(device, j, surface, &supported);
        if (res != VK_SUCCESS) {
            logError("vkGetPhysicalDeviceSurfaceSupportKHR failed: VkResult=%d",
                     static_cast<int>(res));
            supported = VK_FALSE;
        }
        logInfo("queue family %u: flags=0x%x count=%u present=%s", static_cast<unsigned>(j),
                static_cast<unsigned>(families[j].queueFlags),
                static_cast<unsigned>(families[j].queueCount),
                supported != VK_FALSE ? "yes" : "no");
    }
}

std::uint32_t chooseInstanceVersion()
{
    std::uint32_t enumerated = 0;
    const VkResult res = vkEnumerateInstanceVersion(&enumerated);
    std::uint32_t loaderVersion = VK_API_VERSION_1_0;
    if (res != VK_SUCCESS) {
        logError("vkEnumerateInstanceVersion failed: VkResult=%d", static_cast<int>(res));
    } else if (enumerated < VK_API_VERSION_1_3) {
        loaderVersion = enumerated;
    } else {
        loaderVersion = VK_API_VERSION_1_3;
    }
    logInfo("Vulkan loader version %u.%u.%u",
            static_cast<unsigned>(VK_API_VERSION_MAJOR(loaderVersion)),
            static_cast<unsigned>(VK_API_VERSION_MINOR(loaderVersion)),
            static_cast<unsigned>(VK_API_VERSION_PATCH(loaderVersion)));
    return loaderVersion;
}

VkSurfaceKHR createWindowSurface(SDL_Window* window, VkInstance instance)
{
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (!SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface)) {
        logError("SDL_Vulkan_CreateSurface failed: %s", SDL_GetError());
        return VK_NULL_HANDLE;
    }
    return surface;
}

std::vector<VkPhysicalDevice> enumeratePhysicalDevices(VkInstance instance)
{
    std::uint32_t deviceCount = 0;
    VkResult res = vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (res != VK_SUCCESS) {
        logError("vkEnumeratePhysicalDevices failed: VkResult=%d", static_cast<int>(res));
        return {};
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    if (deviceCount > 0) {
        res = vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());
        if (res != VK_SUCCESS) {
            logError("vkEnumeratePhysicalDevices failed: VkResult=%d",
                     static_cast<int>(res));
            return {};
        }
        devices.resize(deviceCount);
    }
    return devices;
}

void logDevice(std::uint32_t index, VkPhysicalDevice device, std::uint32_t instanceApi,
               VkSurfaceKHR surface)
{
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(device, &props);
    logDeviceIdentity(index, props);
    logDriver(index, device, props.apiVersion, instanceApi);
    logFeatures(device);
    logExtensions(index, device);
    logHeaps(device);
    logLimits(props);
    if (surface != VK_NULL_HANDLE) {
        logQueueFamilies(device, surface);
    }
}

void logGpuFound(const std::vector<VkPhysicalDevice>& devices)
{
    if (devices.empty()) {
        logError("no Vulkan physical device");
        return;
    }
    VkPhysicalDeviceProperties props0{};
    vkGetPhysicalDeviceProperties(devices[0], &props0);
    const bool isAdreno =
        std::string_view(props0.deviceName).find("Adreno") != std::string_view::npos;
    if (isAdreno) {
        logInfo("GPU found: %s (Adreno)", props0.deviceName);
    } else {
        logInfo("GPU found: %s", props0.deviceName);
    }
}

void reportVulkan(SDL_Window* window)
{
    const std::uint32_t loaderVersion = chooseInstanceVersion();
    std::uint32_t extCount = 0;
    char const* const* extNames = SDL_Vulkan_GetInstanceExtensions(&extCount);
    if (extNames == nullptr) {
        extCount = 0;
    }
    VkInstance instance = createReportInstance(loaderVersion, extCount, extNames);
    if (instance == VK_NULL_HANDLE) {
        return;
    }
    VkSurfaceKHR surface = createWindowSurface(window, instance);
    const std::vector<VkPhysicalDevice> devices = enumeratePhysicalDevices(instance);
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(devices.size()); ++i) {
        logDevice(i, devices[i], loaderVersion, surface);
    }
    logGpuFound(devices);
    if (surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance, surface, nullptr);
    }
    vkDestroyInstance(instance, nullptr);
}

SDL_Renderer* createRenderer(SDL_Window* window, bool& vsyncAvailable)
{
    vsyncAvailable = true;
    SDL_Renderer* renderer = SDL_CreateRenderer(window, "vulkan");
    if (renderer == nullptr) {
        logError("vulkan renderer unavailable: %s, falling back", SDL_GetError());
        renderer = SDL_CreateRenderer(window, nullptr);
        if (renderer == nullptr) {
            return nullptr;
        }
    }
    logInfo("renderer: %s", SDL_GetRendererName(renderer));
    if (!SDL_SetRenderVSync(renderer, 1)) {
        logError("vsync unavailable: %s", SDL_GetError());
        vsyncAvailable = false;
    }
    return renderer;
}

void handleEvent(const SDL_Event& event, bool& paused, bool& quit)
{
    switch (event.type) {
    case SDL_EVENT_QUIT:
    case SDL_EVENT_TERMINATING:
        quit = true;
        break;
    case SDL_EVENT_WILL_ENTER_BACKGROUND:
        paused = true;
        logInfo("entering background");
        break;
    case SDL_EVENT_DID_ENTER_FOREGROUND:
        paused = false;
        logInfo("back in foreground");
        break;
    case SDL_EVENT_GAMEPAD_ADDED: {
        SDL_Gamepad* pad = SDL_OpenGamepad(event.gdevice.which);
        const char* name = (pad != nullptr) ? SDL_GetGamepadName(pad) : nullptr;
        if (name == nullptr || name[0] == '\0') {
            name = "(unknown)";
        }
        logInfo("gamepad added: %s", name);
        break;
    }
    case SDL_EVENT_GAMEPAD_REMOVED: {
        SDL_Gamepad* pad = SDL_GetGamepadFromID(event.gdevice.which);
        if (pad != nullptr) {
            SDL_CloseGamepad(pad);
        }
        logInfo("gamepad removed");
        break;
    }
    default:
        break;
    }
}

bool drawFrame(SDL_Renderer* renderer)
{
    if (!SDL_SetRenderDrawColor(renderer, kClearR, kClearG, kClearB, kClearA)) {
        return false;
    }
    if (!SDL_RenderClear(renderer)) {
        return false;
    }
    if (!SDL_RenderPresent(renderer)) {
        return false;
    }
    return true;
}

void runLoop(SDL_Renderer* renderer, bool vsyncAvailable)
{
    bool quit = false;
    bool paused = false;
    bool firstFrameDone = false;
    bool frameFailed = false;
    while (!quit) {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            handleEvent(event, paused, quit);
        }
        if (quit) {
            break;
        }
        if (paused) {
            SDL_Delay(kBackgroundDelayMs);
            continue;
        }
        if (drawFrame(renderer)) {
            frameFailed = false;
            if (!firstFrameDone) {
                firstFrameDone = true;
                logInfo("first frame presented");
            }
        } else if (!frameFailed) {
            frameFailed = true;
            logError("frame failed: %s", SDL_GetError());
        }
        if (!vsyncAvailable) {
            SDL_Delay(kNoVsyncDelayMs);
        }
    }
}

}  // namespace

int main(int argc, char* argv[])
{
    logEnvironment(argc, argv);
    if (!SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight")) {
        logError("SDL_SetHint failed: %s", SDL_GetError());
    }
    if (!SDL_Init(static_cast<SDL_InitFlags>(SDL_INIT_VIDEO | SDL_INIT_EVENTS |
                                              SDL_INIT_GAMEPAD))) {
        logError("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    logVersion();
    SDL_Window* window = SDL_CreateWindow(
        "gow2-recomp shell", 0, 0,
        static_cast<SDL_WindowFlags>(SDL_WINDOW_VULKAN | SDL_WINDOW_FULLSCREEN));
    if (window == nullptr) {
        logError("SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    reportVulkan(window);
    bool vsyncAvailable = true;
    SDL_Renderer* renderer = createRenderer(window, vsyncAvailable);
    if (renderer == nullptr) {
        logError("SDL_CreateRenderer failed: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    runLoop(renderer, vsyncAvailable);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    logInfo("placeholder exiting");
    return 0;
}
