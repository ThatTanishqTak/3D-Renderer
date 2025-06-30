#include "Loader/TextureLoader.h"

#include <stb_image.h>

#include "Core/Utilities.h"

#include <filesystem>

namespace Trident
{
    namespace Loader
    {
        TextureData TextureLoader::Load(const std::string& filePath)
        {
            TextureData texture{};

            std::filesystem::path path = Utilities::FileManagement::NormalizePath(filePath);
            stbi_set_flip_vertically_on_load(true);
            stbi_uc* pixels = stbi_load(path.u8string().c_str(), &texture.Width, &texture.Height, &texture.Channels, STBI_rgb_alpha);
            if (!pixels)
            {
                TR_CORE_CRITICAL("Failed to load texture: {} ({})", filePath, stbi_failure_reason());
                return texture;
            }

            texture.Channels = 4;
            size_t size = static_cast<size_t>(texture.Width) * static_cast<size_t>(texture.Height) * static_cast<size_t>(texture.Channels);
            texture.Pixels.assign(pixels, pixels + size);
            stbi_image_free(pixels);

            return texture;
        }
    }
}