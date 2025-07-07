#include "Skybox.h"

#include "Application.h"

#include <glm/gtc/matrix_transform.hpp>

namespace Trident
{
    void Skybox::Init(Buffers& buffers, CommandBufferPool& pool)
    {
        std::vector<Vertex> vertices =
        {
              // positions             // color              // texcoord
            { { -1.0f, -1.0f,  1.0f }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f } },
            { {  1.0f, -1.0f,  1.0f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 0.0f } },
            { {  1.0f,  1.0f,  1.0f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f } },
            { { -1.0f,  1.0f,  1.0f }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 1.0f } },

            { { -1.0f, -1.0f, -1.0f }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f } },
            { {  1.0f, -1.0f, -1.0f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 0.0f } },
            { {  1.0f,  1.0f, -1.0f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f } },
            { { -1.0f,  1.0f, -1.0f }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 1.0f } },
        };

        std::vector<uint32_t> indices =
        {
            0, 1, 2, 2, 3, 0,
            1, 5, 6, 6, 2, 1,
            5, 4, 7, 7, 6, 5,
            4, 0, 3, 3, 7, 4,
            3, 2, 6, 6, 7, 3,
            4, 5, 1, 1, 0, 4
        };

        buffers.CreateVertexBuffer(vertices, pool, m_VertexBuffer, m_VertexBufferMemory);
        buffers.CreateIndexBuffer(indices, pool, m_IndexBuffer, m_IndexBufferMemory, m_IndexCount);
    }

    void Skybox::Cleanup(Buffers& buffers)
    {
        buffers.DestroyBuffer(m_VertexBuffer, m_VertexBufferMemory);
        buffers.DestroyBuffer(m_IndexBuffer, m_IndexBufferMemory);

        m_VertexBuffer = VK_NULL_HANDLE;
        m_VertexBufferMemory = VK_NULL_HANDLE;
        m_IndexBuffer = VK_NULL_HANDLE;
        m_IndexBufferMemory = VK_NULL_HANDLE;
        m_IndexCount = 0;
    }

    void Skybox::Record(VkCommandBuffer cmdBuffer, VkPipelineLayout layout, const VkDescriptorSet* descriptorSets, uint32_t imageIndex)
    {
        if (m_VertexBuffer == VK_NULL_HANDLE || m_IndexBuffer == VK_NULL_HANDLE || m_IndexCount == 0)
        {
            return;
        }

        VkBuffer vBuffers[] = { m_VertexBuffer };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(cmdBuffer, 0, 1, vBuffers, offsets);
        vkCmdBindIndexBuffer(cmdBuffer, m_IndexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &descriptorSets[imageIndex], 0, nullptr);

        glm::mat4 transform = glm::scale(glm::mat4(1.0f), glm::vec3(20.0f));
        vkCmdPushConstants(cmdBuffer, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &transform);
        vkCmdDrawIndexed(cmdBuffer, m_IndexCount, 1, 0, 0, 0);
    }
}