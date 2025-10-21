#include "Skybox.h"

#include "Application.h"

#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <vector>

namespace Trident
{
    void Skybox::Init(Buffers& buffers, CommandBufferPool& pool)
    {
        // Only the vertex positions are required to compute cubemap directions in the shader, so keep the layout minimal.
        std::array<glm::vec3, 8> l_Positions =
        {
            glm::vec3{ -1.0f, -1.0f,  1.0f },
            glm::vec3{  1.0f, -1.0f,  1.0f },
            glm::vec3{  1.0f,  1.0f,  1.0f },
            glm::vec3{ -1.0f,  1.0f,  1.0f },
            glm::vec3{ -1.0f, -1.0f, -1.0f },
            glm::vec3{  1.0f, -1.0f, -1.0f },
            glm::vec3{  1.0f,  1.0f, -1.0f },
            glm::vec3{ -1.0f,  1.0f, -1.0f }
        };

        // Index data remains unchanged; the shader derives all other attributes procedurally using the position vectors.
        std::vector<uint32_t> l_Indices =
        {
            0, 1, 2, 2, 3, 0,
            1, 5, 6, 6, 2, 1,
            5, 4, 7, 7, 6, 5,
            4, 0, 3, 3, 7, 4,
            3, 2, 6, 6, 7, 3,
            4, 5, 1, 1, 0, 4
        };

        // Upload the minimal position-only cube. Additional attributes are derived from the cubemap lookup, so no extra data is transferred.
        buffers.CreateVertexBuffer(l_Positions.data(), l_Positions.size(), sizeof(glm::vec3), pool, m_VertexBuffer, m_VertexBufferMemory);
        buffers.CreateIndexBuffer(l_Indices, pool, m_IndexBuffer, m_IndexBufferMemory, m_IndexCount);

        // TODO: Expand the layout when we introduce per-vertex skybox parallax or procedural horizon blending.
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

        VkBuffer l_VertexBuffers[] = { m_VertexBuffer };
        VkDeviceSize l_Offsets[] = { 0 };
        vkCmdBindVertexBuffers(cmdBuffer, 0, 1, l_VertexBuffers, l_Offsets);
        vkCmdBindIndexBuffer(cmdBuffer, m_IndexBuffer, 0, VK_INDEX_TYPE_UINT32);
        // Guard against missing descriptor sets so render doc captures remain robust while we iterate on cubemap hot-swapping.
        if (descriptorSets != nullptr)
        {
            vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &descriptorSets[imageIndex], 0, nullptr);
        }

        glm::mat4 l_Transform = glm::scale(glm::mat4(1.0f), glm::vec3(20.0f));
        vkCmdPushConstants(cmdBuffer, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &l_Transform);
        vkCmdDrawIndexed(cmdBuffer, m_IndexCount, 1, 0, 0, 0);
    }
}