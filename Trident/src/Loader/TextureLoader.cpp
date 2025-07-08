#include "Loader/TextureLoader.h"

#include "Core/Utilities.h"

#include <stb_image.h>
#include <filesystem>

namespace Trident
{
    namespace Loader
    {
        TextureData TextureLoader::Load(const std::string& filePath)
        {
            TextureData l_Texture{};
            std::filesystem::path l_Path = Utilities::FileManagement::NormalizePath(filePath);

            stbi_set_flip_vertically_on_load(true);
            stbi_uc* l_Pixels = stbi_load(l_Path.u8string().c_str(), &l_Texture.Width, &l_Texture.Height, &l_Texture.Channels, STBI_rgb_alpha);
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