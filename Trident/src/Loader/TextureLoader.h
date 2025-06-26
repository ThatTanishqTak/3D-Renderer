#pragma once

#include <string>
#include <vector>

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
    }
}