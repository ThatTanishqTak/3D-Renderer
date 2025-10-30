#include "Startup.h"

#include "Core/Utilities.h"
#include "Window/Window.h"
#include "Renderer/RenderCommand.h"
#include "ECS/Scene.h"

#include <set>
#include <stdexcept>
#include <filesystem>
#include <fstream>
#include <array>

#include <GLFW/glfw3.h>

#ifdef _WIN32
#include <windows.h>
#endif

namespace Trident
{
    Startup* Startup::s_Instance = nullptr;

    namespace
    {
        std::filesystem::path DetermineExecutableDirectory()
        {
#ifdef _WIN32
            std::array<wchar_t, MAX_PATH> l_Buffer{};
            const DWORD l_Length = GetModuleFileNameW(nullptr, l_Buffer.data(), static_cast<DWORD>(l_Buffer.size()));
            if (l_Length > 0)
            {
                return std::filesystem::path(l_Buffer.data()).parent_path();
            }
#endif

            // Non-Windows builds fall back to the current working directory until platform-specific helpers are added.
            return std::filesystem::current_path();
        }
    }

    Startup::Startup(Window& window) : m_Window(window)
    {
        if (s_Instance != nullptr)
        {
            // Guard against accidental double construction which would leave
            // the static accessors pointing at a stale Startup instance.
            TR_CORE_CRITICAL("Startup already exists");
            throw std::runtime_error("Startup singleton already constructed");
        }

        s_Instance = this;

        Initialize();
    }

    Startup::~Startup()
    {
        Shutdown();

        // Release the singleton slot so a future reinitialisation can succeed.
        s_Instance = nullptr;
    }

    void Startup::Initialize()
    {
        TR_CORE_INFO("-------INITIALIZING VULKAN-------");

        CreateInstance();
#ifdef _DEBUG
        SetupDebugMessenger();
#endif
        CreateSurface();
        PickPhysicalDevice();
        CreateLogicalDevice();

        // Locate packaged content (scenes, camera descriptors, etc.) so the runtime can bootstrap without editor tooling.
        DiscoverPackagedContent();

        TR_CORE_INFO("-------VULKAN INITIALIZED-------");
    }

    void Startup::Shutdown()
    {
        TR_CORE_TRACE("Shutting down Vulkan");

        if (m_Surface != VK_NULL_HANDLE)
        {
            vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);

            m_Surface = VK_NULL_HANDLE;
        }

        if (m_Device != VK_NULL_HANDLE)
        {
            vkDestroyDevice(m_Device, nullptr);

            m_Device = VK_NULL_HANDLE;
        }

#ifdef _DEBUG
        if (m_DebugMessenger != VK_NULL_HANDLE)
        {
            auto a_Function = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_Instance, "vkDestroyDebugUtilsMessengerEXT");
            if (a_Function)
            {
                a_Function(m_Instance, m_DebugMessenger, nullptr);
            }

            m_DebugMessenger = VK_NULL_HANDLE;
        }
#endif
        if (m_Instance != VK_NULL_HANDLE)
        {
            vkDestroyInstance(m_Instance, nullptr);

            m_Instance = VK_NULL_HANDLE;
        }

        TR_CORE_TRACE("Vulkan Shutdown Complete");
    }

    void Startup::DiscoverPackagedContent()
    {
        m_PackagedContentDirectory.clear();
        m_PackagedScenePath.clear();
        m_PackagedScene.reset();
        m_PackagedCameraPosition.reset();
        m_PackagedCameraRotation.reset();
        m_HasAppliedPackagedState = false;

        const std::filesystem::path l_BaseDirectory = DetermineExecutableDirectory();
        const std::filesystem::path l_ContentDirectory = l_BaseDirectory / "Content";

        std::error_code l_ContentError{};
        if (!std::filesystem::exists(l_ContentDirectory, l_ContentError) || !std::filesystem::is_directory(l_ContentDirectory, l_ContentError))
        {
            if (l_ContentError)
            {
                TR_CORE_INFO("Exported content directory '{}' unavailable ({}). Runtime will await streamed assets.", l_ContentDirectory.string(), l_ContentError.message());
            }
            else
            {
                TR_CORE_INFO("Exported content directory '{}' not found. Runtime will await streamed assets.", l_ContentDirectory.string());
            }

            return;
        }

        m_PackagedContentDirectory = l_ContentDirectory;

        const auto FindSceneFile = [](const std::filesystem::path& directory) -> std::filesystem::path
            {
                std::error_code l_SearchError{};
                for (const std::filesystem::directory_entry& it_Entry : std::filesystem::directory_iterator(directory, l_SearchError))
                {
                    if (it_Entry.is_regular_file() && it_Entry.path().extension() == ".trident")
                    {
                        return it_Entry.path();
                    }
                }

                if (l_SearchError)
                {
                    TR_CORE_WARN("Failed to enumerate '{}' while searching for packaged scenes: {}", directory.string(), l_SearchError.message());
                }

                return {};
            };

        m_PackagedScenePath = FindSceneFile(l_ContentDirectory);
        if (m_PackagedScenePath.empty())
        {
            m_PackagedScenePath = FindSceneFile(l_ContentDirectory / "Scenes");
        }

        if (!m_PackagedScenePath.empty())
        {
            m_PackagedScene = std::make_unique<Scene>(m_Registry);
            if (m_PackagedScene->Load(m_PackagedScenePath.string()))
            {
                TR_CORE_INFO("Packaged scene '{}' loaded into runtime registry.", m_PackagedScenePath.string());
            }
            else
            {
                TR_CORE_ERROR("Failed to load packaged scene '{}'. Runtime will start empty.", m_PackagedScenePath.string());
                m_PackagedScene.reset();
            }
        }
        else
        {
            TR_CORE_INFO("No packaged scene discovered under '{}'.", l_ContentDirectory.string());
        }

        LoadPackagedCameraTransform();

        // TODO: Extend the discovery logic for macOS/Linux bundle layouts once the exporter targets multiple platforms.
    }

    void Startup::LoadPackagedCameraTransform()
    {
        if (m_PackagedContentDirectory.empty())
        {
            return;
        }

        const std::filesystem::path l_CameraDescriptor = m_PackagedContentDirectory / "runtime_camera.txt";
        std::ifstream l_Stream(l_CameraDescriptor);
        if (!l_Stream.is_open())
        {
            TR_CORE_INFO("Runtime camera descriptor '{}' not found. Using default camera transform.", l_CameraDescriptor.string());
            return;
        }

        std::string l_Label;
        glm::vec3 l_Position{ 0.0f };
        glm::vec3 l_Rotation{ 0.0f };
        bool l_ReadPosition = false;
        bool l_ReadRotation = false;

        while (l_Stream >> l_Label)
        {
            if (l_Label == "Position")
            {
                l_Stream >> l_Position.x >> l_Position.y >> l_Position.z;
                l_ReadPosition = true;
            }
            else if (l_Label == "Rotation")
            {
                l_Stream >> l_Rotation.x >> l_Rotation.y >> l_Rotation.z;
                l_ReadRotation = true;
            }
        }

        if (l_ReadPosition)
        {
            m_PackagedCameraPosition = l_Position;
        }

        if (l_ReadRotation)
        {
            m_PackagedCameraRotation = l_Rotation;
        }

        if (l_ReadPosition || l_ReadRotation)
        {
            TR_CORE_INFO("Runtime camera transform loaded from '{}'.", l_CameraDescriptor.string());
        }
    }

    void Startup::ApplyPackagedRuntimeState()
    {
        if (m_HasAppliedPackagedState)
        {
            return;
        }

        if (m_PackagedScene)
        {
            TR_CORE_INFO("Applying packaged scene '{}' to runtime state.", m_PackagedScenePath.string());
        }

        if (m_PackagedCameraPosition.has_value())
        {
            m_PackagedRuntimeCamera.SetPosition(*m_PackagedCameraPosition);
        }

        if (m_PackagedCameraRotation.has_value())
        {
            m_PackagedRuntimeCamera.SetRotation(*m_PackagedCameraRotation);
        }

        m_PackagedRuntimeCamera.Invalidate();
        RenderCommand::SetRuntimeCamera(&m_PackagedRuntimeCamera);
        RenderCommand::SetRuntimeCameraReady(true);

        // Highlight future work so the build pipeline can avoid redundant msbuild invocations when nothing changed.
        TR_CORE_INFO("Runtime camera primed from packaged data. Future work: cache build artefacts between exports for faster iteration.");

        m_HasAppliedPackagedState = true;
    }

    //------------------------------------------------------------------------------------------------------------------------------------------------------//

    void Startup::CreateInstance()
    {
        TR_CORE_TRACE("Creating Vulkan Instance");

        // VkApplicationInfo
        VkApplicationInfo l_AppInfo{};

        l_AppInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        l_AppInfo.pApplicationName = "Trident-Application";
        l_AppInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        l_AppInfo.pEngineName = "Trident";
        l_AppInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        l_AppInfo.apiVersion = VK_API_VERSION_1_4;

        std::vector<const char*> l_Layers;
#ifdef _DEBUG
        l_Layers.push_back("VK_LAYER_KHRONOS_validation");
#endif

#ifdef _DEBUG
        if (!CheckValidationLayerSupport())
        {
            TR_CORE_CRITICAL("Validation layers requested, but not available!");
        }
#endif

        // VkInstanceCreateInfo
        VkInstanceCreateInfo l_CreateInfo{};
        auto l_Extensions = GetRequiredExtensions();

#ifdef _DEBUG
        l_Extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

        l_CreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        l_CreateInfo.pApplicationInfo = &l_AppInfo;
        l_CreateInfo.enabledExtensionCount = static_cast<uint32_t>(l_Extensions.size());
        l_CreateInfo.ppEnabledExtensionNames = l_Extensions.data();
        l_CreateInfo.enabledLayerCount = static_cast<uint32_t>(l_Layers.size());
        l_CreateInfo.ppEnabledLayerNames = l_Layers.data();

        if (vkCreateInstance(&l_CreateInfo, nullptr, &m_Instance) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create Vulkan instance");
        }

        TR_CORE_TRACE("Vulkan Instance Created");
    }

#ifdef _DEBUG
    static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT type,
        const VkDebugUtilsMessengerCallbackDataEXT* data, void*)
    {
        TR_CORE_CRITICAL("Validation: {}", data->pMessage);

        return VK_FALSE;
    }

    void Startup::SetupDebugMessenger()
    {
        TR_CORE_TRACE("Setting Up Debug Messenger");

        auto a_Function = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_Instance, "vkCreateDebugUtilsMessengerEXT");

        if (!a_Function)
        {
            TR_CORE_CRITICAL("vkCreateDebugUtilsMessengerEXT not found");

            return;
        }

        VkDebugUtilsMessengerCreateInfoEXT l_DebugInfo{};
        l_DebugInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        l_DebugInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        l_DebugInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        l_DebugInfo.pfnUserCallback = DebugCallback;

        if (a_Function(m_Instance, &l_DebugInfo, nullptr, &m_DebugMessenger) != VK_SUCCESS)
        {
            TR_CORE_ERROR("Failed to create debug messenger");

            return;
        }

        TR_CORE_TRACE("Debug Messenger Setup");
    }
#endif

    void Startup::CreateSurface()
    {
        TR_CORE_TRACE("Creating GLFW Window Surface");

        if (glfwCreateWindowSurface(m_Instance, m_Window.GetNativeWindow(), nullptr, &m_Surface) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create window surface");
        }

        TR_CORE_TRACE("Window Surface Created");
    }

    void Startup::PickPhysicalDevice()
    {
        TR_CORE_TRACE("Selecting Physical Device (GPU)");

        uint32_t l_DeviceCount = 0;
        vkEnumeratePhysicalDevices(m_Instance, &l_DeviceCount, nullptr);

        if (l_DeviceCount == 0)
        {
            TR_CORE_CRITICAL("No Vulkan-capable GPUs found");
        }

        std::vector<VkPhysicalDevice> l_Devices(l_DeviceCount);
        vkEnumeratePhysicalDevices(m_Instance, &l_DeviceCount, l_Devices.data());

        VkPhysicalDevice l_BestDevice = VK_NULL_HANDLE;

        for (const auto& it_Device : l_Devices)
        {
            if (!IsDeviceSuitable(it_Device))
            {
                continue;
            }

            VkPhysicalDeviceProperties l_Properites;
            vkGetPhysicalDeviceProperties(it_Device, &l_Properites);

            if (l_Properites.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
            {
                m_PhysicalDevice = it_Device;
                m_QueueFamilyIndices = FindQueueFamilies(it_Device);

                break;
            }

            // fallback (if no discrete GPU is found)
            if (l_BestDevice == VK_NULL_HANDLE)
            {
                l_BestDevice = it_Device;
                m_QueueFamilyIndices = FindQueueFamilies(it_Device);
            }
        }

        if (m_PhysicalDevice == VK_NULL_HANDLE && l_BestDevice != VK_NULL_HANDLE)
        {
            m_PhysicalDevice = l_BestDevice;
        }

        if (m_PhysicalDevice == VK_NULL_HANDLE)
        {
            TR_CORE_CRITICAL("Failed to find a suitable GPU");
        }

        VkPhysicalDeviceProperties l_Properties{};
        vkGetPhysicalDeviceProperties(m_PhysicalDevice, &l_Properties);

        TR_CORE_TRACE("Selected GPU: {}", l_Properties.deviceName);
    }

    void Startup::CreateLogicalDevice()
    {
        TR_CORE_TRACE("Creating Logical Device And Queues");

        auto a_QueueFamily = m_QueueFamilyIndices;
        if (!a_QueueFamily.IsComplete())
        {
            TR_CORE_CRITICAL("Queue family indices not set");

            return;
        }
        std::set<uint32_t> l_UniqueFamilies = { a_QueueFamily.GraphicsFamily.value(), a_QueueFamily.PresentFamily.value() };

        float l_Priority = 1.0f;
        std::vector<VkDeviceQueueCreateInfo> l_QueueCreateInfo;

        for (auto it_Family : l_UniqueFamilies)
        {
            VkDeviceQueueCreateInfo l_DeviceQueueInfo{};

            l_DeviceQueueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            l_DeviceQueueInfo.queueFamilyIndex = it_Family;
            l_DeviceQueueInfo.queueCount = 1;
            l_DeviceQueueInfo.pQueuePriorities = &l_Priority;
            l_QueueCreateInfo.push_back(l_DeviceQueueInfo);
        }

        VkPhysicalDeviceFeatures l_Features{};
        // Query the device for Vulkan 1.2 descriptor indexing support so we can safely enable it.
        VkPhysicalDeviceVulkan12Features l_AvailableVulkan12Features{};
        l_AvailableVulkan12Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;

        VkPhysicalDeviceFeatures2 l_Features2{};
        l_Features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        l_Features2.pNext = &l_AvailableVulkan12Features;
        vkGetPhysicalDeviceFeatures2(m_PhysicalDevice, &l_Features2);

        if (l_AvailableVulkan12Features.runtimeDescriptorArray != VK_TRUE ||
            l_AvailableVulkan12Features.shaderSampledImageArrayNonUniformIndexing != VK_TRUE)
        {
            TR_CORE_CRITICAL("Selected GPU does not support required descriptor indexing features");
            return;
        }

        VkPhysicalDeviceVulkan12Features l_EnabledVulkan12Features{};
        l_EnabledVulkan12Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        l_EnabledVulkan12Features.runtimeDescriptorArray = VK_TRUE;
        l_EnabledVulkan12Features.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;

        VkDeviceCreateInfo l_DeviceCreateInfo{};

        l_DeviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        l_DeviceCreateInfo.queueCreateInfoCount = static_cast<uint32_t>(l_QueueCreateInfo.size());
        l_DeviceCreateInfo.pQueueCreateInfos = l_QueueCreateInfo.data();
        l_DeviceCreateInfo.pEnabledFeatures = &l_Features;
        l_DeviceCreateInfo.pNext = &l_EnabledVulkan12Features;
        const char* l_Extensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
        l_DeviceCreateInfo.enabledExtensionCount = 1;
        l_DeviceCreateInfo.ppEnabledExtensionNames = l_Extensions;

        if (vkCreateDevice(m_PhysicalDevice, &l_DeviceCreateInfo, nullptr, &m_Device) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create logical Device");
        }

        vkGetDeviceQueue(m_Device, *a_QueueFamily.GraphicsFamily, 0, &m_GraphicsQueue);
        vkGetDeviceQueue(m_Device, *a_QueueFamily.PresentFamily, 0, &m_PresentQueue);

        TR_CORE_TRACE("Logical Device And Queues ready (GFX = {}, Present = {})", *a_QueueFamily.GraphicsFamily, *a_QueueFamily.PresentFamily);
    }

    //----------------------------------------------------------------------------------------------------------------------------------------------------------//

    std::vector<const char*> Startup::GetRequiredExtensions()
    {
        uint32_t l_ExtensionCount = 0;
        const char** l_ExtensionName = glfwGetRequiredInstanceExtensions(&l_ExtensionCount);

        std::vector<const char*> l_Extension(l_ExtensionName, l_ExtensionName + l_ExtensionCount);

        return l_Extension;
    }

    QueueFamilyIndices Startup::FindQueueFamilies(VkPhysicalDevice device)
    {
        QueueFamilyIndices l_Indices;
        uint32_t l_Count = 0;

        vkGetPhysicalDeviceQueueFamilyProperties(device, &l_Count, nullptr);
        std::vector<VkQueueFamilyProperties> l_Families(l_Count);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &l_Count, l_Families.data());

        for (uint32_t i = 0; i < l_Count; ++i)
        {
            if (l_Families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            {
                l_Indices.GraphicsFamily = i;
            }

            VkBool32 l_IsPresent = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, GetSurface(), &l_IsPresent);
            if (l_IsPresent)
            {
                l_Indices.PresentFamily = i;
            }

            if (l_Indices.IsComplete())
            {
                break;
            }
        }

        return l_Indices;
    }

    bool Startup::CheckValidationLayerSupport()
    {
        TR_CORE_TRACE("Checking Validation Layer Support");

        uint32_t l_LayerCount = 0;
        vkEnumerateInstanceLayerProperties(&l_LayerCount, nullptr);

        std::vector<VkLayerProperties> l_AvailableLayers(l_LayerCount);
        vkEnumerateInstanceLayerProperties(&l_LayerCount, l_AvailableLayers.data());

        const std::vector<const char*> l_ValidationLayers = { "VK_LAYER_KHRONOS_validation" };

        for (const char* l_LayerName : l_ValidationLayers)
        {
            bool l_LayerFound = false;

            for (const auto& layerProps : l_AvailableLayers)
            {
                if (strcmp(l_LayerName, layerProps.layerName) == 0)
                {
                    l_LayerFound = true;

                    break;
                }
            }

            if (!l_LayerFound)
            {
                TR_CORE_TRACE("Validation layer {} not present", l_LayerName);

                return false;
            }
        }

        TR_CORE_TRACE("All requested validation layers are available");

        return true;
    }

    bool Startup::IsDeviceSuitable(VkPhysicalDevice device)
    {
        VkPhysicalDeviceProperties l_Properties{};
        vkGetPhysicalDeviceProperties(device, &l_Properties);

        auto a_Indices = FindQueueFamilies(device);

        // Check required device extensions
        uint32_t l_ExtensionCount = 0;
        vkEnumerateDeviceExtensionProperties(device, nullptr, &l_ExtensionCount, nullptr);
        std::vector<VkExtensionProperties> l_AvailableExtensions(l_ExtensionCount);
        vkEnumerateDeviceExtensionProperties(device, nullptr, &l_ExtensionCount, l_AvailableExtensions.data());

        std::set<std::string> l_RequiredExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
        for (const auto& a_Extensions : l_AvailableExtensions)
        {
            l_RequiredExtensions.erase(a_Extensions.extensionName);
        }

        bool l_ExtensionsSupported = l_RequiredExtensions.empty();

        bool l_SwapchainAdequate = false;
        if (l_ExtensionsSupported)
        {
            uint32_t l_FormatCount = 0;
            vkGetPhysicalDeviceSurfaceFormatsKHR(device, GetSurface(), &l_FormatCount, nullptr);

            uint32_t l_PresentModeCount = 0;
            vkGetPhysicalDeviceSurfacePresentModesKHR(device, GetSurface(), &l_PresentModeCount, nullptr);

            l_SwapchainAdequate = l_FormatCount > 0 && l_PresentModeCount > 0;
        }

        return a_Indices.IsComplete() && l_ExtensionsSupported && l_SwapchainAdequate;
    }
}