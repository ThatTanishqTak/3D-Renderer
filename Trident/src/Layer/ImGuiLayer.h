#pragma once

#include "Layer.h"

#include <vulkan/vulkan.h>

struct GLFWwindow;
struct ImGuiContext;

namespace Trident
{
    namespace UI
    {
        /**
         * Lightweight stub implementation of the engine UI bridge.
         * This derives from the base Layer interface so the application can
         * manage its lifetime uniformly alongside gameplay or editor layers.
         */
        class ImGuiLayer : public Layer
        {
        public:
            ImGuiLayer() = default;
            ~ImGuiLayer() override = default;

            /**
             * Injects the graphics handles required for future ImGui setup.
             * The parameters are cached but no GPU work occurs yet so the
             * renderer can call this even when UI features are disabled.
             */
            void Init(GLFWwindow* window, VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device, uint32_t queueFamily, VkQueue graphicsQueue,
                VkRenderPass renderPass, uint32_t imageCount, VkCommandPool commandPool);

            // Layer interface -------------------------------------------------
            void Initialize() override;
            void Shutdown() override;
            void Update() override;
            void Render() override;

            /**
             * Records UI commands into the provided command buffer. No-op for now
             * but kept to match renderer expectations.
             */
            void Render(VkCommandBuffer commandBuffer);

            /**
             * Prepares a new ImGui frame. Empty until UI integration lands.
             */
            void BeginFrame();

            /**
             * Finalises the UI frame and submits draw data. Currently a stub.
             */
            void EndFrame();

            /**
             * Allows multi-viewport rendering once implemented. No-op placeholder.
             */
            void RenderAdditionalViewports();

        private:
            void BeginDockspace();

        private:
            // Track whether Init has received graphics handles. Useful for guarding future work.
            bool m_IsInitialized = false;
            bool m_IsImGuiContextReady = false; ///< Guards against double initialisation and protects draw calls.

            // Cached handles for eventual ImGui hookup with Vulkan.
            GLFWwindow* m_Window = nullptr;
            VkInstance m_Instance = VK_NULL_HANDLE;
            VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
            VkDevice m_Device = VK_NULL_HANDLE;
            uint32_t m_QueueFamily = 0;
            VkQueue m_GraphicsQueue = VK_NULL_HANDLE;
            VkRenderPass m_RenderPass = VK_NULL_HANDLE;
            uint32_t m_ImageCount = 0;
            VkCommandPool m_CommandPool = VK_NULL_HANDLE;

            // ImGui state -------------------------------------------------------
            ImGuiContext* m_ImGuiContext = nullptr; ///< Owned ImGui context bound to this layer.
            VkDescriptorPool m_ImGuiDescriptorPool = VK_NULL_HANDLE; ///< Dedicated pool for ImGui descriptor allocations.
        };
    }
}