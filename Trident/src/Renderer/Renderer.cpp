#include "Renderer/Renderer.h"

#include "Application/Startup.h"

#include "ECS/Components/TransformComponent.h"
#include "ECS/Components/CameraComponent.h"
#include "Geometry/Mesh.h"
#include "Layer/ImGuiLayer.h"
#include "Core/Utilities.h"
#include "Window/Window.h"
#include "Loader/ModelLoader.h"
#include "Loader/TextureLoader.h"

#include <stdexcept>
#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <cmath>
#include <chrono>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <cstdlib>
#include <limits>
#include <numeric>
#include <optional>
#include <cstdint>
#include <ctime>
#include <memory>
#include <system_error>
#include <cstring>
#include <cctype>
#include <cassert>
#include <iterator>
#include <utility>
#include <span>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/constants.hpp>
#include <imgui_impl_vulkan.h>

namespace
{
    constexpr const char* kDefaultTextureKey = "renderer://default-white";

    struct SwapchainFormatInfo
    {
        uint32_t m_BytesPerPixel = 0; // Total bytes consumed by a single pixel in the swapchain format.
        uint32_t m_ChannelCount = 0;  // Number of colour channels represented by the format.
        std::array<uint32_t, 4> m_ChannelMapping{ 0, 1, 2, 3 }; // Source channel order converted into RGBA output.
    };

    /**
     * @brief Lightweight description of the primary tensor layout exposed by the ONNX model.
     */
    struct TensorShapeInfo
    {
        bool m_IsValid = false;              // Signals whether the provided shape matched the expected NHWC layout.
        bool m_HasExplicitBatch = false;     // Tracks whether the model declared the batch dimension explicitly.
        bool m_IsChannelsLast = false;       // Indicates that the tensor arranges colour channels in the final dimension.
        int64_t m_Batch = 1;                 // Batch dimension reported by the model (defaulting to one sample).
        int64_t m_Height = -1;               // Height component extracted from the tensor shape.
        int64_t m_Width = -1;                // Width component extracted from the tensor shape.
        int64_t m_Channels = -1;             // Channel count derived from the tensor shape.
    };

    Trident::Geometry::Mesh BuildPrimitiveQuadMesh()
    {
        Trident::Geometry::Mesh l_Mesh{};

        std::array<Vertex, 4> l_Vertices{};
        l_Vertices[0].Position = { -0.5f, -0.5f, 0.0f };
        l_Vertices[1].Position = { 0.5f, -0.5f, 0.0f };
        l_Vertices[2].Position = { 0.5f, 0.5f, 0.0f };
        l_Vertices[3].Position = { -0.5f, 0.5f, 0.0f };

        const glm::vec3 l_Normal{ 0.0f, 0.0f, 1.0f };
        const glm::vec3 l_Tangent{ 1.0f, 0.0f, 0.0f };
        const glm::vec3 l_Bitangent{ 0.0f, 1.0f, 0.0f };

        for (Vertex& it_Vertex : l_Vertices)
        {
            it_Vertex.Normal = l_Normal;
            it_Vertex.Tangent = l_Tangent;
            it_Vertex.Bitangent = l_Bitangent;
            it_Vertex.Color = { 1.0f, 1.0f, 1.0f };
        }

        l_Vertices[0].TexCoord = { 0.0f, 0.0f };
        l_Vertices[1].TexCoord = { 1.0f, 0.0f };
        l_Vertices[2].TexCoord = { 1.0f, 1.0f };
        l_Vertices[3].TexCoord = { 0.0f, 1.0f };

        l_Mesh.Vertices.assign(l_Vertices.begin(), l_Vertices.end());
        // Counter clockwise winding to match the front face definition with the projection Y flip.
        l_Mesh.Indices = { 0, 1, 2, 0, 2, 3 };

        return l_Mesh;
    }

    Trident::Geometry::Mesh BuildPrimitiveCubeMesh()
    {
        Trident::Geometry::Mesh l_Mesh{};

        struct PrimitiveFace
        {
            glm::vec3 m_Normal{};
            glm::vec3 m_Tangent{};
            glm::vec3 m_Bitangent{};
            std::array<glm::vec3, 4> m_Positions{};
        };

        const std::array<PrimitiveFace, 6> l_Faces
        { {
            { { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f },
                { glm::vec3{ -0.5f, -0.5f, 0.5f }, glm::vec3{ 0.5f, -0.5f, 0.5f },
                  glm::vec3{ 0.5f, 0.5f, 0.5f }, glm::vec3{ -0.5f, 0.5f, 0.5f } } },
            { { 0.0f, 0.0f, -1.0f }, { -1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f },
                { glm::vec3{ 0.5f, -0.5f, -0.5f }, glm::vec3{ -0.5f, -0.5f, -0.5f },
                  glm::vec3{ -0.5f, 0.5f, -0.5f }, glm::vec3{ 0.5f, 0.5f, -0.5f } } },
            { { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, { 0.0f, 1.0f, 0.0f },
                { glm::vec3{ 0.5f, -0.5f, 0.5f }, glm::vec3{ 0.5f, -0.5f, -0.5f },
                  glm::vec3{ 0.5f, 0.5f, -0.5f }, glm::vec3{ 0.5f, 0.5f, 0.5f } } },
            { { -1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f },
                { glm::vec3{ -0.5f, -0.5f, -0.5f }, glm::vec3{ -0.5f, -0.5f, 0.5f },
                  glm::vec3{ -0.5f, 0.5f, 0.5f }, glm::vec3{ -0.5f, 0.5f, -0.5f } } },
            { { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f },
                { glm::vec3{ -0.5f, 0.5f, 0.5f }, glm::vec3{ 0.5f, 0.5f, 0.5f },
                  glm::vec3{ 0.5f, 0.5f, -0.5f }, glm::vec3{ -0.5f, 0.5f, -0.5f } } },
            { { 0.0f, -1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f },
                { glm::vec3{ -0.5f, -0.5f, -0.5f }, glm::vec3{ 0.5f, -0.5f, -0.5f },
                  glm::vec3{ 0.5f, -0.5f, 0.5f }, glm::vec3{ -0.5f, -0.5f, 0.5f } } }
        } };

        l_Mesh.Vertices.reserve(l_Faces.size() * 4);
        l_Mesh.Indices.reserve(l_Faces.size() * 6);

        uint32_t l_VertexOffset = 0;
        for (const PrimitiveFace& it_Face : l_Faces)
        {
            const std::array<glm::vec2, 4> l_TexCoords
            { {
                { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f }
            } };

            for (size_t it_Vertex = 0; it_Vertex < it_Face.m_Positions.size(); ++it_Vertex)
            {
                Vertex l_Vertex{};
                l_Vertex.Position = it_Face.m_Positions[it_Vertex];
                l_Vertex.Normal = it_Face.m_Normal;
                l_Vertex.Tangent = it_Face.m_Tangent;
                l_Vertex.Bitangent = it_Face.m_Bitangent;
                l_Vertex.Color = { 1.0f, 1.0f, 1.0f };
                l_Vertex.TexCoord = l_TexCoords[it_Vertex];
                l_Mesh.Vertices.push_back(l_Vertex);
            }

            l_Mesh.Indices.push_back(l_VertexOffset + 0);
            l_Mesh.Indices.push_back(l_VertexOffset + 2);
            l_Mesh.Indices.push_back(l_VertexOffset + 1);
            l_Mesh.Indices.push_back(l_VertexOffset + 0);
            l_Mesh.Indices.push_back(l_VertexOffset + 3);
            l_Mesh.Indices.push_back(l_VertexOffset + 2);
            l_VertexOffset += 4;
        }

        return l_Mesh;
    }

    Trident::Geometry::Mesh BuildPrimitiveSphereMesh()
    {
        Trident::Geometry::Mesh l_Mesh{};

        const uint32_t l_RingCount = 16;
        const uint32_t l_SegmentCount = 24;
        const float l_Radius = 0.5f;

        l_Mesh.Vertices.reserve((l_RingCount + 1) * (l_SegmentCount + 1));
        l_Mesh.Indices.reserve(l_RingCount * l_SegmentCount * 6);

        for (uint32_t it_Ring = 0; it_Ring <= l_RingCount; ++it_Ring)
        {
            const float l_V = static_cast<float>(it_Ring) / static_cast<float>(l_RingCount);
            const float l_Phi = l_V * glm::pi<float>();

            for (uint32_t it_Segment = 0; it_Segment <= l_SegmentCount; ++it_Segment)
            {
                const float l_U = static_cast<float>(it_Segment) / static_cast<float>(l_SegmentCount);
                const float l_Theta = l_U * glm::two_pi<float>();

                const float l_SinPhi = std::sin(l_Phi);
                const float l_CosPhi = std::cos(l_Phi);
                const float l_SinTheta = std::sin(l_Theta);
                const float l_CosTheta = std::cos(l_Theta);

                glm::vec3 l_Position{ l_Radius * l_SinPhi * l_CosTheta, l_Radius * l_CosPhi, l_Radius * l_SinPhi * l_SinTheta };
                glm::vec3 l_Normal = glm::normalize(l_Position);
                glm::vec3 l_Tangent{ -l_SinTheta, 0.0f, l_CosTheta };
                if (glm::length(l_Tangent) < 0.0001f)
                {
                    l_Tangent = { 1.0f, 0.0f, 0.0f };
                }
                l_Tangent = glm::normalize(l_Tangent);
                glm::vec3 l_Bitangent = glm::normalize(glm::cross(l_Normal, l_Tangent));
                if (glm::length(l_Bitangent) < 0.0001f)
                {
                    l_Bitangent = { 0.0f, 1.0f, 0.0f };
                }

                Vertex l_Vertex{};
                l_Vertex.Position = l_Position;
                l_Vertex.Normal = l_Normal;
                l_Vertex.Tangent = l_Tangent;
                l_Vertex.Bitangent = l_Bitangent;
                l_Vertex.Color = { 1.0f, 1.0f, 1.0f };
                l_Vertex.TexCoord = { l_U, 1.0f - l_V };
                l_Mesh.Vertices.push_back(l_Vertex);
            }
        }

        const uint32_t l_RowVertexCount = l_SegmentCount + 1;
        for (uint32_t it_Ring = 0; it_Ring < l_RingCount; ++it_Ring)
        {
            for (uint32_t it_Segment = 0; it_Segment < l_SegmentCount; ++it_Segment)
            {
                const uint32_t l_Idx0 = it_Ring * l_RowVertexCount + it_Segment;
                const uint32_t l_Idx1 = (it_Ring + 1) * l_RowVertexCount + it_Segment;
                const uint32_t l_Idx2 = (it_Ring + 1) * l_RowVertexCount + it_Segment + 1;
                const uint32_t l_Idx3 = it_Ring * l_RowVertexCount + it_Segment + 1;

                l_Mesh.Indices.push_back(l_Idx0);
                l_Mesh.Indices.push_back(l_Idx2);
                l_Mesh.Indices.push_back(l_Idx1);
                l_Mesh.Indices.push_back(l_Idx0);
                l_Mesh.Indices.push_back(l_Idx3);
                l_Mesh.Indices.push_back(l_Idx2);
            }
        }

        return l_Mesh;
    }

    SwapchainFormatInfo QuerySwapchainFormatInfo(VkFormat format)
    {
        // Extendable helper that converts the swapchain format into a CPU-friendly description.
        switch (format)
        {
        case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_B8G8R8A8_SRGB:
            // Vulkan stores pixels as BGRA for this swapchain format, so remap into RGBA before consumption.
            return { 4u, 4u, { 2u, 1u, 0u, 3u } };
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SRGB:
            // Already in RGBA order, so no remapping is required.
            return { 4u, 4u, { 0u, 1u, 2u, 3u } };
        default:
            return {};
        }
    }

    /**
     * @brief Extract the NHWC layout information from the model supplied tensor shape.
     */
    TensorShapeInfo ParseTensorShapeNhwc(std::span<const int64_t> shape)
    {
        TensorShapeInfo l_Info{};
        if (shape.empty())
        {
            return l_Info;
        }

        if (shape.size() == 4)
        {
            l_Info.m_IsValid = true;
            l_Info.m_HasExplicitBatch = true;
            l_Info.m_IsChannelsLast = true;
            l_Info.m_Batch = shape[0];
            l_Info.m_Height = shape[1];
            l_Info.m_Width = shape[2];
            l_Info.m_Channels = shape[3];
        }
        else if (shape.size() == 3)
        {
            l_Info.m_IsValid = true;
            l_Info.m_HasExplicitBatch = false;
            l_Info.m_IsChannelsLast = true;
            l_Info.m_Batch = 1;
            l_Info.m_Height = shape[0];
            l_Info.m_Width = shape[1];
            l_Info.m_Channels = shape[2];
        }

        return l_Info;
    }

    /**
     * @brief Replace dynamic tensor dimensions with a sensible runtime fallback.
     */
    int64_t ResolveTensorDimension(int64_t value, int64_t fallback)
    {
        if (value <= 0)
        {
            return fallback;
        }

        return value;
    }

    /**
     * @brief Build a canonical NHWC shape so downstream systems can treat the tensor consistently.
     */
    std::array<int64_t, 4> BuildCanonicalNhwcShape(const TensorShapeInfo& shape, VkExtent2D fallbackExtent, uint32_t fallbackChannels)
    {
        const int64_t l_Height = ResolveTensorDimension(shape.m_Height, static_cast<int64_t>(fallbackExtent.height));
        const int64_t l_Width = ResolveTensorDimension(shape.m_Width, static_cast<int64_t>(fallbackExtent.width));
        const int64_t l_Channels = ResolveTensorDimension(shape.m_Channels, static_cast<int64_t>(fallbackChannels));

        return
        {
            1, std::max<int64_t>(l_Height, 0), std::max<int64_t>(l_Width, 0), std::max<int64_t>(l_Channels, 0)
        };
    }

    /**
     * @brief Convert a raw tensor shape into a debug-friendly string representation.
     */
    std::string BuildShapeDebugString(std::span<const int64_t> shape)
    {
        if (shape.empty())
        {
            return "[]";
        }

        std::ostringstream l_Stream;
        l_Stream << '[';
        for (size_t it_Index = 0; it_Index < shape.size(); ++it_Index)
        {
            l_Stream << shape[it_Index];
            const bool l_IsLast = (it_Index + 1) == shape.size();
            if (!l_IsLast)
            {
                l_Stream << ", ";
            }
        }
        l_Stream << ']';

        return l_Stream.str();
    }

    /**
     * @brief Convert the AI output tensor into normalised BGRA data that matches the swapchain format.
     */
    void NormaliseAndReorderAiOutput(std::vector<float>& data, const TensorShapeInfo& shape, VkExtent2D fallbackExtent, uint32_t fallbackChannels)
    {
        if (data.empty())
        {
            return;
        }

        const std::array<int64_t, 4> l_CanonicalShape = BuildCanonicalNhwcShape(shape, fallbackExtent, fallbackChannels);
        const int64_t l_ChannelCount64 = l_CanonicalShape[3];
        if (l_ChannelCount64 <= 0)
        {
            TR_CORE_WARN("AI output tensor reported an invalid channel count ({}).", l_ChannelCount64);
            return;
        }

        const size_t l_ChannelCount = static_cast<size_t>(l_ChannelCount64);
        if (l_ChannelCount == 0)
        {
            return;
        }

        if (data.size() % l_ChannelCount != 0)
        {
            TR_CORE_WARN("AI output tensor element count ({}) did not align with the channel count ({}).", data.size(), l_ChannelCount);
            return;
        }

        const size_t l_PixelCount = data.size() / l_ChannelCount;

        const auto [l_MinIt, l_MaxIt] = std::minmax_element(data.begin(), data.end());
        const float l_MinValue = *l_MinIt;
        const float l_MaxValue = *l_MaxIt;
        constexpr float s_Tolerance = 1.0e-3f;
        const bool l_NeedsScaling = (l_MaxValue > 1.0f + s_Tolerance) || (l_MinValue < -s_Tolerance);
        const float l_Scale = l_NeedsScaling ? (1.0f / 255.0f) : 1.0f;

        if (l_Scale != 1.0f)
        {
            for (float& l_Value : data)
            {
                l_Value *= l_Scale;
            }
        }

        if (l_ChannelCount >= 3)
        {
            for (size_t it_Pixel = 0; it_Pixel < l_PixelCount; ++it_Pixel)
            {
                const size_t l_BaseIndex = it_Pixel * l_ChannelCount;
                std::swap(data[l_BaseIndex], data[l_BaseIndex + 2]);
            }
        }

        for (float& l_Value : data)
        {
            l_Value = std::clamp(l_Value, 0.0f, 1.0f);
        }
    }

    glm::mat4 ComposeTransform(const Trident::Transform& transform)
    {
        glm::mat4 l_Mat{ 1.0f };
        l_Mat = glm::translate(l_Mat, transform.Position);
        l_Mat = glm::rotate(l_Mat, glm::radians(transform.Rotation.x), glm::vec3{ 1.0f, 0.0f, 0.0f });
        l_Mat = glm::rotate(l_Mat, glm::radians(transform.Rotation.y), glm::vec3{ 0.0f, 1.0f, 0.0f });
        l_Mat = glm::rotate(l_Mat, glm::radians(transform.Rotation.z), glm::vec3{ 0.0f, 0.0f, 1.0f });
        l_Mat = glm::scale(l_Mat, transform.Scale);

        return l_Mat;
    }

    Trident::Transform DecomposeWorldTransform(const glm::mat4& worldTransform, const Trident::Transform& fallback)
    {
        // Convert the provided matrix back into authorable TRS values, preserving a fallback when decomposition fails.
        Trident::Transform l_Result = fallback;
        glm::vec3 l_Scale{ 1.0f };
        glm::quat l_Rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
        glm::vec3 l_Translation{ 0.0f };
        glm::vec3 l_Skew{ 0.0f };
        glm::vec4 l_Perspective{ 0.0f };

        if (glm::decompose(worldTransform, l_Scale, l_Rotation, l_Translation, l_Skew, l_Perspective))
        {
            l_Rotation = glm::normalize(l_Rotation);
            l_Result.Position = l_Translation;
            l_Result.Scale = l_Scale;
            l_Result.Rotation = glm::degrees(glm::eulerAngles(l_Rotation));
        }

        return l_Result;
    }

    glm::mat3 BuildRotationMatrix(const Trident::Transform& transform)
    {
        // Compose the camera orientation without translation/scale so we can transform local frustum points.
        glm::mat4 l_Rotation{ 1.0f };
        l_Rotation = glm::rotate(l_Rotation, glm::radians(transform.Rotation.x), glm::vec3{ 1.0f, 0.0f, 0.0f });
        l_Rotation = glm::rotate(l_Rotation, glm::radians(transform.Rotation.y), glm::vec3{ 0.0f, 1.0f, 0.0f });
        l_Rotation = glm::rotate(l_Rotation, glm::radians(transform.Rotation.z), glm::vec3{ 0.0f, 0.0f, 1.0f });

        return glm::mat3(l_Rotation);
    }

    bool BuildFrustumPreview(const Trident::Transform& transform, const Trident::CameraComponent& cameraComponent,
        const glm::vec2& viewportSize, std::array<glm::vec3, 4>& outWorldCorners)
    {
        // Ensure the viewport has a sensible aspect ratio before attempting calculations.
        if (viewportSize.x <= std::numeric_limits<float>::epsilon() || viewportSize.y <= std::numeric_limits<float>::epsilon())
        {
            return false;
        }

        const float l_ViewportAspect = viewportSize.x / viewportSize.y;
        const bool l_UseFixedAspect = cameraComponent.m_FixedAspectRatio && cameraComponent.m_AspectRatio > std::numeric_limits<float>::epsilon();
        const float l_AspectRatio = l_UseFixedAspect ? cameraComponent.m_AspectRatio : l_ViewportAspect;
        if (l_AspectRatio <= std::numeric_limits<float>::epsilon())
        {
            return false;
        }

        const float l_MinClip = 0.001f;
        const float l_NearClip = std::max(cameraComponent.m_NearClip, l_MinClip);
        const float l_FarClip = std::max(cameraComponent.m_FarClip, l_NearClip + l_MinClip);

        // Limit the overlay depth so the preview remains legible regardless of far clip distance.
        constexpr float s_MaxPreviewDepth = 5.0f;
        const float l_PreviewDepth = std::min(l_NearClip + s_MaxPreviewDepth, l_FarClip);
        if (l_PreviewDepth <= l_MinClip)
        {
            return false;
        }

        const glm::mat3 l_RotationMatrix = BuildRotationMatrix(transform);
        const glm::vec3 l_Position = transform.Position;

        std::array<glm::vec3, 4> l_LocalCorners{};
        if (cameraComponent.m_ProjectionType == Trident::Camera::ProjectionType::Perspective)
        {
            const float l_FieldOfView = glm::radians(std::clamp(cameraComponent.m_FieldOfView, 1.0f, 179.0f));
            const float l_HalfHeight = std::tan(l_FieldOfView * 0.5f) * l_PreviewDepth;
            const float l_HalfWidth = l_HalfHeight * l_AspectRatio;

            l_LocalCorners[0] = { -l_HalfWidth, l_HalfHeight, -l_PreviewDepth };
            l_LocalCorners[1] = { l_HalfWidth, l_HalfHeight, -l_PreviewDepth };
            l_LocalCorners[2] = { l_HalfWidth, -l_HalfHeight, -l_PreviewDepth };
            l_LocalCorners[3] = { -l_HalfWidth, -l_HalfHeight, -l_PreviewDepth };
        }
        else if (cameraComponent.m_ProjectionType == Trident::Camera::ProjectionType::Orthographic)
        {
            const float l_HalfHeight = std::max(cameraComponent.m_OrthographicSize * 0.5f, 0.01f);
            const float l_HalfWidth = l_HalfHeight * l_AspectRatio;

            l_LocalCorners[0] = { -l_HalfWidth, l_HalfHeight, -l_PreviewDepth };
            l_LocalCorners[1] = { l_HalfWidth, l_HalfHeight, -l_PreviewDepth };
            l_LocalCorners[2] = { l_HalfWidth, -l_HalfHeight, -l_PreviewDepth };
            l_LocalCorners[3] = { -l_HalfWidth, -l_HalfHeight, -l_PreviewDepth };
        }
        else
        {
            return false;
        }

        for (size_t it_Index = 0; it_Index < l_LocalCorners.size(); ++it_Index)
        {
            const glm::vec3 l_WorldCorner = l_Position + l_RotationMatrix * l_LocalCorners[it_Index];
            outWorldCorners[it_Index] = l_WorldCorner;
        }

        return true;
    }

    std::tm ToLocalTime(std::time_t time)
    {
        std::tm l_LocalTime{};
#ifdef _WIN32
        localtime_s(&l_LocalTime, &time);
#else
        localtime_r(&time, &l_LocalTime);
#endif
        return l_LocalTime;
    }
}

namespace Trident
{
    Renderer::Renderer()
    {
        m_AiDebugStats.m_BlendStrength = m_AiBlendStrength;
        m_AiDebugStats.m_TextureExtent = m_AiTextureExtent;
        m_AiDebugStats.m_TextureReady = m_AiTextureReady;
        m_AiDebugStats.m_ModelInitialised = m_FrameGenerator.IsInitialised();

        // Default dataset capture settings point at a local directory and remain disabled until requested.
        m_FrameDatasetCaptureDirectory = std::filesystem::current_path() / "DatasetCapture";
        m_FrameDatasetCaptureInterval = 1;
        m_FrameDatasetCaptureEnabled = false;

        // Attempt to locate an AI model so frame interpolation can be ready before the first render.
        const bool l_ModelInitialised = TryInitialiseAiModel();
        m_AiDebugStats.m_ModelInitialised = l_ModelInitialised;
        m_AiNextModelSearchTime = std::chrono::steady_clock::now() + s_AiModelSearchInterval;

        // Dataset capture can be toggled via environment variables so tooling scripts enable it on demand.
        const char* l_EnableCaptureEnv = std::getenv("TRIDENT_DATASET_CAPTURE_ENABLE");
        if (l_EnableCaptureEnv != nullptr && std::strlen(l_EnableCaptureEnv) > 0)
        {
            const char* l_CustomDirectory = std::getenv("TRIDENT_DATASET_CAPTURE_DIR");
            if (l_CustomDirectory != nullptr && std::strlen(l_CustomDirectory) > 0)
            {
                SetFrameDatasetCaptureDirectory(l_CustomDirectory);
            }

            SetFrameDatasetCaptureEnabled(true);
            TR_CORE_INFO("Frame dataset capture enabled. Writing samples to '{}'", m_FrameDatasetCaptureDirectory.string());
        }
        else
        {
            SetFrameDatasetCaptureEnabled(false);
        }
    }

    Renderer::~Renderer()
    {
        if (!m_Shutdown)
        {
            Shutdown();
        }
    }

    void Renderer::Init()
    {
        TR_CORE_INFO("-------INITIALIZING RENDERER-------");

        SetActiveRegistry(&Startup::GetRegistry());

        m_Swapchain.Init();
        // Reset the cached swapchain image layouts so new back buffers start from a known undefined state.
        m_SwapchainImageLayouts.assign(m_Swapchain.GetImageCount(), VK_IMAGE_LAYOUT_UNDEFINED);
        m_SwapchainDepthLayouts.assign(m_Swapchain.GetImageCount(), VK_IMAGE_LAYOUT_UNDEFINED);
        m_Pipeline.Init(m_Swapchain);
        m_Commands.Init(m_Swapchain.GetImageCount());

        // Pre-size the performance history buffer so we can efficiently track frame timings.
        m_PerformanceHistory.clear();
        m_PerformanceHistory.resize(s_PerformanceHistorySize);
        m_PerformanceHistoryNextIndex = 0;
        m_PerformanceSampleCount = 0;
        m_PerformanceStats = {};

        VkDeviceSize l_GlobalSize = sizeof(GlobalUniformBuffer);
        // Allocate per-frame uniform buffers for camera/light state and the material table.
        m_Buffers.CreateUniformBuffers(m_Swapchain.GetImageCount(), l_GlobalSize, m_GlobalUniformBuffers, m_GlobalUniformBuffersMemory);
        EnsureMaterialBufferCapacity(m_Materials.size());
        EnsureSkinningBufferCapacity(std::max<size_t>(m_BonePaletteMatrixCapacity, static_cast<size_t>(s_MaxBonesPerSkeleton)));

        CreateDescriptorPool();
        CreateDefaultTexture();
        CreateDefaultSkybox();
        CreateDescriptorSets();

        const uint32_t l_FrameCount = static_cast<uint32_t>(m_Swapchain.GetImageCount());
        m_TextRenderer.Init(m_Buffers, m_Commands, m_DescriptorPool, m_Pipeline.GetRenderPass(), l_FrameCount);

        // Prepare shared quad geometry so every sprite draw can reference the same GPU buffers.
        BuildSpriteGeometry();

        m_ViewportContexts.clear();
        m_ActiveViewportId = 0;

        ViewportContext& l_DefaultContext = GetOrCreateViewportContext(m_ActiveViewportId);
        l_DefaultContext.m_Info.ViewportID = m_ActiveViewportId;
        l_DefaultContext.m_Info.Position = { 0.0f, 0.0f };
        l_DefaultContext.m_Info.Size = { static_cast<float>(m_Swapchain.GetExtent().width), static_cast<float>(m_Swapchain.GetExtent().height) };
        l_DefaultContext.m_CachedExtent = { 0, 0 };

        // Only allocate CPU-visible readback resources once a caller explicitly requests them.
        if (m_ReadbackEnabled)
        {
            RequestReadbackResize(m_Swapchain.GetExtent(), true);
            ApplyPendingReadbackResize();
        }

        VkFenceCreateInfo l_FenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        l_FenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        if (vkCreateFence(Startup::GetDevice(), &l_FenceInfo, nullptr, &m_ResourceFence) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create resource fence");
        }

        TR_CORE_INFO("-------RENDERER INITIALIZED-------");
    }

    void Renderer::Shutdown()
    {
        TR_CORE_TRACE("Shutting Down Renderer");

        // Block until all GPU work completes so that pipelines, descriptor pools and swapchain-backed
        // images are no longer referenced by in-flight command buffers before teardown begins.
        vkDeviceWaitIdle(Startup::GetDevice());

        m_Commands.Cleanup();
        m_TextRenderer.Shutdown();

        // Tear down any editor viewport resources before the core pipeline disappears.
        DestroyAllOffscreenResources();
        m_ViewportContexts.clear();

        // Release shared sprite geometry before the buffer allocator clears tracked allocations.
        DestroySpriteGeometry();

        for (size_t it_Index = 0; it_Index < m_BonePaletteBuffers.size(); ++it_Index)
        {
            m_Buffers.DestroyBuffer(m_BonePaletteBuffers[it_Index], (it_Index < m_BonePaletteMemory.size()) ? m_BonePaletteMemory[it_Index] : VK_NULL_HANDLE);
        }
        m_BonePaletteBuffers.clear();
        m_BonePaletteMemory.clear();
        m_BonePaletteScratch.clear();
        m_BonePaletteBufferSize = 0;
        m_BonePaletteMatrixCapacity = 0;

        if (m_DescriptorPool != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorPool(Startup::GetDevice(), m_DescriptorPool, nullptr);

            m_DescriptorPool = VK_NULL_HANDLE;
        }

        m_DescriptorSets.clear();
        DestroySkyboxDescriptorSets();
        m_Pipeline.Cleanup();
        m_Swapchain.Cleanup();
        m_Skybox.Cleanup(m_Buffers);
        m_Buffers.Cleanup();
        m_GlobalUniformBuffers.clear();
        m_GlobalUniformBuffersMemory.clear();
        m_MaterialBuffers.clear();
        m_MaterialBuffersMemory.clear();
        m_MaterialBufferDirty.clear();
        m_MaterialBufferElementCount = 0;

        for (TextureSlot& it_Slot : m_TextureSlots)
        {
            DestroyTextureSlot(it_Slot);
        }
        m_TextureSlots.clear();
        m_TextureSlotLookup.clear();
        m_TextureDescriptorCache.clear();

        DestroySkyboxCubemap();

        // Release the CPU-visible staging buffers used for AI readback before the buffer allocator is reset.
        DestroyReadbackResources();
        DestroyAiResources();

        for (auto& it_Texture : m_ImGuiTexturePool)
        {
            if (it_Texture)
            {
                DestroyImGuiTexture(*it_Texture);
            }
        }
        m_ImGuiTexturePool.clear();

        if (m_ResourceFence != VK_NULL_HANDLE)
        {
            vkDestroyFence(Startup::GetDevice(), m_ResourceFence, nullptr);

            m_ResourceFence = VK_NULL_HANDLE;
        }

        m_Shutdown = true;

        TR_CORE_TRACE("Renderer Shutdown Complete");
    }

    void Renderer::DrawFrame()
    {
        const auto l_FrameStartTime = std::chrono::steady_clock::now();
        const auto l_FrameWallClock = std::chrono::system_clock::now();
        VkExtent2D l_FrameExtent{ 0, 0 };

        Utilities::Allocation::ResetFrame();
        ProcessReloadEvents();

        // Apply any pending readback resizes once the GPU is idle so resource churn stays out of the hot path.
        ApplyPendingReadbackResize();

        // Allow developers to tweak GLSL and get instant feedback without restarting the app.
        if (m_Pipeline.ReloadIfNeeded(m_Swapchain))
        {
            TR_CORE_INFO("Graphics pipeline reloaded after shader edit");
            m_TextRenderer.RecreatePipeline(m_Pipeline.GetRenderPass());
        }

        VkFence l_InFlightFence = m_Commands.GetInFlightFence(m_Commands.CurrentFrame());
        if (m_Commands.SupportsTimelineSemaphores())
        {
            // With timeline semaphores available we can reuse a single handle and just wait for the next counter value instead
            // of resetting the binary fence every frame. This keeps command submission lightweight while preserving correctness.
            const uint64_t l_TargetValue = m_Commands.GetTimelineValue();
            if (l_TargetValue > 0)
            {
                VkSemaphore l_WaitSemaphore = m_Commands.GetFrameTimelineSemaphore();
                VkSemaphoreWaitInfo l_WaitInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO };
                l_WaitInfo.semaphoreCount = 1;
                l_WaitInfo.pSemaphores = &l_WaitSemaphore;
                l_WaitInfo.pValues = &l_TargetValue;

                vkWaitSemaphores(Startup::GetDevice(), &l_WaitInfo, UINT64_MAX);
            }
        }
        else
        {
            vkWaitForFences(Startup::GetDevice(), 1, &l_InFlightFence, VK_TRUE, UINT64_MAX);
        }

        // Set the frame index for deferred destruction so buffer recycling tracks the correct fence slot.
        m_Buffers.SetCurrentFrame(m_Commands.CurrentFrame());
        m_Buffers.ProcessPendingDestroys(l_InFlightFence, m_Commands.CurrentFrame());

        uint32_t l_ImageIndex = 0;
        if (!AcquireNextImage(l_ImageIndex, l_InFlightFence))
        {
            TR_CORE_CRITICAL("Failed to acquire the next image");

            return;
        }

        // Enable readback only when AI processing or viewport recording explicitly requests it.
        const bool l_ReadbackRequired = m_FrameGenerator.IsInitialised() || m_ViewportRecordingEnabled;
        SetReadbackEnabled(l_ReadbackRequired, m_Swapchain.GetExtent());

        // Resolve any completed GPU readback before new commands reuse the staging buffers for this swapchain image.
        if (m_ReadbackEnabled)
        {
            ResolvePendingReadback(l_ImageIndex, l_FrameWallClock);
        }

        UpdateUniformBuffer(l_ImageIndex);

        // Reset the submission fence before queuing work so validation never sees a previously signaled handle, regardless of
        // whether timeline semaphores cover CPU/GPU pacing on this platform. Keeping the fence unsignaled avoids warnings when
        // vkQueueSubmit is given a fence that was already satisfied.
        vkResetFences(Startup::GetDevice(), 1, &l_InFlightFence);

        if (!RecordCommandBuffer(l_ImageIndex))
        {
            TR_CORE_CRITICAL("Failed to record command buffer");
            
            return;
        }

        // Once the main color pass is ready we can offer the frame to the AI helper.
        ProcessAiFrame();

        // Offer the newly resolved readback to the recording path if a viewport capture is active.
        SubmitViewportFrame(l_ImageIndex, l_FrameWallClock);

        // Capture the extent actually used when recording commands so our metrics reflect the final render target dimensions.
        l_FrameExtent = m_Swapchain.GetExtent();

        if (!SubmitFrame(l_ImageIndex, l_InFlightFence))
        {
            TR_CORE_CRITICAL("Failed to submit frame");

            return;
        }

        PresentFrame(l_ImageIndex);

        m_Commands.CurrentFrame() = (m_Commands.CurrentFrame() + 1) % m_Commands.GetFrameCount();
        m_FrameAllocationCount = Utilities::Allocation::GetFrameCount();
        
        //TR_CORE_TRACE("Frame allocations: {}", m_FrameAllocationCount);

        const auto l_FrameEndTime = std::chrono::steady_clock::now();
        const double l_FrameMilliseconds = std::chrono::duration<double, std::milli>(l_FrameEndTime - l_FrameStartTime).count();
        const double l_FrameFPS = l_FrameMilliseconds > 0.0 ? 1000.0 / l_FrameMilliseconds : 0.0;
        AccumulateFrameTiming(l_FrameMilliseconds, l_FrameFPS, l_FrameExtent, l_FrameWallClock);
    }

    void Renderer::ProcessAiFrame()
    {
        m_AiDebugStats.m_ModelInitialised = m_FrameGenerator.IsInitialised();
        m_AiDebugStats.m_BlendStrength = m_AiBlendStrength;
        m_AiDebugStats.m_TextureReady = m_AiTextureReady;
        m_AiDebugStats.m_TextureExtent = m_AiTextureExtent;

        if (!m_AiDebugStats.m_ModelInitialised)
        {
            // Periodically retry model discovery so dropping new weights on disk activates the AI path without a restart.
            const auto l_Now = std::chrono::steady_clock::now();
            if (l_Now >= m_AiNextModelSearchTime)
            {
                TryInitialiseAiModel();
                m_AiDebugStats.m_ModelInitialised = m_FrameGenerator.IsInitialised();
                m_AiNextModelSearchTime = l_Now + s_AiModelSearchInterval;
            }

            if (!m_AiDebugStats.m_ModelInitialised)
            {
                m_AiDebugStats.m_PendingJobCount = 0;
                m_AiDebugStats.m_CompletedInferenceCount = 0;
                m_AiDebugStats.m_LastInferenceMilliseconds = 0.0;
                m_AiDebugStats.m_AverageInferenceMilliseconds = 0.0;
            }

            return;
        }

        // Lambda keeps the sampling logic centralised so every exit path reports consistent queue and timing metrics.
        const auto a_RefreshStats = [this]()
            {
                m_AiDebugStats.m_PendingJobCount = m_FrameGenerator.GetPendingJobCount();
                m_AiDebugStats.m_CompletedInferenceCount = m_FrameGenerator.GetCompletedInferenceCount();
                m_AiDebugStats.m_LastInferenceMilliseconds = m_FrameGenerator.GetLastInferenceMilliseconds();
                m_AiDebugStats.m_AverageInferenceMilliseconds = m_FrameGenerator.GetAverageInferenceMilliseconds();
            };

        a_RefreshStats();

        const std::span<const int64_t> l_PrimaryInputShape = m_FrameGenerator.GetPrimaryInputShape();
        const TensorShapeInfo l_InputShapeInfo = ParseTensorShapeNhwc(l_PrimaryInputShape);
        if (!m_AiInputLayoutVerified && !l_PrimaryInputShape.empty())
        {
            if (!l_InputShapeInfo.m_IsValid || !l_InputShapeInfo.m_IsChannelsLast)
            {
                TR_CORE_ERROR("AI input tensor is expected to follow the [1, height, width, channels] layout. The reported shape was {}.", BuildShapeDebugString(l_PrimaryInputShape));
                return;
            }

            if (l_InputShapeInfo.m_HasExplicitBatch && l_InputShapeInfo.m_Batch > 1)
            {
                TR_CORE_WARN("AI model declared a batch size of {}. The renderer currently feeds a single frame per submission.", l_InputShapeInfo.m_Batch);
            }

            m_AiInputLayoutVerified = true;
        }

        const std::span<const int64_t> l_PrimaryOutputShape = m_FrameGenerator.GetPrimaryOutputShape();
        const TensorShapeInfo l_OutputShapeInfo = ParseTensorShapeNhwc(l_PrimaryOutputShape);
        if (!m_AiOutputLayoutVerified && !l_PrimaryOutputShape.empty())
        {
            if (!l_OutputShapeInfo.m_IsValid || !l_OutputShapeInfo.m_IsChannelsLast)
            {
                TR_CORE_ERROR("AI output tensor must expose the channels-last layout so dataset capture stays consistent. The reported shape was {}.", BuildShapeDebugString(l_PrimaryOutputShape));
                return;
            }

            m_AiOutputLayoutVerified = true;
        }

        // Poll for completed AI tensors before scheduling new work so post-processing can react immediately.
        std::vector<float> l_CompletedOutput;
        if (m_FrameGenerator.TryConsumeOutput(l_CompletedOutput) && !l_CompletedOutput.empty())
        {
            NormaliseAndReorderAiOutput(l_CompletedOutput, l_OutputShapeInfo, m_FrameReadbackExtent, m_FrameReadbackChannelCount);
            const std::array<int64_t, 4> l_OutputCaptureShape = BuildCanonicalNhwcShape(l_OutputShapeInfo, m_FrameReadbackExtent, m_FrameReadbackChannelCount);

            if (m_FrameDatasetCaptureEnabled)
            {
                // Persist the freshly produced AI tensor alongside the matching rasterised frame for offline training.
                m_FrameDatasetRecorder.RecordAiOutput(l_CompletedOutput, l_OutputCaptureShape);
            }

            m_AiInterpolationBuffer = std::move(l_CompletedOutput);
            m_AiTextureDirty = true;
            a_RefreshStats();
        }

        UploadAiInterpolationToGpu();

        std::vector<float> l_FrameReadback;
        if (!TryAcquireRenderedFrame(l_FrameReadback))
        {
            // No fresh GPU readback is available. This occurs when the viewport resolution changes or the frame has not
            // completed yet; the AI helper simply reuses the previous output in that scenario.
            return;
        }

        const std::array<int64_t, 4> l_InputCaptureShape = BuildCanonicalNhwcShape(l_InputShapeInfo, m_FrameReadbackExtent, m_FrameReadbackChannelCount);
        size_t l_ExpectedElements = 0;
        if (l_InputCaptureShape[1] > 0 && l_InputCaptureShape[2] > 0 && l_InputCaptureShape[3] > 0)
        {
            l_ExpectedElements = static_cast<size_t>(l_InputCaptureShape[1]) * static_cast<size_t>(l_InputCaptureShape[2]) * static_cast<size_t>(l_InputCaptureShape[3]);
        }

        if (l_ExpectedElements > 0 && l_FrameReadback.size() != l_ExpectedElements)
        {
            TR_CORE_WARN("AI frame generator received {} elements but expected {}. Skipping inference this frame.", l_FrameReadback.size(), l_ExpectedElements);
            a_RefreshStats();

            return;
        }

        if (!m_FrameGenerator.ProcessFrame(l_FrameReadback))
        {
            TR_CORE_WARN("AI frame generator rejected the current frame. Retaining the previous AI output for now.");
            a_RefreshStats();

            return;
        }

        if (m_FrameDatasetCaptureEnabled)
        {
            // Cache the exact tensor submitted to the AI worker so the dataset stays perfectly synchronised.
            m_FrameDatasetRecorder.RecordInputFrame(l_FrameReadback, m_FrameReadbackExtent, m_FrameReadbackChannelCount, l_InputCaptureShape);
        }

        a_RefreshStats();

        // TODO: Once the async path matures, explore scheduling policies such as adaptive batching or prioritising history frames.
    }

    bool Renderer::TryAcquireRenderedFrame(std::vector<float>& a_OutPixels)
    {
        if (m_PendingFrameReadback.empty())
        {
            return false;
        }

        a_OutPixels = m_PendingFrameReadback;
        m_PendingFrameReadback.clear();

        return true;
    }

    void Renderer::SubmitViewportFrame(uint32_t imageIndex, std::chrono::system_clock::time_point captureTimestamp)
    {
        (void)captureTimestamp;

        if (!m_ViewportRecordingEnabled)
        {
            return;
        }

        const bool l_SessionActive = m_ViewportRecordingSessionActive && m_VideoEncoder && m_VideoEncoder->IsSessionActive();

        // Detect encoder session drift and either restart the encoder or disable recording to keep UI state accurate.
        if (!l_SessionActive)
        {
            if (!m_VideoEncoder)
            {
                m_VideoEncoder = std::make_unique<VideoEncoder>();
            }

            if (!m_VideoEncoder)
            {
                TR_CORE_WARN("Viewport recording disabled because the encoder could not be created.");
                m_ViewportRecordingEnabled = false;
                m_ViewportRecordingSessionActive = false;
                m_RecordingViewportId = s_InvalidViewportId;

                return;
            }

            const bool l_SessionRestarted = m_VideoEncoder->BeginSession(m_RecordingOutputPath, m_RecordingExtent, 30);
            const bool l_ReinitialisedSessionActive = l_SessionRestarted && m_VideoEncoder->IsSessionActive();

            if (!l_ReinitialisedSessionActive)
            {
                TR_CORE_WARN("Viewport recording disabled because the encoder session is inactive.");
                m_ViewportRecordingEnabled = false;
                m_ViewportRecordingSessionActive = false;
                m_RecordingViewportId = s_InvalidViewportId;

                return;
            }

            m_ViewportRecordingEnabled = true;
            m_ViewportRecordingSessionActive = true;
        }

        if (m_RecordingViewportId == s_InvalidViewportId)
        {
            return;
        }

        if (m_PendingFrameReadbackBytes.empty())
        {
            return;
        }

        if (m_RecordingExtent.width == 0 || m_RecordingExtent.height == 0)
        {
            TR_CORE_WARN("Viewport recording rejected because the extent is invalid.");
            return;
        }

        VideoEncoder::RecordedFrame l_Frame{};
        l_Frame.m_Pixels = m_PendingFrameReadbackBytes;
        l_Frame.m_Extent = m_RecordingExtent;
        l_Frame.m_Timestamp = m_LastReadbackTimestamp;
        l_Frame.m_FrameIndex = imageIndex;
        l_Frame.m_ViewportId = m_RecordingViewportId;

        m_ViewportFrameBuffer.push_back(l_Frame);

        if (m_VideoEncoder)
        {
            if (!m_VideoEncoder->SubmitFrame(l_Frame))
            {
                TR_CORE_WARN("Video encoder rejected frame {} for viewport {}", imageIndex, m_RecordingViewportId);
            }
        }
    }

    bool Renderer::TryInitialiseAiModel()
    {
        // Attempt to resolve the AI model and initialise the frame generator. Logging is throttled so development builds remain readable.
        const std::optional<std::filesystem::path> l_ModelPath = ResolveAiModelPath();
        if (!l_ModelPath.has_value())
        {
            if (!m_AiModelMissingWarningIssued)
            {
                TR_CORE_INFO("AI frame generator did not locate a model. Rendering will continue without AI augmentation until one is provided.");
                m_AiModelMissingWarningIssued = true;
                m_AiModelInitialiseWarningIssued = false;
            }

            return false;
        }

        if (m_FrameGenerator.Initialise(l_ModelPath.value()))
        {
            TR_CORE_INFO("AI frame generator initialised with model '{}'", l_ModelPath->string());
            m_AiModelMissingWarningIssued = false;
            m_AiModelInitialiseWarningIssued = false;

            return true;
        }

        if (!m_AiModelInitialiseWarningIssued)
        {
            TR_CORE_WARN("AI frame generator failed to initialise using model '{}'. Future frames will skip AI processing until a valid model is supplied.", l_ModelPath->string());
            m_AiModelInitialiseWarningIssued = true;
        }

        return false;
    }

    void Renderer::SetReadbackEnabled(bool enabled, VkExtent2D resizeTarget)
    {
        // Toggle CPU readback resources based on the current feature requirements.
        if (m_ReadbackEnabled == enabled)
        {
            return;
        }

        m_ReadbackEnabled = enabled;

        if (m_ReadbackEnabled)
        {
            RequestReadbackResize(resizeTarget, true);
            ApplyPendingReadbackResize();

            return;
        }

        // Clear pending resize requests and release staging buffers when readback is disabled.
        vkDeviceWaitIdle(Startup::GetDevice());
        m_ReadbackResizePending = false;
        m_FrameReadbackPending.assign(m_FrameReadbackPending.size(), false);
        DestroyReadbackResources();
    }

    void Renderer::RequestReadbackResize(VkExtent2D targetExtent, bool force)
    {
        if (!m_ReadbackEnabled)
        {
            return;
        }

        // Cache the incoming request and mark the staging buffers dirty when the viewport geometry changes.
        VkExtent2D l_TargetExtent = targetExtent;
        if (l_TargetExtent.width == 0 || l_TargetExtent.height == 0)
        {
            l_TargetExtent = m_Swapchain.GetExtent();
        }

        const bool l_ExtentChanged = (l_TargetExtent.width != m_LastReadbackExtent.width) || (l_TargetExtent.height != m_LastReadbackExtent.height);
        const bool l_ImageCountChanged = (m_FrameReadbackBuffers.size() != m_Swapchain.GetImageCount());

        if (!force && !l_ExtentChanged && !l_ImageCountChanged)
        {
            return;
        }

        m_PendingReadbackExtent = l_TargetExtent;
        m_ReadbackResizePending = true;
    }

    void Renderer::ApplyPendingReadbackResize()
    {
        if (!m_ReadbackResizePending)
        {
            return;
        }

        // Block until the GPU is idle before rebuilding staging buffers so in-flight frames are not disrupted.
        vkDeviceWaitIdle(Startup::GetDevice());
        CreateOrResizeReadbackResources(m_PendingReadbackExtent);
        m_LastReadbackExtent = m_FrameReadbackExtent;
        m_ReadbackResizePending = false;
    }

    void Renderer::CreateOrResizeReadbackResources()
    {
        // Preserve the legacy entry point while routing the resize logic through the viewport-aware overload.
        CreateOrResizeReadbackResources(m_Swapchain.GetExtent());
    }

    void Renderer::CreateOrResizeReadbackResources(VkExtent2D targetExtent)
    {
        // Prefer the provided target extent (typically the primary render target) so staging buffers match the rendered viewport.
        VkExtent2D l_TargetExtent = targetExtent;
        if (l_TargetExtent.width == 0 || l_TargetExtent.height == 0)
        {
            // Fall back to the swapchain size so readback remains available even when no viewport is active.
            l_TargetExtent = m_Swapchain.GetExtent();
        }
        const uint32_t l_ImageCount = m_Swapchain.GetImageCount();
        const SwapchainFormatInfo l_FormatInfo = QuerySwapchainFormatInfo(m_Swapchain.GetImageFormat());

        if (l_TargetExtent.width == 0 || l_TargetExtent.height == 0 || l_ImageCount == 0 || l_FormatInfo.m_BytesPerPixel == 0 || l_FormatInfo.m_ChannelCount == 0)
        {
            if (l_TargetExtent.width > 0 && l_TargetExtent.height > 0 && l_FormatInfo.m_BytesPerPixel == 0)
            {
                TR_CORE_WARN("Swapchain format {} is not supported for frame readback yet.", static_cast<int32_t>(m_Swapchain.GetImageFormat()));
            }

            // Either the swapchain is minimised or we encountered an unsupported format; clear any stale allocations.
            vkDeviceWaitIdle(Startup::GetDevice());
            DestroyReadbackResources();

            m_LastReadbackExtent = { 0, 0 };

            return;
        }

        const VkDeviceSize l_BufferSize = static_cast<VkDeviceSize>(l_TargetExtent.width) * static_cast<VkDeviceSize>(l_TargetExtent.height) * l_FormatInfo.m_BytesPerPixel;

        const bool l_MatchingExtent = (m_FrameReadbackExtent.width == l_TargetExtent.width) && (m_FrameReadbackExtent.height == l_TargetExtent.height);
        const bool l_MatchingSize = (m_FrameReadbackBufferSize == l_BufferSize);
        const bool l_MatchingCount = (m_FrameReadbackBuffers.size() == l_ImageCount);

        m_LastReadbackExtent = l_TargetExtent;
        if (l_MatchingExtent && l_MatchingSize && l_MatchingCount)
        {
            return;
        }

        DestroyReadbackResources();

        m_FrameReadbackBuffers.resize(l_ImageCount, VK_NULL_HANDLE);
        m_FrameReadbackMemory.resize(l_ImageCount, VK_NULL_HANDLE);
        m_FrameReadbackPending.assign(l_ImageCount, false);

        for (uint32_t it_Index = 0; it_Index < l_ImageCount; ++it_Index)
        {
            VkBuffer l_Buffer = VK_NULL_HANDLE;
            VkDeviceMemory l_Memory = VK_NULL_HANDLE;
            m_Buffers.CreateBuffer(l_BufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, l_Buffer, l_Memory);

            m_FrameReadbackBuffers[it_Index] = l_Buffer;
            m_FrameReadbackMemory[it_Index] = l_Memory;
        }

        m_FrameReadbackExtent = l_TargetExtent;
        m_FrameReadbackBufferSize = l_BufferSize;
        m_FrameReadbackBytesPerPixel = l_FormatInfo.m_BytesPerPixel;
        m_FrameReadbackChannelCount = l_FormatInfo.m_ChannelCount;
        m_FrameReadbackChannelMapping = l_FormatInfo.m_ChannelMapping;
        m_ReadbackConfigurationWarningIssued = false;

        TR_CORE_TRACE("Frame readback staging resized to {}x{} ({} bytes per pixel, {} buffers)", l_TargetExtent.width, l_TargetExtent.height,
            l_FormatInfo.m_BytesPerPixel, l_ImageCount);
    }

    void Renderer::DestroyReadbackResources()
    {
        for (size_t it_Index = 0; it_Index < m_FrameReadbackBuffers.size(); ++it_Index)
        {
            VkBuffer& l_Buffer = m_FrameReadbackBuffers[it_Index];
            VkDeviceMemory l_Memory = (it_Index < m_FrameReadbackMemory.size()) ? m_FrameReadbackMemory[it_Index] : VK_NULL_HANDLE;
            if (l_Buffer != VK_NULL_HANDLE || l_Memory != VK_NULL_HANDLE)
            {
                m_Buffers.DestroyBuffer(l_Buffer, l_Memory);
            }
        }

        m_FrameReadbackBuffers.clear();
        m_FrameReadbackMemory.clear();
        m_FrameReadbackPending.clear();
        m_FrameReadbackExtent = { 0, 0 };
        m_LastReadbackExtent = { 0, 0 };
        m_FrameReadbackBufferSize = 0;
        m_FrameReadbackBytesPerPixel = 0;
        m_FrameReadbackChannelCount = 0;
        m_FrameReadbackChannelMapping = { 0, 1, 2, 3 };
        m_ReadbackConfigurationWarningIssued = false;
        m_PendingFrameReadback.clear();
        m_PendingFrameReadbackBytes.clear();
    }

    void Renderer::ResolvePendingReadback(uint32_t imageIndex, std::chrono::system_clock::time_point captureTimestamp)
    {
        if (!m_ReadbackEnabled)
        {
            return;
        }

        if (imageIndex >= m_FrameReadbackBuffers.size())
        {
            return;
        }

        if (!m_FrameReadbackPending[imageIndex])
        {
            return;
        }

        const bool l_HasExtent = (m_FrameReadbackExtent.width > 0) && (m_FrameReadbackExtent.height > 0);
        const bool l_HasBuffer = (m_FrameReadbackBufferSize > 0);
        const bool l_HasChannels = (m_FrameReadbackChannelCount > 0);

        if (!l_HasExtent || !l_HasBuffer || !l_HasChannels)
        {
            if (!m_ReadbackConfigurationWarningIssued)
            {
                TR_CORE_WARN("Deferred frame readback is waiting for valid configuration (extent {}x{}, buffer {}, channels {}).", m_FrameReadbackExtent.width,
                    m_FrameReadbackExtent.height, m_FrameReadbackBufferSize, m_FrameReadbackChannelCount);
                m_ReadbackConfigurationWarningIssued = true;
            }

            // Keep the pending flag intact so the frame is not silently dropped while resources are recreated.
            return;
        }

        VkDevice l_Device = Startup::GetDevice();
        void* l_Mapped = nullptr;
        if (vkMapMemory(l_Device, m_FrameReadbackMemory[imageIndex], 0, m_FrameReadbackBufferSize, 0, &l_Mapped) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to map frame readback buffer {}", imageIndex);
            m_FrameReadbackPending[imageIndex] = false;
            return;
        }

        const uint8_t* l_SourceBytes = static_cast<const uint8_t*>(l_Mapped);
        const size_t l_PixelCount = static_cast<size_t>(m_FrameReadbackExtent.width) * static_cast<size_t>(m_FrameReadbackExtent.height);
        const size_t l_TotalElements = l_PixelCount * static_cast<size_t>(m_FrameReadbackChannelCount);

        m_PendingFrameReadback.resize(l_TotalElements);
        m_PendingFrameReadbackBytes.resize(l_TotalElements);

        const float l_Normalise = 1.0f / 255.0f;
        const uint32_t l_ChannelCount = m_FrameReadbackChannelCount;
        const bool l_ApplyMapping = (l_ChannelCount == 4u) && (m_FrameReadbackBytesPerPixel >= 4u);

        for (size_t it_PixelIndex = 0; it_PixelIndex < l_PixelCount; ++it_PixelIndex)
        {
            const size_t l_SourceOffset = it_PixelIndex * static_cast<size_t>(m_FrameReadbackBytesPerPixel);
            const size_t l_DestinationOffset = it_PixelIndex * static_cast<size_t>(l_ChannelCount);

            for (uint32_t it_Channel = 0; it_Channel < l_ChannelCount; ++it_Channel)
            {
                uint32_t l_SourceChannel = l_ApplyMapping ? m_FrameReadbackChannelMapping[it_Channel] : it_Channel;

                // Protect against unexpected formats by falling back to the original order.
                if (l_SourceChannel >= m_FrameReadbackBytesPerPixel)
                {
                    l_SourceChannel = it_Channel;
                }

                const uint8_t l_Value = l_SourceBytes[l_SourceOffset + static_cast<size_t>(l_SourceChannel)];

                // AI consumers expect normalised floats in RGBA order.
                m_PendingFrameReadback[l_DestinationOffset + it_Channel] = static_cast<float>(l_Value) * l_Normalise;

                // Video encoder expects raw bytes in RGBA order so it always feeds FFmpeg the same layout.
                m_PendingFrameReadbackBytes[l_DestinationOffset + it_Channel] = l_Value;
            }
        }

        vkUnmapMemory(l_Device, m_FrameReadbackMemory[imageIndex]);
        m_FrameReadbackPending[imageIndex] = false;
        m_LastReadbackTimestamp = captureTimestamp;
    }

    bool Renderer::EnsureAiTextureResources(VkExtent2D extent)
    {
        if (extent.width == 0 || extent.height == 0)
        {
            return false;
        }

        const VkDeviceSize l_RequiredBytes = static_cast<VkDeviceSize>(extent.width) * static_cast<VkDeviceSize>(extent.height) * 4ull;

        if ((m_AiTextureImage != VK_NULL_HANDLE) && (extent.width != m_AiTextureExtent.width || extent.height != m_AiTextureExtent.height))
        {
            // Resolution changed, destroy the previous GPU resources before allocating replacements.
            DestroyAiResources();
        }

        if (m_AiTextureImage == VK_NULL_HANDLE)
        {
            VkImageCreateInfo l_ImageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
            l_ImageInfo.imageType = VK_IMAGE_TYPE_2D;
            l_ImageInfo.extent = { extent.width, extent.height, 1u };
            l_ImageInfo.mipLevels = 1;
            l_ImageInfo.arrayLayers = 1;
            l_ImageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
            l_ImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            l_ImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            l_ImageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            l_ImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            l_ImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

            if (vkCreateImage(Startup::GetDevice(), &l_ImageInfo, nullptr, &m_AiTextureImage) != VK_SUCCESS)
            {
                TR_CORE_CRITICAL("Failed to create AI interpolation image");
                DestroyAiResources();

                return false;
            }

            VkMemoryRequirements l_Requirements{};
            vkGetImageMemoryRequirements(Startup::GetDevice(), m_AiTextureImage, &l_Requirements);

            VkMemoryAllocateInfo l_AllocInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
            l_AllocInfo.allocationSize = l_Requirements.size;
            l_AllocInfo.memoryTypeIndex = m_Buffers.FindMemoryType(l_Requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

            if (vkAllocateMemory(Startup::GetDevice(), &l_AllocInfo, nullptr, &m_AiTextureMemory) != VK_SUCCESS)
            {
                TR_CORE_CRITICAL("Failed to allocate memory for AI interpolation image");
                DestroyAiResources();

                return false;
            }

            vkBindImageMemory(Startup::GetDevice(), m_AiTextureImage, m_AiTextureMemory, 0);

            VkImageViewCreateInfo l_ViewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
            l_ViewInfo.image = m_AiTextureImage;
            l_ViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            l_ViewInfo.format = l_ImageInfo.format;
            l_ViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            l_ViewInfo.subresourceRange.baseMipLevel = 0;
            l_ViewInfo.subresourceRange.levelCount = 1;
            l_ViewInfo.subresourceRange.baseArrayLayer = 0;
            l_ViewInfo.subresourceRange.layerCount = 1;

            if (vkCreateImageView(Startup::GetDevice(), &l_ViewInfo, nullptr, &m_AiTextureView) != VK_SUCCESS)
            {
                TR_CORE_CRITICAL("Failed to create AI interpolation image view");
                DestroyAiResources();

                return false;
            }

            VkSamplerCreateInfo l_SamplerInfo{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
            l_SamplerInfo.magFilter = VK_FILTER_LINEAR;
            l_SamplerInfo.minFilter = VK_FILTER_LINEAR;
            l_SamplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            l_SamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            l_SamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            l_SamplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            l_SamplerInfo.minLod = 0.0f;
            l_SamplerInfo.maxLod = 0.0f;

            if (vkCreateSampler(Startup::GetDevice(), &l_SamplerInfo, nullptr, &m_AiTextureSampler) != VK_SUCCESS)
            {
                TR_CORE_CRITICAL("Failed to create AI interpolation sampler");
                DestroyAiResources();

                return false;
            }

            m_AiTextureExtent = extent;
            m_AiTextureLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        }

        if (m_AiUploadBuffer == VK_NULL_HANDLE || m_AiUploadBufferSize < l_RequiredBytes)
        {
            if (m_AiUploadBuffer != VK_NULL_HANDLE)
            {
                m_Buffers.DestroyBuffer(m_AiUploadBuffer, m_AiUploadMemory);
            }

            m_Buffers.CreateBuffer(l_RequiredBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_AiUploadBuffer, m_AiUploadMemory);

            if (m_AiUploadBuffer == VK_NULL_HANDLE)
            {
                TR_CORE_CRITICAL("Failed to allocate AI interpolation staging buffer");
                DestroyAiResources();

                return false;
            }

            m_AiUploadBufferSize = l_RequiredBytes;
        }

        return (m_AiTextureImage != VK_NULL_HANDLE) && (m_AiUploadBuffer != VK_NULL_HANDLE);
    }

    void Renderer::DestroyAiResources()
    {
        if (m_AiUploadBuffer != VK_NULL_HANDLE || m_AiUploadMemory != VK_NULL_HANDLE)
        {
            m_Buffers.DestroyBuffer(m_AiUploadBuffer, m_AiUploadMemory);
        }

        if (m_AiTextureSampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(Startup::GetDevice(), m_AiTextureSampler, nullptr);
        }

        if (m_AiTextureView != VK_NULL_HANDLE)
        {
            vkDestroyImageView(Startup::GetDevice(), m_AiTextureView, nullptr);
        }

        if (m_AiTextureImage != VK_NULL_HANDLE)
        {
            vkDestroyImage(Startup::GetDevice(), m_AiTextureImage, nullptr);
        }

        if (m_AiTextureMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(Startup::GetDevice(), m_AiTextureMemory, nullptr);
        }

        m_AiUploadBuffer = VK_NULL_HANDLE;
        m_AiUploadMemory = VK_NULL_HANDLE;
        m_AiUploadBufferSize = 0;
        m_AiTextureSampler = VK_NULL_HANDLE;
        m_AiTextureView = VK_NULL_HANDLE;
        m_AiTextureImage = VK_NULL_HANDLE;
        m_AiTextureMemory = VK_NULL_HANDLE;
        m_AiTextureExtent = { 0, 0 };
        m_AiTextureLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        m_AiTextureReady = false;
        m_AiDebugStats.m_TextureReady = false;
        m_AiDebugStats.m_TextureExtent = { 0, 0 };

        if (!m_AiInterpolationBuffer.empty())
        {
            // Preserve the most recent AI frame so we can re-upload once new resources are ready.
            m_AiTextureDirty = true;
        }

        UpdateAiDescriptorBinding();
    }

    void Renderer::UploadAiInterpolationToGpu()
    {
        // Keep texture related debug values aligned with the descriptor state so the UI can react immediately.
        const auto a_UpdateTextureStats = [this]()
            {
                m_AiDebugStats.m_TextureReady = m_AiTextureReady;
                m_AiDebugStats.m_TextureExtent = m_AiTextureExtent;
            };

        if (!m_AiTextureDirty)
        {
            return;
        }

        if (m_AiInterpolationBuffer.empty())
        {
            m_AiTextureDirty = false;
            m_AiTextureReady = false;

            UpdateAiDescriptorBinding();
            a_UpdateTextureStats();

            return;
        }

        if (m_FrameReadbackExtent.width == 0 || m_FrameReadbackExtent.height == 0 || m_FrameReadbackChannelCount == 0)
        {
            a_UpdateTextureStats();

            return;
        }

        const size_t l_PixelCount = static_cast<size_t>(m_FrameReadbackExtent.width) * static_cast<size_t>(m_FrameReadbackExtent.height);
        const size_t l_ExpectedElements = l_PixelCount * static_cast<size_t>(m_FrameReadbackChannelCount);

        if (m_AiInterpolationBuffer.size() < l_ExpectedElements)
        {
            TR_CORE_WARN("AI interpolation buffer contained {} elements but {} were required to fill the GPU texture.", m_AiInterpolationBuffer.size(), l_ExpectedElements);
            m_AiTextureDirty = false;
            m_AiTextureReady = false;

            UpdateAiDescriptorBinding();
            a_UpdateTextureStats();

            return;
        }

        if (!EnsureAiTextureResources(m_FrameReadbackExtent))
        {
            a_UpdateTextureStats();

            return;
        }

        std::vector<uint8_t> l_PackedPixels;
        l_PackedPixels.resize(l_PixelCount * 4u);

        const size_t l_ChannelCount = static_cast<size_t>(m_FrameReadbackChannelCount);
        for (size_t it_Pixel = 0; it_Pixel < l_PixelCount; ++it_Pixel)
        {
            const size_t l_BaseIndex = it_Pixel * l_ChannelCount;
            for (size_t it_Channel = 0; it_Channel < 4; ++it_Channel)
            {
                float l_Value = 0.0f;
                if (it_Channel < l_ChannelCount)
                {
                    l_Value = m_AiInterpolationBuffer[l_BaseIndex + it_Channel];
                }
                else if (it_Channel == 3)
                {
                    l_Value = 1.0f; // Default alpha to opaque when the network omits it.
                }

                l_Value = std::clamp(l_Value, 0.0f, 1.0f);
                l_PackedPixels[it_Pixel * 4u + it_Channel] = static_cast<uint8_t>(std::round(l_Value * 255.0f));
            }
        }

        void* l_Mapped = nullptr;
        if (vkMapMemory(Startup::GetDevice(), m_AiUploadMemory, 0, m_AiUploadBufferSize, 0, &l_Mapped) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to map AI interpolation staging buffer");
            a_UpdateTextureStats();

            return;
        }

        std::memcpy(l_Mapped, l_PackedPixels.data(), l_PackedPixels.size());
        vkUnmapMemory(Startup::GetDevice(), m_AiUploadMemory);

        VkCommandBuffer l_CommandBuffer = m_Commands.BeginSingleTimeCommands();

        VkImageMemoryBarrier l_PrepareBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        l_PrepareBarrier.oldLayout = m_AiTextureLayout;
        l_PrepareBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        l_PrepareBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        l_PrepareBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        l_PrepareBarrier.image = m_AiTextureImage;
        l_PrepareBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        l_PrepareBarrier.subresourceRange.baseMipLevel = 0;
        l_PrepareBarrier.subresourceRange.levelCount = 1;
        l_PrepareBarrier.subresourceRange.baseArrayLayer = 0;
        l_PrepareBarrier.subresourceRange.layerCount = 1;
        l_PrepareBarrier.srcAccessMask = (m_AiTextureLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) ? VK_ACCESS_SHADER_READ_BIT : 0;
        l_PrepareBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        // Transition from the previous shader-read layout (if any) so the copy can safely overwrite the image contents.
        vkCmdPipelineBarrier(l_CommandBuffer, (m_AiTextureLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &l_PrepareBarrier);

        VkBufferImageCopy l_CopyRegion{};
        l_CopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        l_CopyRegion.imageSubresource.baseArrayLayer = 0;
        l_CopyRegion.imageSubresource.layerCount = 1;
        l_CopyRegion.imageExtent = { m_AiTextureExtent.width, m_AiTextureExtent.height, 1 };
        l_CopyRegion.bufferOffset = 0;

        vkCmdCopyBufferToImage(l_CommandBuffer, m_AiUploadBuffer, m_AiTextureImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &l_CopyRegion);

        VkImageMemoryBarrier l_ToShader{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        l_ToShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        l_ToShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        l_ToShader.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        l_ToShader.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        l_ToShader.image = m_AiTextureImage;
        l_ToShader.subresourceRange = l_PrepareBarrier.subresourceRange;
        l_ToShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        l_ToShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        // Make the freshly uploaded pixels visible to fragment shader sampling before the next draw begins.
        vkCmdPipelineBarrier(l_CommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &l_ToShader);

        m_Commands.EndSingleTimeCommands(l_CommandBuffer);

        m_AiTextureLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        m_AiTextureDirty = false;
        m_AiTextureReady = true;

        UpdateAiDescriptorBinding();
        a_UpdateTextureStats();
    }

    void Renderer::UpdateAiDescriptorBinding()
    {
        if (m_DescriptorSets.empty())
        {
            return;
        }

        VkDescriptorImageInfo l_ImageInfo{};
        if (m_AiTextureReady && m_AiTextureView != VK_NULL_HANDLE && m_AiTextureSampler != VK_NULL_HANDLE)
        {
            l_ImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            l_ImageInfo.imageView = m_AiTextureView;
            l_ImageInfo.sampler = m_AiTextureSampler;
        }
        else if (!m_TextureSlots.empty())
        {
            l_ImageInfo = m_TextureSlots.front().m_Descriptor;
        }
        else
        {
            l_ImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            l_ImageInfo.imageView = VK_NULL_HANDLE;
            l_ImageInfo.sampler = VK_NULL_HANDLE;
        }

        std::vector<VkWriteDescriptorSet> l_Writes;
        l_Writes.reserve(m_DescriptorSets.size());

        for (VkDescriptorSet l_Set : m_DescriptorSets)
        {
            VkWriteDescriptorSet l_Write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            l_Write.dstSet = l_Set;
            l_Write.dstBinding = 5;
            l_Write.dstArrayElement = 0;
            l_Write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            l_Write.descriptorCount = 1;
            l_Write.pImageInfo = &l_ImageInfo;
            l_Writes.push_back(l_Write);
        }

        vkUpdateDescriptorSets(Startup::GetDevice(), static_cast<uint32_t>(l_Writes.size()), l_Writes.data(), 0, nullptr);
    }

    std::optional<std::filesystem::path> Renderer::ResolveAiModelPath() const
    {
        // Allow developers to override the bundled sample model when iterating on higher quality alternatives.
        const char* l_EnvironmentModel = std::getenv("TRIDENT_AI_MODEL");
        if (l_EnvironmentModel != nullptr && l_EnvironmentModel[0] != '\0')
        {
            const std::filesystem::path l_Candidate{ l_EnvironmentModel };
            if (std::filesystem::exists(l_Candidate))
            {
                return std::filesystem::absolute(l_Candidate);
            }

            TR_CORE_WARN("TRIDENT_AI_MODEL pointed at '{}' but the file does not exist. Falling back to default search paths.", l_Candidate.string());
        }

        // Search a handful of likely roots so both local runs and packaged builds resolve the shipped sample consistently.
        const std::array<std::filesystem::path, 3> l_SearchRoots =
        {
            std::filesystem::current_path(),
            std::filesystem::current_path().parent_path(),
            std::filesystem::current_path().parent_path().parent_path()
        };

        for (const std::filesystem::path& it_Root : l_SearchRoots)
        {
            if (it_Root.empty())
            {
                continue;
            }

            const std::filesystem::path l_DefaultPath = it_Root / "Assets" / "AI" / "frame_generator.onnx";
            if (std::filesystem::exists(l_DefaultPath))
            {
                return std::filesystem::absolute(l_DefaultPath);
            }
        }

        // No model was found; the renderer can still run while we wire the asset pipeline to supply one.
        return std::nullopt;
    }

    void Renderer::UploadMesh(const std::vector<Geometry::Mesh>& meshes, const std::vector<Geometry::Material>& materials, const std::vector<std::string>& textures)
    {
        // Persist the latest geometry so subsequent drag-and-drop operations can rebuild GPU buffers without reloading from disk.
        m_GeometryCache = meshes;
        m_Materials = materials;

        // Resolve texture slots for every material so the fragment shader can index the descriptor array safely.
        ResolveMaterialTextureSlots(textures, 0, m_Materials.size());

        UploadMeshFromCache();
    }

    void Renderer::AppendMeshes(std::vector<Geometry::Mesh> meshes, std::vector<Geometry::Material> materials, std::vector<std::string> textures)
    {
        if (meshes.empty())
        {
            return;
        }

        const size_t l_OldMaterialCount = m_Materials.size();
        m_GeometryCache.reserve(m_GeometryCache.size() + meshes.size());

        for (Geometry::Mesh& it_Mesh : meshes)
        {
            if (it_Mesh.MaterialIndex >= 0)
            {
                it_Mesh.MaterialIndex += static_cast<int32_t>(l_OldMaterialCount);
            }
            m_GeometryCache.emplace_back(std::move(it_Mesh));
        }

        const size_t l_NewMaterialOffset = m_Materials.size();
        m_Materials.reserve(m_Materials.size() + materials.size());
        for (Geometry::Material& it_Material : materials)
        {
            m_Materials.emplace_back(std::move(it_Material));
        }

        // Texture indices stored on the incoming materials refer to the provided texture array. Resolve the
        // GPU slot mapping now so draws issued after the upload can reference the correct descriptor.
        ResolveMaterialTextureSlots(textures, l_NewMaterialOffset, materials.size());

        UploadMeshFromCache();
    }

    size_t Renderer::GetOrCreatePrimitiveMeshIndex(MeshComponent::PrimitiveType primitiveType)
    {
        const size_t l_InvalidMeshIndex = std::numeric_limits<size_t>::max();
        if (primitiveType == MeshComponent::PrimitiveType::None)
        {
            return l_InvalidMeshIndex;
        }

        const size_t l_PrimitiveSlot = static_cast<size_t>(primitiveType) - 1;
        if (l_PrimitiveSlot >= m_PrimitiveMeshIndices.size())
        {
            return l_InvalidMeshIndex;
        }

        const size_t l_ExistingIndex = m_PrimitiveMeshIndices[l_PrimitiveSlot];
        if (l_ExistingIndex != l_InvalidMeshIndex && l_ExistingIndex < m_GeometryCache.size())
        {
            return l_ExistingIndex;
        }

        const size_t l_PrimitiveIndex = CreatePrimitiveMeshInCache(primitiveType);
        if (l_PrimitiveIndex == l_InvalidMeshIndex)
        {
            return l_InvalidMeshIndex;
        }

        if (!m_IsUploadingMeshes)
        {
            // Upload the new primitive geometry now so draw calls can reference valid buffers.
            UploadMeshFromCache();
        }

        return l_PrimitiveIndex;
    }

    size_t Renderer::CreatePrimitiveMeshInCache(MeshComponent::PrimitiveType primitiveType)
    {
        const size_t l_InvalidMeshIndex = std::numeric_limits<size_t>::max();
        if (primitiveType == MeshComponent::PrimitiveType::None)
        {
            return l_InvalidMeshIndex;
        }

        const size_t l_PrimitiveSlot = static_cast<size_t>(primitiveType) - 1;
        if (l_PrimitiveSlot >= m_PrimitiveMeshIndices.size())
        {
            return l_InvalidMeshIndex;
        }

        const size_t l_ExistingIndex = m_PrimitiveMeshIndices[l_PrimitiveSlot];
        if (l_ExistingIndex != l_InvalidMeshIndex && l_ExistingIndex < m_GeometryCache.size())
        {
            return l_ExistingIndex;
        }

        Geometry::Mesh l_PrimitiveMesh{};
        switch (primitiveType)
        {
        case MeshComponent::PrimitiveType::Cube:
            l_PrimitiveMesh = BuildPrimitiveCubeMesh();
            break;
        case MeshComponent::PrimitiveType::Sphere:
            l_PrimitiveMesh = BuildPrimitiveSphereMesh();
            break;
        case MeshComponent::PrimitiveType::Quad:
            l_PrimitiveMesh = BuildPrimitiveQuadMesh();
            break;
        default:
            return l_InvalidMeshIndex;
        }

        const int32_t l_MaterialIndex = static_cast<int32_t>(m_Materials.size());
        Geometry::Material l_Material{};
        l_Material.BaseColorFactor = { 1.0f, 1.0f, 1.0f, 1.0f };
        l_Material.MetallicFactor = 0.0f;
        l_Material.RoughnessFactor = 1.0f;
        l_Material.BaseColorTextureSlot = 0;
        m_Materials.push_back(l_Material);

        l_PrimitiveMesh.MaterialIndex = l_MaterialIndex;
        m_GeometryCache.push_back(std::move(l_PrimitiveMesh));

        const size_t l_NewIndex = m_GeometryCache.size() - 1;
        m_PrimitiveMeshIndices[l_PrimitiveSlot] = l_NewIndex;
        return l_NewIndex;
    }

    void Renderer::EnsurePrimitiveMeshesInCache()
    {
        if (!m_Registry)
        {
            return;
        }

        const auto& l_Entities = m_Registry->GetEntities();
        const size_t l_InvalidMeshIndex = std::numeric_limits<size_t>::max();

        for (ECS::Entity it_Entity : l_Entities)
        {
            if (!m_Registry->HasComponent<MeshComponent>(it_Entity))
            {
                continue;
            }

            MeshComponent& l_Component = m_Registry->GetComponent<MeshComponent>(it_Entity);
            if (l_Component.m_Primitive == MeshComponent::PrimitiveType::None)
            {
                continue;
            }

            if (l_Component.m_MeshIndex != l_InvalidMeshIndex && l_Component.m_MeshIndex < m_GeometryCache.size())
            {
                continue;
            }

            const size_t l_PrimitiveSlot = static_cast<size_t>(l_Component.m_Primitive) - 1;
            if (l_PrimitiveSlot < m_PrimitiveMeshIndices.size())
            {
                const size_t l_KnownIndex = m_PrimitiveMeshIndices[l_PrimitiveSlot];
                if (l_KnownIndex != l_InvalidMeshIndex && l_KnownIndex < m_GeometryCache.size())
                {
                    // Reuse the cached primitive mesh so entities can render without reimporting assets.
                    l_Component.m_MeshIndex = l_KnownIndex;
                    continue;
                }
            }

            const size_t l_NewIndex = CreatePrimitiveMeshInCache(l_Component.m_Primitive);
            if (l_NewIndex != l_InvalidMeshIndex)
            {
                // Primitives now cache mesh indices so upload and draw paths can treat them like imported meshes.
                l_Component.m_MeshIndex = l_NewIndex;
            }
        }
    }

    void Renderer::UploadMeshFromCache()
    {
        struct UploadGuard
        {
            bool& m_Flag;
            explicit UploadGuard(bool& flag) : m_Flag(flag)
            {
                m_Flag = true;
            }
            ~UploadGuard()
            {
                m_Flag = false;
            }
        };

        UploadGuard l_UploadGuard(m_IsUploadingMeshes);
        // Ensure no GPU operations are using the old buffers before reallocating resources.
        vkWaitForFences(Startup::GetDevice(), 1, &m_ResourceFence, VK_TRUE, UINT64_MAX);
        
        // Prebuild primitive meshes so this upload includes their geometry and draw metadata.
        EnsurePrimitiveMeshesInCache();

        // Ensure the GPU-visible material table matches the CPU cache before geometry uploads begin.
        EnsureMaterialBufferCapacity(m_Materials.size());
        MarkMaterialBuffersDirty();

        if (m_VertexBuffer != VK_NULL_HANDLE)
        {
            m_Buffers.DestroyBuffer(m_VertexBuffer, m_VertexBufferMemory);
            m_VertexBuffer = VK_NULL_HANDLE;
            m_VertexBufferMemory = VK_NULL_HANDLE;
        }

        if (m_IndexBuffer != VK_NULL_HANDLE)
        {
            m_Buffers.DestroyBuffer(m_IndexBuffer, m_IndexBufferMemory);
            m_IndexBuffer = VK_NULL_HANDLE;
            m_IndexBufferMemory = VK_NULL_HANDLE;
            m_IndexCount = 0;
        }

        const auto& l_Meshes = m_GeometryCache;

        size_t l_VertexCount = 0;
        size_t l_IndexCount = 0;
        for (const auto& it_Mesh : l_Meshes)
        {
            l_VertexCount += it_Mesh.Vertices.size();
            l_IndexCount += it_Mesh.Indices.size();
        }

        if (l_VertexCount > m_MaxVertexCount)
        {
            m_MaxVertexCount = l_VertexCount;
            m_StagingVertices.reset(new Vertex[m_MaxVertexCount]);
        }
        if (l_IndexCount > m_MaxIndexCount)
        {
            m_MaxIndexCount = l_IndexCount;
            m_StagingIndices.reset(new uint32_t[m_MaxIndexCount]);
        }

        size_t l_VertOffset = 0;
        size_t l_IndexOffset = 0;
        for (const auto& it_Mesh : l_Meshes)
        {
            std::copy(it_Mesh.Vertices.begin(), it_Mesh.Vertices.end(), m_StagingVertices.get() + l_VertOffset);
            for (auto index : it_Mesh.Indices)
            {
                // Preserve the mesh-local indices and rely on the base vertex during draw submission.
                // Offsetting the index here as well as supplying a base vertex later would double-apply
                // the vertex offset and corrupt geometry once multiple meshes share the combined buffers.
                m_StagingIndices[l_IndexOffset++] = index;
            }
            l_VertOffset += it_Mesh.Vertices.size();
        }

        std::vector<Vertex> l_AllVertices(m_StagingVertices.get(), m_StagingVertices.get() + l_VertexCount);
        std::vector<uint32_t> l_AllIndices(m_StagingIndices.get(), m_StagingIndices.get() + l_IndexCount);

        // Upload the combined geometry once per load so every mesh can share the same GPU buffers.
        if (!l_AllVertices.empty())
        {
            m_Buffers.CreateVertexBuffer(l_AllVertices, m_Commands.GetOneTimePool(), m_VertexBuffer, m_VertexBufferMemory);
        }
        if (!l_AllIndices.empty())
        {
            m_Buffers.CreateIndexBuffer(l_AllIndices, m_Commands.GetOneTimePool(), m_IndexBuffer, m_IndexBufferMemory, m_IndexCount);
        }

        // Record the uploaded index count so the command buffer draw guard can validate pending draws.
        m_IndexCount = static_cast<uint32_t>(l_IndexCount);

        // Cache draw metadata for each mesh so render submissions can address shared buffers safely.
        m_MeshDrawInfo.clear();
        m_MeshDrawInfo.reserve(l_Meshes.size());

        uint32_t l_FirstIndexCursor = 0;
        int32_t l_BaseVertexCursor = 0;
        for (size_t l_MeshIndex = 0; l_MeshIndex < l_Meshes.size(); ++l_MeshIndex)
        {
            const auto& it_Mesh = l_Meshes[l_MeshIndex];
            MeshDrawInfo l_DrawInfo{};
            l_DrawInfo.m_FirstIndex = l_FirstIndexCursor;
            l_DrawInfo.m_IndexCount = static_cast<uint32_t>(it_Mesh.Indices.size());
            l_DrawInfo.m_BaseVertex = l_BaseVertexCursor;
            l_DrawInfo.m_MaterialIndex = it_Mesh.MaterialIndex;

            m_MeshDrawInfo.push_back(l_DrawInfo);

            l_FirstIndexCursor += l_DrawInfo.m_IndexCount;
            l_BaseVertexCursor += static_cast<int32_t>(it_Mesh.Vertices.size());
        }

        // Clear any cached draw list so the next frame rebuilds commands using the fresh offsets.
        m_MeshDrawCommands.clear();

        if (m_Registry)
        {
            const auto& l_Entities = m_Registry->GetEntities();
            for (ECS::Entity it_Entity : l_Entities)
            {
                if (!m_Registry->HasComponent<MeshComponent>(it_Entity))
                {
                    continue;
                }

                MeshComponent& l_Component = m_Registry->GetComponent<MeshComponent>(it_Entity);
                if (l_Component.m_Primitive != MeshComponent::PrimitiveType::None && l_Component.m_MeshIndex == std::numeric_limits<size_t>::max())
                {
                    // Primitives are expected to resolve mesh indices during cache preparation.
                    continue;
                }

                if (l_Component.m_MeshIndex >= m_MeshDrawInfo.size())
                {
                    continue;
                }

                const MeshDrawInfo& l_DrawInfo = m_MeshDrawInfo[l_Component.m_MeshIndex];
                l_Component.m_FirstIndex = l_DrawInfo.m_FirstIndex;
                l_Component.m_IndexCount = l_DrawInfo.m_IndexCount;
                l_Component.m_BaseVertex = l_DrawInfo.m_BaseVertex;
                l_Component.m_MaterialIndex = l_DrawInfo.m_MaterialIndex;
            }
        }

        m_ModelCount = l_Meshes.size();
        m_TriangleCount = l_IndexCount / 3;

        TR_CORE_INFO("Scene info - Models: {} Triangles: {} Materials: {}", m_ModelCount, m_TriangleCount, m_Materials.size());
    }

    void Renderer::UploadTexture(const std::string& texturePath, const Loader::TextureData& texture)
    {
        std::string l_NormalizedPath = NormalizeTexturePath(texturePath);
        TR_CORE_TRACE("Uploading texture refresh for '{}' ({}x{})", l_NormalizedPath.c_str(), texture.Width, texture.Height);

        if (l_NormalizedPath.empty())
        {
            TR_CORE_WARN("Texture refresh skipped because the provided path was empty.");
            return;
        }

        if (texture.Pixels.empty())
        {
            TR_CORE_WARN("Texture '{}' has no pixel data. Keeping the existing descriptor binding.", l_NormalizedPath.c_str());
            return;
        }

        if (m_ResourceFence != VK_NULL_HANDLE)
        {
            vkWaitForFences(Startup::GetDevice(), 1, &m_ResourceFence, VK_TRUE, UINT64_MAX);
        }

        auto a_Existing = m_TextureSlotLookup.find(l_NormalizedPath);
        if (a_Existing == m_TextureSlotLookup.end())
        {
            if (m_TextureSlots.size() >= Pipeline::s_MaxMaterialTextures)
            {
                TR_CORE_WARN("Texture budget exhausted. Unable to hot-reload '{}'.", l_NormalizedPath.c_str());
                return;
            }

            TextureSlot l_NewSlot{};
            if (!PopulateTextureSlot(l_NewSlot, texture))
            {
                TR_CORE_WARN("Failed to upload new texture data for '{}'. Keeping previous bindings.", l_NormalizedPath.c_str());
                return;
            }

            l_NewSlot.m_SourcePath = l_NormalizedPath;
            m_TextureSlots.push_back(std::move(l_NewSlot));
            const uint32_t l_NewIndex = static_cast<uint32_t>(m_TextureSlots.size() - 1);
            m_TextureSlotLookup.emplace(l_NormalizedPath, l_NewIndex);
        }
        else
        {
            const uint32_t l_SlotIndex = a_Existing->second;
            if (l_SlotIndex >= m_TextureSlots.size())
            {
                TR_CORE_WARN("Texture cache entry for '{}' referenced an invalid slot. Reverting to default.", l_NormalizedPath.c_str());
                m_TextureSlotLookup[l_NormalizedPath] = 0u;
            }
            else
            {
                DestroyTextureSlot(m_TextureSlots[l_SlotIndex]);

                TextureSlot l_Replacement{};
                if (!PopulateTextureSlot(l_Replacement, texture))
                {
                    TR_CORE_WARN("Failed to refresh texture '{}'. Using the default slot instead.", l_NormalizedPath.c_str());
                    m_TextureSlotLookup[l_NormalizedPath] = 0u;
                }
                else
                {
                    l_Replacement.m_SourcePath = l_NormalizedPath;
                    m_TextureSlots[l_SlotIndex] = std::move(l_Replacement);
                }
            }
        }

        RefreshTextureDescriptorBindings();
    }

    Renderer::ImGuiTexture* Renderer::CreateImGuiTexture(const Loader::TextureData& texture)
    {
        // Icons use the same upload path as standard textures but retain their Vulkan
        // objects so ImGui can sample them every frame. This method centralises the
        // boilerplate and keeps lifetime management within the renderer.
        if (texture.Pixels.empty())
        {
            TR_CORE_WARN("ImGui texture creation skipped because no pixel data was supplied.");

            return nullptr;
        }

        VkDevice l_Device = Startup::GetDevice();

        auto l_TextureStorage = std::make_unique<ImGuiTexture>();
        ImGuiTexture& l_Texture = *l_TextureStorage;

        VkDeviceSize l_ImageSize = static_cast<VkDeviceSize>(texture.Pixels.size());

        VkBuffer l_StagingBuffer = VK_NULL_HANDLE;
        VkDeviceMemory l_StagingMemory = VK_NULL_HANDLE;
        m_Buffers.CreateBuffer(l_ImageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, l_StagingBuffer, l_StagingMemory);

        void* l_Data = nullptr;
        vkMapMemory(l_Device, l_StagingMemory, 0, l_ImageSize, 0, &l_Data);
        memcpy(l_Data, texture.Pixels.data(), static_cast<size_t>(l_ImageSize));
        vkUnmapMemory(l_Device, l_StagingMemory);

        VkImageCreateInfo l_ImageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        l_ImageInfo.imageType = VK_IMAGE_TYPE_2D;
        l_ImageInfo.extent.width = static_cast<uint32_t>(texture.Width);
        l_ImageInfo.extent.height = static_cast<uint32_t>(texture.Height);
        l_ImageInfo.extent.depth = 1;
        l_ImageInfo.mipLevels = 1;
        l_ImageInfo.arrayLayers = 1;
        l_ImageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
        l_ImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        l_ImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        l_ImageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        l_ImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        l_ImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

        if (vkCreateImage(l_Device, &l_ImageInfo, nullptr, &l_Texture.m_Image) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create ImGui texture image");

            m_Buffers.DestroyBuffer(l_StagingBuffer, l_StagingMemory);

            return nullptr;
        }

        VkMemoryRequirements l_MemReq{};
        vkGetImageMemoryRequirements(l_Device, l_Texture.m_Image, &l_MemReq);

        VkMemoryAllocateInfo l_AllocInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        l_AllocInfo.allocationSize = l_MemReq.size;
        l_AllocInfo.memoryTypeIndex = m_Buffers.FindMemoryType(l_MemReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(l_Device, &l_AllocInfo, nullptr, &l_Texture.m_ImageMemory) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to allocate ImGui texture memory");

            vkDestroyImage(l_Device, l_Texture.m_Image, nullptr);
            l_Texture.m_Image = VK_NULL_HANDLE;
            m_Buffers.DestroyBuffer(l_StagingBuffer, l_StagingMemory);

            return nullptr;
        }

        vkBindImageMemory(l_Device, l_Texture.m_Image, l_Texture.m_ImageMemory, 0);

        VkCommandBuffer l_CommandBuffer = m_Commands.BeginSingleTimeCommands();

        VkImageMemoryBarrier l_BarrierToTransfer{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        l_BarrierToTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        l_BarrierToTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        l_BarrierToTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        l_BarrierToTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        l_BarrierToTransfer.image = l_Texture.m_Image;
        l_BarrierToTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        l_BarrierToTransfer.subresourceRange.baseMipLevel = 0;
        l_BarrierToTransfer.subresourceRange.levelCount = 1;
        l_BarrierToTransfer.subresourceRange.baseArrayLayer = 0;
        l_BarrierToTransfer.subresourceRange.layerCount = 1;
        l_BarrierToTransfer.srcAccessMask = 0;
        l_BarrierToTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        vkCmdPipelineBarrier(l_CommandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &l_BarrierToTransfer);

        VkBufferImageCopy l_CopyRegion{};
        l_CopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        l_CopyRegion.imageSubresource.mipLevel = 0;
        l_CopyRegion.imageSubresource.baseArrayLayer = 0;
        l_CopyRegion.imageSubresource.layerCount = 1;
        l_CopyRegion.imageOffset = { 0, 0, 0 };
        l_CopyRegion.imageExtent = { static_cast<uint32_t>(texture.Width), static_cast<uint32_t>(texture.Height), 1 };

        vkCmdCopyBufferToImage(l_CommandBuffer, l_StagingBuffer, l_Texture.m_Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &l_CopyRegion);

        VkImageMemoryBarrier l_BarrierToShader{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        l_BarrierToShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        l_BarrierToShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        l_BarrierToShader.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        l_BarrierToShader.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        l_BarrierToShader.image = l_Texture.m_Image;
        l_BarrierToShader.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        l_BarrierToShader.subresourceRange.baseMipLevel = 0;
        l_BarrierToShader.subresourceRange.levelCount = 1;
        l_BarrierToShader.subresourceRange.baseArrayLayer = 0;
        l_BarrierToShader.subresourceRange.layerCount = 1;
        l_BarrierToShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        l_BarrierToShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(l_CommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &l_BarrierToShader);

        m_Commands.EndSingleTimeCommands(l_CommandBuffer);

        m_Buffers.DestroyBuffer(l_StagingBuffer, l_StagingMemory);

        VkImageViewCreateInfo l_ViewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        l_ViewInfo.image = l_Texture.m_Image;
        l_ViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        l_ViewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
        l_ViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        l_ViewInfo.subresourceRange.baseMipLevel = 0;
        l_ViewInfo.subresourceRange.levelCount = 1;
        l_ViewInfo.subresourceRange.baseArrayLayer = 0;
        l_ViewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(l_Device, &l_ViewInfo, nullptr, &l_Texture.m_ImageView) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create ImGui texture image view");

            DestroyImGuiTexture(l_Texture);

            return nullptr;
        }

        VkSamplerCreateInfo l_SamplerInfo{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        l_SamplerInfo.magFilter = VK_FILTER_LINEAR;
        l_SamplerInfo.minFilter = VK_FILTER_LINEAR;
        l_SamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        l_SamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        l_SamplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        l_SamplerInfo.anisotropyEnable = VK_FALSE;
        l_SamplerInfo.maxAnisotropy = 1.0f;
        l_SamplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        l_SamplerInfo.unnormalizedCoordinates = VK_FALSE;
        l_SamplerInfo.compareEnable = VK_FALSE;
        l_SamplerInfo.compareOp = VK_COMPARE_OP_NEVER;
        l_SamplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        l_SamplerInfo.mipLodBias = 0.0f;
        l_SamplerInfo.minLod = 0.0f;
        l_SamplerInfo.maxLod = 0.0f;

        if (vkCreateSampler(l_Device, &l_SamplerInfo, nullptr, &l_Texture.m_Sampler) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create ImGui texture sampler");

            DestroyImGuiTexture(l_Texture);

            return nullptr;
        }

        l_Texture.m_Descriptor = (ImTextureID)ImGui_ImplVulkan_AddTexture(l_Texture.m_Sampler, l_Texture.m_ImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        l_Texture.m_Extent.width = static_cast<uint32_t>(texture.Width);
        l_Texture.m_Extent.height = static_cast<uint32_t>(texture.Height);

        m_ImGuiTexturePool.push_back(std::move(l_TextureStorage));

        return m_ImGuiTexturePool.back().get();
    }

    void Renderer::DestroyImGuiTexture(ImGuiTexture& texture)
    {
        // Safe guard every handle so the method tolerates partially initialised textures created during error paths.
        if (texture.m_Sampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(Startup::GetDevice(), texture.m_Sampler, nullptr);
            texture.m_Sampler = VK_NULL_HANDLE;
        }

        if (texture.m_ImageView != VK_NULL_HANDLE)
        {
            vkDestroyImageView(Startup::GetDevice(), texture.m_ImageView, nullptr);
            texture.m_ImageView = VK_NULL_HANDLE;
        }

        if (texture.m_Image != VK_NULL_HANDLE)
        {
            vkDestroyImage(Startup::GetDevice(), texture.m_Image, nullptr);
            texture.m_Image = VK_NULL_HANDLE;
        }

        if (texture.m_ImageMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(Startup::GetDevice(), texture.m_ImageMemory, nullptr);
            texture.m_ImageMemory = VK_NULL_HANDLE;
        }

        texture.m_Extent = { 0, 0 };
    }

    void Renderer::SetImGuiLayer(UI::ImGuiLayer* layer)
    {
        m_ImGuiLayer = layer;
    }

    void Renderer::SetEditorCamera(Camera* camera)
    {
        m_EditorCamera = camera;
        if (m_EditorCamera)
        {
            glm::vec2 l_ViewportSize{ static_cast<float>(m_Swapchain.GetExtent().width), static_cast<float>(m_Swapchain.GetExtent().height) };

            // Prefer the dedicated editor viewport (ID 1U) when sizing the camera so the game view never steals its feed.
            if (const ViewportContext* l_EditorContext = FindViewportContext(1U))
            {
                if (IsValidViewport(l_EditorContext->m_Info))
                {
                    l_ViewportSize = l_EditorContext->m_Info.Size;
                }
            }
            else if (const ViewportContext* l_Context = FindViewportContext(m_ActiveViewportId))
            {
                if (IsValidViewport(l_Context->m_Info))
                {
                    l_ViewportSize = l_Context->m_Info.Size;
                }
            }
            else
            {
                for (const std::pair<const uint32_t, ViewportContext>& it_ContextPair : m_ViewportContexts)
                {
                    if (IsValidViewport(it_ContextPair.second.m_Info))
                    {
                        l_ViewportSize = it_ContextPair.second.m_Info.Size;
                        break;
                    }
                }
            }

            // Keep the editor camera aligned with the authoring viewport so manipulator math remains predictable.
            m_EditorCamera->SetViewportSize(l_ViewportSize);
        }
    }

    void Renderer::SetRuntimeCamera(Camera* camera)
    {
        m_RuntimeCamera = camera;
        m_RuntimeCameraReady = false;
        if (!m_RuntimeCamera)
        {
            // Clearing the runtime camera prevents stale frames from showing while the scene searches for a replacement.
            return;
        }

        glm::vec2 l_ViewportSize{ static_cast<float>(m_Swapchain.GetExtent().width), static_cast<float>(m_Swapchain.GetExtent().height) };

        // Runtime output targets the game viewport (ID 2U); fall back to any active viewport if it has not initialised yet.
        if (const ViewportContext* l_GameContext = FindViewportContext(2U))
        {
            if (IsValidViewport(l_GameContext->m_Info))
            {
                l_ViewportSize = l_GameContext->m_Info.Size;
            }
        }
        else if (const ViewportContext* l_Context = FindViewportContext(m_ActiveViewportId))
        {
            if (IsValidViewport(l_Context->m_Info))
            {
                l_ViewportSize = l_Context->m_Info.Size;
            }
        }
        else
        {
            for (const std::pair<const uint32_t, ViewportContext>& it_ContextPair : m_ViewportContexts)
            {
                if (IsValidViewport(it_ContextPair.second.m_Info))
                {
                    l_ViewportSize = it_ContextPair.second.m_Info.Size;
                    break;
                }
            }
        }

        // Keep the runtime camera sized to its viewport so gameplay simulations respect their intended aspect ratio.
        m_RuntimeCamera->SetViewportSize(l_ViewportSize);
    }

    void Renderer::SetRuntimeCameraReady(bool ready)
    {
        // Guard the flag with the pointer so callers cannot accidentally mark a cleared camera as ready.
        m_RuntimeCameraReady = ready && (m_RuntimeCamera != nullptr);
    }

    void Renderer::SetActiveRegistry(ECS::Registry* registry)
    {
        // Persist the registry pointer so draw gathering always references the intended data source (editor vs runtime).
        m_Registry = registry;
        if (m_ViewportCamera != std::numeric_limits<ECS::Entity>::max())
        {
            // Reset the cached viewport camera when the underlying registry changes to avoid dangling entity references.
            m_ViewportCamera = std::numeric_limits<ECS::Entity>::max();
        }
    }

    void Renderer::SetFrameDatasetCaptureEnabled(bool enabled)
    {
        // Allow runtime toggles so tooling can pause dataset capture without requiring environment variable restarts.
        m_FrameDatasetCaptureEnabled = enabled;
        SyncFrameDatasetRecorder();
    }

    void Renderer::SetFrameDatasetCaptureDirectory(const std::filesystem::path& directory)
    {
        // Cache the directory for UI display and ensure the recorder points at a valid path even if the user clears the field.
        if (directory.empty())
        {
            m_FrameDatasetCaptureDirectory = std::filesystem::current_path() / "DatasetCapture";
        }
        else
        {
            m_FrameDatasetCaptureDirectory = directory;
        }

        SyncFrameDatasetRecorder();
    }

    void Renderer::SetFrameDatasetCaptureInterval(uint32_t interval)
    {
        // Clamp the interval to at least one so the modulus check in the recorder remains valid.
        m_FrameDatasetCaptureInterval = std::max<uint32_t>(1, interval);
        m_FrameDatasetRecorder.SetSampleInterval(m_FrameDatasetCaptureInterval);
    }

    void Renderer::SetClearColor(const glm::vec4& color)
    {
        // Persist the preferred clear colour so both render passes remain visually consistent.
        m_ClearColor = color;
    }

    void Renderer::SetAiBlendStrength(float blendStrength)
    {
        const float l_ClampedStrength = std::clamp(blendStrength, 0.0f, 1.0f);
        m_AiBlendStrength = l_ClampedStrength;
        m_AiDebugStats.m_BlendStrength = m_AiBlendStrength;

        // Future revisions may expose curve-driven blending or per-view overrides. Keeping the setter focused for now makes
        // those extensions straightforward without rewriting the plumbing.
    }

    ImTextureID Renderer::GetAiTextureDescriptor() const
    {
        // UI tooling will eventually register an ImGui descriptor for the AI output. Explicitly cast the null handle so
        // MSVC accepts the placeholder value regardless of how ImTextureID is defined (pointer or integral).
        return reinterpret_cast<ImTextureID>(nullptr);
    }

    VkDescriptorSet Renderer::GetViewportTexture(uint32_t viewportID) const
    {
        // Provide the descriptor set that ImGui::Image expects when the viewport is active.
        // The active camera routing happens elsewhere; this helper only surfaces the render target texture to ImGui.
        const ViewportContext* l_Context = FindViewportContext(viewportID);
        if (!l_Context || !IsValidViewport(l_Context->m_Info))
        {
            return VK_NULL_HANDLE;
        }

        const OffscreenTarget& l_Target = l_Context->m_Target;
        if (l_Target.m_TextureID != VK_NULL_HANDLE)
        {
            return l_Target.m_TextureID;
        }

        return VK_NULL_HANDLE;
    }

    const Camera* Renderer::GetActiveCamera() const
    {
        const ViewportContext* l_Context = FindViewportContext(m_ActiveViewportId);
        if (!l_Context)
        {
            // When no context is active prefer the editor camera to keep tools responsive, otherwise fall back to runtime.
            return m_EditorCamera ? m_EditorCamera : m_RuntimeCamera;
        }

        return GetActiveCamera(*l_Context);
    }

    glm::mat4 Renderer::GetViewportViewMatrix(uint32_t viewportID) const
    {
        const ViewportContext* l_Context = FindViewportContext(viewportID);
        const Camera* l_Camera = l_Context ? GetActiveCamera(*l_Context) : nullptr;
        if (!l_Camera)
        {
            return glm::mat4{ 1.0f };
        }

        return l_Camera->GetViewMatrix();
    }

    glm::mat4 Renderer::GetViewportProjectionMatrix(uint32_t viewportID) const
    {
        const ViewportContext* l_Context = FindViewportContext(viewportID);
        const Camera* l_Camera = l_Context ? GetActiveCamera(*l_Context) : nullptr;
        if (!l_Camera)
        {
            return glm::mat4{ 1.0f };
        }

        return l_Camera->GetProjectionMatrix();
    }

    glm::mat4 Renderer::GetEditorCameraViewMatrix() const
    {
        // Always source the editor camera even when runtime cameras are active elsewhere to keep authoring overlays stable.
        if (m_EditorCamera)
        {
            return m_EditorCamera->GetViewMatrix();
        }

        // Fallback to the currently active camera so callers receive something meaningful when no editor camera exists.
        const Camera* l_Camera = GetActiveCamera();
        return l_Camera ? l_Camera->GetViewMatrix() : glm::mat4{ 1.0f };
    }

    glm::mat4 Renderer::GetEditorCameraProjectionMatrix() const
    {
        if (m_EditorCamera)
        {
            return m_EditorCamera->GetProjectionMatrix();
        }

        const Camera* l_Camera = GetActiveCamera();
        return l_Camera ? l_Camera->GetProjectionMatrix() : glm::mat4{ 1.0f };
    }

    std::vector<CameraOverlayInstance> Renderer::GetCameraOverlayInstances(uint32_t viewportID) const
    {
        std::vector<CameraOverlayInstance> l_Instances{};

        const ViewportContext* l_Context = FindViewportContext(viewportID);
        if (!l_Context || !IsValidViewport(l_Context->m_Info))
        {
            // Without a valid viewport the overlay has nowhere to draw, so bail out early.
            return l_Instances;
        }

        if (m_Registry == nullptr)
        {
            // No registry means no entities to evaluate for overlays.
            return l_Instances;
        }

        const Camera* l_Camera = GetActiveCamera(*l_Context);
        if (l_Camera == nullptr)
        {
            // Viewports without an active camera cannot project world-space positions into screen-space.
            return l_Instances;
        }

        const glm::vec2 l_ViewportSize = l_Context->m_Info.Size;
        if (l_ViewportSize.x <= std::numeric_limits<float>::epsilon() || l_ViewportSize.y <= std::numeric_limits<float>::epsilon())
        {
            return l_Instances;
        }

        const glm::mat4 l_ViewProjection = l_Camera->GetProjectionMatrix() * l_Camera->GetViewMatrix();
        const std::vector<ECS::Entity>& l_Entities = m_Registry->GetEntities();
        l_Instances.reserve(l_Entities.size());

        for (ECS::Entity it_Entity : l_Entities)
        {
            if (!m_Registry->HasComponent<CameraComponent>(it_Entity))
            {
                continue;
            }

            if (!m_Registry->HasComponent<Transform>(it_Entity))
            {
                // Cameras without transforms cannot be located in the world yet.
                continue;
            }

            const Transform& l_Transform = m_Registry->GetComponent<Transform>(it_Entity);
            const glm::vec4 l_WorldPosition{ l_Transform.Position, 1.0f };
            const glm::vec4 l_ClipPosition = l_ViewProjection * l_WorldPosition;

            if (std::abs(l_ClipPosition.w) <= std::numeric_limits<float>::epsilon())
            {
                continue;
            }

            const glm::vec3 l_Ndc = glm::vec3(l_ClipPosition) / l_ClipPosition.w;
            if (l_Ndc.z < 0.0f || l_Ndc.z > 1.0f)
            {
                continue;
            }

            if (std::abs(l_Ndc.x) > 1.0f || std::abs(l_Ndc.y) > 1.0f)
            {
                continue;
            }

            CameraOverlayInstance& l_Instance = l_Instances.emplace_back();
            l_Instance.m_Entity = it_Entity;
            l_Instance.m_ScreenPosition.x = (l_Ndc.x * 0.5f + 0.5f) * l_ViewportSize.x;
            l_Instance.m_ScreenPosition.y = (1.0f - (l_Ndc.y * 0.5f + 0.5f)) * l_ViewportSize.y;
            l_Instance.m_Depth = l_Ndc.z;

            const CameraComponent& l_CameraComponent = m_Registry->GetComponent<CameraComponent>(it_Entity);
            l_Instance.m_IsPrimary = l_CameraComponent.m_Primary;
            l_Instance.m_IsViewportCamera = (it_Entity == m_ViewportCamera);

            // Default to a hidden frustum so callers can rely on deterministic state before projection succeeds.
            l_Instance.m_HasFrustum = false;
            l_Instance.m_FrustumCorners.fill(glm::vec2{ 0.0f, 0.0f });
            l_Instance.m_FrustumCornerVisible.fill(false);

            std::array<glm::vec3, 4> l_WorldCorners{};
            if (BuildFrustumPreview(l_Transform, l_CameraComponent, l_ViewportSize, l_WorldCorners))
            {
                size_t l_VisibleCornerCount = 0;
                for (size_t it_Corner = 0; it_Corner < l_WorldCorners.size(); ++it_Corner)
                {
                    const glm::vec4 l_CornerClip = l_ViewProjection * glm::vec4(l_WorldCorners[it_Corner], 1.0f);
                    if (std::abs(l_CornerClip.w) <= std::numeric_limits<float>::epsilon())
                    {
                        continue;
                    }

                    const glm::vec3 l_CornerNdc = glm::vec3(l_CornerClip) / l_CornerClip.w;
                    const bool l_DepthVisible = (l_CornerNdc.z >= 0.0f) && (l_CornerNdc.z <= 1.0f);
                    const bool l_InBounds = (std::abs(l_CornerNdc.x) <= 1.0f) && (std::abs(l_CornerNdc.y) <= 1.0f);
                    if (!(l_DepthVisible && l_InBounds))
                    {
                        continue;
                    }

                    l_Instance.m_FrustumCorners[it_Corner].x = (l_CornerNdc.x * 0.5f + 0.5f) * l_ViewportSize.x;
                    l_Instance.m_FrustumCorners[it_Corner].y = (1.0f - (l_CornerNdc.y * 0.5f + 0.5f)) * l_ViewportSize.y;
                    l_Instance.m_FrustumCornerVisible[it_Corner] = true;
                    ++l_VisibleCornerCount;
                }

                // Only flag the frustum as visible when at least two corners remain on-screen to avoid stray lines.
                l_Instance.m_HasFrustum = (l_VisibleCornerCount >= 2);
            }
        }

        std::sort(l_Instances.begin(), l_Instances.end(), [](const CameraOverlayInstance& lhs, const CameraOverlayInstance& rhs)
            {
                // Draw farther cameras first so nearer overlays stack on top of the list visually.
                return lhs.m_Depth > rhs.m_Depth;
            });

        return l_Instances;
    }

    void Renderer::SetViewport(uint32_t viewportID, const ViewportInfo& info)
    {
        const uint32_t l_PreviousViewportId = m_ActiveViewportId;
        m_ActiveViewportId = viewportID;

        ViewportContext& l_Context = GetOrCreateViewportContext(viewportID);
        l_Context.m_Info = info;
        l_Context.m_Info.ViewportID = viewportID;

        auto a_UpdateCameraSize = [info](Camera* targetCamera)
            {
                if (targetCamera)
                {
                    // Ensure the camera's projection matches the viewport so culling and mouse picking behave correctly.
                    targetCamera->SetViewportSize(info.Size);
                }
            };

        if (viewportID == 1U)
        {
            // Explicitly size the editor camera against the editor viewport. If it is missing fall back so rendering continues.
            if (m_EditorCamera)
            {
                a_UpdateCameraSize(m_EditorCamera);
            }
            else
            {
                a_UpdateCameraSize(m_RuntimeCamera);
            }
        }
        else if (viewportID == 2U)
        {
            // Runtime viewport pulls from the gameplay camera, falling back to editor output for inactive simulations.
            if (m_RuntimeCamera)
            {
                a_UpdateCameraSize(m_RuntimeCamera);
            }
            else
            {
                a_UpdateCameraSize(m_EditorCamera);
            }
        }
        else
        {
            // Additional viewports inherit editor sizing today; future multi-camera routing can specialise this branch.
            a_UpdateCameraSize(m_EditorCamera ? m_EditorCamera : m_RuntimeCamera);
        }

        if (!IsValidViewport(info))
        {
            // The viewport was closed or minimized, so free the auxiliary render target when possible.
            DestroyOffscreenResources(l_PreviousViewportId);

            return;
        }

        VkExtent2D l_RequestedExtent{};
        l_RequestedExtent.width = static_cast<uint32_t>(std::max(info.Size.x, 0.0f));
        l_RequestedExtent.height = static_cast<uint32_t>(std::max(info.Size.y, 0.0f));

        l_Context.m_CachedExtent = l_RequestedExtent;

        // Only allow readback resizing if we aren't recording, 
        // or if this IS the viewport we are recording.
        if (!m_ViewportRecordingEnabled || viewportID == m_RecordingViewportId)
        {
            RequestReadbackResize(l_RequestedExtent);
        }

        // Request a readback resize whenever the viewport dimensions change so staging buffers track the active target.
        //RequestReadbackResize(l_RequestedExtent);

        if (l_RequestedExtent.width == 0 || l_RequestedExtent.height == 0)
        {
            DestroyOffscreenResources(viewportID);

            return;
        }

        OffscreenTarget& l_Target = l_Context.m_Target;
        if (l_Target.m_Extent.width == l_RequestedExtent.width && l_Target.m_Extent.height == l_RequestedExtent.height)
        {
            // Nothing to do – the backing image already matches the requested size.
            return;
        }

        CreateOrResizeOffscreenResources(l_Target, l_RequestedExtent);

        // Future: consider pooling and recycling detached targets so background viewports can warm-start when reopened.
    }

    ViewportInfo Renderer::GetViewport() const
    {
        const ViewportContext* l_Context = FindViewportContext(m_ActiveViewportId);
        if (l_Context)
        {
            return l_Context->m_Info;
        }

        return {};
    }

    void Renderer::BuildSpriteGeometry()
    {
        // Ensure any previous allocation is cleared before creating a new quad.
        DestroySpriteGeometry();

        std::array<Vertex, 4> l_Vertices{};
        // Define a unit quad facing the camera so transforms can scale it to the desired size.
        l_Vertices[0].Position = { -0.5f, -0.5f, 0.0f };
        l_Vertices[1].Position = { 0.5f, -0.5f, 0.0f };
        l_Vertices[2].Position = { 0.5f, 0.5f, 0.0f };
        l_Vertices[3].Position = { -0.5f, 0.5f, 0.0f };

        for (Vertex& it_Vertex : l_Vertices)
        {
            it_Vertex.Normal = { 0.0f, 0.0f, -1.0f };   // Point towards the default camera direction.
            it_Vertex.Tangent = { 1.0f, 0.0f, 0.0f };  // Tangent aligned with X for normal mapping compatibility.
            it_Vertex.Bitangent = { 0.0f, 1.0f, 0.0f }; // Bitangent aligned with Y.
            it_Vertex.Color = { 1.0f, 1.0f, 1.0f };     // White default so tinting works consistently.
        }

        l_Vertices[0].TexCoord = { 0.0f, 0.0f };
        l_Vertices[1].TexCoord = { 1.0f, 0.0f };
        l_Vertices[2].TexCoord = { 1.0f, 1.0f };
        l_Vertices[3].TexCoord = { 0.0f, 1.0f };

        const std::array<uint32_t, 6> l_Indices{ 0, 2, 1, 0, 3, 2 };

        std::vector<Vertex> l_VertexData(l_Vertices.begin(), l_Vertices.end());
        std::vector<uint32_t> l_IndexData(l_Indices.begin(), l_Indices.end());

        m_Buffers.CreateVertexBuffer(l_VertexData, m_Commands.GetOneTimePool(), m_SpriteVertexBuffer, m_SpriteVertexMemory);
        m_Buffers.CreateIndexBuffer(l_IndexData, m_Commands.GetOneTimePool(), m_SpriteIndexBuffer, m_SpriteIndexMemory, m_SpriteIndexCount);

        if (m_SpriteIndexCount == 0)
        {
            m_SpriteIndexCount = static_cast<uint32_t>(l_Indices.size());
        }
    }

    void Renderer::DestroySpriteGeometry()
    {
        if (m_SpriteVertexBuffer != VK_NULL_HANDLE || m_SpriteVertexMemory != VK_NULL_HANDLE)
        {
            m_Buffers.DestroyBuffer(m_SpriteVertexBuffer, m_SpriteVertexMemory);
            m_SpriteVertexBuffer = VK_NULL_HANDLE;
            m_SpriteVertexMemory = VK_NULL_HANDLE;
        }

        if (m_SpriteIndexBuffer != VK_NULL_HANDLE || m_SpriteIndexMemory != VK_NULL_HANDLE)
        {
            m_Buffers.DestroyBuffer(m_SpriteIndexBuffer, m_SpriteIndexMemory);
            m_SpriteIndexBuffer = VK_NULL_HANDLE;
            m_SpriteIndexMemory = VK_NULL_HANDLE;
            m_SpriteIndexCount = 0;
        }
    }

    void Renderer::GatherMeshDraws()
    {
        m_MeshDrawCommands.clear();

        if (!m_Registry)
        {
            return;
        }

        const auto& l_Entities = m_Registry->GetEntities();
        // Reserve upfront so dynamic scenes with many meshes avoid repeated allocations.
        m_MeshDrawCommands.reserve(l_Entities.size());

        for (ECS::Entity it_Entity : l_Entities)
        {
            if (!m_Registry->HasComponent<MeshComponent>(it_Entity))
            {
                continue;
            }

            MeshComponent& l_MeshComponent = m_Registry->GetComponent<MeshComponent>(it_Entity);
            if (!l_MeshComponent.m_Visible)
            {
                continue;
            }

            if (l_MeshComponent.m_Primitive != MeshComponent::PrimitiveType::None && l_MeshComponent.m_MeshIndex == std::numeric_limits<size_t>::max())
            {
                // Ensure primitives have mesh indices so they can participate in draw command gathering.
                l_MeshComponent.m_MeshIndex = GetOrCreatePrimitiveMeshIndex(l_MeshComponent.m_Primitive);
                if (l_MeshComponent.m_MeshIndex == std::numeric_limits<size_t>::max())
                {
                    continue;
                }
            }

            if (l_MeshComponent.m_MeshIndex >= m_MeshDrawInfo.size())
            {
                // The component references geometry that has not been uploaded yet. Future streaming work can patch this once asynchronous loading lands.
                continue;
            }

            const MeshDrawInfo& l_DrawInfo = m_MeshDrawInfo[l_MeshComponent.m_MeshIndex];
            if (l_DrawInfo.m_IndexCount == 0)
            {
                continue;
            }

            glm::mat4 l_ModelMatrix{ 1.0f };
            if (m_Registry->HasComponent<Transform>(it_Entity))
            {
                l_ModelMatrix = ComposeTransform(m_Registry->GetComponent<Transform>(it_Entity));
            }

            TextureComponent* l_TextureComponent = nullptr;
            if (m_Registry->HasComponent<TextureComponent>(it_Entity))
            {
                TextureComponent& l_ComponentTexture = m_Registry->GetComponent<TextureComponent>(it_Entity);
                l_TextureComponent = &l_ComponentTexture;
                if (l_ComponentTexture.m_IsDirty || l_ComponentTexture.m_TextureSlot < 0)
                {
                    const int32_t l_ResolvedSlot = ResolveTextureSlot(l_ComponentTexture.m_TexturePath);
                    l_ComponentTexture.m_TextureSlot = l_ResolvedSlot;
                    // Clear the dirty flag so subsequent frames reuse the cached slot. Future follow-up: retry failures on demand.
                    l_ComponentTexture.m_IsDirty = false;
                }
            }

            const AnimationComponent* l_AnimationComponent = nullptr;
            if (m_Registry->HasComponent<AnimationComponent>(it_Entity))
            {
                l_AnimationComponent = &m_Registry->GetComponent<AnimationComponent>(it_Entity);
            }

            MeshDrawCommand l_Command{};
            l_Command.m_ModelMatrix = l_ModelMatrix;
            l_Command.m_Component = &l_MeshComponent;
            l_Command.m_TextureComponent = l_TextureComponent;
            l_Command.m_AnimationComponent = l_AnimationComponent;
            l_Command.m_BoneOffset = 0;
            l_Command.m_BoneCount = 0;
            l_Command.m_Entity = it_Entity;
            m_MeshDrawCommands.push_back(l_Command);
        }
    }

    void Renderer::GatherSpriteDraws()
    {
        m_SpriteDrawList.clear();

        if (!m_Registry)
        {
            return;
        }

        const auto& l_Entities = m_Registry->GetEntities();
        m_SpriteDrawList.reserve(l_Entities.size());

        for (ECS::Entity it_Entity : l_Entities)
        {
            if (!m_Registry->HasComponent<Transform>(it_Entity) || !m_Registry->HasComponent<SpriteComponent>(it_Entity))
            {
                continue;
            }

            SpriteComponent& l_Sprite = m_Registry->GetComponent<SpriteComponent>(it_Entity);
            if (!l_Sprite.m_Visible)
            {
                continue;
            }

            TextureComponent* l_TextureComponent = nullptr;
            if (m_Registry->HasComponent<TextureComponent>(it_Entity))
            {
                TextureComponent& l_ComponentTexture = m_Registry->GetComponent<TextureComponent>(it_Entity);
                l_TextureComponent = &l_ComponentTexture;
                if (l_ComponentTexture.m_IsDirty || l_ComponentTexture.m_TextureSlot < 0)
                {
                    const int32_t l_ResolvedSlot = ResolveTextureSlot(l_ComponentTexture.m_TexturePath);
                    l_ComponentTexture.m_TextureSlot = l_ResolvedSlot;
                    l_ComponentTexture.m_IsDirty = false;
                }
            }

            SpriteDrawCommand l_Command{};
            l_Command.m_ModelMatrix = ComposeTransform(m_Registry->GetComponent<Transform>(it_Entity));
            l_Command.m_Component = &l_Sprite;
            l_Command.m_TextureComponent = l_TextureComponent;
            l_Command.m_Entity = it_Entity;

            m_SpriteDrawList.push_back(l_Command);
        }
    }

    void Renderer::DrawSprites(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        (void)imageIndex; // Reserved for future per-swapchain sprite atlas selection.

        if (m_SpriteDrawList.empty() || m_SpriteVertexBuffer == VK_NULL_HANDLE || m_SpriteIndexBuffer == VK_NULL_HANDLE)
        {
            return;
        }

        VkBuffer l_VertexBuffers[] = { m_SpriteVertexBuffer };
        VkDeviceSize l_Offsets[] = { 0 };
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, l_VertexBuffers, l_Offsets);
        vkCmdBindIndexBuffer(commandBuffer, m_SpriteIndexBuffer, 0, VK_INDEX_TYPE_UINT32);

        for (const SpriteDrawCommand& it_Command : m_SpriteDrawList)
        {
            if (it_Command.m_Component == nullptr)
            {
                continue;
            }

            RenderablePushConstant l_PushConstant{};
            l_PushConstant.m_ModelMatrix = it_Command.m_ModelMatrix;
            l_PushConstant.m_TintColor = it_Command.m_Component->m_TintColor;
            l_PushConstant.m_TextureScale = it_Command.m_Component->m_UVScale;
            l_PushConstant.m_TextureOffset = it_Command.m_Component->m_UVOffset;
            l_PushConstant.m_TilingFactor = it_Command.m_Component->m_TilingFactor;
            l_PushConstant.m_UseMaterialOverride = it_Command.m_Component->m_UseMaterialOverride ? 1 : 0;
            l_PushConstant.m_SortBias = it_Command.m_Component->m_SortOffset;
            l_PushConstant.m_TextureSlot = 0; // Sprites currently sample the default texture until atlas streaming is wired up.

            if (it_Command.m_TextureComponent != nullptr && it_Command.m_TextureComponent->m_TextureSlot >= 0)
            {
                l_PushConstant.m_TextureSlot = it_Command.m_TextureComponent->m_TextureSlot;
            }
            else
            {
                // Sprites currently sample the default texture until atlas streaming is wired up.
                l_PushConstant.m_TextureSlot = 0;
            }

            vkCmdPushConstants(commandBuffer, m_Pipeline.GetPipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 
                sizeof(RenderablePushConstant), &l_PushConstant);
            vkCmdDrawIndexed(commandBuffer, m_SpriteIndexCount, 1, 0, 0, 0);
        }
    }

    void Renderer::EnsureSkinningBufferCapacity(size_t requiredMatrices)
    {
        const uint32_t l_ImageCount = m_Swapchain.GetImageCount();
        const size_t l_TargetMatrices = std::max({ requiredMatrices, static_cast<size_t>(1), static_cast<size_t>(s_MaxBonesPerSkeleton) });
        const VkDeviceSize l_TargetSize = static_cast<VkDeviceSize>(l_TargetMatrices * sizeof(glm::mat4));

        if (l_ImageCount == 0)
        {
            m_BonePaletteMatrixCapacity = l_TargetMatrices;
            m_BonePaletteBufferSize = l_TargetSize;
            return;
        }

        const bool l_ImageMismatch = m_BonePaletteBuffers.size() != l_ImageCount;
        const bool l_CapacityMismatch = l_TargetMatrices > m_BonePaletteMatrixCapacity;
        if (!l_ImageMismatch && !l_CapacityMismatch)
        {
            return;
        }

        if (!m_BonePaletteBuffers.empty() && m_ResourceFence != VK_NULL_HANDLE)
        {
            vkWaitForFences(Startup::GetDevice(), 1, &m_ResourceFence, VK_TRUE, UINT64_MAX);
        }

        for (size_t it_Index = 0; it_Index < m_BonePaletteBuffers.size(); ++it_Index)
        {
            m_Buffers.DestroyBuffer(m_BonePaletteBuffers[it_Index], (it_Index < m_BonePaletteMemory.size()) ? m_BonePaletteMemory[it_Index] : VK_NULL_HANDLE);
        }

        m_BonePaletteBuffers.clear();
        m_BonePaletteMemory.clear();

        if (l_TargetSize == 0)
        {
            return;
        }

        // Future improvement: stream these palettes through a device-local ring buffer once animation streaming is mature.
        m_Buffers.CreateStorageBuffers(l_ImageCount, l_TargetSize, m_BonePaletteBuffers, m_BonePaletteMemory);
        m_BonePaletteMatrixCapacity = l_TargetMatrices;
        m_BonePaletteBufferSize = l_TargetSize;
        m_BonePaletteScratch.resize(m_BonePaletteMatrixCapacity);

        RefreshBonePaletteDescriptors();
    }

    void Renderer::RefreshBonePaletteDescriptors()
    {
        if (m_DescriptorSets.empty() || m_BonePaletteBuffers.empty() || m_BonePaletteBufferSize == 0)
        {
            return;
        }

        std::vector<VkDescriptorBufferInfo> l_BufferInfos(m_DescriptorSets.size());
        std::vector<VkWriteDescriptorSet> l_Writes(m_DescriptorSets.size());

        for (size_t it_Index = 0; it_Index < m_DescriptorSets.size(); ++it_Index)
        {
            VkDescriptorBufferInfo& l_Info = l_BufferInfos[it_Index];
            l_Info.buffer = (it_Index < m_BonePaletteBuffers.size()) ? m_BonePaletteBuffers[it_Index] : VK_NULL_HANDLE;
            l_Info.offset = 0;
            l_Info.range = m_BonePaletteBufferSize;

            VkWriteDescriptorSet& l_Write = l_Writes[it_Index];
            l_Write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            l_Write.dstSet = m_DescriptorSets[it_Index];
            l_Write.dstBinding = 4;
            l_Write.dstArrayElement = 0;
            l_Write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            l_Write.descriptorCount = 1;
            l_Write.pBufferInfo = &l_BufferInfos[it_Index];
        }

        vkUpdateDescriptorSets(Startup::GetDevice(), static_cast<uint32_t>(l_Writes.size()), l_Writes.data(), 0, nullptr);
    }

    void Renderer::PrepareBonePaletteBuffer(uint32_t imageIndex)
    {
        if (imageIndex >= m_BonePaletteBuffers.size() || imageIndex >= m_BonePaletteMemory.size())
        {
            return;
        }

        if (m_BonePaletteBuffers[imageIndex] == VK_NULL_HANDLE || m_BonePaletteMemory[imageIndex] == VK_NULL_HANDLE)
        {
            return;
        }

        size_t l_TotalMatrices = 0;
        for (MeshDrawCommand& it_Command : m_MeshDrawCommands)
        {
            it_Command.m_BoneOffset = 0;
            it_Command.m_BoneCount = 0;

            if (it_Command.m_AnimationComponent == nullptr)
            {
                continue;
            }

            const auto& l_Source = it_Command.m_AnimationComponent->m_BoneMatrices;
            if (l_Source.empty())
            {
                continue;
            }

            const uint32_t l_ClampedCount = static_cast<uint32_t>(std::min<size_t>(l_Source.size(), s_MaxBonesPerSkeleton));
            it_Command.m_BoneOffset = static_cast<uint32_t>(l_TotalMatrices);
            it_Command.m_BoneCount = l_ClampedCount;
            l_TotalMatrices += l_ClampedCount;
        }

        if (l_TotalMatrices == 0)
        {
            return;
        }

        EnsureSkinningBufferCapacity(l_TotalMatrices);
        if (m_BonePaletteBuffers.empty() || imageIndex >= m_BonePaletteBuffers.size())
        {
            return;
        }

        const VkDeviceSize l_CopySize = static_cast<VkDeviceSize>(l_TotalMatrices * sizeof(glm::mat4));
        if (l_CopySize == 0 || l_CopySize > m_BonePaletteBufferSize)
        {
            return;
        }

        m_BonePaletteScratch.resize(l_TotalMatrices);
        size_t l_WriteIndex = 0;
        for (const MeshDrawCommand& it_Command : m_MeshDrawCommands)
        {
            if (it_Command.m_BoneCount == 0 || it_Command.m_AnimationComponent == nullptr)
            {
                continue;
            }

            const auto& l_Source = it_Command.m_AnimationComponent->m_BoneMatrices;
            for (uint32_t it_Bone = 0; it_Bone < it_Command.m_BoneCount && l_WriteIndex < m_BonePaletteScratch.size(); ++it_Bone)
            {
                m_BonePaletteScratch[l_WriteIndex++] = l_Source[it_Bone];
            }
        }

        void* l_Mapped = nullptr;
        if (vkMapMemory(Startup::GetDevice(), m_BonePaletteMemory[imageIndex], 0, l_CopySize, 0, &l_Mapped) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to map bone palette buffer for image {}", imageIndex);
            return;
        }

        std::memcpy(l_Mapped, m_BonePaletteScratch.data(), static_cast<size_t>(l_CopySize));
        vkUnmapMemory(Startup::GetDevice(), m_BonePaletteMemory[imageIndex]);
    }

    void Renderer::RecreateSwapchain()
    {
        TR_CORE_TRACE("Recreating Swapchain");

        uint32_t l_Width = 0;
        uint32_t l_Height = 0;

        Startup::GetWindow().GetFramebufferSize(l_Width, l_Height);

        while (l_Width == 0 || l_Height == 0)
        {
            glfwWaitEvents();

            Startup::GetWindow().GetFramebufferSize(l_Width, l_Height);
        }

        vkDeviceWaitIdle(Startup::GetDevice());

        m_Pipeline.CleanupFramebuffers();

        m_Swapchain.Cleanup();
        m_Swapchain.Init();
        // Whenever the swapchain rebuilds, reset the cached layouts because new images arrive in an undefined state.
        m_SwapchainImageLayouts.assign(m_Swapchain.GetImageCount(), VK_IMAGE_LAYOUT_UNDEFINED);
        m_SwapchainDepthLayouts.assign(m_Swapchain.GetImageCount(), VK_IMAGE_LAYOUT_UNDEFINED);

        // Rebuild the swapchain-backed framebuffers so that they point at the freshly created images.
        m_Pipeline.RecreateFramebuffers(m_Swapchain);

        uint32_t l_ImageCount = m_Swapchain.GetImageCount();
        if (m_Commands.GetFrameCount() != l_ImageCount)
        {
            // Command buffers and synchronization objects are per swapchain image, so rebuild them if the count changed.
            TR_CORE_TRACE("Resizing command resources (Old = {}, New = {})", m_Commands.GetFrameCount(), l_ImageCount);
            m_Commands.Recreate(l_ImageCount);
        }

        if (l_ImageCount != m_GlobalUniformBuffers.size())
        {
            // We have a different swapchain image count, so destroy and rebuild any per-frame resources.
            for (size_t i = 0; i < m_GlobalUniformBuffers.size(); ++i)
            {
                m_Buffers.DestroyBuffer(m_GlobalUniformBuffers[i], m_GlobalUniformBuffersMemory[i]);
            }

            for (size_t i = 0; i < m_MaterialBuffers.size(); ++i)
            {
                m_Buffers.DestroyBuffer(m_MaterialBuffers[i], m_MaterialBuffersMemory[i]);
            }

            if (!m_DescriptorSets.empty())
            {
                // Free descriptor sets from the old pool so we can rebuild them cleanly.
                vkFreeDescriptorSets(Startup::GetDevice(), m_DescriptorPool, static_cast<uint32_t>(m_DescriptorSets.size()), m_DescriptorSets.data());
                m_DescriptorSets.clear();
            }

            DestroySkyboxDescriptorSets();

            if (m_DescriptorPool != VK_NULL_HANDLE)
            {
                // Tear down the descriptor pool so that we can rebuild it with the new descriptor counts.
                vkDestroyDescriptorPool(Startup::GetDevice(), m_DescriptorPool, nullptr);
                m_DescriptorPool = VK_NULL_HANDLE;
            }

            m_GlobalUniformBuffers.clear();
            m_GlobalUniformBuffersMemory.clear();
            m_MaterialBuffers.clear();
            m_MaterialBuffersMemory.clear();
            m_MaterialBufferDirty.clear();

            VkDeviceSize l_GlobalSize = sizeof(GlobalUniformBuffer);

            m_Buffers.CreateUniformBuffers(l_ImageCount, l_GlobalSize, m_GlobalUniformBuffers, m_GlobalUniformBuffersMemory);
            EnsureMaterialBufferCapacity(m_Materials.size());

            // Recreate the descriptor pool before allocating descriptor sets so the pool matches the new swapchain image count.
            CreateDescriptorPool();
            CreateDescriptorSets();
            m_TextRenderer.RecreateDescriptors(m_DescriptorPool, static_cast<uint32_t>(m_Swapchain.GetImageCount()));
            m_TextRenderer.RecreatePipeline(m_Pipeline.GetRenderPass());

            TR_CORE_TRACE("Descriptor resources recreated (SwapchainImages = {}, GlobalUBOs = {}, MaterialBuffers = {}, CombinedSamplers = {}, DescriptorSets = {})",
                l_ImageCount, m_GlobalUniformBuffers.size(), m_MaterialBuffers.size(), l_ImageCount, m_DescriptorSets.size());
        }

        VkExtent2D l_ReadbackExtent = m_Swapchain.GetExtent();
        const ViewportContext* l_ActiveContext = FindViewportContext(m_ActiveViewportId);

        if (l_ActiveContext && IsValidViewport(l_ActiveContext->m_Info) && m_ActiveViewportId != 0)
        {
            VkExtent2D l_ViewportExtent{};
            l_ViewportExtent.width = static_cast<uint32_t>(std::max(l_ActiveContext->m_Info.Size.x, 0.0f));
            l_ViewportExtent.height = static_cast<uint32_t>(std::max(l_ActiveContext->m_Info.Size.y, 0.0f));

            if (l_ViewportExtent.width > 0 && l_ViewportExtent.height > 0)
            {
                ViewportContext& l_MutableContext = GetOrCreateViewportContext(m_ActiveViewportId);
                CreateOrResizeOffscreenResources(l_MutableContext.m_Target, l_ViewportExtent);

                l_ReadbackExtent = l_ViewportExtent;
            }
            else
            {
                DestroyOffscreenResources(m_ActiveViewportId);
            }
        }
        else if (m_ActiveViewportId != 0)
        {
            DestroyOffscreenResources(m_ActiveViewportId);
        }

        DestroyAiResources();
        // Ensure the readback staging buffers match the refreshed swapchain or viewport geometry before re-uploading AI frames.
        RequestReadbackResize(l_ReadbackExtent, true);
        ApplyPendingReadbackResize();
        m_TextRenderer.RecreatePipeline(m_Pipeline.GetRenderPass());
    }

    //------------------------------------------------------------------------------------------------------------------------------------------------------//

    void Renderer::CreateDescriptorPool()
    {
        TR_CORE_TRACE("Creating Descriptor Pool");

        uint32_t l_ImageCount = m_Swapchain.GetImageCount();
        VkDescriptorPoolSize l_PoolSizes[4]{};
        l_PoolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        l_PoolSizes[0].descriptorCount = l_ImageCount * 2; // Global UBO for the main pipeline plus the skybox uniform.
        l_PoolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        l_PoolSizes[1].descriptorCount = l_ImageCount; // Material uniform buffer bound once per swapchain image.
        l_PoolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        l_PoolSizes[2].descriptorCount = l_ImageCount; // Bone palette buffer updated once per swapchain image.
        l_PoolSizes[3].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        // Each swapchain image consumes an array of material textures, an AI blend texture and a cubemap sampler in the main
        // set, plus a cubemap sampler in the dedicated skybox set. The text renderer also binds a combined image sampler once
        // per frame, so reserve an additional descriptor for that path.
        l_PoolSizes[3].descriptorCount = l_ImageCount * (Pipeline::s_MaxMaterialTextures + 4);

        VkDescriptorPoolCreateInfo l_PoolInfo{};
        l_PoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        // We free and recreate descriptor sets whenever the swapchain is resized, so enable free-descriptor support.
        l_PoolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        l_PoolInfo.poolSizeCount = static_cast<uint32_t>(std::size(l_PoolSizes));
        l_PoolInfo.pPoolSizes = l_PoolSizes;
        // Main render pipeline + dedicated skybox descriptors + per-frame text descriptor sets.
        l_PoolInfo.maxSets = l_ImageCount * 3;

        if (vkCreateDescriptorPool(Startup::GetDevice(), &l_PoolInfo, nullptr, &m_DescriptorPool) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create descriptor pool");
        }

        TR_CORE_TRACE("Descriptor Pool Created (MaxSets = {})", l_PoolInfo.maxSets);
    }

    void Renderer::CreateDefaultTexture()
    {
        TR_CORE_TRACE("Creating Default Texture");

        for (TextureSlot& it_Slot : m_TextureSlots)
        {
            DestroyTextureSlot(it_Slot);
        }
        m_TextureSlots.clear();
        m_TextureSlotLookup.clear();

        Loader::TextureData l_DefaultData{};
        l_DefaultData.Width = 1;
        l_DefaultData.Height = 1;
        l_DefaultData.Channels = 4;
        l_DefaultData.Pixels = { 0xFF, 0xFF, 0xFF, 0xFF };

        TextureSlot l_DefaultSlot{};
        if (!PopulateTextureSlot(l_DefaultSlot, l_DefaultData))
        {
            TR_CORE_CRITICAL("Failed to create default texture slot");
            return;
        }

        l_DefaultSlot.m_SourcePath = kDefaultTextureKey;
        m_TextureSlots.push_back(std::move(l_DefaultSlot));
        m_TextureSlotLookup.emplace(kDefaultTextureKey, 0u);

        EnsureTextureDescriptorCapacity();
        RefreshTextureDescriptorBindings();

        TR_CORE_TRACE("Default Texture Created");
    }

    void Renderer::DestroyTextureSlot(TextureSlot& slot)
    {
        VkDevice l_Device = Startup::GetDevice();

        if (slot.m_Sampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(l_Device, slot.m_Sampler, nullptr);
            slot.m_Sampler = VK_NULL_HANDLE;
        }

        if (slot.m_View != VK_NULL_HANDLE)
        {
            vkDestroyImageView(l_Device, slot.m_View, nullptr);
            slot.m_View = VK_NULL_HANDLE;
        }

        if (slot.m_Image != VK_NULL_HANDLE)
        {
            vkDestroyImage(l_Device, slot.m_Image, nullptr);
            slot.m_Image = VK_NULL_HANDLE;
        }

        if (slot.m_Memory != VK_NULL_HANDLE)
        {
            vkFreeMemory(l_Device, slot.m_Memory, nullptr);
            slot.m_Memory = VK_NULL_HANDLE;
        }

        slot.m_Descriptor = {};
    }

    bool Renderer::PopulateTextureSlot(TextureSlot& slot, const Loader::TextureData& textureData)
    {
        DestroyTextureSlot(slot);

        if (textureData.Pixels.empty() || textureData.Width <= 0 || textureData.Height <= 0)
        {
            return false;
        }

        VkDevice l_Device = Startup::GetDevice();
        const VkDeviceSize l_ImageSize = static_cast<VkDeviceSize>(textureData.Pixels.size());

        VkBuffer l_StagingBuffer = VK_NULL_HANDLE;
        VkDeviceMemory l_StagingMemory = VK_NULL_HANDLE;
        m_Buffers.CreateBuffer(l_ImageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, l_StagingBuffer, l_StagingMemory);

        void* l_Data = nullptr;
        vkMapMemory(l_Device, l_StagingMemory, 0, l_ImageSize, 0, &l_Data);
        std::memcpy(l_Data, textureData.Pixels.data(), static_cast<size_t>(l_ImageSize));
        vkUnmapMemory(l_Device, l_StagingMemory);

        VkImageCreateInfo l_ImageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        l_ImageInfo.imageType = VK_IMAGE_TYPE_2D;
        l_ImageInfo.extent.width = static_cast<uint32_t>(std::max(textureData.Width, 1));
        l_ImageInfo.extent.height = static_cast<uint32_t>(std::max(textureData.Height, 1));
        l_ImageInfo.extent.depth = 1;
        l_ImageInfo.mipLevels = 1;
        l_ImageInfo.arrayLayers = 1;
        l_ImageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
        l_ImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        l_ImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        l_ImageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        l_ImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        l_ImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

        if (vkCreateImage(l_Device, &l_ImageInfo, nullptr, &slot.m_Image) != VK_SUCCESS)
        {
            m_Buffers.DestroyBuffer(l_StagingBuffer, l_StagingMemory);
            DestroyTextureSlot(slot);
            return false;
        }

        VkMemoryRequirements l_MemoryRequirements{};
        vkGetImageMemoryRequirements(l_Device, slot.m_Image, &l_MemoryRequirements);

        VkMemoryAllocateInfo l_AllocInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        l_AllocInfo.allocationSize = l_MemoryRequirements.size;
        l_AllocInfo.memoryTypeIndex = m_Buffers.FindMemoryType(l_MemoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(l_Device, &l_AllocInfo, nullptr, &slot.m_Memory) != VK_SUCCESS)
        {
            m_Buffers.DestroyBuffer(l_StagingBuffer, l_StagingMemory);
            DestroyTextureSlot(slot);
            return false;
        }

        vkBindImageMemory(l_Device, slot.m_Image, slot.m_Memory, 0);

        VkCommandBuffer l_CommandBuffer = m_Commands.BeginSingleTimeCommands();

        VkImageMemoryBarrier l_BarrierToTransfer{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        l_BarrierToTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        l_BarrierToTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        l_BarrierToTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        l_BarrierToTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        l_BarrierToTransfer.image = slot.m_Image;
        l_BarrierToTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        l_BarrierToTransfer.subresourceRange.baseMipLevel = 0;
        l_BarrierToTransfer.subresourceRange.levelCount = 1;
        l_BarrierToTransfer.subresourceRange.baseArrayLayer = 0;
        l_BarrierToTransfer.subresourceRange.layerCount = 1;
        l_BarrierToTransfer.srcAccessMask = 0;
        l_BarrierToTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        vkCmdPipelineBarrier(l_CommandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &l_BarrierToTransfer);

        VkBufferImageCopy l_CopyRegion{};
        l_CopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        l_CopyRegion.imageSubresource.mipLevel = 0;
        l_CopyRegion.imageSubresource.baseArrayLayer = 0;
        l_CopyRegion.imageSubresource.layerCount = 1;
        l_CopyRegion.imageOffset = { 0, 0, 0 };
        l_CopyRegion.imageExtent = { l_ImageInfo.extent.width, l_ImageInfo.extent.height, 1 };

        vkCmdCopyBufferToImage(l_CommandBuffer, l_StagingBuffer, slot.m_Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &l_CopyRegion);

        VkImageMemoryBarrier l_BarrierToShader{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        l_BarrierToShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        l_BarrierToShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        l_BarrierToShader.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        l_BarrierToShader.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        l_BarrierToShader.image = slot.m_Image;
        l_BarrierToShader.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        l_BarrierToShader.subresourceRange.baseMipLevel = 0;
        l_BarrierToShader.subresourceRange.levelCount = 1;
        l_BarrierToShader.subresourceRange.baseArrayLayer = 0;
        l_BarrierToShader.subresourceRange.layerCount = 1;
        l_BarrierToShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        l_BarrierToShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(l_CommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &l_BarrierToShader);

        m_Commands.EndSingleTimeCommands(l_CommandBuffer);

        m_Buffers.DestroyBuffer(l_StagingBuffer, l_StagingMemory);

        VkImageViewCreateInfo l_ViewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        l_ViewInfo.image = slot.m_Image;
        l_ViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        l_ViewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
        l_ViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        l_ViewInfo.subresourceRange.baseMipLevel = 0;
        l_ViewInfo.subresourceRange.levelCount = 1;
        l_ViewInfo.subresourceRange.baseArrayLayer = 0;
        l_ViewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(l_Device, &l_ViewInfo, nullptr, &slot.m_View) != VK_SUCCESS)
        {
            DestroyTextureSlot(slot);
            return false;
        }

        VkSamplerCreateInfo l_SamplerInfo{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        l_SamplerInfo.magFilter = VK_FILTER_LINEAR;
        l_SamplerInfo.minFilter = VK_FILTER_LINEAR;
        l_SamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        l_SamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        l_SamplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        l_SamplerInfo.anisotropyEnable = VK_FALSE;
        l_SamplerInfo.maxAnisotropy = 1.0f;
        l_SamplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_WHITE;
        l_SamplerInfo.unnormalizedCoordinates = VK_FALSE;
        l_SamplerInfo.compareEnable = VK_FALSE;
        l_SamplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
        l_SamplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        l_SamplerInfo.mipLodBias = 0.0f;
        l_SamplerInfo.minLod = 0.0f;
        l_SamplerInfo.maxLod = 0.0f;

        if (vkCreateSampler(l_Device, &l_SamplerInfo, nullptr, &slot.m_Sampler) != VK_SUCCESS)
        {
            DestroyTextureSlot(slot);
            return false;
        }

        slot.m_Descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        slot.m_Descriptor.imageView = slot.m_View;
        slot.m_Descriptor.sampler = slot.m_Sampler;

        return true;
    }

    void Renderer::EnsureTextureDescriptorCapacity()
    {
        if (m_TextureDescriptorCache.size() != Pipeline::s_MaxMaterialTextures)
        {
            m_TextureDescriptorCache.resize(Pipeline::s_MaxMaterialTextures);
        }
    }

    void Renderer::RefreshTextureDescriptorBindings()
    {
        if (m_DescriptorSets.empty())
        {
            return;
        }

        if (m_TextureSlots.empty())
        {
            TR_CORE_WARN("Texture descriptor refresh skipped because no slots were initialised");
            return;
        }

        EnsureTextureDescriptorCapacity();

        const VkDescriptorImageInfo l_DefaultDescriptor = m_TextureSlots.front().m_Descriptor;
        for (uint32_t it_Index = 0; it_Index < Pipeline::s_MaxMaterialTextures; ++it_Index)
        {
            if (it_Index < m_TextureSlots.size())
            {
                m_TextureDescriptorCache[it_Index] = m_TextureSlots[it_Index].m_Descriptor;
            }
            else
            {
                m_TextureDescriptorCache[it_Index] = l_DefaultDescriptor;
            }
        }

        for (VkDescriptorSet l_Set : m_DescriptorSets)
        {
            VkWriteDescriptorSet l_TextureWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            l_TextureWrite.dstSet = l_Set;
            l_TextureWrite.dstBinding = 2;
            l_TextureWrite.dstArrayElement = 0;
            l_TextureWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            l_TextureWrite.descriptorCount = Pipeline::s_MaxMaterialTextures;
            l_TextureWrite.pImageInfo = m_TextureDescriptorCache.data();

            vkUpdateDescriptorSets(Startup::GetDevice(), 1, &l_TextureWrite, 0, nullptr);
        }
    }

    int32_t Renderer::ResolveTextureSlot(const std::string& texturePath)
    {
        // Normalise first so hot-reloads treat equivalent paths consistently across platforms.
        const std::string l_NormalizedPath = NormalizeTexturePath(texturePath);
        if (l_NormalizedPath.empty())
        {
            return 0;
        }

        const auto a_Existing = m_TextureSlotLookup.find(l_NormalizedPath);
        if (a_Existing != m_TextureSlotLookup.end())
        {
            return static_cast<int32_t>(a_Existing->second);
        }

        // Load the texture data on demand; the renderer caches GPU uploads so subsequent resolves are inexpensive.
        Loader::TextureData l_TextureData = Loader::TextureLoader::Load(l_NormalizedPath);
        const uint32_t l_SlotIndex = AcquireTextureSlot(l_NormalizedPath, l_TextureData);
        // Refresh descriptor arrays so any new slot becomes available to materials and push constants immediately.
        RefreshTextureDescriptorBindings();

        // Future follow-up: detect when no new slot was created and skip the descriptor refresh to save CPU work.
        return static_cast<int32_t>(l_SlotIndex);
    }

    std::string Renderer::NormalizeTexturePath(const std::string& texturePath) const
    {
        if (texturePath.empty())
        {
            return {};
        }

        return Utilities::FileManagement::NormalizePath(texturePath);
    }

    uint32_t Renderer::AcquireTextureSlot(const std::string& normalizedPath, const Loader::TextureData& textureData)
    {
        if (normalizedPath.empty())
        {
            return 0;
        }

        auto a_Existing = m_TextureSlotLookup.find(normalizedPath);
        if (a_Existing != m_TextureSlotLookup.end())
        {
            return a_Existing->second;
        }

        if (m_TextureSlots.size() >= Pipeline::s_MaxMaterialTextures)
        {
            TR_CORE_WARN("Material texture budget ({}) exhausted. {} will fall back to the default slot.", Pipeline::s_MaxMaterialTextures, normalizedPath.c_str());
        }
        else if (!textureData.Pixels.empty())
        {
            TextureSlot l_NewSlot{};
            if (PopulateTextureSlot(l_NewSlot, textureData))
            {
                l_NewSlot.m_SourcePath = normalizedPath;
                m_TextureSlots.push_back(std::move(l_NewSlot));
                const uint32_t l_NewIndex = static_cast<uint32_t>(m_TextureSlots.size() - 1);
                m_TextureSlotLookup.emplace(normalizedPath, l_NewIndex);
                return l_NewIndex;
            }

            TR_CORE_WARN("Failed to upload texture '{}'. Using the default slot instead.", normalizedPath.c_str());
        }
        else
        {
            TR_CORE_WARN("Texture '{}' provided no pixel data. Using the default slot instead.", normalizedPath.c_str());
        }

        m_TextureSlotLookup.emplace(normalizedPath, 0u);
        return 0;
    }

    void Renderer::ResolveMaterialTextureSlots(const std::vector<std::string>& textures, size_t materialOffset, size_t materialCount)
    {
        if (m_Materials.empty())
        {
            return;
        }

        const size_t l_SafeOffset = std::min(materialOffset, m_Materials.size());
        const size_t l_ResolvedCount = std::min(materialCount, m_Materials.size() - l_SafeOffset);

        std::vector<std::string> l_NormalizedPaths{};
        l_NormalizedPaths.reserve(textures.size());

        for (const std::string& it_Path : textures)
        {
            std::string l_Normalized = NormalizeTexturePath(it_Path);
            l_NormalizedPaths.push_back(l_Normalized);

            if (l_Normalized.empty() || m_TextureSlotLookup.find(l_Normalized) != m_TextureSlotLookup.end())
            {
                continue;
            }

            Loader::TextureData l_TextureData = Loader::TextureLoader::Load(l_Normalized);
            AcquireTextureSlot(l_Normalized, l_TextureData);
        }

        for (size_t it_Index = 0; it_Index < l_ResolvedCount; ++it_Index)
        {
            Geometry::Material& l_Material = m_Materials[l_SafeOffset + it_Index];
            l_Material.BaseColorTextureSlot = 0; // Fallback to the default white texture when the source is missing.

            if (l_Material.BaseColorTextureIndex < 0)
            {
                continue;
            }

            const size_t l_TextureIndex = static_cast<size_t>(l_Material.BaseColorTextureIndex);
            if (l_TextureIndex >= l_NormalizedPaths.size())
            {
                continue;
            }

            const std::string& l_Path = l_NormalizedPaths[l_TextureIndex];
            if (l_Path.empty())
            {
                continue;
            }

            auto a_Mapping = m_TextureSlotLookup.find(l_Path);
            if (a_Mapping != m_TextureSlotLookup.end())
            {
                l_Material.BaseColorTextureSlot = static_cast<int32_t>(a_Mapping->second);
            }
        }

        RefreshTextureDescriptorBindings();
    }

    void Renderer::CreateDefaultSkybox()
    {
        TR_CORE_TRACE("Creating Default Skybox");

        // Build a placeholder cubemap so the dedicated skybox shaders have a valid texture binding.
        CreateSkyboxCubemap();

        m_Skybox.Init(m_Buffers, m_Commands.GetOneTimePool());

        TR_CORE_TRACE("Default Skybox Created");
    }

    void Renderer::CreateSkyboxCubemap()
    {
        DestroySkyboxCubemap();

        TR_CORE_TRACE("Creating skybox cubemap");

        Loader::CubemapTextureData l_CubemapData{};
        std::string l_CubemapSource{};

        // Try loading a pre-authored cubemap from disk. This keeps the renderer flexible and allows
        // artists to swap between KTX packages and loose face images without touching the code.
        // Assets are copied next to the executable under Assets/ (see Trident-Forge/CMakeLists.txt post-build copy).
        const std::filesystem::path l_DefaultSkyboxRoot = std::filesystem::path("Assets") / "Skyboxes";
        const std::filesystem::path l_DefaultKtx = l_DefaultSkyboxRoot / "DefaultSkybox.ktx";
        std::error_code l_FileError{};
        if (std::filesystem::exists(l_DefaultKtx, l_FileError))
        {
            l_CubemapData = Loader::SkyboxTextureLoader::LoadFromKtx(l_DefaultKtx);
            l_CubemapSource = "DefaultSkybox.ktx";
        }
        else
        {
            const std::filesystem::path l_DefaultFaces = l_DefaultSkyboxRoot / "Default";
            if (std::filesystem::exists(l_DefaultFaces, l_FileError))
            {
                l_CubemapData = Loader::SkyboxTextureLoader::LoadFromDirectory(l_DefaultFaces);
                l_CubemapSource = "Default directory";
            }
            else if (std::filesystem::is_directory(l_DefaultSkyboxRoot, l_FileError))
            {
                // Discover loose PNG faces named using the short px/nx/... format so older assets continue to load.
                std::array<std::filesystem::path, 6> l_FallbackFaces{};
                std::array<bool, 6> l_FoundFaces{};
                const auto a_ToLower = [](std::string text)
                    {
                        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char character)
                            {
                                return static_cast<char>(std::tolower(character));
                            });
                        return text;
                    };
                constexpr std::array<std::array<std::string_view, 2>, 6> s_ShortFaceTokens{ {
                    { "posx", "px" },
                    { "negx", "nx" },
                    { "posy", "py" },
                    { "negy", "ny" },
                    { "posz", "pz" },
                    { "negz", "nz" }
                } };

                std::error_code l_IterError{};
                for (const auto& it_Entry : std::filesystem::directory_iterator(l_DefaultSkyboxRoot, l_IterError))
                {
                    if (!it_Entry.is_regular_file())
                    {
                        continue;
                    }

                    std::string l_StemLower = a_ToLower(it_Entry.path().stem().string());
                    for (size_t it_FaceIndex = 0; it_FaceIndex < s_ShortFaceTokens.size(); ++it_FaceIndex)
                    {
                        if (l_FoundFaces[it_FaceIndex])
                        {
                            continue;
                        }

                        for (std::string_view it_Token : s_ShortFaceTokens[it_FaceIndex])
                        {
                            if (it_Token.empty())
                            {
                                continue;
                            }

                            if (l_StemLower.find(it_Token) != std::string::npos)
                            {
                                l_FallbackFaces[it_FaceIndex] = it_Entry.path();
                                l_FoundFaces[it_FaceIndex] = true;
                                break;
                            }
                        }
                    }
                }

                if (l_IterError)
                {
                    TR_CORE_ERROR("Failed to enumerate fallback skybox directory '{}': {}", l_DefaultSkyboxRoot.string(), l_IterError.message());
                }

                const bool l_AllFacesDiscovered = std::all_of(l_FoundFaces.begin(), l_FoundFaces.end(), [](bool found)
                    {
                        return found;
                    });

                if (l_AllFacesDiscovered)
                {
                    l_CubemapData = Loader::SkyboxTextureLoader::LoadFromFaces(l_FallbackFaces);
                    l_CubemapSource = "PNG fallback";
                }
                else
                {
                    TR_CORE_WARN("PNG fallback skybox faces are incomplete; expected 6 unique matches in '{}'", l_DefaultSkyboxRoot.string());
                }
            }
        }

        if (!l_CubemapData.IsValid())
        {
            TR_CORE_WARN("Falling back to a solid colour cubemap because no skybox textures were found on disk");
            l_CubemapData = Loader::CubemapTextureData::CreateSolidColor(0x808080);
        }
        else if (!l_CubemapSource.empty())
        {
            TR_CORE_INFO("Skybox cubemap successfully loaded from {}", l_CubemapSource);
            // TODO: Extend this path with HDR pre-integration and IBL convolutions when the asset pipeline provides them.
        }

        if (l_CubemapData.m_Format == VK_FORMAT_UNDEFINED)
        {
            l_CubemapData.m_Format = VK_FORMAT_R8G8B8A8_SRGB;
        }

        const VkDeviceSize l_StagingSize = static_cast<VkDeviceSize>(l_CubemapData.m_PixelData.size());
        if (l_StagingSize == 0)
        {
            TR_CORE_CRITICAL("Skybox cubemap contains no pixel data");
            return;
        }

        // Stage 1: allocate a CPU-visible staging buffer so the pixel data can be uploaded efficiently.
        VkBuffer l_StagingBuffer = VK_NULL_HANDLE;
        VkDeviceMemory l_StagingMemory = VK_NULL_HANDLE;
        m_Buffers.CreateBuffer(l_StagingSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, l_StagingBuffer, l_StagingMemory);

        void* l_Data = nullptr;
        vkMapMemory(Startup::GetDevice(), l_StagingMemory, 0, l_StagingSize, 0, &l_Data);
        std::memcpy(l_Data, l_CubemapData.m_PixelData.data(), static_cast<size_t>(l_StagingSize));
        vkUnmapMemory(Startup::GetDevice(), l_StagingMemory);

        // Stage 2: create the GPU image backing the cubemap. Keeping the sample count and usage flags
        // conservative for now leaves room for future HDR/IBL upgrades without reallocating everything.
        VkImageCreateInfo l_ImageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        l_ImageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        l_ImageInfo.imageType = VK_IMAGE_TYPE_2D;
        l_ImageInfo.extent.width = l_CubemapData.m_Width;
        l_ImageInfo.extent.height = l_CubemapData.m_Height;
        l_ImageInfo.extent.depth = 1;
        l_ImageInfo.mipLevels = l_CubemapData.m_MipCount;
        l_ImageInfo.arrayLayers = 6;
        l_ImageInfo.format = l_CubemapData.m_Format;
        l_ImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        l_ImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        l_ImageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        l_ImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        l_ImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

        if (vkCreateImage(Startup::GetDevice(), &l_ImageInfo, nullptr, &m_SkyboxTextureImage) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create skybox image");
        }

        VkMemoryRequirements l_ImageRequirements{};
        vkGetImageMemoryRequirements(Startup::GetDevice(), m_SkyboxTextureImage, &l_ImageRequirements);

        VkMemoryAllocateInfo l_AllocInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        l_AllocInfo.allocationSize = l_ImageRequirements.size;
        l_AllocInfo.memoryTypeIndex = m_Buffers.FindMemoryType(l_ImageRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(Startup::GetDevice(), &l_AllocInfo, nullptr, &m_SkyboxTextureImageMemory) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to allocate skybox image memory");
        }

        vkBindImageMemory(Startup::GetDevice(), m_SkyboxTextureImage, m_SkyboxTextureImageMemory, 0);

        // Stage 3: record layout transitions and buffer copies. Future async streaming can split this
        // block so uploads happen on dedicated transfer queues.
        VkCommandBuffer l_CommandBuffer = m_Commands.BeginSingleTimeCommands();

        VkImageMemoryBarrier l_BarrierToTransfer{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        l_BarrierToTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        l_BarrierToTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        l_BarrierToTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        l_BarrierToTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        l_BarrierToTransfer.image = m_SkyboxTextureImage;
        l_BarrierToTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        l_BarrierToTransfer.subresourceRange.baseMipLevel = 0;
        l_BarrierToTransfer.subresourceRange.levelCount = l_CubemapData.m_MipCount;
        l_BarrierToTransfer.subresourceRange.baseArrayLayer = 0;
        l_BarrierToTransfer.subresourceRange.layerCount = 6;
        l_BarrierToTransfer.srcAccessMask = 0;
        l_BarrierToTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        vkCmdPipelineBarrier(l_CommandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
            0, nullptr, 0, nullptr, 1, &l_BarrierToTransfer);

        std::vector<VkBufferImageCopy> l_CopyRegions{};
        l_CopyRegions.reserve(l_CubemapData.m_FaceRegions.size() * 6);

        for (uint32_t it_Mip = 0; it_Mip < l_CubemapData.m_MipCount; ++it_Mip)
        {
            uint32_t l_MipWidth = std::max(1u, l_CubemapData.m_Width >> it_Mip);
            uint32_t l_MipHeight = std::max(1u, l_CubemapData.m_Height >> it_Mip);

            for (uint32_t it_Face = 0; it_Face < 6; ++it_Face)
            {
                const Loader::CubemapFaceRegion& l_Region = l_CubemapData.m_FaceRegions[it_Mip][it_Face];

                VkBufferImageCopy l_CopyRegion{};
                l_CopyRegion.bufferOffset = static_cast<VkDeviceSize>(l_Region.m_Offset);
                l_CopyRegion.bufferRowLength = 0;
                l_CopyRegion.bufferImageHeight = 0;
                l_CopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                l_CopyRegion.imageSubresource.mipLevel = it_Mip;
                l_CopyRegion.imageSubresource.baseArrayLayer = it_Face;
                l_CopyRegion.imageSubresource.layerCount = 1;
                l_CopyRegion.imageOffset = { 0, 0, 0 };
                l_CopyRegion.imageExtent = { l_MipWidth, l_MipHeight, 1 };

                l_CopyRegions.push_back(l_CopyRegion);
            }
        }

        vkCmdCopyBufferToImage(l_CommandBuffer, l_StagingBuffer, m_SkyboxTextureImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            static_cast<uint32_t>(l_CopyRegions.size()), l_CopyRegions.data());

        VkImageMemoryBarrier l_BarrierToShader{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        l_BarrierToShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        l_BarrierToShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        l_BarrierToShader.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        l_BarrierToShader.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        l_BarrierToShader.image = m_SkyboxTextureImage;
        l_BarrierToShader.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        l_BarrierToShader.subresourceRange.baseMipLevel = 0;
        l_BarrierToShader.subresourceRange.levelCount = l_CubemapData.m_MipCount;
        l_BarrierToShader.subresourceRange.baseArrayLayer = 0;
        l_BarrierToShader.subresourceRange.layerCount = 6;
        l_BarrierToShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        l_BarrierToShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(l_CommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
            0, nullptr, 0, nullptr, 1, &l_BarrierToShader);

        m_Commands.EndSingleTimeCommands(l_CommandBuffer);

        m_Buffers.DestroyBuffer(l_StagingBuffer, l_StagingMemory);

        // Stage 4: create the view and sampler consumed by the skybox shaders. Sampling remains simple for now
        // but clamped addressing keeps seams hidden when future HDR content arrives.
        VkImageViewCreateInfo l_ViewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        l_ViewInfo.image = m_SkyboxTextureImage;
        l_ViewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
        l_ViewInfo.format = l_CubemapData.m_Format;
        l_ViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        l_ViewInfo.subresourceRange.baseMipLevel = 0;
        l_ViewInfo.subresourceRange.levelCount = l_CubemapData.m_MipCount;
        l_ViewInfo.subresourceRange.baseArrayLayer = 0;
        l_ViewInfo.subresourceRange.layerCount = 6;

        if (vkCreateImageView(Startup::GetDevice(), &l_ViewInfo, nullptr, &m_SkyboxTextureView) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create skybox view");
        }

        VkSamplerCreateInfo l_SamplerInfo{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        l_SamplerInfo.magFilter = VK_FILTER_LINEAR;
        l_SamplerInfo.minFilter = VK_FILTER_LINEAR;
        l_SamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        l_SamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        l_SamplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        l_SamplerInfo.anisotropyEnable = VK_FALSE;
        l_SamplerInfo.maxAnisotropy = 1.0f;
        l_SamplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        l_SamplerInfo.unnormalizedCoordinates = VK_FALSE;
        l_SamplerInfo.compareEnable = VK_FALSE;
        l_SamplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
        l_SamplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        l_SamplerInfo.mipLodBias = 0.0f;
        l_SamplerInfo.minLod = 0.0f;
        l_SamplerInfo.maxLod = static_cast<float>(l_CubemapData.m_MipCount);

        if (vkCreateSampler(Startup::GetDevice(), &l_SamplerInfo, nullptr, &m_SkyboxTextureSampler) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create skybox sampler");
        }

        CreateSkyboxDescriptorSets();

        // Stage 5: rewrite the main descriptor sets so forward lighting shaders can sample the cubemap.
        UpdateSkyboxBindingOnMainSets();

        // Leave space for HDR workflow upgrades (prefiltered mip chains, BRDF LUTs) without rewriting the upload path.
    }

    void Renderer::DestroySkyboxCubemap()
    {
        VkDevice l_Device = Startup::GetDevice();

        if (m_SkyboxTextureSampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(l_Device, m_SkyboxTextureSampler, nullptr);
            m_SkyboxTextureSampler = VK_NULL_HANDLE;
        }

        if (m_SkyboxTextureView != VK_NULL_HANDLE)
        {
            vkDestroyImageView(l_Device, m_SkyboxTextureView, nullptr);
            m_SkyboxTextureView = VK_NULL_HANDLE;
        }

        if (m_SkyboxTextureImage != VK_NULL_HANDLE)
        {
            vkDestroyImage(l_Device, m_SkyboxTextureImage, nullptr);
            m_SkyboxTextureImage = VK_NULL_HANDLE;
        }

        if (m_SkyboxTextureImageMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(l_Device, m_SkyboxTextureImageMemory, nullptr);
            m_SkyboxTextureImageMemory = VK_NULL_HANDLE;
        }
    }

    void Renderer::CreateDescriptorSets()
    {
        TR_CORE_TRACE("Allocating Descriptor Sets");

        const size_t l_ImageCount = m_Swapchain.GetImageCount();
        const uint32_t l_ImageCount32 = static_cast<uint32_t>(l_ImageCount); // Vulkan expects 32-bit counts in allocation structs.

        std::vector<VkDescriptorSetLayout> l_Layouts(l_ImageCount, m_Pipeline.GetDescriptorSetLayout());

        VkDescriptorSetAllocateInfo l_AllocateInfo{};
        l_AllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        l_AllocateInfo.descriptorPool = m_DescriptorPool;
        l_AllocateInfo.descriptorSetCount = l_ImageCount32;
        l_AllocateInfo.pSetLayouts = l_Layouts.data();

        m_DescriptorSets.resize(l_ImageCount);
        if (vkAllocateDescriptorSets(Startup::GetDevice(), &l_AllocateInfo, m_DescriptorSets.data()) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to allocate descriptor sets");
        }

        const VkDeviceSize l_MaterialRange = static_cast<VkDeviceSize>(std::max<size_t>(m_MaterialBufferElementCount, static_cast<size_t>(1)) * sizeof(MaterialUniformBuffer));
        EnsureSkinningBufferCapacity(std::max(m_BonePaletteMatrixCapacity, static_cast<size_t>(s_MaxBonesPerSkeleton)));

        for (size_t i = 0; i < l_ImageCount; ++i)
        {
            VkDescriptorBufferInfo l_GlobalBufferInfo{};
            l_GlobalBufferInfo.buffer = m_GlobalUniformBuffers[i];
            l_GlobalBufferInfo.offset = 0;
            l_GlobalBufferInfo.range = sizeof(GlobalUniformBuffer);

            VkDescriptorBufferInfo l_MaterialBufferInfo{};
            l_MaterialBufferInfo.buffer = (i < m_MaterialBuffers.size()) ? m_MaterialBuffers[i] : VK_NULL_HANDLE;
            l_MaterialBufferInfo.offset = 0;
            l_MaterialBufferInfo.range = l_MaterialRange;

            VkDescriptorBufferInfo l_BonePaletteInfo{};
            l_BonePaletteInfo.buffer = (i < m_BonePaletteBuffers.size()) ? m_BonePaletteBuffers[i] : VK_NULL_HANDLE;
            l_BonePaletteInfo.offset = 0;
            l_BonePaletteInfo.range = m_BonePaletteBufferSize;

            VkDescriptorImageInfo l_AiImageInfo{};
            if (!m_TextureSlots.empty())
            {
                // Default to the white texture so descriptor validation remains happy until the AI upload completes.
                l_AiImageInfo = m_TextureSlots.front().m_Descriptor;
            }

            VkWriteDescriptorSet l_GlobalWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            l_GlobalWrite.dstSet = m_DescriptorSets[i];
            l_GlobalWrite.dstBinding = 0;
            l_GlobalWrite.dstArrayElement = 0;
            l_GlobalWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            l_GlobalWrite.descriptorCount = 1;
            l_GlobalWrite.pBufferInfo = &l_GlobalBufferInfo;

            VkWriteDescriptorSet l_MaterialWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            l_MaterialWrite.dstSet = m_DescriptorSets[i];
            l_MaterialWrite.dstBinding = 1;
            l_MaterialWrite.dstArrayElement = 0;
            // Bind the material data as a uniform buffer so the shader receives the expected descriptor type.
            l_MaterialWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            l_MaterialWrite.descriptorCount = 1;
            l_MaterialWrite.pBufferInfo = &l_MaterialBufferInfo;

            VkWriteDescriptorSet l_BonePaletteWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            l_BonePaletteWrite.dstSet = m_DescriptorSets[i];
            l_BonePaletteWrite.dstBinding = 4;
            l_BonePaletteWrite.dstArrayElement = 0;
            l_BonePaletteWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            l_BonePaletteWrite.descriptorCount = 1;
            l_BonePaletteWrite.pBufferInfo = &l_BonePaletteInfo;

            VkWriteDescriptorSet l_AiWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            l_AiWrite.dstSet = m_DescriptorSets[i];
            l_AiWrite.dstBinding = 5;
            l_AiWrite.dstArrayElement = 0;
            l_AiWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            l_AiWrite.descriptorCount = 1;
            l_AiWrite.pImageInfo = &l_AiImageInfo;

            VkWriteDescriptorSet l_Writes[] = { l_GlobalWrite, l_MaterialWrite, l_BonePaletteWrite, l_AiWrite };
            vkUpdateDescriptorSets(Startup::GetDevice(), static_cast<uint32_t>(std::size(l_Writes)), l_Writes, 0, nullptr);
        }

        RefreshTextureDescriptorBindings();
        UpdateSkyboxBindingOnMainSets();
        UpdateAiDescriptorBinding();
        CreateSkyboxDescriptorSets();

        TR_CORE_TRACE("Descriptor Sets Allocated (Main = {}, Skybox = {})", l_ImageCount, m_SkyboxDescriptorSets.size());

        MarkMaterialBuffersDirty();
    }

    void Renderer::UpdateSkyboxBindingOnMainSets()
    {
        if (m_DescriptorSets.empty())
        {
            return;
        }

        if (m_SkyboxTextureView == VK_NULL_HANDLE || m_SkyboxTextureSampler == VK_NULL_HANDLE)
        {
            TR_CORE_WARN("Skipping skybox binding update because the cubemap view or sampler is missing");
            return;
        }

        for (size_t it_Image = 0; it_Image < m_DescriptorSets.size(); ++it_Image)
        {
            VkDescriptorImageInfo l_SkyboxInfo{};
            l_SkyboxInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            l_SkyboxInfo.imageView = m_SkyboxTextureView;
            l_SkyboxInfo.sampler = m_SkyboxTextureSampler;

            VkWriteDescriptorSet l_SkyboxWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            l_SkyboxWrite.dstSet = m_DescriptorSets[it_Image];
            l_SkyboxWrite.dstBinding = 3;
            l_SkyboxWrite.dstArrayElement = 0;
            l_SkyboxWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            l_SkyboxWrite.descriptorCount = 1;
            l_SkyboxWrite.pImageInfo = &l_SkyboxInfo;

            // Synchronization note: descriptor writes are host operations, but consumers must still wait for the
            // transfer commands recorded in CreateSkyboxCubemap to finish before sampling. Our frame fence makes
            // sure the upload completes before the next draw touches the cubemap.
            vkUpdateDescriptorSets(Startup::GetDevice(), 1, &l_SkyboxWrite, 0, nullptr);
        }
    }

    void Renderer::EnsureMaterialBufferCapacity(size_t materialCount)
    {
        const size_t l_ImageCount = m_Swapchain.GetImageCount();
        const size_t l_RequiredCount = std::max(materialCount, static_cast<size_t>(1));

        if (l_ImageCount == 0)
        {
            // Record the desired size so the next swapchain creation allocates the correct capacity.
            m_MaterialBufferElementCount = l_RequiredCount;
            m_MaterialBuffers.clear();
            m_MaterialBuffersMemory.clear();
            m_MaterialBufferDirty.clear();
            return;
        }

        const bool l_SizeMismatch = (l_RequiredCount != m_MaterialBufferElementCount);
        const bool l_ImageMismatch = (m_MaterialBuffers.size() != l_ImageCount);
        if (!l_SizeMismatch && !l_ImageMismatch)
        {
            return;
        }

        if (!m_MaterialBuffers.empty() && m_ResourceFence != VK_NULL_HANDLE)
        {
            // Guarantee that no in-flight draw is still referencing the previous buffers before we recycle them.
            vkWaitForFences(Startup::GetDevice(), 1, &m_ResourceFence, VK_TRUE, UINT64_MAX);
        }

        for (size_t it_Index = 0; it_Index < m_MaterialBuffers.size(); ++it_Index)
        {
            m_Buffers.DestroyBuffer(m_MaterialBuffers[it_Index], m_MaterialBuffersMemory[it_Index]);
        }

        const VkDeviceSize l_BufferSize = static_cast<VkDeviceSize>(l_RequiredCount * sizeof(MaterialUniformBuffer));
        // Material data is consumed through a uniform buffer binding, so allocate matching buffers per swapchain image.
        m_Buffers.CreateUniformBuffers(static_cast<uint32_t>(l_ImageCount), l_BufferSize, m_MaterialBuffers, m_MaterialBuffersMemory);

        m_MaterialBufferElementCount = l_RequiredCount;
        MarkMaterialBuffersDirty();
        UpdateMaterialDescriptorBindings();
    }

    void Renderer::UpdateMaterialDescriptorBindings()
    {
        if (m_DescriptorSets.empty() || m_MaterialBuffers.empty())
        {
            return;
        }

        const VkDeviceSize l_MaterialRange = static_cast<VkDeviceSize>(std::max<size_t>(m_MaterialBufferElementCount, static_cast<size_t>(1)) * sizeof(MaterialUniformBuffer));
        for (size_t it_Image = 0; it_Image < m_DescriptorSets.size() && it_Image < m_MaterialBuffers.size(); ++it_Image)
        {
            VkDescriptorBufferInfo l_MaterialInfo{};
            l_MaterialInfo.buffer = m_MaterialBuffers[it_Image];
            l_MaterialInfo.offset = 0;
            l_MaterialInfo.range = l_MaterialRange;

            VkWriteDescriptorSet l_MaterialWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            l_MaterialWrite.dstSet = m_DescriptorSets[it_Image];
            l_MaterialWrite.dstBinding = 1;
            l_MaterialWrite.dstArrayElement = 0;
            // Maintain uniform buffer bindings whenever the material payload changes.
            l_MaterialWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            l_MaterialWrite.descriptorCount = 1;
            l_MaterialWrite.pBufferInfo = &l_MaterialInfo;

            vkUpdateDescriptorSets(Startup::GetDevice(), 1, &l_MaterialWrite, 0, nullptr);
        }
    }

    void Renderer::MarkMaterialBuffersDirty()
    {
        const size_t l_ImageCount = m_Swapchain.GetImageCount();
        m_MaterialBufferDirty.resize(l_ImageCount);
        std::fill(m_MaterialBufferDirty.begin(), m_MaterialBufferDirty.end(), true);
    }

    void Renderer::CreateSkyboxDescriptorSets()
    {
        DestroySkyboxDescriptorSets();

        size_t l_ImageCount = m_Swapchain.GetImageCount();
        if (l_ImageCount == 0 || m_SkyboxTextureView == VK_NULL_HANDLE || m_SkyboxTextureSampler == VK_NULL_HANDLE)
        {
            TR_CORE_WARN("Skipped skybox descriptor allocation because required resources are missing");
            return;
        }

        std::vector<VkDescriptorSetLayout> l_Layouts(l_ImageCount, m_Pipeline.GetSkyboxDescriptorSetLayout());

        VkDescriptorSetAllocateInfo l_AllocateInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        l_AllocateInfo.descriptorPool = m_DescriptorPool;
        l_AllocateInfo.descriptorSetCount = static_cast<uint32_t>(l_ImageCount);
        l_AllocateInfo.pSetLayouts = l_Layouts.data();

        m_SkyboxDescriptorSets.resize(l_ImageCount);
        if (vkAllocateDescriptorSets(Startup::GetDevice(), &l_AllocateInfo, m_SkyboxDescriptorSets.data()) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to allocate skybox descriptor sets");
            m_SkyboxDescriptorSets.clear();

            return;
        }

        for (size_t i = 0; i < l_ImageCount; ++i)
        {
            VkDescriptorBufferInfo l_GlobalBufferInfo{};
            l_GlobalBufferInfo.buffer = m_GlobalUniformBuffers[i];
            l_GlobalBufferInfo.offset = 0;
            l_GlobalBufferInfo.range = sizeof(GlobalUniformBuffer);

            VkDescriptorImageInfo l_CubemapInfo{};
            l_CubemapInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            l_CubemapInfo.imageView = m_SkyboxTextureView;
            l_CubemapInfo.sampler = m_SkyboxTextureSampler;

            VkWriteDescriptorSet l_GlobalWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            l_GlobalWrite.dstSet = m_SkyboxDescriptorSets[i];
            l_GlobalWrite.dstBinding = 0;
            l_GlobalWrite.dstArrayElement = 0;
            l_GlobalWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            l_GlobalWrite.descriptorCount = 1;
            l_GlobalWrite.pBufferInfo = &l_GlobalBufferInfo;

            VkWriteDescriptorSet l_CubemapWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            l_CubemapWrite.dstSet = m_SkyboxDescriptorSets[i];
            l_CubemapWrite.dstBinding = 1;
            l_CubemapWrite.dstArrayElement = 0;
            l_CubemapWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            l_CubemapWrite.descriptorCount = 1;
            l_CubemapWrite.pImageInfo = &l_CubemapInfo;

            VkWriteDescriptorSet l_Writes[] = { l_GlobalWrite, l_CubemapWrite };
            vkUpdateDescriptorSets(Startup::GetDevice(), 2, l_Writes, 0, nullptr);
        }
    }

    void Renderer::DestroySkyboxDescriptorSets()
    {
        if (!m_SkyboxDescriptorSets.empty() && m_DescriptorPool != VK_NULL_HANDLE)
        {
            vkFreeDescriptorSets(Startup::GetDevice(), m_DescriptorPool, static_cast<uint32_t>(m_SkyboxDescriptorSets.size()), m_SkyboxDescriptorSets.data());
        }

        m_SkyboxDescriptorSets.clear();
    }

    void Renderer::DestroyOffscreenResources(uint32_t viewportID)
    {
        if (viewportID == 0)
        {
            return;
        }

        ViewportContext* l_Context = FindViewportContext(viewportID);
        if (l_Context == nullptr)
        {
            return;
        }

        VkDevice l_Device = Startup::GetDevice();
        OffscreenTarget& l_Target = l_Context->m_Target;

        vkDeviceWaitIdle(l_Device);

        // TODO: LOOK INTO RAII TO HANDLE ALL THIS RESOURCE
        //if (l_Target.m_TextureID != VK_NULL_HANDLE)
        //{
        //    ImGui_ImplVulkan_RemoveTexture(l_Target.m_TextureID);
        //    l_Target.m_TextureID = VK_NULL_HANDLE;
        //}

        // The renderer owns these handles; releasing them here avoids dangling ImGui descriptors or image memory leaks. 
        if (l_Target.m_Framebuffer != VK_NULL_HANDLE)
        {
            vkDestroyFramebuffer(l_Device, l_Target.m_Framebuffer, nullptr);
            l_Target.m_Framebuffer = VK_NULL_HANDLE;
        }

        if (l_Target.m_DepthView != VK_NULL_HANDLE)
        {
            vkDestroyImageView(l_Device, l_Target.m_DepthView, nullptr);
            l_Target.m_DepthView = VK_NULL_HANDLE;
        }

        if (l_Target.m_ImageView != VK_NULL_HANDLE)
        {
            vkDestroyImageView(l_Device, l_Target.m_ImageView, nullptr);
            l_Target.m_ImageView = VK_NULL_HANDLE;
        }

        if (l_Target.m_DepthImage != VK_NULL_HANDLE)
        {
            vkDestroyImage(l_Device, l_Target.m_DepthImage, nullptr);
            l_Target.m_DepthImage = VK_NULL_HANDLE;
        }

        if (l_Target.m_Image != VK_NULL_HANDLE)
        {
            vkDestroyImage(l_Device, l_Target.m_Image, nullptr);
            l_Target.m_Image = VK_NULL_HANDLE;
        }
        if (l_Target.m_DepthMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(l_Device, l_Target.m_DepthMemory, nullptr);
            l_Target.m_DepthMemory = VK_NULL_HANDLE;
        }

        if (l_Target.m_Memory != VK_NULL_HANDLE)
        {
            vkFreeMemory(l_Device, l_Target.m_Memory, nullptr);
            l_Target.m_Memory = VK_NULL_HANDLE;
        }

        if (l_Target.m_Sampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(l_Device, l_Target.m_Sampler, nullptr);
            l_Target.m_Sampler = VK_NULL_HANDLE;
        }

        l_Target.m_Extent = { 0, 0 };
        l_Target.m_CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        l_Target.m_DepthLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        l_Context->m_CachedExtent = { 0, 0 };
        l_Context->m_Info.Size = { 0.0f, 0.0f };
    }

    void Renderer::DestroyAllOffscreenResources()
    {
        // Iterate carefully so erasing entries mid-loop remains valid across MSVC/STL implementations.
        auto it_Context = m_ViewportContexts.begin();
        while (it_Context != m_ViewportContexts.end())
        {
            const uint32_t l_ViewportId = it_Context->first;
            ++it_Context;
            DestroyOffscreenResources(l_ViewportId);
        }

        // Future: consider retaining unused targets in a pool so secondary editor panels can resume instantly.
        m_ActiveViewportId = 0;
    }

    Renderer::ViewportContext& Renderer::GetOrCreateViewportContext(uint32_t viewportID)
    {
        auto [it_Context, l_Inserted] = m_ViewportContexts.try_emplace(viewportID);
        ViewportContext& l_Context = it_Context->second;

        if (l_Inserted)
        {
            l_Context.m_Info.ViewportID = viewportID;
            l_Context.m_Target.m_Extent = { 0, 0 };
            l_Context.m_Target.m_CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            l_Context.m_Target.m_DepthLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        }

        return l_Context;
    }

    const Renderer::ViewportContext* Renderer::FindViewportContext(uint32_t viewportID) const
    {
        const auto it_Context = m_ViewportContexts.find(viewportID);
        if (it_Context == m_ViewportContexts.end())
        {
            return nullptr;
        }

        return &it_Context->second;
    }

    Renderer::ViewportContext* Renderer::FindViewportContext(uint32_t viewportID)
    {
        return const_cast<ViewportContext*>(static_cast<const Renderer*>(this)->FindViewportContext(viewportID));
    }

    const Camera* Renderer::GetActiveCamera(const ViewportContext& context) const
    {
        const uint32_t l_ViewportId = context.m_Info.ViewportID;

        if (l_ViewportId == 1U)
        {
            // The Scene viewport must always reflect the editor's navigation camera.
            return m_EditorCamera;
        }

        if (l_ViewportId == 2U)
        {
            // The Game viewport only exposes the runtime camera once the simulation has produced valid matrices.
            if (m_RuntimeCameraReady && m_RuntimeCamera)
            {
                return m_RuntimeCamera;
            }

            // Fall back to the editor camera so the viewport continues to display a useful image.
            return m_EditorCamera;
        }

        // Additional viewports prefer the editor output; if none exist allow the caller to fall back to identity matrices.
        if (m_EditorCamera)
        {
            return m_EditorCamera;
        }

        return (m_RuntimeCameraReady && m_RuntimeCamera) ? m_RuntimeCamera : nullptr;
    }

    void Renderer::CreateOrResizeOffscreenResources(OffscreenTarget& target, VkExtent2D extent)
    {
        VkDevice l_Device = Startup::GetDevice();

        // Ensure the GPU is idle before we reuse or release any image memory.
        vkDeviceWaitIdle(l_Device);

        auto a_ResetTarget = [l_Device](OffscreenTarget& target)
            {
                if (target.m_TextureID != VK_NULL_HANDLE)
                {
                    ImGui_ImplVulkan_RemoveTexture(target.m_TextureID);
                    target.m_TextureID = VK_NULL_HANDLE;
                }

                if (target.m_Framebuffer != VK_NULL_HANDLE)
                {
                    vkDestroyFramebuffer(l_Device, target.m_Framebuffer, nullptr);
                    target.m_Framebuffer = VK_NULL_HANDLE;
                }

                if (target.m_DepthView != VK_NULL_HANDLE)
                {
                    vkDestroyImageView(l_Device, target.m_DepthView, nullptr);
                    target.m_DepthView = VK_NULL_HANDLE;
                }

                if (target.m_ImageView != VK_NULL_HANDLE)
                {
                    vkDestroyImageView(l_Device, target.m_ImageView, nullptr);
                    target.m_ImageView = VK_NULL_HANDLE;
                }

                if (target.m_DepthImage != VK_NULL_HANDLE)
                {
                    vkDestroyImage(l_Device, target.m_DepthImage, nullptr);
                    target.m_DepthImage = VK_NULL_HANDLE;
                }

                if (target.m_Image != VK_NULL_HANDLE)
                {
                    vkDestroyImage(l_Device, target.m_Image, nullptr);
                    target.m_Image = VK_NULL_HANDLE;
                }

                if (target.m_DepthMemory != VK_NULL_HANDLE)
                {
                    vkFreeMemory(l_Device, target.m_DepthMemory, nullptr);
                    target.m_DepthMemory = VK_NULL_HANDLE;
                }

                if (target.m_Memory != VK_NULL_HANDLE)
                {
                    vkFreeMemory(l_Device, target.m_Memory, nullptr);
                    target.m_Memory = VK_NULL_HANDLE;
                }

                if (target.m_Sampler != VK_NULL_HANDLE)
                {
                    vkDestroySampler(l_Device, target.m_Sampler, nullptr);
                    target.m_Sampler = VK_NULL_HANDLE;
                }

                target.m_Extent = { 0, 0 };
                target.m_CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                target.m_DepthLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            };

        a_ResetTarget(target);

        if (extent.width == 0 || extent.height == 0)
        {
            return;
        }

        VkImageCreateInfo l_ImageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        l_ImageInfo.imageType = VK_IMAGE_TYPE_2D;
        l_ImageInfo.extent.width = extent.width;
        l_ImageInfo.extent.height = extent.height;
        l_ImageInfo.extent.depth = 1;
        l_ImageInfo.mipLevels = 1;
        l_ImageInfo.arrayLayers = 1;
        l_ImageInfo.format = m_Swapchain.GetImageFormat();
        l_ImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        l_ImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        l_ImageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        l_ImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        l_ImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

        if (vkCreateImage(l_Device, &l_ImageInfo, nullptr, &target.m_Image) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create offscreen image");

            a_ResetTarget(target);

            return;
        }

        VkMemoryRequirements l_MemoryRequirements{};
        vkGetImageMemoryRequirements(l_Device, target.m_Image, &l_MemoryRequirements);

        VkMemoryAllocateInfo l_AllocateInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        l_AllocateInfo.allocationSize = l_MemoryRequirements.size;
        l_AllocateInfo.memoryTypeIndex = m_Buffers.FindMemoryType(l_MemoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(l_Device, &l_AllocateInfo, nullptr, &target.m_Memory) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to allocate offscreen image memory");

            a_ResetTarget(target);

            return;
        }

        vkBindImageMemory(l_Device, target.m_Image, target.m_Memory, 0);

        VkImageViewCreateInfo l_ViewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        l_ViewInfo.image = target.m_Image;
        l_ViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        l_ViewInfo.format = l_ImageInfo.format;
        l_ViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        l_ViewInfo.subresourceRange.baseMipLevel = 0;
        l_ViewInfo.subresourceRange.levelCount = 1;
        l_ViewInfo.subresourceRange.baseArrayLayer = 0;
        l_ViewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(l_Device, &l_ViewInfo, nullptr, &target.m_ImageView) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create offscreen image view");

            a_ResetTarget(target);

            return;
        }

        // Mirror the swapchain depth handling so editor viewports respect the same occlusion rules.
        VkImageCreateInfo l_DepthInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        l_DepthInfo.imageType = VK_IMAGE_TYPE_2D;
        l_DepthInfo.extent.width = extent.width;
        l_DepthInfo.extent.height = extent.height;
        l_DepthInfo.extent.depth = 1;
        l_DepthInfo.mipLevels = 1;
        l_DepthInfo.arrayLayers = 1;
        l_DepthInfo.format = m_Pipeline.GetDepthFormat();
        l_DepthInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        l_DepthInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        l_DepthInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        l_DepthInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        l_DepthInfo.samples = VK_SAMPLE_COUNT_1_BIT;

        if (vkCreateImage(l_Device, &l_DepthInfo, nullptr, &target.m_DepthImage) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create offscreen depth image");

            a_ResetTarget(target);

            return;
        }

        VkMemoryRequirements l_DepthRequirements{};
        vkGetImageMemoryRequirements(l_Device, target.m_DepthImage, &l_DepthRequirements);

        VkMemoryAllocateInfo l_DepthAllocate{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        l_DepthAllocate.allocationSize = l_DepthRequirements.size;
        l_DepthAllocate.memoryTypeIndex = m_Buffers.FindMemoryType(l_DepthRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(l_Device, &l_DepthAllocate, nullptr, &target.m_DepthMemory) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to allocate offscreen depth memory");

            a_ResetTarget(target);

            return;
        }

        vkBindImageMemory(l_Device, target.m_DepthImage, target.m_DepthMemory, 0);

        VkImageViewCreateInfo l_DepthViewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        l_DepthViewInfo.image = target.m_DepthImage;
        l_DepthViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        l_DepthViewInfo.format = l_DepthInfo.format;
        l_DepthViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        l_DepthViewInfo.subresourceRange.baseMipLevel = 0;
        l_DepthViewInfo.subresourceRange.levelCount = 1;
        l_DepthViewInfo.subresourceRange.baseArrayLayer = 0;
        l_DepthViewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(l_Device, &l_DepthViewInfo, nullptr, &target.m_DepthView) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create offscreen depth view");

            a_ResetTarget(target);

            return;
        }

        std::array<VkImageView, 2> l_FramebufferAttachments{ target.m_ImageView, target.m_DepthView };
        VkFramebufferCreateInfo l_FramebufferInfo{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
        l_FramebufferInfo.renderPass = m_Pipeline.GetRenderPass();
        l_FramebufferInfo.attachmentCount = static_cast<uint32_t>(l_FramebufferAttachments.size());
        l_FramebufferInfo.pAttachments = l_FramebufferAttachments.data();
        l_FramebufferInfo.width = extent.width;
        l_FramebufferInfo.height = extent.height;
        l_FramebufferInfo.layers = 1;

        if (vkCreateFramebuffer(l_Device, &l_FramebufferInfo, nullptr, &target.m_Framebuffer) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create offscreen framebuffer");

            a_ResetTarget(target);

            return;
        }

        VkSamplerCreateInfo l_SamplerInfo{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        l_SamplerInfo.magFilter = VK_FILTER_LINEAR;
        l_SamplerInfo.minFilter = VK_FILTER_LINEAR;
        l_SamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        l_SamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        l_SamplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        l_SamplerInfo.anisotropyEnable = VK_FALSE;
        l_SamplerInfo.maxAnisotropy = 1.0f;
        l_SamplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
        l_SamplerInfo.unnormalizedCoordinates = VK_FALSE;
        l_SamplerInfo.compareEnable = VK_FALSE;
        l_SamplerInfo.compareOp = VK_COMPARE_OP_NEVER;
        l_SamplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        l_SamplerInfo.mipLodBias = 0.0f;
        l_SamplerInfo.minLod = 0.0f;
        l_SamplerInfo.maxLod = 0.0f;

        if (vkCreateSampler(l_Device, &l_SamplerInfo, nullptr, &target.m_Sampler) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create offscreen sampler");

            a_ResetTarget(target);

            return;
        }

        VkCommandBuffer l_BootstrapCommandBuffer = m_Commands.GetOneTimePool().Acquire();
        VkCommandBufferBeginInfo l_BootstrapBeginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        l_BootstrapBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(l_BootstrapCommandBuffer, &l_BootstrapBeginInfo);

        VkImageMemoryBarrier l_BootstrapBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        l_BootstrapBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        l_BootstrapBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        l_BootstrapBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        l_BootstrapBarrier.subresourceRange.baseMipLevel = 0;
        l_BootstrapBarrier.subresourceRange.levelCount = 1;
        l_BootstrapBarrier.subresourceRange.baseArrayLayer = 0;
        l_BootstrapBarrier.subresourceRange.layerCount = 1;
        l_BootstrapBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        l_BootstrapBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        l_BootstrapBarrier.srcAccessMask = 0;
        l_BootstrapBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        l_BootstrapBarrier.image = target.m_Image;

        // Bootstrap the image layout so descriptor writes and validation stay in sync when the viewport samples before the first render pass.
        vkCmdPipelineBarrier(l_BootstrapCommandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &l_BootstrapBarrier);

        vkEndCommandBuffer(l_BootstrapCommandBuffer);

        VkSubmitInfo l_BootstrapSubmitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
        l_BootstrapSubmitInfo.commandBufferCount = 1;
        l_BootstrapSubmitInfo.pCommandBuffers = &l_BootstrapCommandBuffer;

        vkQueueSubmit(Startup::GetGraphicsQueue(), 1, &l_BootstrapSubmitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(Startup::GetGraphicsQueue());

        m_Commands.GetOneTimePool().Release(l_BootstrapCommandBuffer);

        // Register (or refresh) the descriptor used by the viewport panel and keep it cached for quick retrieval.
        target.m_TextureID = ImGui_ImplVulkan_AddTexture(target.m_Sampler, target.m_ImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        target.m_Extent = extent;
        target.m_CurrentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        target.m_DepthLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        TR_CORE_TRACE("Offscreen render target resized to {}x{}", extent.width, extent.height);
    }

    bool Renderer::AcquireNextImage(uint32_t& imageIndex, VkFence inFlightFence)
    {
        VkResult l_Result = vkAcquireNextImageKHR(Startup::GetDevice(), m_Swapchain.GetSwapchain(), UINT64_MAX,
            m_Commands.GetImageAvailableSemaphorePerImage(m_Commands.CurrentFrame()), VK_NULL_HANDLE, &imageIndex);

        if (l_Result == VK_ERROR_OUT_OF_DATE_KHR)
        {
            // The swapchain is no longer compatible with the window surface; rebuild and skip this frame.
            TR_CORE_WARN("Swapchain out of date detected during AcquireNextImage, recreating and skipping frame");
            RecreateSwapchain();

            return false;
        }

        if (l_Result != VK_SUCCESS && l_Result != VK_SUBOPTIMAL_KHR)
        {
            TR_CORE_CRITICAL("Failed to acquire swap chain image!");

            return false;
        }

        VkFence& l_ImageFence = m_Commands.GetImageInFlight(imageIndex);
        if (l_ImageFence != VK_NULL_HANDLE)
        {
            vkWaitForFences(Startup::GetDevice(), 1, &l_ImageFence, VK_TRUE, UINT64_MAX);
        }

        m_Commands.SetImageInFlight(imageIndex, inFlightFence);

        return true;
    }

    bool Renderer::RecordCommandBuffer(uint32_t imageIndex)
    {
        // Collect sprite draw requests up front so the render pass can submit them without additional ECS lookups.
        GatherSpriteDraws();

        m_TextRenderer.BeginFrame();
        for (const auto& a_ViewportEntry : m_TextSubmissionQueue)
        {
            const uint32_t l_ViewportId = a_ViewportEntry.first;
            const std::vector<TextSubmission>& l_Submissions = a_ViewportEntry.second;
            for (const TextSubmission& l_Submission : l_Submissions)
            {
                m_TextRenderer.QueueText(l_ViewportId, l_Submission.m_Position, l_Submission.m_Color, l_Submission.m_Text);
            }
        }
        m_TextSubmissionQueue.clear();

        VkCommandBuffer l_CommandBuffer = m_Commands.GetCommandBuffer(imageIndex);

        VkCommandBufferBeginInfo l_BeginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        vkBeginCommandBuffer(l_CommandBuffer, &l_BeginInfo);

        auto a_BuildClearValue = [this]() -> VkClearValue
            {
                VkClearValue l_Value{};
                l_Value.color.float32[0] = m_ClearColor.r;
                l_Value.color.float32[1] = m_ClearColor.g;
                l_Value.color.float32[2] = m_ClearColor.b;
                l_Value.color.float32[3] = m_ClearColor.a;

                return l_Value;
            };

        ViewportContext* l_PrimaryContext = FindViewportContext(m_ActiveViewportId);
        OffscreenTarget* l_PrimaryTarget = nullptr;
        if (l_PrimaryContext && IsValidViewport(l_PrimaryContext->m_Info) && l_PrimaryContext->m_Target.m_Framebuffer != VK_NULL_HANDLE)
        {
            l_PrimaryTarget = &l_PrimaryContext->m_Target;
        }

        if (m_ReadbackResizePending)
        {
            ApplyPendingReadbackResize();
        }

        bool l_RenderedViewport = false;

        // Prepare the shared draw lists once so each viewport iteration can reuse the same data set.
        GatherMeshDraws();
        PrepareBonePaletteBuffer(imageIndex);

        auto a_RenderViewport = [&](uint32_t viewportID, ViewportContext& context, bool isPrimary)
            {
                OffscreenTarget& l_Target = context.m_Target;
                if (!IsValidViewport(context.m_Info) || l_Target.m_Framebuffer == VK_NULL_HANDLE)
                {
                    return;
                }

                if (l_Target.m_Extent.width == 0 || l_Target.m_Extent.height == 0)
                {
                    return;
                }

                l_RenderedViewport = true;

                // Temporarily mark the context as active so shared helpers resolve relative camera state correctly.
                const uint32_t l_PreviousViewportId = m_ActiveViewportId;
                m_ActiveViewportId = context.m_Info.ViewportID;

                const Camera* l_ContextCamera = GetActiveCamera(context);

                if (context.m_Info.ViewportID == 2U && !m_RuntimeCameraReady)
                {
                    // When the runtime camera has not been initialised we expect to reuse the editor matrices instead.
                    assert(l_ContextCamera == nullptr || l_ContextCamera == m_EditorCamera);
                    l_ContextCamera = nullptr;
                }

                UpdateUniformBuffer(imageIndex, l_ContextCamera, l_CommandBuffer);

                // Restore the previously active viewport so editor interactions remain consistent outside this pass.
                m_ActiveViewportId = l_PreviousViewportId;

                VkPipelineStageFlags l_PreviousStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                VkAccessFlags l_PreviousAccess = 0;
                if (l_Target.m_CurrentLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                {
                    l_PreviousStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
                    l_PreviousAccess = VK_ACCESS_SHADER_READ_BIT;
                }
                else if (l_Target.m_CurrentLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
                {
                    l_PreviousStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
                    l_PreviousAccess = VK_ACCESS_TRANSFER_READ_BIT;
                }

                VkPipelineStageFlags l_DepthPreviousStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                VkAccessFlags l_DepthPreviousAccess = 0;
                if (l_Target.m_DepthLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
                {
                    l_DepthPreviousStage = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
                    l_DepthPreviousAccess = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                }

                VkImageMemoryBarrier l_PrepareDepth{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
                l_PrepareDepth.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                l_PrepareDepth.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                l_PrepareDepth.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
                l_PrepareDepth.subresourceRange.baseMipLevel = 0;
                l_PrepareDepth.subresourceRange.levelCount = 1;
                l_PrepareDepth.subresourceRange.baseArrayLayer = 0;
                l_PrepareDepth.subresourceRange.layerCount = 1;
                l_PrepareDepth.oldLayout = l_Target.m_DepthLayout;
                l_PrepareDepth.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                l_PrepareDepth.srcAccessMask = l_DepthPreviousAccess;
                l_PrepareDepth.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                l_PrepareDepth.image = l_Target.m_DepthImage;

                vkCmdPipelineBarrier(l_CommandBuffer, l_DepthPreviousStage, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0, 0, nullptr, 0, nullptr, 1, &l_PrepareDepth);
                l_Target.m_DepthLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

                VkImageMemoryBarrier l_PrepareOffscreen{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
                l_PrepareOffscreen.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                l_PrepareOffscreen.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                l_PrepareOffscreen.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                l_PrepareOffscreen.subresourceRange.baseMipLevel = 0;
                l_PrepareOffscreen.subresourceRange.levelCount = 1;
                l_PrepareOffscreen.subresourceRange.baseArrayLayer = 0;
                l_PrepareOffscreen.subresourceRange.layerCount = 1;
                l_PrepareOffscreen.oldLayout = l_Target.m_CurrentLayout;
                l_PrepareOffscreen.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                l_PrepareOffscreen.srcAccessMask = l_PreviousAccess;
                l_PrepareOffscreen.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                l_PrepareOffscreen.image = l_Target.m_Image;

                vkCmdPipelineBarrier(l_CommandBuffer, l_PreviousStage, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &l_PrepareOffscreen);
                l_Target.m_CurrentLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

                VkRenderPassBeginInfo l_OffscreenPass{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
                l_OffscreenPass.renderPass = m_Pipeline.GetRenderPass();
                l_OffscreenPass.framebuffer = l_Target.m_Framebuffer;
                l_OffscreenPass.renderArea.offset = { 0, 0 };
                l_OffscreenPass.renderArea.extent = l_Target.m_Extent;

                std::array<VkClearValue, 2> l_OffscreenClearValues{};
                l_OffscreenClearValues[0] = a_BuildClearValue();
                l_OffscreenClearValues[1].depthStencil.depth = 1.0f;
                l_OffscreenClearValues[1].depthStencil.stencil = 0;
                l_OffscreenPass.clearValueCount = static_cast<uint32_t>(l_OffscreenClearValues.size());
                l_OffscreenPass.pClearValues = l_OffscreenClearValues.data();

                vkCmdBeginRenderPass(l_CommandBuffer, &l_OffscreenPass, VK_SUBPASS_CONTENTS_INLINE);

                VkClearAttachment l_ColorAttachmentClear{};
                l_ColorAttachmentClear.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                l_ColorAttachmentClear.colorAttachment = 0;
                l_ColorAttachmentClear.clearValue.color.float32[0] = m_ClearColor.r;
                l_ColorAttachmentClear.clearValue.color.float32[1] = m_ClearColor.g;
                l_ColorAttachmentClear.clearValue.color.float32[2] = m_ClearColor.b;
                l_ColorAttachmentClear.clearValue.color.float32[3] = m_ClearColor.a;

                VkClearRect l_ColorClearRect{};
                l_ColorClearRect.rect.offset = { 0, 0 };
                l_ColorClearRect.rect.extent = l_Target.m_Extent;
                l_ColorClearRect.baseArrayLayer = 0;
                l_ColorClearRect.layerCount = 1;

                vkCmdClearAttachments(l_CommandBuffer, 1, &l_ColorAttachmentClear, 1, &l_ColorClearRect);
                // Manual QA: Scene and Game panels now show independent images driven by their respective camera selections.
                // TODO: Explore asynchronous command recording so runtime and editor paths can execute in parallel where feasible.

                VkViewport l_OffscreenViewport{};
                l_OffscreenViewport.x = 0.0f;
                l_OffscreenViewport.y = 0.0f;
                l_OffscreenViewport.width = static_cast<float>(l_Target.m_Extent.width);
                l_OffscreenViewport.height = static_cast<float>(l_Target.m_Extent.height);
                l_OffscreenViewport.minDepth = 0.0f;
                l_OffscreenViewport.maxDepth = 1.0f;
                vkCmdSetViewport(l_CommandBuffer, 0, 1, &l_OffscreenViewport);

                VkRect2D l_OffscreenScissor{};
                l_OffscreenScissor.offset = { 0, 0 };
                l_OffscreenScissor.extent = l_Target.m_Extent;
                vkCmdSetScissor(l_CommandBuffer, 0, 1, &l_OffscreenScissor);

                const VkPipeline l_SkyboxPipeline = m_Pipeline.GetSkyboxPipeline();
                const bool l_HasSkyboxDescriptors = imageIndex < m_SkyboxDescriptorSets.size() && m_SkyboxDescriptorSets[imageIndex] != VK_NULL_HANDLE;
                if (l_HasSkyboxDescriptors && l_SkyboxPipeline != VK_NULL_HANDLE)
                {
                    // Guard the skybox bind so a missing pipeline during hot-reload does not poison the command buffer.
                    vkCmdBindPipeline(l_CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, l_SkyboxPipeline);
                    m_Skybox.Record(l_CommandBuffer, m_Pipeline.GetSkyboxPipelineLayout(), m_SkyboxDescriptorSets.data(), imageIndex);
                }
                else if (l_HasSkyboxDescriptors && l_SkyboxPipeline == VK_NULL_HANDLE)
                {
                    // Warn once the pipeline disappears so hot-reload issues surface quickly while keeping the pass valid.
                    TR_CORE_WARN("Skybox pipeline missing; skipping skybox draw for viewport {} until the pipeline is rebuilt.", context.m_Info.ViewportID);
                }

                const VkPipeline l_RenderPipeline = m_Pipeline.GetPipeline();
                const bool l_CanRender = l_RenderPipeline != VK_NULL_HANDLE;
                if (l_CanRender)
                {
                    // Avoid binding a null pipeline so command recording stays valid if hot-reload briefly invalidates pipelines.
                    vkCmdBindPipeline(l_CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, l_RenderPipeline);

                    const bool l_HasDescriptorSet = imageIndex < m_DescriptorSets.size();
                    if (l_HasDescriptorSet)
                    {
                        vkCmdBindDescriptorSets(l_CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline.GetPipelineLayout(), 0, 1, &m_DescriptorSets[imageIndex], 0, nullptr);
                    }

                    if (m_VertexBuffer != VK_NULL_HANDLE && m_IndexBuffer != VK_NULL_HANDLE && !m_MeshDrawInfo.empty() && !m_MeshDrawCommands.empty() && l_HasDescriptorSet)
                    {
                        VkBuffer l_VertexBuffers[] = { m_VertexBuffer };
                        VkDeviceSize l_Offsets[] = { 0 };
                        vkCmdBindVertexBuffers(l_CommandBuffer, 0, 1, l_VertexBuffers, l_Offsets);
                        vkCmdBindIndexBuffer(l_CommandBuffer, m_IndexBuffer, 0, VK_INDEX_TYPE_UINT32);

                        for (const MeshDrawCommand& l_Command : m_MeshDrawCommands)
                        {
                            if (!l_Command.m_Component)
                            {
                                continue;
                            }

                            const MeshComponent& l_Component = *l_Command.m_Component;
                            if (l_Component.m_MeshIndex >= m_MeshDrawInfo.size())
                            {
                                continue;
                            }

                            const MeshDrawInfo& l_DrawInfo = m_MeshDrawInfo[l_Component.m_MeshIndex];
                            if (l_DrawInfo.m_IndexCount == 0)
                            {
                                continue;
                            }

                            RenderablePushConstant l_PushConstant{};
                            l_PushConstant.m_ModelMatrix = l_Command.m_ModelMatrix;
                            int32_t l_MaterialIndex = l_DrawInfo.m_MaterialIndex;
                            int32_t l_TextureSlot = 0;
                            if (l_Command.m_TextureComponent != nullptr && l_Command.m_TextureComponent->m_TextureSlot >= 0)
                            {
                                // Prefer the entity supplied texture slot so material overrides remain reactive in-editor.
                                l_TextureSlot = l_Command.m_TextureComponent->m_TextureSlot;
                            }
                            else if (l_MaterialIndex >= 0 && static_cast<size_t>(l_MaterialIndex) < m_Materials.size())
                            {
                                l_TextureSlot = m_Materials[l_MaterialIndex].BaseColorTextureSlot;
                            }

                            l_PushConstant.m_TextureSlot = l_TextureSlot;
                            l_PushConstant.m_MaterialIndex = l_MaterialIndex;
                            l_PushConstant.m_BoneOffset = static_cast<int32_t>(l_Command.m_BoneOffset);
                            l_PushConstant.m_BoneCount = static_cast<int32_t>(l_Command.m_BoneCount);
                            vkCmdPushConstants(l_CommandBuffer, m_Pipeline.GetPipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                sizeof(RenderablePushConstant), &l_PushConstant);

                            vkCmdDrawIndexed(l_CommandBuffer, l_DrawInfo.m_IndexCount, 1, l_DrawInfo.m_FirstIndex, l_DrawInfo.m_BaseVertex, 0);
                        }
                    }

                    if (l_HasDescriptorSet)
                    {
                        DrawSprites(l_CommandBuffer, imageIndex);
                    }

                    m_TextRenderer.RecordViewport(l_CommandBuffer, imageIndex, context.m_Info.ViewportID, l_Target.m_Extent);
                }
                else
                {
                    TR_CORE_WARN("Primary render pipeline missing; skipping offscreen draw for viewport {} until pipelines are rebuilt.", context.m_Info.ViewportID);
                }

                vkCmdEndRenderPass(l_CommandBuffer);

                VkImageMemoryBarrier l_OffscreenBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
                l_OffscreenBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                l_OffscreenBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                l_OffscreenBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                l_OffscreenBarrier.subresourceRange.baseMipLevel = 0;
                l_OffscreenBarrier.subresourceRange.levelCount = 1;
                l_OffscreenBarrier.subresourceRange.baseArrayLayer = 0;
                l_OffscreenBarrier.subresourceRange.layerCount = 1;
                l_OffscreenBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                l_OffscreenBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                l_OffscreenBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                l_OffscreenBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                l_OffscreenBarrier.image = l_Target.m_Image;

                vkCmdPipelineBarrier(l_CommandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                    0, 0, nullptr, 0, nullptr, 1, &l_OffscreenBarrier);
                l_Target.m_CurrentLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

                if (!isPrimary)
                {
                    VkImageMemoryBarrier l_ToSample{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
                    l_ToSample.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    l_ToSample.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    l_ToSample.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    l_ToSample.subresourceRange.baseMipLevel = 0;
                    l_ToSample.subresourceRange.levelCount = 1;
                    l_ToSample.subresourceRange.baseArrayLayer = 0;
                    l_ToSample.subresourceRange.layerCount = 1;
                    l_ToSample.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                    l_ToSample.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    l_ToSample.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                    l_ToSample.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                    l_ToSample.image = l_Target.m_Image;

                    vkCmdPipelineBarrier(l_CommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                        0, 0, nullptr, 0, nullptr, 1, &l_ToSample);
                    l_Target.m_CurrentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                }
            };

        for (auto& it_Context : m_ViewportContexts)
        {
            if (it_Context.first == m_ActiveViewportId)
            {
                continue;
            }

            a_RenderViewport(it_Context.first, it_Context.second, false);
        }

        if (l_PrimaryContext && l_PrimaryTarget)
        {
            a_RenderViewport(m_ActiveViewportId, *l_PrimaryContext, true);
        }

        if (!l_RenderedViewport)
        {
            UpdateUniformBuffer(imageIndex, nullptr, l_CommandBuffer);
        }

        if (l_PrimaryTarget && (l_PrimaryTarget->m_Framebuffer == VK_NULL_HANDLE || l_PrimaryTarget->m_Extent.width == 0 || l_PrimaryTarget->m_Extent.height == 0))
        {
            l_PrimaryTarget = nullptr;
        }

        const bool l_PrimaryViewportActive = l_PrimaryTarget != nullptr;

        VkImage l_SwapchainImage = m_Swapchain.GetImages()[imageIndex];
        VkImage l_SwapchainDepthImage = VK_NULL_HANDLE;
        const auto& l_DepthImages = m_Pipeline.GetDepthImages();
        if (imageIndex < l_DepthImages.size())
        {
            l_SwapchainDepthImage = l_DepthImages[imageIndex];
        }
        VkPipelineStageFlags l_SwapchainSrcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        VkAccessFlags l_SwapchainSrcAccess = 0;
        VkImageLayout l_PreviousLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        if (imageIndex < m_SwapchainImageLayouts.size())
        {
            l_PreviousLayout = m_SwapchainImageLayouts[imageIndex];
        }

        // Map the cached layout to the pipeline stage/access masks the barrier expects.
        switch (l_PreviousLayout)
        {
        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
            // Presented images relax to bottom-of-pipe with no further access requirements.
            l_SwapchainSrcStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
            l_SwapchainSrcAccess = 0;
            break;
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            l_SwapchainSrcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            l_SwapchainSrcAccess = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            l_SwapchainSrcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            l_SwapchainSrcAccess = VK_ACCESS_TRANSFER_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_UNDEFINED:
        default:
            // Fresh images begin at the top of the pipe with no access hazards to satisfy.
            l_SwapchainSrcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            l_SwapchainSrcAccess = 0;
            break;
        }

        // Prepare the swapchain image for either a blit copy or an explicit clear prior to the presentation render pass.
        VkImageMemoryBarrier l_PrepareSwapchain{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        l_PrepareSwapchain.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        l_PrepareSwapchain.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        l_PrepareSwapchain.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        l_PrepareSwapchain.subresourceRange.baseMipLevel = 0;
        l_PrepareSwapchain.subresourceRange.levelCount = 1;
        l_PrepareSwapchain.subresourceRange.baseArrayLayer = 0;
        l_PrepareSwapchain.subresourceRange.layerCount = 1;
        l_PrepareSwapchain.oldLayout = l_PreviousLayout;
        l_PrepareSwapchain.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        l_PrepareSwapchain.srcAccessMask = l_SwapchainSrcAccess;
        l_PrepareSwapchain.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        l_PrepareSwapchain.image = l_SwapchainImage;

        vkCmdPipelineBarrier(l_CommandBuffer, l_SwapchainSrcStage, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &l_PrepareSwapchain);
        // Persist the layout change so the next frame knows the transfer destination state is active and validation stays happy.
        if (imageIndex < m_SwapchainImageLayouts.size())
        {
            m_SwapchainImageLayouts[imageIndex] = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        }

        if (l_PrimaryViewportActive)
        {
            if (m_ReadbackEnabled && imageIndex < m_FrameReadbackBuffers.size() && imageIndex < m_FrameReadbackPending.size())
            {
                const bool l_ExtentMatches = (m_FrameReadbackExtent.width == l_PrimaryTarget->m_Extent.width) && (m_FrameReadbackExtent.height == l_PrimaryTarget->m_Extent.height);
                VkBuffer l_ReadbackBuffer = m_FrameReadbackBuffers[imageIndex];

                if (l_ExtentMatches && l_ReadbackBuffer != VK_NULL_HANDLE)
                {
                    VkBufferImageCopy l_ReadbackRegion{};
                    l_ReadbackRegion.bufferOffset = 0;
                    l_ReadbackRegion.bufferRowLength = 0;
                    l_ReadbackRegion.bufferImageHeight = 0;
                    l_ReadbackRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    l_ReadbackRegion.imageSubresource.mipLevel = 0;
                    l_ReadbackRegion.imageSubresource.baseArrayLayer = 0;
                    l_ReadbackRegion.imageSubresource.layerCount = 1;
                    l_ReadbackRegion.imageOffset = { 0, 0, 0 };
                    l_ReadbackRegion.imageExtent = { l_PrimaryTarget->m_Extent.width, l_PrimaryTarget->m_Extent.height, 1 };

                    // Copy the rendered colour attachment into a CPU-visible buffer so AI tooling can inspect the pixels.
                    vkCmdCopyImageToBuffer(l_CommandBuffer, l_PrimaryTarget->m_Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, l_ReadbackBuffer, 1, &l_ReadbackRegion);

                    VkBufferMemoryBarrier l_ReadbackBarrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
                    l_ReadbackBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    l_ReadbackBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    l_ReadbackBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                    l_ReadbackBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
                    l_ReadbackBarrier.buffer = l_ReadbackBuffer;
                    l_ReadbackBarrier.offset = 0;
                    l_ReadbackBarrier.size = VK_WHOLE_SIZE;

                    vkCmdPipelineBarrier(l_CommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &l_ReadbackBarrier, 0, nullptr);

                    m_FrameReadbackPending[imageIndex] = true;
                }
                else
                {
                    // Resolution mismatches are expected once asynchronous readback arrives; skip the copy for now.
                    m_FrameReadbackPending[imageIndex] = false;
                }
            }
            else if (imageIndex < m_FrameReadbackPending.size())
            {
                // Disable readback when it is not required so staging buffers are not touched.
                m_FrameReadbackPending[imageIndex] = false;
            }

            // Multi-panel path: copy the rendered viewport into the swapchain image so every editor panel sees a synchronized back buffer.
            VkImageBlit l_BlitRegion{};
            l_BlitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            l_BlitRegion.srcSubresource.mipLevel = 0;
            l_BlitRegion.srcSubresource.baseArrayLayer = 0;
            l_BlitRegion.srcSubresource.layerCount = 1;
            l_BlitRegion.srcOffsets[0] = { 0, 0, 0 };
            l_BlitRegion.srcOffsets[1] = { static_cast<int32_t>(l_PrimaryTarget->m_Extent.width), static_cast<int32_t>(l_PrimaryTarget->m_Extent.height), 1 };
            l_BlitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            l_BlitRegion.dstSubresource.mipLevel = 0;
            l_BlitRegion.dstSubresource.baseArrayLayer = 0;
            l_BlitRegion.dstSubresource.layerCount = 1;
            l_BlitRegion.dstOffsets[0] = { 0, 0, 0 };
            l_BlitRegion.dstOffsets[1] = { static_cast<int32_t>(m_Swapchain.GetExtent().width), static_cast<int32_t>(m_Swapchain.GetExtent().height), 1 };

            vkCmdBlitImage(l_CommandBuffer, l_PrimaryTarget->m_Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, l_SwapchainImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1, &l_BlitRegion, VK_FILTER_LINEAR);

            // After the blit the ImGui descriptor still expects shader read, so return the offscreen image to that layout.
            VkImageMemoryBarrier l_ToSample{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
            l_ToSample.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            l_ToSample.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            l_ToSample.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            l_ToSample.subresourceRange.baseMipLevel = 0;
            l_ToSample.subresourceRange.levelCount = 1;
            l_ToSample.subresourceRange.baseArrayLayer = 0;
            l_ToSample.subresourceRange.layerCount = 1;
            l_ToSample.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            l_ToSample.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            l_ToSample.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            l_ToSample.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            l_ToSample.image = l_PrimaryTarget->m_Image;

            vkCmdPipelineBarrier(l_CommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &l_ToSample);
            l_PrimaryTarget->m_CurrentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            // Future improvement: evaluate layered compositing so multiple render targets can blend before hitting the back buffer.
        }
        else
        {
            if (imageIndex < m_FrameReadbackPending.size())
            {
                m_FrameReadbackPending[imageIndex] = false;
            }

            // Legacy path clear performed via transfer op now that the render pass load operation no longer performs it implicitly.
            VkClearColorValue l_ClearValue{};
            l_ClearValue.float32[0] = m_ClearColor.r;
            l_ClearValue.float32[1] = m_ClearColor.g;
            l_ClearValue.float32[2] = m_ClearColor.b;
            l_ClearValue.float32[3] = m_ClearColor.a;

            VkImageSubresourceRange l_ClearRange{};
            l_ClearRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            l_ClearRange.baseMipLevel = 0;
            l_ClearRange.levelCount = 1;
            l_ClearRange.baseArrayLayer = 0;
            l_ClearRange.layerCount = 1;

            vkCmdClearColorImage(l_CommandBuffer, l_SwapchainImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &l_ClearValue, 1, &l_ClearRange);
        }

        // Transition the swapchain back to COLOR_ATTACHMENT_OPTIMAL so the render pass can output ImGui and any additional overlays.
        VkImageMemoryBarrier l_ToColorAttachment{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        l_ToColorAttachment.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        l_ToColorAttachment.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        l_ToColorAttachment.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        l_ToColorAttachment.subresourceRange.baseMipLevel = 0;
        l_ToColorAttachment.subresourceRange.levelCount = 1;
        l_ToColorAttachment.subresourceRange.baseArrayLayer = 0;
        l_ToColorAttachment.subresourceRange.layerCount = 1;
        l_ToColorAttachment.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        l_ToColorAttachment.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        l_ToColorAttachment.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        l_ToColorAttachment.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        l_ToColorAttachment.image = l_SwapchainImage;

        vkCmdPipelineBarrier(l_CommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            0, 0, nullptr, 0, nullptr, 1, &l_ToColorAttachment);
        if (imageIndex < m_SwapchainImageLayouts.size())
        {
            m_SwapchainImageLayouts[imageIndex] = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }

        if (l_SwapchainDepthImage != VK_NULL_HANDLE)
        {
            VkPipelineStageFlags l_DepthSrcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            VkAccessFlags l_DepthSrcAccess = 0;
            VkImageLayout l_PreviousDepthLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            if (imageIndex < m_SwapchainDepthLayouts.size())
            {
                l_PreviousDepthLayout = m_SwapchainDepthLayouts[imageIndex];
            }

            if (l_PreviousDepthLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
            {
                l_DepthSrcStage = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
                l_DepthSrcAccess = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            }

            VkImageMemoryBarrier l_PrepareDepthAttachment{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
            l_PrepareDepthAttachment.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            l_PrepareDepthAttachment.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            l_PrepareDepthAttachment.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            l_PrepareDepthAttachment.subresourceRange.baseMipLevel = 0;
            l_PrepareDepthAttachment.subresourceRange.levelCount = 1;
            l_PrepareDepthAttachment.subresourceRange.baseArrayLayer = 0;
            l_PrepareDepthAttachment.subresourceRange.layerCount = 1;
            l_PrepareDepthAttachment.oldLayout = l_PreviousDepthLayout;
            l_PrepareDepthAttachment.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            l_PrepareDepthAttachment.srcAccessMask = l_DepthSrcAccess;
            l_PrepareDepthAttachment.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            l_PrepareDepthAttachment.image = l_SwapchainDepthImage;

            vkCmdPipelineBarrier(l_CommandBuffer, l_DepthSrcStage, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0, 0, nullptr, 0, nullptr, 1, &l_PrepareDepthAttachment);

            if (imageIndex < m_SwapchainDepthLayouts.size())
            {
                m_SwapchainDepthLayouts[imageIndex] = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            }
        }

        // Second pass: draw the main swapchain image. The attachment now preserves the blit results for multi-panel compositing.
        VkRenderPassBeginInfo l_SwapchainPass{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
        l_SwapchainPass.renderPass = m_Pipeline.GetRenderPass();
        l_SwapchainPass.framebuffer = m_Pipeline.GetFramebuffers()[imageIndex];
        l_SwapchainPass.renderArea.offset = { 0, 0 };
        l_SwapchainPass.renderArea.extent = m_Swapchain.GetExtent();
        // Provide both colour and depth clear values; the colour entry is ignored because the attachment loads, but depth needs a fresh 1.0f each frame.
        std::array<VkClearValue, 2> l_SwapchainClearValues{};
        l_SwapchainClearValues[0] = a_BuildClearValue();
        l_SwapchainClearValues[1].depthStencil.depth = 1.0f;
        l_SwapchainClearValues[1].depthStencil.stencil = 0;
        l_SwapchainPass.clearValueCount = static_cast<uint32_t>(l_SwapchainClearValues.size());
        l_SwapchainPass.pClearValues = l_SwapchainClearValues.data();

        vkCmdBeginRenderPass(l_CommandBuffer, &l_SwapchainPass, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport l_SwapchainViewport{};
        l_SwapchainViewport.x = 0.0f;
        l_SwapchainViewport.y = 0.0f;
        l_SwapchainViewport.width = static_cast<float>(m_Swapchain.GetExtent().width);
        l_SwapchainViewport.height = static_cast<float>(m_Swapchain.GetExtent().height);
        l_SwapchainViewport.minDepth = 0.0f;
        l_SwapchainViewport.maxDepth = 1.0f;
        vkCmdSetViewport(l_CommandBuffer, 0, 1, &l_SwapchainViewport);

        VkRect2D l_SwapchainScissor{};
        l_SwapchainScissor.offset = { 0, 0 };
        l_SwapchainScissor.extent = m_Swapchain.GetExtent();
        vkCmdSetScissor(l_CommandBuffer, 0, 1, &l_SwapchainScissor);

        if (!l_PrimaryViewportActive)
        {
            // Legacy rendering path: draw directly to the back buffer when the editor viewport is hidden.
            const bool l_HasSkyboxDescriptors = imageIndex < m_SkyboxDescriptorSets.size() && m_SkyboxDescriptorSets[imageIndex] != VK_NULL_HANDLE;
            const VkPipeline l_SkyboxPipeline = m_Pipeline.GetSkyboxPipeline();
            if (l_HasSkyboxDescriptors && l_SkyboxPipeline != VK_NULL_HANDLE)
            {
                // Skip skybox recording when the pipeline is unavailable to keep the command buffer consistent during rebuilds.
                vkCmdBindPipeline(l_CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, l_SkyboxPipeline);
                m_Skybox.Record(l_CommandBuffer, m_Pipeline.GetSkyboxPipelineLayout(), m_SkyboxDescriptorSets.data(), imageIndex);
            }
            else if (l_HasSkyboxDescriptors && l_SkyboxPipeline == VK_NULL_HANDLE)
            {
                TR_CORE_WARN("Skybox pipeline missing; skipping swapchain skybox draw until the pipeline is rebuilt.");
            }

            const VkPipeline l_RenderPipeline = m_Pipeline.GetPipeline();
            const bool l_CanRender = l_RenderPipeline != VK_NULL_HANDLE;
            if (l_CanRender)
            {
                vkCmdBindPipeline(l_CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, l_RenderPipeline);

                const bool l_HasDescriptorSet = imageIndex < m_DescriptorSets.size();
                if (l_HasDescriptorSet)
                {
                    vkCmdBindDescriptorSets(l_CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline.GetPipelineLayout(), 0, 1, &m_DescriptorSets[imageIndex], 0, nullptr);
                }

                GatherMeshDraws();

                if (m_VertexBuffer != VK_NULL_HANDLE && m_IndexBuffer != VK_NULL_HANDLE && !m_MeshDrawInfo.empty() && !m_MeshDrawCommands.empty() && l_HasDescriptorSet)
                {
                    VkBuffer l_VertexBuffers[] = { m_VertexBuffer };
                    VkDeviceSize l_Offsets[] = { 0 };
                    vkCmdBindVertexBuffers(l_CommandBuffer, 0, 1, l_VertexBuffers, l_Offsets);
                    vkCmdBindIndexBuffer(l_CommandBuffer, m_IndexBuffer, 0, VK_INDEX_TYPE_UINT32);

                    for (const MeshDrawCommand& l_Command : m_MeshDrawCommands)
                    {
                        if (!l_Command.m_Component)
                        {
                            continue;
                        }

                        const MeshComponent& l_Component = *l_Command.m_Component;
                        if (l_Component.m_MeshIndex >= m_MeshDrawInfo.size())
                        {
                            continue;
                        }

                        const MeshDrawInfo& l_DrawInfo = m_MeshDrawInfo[l_Component.m_MeshIndex];
                        if (l_DrawInfo.m_IndexCount == 0)
                        {
                            continue;
                        }

                        RenderablePushConstant l_PushConstant{};
                        l_PushConstant.m_ModelMatrix = l_Command.m_ModelMatrix;
                        int32_t l_MaterialIndex = l_DrawInfo.m_MaterialIndex;
                        int32_t l_TextureSlot = 0;
                        if (l_Command.m_TextureComponent != nullptr && l_Command.m_TextureComponent->m_TextureSlot >= 0)
                        {
                            // Prefer the entity supplied texture slot so material overrides remain reactive in-editor.
                            l_TextureSlot = l_Command.m_TextureComponent->m_TextureSlot;
                        }
                        else if (l_MaterialIndex >= 0 && static_cast<size_t>(l_MaterialIndex) < m_Materials.size())
                        {
                            l_TextureSlot = m_Materials[l_MaterialIndex].BaseColorTextureSlot;
                        }

                        l_PushConstant.m_TextureSlot = l_TextureSlot;
                        l_PushConstant.m_MaterialIndex = l_MaterialIndex;
                        l_PushConstant.m_BoneOffset = static_cast<int32_t>(l_Command.m_BoneOffset);
                        l_PushConstant.m_BoneCount = static_cast<int32_t>(l_Command.m_BoneCount);
                        vkCmdPushConstants(l_CommandBuffer, m_Pipeline.GetPipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                            sizeof(RenderablePushConstant), &l_PushConstant);

                        vkCmdDrawIndexed(l_CommandBuffer, l_DrawInfo.m_IndexCount, 1, l_DrawInfo.m_FirstIndex, l_DrawInfo.m_BaseVertex, 0);
                    }
                }

                if (l_HasDescriptorSet)
                {
                    DrawSprites(l_CommandBuffer, imageIndex);
                }
            }
            else
            {
                // Keep the render pass balanced while avoiding invalid binds when the pipeline is unavailable.
                TR_CORE_WARN("Primary render pipeline missing; skipping swapchain draw until pipelines are rebuilt.");
            }
        }
        else
        {
            // The multi-panel image was already blitted; leave the pass empty so UI overlays stack on top cleanly.
            // Future improvement: extend this path to support post-processing effects before UI submission.
        }

        if (m_ImGuiLayer)
        {
            m_ImGuiLayer->Render(l_CommandBuffer);
        }

        vkCmdEndRenderPass(l_CommandBuffer);

        VkImageMemoryBarrier l_PresentBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        l_PresentBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        l_PresentBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        l_PresentBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        l_PresentBarrier.subresourceRange.baseMipLevel = 0;
        l_PresentBarrier.subresourceRange.levelCount = 1;
        l_PresentBarrier.subresourceRange.baseArrayLayer = 0;
        l_PresentBarrier.subresourceRange.layerCount = 1;
        l_PresentBarrier.oldLayout = (imageIndex < m_SwapchainImageLayouts.size()) ? m_SwapchainImageLayouts[imageIndex] : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        l_PresentBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        l_PresentBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        l_PresentBarrier.dstAccessMask = 0;
        l_PresentBarrier.image = m_Swapchain.GetImages()[imageIndex];

        vkCmdPipelineBarrier(l_CommandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            0, 0, nullptr, 0, nullptr, 1, &l_PresentBarrier);
        
        if (imageIndex < m_SwapchainImageLayouts.size())
        {
            // Keep the cached state aligned with the presentation transition so validation remains silent in future frames.
            m_SwapchainImageLayouts[imageIndex] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        }

        if (vkEndCommandBuffer(l_CommandBuffer) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to record command buffer!");

            // Abort the frame so the caller can handle the failure gracefully instead of terminating the process.

            return false;
        }

        return true;
    }

    bool Renderer::SubmitFrame(uint32_t imageIndex, VkFence inFlightFence)
    {
        VkCommandBuffer l_CommandBuffer = m_Commands.GetCommandBuffer(imageIndex);
        const size_t l_CurrentFrame = m_Commands.CurrentFrame();

        // Synchronization chain:
        // 1. Wait for the swapchain image acquired semaphore tied to the frame slot (keeps acquire/submit pacing aligned).
        // 2. Submit work that renders into the image for this frame-in-flight.
        // 3. Signal the image-scoped render-finished semaphore so presentation waits on the exact same handle when that image is presented.
        VkSemaphore l_WaitSemaphores[] = { m_Commands.GetImageAvailableSemaphorePerImage(l_CurrentFrame) };
        VkPipelineStageFlags l_WaitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        VkSemaphore l_SignalSemaphores[] = { m_Commands.GetRenderFinishedSemaphoreForImage(imageIndex), m_Commands.GetFrameTimelineSemaphore() };
        uint64_t l_WaitValues[] = { 0 };
        uint64_t l_SignalValues[] = { 0, 0 };

        VkTimelineSemaphoreSubmitInfo l_TimelineSubmitInfo{ VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO };
        if (m_Commands.SupportsTimelineSemaphores())
        {
            // Increment the shared timeline so each queue submission signals a unique, increasing value across all frames.
            const uint64_t l_NextTimelineValue = m_Commands.IncrementTimelineValue();

            l_WaitValues[0] = 0;
            l_SignalValues[0] = 0; // Binary semaphore still signals render completion for presentation.
            l_SignalValues[1] = l_NextTimelineValue;

            l_TimelineSubmitInfo.waitSemaphoreValueCount = 1;
            l_TimelineSubmitInfo.pWaitSemaphoreValues = l_WaitValues;
            l_TimelineSubmitInfo.signalSemaphoreValueCount = 2;
            l_TimelineSubmitInfo.pSignalSemaphoreValues = l_SignalValues;
        }

        VkSubmitInfo l_SubmitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
        l_SubmitInfo.pNext = m_Commands.SupportsTimelineSemaphores() ? &l_TimelineSubmitInfo : nullptr;
        l_SubmitInfo.waitSemaphoreCount = 1;
        l_SubmitInfo.pWaitSemaphores = l_WaitSemaphores;
        l_SubmitInfo.pWaitDstStageMask = l_WaitStages;
        l_SubmitInfo.commandBufferCount = 1;
        l_SubmitInfo.pCommandBuffers = &l_CommandBuffer;
        l_SubmitInfo.signalSemaphoreCount = m_Commands.SupportsTimelineSemaphores() ? 2 : 1;
        l_SubmitInfo.pSignalSemaphores = l_SignalSemaphores;

        if (vkQueueSubmit(Startup::GetGraphicsQueue(), 1, &l_SubmitInfo, inFlightFence) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to submit draw command buffer!");

            // Notify the caller that the queue submission failed so the frame can be skipped cleanly.

            return false;
        }

        if (vkGetFenceStatus(Startup::GetDevice(), m_ResourceFence) == VK_NOT_READY)
        {
            vkWaitForFences(Startup::GetDevice(), 1, &m_ResourceFence, VK_TRUE, UINT64_MAX);
        }

        vkResetFences(Startup::GetDevice(), 1, &m_ResourceFence);
        VkSubmitInfo l_FenceSubmit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
        l_FenceSubmit.commandBufferCount = 0;
        if (vkQueueSubmit(Startup::GetGraphicsQueue(), 1, &l_FenceSubmit, m_ResourceFence) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to submit resource fence");
            // Allow the caller to decide how to recover from a failed fence submission.

            return false;
        }

        return true;
    }

    void Renderer::PresentFrame(uint32_t imageIndex)
    {
        const size_t l_CurrentFrame = m_Commands.CurrentFrame();

        // Presentation waits on the per-image semaphore that SubmitFrame signaled. This keeps validation happy by ensuring
        // the handle is only recycled after vkQueuePresentKHR consumes it and the swapchain re-issues the image.
        VkSemaphore l_WaitSemaphores[] = { m_Commands.GetRenderFinishedSemaphoreForImage(imageIndex) };

        VkPresentInfoKHR l_PresentInfo{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
        l_PresentInfo.waitSemaphoreCount = 1;
        l_PresentInfo.pWaitSemaphores = l_WaitSemaphores;

        VkSwapchainKHR l_Swapchains[] = { m_Swapchain.GetSwapchain() };
        l_PresentInfo.swapchainCount = 1;
        l_PresentInfo.pSwapchains = l_Swapchains;
        l_PresentInfo.pImageIndices = &imageIndex;

        VkResult l_PresentResult = vkQueuePresentKHR(Startup::GetPresentQueue(), &l_PresentInfo);

        // Future improvement: leverage VK_EXT_swapchain_maintenance1 to release images earlier if presentation gets backlogged.

        if (l_PresentResult == VK_ERROR_OUT_OF_DATE_KHR || l_PresentResult == VK_SUBOPTIMAL_KHR)
        {
            RecreateSwapchain();
        }

        else if (l_PresentResult != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to present swap chain image!");
        }
    }

    void Renderer::ProcessReloadEvents()
    {
        auto& a_Watcher = Utilities::FileWatcher::Get();
        bool l_DeviceIdle = false;

        while (auto a_Event = a_Watcher.PopPendingEvent())
        {
            if (!l_DeviceIdle)
            {
                // Block the graphics queue once before processing the first reload to ensure resources are idle.
                vkDeviceWaitIdle(Startup::GetDevice());
                l_DeviceIdle = true;
            }

            bool l_Success = false;
            std::string l_Message{};

            switch (a_Event->Type)
            {
            case Utilities::FileWatcher::WatchType::Shader:
            {
                // Shader reload leverages the existing hot-reload path but skips the internal wait because we already idled above.
                bool l_Reloaded = m_Pipeline.ReloadIfNeeded(m_Swapchain, false);
                if (l_Reloaded && m_Pipeline.GetPipeline() != VK_NULL_HANDLE)
                {
                    l_Success = true;
                    l_Message = "Graphics pipeline rebuilt";
                }
                else
                {
                    l_Message = "Shader reload failed - check compiler output";
                }
                break;
            }
            case Utilities::FileWatcher::WatchType::Model:
            {
                auto a_ModelData = Loader::ModelLoader::Load(a_Event->Path);
                if (!a_ModelData.m_Meshes.empty())
                {
                    UploadMesh(a_ModelData.m_Meshes, a_ModelData.m_Materials, a_ModelData.m_Textures);
                    l_Success = true;
                    l_Message = "Model assets reuploaded";
                }
                else
                {
                    l_Message = "Model loader returned no meshes";
                }
                break;
            }
            case Utilities::FileWatcher::WatchType::Texture:
            {
                auto texture = Loader::TextureLoader::Load(a_Event->Path);
                if (!texture.Pixels.empty())
                {
                    // TODO: Investigate streaming or impostor generation so large material libraries can refresh incrementally.
                    UploadTexture(a_Event->Path, texture);
                    l_Success = true;
                    l_Message = "Texture refreshed";
                }
                else
                {
                    l_Message = "Texture loader returned empty pixel data";
                }
                break;
            }
            default:
                l_Message = "Unhandled reload type";
                break;
            }

            if (l_Success)
            {
                a_Watcher.MarkEventSuccess(a_Event->Id, l_Message);
                TR_CORE_INFO("Hot reload succeeded for {}", a_Event->Path.c_str());
            }
            else
            {
                a_Watcher.MarkEventFailure(a_Event->Id, l_Message);
                TR_CORE_ERROR("Hot reload failed for {}: {}", a_Event->Path.c_str(), l_Message.c_str());
            }
        }
    }

    void Renderer::UpdateUniformBuffer(uint32_t currentImage, const Camera* cameraOverride, VkCommandBuffer commandBuffer)
    {
        if (currentImage >= m_GlobalUniformBuffersMemory.size() || currentImage >= m_MaterialBuffersMemory.size())
        {
            return;
        }

        GlobalUniformBuffer l_Global{};
        const Camera* l_ActiveCamera = cameraOverride ? cameraOverride : GetActiveCamera();
        if (l_ActiveCamera)
        {
            l_Global.View = l_ActiveCamera->GetViewMatrix();
            l_Global.Projection = l_ActiveCamera->GetProjectionMatrix();
            l_Global.CameraPosition = glm::vec4(l_ActiveCamera->GetPosition(), 1.0f);
        }
        else
        {
            l_Global.View = glm::mat4{ 1.0f };
            l_Global.Projection = glm::mat4{ 1.0f };
            l_Global.CameraPosition = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
            // TODO: Provide a jittered fallback matrix when temporal AA is introduced.
        }

        l_Global.AmbientColorIntensity = glm::vec4(m_AmbientColor, m_AmbientIntensity);

        glm::vec3 l_DirectionalDirection = glm::normalize(s_DefaultDirectionalDirection);
        glm::vec3 l_DirectionalColor = s_DefaultDirectionalColor;
        float l_DirectionalIntensity = s_DefaultDirectionalIntensity;
        uint32_t l_DirectionalCount = 0;
        uint32_t l_PointLightWriteCount = 0;

        if (m_Registry)
        {
            const std::vector<ECS::Entity>& l_Entities = m_Registry->GetEntities();
            for (ECS::Entity it_Entity : l_Entities)
            {
                if (!m_Registry->HasComponent<LightComponent>(it_Entity))
                {
                    continue;
                }

                const LightComponent& l_LightComponent = m_Registry->GetComponent<LightComponent>(it_Entity);
                if (!l_LightComponent.m_Enabled)
                {
                    continue;
                }

                if (l_LightComponent.m_Type == LightComponent::Type::Directional)
                {
                    if (l_DirectionalCount == 0)
                    {
                        const float l_LengthSquared = glm::dot(l_LightComponent.m_Direction, l_LightComponent.m_Direction);
                        if (l_LengthSquared > 0.0001f)
                        {
                            l_DirectionalDirection = glm::normalize(l_LightComponent.m_Direction);
                        }
                        l_DirectionalColor = l_LightComponent.m_Color;
                        l_DirectionalIntensity = std::max(l_LightComponent.m_Intensity, 0.0f);
                    }
                    ++l_DirectionalCount;
                    continue;
                }

                if (l_LightComponent.m_Type == LightComponent::Type::Point)
                {
                    if (l_PointLightWriteCount >= s_MaxPointLights)
                    {
                        continue;
                    }

                    glm::vec3 l_Position{ 0.0f };
                    if (m_Registry->HasComponent<Transform>(it_Entity))
                    {
                        l_Position = m_Registry->GetComponent<Transform>(it_Entity).Position;
                    }

                    const float l_Range = std::max(l_LightComponent.m_Range, 0.0f);
                    const float l_Intensity = std::max(l_LightComponent.m_Intensity, 0.0f);

                    l_Global.PointLights[l_PointLightWriteCount].PositionRange = glm::vec4(l_Position, l_Range);
                    l_Global.PointLights[l_PointLightWriteCount].ColorIntensity = glm::vec4(l_LightComponent.m_Color, l_Intensity);
                    ++l_PointLightWriteCount;
                    continue;
                }
            }
        }

        const bool l_ShouldUseFallbackDirectional = (l_DirectionalCount == 0 && l_PointLightWriteCount == 0);
        const uint32_t l_DirectionalUsed = (l_DirectionalCount > 0 || l_ShouldUseFallbackDirectional) ? 1u : 0u;

        l_Global.DirectionalLightDirection = glm::vec4(l_DirectionalDirection, 0.0f);
        l_Global.DirectionalLightColor = glm::vec4(l_DirectionalColor, l_DirectionalIntensity);
        l_Global.LightCounts = glm::uvec4(l_DirectionalUsed, l_PointLightWriteCount, 0u, 0u);

        if (m_AiTextureReady && m_AiTextureExtent.width > 0 && m_AiTextureExtent.height > 0)
        {
            const float l_InvWidth = 1.0f / static_cast<float>(std::max<uint32_t>(m_AiTextureExtent.width, 1));
            const float l_InvHeight = 1.0f / static_cast<float>(std::max<uint32_t>(m_AiTextureExtent.height, 1));
            l_Global.AiBlendConfig = glm::vec4(m_AiBlendStrength, l_InvWidth, l_InvHeight, 1.0f);
        }
        else
        {
            l_Global.AiBlendConfig = glm::vec4(0.0f);
        }

        auto BuildMaterialPayload = [this]()
            {
                std::vector<MaterialUniformBuffer> l_Payload{};
                const size_t l_TargetCount = std::max(m_MaterialBufferElementCount, static_cast<size_t>(1));
                l_Payload.reserve(l_TargetCount);

                for (const Geometry::Material& it_Material : m_Materials)
                {
                    MaterialUniformBuffer l_Record{};
                    l_Record.BaseColorFactor = it_Material.BaseColorFactor;
                    l_Record.MaterialFactors = glm::vec4(it_Material.MetallicFactor, it_Material.RoughnessFactor, 1.0f, 0.0f);
                    l_Payload.push_back(l_Record);
                }

                MaterialUniformBuffer l_Default{};
                l_Default.BaseColorFactor = glm::vec4(1.0f);
                l_Default.MaterialFactors = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);

                while (l_Payload.size() < l_TargetCount)
                {
                    l_Payload.push_back(l_Default);
                }

                return l_Payload;
            };

        const bool l_HasMaterialBuffers = currentImage < m_MaterialBuffers.size() && currentImage < m_MaterialBuffersMemory.size();

        if (commandBuffer != VK_NULL_HANDLE)
        {
            // Record GPU-side buffer updates so each viewport captures the correct camera state before its render pass begins.
            vkCmdUpdateBuffer(commandBuffer, m_GlobalUniformBuffers[currentImage], 0, sizeof(l_Global), &l_Global);
            std::vector<VkBufferMemoryBarrier> l_Barriers{};
            l_Barriers.reserve(2);

            VkBufferMemoryBarrier l_GlobalBarrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
            l_GlobalBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            l_GlobalBarrier.dstAccessMask = VK_ACCESS_UNIFORM_READ_BIT;
            l_GlobalBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            l_GlobalBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            l_GlobalBarrier.buffer = m_GlobalUniformBuffers[currentImage];
            l_GlobalBarrier.offset = 0;
            l_GlobalBarrier.size = sizeof(l_Global);
            l_Barriers.push_back(l_GlobalBarrier);

            if (l_HasMaterialBuffers && currentImage < m_MaterialBufferDirty.size() && m_MaterialBufferDirty[currentImage])
            {
                std::vector<MaterialUniformBuffer> l_MaterialPayload = BuildMaterialPayload();
                const VkDeviceSize l_CopySize = static_cast<VkDeviceSize>(l_MaterialPayload.size() * sizeof(MaterialUniformBuffer));
                if (l_CopySize > 0)
                {
                    const uint8_t* l_RawData = reinterpret_cast<const uint8_t*>(l_MaterialPayload.data());
                    VkDeviceSize l_Remaining = l_CopySize;
                    VkDeviceSize l_Offset = 0;
                    const VkDeviceSize l_MaxChunk = 65536;

                    while (l_Remaining > 0)
                    {
                        const VkDeviceSize l_ChunkSize = std::min(l_Remaining, l_MaxChunk);
                        vkCmdUpdateBuffer(commandBuffer, m_MaterialBuffers[currentImage], l_Offset, l_ChunkSize, l_RawData + l_Offset);
                        l_Remaining -= l_ChunkSize;
                        l_Offset += l_ChunkSize;
                    }

                    VkBufferMemoryBarrier l_MaterialBarrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
                    l_MaterialBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                    // Ensure uniform buffer reads see the freshly uploaded material parameters.
                    l_MaterialBarrier.dstAccessMask = VK_ACCESS_UNIFORM_READ_BIT;
                    l_MaterialBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    l_MaterialBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    l_MaterialBarrier.buffer = m_MaterialBuffers[currentImage];
                    l_MaterialBarrier.offset = 0;
                    l_MaterialBarrier.size = l_CopySize;
                    l_Barriers.push_back(l_MaterialBarrier);

                    m_MaterialBufferDirty[currentImage] = false;
                }
            }

            // Guarantee the transfer writes are visible before the shader stages fetch the uniform data.
            if (!l_Barriers.empty())
            {
                vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    0, 0, nullptr, static_cast<uint32_t>(l_Barriers.size()), l_Barriers.data(), 0, nullptr);
            }
        }
        else
        {
            // Fall back to direct CPU writes when no command buffer is available (e.g., pre-frame initialisation).
            void* l_Data = nullptr;
            vkMapMemory(Startup::GetDevice(), m_GlobalUniformBuffersMemory[currentImage], 0, sizeof(l_Global), 0, &l_Data);
            std::memcpy(l_Data, &l_Global, sizeof(l_Global));
            vkUnmapMemory(Startup::GetDevice(), m_GlobalUniformBuffersMemory[currentImage]);

            if (l_HasMaterialBuffers)
            {
                const bool l_ShouldUploadAll = std::any_of(m_MaterialBufferDirty.begin(), m_MaterialBufferDirty.end(), [](bool it_Dirty) { return it_Dirty; });
                if (l_ShouldUploadAll)
                {
                    std::vector<MaterialUniformBuffer> l_MaterialPayload = BuildMaterialPayload();
                    const VkDeviceSize l_CopySize = static_cast<VkDeviceSize>(l_MaterialPayload.size() * sizeof(MaterialUniformBuffer));
                    for (size_t it_Image = 0; it_Image < m_MaterialBuffersMemory.size() && it_Image < m_MaterialBufferDirty.size(); ++it_Image)
                    {
                        if (!m_MaterialBufferDirty[it_Image])
                        {
                            continue;
                        }

                        if (m_MaterialBuffersMemory[it_Image] == VK_NULL_HANDLE)
                        {
                            continue;
                        }

                        void* l_MaterialData = nullptr;
                        vkMapMemory(Startup::GetDevice(), m_MaterialBuffersMemory[it_Image], 0, l_CopySize, 0, &l_MaterialData);
                        std::memcpy(l_MaterialData, l_MaterialPayload.data(), static_cast<size_t>(l_CopySize));
                        vkUnmapMemory(Startup::GetDevice(), m_MaterialBuffersMemory[it_Image]);
                        m_MaterialBufferDirty[it_Image] = false;
                    }
                }
            }
        }

        // TODO: Expand the uniform population to handle per-camera post-processing once those systems exist.
    }

    void Renderer::SetSelectedEntity(ECS::Entity entity)
    {
        // Store the inspector's active entity so gizmo updates reference the same transform as the UI selection.
        m_Entity = entity;
    }

    void Renderer::SetViewportCamera(ECS::Entity entity)
    {
        // Cache which ECS camera currently drives the viewport so overlays can highlight it.
        m_ViewportCamera = entity;
    }

    void Renderer::SubmitText(uint32_t viewportID, const glm::vec2& position, const glm::vec4& color, std::string_view text)
    {
        if (text.empty())
        {
            return;
        }

        TextSubmission l_Submission{};
        l_Submission.m_ViewportId = viewportID;
        l_Submission.m_Position = position;
        l_Submission.m_Color = color;
        l_Submission.m_Text.assign(text.begin(), text.end());
        // TODO: Surface localization-aware font selection once the editor exposes language packs.
        m_TextSubmissionQueue[viewportID].emplace_back(std::move(l_Submission));
    }

    void Renderer::SetTransform(const Transform& transform)
    {
        if (m_Registry)
        {
            if (!m_Registry->HasComponent<Transform>(m_Entity))
            {
                m_Registry->AddComponent<Transform>(m_Entity, transform);
            }
            else
            {
                m_Registry->GetComponent<Transform>(m_Entity) = transform;
            }
        }
    }

    Transform Renderer::GetTransform() const
    {
        if (m_Registry && m_Registry->HasComponent<Transform>(m_Entity))
        {
            return m_Registry->GetComponent<Transform>(m_Entity);
        }

        return {};
    }

    glm::mat4 Renderer::GetWorldTransform(ECS::Entity entity) const
    {
        // Resolve the world matrix from the scene graph; fall back to identity when no transform exists.
        if (m_Registry && m_Registry->HasComponent<Transform>(entity))
        {
            return ComposeTransform(m_Registry->GetComponent<Transform>(entity));
        }

        return glm::mat4{ 1.0f };
    }

    void Renderer::SetWorldTransform(ECS::Entity entity, const glm::mat4& worldTransform)
    {
        if (!m_Registry)
        {
            return;
        }

        Transform l_Fallback{};
        if (m_Registry->HasComponent<Transform>(entity))
        {
            l_Fallback = m_Registry->GetComponent<Transform>(entity);
        }

        const Transform l_Decomposed = DecomposeWorldTransform(worldTransform, l_Fallback);

        if (!m_Registry->HasComponent<Transform>(entity))
        {
            m_Registry->AddComponent<Transform>(entity, l_Decomposed);
        }
        else
        {
            m_Registry->GetComponent<Transform>(entity) = l_Decomposed;
        }

        // Keep the renderer's entity selection aligned with the transform being edited by the gizmo.
        m_Entity = entity;
    }

    void Renderer::SetPerformanceCaptureEnabled(bool enabled)
    {
        if (enabled == m_PerformanceCaptureEnabled)
        {
            return;
        }

        m_PerformanceCaptureEnabled = enabled;
        if (m_PerformanceCaptureEnabled)
        {
            // Reset capture buffer so the exported data only contains the new capture session.
            m_PerformanceCaptureBuffer.clear();
            m_PerformanceCaptureBuffer.reserve(s_PerformanceHistorySize);
            m_PerformanceCaptureStartTime = std::chrono::system_clock::now();

            TR_CORE_INFO("Performance capture enabled");
        }
        else
        {
            ExportPerformanceCapture();
            m_PerformanceCaptureBuffer.clear();

            TR_CORE_INFO("Performance capture disabled");
        }
    }

    bool Renderer::SetViewportRecordingEnabled(bool enabled, uint32_t viewportID, VkExtent2D extent, const std::filesystem::path& outputPath)
    {
        if (enabled)
        {
            // YUV420P/H.264 encoders expect even-sized planes. Round any odd dimension up to the next even value so the
            // encoder receives a compatible resolution while keeping user intent as close as possible.
            VkExtent2D l_SanitizedExtent = extent;
            if ((l_SanitizedExtent.width % 2U) != 0U)
            {
                ++l_SanitizedExtent.width;
            }

            if ((l_SanitizedExtent.height % 2U) != 0U)
            {
                ++l_SanitizedExtent.height;
            }

            if (l_SanitizedExtent.width != extent.width || l_SanitizedExtent.height != extent.height)
            {
                TR_CORE_WARN("Viewport recording extent {}x{} adjusted to {}x{} to satisfy even YUV420P dimensions.",
                    extent.width, extent.height, l_SanitizedExtent.width, l_SanitizedExtent.height);
            }

            if (ViewportContext* l_Context = FindViewportContext(viewportID))
            {
                CreateOrResizeOffscreenResources(l_Context->m_Target, l_SanitizedExtent);
                // Update the cached info so the layout system doesn't immediately shrink it back.
                l_Context->m_CachedExtent = l_SanitizedExtent;
            }

            // Ensure readback buffers exist and match the sanitized resolution so staging, readback, and encoder dimensions stay aligned.
            SetReadbackEnabled(true, l_SanitizedExtent);
            RequestReadbackResize(l_SanitizedExtent, true);
            ApplyPendingReadbackResize();

            const bool l_HasValidReadbackExtent = (m_FrameReadbackExtent.width > 0) && (m_FrameReadbackExtent.height > 0);
            const bool l_HasValidRecordingExtent = (l_SanitizedExtent.width > 0) && (l_SanitizedExtent.height > 0);
            const bool l_HasValidChannelCount = (m_FrameReadbackChannelCount > 0);

            if (!l_HasValidReadbackExtent || !l_HasValidRecordingExtent || !l_HasValidChannelCount)
            {
                TR_CORE_WARN("Viewport recording unavailable. Extent {}x{} (readback {}x{}), channels {}.", l_SanitizedExtent.width, l_SanitizedExtent.height,
                    m_FrameReadbackExtent.width, m_FrameReadbackExtent.height, m_FrameReadbackChannelCount);

                const bool l_AiReadbackRequired = m_FrameGenerator.IsInitialised();
                SetReadbackEnabled(l_AiReadbackRequired, m_Swapchain.GetExtent());

                return false;
            }

            // Keep recording state consistent with the active readback buffers so encoded frames and staging share the same extent.
            l_SanitizedExtent = m_FrameReadbackExtent;

            m_ViewportFrameBuffer.clear();
            m_RecordingViewportId = viewportID;
            m_RecordingExtent = l_SanitizedExtent;
            m_RecordingOutputPath = outputPath;

            if (!m_VideoEncoder)
            {
                m_VideoEncoder = std::make_unique<VideoEncoder>();
            }

            if (!m_VideoEncoder)
            {
                TR_CORE_WARN("Viewport recording unavailable. Video encoder failed to initialise.");

                return false;
            }

            const bool l_SessionStarted = m_VideoEncoder->BeginSession(outputPath, l_SanitizedExtent, 1);
            const bool l_SessionActive = l_SessionStarted && m_VideoEncoder->IsSessionActive();

            // Reject the start request if the encoder did not accept the session so the caller can surface an error.
            if (!l_SessionActive)
            {
                TR_CORE_ERROR("Failed to start video encoding session for viewport {} at {}", viewportID, outputPath.string());

                m_ViewportRecordingEnabled = false;
                m_ViewportRecordingSessionActive = false;

                const bool l_AiReadbackRequired = m_FrameGenerator.IsInitialised();
                SetReadbackEnabled(l_AiReadbackRequired, m_Swapchain.GetExtent());

                return false;
            }

            m_ViewportRecordingEnabled = l_SessionActive;
            m_ViewportRecordingSessionActive = l_SessionActive;
            m_ReadbackConfigurationWarningIssued = false;

            return true;
        }
        else
        {
            m_ViewportRecordingEnabled = false;
            m_ViewportRecordingSessionActive = false;
            m_RecordingViewportId = s_InvalidViewportId;

            if (m_VideoEncoder && m_VideoEncoder->IsSessionActive())
            {
                if (!m_VideoEncoder->EndSession())
                {
                    TR_CORE_WARN("Video encoder failed to finalize output at {}", m_RecordingOutputPath.string());
                }
            }

            // Disable readback again unless AI processing still requires it.
            const bool l_AiReadbackRequired = m_FrameGenerator.IsInitialised();
            SetReadbackEnabled(l_AiReadbackRequired, m_Swapchain.GetExtent());

            return true;
        }
    }

    void Renderer::AccumulateFrameTiming(double frameMilliseconds, double framesPerSecond, VkExtent2D extent, std::chrono::system_clock::time_point captureTimestamp)
    {
        FrameTimingSample l_Sample{};
        l_Sample.FrameMilliseconds = frameMilliseconds;
        l_Sample.FramesPerSecond = framesPerSecond;
        l_Sample.Extent = extent;
        l_Sample.CaptureTime = captureTimestamp;

        // Store the latest sample inside the fixed-size ring buffer.
        m_PerformanceHistory[m_PerformanceHistoryNextIndex] = l_Sample;
        m_PerformanceHistoryNextIndex = (m_PerformanceHistoryNextIndex + 1) % m_PerformanceHistory.size();
        if (m_PerformanceSampleCount < m_PerformanceHistory.size())
        {
            ++m_PerformanceSampleCount;
        }

        UpdateFrameTimingStats();

        if (m_PerformanceCaptureEnabled)
        {
            m_PerformanceCaptureBuffer.push_back(l_Sample);
        }
    }

    void Renderer::UpdateFrameTimingStats()
    {
        if (m_PerformanceSampleCount == 0)
        {
            m_PerformanceStats = {};

            return;
        }

        double l_MinMilliseconds = std::numeric_limits<double>::max();
        double l_MaxMilliseconds = 0.0;
        double l_TotalMilliseconds = 0.0;
        double l_TotalFPS = 0.0;

        const size_t l_BufferSize = m_PerformanceHistory.size();
        const size_t l_ValidCount = m_PerformanceSampleCount;
        const size_t l_FirstIndex = (m_PerformanceHistoryNextIndex + l_BufferSize - l_ValidCount) % l_BufferSize;
        for (size_t l_Offset = 0; l_Offset < l_ValidCount; ++l_Offset)
        {
            const size_t l_Index = (l_FirstIndex + l_Offset) % l_BufferSize;
            const FrameTimingSample& it_Sample = m_PerformanceHistory[l_Index];

            l_MinMilliseconds = std::min(l_MinMilliseconds, it_Sample.FrameMilliseconds);
            l_MaxMilliseconds = std::max(l_MaxMilliseconds, it_Sample.FrameMilliseconds);
            l_TotalMilliseconds += it_Sample.FrameMilliseconds;
            l_TotalFPS += it_Sample.FramesPerSecond;
        }

        const double l_Count = static_cast<double>(l_ValidCount);
        m_PerformanceStats.MinimumMilliseconds = l_MinMilliseconds;
        m_PerformanceStats.MaximumMilliseconds = l_MaxMilliseconds;
        m_PerformanceStats.AverageMilliseconds = l_TotalMilliseconds / l_Count;
        m_PerformanceStats.AverageFPS = l_TotalFPS / l_Count;
    }

    void Renderer::ExportPerformanceCapture()
    {
        if (m_PerformanceCaptureBuffer.empty())
        {
            TR_CORE_WARN("Performance capture requested without any collected samples");

            return;
        }

        std::filesystem::path l_OutputDirectory{ "PerformanceCaptures" };
        std::error_code l_CreateError{};
        std::filesystem::create_directories(l_OutputDirectory, l_CreateError);
        if (l_CreateError)
        {
            TR_CORE_ERROR("Failed to create performance capture directory: {}", l_CreateError.message().c_str());

            return;
        }

        const std::time_t l_StartTime = std::chrono::system_clock::to_time_t(m_PerformanceCaptureStartTime);
        const std::tm l_StartLocal = ToLocalTime(l_StartTime);
        std::ostringstream l_FileNameStream;
        l_FileNameStream << "capture_" << std::put_time(&l_StartLocal, "%Y%m%d_%H%M%S") << ".csv";
        const std::filesystem::path l_FilePath = l_OutputDirectory / l_FileNameStream.str();

        std::ofstream l_File(l_FilePath, std::ios::trunc);
        if (!l_File.is_open())
        {
            TR_CORE_ERROR("Failed to open performance capture file: {}", l_FilePath.string().c_str());

            return;
        }

        // Write CSV header to simplify downstream analysis in spreadsheets.
        l_File << "Timestamp,Frame (ms),FPS,Extent Width,Extent Height\n";
        for (const FrameTimingSample& it_Sample : m_PerformanceCaptureBuffer)
        {
            const std::time_t l_SampleTime = std::chrono::system_clock::to_time_t(it_Sample.CaptureTime);
            const std::tm l_SampleLocal = ToLocalTime(l_SampleTime);
            l_File << std::put_time(&l_SampleLocal, "%Y-%m-%d %H:%M:%S") << ',' << it_Sample.FrameMilliseconds << ','
                << it_Sample.FramesPerSecond << ',' << it_Sample.Extent.width << ',' << it_Sample.Extent.height << '\n';
        }

        l_File.close();

        TR_CORE_INFO("Performance capture exported to {}", l_FilePath.string().c_str());
    }

    void Renderer::SyncFrameDatasetRecorder()
    {
        m_FrameDatasetRecorder.SetCaptureDirectory(m_FrameDatasetCaptureDirectory);
        m_FrameDatasetRecorder.SetSampleInterval(m_FrameDatasetCaptureInterval);
        m_FrameDatasetRecorder.EnableCapture(m_FrameDatasetCaptureEnabled);
    }
}