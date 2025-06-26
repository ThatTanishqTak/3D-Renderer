#include "Loader/TextureLoader.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "Core/Utilities.h"

namespace Trident
{
    namespace Loader
    {
        TextureData TextureLoader::Load(const std::string& filePath)
        {
            TextureData l_Texture;

            stbi_uc* l_Pixels = stbi_load(filePath.c_str(), &l_Texture.Width, &l_Texture.Height, &l_Texture.Channels, STBI_rgb_alpha);
            if (!l_Pixels)
            {
                TR_CORE_CRITICAL("Failed to load texture: {}", filePath);
                return l_Texture;
            }

            l_Texture.Channels = 4;
            size_t l_Size = static_cast<size_t>(l_Texture.Width) * static_cast<size_t>(l_Texture.Height) * 4;
            l_Texture.Pixels.assign(l_Pixels, l_Pixels + l_Size);
            stbi_image_free(l_Pixels);

            return l_Texture;
        }
    }
}