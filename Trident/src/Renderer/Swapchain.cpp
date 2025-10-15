#include "Renderer/Swapchain.h"

#include "Application/Startup.h"
#include "Core/Utilities.h"

#include <algorithm>
#include <limits>

namespace Trident
{
    void Swapchain::Init()
    {
        CreateSwapchain();
        CreateImageViews();
    }

    void Swapchain::Cleanup()
    {
        for (VkImageView it_View : m_ImageViews)
        {
            if (it_View != VK_NULL_HANDLE)
            {
                vkDestroyImageView(Startup::GetDevice(), it_View, nullptr);
            }
        }

        m_ImageViews.clear();

        if (m_Swapchain != VK_NULL_HANDLE)
        {
            vkDestroySwapchainKHR(Startup::GetDevice(), m_Swapchain, nullptr);

            m_Swapchain = VK_NULL_HANDLE;
        }

        m_Images.clear();
    }

    void Swapchain::CreateSwapchain()
    {
        TR_CORE_TRACE("Creating Swapchain");

        auto l_Details = QuerySwapchainSupport(Startup::GetPhysicalDevice(), Startup::GetSurface());

        VkSurfaceFormatKHR l_SurfaceFormat = ChooseSwapSurfaceFormat(l_Details.Formats);
        VkPresentModeKHR l_PresentMode = ChooseSwapPresentMode(l_Details.PresentModes);
        VkExtent2D l_Extent = ChooseSwapExtent(l_Details.Capabilities);

        uint32_t l_ImageCount = l_Details.Capabilities.minImageCount + 1;
        if (l_Details.Capabilities.maxImageCount > 0 && l_ImageCount > l_Details.Capabilities.maxImageCount)
        {
            l_ImageCount = l_Details.Capabilities.maxImageCount;
        }

        VkSwapchainCreateInfoKHR l_CreateInfo{};
        l_CreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        l_CreateInfo.surface = Startup::GetSurface();
        l_CreateInfo.minImageCount = l_ImageCount;
        l_CreateInfo.imageFormat = l_SurfaceFormat.format;
        l_CreateInfo.imageColorSpace = l_SurfaceFormat.colorSpace;
        l_CreateInfo.imageExtent = l_Extent;
        l_CreateInfo.imageArrayLayers = 1;
        // Ensure swapchain images can also be used as transfer destinations for layout transitions in Renderer.cpp
        l_CreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

        auto a_Indices = Startup::GetQueueFamilyIndices();
        uint32_t l_QueueFamilyIndices[] = { a_Indices.GraphicsFamily.value(), a_Indices.PresentFamily.value() };

        if (a_Indices.GraphicsFamily != a_Indices.PresentFamily)
        {
            l_CreateInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            l_CreateInfo.queueFamilyIndexCount = 2;
            l_CreateInfo.pQueueFamilyIndices = l_QueueFamilyIndices;
        }

        else
        {
            l_CreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            l_CreateInfo.queueFamilyIndexCount = 0;
            l_CreateInfo.pQueueFamilyIndices = nullptr;
        }

        l_CreateInfo.preTransform = l_Details.Capabilities.currentTransform;
        l_CreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        l_CreateInfo.presentMode = l_PresentMode;
        l_CreateInfo.clipped = VK_TRUE;
        l_CreateInfo.oldSwapchain = VK_NULL_HANDLE;

        if (vkCreateSwapchainKHR(Startup::GetDevice(), &l_CreateInfo, nullptr, &m_Swapchain) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create swap chain");
        }

        vkGetSwapchainImagesKHR(Startup::GetDevice(), m_Swapchain, &l_ImageCount, nullptr);
        m_Images.resize(l_ImageCount);
        vkGetSwapchainImagesKHR(Startup::GetDevice(), m_Swapchain, &l_ImageCount, m_Images.data());

        m_ImageFormat = l_SurfaceFormat.format;
        m_Extent = l_Extent;

        TR_CORE_TRACE("Swapchain Created: {} Images, Format {}, Extent {}x{}", l_ImageCount, (int)l_SurfaceFormat.format, l_Extent.width, l_Extent.height);
    }

    void Swapchain::CreateImageViews()
    {
        TR_CORE_TRACE("Creating Image Views");

        m_ImageViews.resize(m_Images.size());

        for (size_t i = 0; i < m_Images.size(); ++i)
        {
            VkImageViewCreateInfo l_ViewInfo{};
            l_ViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            l_ViewInfo.image = m_Images[i];
            l_ViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            l_ViewInfo.format = m_ImageFormat;
            l_ViewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            l_ViewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            l_ViewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            l_ViewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
            l_ViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            l_ViewInfo.subresourceRange.baseMipLevel = 0;
            l_ViewInfo.subresourceRange.levelCount = 1;
            l_ViewInfo.subresourceRange.baseArrayLayer = 0;
            l_ViewInfo.subresourceRange.layerCount = 1;

            if (vkCreateImageView(Startup::GetDevice(), &l_ViewInfo, nullptr, &m_ImageViews[i]) != VK_SUCCESS)
            {
                TR_CORE_CRITICAL("Failed to create image view for swapchain image {}", i);
            }
        }

        TR_CORE_TRACE("Image Views Created ({} Views)", m_ImageViews.size());
    }

    SwapchainSupportDetails Swapchain::QuerySwapchainSupport(VkPhysicalDevice device, VkSurfaceKHR surface)
    {
        SwapchainSupportDetails l_Details;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &l_Details.Capabilities);

        uint32_t l_FormatCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &l_FormatCount, nullptr);
        if (l_FormatCount)
        {
            l_Details.Formats.resize(l_FormatCount);
            vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &l_FormatCount, l_Details.Formats.data());
        }

        uint32_t l_PresentModeCount = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &l_PresentModeCount, nullptr);
        if (l_PresentModeCount)
        {
            l_Details.PresentModes.resize(l_PresentModeCount);
            vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &l_PresentModeCount, l_Details.PresentModes.data());
        }

        return l_Details;
    }

    VkSurfaceFormatKHR Swapchain::ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats)
    {
        for (const auto& a_Format : availableFormats)
        {
            if (a_Format.format == VK_FORMAT_B8G8R8A8_UNORM && a_Format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            {
                return a_Format;
            }
        }

        return availableFormats[0];
    }

    VkPresentModeKHR Swapchain::ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes)
    {
        for (auto a_Mode : availablePresentModes)
        {
            if (a_Mode == VK_PRESENT_MODE_MAILBOX_KHR)
            {
                return a_Mode;
            }
        }

        return VK_PRESENT_MODE_FIFO_KHR;
    }

    VkExtent2D Swapchain::ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities)
    {
        if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
        {
            return capabilities.currentExtent;
        }

        else
        {
            uint32_t l_Width = 0;
            uint32_t l_Height = 0;
            Startup::GetWindow().GetFramebufferSize(l_Width, l_Height);

            VkExtent2D l_ActualExtent = { l_Width, l_Height };

            l_ActualExtent.width = std::max(capabilities.minImageExtent.width, std::min(capabilities.maxImageExtent.width, l_ActualExtent.width));
            l_ActualExtent.height = std::max(capabilities.minImageExtent.height, std::min(capabilities.maxImageExtent.height, l_ActualExtent.height));

            return l_ActualExtent;
        }
    }
}