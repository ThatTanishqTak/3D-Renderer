#pragma once

#include "ECS/Scene.h"
#include "Renderer/Camera/RuntimeCamera.h"

#include <filesystem>
#include <string>

/**
 * @brief Coordinates packaging the active editor scene for distribution.
 */
class EditorExportService
{
public:
    struct ExportOptions
    {
        std::filesystem::path m_OutputDirectory; ///< Destination folder chosen by the author.
        std::string m_BuildConfiguration;        ///< Visual Studio/MSVC configuration to build (e.g. Debug, Release).
    };

    struct ExportResult
    {
        bool m_Succeeded = false; ///< Indicates whether the export finished without fatal errors.
        std::string m_Message;    ///< Human-friendly status surfaced back to the UI.
    };

    explicit EditorExportService(std::filesystem::path projectRoot = std::filesystem::current_path());

    void SetProjectRoot(const std::filesystem::path& projectRoot);

    ExportResult ExportScene(const Trident::Scene& scene,
        const Trident::RuntimeCamera& runtimeCamera,
        const std::filesystem::path& currentScenePath,
        const ExportOptions& options) const;

private:
    bool BuildRuntimeProject(const std::string& configuration, std::string& outMessage) const;
    bool CopyRuntimeOutputs(const std::filesystem::path& destination, const std::string& configuration, std::string& outMessage) const;
    bool WriteRuntimeCameraFile(const std::filesystem::path& contentDirectory, const Trident::RuntimeCamera& runtimeCamera, std::string& outMessage) const;
    std::filesystem::path ResolveRuntimeProjectFile() const;
    std::filesystem::path ResolveRuntimeBuildDirectory() const;
    std::filesystem::path ResolveRuntimeBinaryDirectory(const std::string& configuration) const;
    std::filesystem::path ResolveRuntimeAssetsDirectory() const;

private:
    void InvalidateRuntimeCache();

private:
    std::filesystem::path m_ProjectRoot;                 ///< Root of the repository; used to locate the runtime project and assets.
    mutable bool m_HasCachedBuildDirectory = false;      ///< Tracks whether a build directory probe succeeded previously.
    mutable std::filesystem::path m_CachedBuildDirectory;///< Stores the last known runtime build directory.
    mutable bool m_HasCachedProjectFile = false;         ///< Indicates whether we probed for the runtime project file already.
    mutable std::filesystem::path m_CachedProjectFile;   ///< Stores the last known runtime project file path.
};