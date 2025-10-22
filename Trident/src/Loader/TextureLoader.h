#pragma once

#include <array>
#include <filesystem>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

namespace Trident
{
    namespace Loader
    {
        struct TextureData
        {
            int Width = 0;
            int Height = 0;
            int Channels = 0;
            std::vector<unsigned char> Pixels;
        };

        class TextureLoader
        {
        public:
            static TextureData Load(const std::string& filePath);
        };

        struct CubemapFaceRegion
        {
            size_t m_Offset = 0;
            size_t m_Size = 0;
        };

        struct CubemapTextureData
        {
            uint32_t m_Width = 0;
            uint32_t m_Height = 0;
            uint32_t m_MipCount = 1;
            uint32_t m_BytesPerPixel = 0;
            bool m_IsHdr = false;
            VkFormat m_Format = VK_FORMAT_UNDEFINED;
            std::vector<uint8_t> m_PixelData;
            // Subresources are stored in Vulkan's +X, -X, +Y, -Y, +Z, -Z order so the renderer can issue a direct copy.
            std::vector<std::array<CubemapFaceRegion, 6>> m_FaceRegions;

            bool IsValid() const { return m_Width > 0 && m_Height > 0 && !m_FaceRegions.empty(); }

            static CubemapTextureData CreateSolidColor(uint32_t rgba8888);
        };

        class SkyboxTextureLoader
        {
        public:
            static CubemapTextureData LoadFromFaces(const std::array<std::filesystem::path, 6>& facePaths);
            static CubemapTextureData LoadFromDirectory(const std::filesystem::path& directoryPath);
            static CubemapTextureData LoadFromKtx(const std::filesystem::path& filePath);

        private:
            static CubemapTextureData LoadFromFileList(const std::array<std::filesystem::path, 6>& normalizedFaces);
        };
    }
}