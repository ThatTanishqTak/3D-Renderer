#pragma once

#include <functional>
#include <memory>
#include <string>

#include <assimp/IOSystem.hpp>

namespace Trident
{
    namespace Loader
    {
        /**
         * @brief Describes where model data should be read from during import.
         *
         * Consumers can supply either a concrete filesystem path or provide a custom Assimp IO handler
         * factory so models embedded inside archives, memory streams or virtual file systems can be
         * imported. The optional texture resolver callback allows callers to control how auxiliary
         * resources referenced by the model are located.
         */
        struct ModelSource
        {
            using IoFactory = std::function<std::unique_ptr<Assimp::IOSystem>()>;
            using TextureResolver = std::function<std::string(const std::string&)>;

            std::string m_AssetIdentifier; //!< Stable identifier used for canonicalising skeleton data.
            std::string m_FilePath;        //!< Source path on disk when importing directly from the filesystem.
            std::string m_VirtualPath;     //!< Path or identifier passed to Assimp when using custom IO handlers.
            IoFactory m_CreateIoSystem;    //!< Factory creating the IO handler consumed by Assimp.
            TextureResolver m_TextureResolver; //!< Callback resolving texture paths referenced by the model.

            [[nodiscard]] bool HasFilePath() const { return !m_FilePath.empty(); }
            [[nodiscard]] bool HasCustomIo() const { return static_cast<bool>(m_CreateIoSystem); }

            [[nodiscard]] std::string Describe() const
            {
                if (!m_FilePath.empty())
                {
                    return m_FilePath;
                }
                if (!m_VirtualPath.empty())
                {
                    return m_VirtualPath;
                }
                if (!m_AssetIdentifier.empty())
                {
                    return m_AssetIdentifier;
                }

                return "Unknown model source";
            }

            static ModelSource FromFile(std::string filePath, std::string assetIdentifier = {})
            {
                ModelSource l_Source{};
                l_Source.m_FilePath = std::move(filePath);
                l_Source.m_AssetIdentifier = std::move(assetIdentifier);
                return l_Source;
            }

            static ModelSource FromVirtualFile(std::string virtualPath, IoFactory ioFactory, std::string assetIdentifier = {}, TextureResolver textureResolver = {})
            {
                ModelSource l_Source{};
                l_Source.m_VirtualPath = std::move(virtualPath);
                l_Source.m_CreateIoSystem = std::move(ioFactory);
                l_Source.m_AssetIdentifier = std::move(assetIdentifier);
                l_Source.m_TextureResolver = std::move(textureResolver);
                return l_Source;
            }
        };
    }
}