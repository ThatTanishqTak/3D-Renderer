#include "Loader/ModelSource.h"

#include "Core/Utilities.h"

#include <algorithm>
#include <cctype>

namespace Trident
{
    namespace Loader
    {
        namespace
        {
            std::filesystem::path NormaliseDirectory(const std::filesystem::path& path)
            {
                if (path.empty())
                {
                    return {};
                }

                std::filesystem::path l_Normalised = std::filesystem::path(Utilities::FileManagement::NormalizePath(path.string()));

                return l_Normalised;
            }
        }

        ModelSource ModelSource::FromFile(const std::string& filePath)
        {
            std::string l_Normalised = Utilities::FileManagement::NormalizePath(filePath);
            std::filesystem::path l_PathView{ l_Normalised };
            std::filesystem::path l_WorkingDirectory = l_PathView.has_parent_path() ? l_PathView.parent_path() : std::filesystem::path{};

            return ModelSource(SourceType::File, l_Normalised, NormaliseDirectory(l_WorkingDirectory), {});
        }

        ModelSource ModelSource::FromMemory(std::string identifier, std::vector<uint8_t> buffer, std::filesystem::path workingDirectory)
        {
            if (identifier.empty())
            {
                identifier = "InMemoryAsset";
            }

            std::filesystem::path l_Working = NormaliseDirectory(workingDirectory);
            if (l_Working.empty())
            {
                l_Working = std::filesystem::current_path();
            }

            return ModelSource(SourceType::Memory, std::move(identifier), std::move(l_Working), std::move(buffer));
        }

        std::string ModelSource::GetFormatHint() const
        {
            std::filesystem::path l_PathView{ m_Identifier };
            if (!l_PathView.has_extension())
            {
                return {};
            }

            std::string l_Extension = l_PathView.extension().string();
            std::transform(l_Extension.begin(), l_Extension.end(), l_Extension.begin(), [](unsigned char character)
                {
                    return static_cast<char>(std::tolower(character));
                });
            if (!l_Extension.empty() && l_Extension.front() == '.')
            {
                l_Extension.erase(l_Extension.begin());
            }

            return l_Extension;
        }

        ModelSource::ModelSource(SourceType type, std::string identifier, std::filesystem::path workingDirectory, std::vector<uint8_t> buffer) : m_Type(type),
            m_Identifier(std::move(identifier)), m_WorkingDirectory(std::move(workingDirectory)), m_Buffer(std::move(buffer))
        {

        }
    }
}