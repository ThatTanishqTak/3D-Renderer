#include "Loader/TextureLoader.h"

#include "Core/Utilities.h"

#include <stb_image.h>

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
    }
}