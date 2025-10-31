#pragma once

#include "Core/Utilities.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Trident
{
    namespace Loader
    {
        /**
         * @brief Describes a logical source that can supply bytes for the model importer.
         *
         * The loader historically assumed assets originated from the filesystem. Tooling now
         * streams models from a variety of locations (drag-and-drop buffers, network payloads,
         * test harnesses) so the importer needs to abstract how bytes are provided. The source
         * keeps track of the identifying string, the working directory used for resolving
         * relative resources and an optional in-memory buffer when the asset is not backed by
         * a file path.
         */
        class ModelSource
        {
        public:
            /// Supported delivery mechanisms for model data.
            enum class SourceType
            {
                File,
                Memory
            };

            /// @brief Create a source that resolves bytes from the filesystem.
            static ModelSource FromFile(const std::string& filePath);

            /// @brief Create a source backed by an in-memory buffer.
            static ModelSource FromMemory(std::string identifier, std::vector<uint8_t> buffer, std::filesystem::path workingDirectory = {});

            [[nodiscard]] SourceType GetType() const noexcept { return m_Type; }
            [[nodiscard]] const std::string& GetIdentifier() const noexcept { return m_Identifier; }
            [[nodiscard]] const std::filesystem::path& GetWorkingDirectory() const noexcept { return m_WorkingDirectory; }
            [[nodiscard]] const std::vector<uint8_t>& GetBuffer() const noexcept { return m_Buffer; }
            [[nodiscard]] bool HasBuffer() const noexcept { return !m_Buffer.empty(); }

            /**
             * @brief Derive a format hint for Assimp when reading from memory.
             *
             * Assimp can autodetect most formats from memory buffers when supplied with the
             * original file extension. The loader stores the identifier verbatim so we extract
             * the extension lazily rather than forcing every caller to provide it explicitly.
             */
            [[nodiscard]] std::string GetFormatHint() const;

        private:
            ModelSource(SourceType type, std::string identifier, std::filesystem::path workingDirectory, std::vector<uint8_t> buffer);

            SourceType m_Type{ SourceType::File };                     //!< Delivery mechanism for the asset.
            std::string m_Identifier{};                                //!< Logical identifier for the source (path or name).
            std::filesystem::path m_WorkingDirectory{};                //!< Directory used to resolve relative resources.
            std::vector<uint8_t> m_Buffer{};                           //!< Optional in-memory payload when not file-backed.
        };
    }
}