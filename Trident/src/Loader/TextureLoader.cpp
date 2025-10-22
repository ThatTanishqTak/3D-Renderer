#include "Loader/TextureLoader.h"

#include "Core/Utilities.h"

#include <stb_image.h>
#define TINYEXR_USE_OPENMP 0
#define TINYEXR_IMPLEMENTATION
#include <tinyexr.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cstdint>
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

    size_t AlignToDword(size_t value)
    {
        return (value + 3u) & ~static_cast<size_t>(3u);
    }

    std::string ToLowerCopy(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char character)
            {
                return static_cast<char>(std::tolower(character));
            });
        return text;
    }

    bool TryMatchFaceIndex(const std::filesystem::path& path, size_t& outIndex)
    {
        std::string l_Stem = ToLowerCopy(path.stem().string());
        for (size_t it_Index = 0; it_Index < s_FaceTokens.size(); ++it_Index)
        {
            if (l_Stem.find(s_FaceTokens[it_Index]) != std::string::npos)
            {
                outIndex = it_Index;
                return true;
            }
        }
        return false;
    }

    uint16_t FloatToHalf(float value)
    {
        // Convert IEEE 754 float to 16-bit float with round-to-nearest-even semantics.
        uint32_t l_Bits = std::bit_cast<uint32_t>(value);
        uint32_t l_Sign = (l_Bits >> 16u) & 0x8000u;
        int32_t l_Exponent = static_cast<int32_t>((l_Bits >> 23u) & 0xFFu) - 127 + 15;
        uint32_t l_Mantissa = l_Bits & 0x7FFFFFu;

        if (l_Exponent <= 0)
        {
            if (l_Exponent < -10)
            {
                // Underflow, return signed zero.
                return static_cast<uint16_t>(l_Sign);
            }

            // Convert to denormalized half-precision value.
            l_Mantissa |= 0x800000u;
            uint32_t l_Shift = static_cast<uint32_t>(1 - l_Exponent);
            uint16_t l_Denorm = static_cast<uint16_t>(l_Mantissa >> (13u + l_Shift));
            // Apply round-to-nearest-even adjustment.
            if ((l_Mantissa >> (12u + l_Shift)) & 0x1u)
            {
                l_Denorm = static_cast<uint16_t>(l_Denorm + 1u);
            }

            return static_cast<uint16_t>(l_Sign | l_Denorm);
        }
        else if (l_Exponent >= 31)
        {
            // Overflow -> represent as infinity; keep mantissa for NaNs.
            return static_cast<uint16_t>(l_Sign | 0x7C00u | (l_Mantissa ? 0x1u : 0u));
        }

        uint16_t l_Result = static_cast<uint16_t>(l_Sign | (static_cast<uint32_t>(l_Exponent) << 10u) | (l_Mantissa >> 13u));
        if (l_Mantissa & 0x1000u)
        {
            // Round to nearest even for normalized values.
            l_Result = static_cast<uint16_t>(l_Result + 1u);
        }

        return l_Result;
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

        CubemapTextureData SkyboxTextureLoader::LoadFromExrFaces(const std::array<std::filesystem::path, 6>& normalizedFaces)
        {
            CubemapTextureData l_Result{};
            std::vector<uint8_t> l_Pixels{};
            std::array<int, 6> l_Widths{};
            std::array<int, 6> l_Heights{};

            for (size_t it_Face = 0; it_Face < normalizedFaces.size(); ++it_Face)
            {
                const std::filesystem::path& l_Path = normalizedFaces[it_Face];
                std::string l_PathUtf8 = Utilities::FileManagement::NormalizePath(l_Path.string());

                EXRVersion l_Version{};
                int l_VersionResult = ParseEXRVersionFromFile(&l_Version, l_PathUtf8.c_str());
                if (l_VersionResult != TINYEXR_SUCCESS)
                {
                    TR_CORE_ERROR("Failed to parse EXR version for cubemap face {} from '{}'", s_FaceFriendlyNames[it_Face], l_PathUtf8);
                    return {};
                }

                EXRHeader l_Header{};
                InitEXRHeader(&l_Header);
                EXRImage l_Image{};
                InitEXRImage(&l_Image);

                const char* l_Error = nullptr;
                int l_HeaderResult = ParseEXRHeaderFromFile(&l_Header, &l_Version, l_PathUtf8.c_str(), &l_Error);
                if (l_HeaderResult != TINYEXR_SUCCESS)
                {
                    if (l_Error != nullptr)
                    {
                        TR_CORE_ERROR("Failed to parse EXR header for cubemap face {} from '{}' ({})", s_FaceFriendlyNames[it_Face], l_PathUtf8, l_Error);
                        FreeEXRErrorMessage(l_Error);
                    }
                    else
                    {
                        TR_CORE_ERROR("Failed to parse EXR header for cubemap face {} from '{}'", s_FaceFriendlyNames[it_Face], l_PathUtf8);
                    }

                    FreeEXRHeader(&l_Header);

                    return {};
                }

                // Request float data for every channel to simplify downstream conversion to half floats.
                for (int it_Channel = 0; it_Channel < l_Header.num_channels; ++it_Channel)
                {
                    l_Header.requested_pixel_types[it_Channel] = TINYEXR_PIXELTYPE_FLOAT;
                }

                int l_LoadResult = LoadEXRImageFromFile(&l_Image, &l_Header, l_PathUtf8.c_str(), &l_Error);
                if (l_LoadResult != TINYEXR_SUCCESS)
                {
                    if (l_Error != nullptr)
                    {
                        TR_CORE_ERROR("Failed to load EXR image for cubemap face {} from '{}' ({})", s_FaceFriendlyNames[it_Face], l_PathUtf8, l_Error);
                        FreeEXRErrorMessage(l_Error);
                    }
                    else
                    {
                        TR_CORE_ERROR("Failed to load EXR image for cubemap face {} from '{}'", s_FaceFriendlyNames[it_Face], l_PathUtf8);
                    }
                    FreeEXRImage(&l_Image);
                    FreeEXRHeader(&l_Header);

                    return {};
                }

                if (l_Image.num_channels < 3)
                {
                    TR_CORE_ERROR("EXR cubemap face {} from '{}' must contain at least three channels", s_FaceFriendlyNames[it_Face], l_PathUtf8);
                    FreeEXRImage(&l_Image);
                    FreeEXRHeader(&l_Header);

                    return {};
                }

                l_Widths[it_Face] = l_Image.width;
                l_Heights[it_Face] = l_Image.height;

                if (it_Face > 0)
                {
                    if (l_Widths[it_Face] != l_Widths[0] || l_Heights[it_Face] != l_Heights[0])
                    {
                        TR_CORE_ERROR("EXR cubemap faces must share the same resolution. '{}' differs from the first face", l_PathUtf8);
                        FreeEXRImage(&l_Image);
                        FreeEXRHeader(&l_Header);

                        return {};
                    }
                }

                // Identify channel indices so R/G/B map correctly even if the file orders them differently.
                int l_RIndex = -1;
                int l_GIndex = -1;
                int l_BIndex = -1;
                int l_AIndex = -1;
                for (int it_Channel = 0; it_Channel < l_Header.num_channels; ++it_Channel)
                {
                    const char* l_Name = l_Header.channels[it_Channel].name;
                    if (l_Name == nullptr || l_Name[0] == '\0')
                    {
                        continue;
                    }

                    if (l_Name[0] == 'R' && l_RIndex == -1)
                    {
                        l_RIndex = it_Channel;
                    }
                    else if (l_Name[0] == 'G' && l_GIndex == -1)
                    {
                        l_GIndex = it_Channel;
                    }
                    else if (l_Name[0] == 'B' && l_BIndex == -1)
                    {
                        l_BIndex = it_Channel;
                    }
                    else if (l_Name[0] == 'A' && l_AIndex == -1)
                    {
                        l_AIndex = it_Channel;
                    }
                }

                // Default the optional alpha channel to opaque if the file does not provide one.
                size_t l_PixelCount = static_cast<size_t>(l_Image.width) * static_cast<size_t>(l_Image.height);
                std::vector<uint16_t> l_FaceHalf(static_cast<size_t>(4u) * l_PixelCount);

                const float* l_RChannel = l_RIndex >= 0 ? reinterpret_cast<float*>(l_Image.images[l_RIndex]) : nullptr;
                const float* l_GChannel = l_GIndex >= 0 ? reinterpret_cast<float*>(l_Image.images[l_GIndex]) : nullptr;
                const float* l_BChannel = l_BIndex >= 0 ? reinterpret_cast<float*>(l_Image.images[l_BIndex]) : nullptr;
                const float* l_AChannel = l_AIndex >= 0 ? reinterpret_cast<float*>(l_Image.images[l_AIndex]) : nullptr;

                for (size_t it_Pixel = 0; it_Pixel < l_PixelCount; ++it_Pixel)
                {
                    float l_R = l_RChannel != nullptr ? l_RChannel[it_Pixel] : 0.0f;
                    float l_G = l_GChannel != nullptr ? l_GChannel[it_Pixel] : 0.0f;
                    float l_B = l_BChannel != nullptr ? l_BChannel[it_Pixel] : 0.0f;
                    float l_A = l_AChannel != nullptr ? l_AChannel[it_Pixel] : 1.0f;

                    size_t l_BaseIndex = it_Pixel * 4u;
                    l_FaceHalf[l_BaseIndex + 0u] = FloatToHalf(l_R);
                    l_FaceHalf[l_BaseIndex + 1u] = FloatToHalf(l_G);
                    l_FaceHalf[l_BaseIndex + 2u] = FloatToHalf(l_B);
                    l_FaceHalf[l_BaseIndex + 3u] = FloatToHalf(l_A);
                }

                CubemapFaceRegion l_Region{};
                l_Region.m_Offset = l_Pixels.size();
                l_Region.m_Size = l_FaceHalf.size() * sizeof(uint16_t);
                const uint8_t* l_RawFace = reinterpret_cast<const uint8_t*>(l_FaceHalf.data());
                l_Pixels.insert(l_Pixels.end(), l_RawFace, l_RawFace + l_Region.m_Size);

                if (l_Result.m_FaceRegions.empty())
                {
                    l_Result.m_FaceRegions.emplace_back();
                }
                l_Result.m_FaceRegions[0][it_Face] = l_Region;

                FreeEXRImage(&l_Image);
                FreeEXRHeader(&l_Header);
            }

            l_Result.m_Width = static_cast<uint32_t>(l_Widths[0]);
            l_Result.m_Height = static_cast<uint32_t>(l_Heights[0]);
            l_Result.m_MipCount = 1;
            l_Result.m_BytesPerPixel = 8;
            l_Result.m_IsHdr = true;
            l_Result.m_Format = VK_FORMAT_R16G16B16A16_SFLOAT;
            l_Result.m_PixelData = std::move(l_Pixels);

            return l_Result;
        }

        CubemapTextureData SkyboxTextureLoader::LoadFromFileList(const std::array<std::filesystem::path, 6>& normalizedFaces)
        {
            CubemapTextureData l_Result{};

            if (normalizedFaces.empty())
            {
                return l_Result;
            }

            bool l_AllExr = true;
            for (const auto& it_Path : normalizedFaces)
            {
                if (it_Path.empty())
                {
                    l_AllExr = false;
                    break;
                }
                std::string l_Extension = ToLowerCopy(it_Path.extension().string());
                if (l_Extension != ".exr")
                {
                    l_AllExr = false;
                    break;
                }
            }

            if (l_AllExr)
            {
                // Decode high-dynamic-range EXR faces through TinyEXR when available.
                return LoadFromExrFaces(normalizedFaces);
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