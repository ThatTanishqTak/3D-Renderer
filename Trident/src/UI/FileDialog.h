#pragma once

#include <string>

namespace Trident
{
    namespace UI
    {
        class FileDialog
        {
        public:
            static bool Open(const char* id, std::string& path, const char* extension = nullptr);
        };
    }
}