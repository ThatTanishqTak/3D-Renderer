#include "Loader/TextureLoader.h"

#include "Core/Utilities.h"

#include <stb_image.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>
#include <string_view>

namespace
{
    // Faces are listed in Vulkan's expected order so uploads can bind directly without swizzling.
    constexpr std::array<std::string_view, 6> s_FaceTokens{ "posx", "negx", "posy", "negy", "posz", "negz" };
    constexpr std::array<std::string_view, 6> s_FaceFriendlyNames{ "+X", "-X", "+Y", "-Y", "+Z", "-Z" };

    size_t AlignToDword(size_t a_Value)
    {
        return (a_Value + 3u) & ~static_cast<size_t>(3u);
    }

    std::string ToLowerCopy(std::string a_Text)
    {
        std::transform(a_Text.begin(), a_Text.end(), a_Text.begin(), [](unsigned char a_Char)
            {
                return static_cast<char>(std::tolower(a_Char));
            });
        return a_Text;
    }

    bool TryMatchFaceIndex(const std::filesystem::path& path, size_t& a_OutIndex)
    {
        std::string l_Stem = ToLowerCopy(path.stem().string());
        for (size_t it_Index = 0; it_Index < s_FaceTokens.size(); ++it_Index)
        {
            if (l_Stem.find(s_FaceTokens[it_Index]) != std::string::npos)
            {
                a_OutIndex = it_Index;
                return true;
            }
        }
        return false;
    }

    struct KtxHeader
    {
        uint8_t m_Identifier[12]{};
        uint32_t m_Endianness = 0;
        uint32_t m_GlType = 0;
        uint32_t m_GlTypeSize = 0;
        uint32_t m_GlFormat = 0;
        uint32_t m_GlInternalFormat = 0;
        uint32_t m_GlBaseInternalFormat = 0;
        uint32_t m_PixelWidth = 0;
        uint32_t m_PixelHeight = 0;
        uint32_t m_PixelDepth = 0;
        uint32_t m_NumberOfArrayElements = 0;
        uint32_t m_NumberOfFaces = 0;
        uint32_t m_NumberOfMipmapLevels = 0;
        uint32_t m_BytesOfKeyValueData = 0;
    };

    constexpr uint32_t KtxEndiannessLittle = 0x04030201u;
    constexpr uint32_t GlUnsignedByte = 0x1401u;
    constexpr uint32_t GlHalfFloat = 0x140Bu;
    constexpr uint32_t GlFloat = 0x1406u;
    constexpr uint32_t GlRgba = 0x1908u;
    constexpr uint32_t GlSrgbAlpha = 0x8C43u;
    constexpr uint32_t GlRgba8 = 0x8058u;
    constexpr uint32_t GlSrgb8Alpha8 = 0x8C43u;
    constexpr uint32_t GlRgba16f = 0x881Au;

    bool ReadFileBytes(const std::filesystem::path& path, std::vector<uint8_t>& a_Output)
    {
        std::ifstream l_File(path, std::ios::binary | std::ios::ate);
        if (!l_File.is_open())
        {
            TR_CORE_ERROR("Failed to open cubemap file: {}", path.string());
            return false;
        }

        std::streamsize l_Size = l_File.tellg();
        if (l_Size <= 0)
        {
            TR_CORE_ERROR("Cubemap file '{}' is empty", path.string());
            return false;
        }

        a_Output.resize(static_cast<size_t>(l_Size));
        l_File.seekg(0);
        l_File.read(reinterpret_cast<char*>(a_Output.data()), l_Size);
        return l_File.good();
    }

    std::filesystem::path NormalizeFsPath(const std::filesystem::path& path)
    {
        std::string l_Normalized = Trident::Utilities::FileManagement::NormalizePath(path.string());
        return std::filesystem::path(l_Normalized);
    }
}

namespace Trident
{
    namespace Loader
    {
        TextureData TextureLoader::Load(const std::string& filePath)
        {
            TextureData l_Texture{};
            std::string l_PathUtf8 = Utilities::FileManagement::NormalizePath(filePath);

            stbi_set_flip_vertically_on_load(true);
            // When compiling with MSVC in C++20 mode, stbi_load requires a const char* instead of const char8_t*,
            // so we keep the normalized UTF-8 path in a std::string to ensure compatibility.
            stbi_uc* l_Pixels = stbi_load(l_PathUtf8.c_str(), &l_Texture.Width, &l_Texture.Height, &l_Texture.Channels, STBI_rgb_alpha);
            if (!l_Pixels)
            {
                TR_CORE_CRITICAL("Failed to load texture: {} ({})", filePath, stbi_failure_reason());

                return l_Texture;
            }

            l_Texture.Channels = 4;
            size_t l_Size = static_cast<size_t>(l_Texture.Width) * static_cast<size_t>(l_Texture.Height) * static_cast<size_t>(l_Texture.Channels);
            l_Texture.Pixels.assign(l_Pixels, l_Pixels + l_Size);
            stbi_image_free(l_Pixels);

            return l_Texture;
        }

        CubemapTextureData CubemapTextureData::CreateSolidColor(uint32_t rgba8888)
        {
            CubemapTextureData l_Data{};
            l_Data.m_Width = 1;
            l_Data.m_Height = 1;
            l_Data.m_MipCount = 1;
            l_Data.m_BytesPerPixel = 4;
            l_Data.m_Format = VK_FORMAT_R8G8B8A8_SRGB;
            l_Data.m_IsHdr = false;
            l_Data.m_PixelData.resize(static_cast<size_t>(6u) * l_Data.m_BytesPerPixel);

            std::array<CubemapFaceRegion, 6> l_Faces{};
            for (size_t it_Face = 0; it_Face < 6; ++it_Face)
            {
                CubemapFaceRegion& l_Region = l_Faces[it_Face];
                l_Region.m_Offset = static_cast<size_t>(it_Face) * l_Data.m_BytesPerPixel;
                l_Region.m_Size = l_Data.m_BytesPerPixel;

                std::memcpy(l_Data.m_PixelData.data() + l_Region.m_Offset, &rgba8888, l_Data.m_BytesPerPixel);
            }

            l_Data.m_FaceRegions.push_back(l_Faces);
            return l_Data;
        }

        CubemapTextureData SkyboxTextureLoader::LoadFromFaces(const std::array<std::filesystem::path, 6>& facePaths)
        {
            std::array<std::filesystem::path, 6> l_Normalized{};
            for (size_t it_Index = 0; it_Index < facePaths.size(); ++it_Index)
            {
                l_Normalized[it_Index] = NormalizeFsPath(facePaths[it_Index]);
            }

            return LoadFromFileList(l_Normalized);
        }

        CubemapTextureData SkyboxTextureLoader::LoadFromDirectory(const std::filesystem::path& directoryPath)
        {
            std::array<std::filesystem::path, 6> l_Faces{};
            std::array<bool, 6> l_Assigned{};

            std::error_code l_Error{};
            if (!std::filesystem::is_directory(directoryPath, l_Error))
            {
                TR_CORE_ERROR("Cubemap directory '{}' is invalid", directoryPath.string());
                return {};
            }

            for (const auto& it_Entry : std::filesystem::directory_iterator(directoryPath, l_Error))
            {
                if (it_Entry.is_directory())
                {
                    continue;
                }

                size_t l_FaceIndex = 0;
                if (!TryMatchFaceIndex(it_Entry.path(), l_FaceIndex))
                {
                    continue;
                }

                if (l_Assigned[l_FaceIndex])
                {
                    TR_CORE_WARN("Multiple files mapped to cubemap face {} in '{}'; keeping the first match", s_FaceFriendlyNames[l_FaceIndex], directoryPath.string());
                    continue;
                }

                l_Assigned[l_FaceIndex] = true;
                l_Faces[l_FaceIndex] = NormalizeFsPath(it_Entry.path());
            }

            for (size_t it_Index = 0; it_Index < l_Assigned.size(); ++it_Index)
            {
                if (!l_Assigned[it_Index])
                {
                    TR_CORE_ERROR("Missing cubemap face {} in directory '{}'", s_FaceFriendlyNames[it_Index], directoryPath.string());
                    return {};
                }
            }

            return LoadFromFileList(l_Faces);
        }

        CubemapTextureData SkyboxTextureLoader::LoadFromKtx(const std::filesystem::path& filePath)
        {
            CubemapTextureData l_Result{};
            std::filesystem::path l_Normalized = NormalizeFsPath(filePath);

            std::vector<uint8_t> l_FileData{};
            if (!ReadFileBytes(l_Normalized, l_FileData))
            {
                return l_Result;
            }

            if (l_FileData.size() < sizeof(KtxHeader))
            {
                TR_CORE_ERROR("KTX file '{}' is too small to contain a header", l_Normalized.string());
                return l_Result;
            }

            const auto* l_Header = reinterpret_cast<const KtxHeader*>(l_FileData.data());
            if (l_Header->m_Endianness != KtxEndiannessLittle)
            {
                TR_CORE_ERROR("KTX file '{}' uses unsupported endianness", l_Normalized.string());
                return l_Result;
            }

            if (l_Header->m_NumberOfFaces != 6u)
            {
                TR_CORE_ERROR("KTX file '{}' does not contain 6 faces (found {})", l_Normalized.string(), l_Header->m_NumberOfFaces);
                return l_Result;
            }

            if (l_Header->m_PixelHeight == 0u || l_Header->m_PixelWidth == 0u)
            {
                TR_CORE_ERROR("KTX file '{}' has invalid dimensions", l_Normalized.string());
                return l_Result;
            }

            uint32_t l_PixelSize = 0u;
            VkFormat l_Format = VK_FORMAT_UNDEFINED;
            bool l_IsHdr = false;

            if (l_Header->m_GlType == GlUnsignedByte && l_Header->m_GlInternalFormat == GlSrgb8Alpha8)
            {
                l_PixelSize = 4u;
                l_Format = VK_FORMAT_R8G8B8A8_SRGB;
            }
            else if (l_Header->m_GlType == GlUnsignedByte && l_Header->m_GlFormat == GlRgba)
            {
                l_PixelSize = 4u;
                l_Format = VK_FORMAT_R8G8B8A8_UNORM;
            }
            else if ((l_Header->m_GlType == GlHalfFloat || l_Header->m_GlType == GlFloat) && l_Header->m_GlInternalFormat == GlRgba16f)
            {
                l_PixelSize = 8u;
                l_Format = VK_FORMAT_R16G16B16A16_SFLOAT;
                l_IsHdr = true;
            }

            if (l_Format == VK_FORMAT_UNDEFINED)
            {
                TR_CORE_ERROR("KTX file '{}' uses unsupported pixel format (glType={}, glInternalFormat={})", l_Normalized.string(), l_Header->m_GlType, l_Header->m_GlInternalFormat);
                return l_Result;
            }

            const uint8_t* l_DataPtr = l_FileData.data() + sizeof(KtxHeader) + l_Header->m_BytesOfKeyValueData;
            const uint8_t* l_EndPtr = l_FileData.data() + l_FileData.size();
            uint32_t l_MipCount = l_Header->m_NumberOfMipmapLevels == 0u ? 1u : l_Header->m_NumberOfMipmapLevels;

            l_Result.m_Width = l_Header->m_PixelWidth;
            l_Result.m_Height = l_Header->m_PixelHeight;
            l_Result.m_MipCount = l_MipCount;
            l_Result.m_BytesPerPixel = l_PixelSize;
            l_Result.m_Format = l_Format;
            l_Result.m_IsHdr = l_IsHdr;
            l_Result.m_FaceRegions.resize(l_MipCount);

            uint32_t l_CurrentWidth = l_Header->m_PixelWidth;
            uint32_t l_CurrentHeight = l_Header->m_PixelHeight;

            for (uint32_t it_Mip = 0; it_Mip < l_MipCount; ++it_Mip)
            {
                if (l_DataPtr + sizeof(uint32_t) > l_EndPtr)
                {
                    TR_CORE_ERROR("KTX file '{}' ended unexpectedly while reading mip level {}", l_Normalized.string(), it_Mip);
                    return {};
                }

                uint32_t l_ImageSize = *reinterpret_cast<const uint32_t*>(l_DataPtr);
                l_DataPtr += sizeof(uint32_t);

                if (l_ImageSize == 0u)
                {
                    TR_CORE_ERROR("KTX file '{}' reported zero-sized mip level {}", l_Normalized.string(), it_Mip);
                    return {};
                }

                const uint8_t* l_MipLevelStart = l_DataPtr;
                const size_t l_FaceSizeExpected = static_cast<size_t>(std::max(1u, l_CurrentWidth) * std::max(1u, l_CurrentHeight) * l_PixelSize);
                std::array<CubemapFaceRegion, 6> l_FaceRegions{};

                for (size_t it_Face = 0; it_Face < 6; ++it_Face)
                {
                    if (l_DataPtr + l_FaceSizeExpected > l_EndPtr)
                    {
                        TR_CORE_ERROR("KTX file '{}' ended unexpectedly while reading face {} mip {}", l_Normalized.string(), s_FaceFriendlyNames[it_Face], it_Mip);
                        return {};
                    }

                    CubemapFaceRegion& l_Region = l_FaceRegions[it_Face];
                    l_Region.m_Offset = l_Result.m_PixelData.size();
                    l_Region.m_Size = l_FaceSizeExpected;

                    l_Result.m_PixelData.insert(l_Result.m_PixelData.end(), l_DataPtr, l_DataPtr + l_FaceSizeExpected);
                    l_DataPtr += l_FaceSizeExpected;

                    size_t l_RowPad = AlignToDword(l_FaceSizeExpected) - l_FaceSizeExpected;
                    if (l_RowPad > 0)
                    {
                        if (l_DataPtr + l_RowPad > l_EndPtr)
                        {
                            TR_CORE_ERROR("KTX file '{}' missing padding bytes", l_Normalized.string());
                            return {};
                        }
                        l_DataPtr += l_RowPad;
                    }
                }

                l_Result.m_FaceRegions[it_Mip] = l_FaceRegions;

                size_t l_MipConsumed = static_cast<size_t>(l_DataPtr - l_MipLevelStart);
                size_t l_MipAligned = AlignToDword(l_MipConsumed);
                if (l_MipAligned > l_MipConsumed)
                {
                    size_t l_Pad = l_MipAligned - l_MipConsumed;
                    if (l_DataPtr + l_Pad > l_EndPtr)
                    {
                        TR_CORE_ERROR("KTX file '{}' missing mip padding", l_Normalized.string());
                        return {};
                    }
                    l_DataPtr += l_Pad;
                }

                l_CurrentWidth = std::max(1u, l_CurrentWidth / 2u);
                l_CurrentHeight = std::max(1u, l_CurrentHeight / 2u);
            }

            return l_Result;
        }

        CubemapTextureData SkyboxTextureLoader::LoadFromFileList(const std::array<std::filesystem::path, 6>& normalizedFaces)
        {
            CubemapTextureData l_Result{};

            if (normalizedFaces.empty())
            {
                return l_Result;
            }

            std::array<int, 6> l_Widths{};
            std::array<int, 6> l_Heights{};
            std::vector<uint8_t> l_Pixels{};

            stbi_set_flip_vertically_on_load(false);

            for (size_t it_Face = 0; it_Face < normalizedFaces.size(); ++it_Face)
            {
                const std::filesystem::path& l_Path = normalizedFaces[it_Face];
                if (l_Path.empty())
                {
                    TR_CORE_ERROR("Cubemap face {} has an empty path", s_FaceFriendlyNames[it_Face]);
                    return {};
                }

                int l_Channels = 0;
                int l_Width = 0;
                int l_Height = 0;

                std::string l_PathUtf8 = Utilities::FileManagement::NormalizePath(l_Path.string());
                stbi_uc* l_FacePixels = stbi_load(l_PathUtf8.c_str(), &l_Width, &l_Height, &l_Channels, STBI_rgb_alpha);
                if (!l_FacePixels)
                {
                    TR_CORE_ERROR("Failed to load cubemap face {} from '{}' ({})", s_FaceFriendlyNames[it_Face], l_PathUtf8, stbi_failure_reason());
                    return {};
                }

                const size_t l_FacePixelCount = static_cast<size_t>(l_Width) * static_cast<size_t>(l_Height) * 4u;
                CubemapFaceRegion l_Region{};
                l_Region.m_Offset = l_Pixels.size();
                l_Region.m_Size = l_FacePixelCount;

                l_Pixels.insert(l_Pixels.end(), l_FacePixels, l_FacePixels + l_FacePixelCount);
                stbi_image_free(l_FacePixels);

                l_Widths[it_Face] = l_Width;
                l_Heights[it_Face] = l_Height;

                if (it_Face > 0)
                {
                    if (l_Widths[it_Face] != l_Widths[0] || l_Heights[it_Face] != l_Heights[0])
                    {
                        TR_CORE_ERROR("Cubemap faces must share the same resolution. '{}' differs from the first face", l_PathUtf8);
                        return {};
                    }
                }

                if (l_Result.m_FaceRegions.empty())
                {
                    l_Result.m_FaceRegions.emplace_back();
                }
                l_Result.m_FaceRegions[0][it_Face] = l_Region;
            }

            l_Result.m_Width = static_cast<uint32_t>(l_Widths[0]);
            l_Result.m_Height = static_cast<uint32_t>(l_Heights[0]);
            l_Result.m_MipCount = 1;
            l_Result.m_BytesPerPixel = 4;
            l_Result.m_IsHdr = false;
            l_Result.m_Format = VK_FORMAT_R8G8B8A8_SRGB;
            l_Result.m_PixelData = std::move(l_Pixels);

            return l_Result;
        }
    }
}