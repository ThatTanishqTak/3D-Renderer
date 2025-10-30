#include "EditorExportService.h"

#include "Core/Utilities.h"

#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <cstdlib>
#include <string_view>

namespace
{
    std::filesystem::path NormalisePath(const std::filesystem::path& path)
    {
        std::error_code l_Error{};
        std::filesystem::path l_Normalised = std::filesystem::weakly_canonical(path, l_Error);
        if (l_Error)
        {
            return path;
        }

        return l_Normalised;
    }
}

EditorExportService::EditorExportService(std::filesystem::path projectRoot) : m_ProjectRoot(std::move(projectRoot))
{
    m_ProjectRoot = NormalisePath(m_ProjectRoot);
}

void EditorExportService::SetProjectRoot(const std::filesystem::path& projectRoot)
{
    m_ProjectRoot = NormalisePath(projectRoot);
}

EditorExportService::ExportResult EditorExportService::ExportScene(const Trident::Scene& scene,
    const Trident::RuntimeCamera& runtimeCamera,
    const std::filesystem::path& currentScenePath,
    const ExportOptions& options) const
{
    ExportResult l_Result{};

    if (options.m_OutputDirectory.empty())
    {
        l_Result.m_Message = "Select an output directory before exporting.";
        TR_CORE_ERROR("Export aborted because no destination directory was provided.");
        return l_Result;
    }

    std::filesystem::path l_OutputDirectory = NormalisePath(options.m_OutputDirectory);
    std::error_code l_CreateError{};
    std::filesystem::create_directories(l_OutputDirectory, l_CreateError);
    if (l_CreateError)
    {
        std::ostringstream l_Stream;
        l_Stream << "Failed to create export directory '" << l_OutputDirectory.string() << "': " << l_CreateError.message();
        l_Result.m_Message = l_Stream.str();
        TR_CORE_ERROR(l_Result.m_Message);
        return l_Result;
    }

    const std::filesystem::path l_ContentDirectory = l_OutputDirectory / "Content";
    std::filesystem::create_directories(l_ContentDirectory, l_CreateError);
    if (l_CreateError)
    {
        std::ostringstream l_Stream;
        l_Stream << "Failed to create content directory '" << l_ContentDirectory.string() << "': " << l_CreateError.message();
        l_Result.m_Message = l_Stream.str();
        TR_CORE_ERROR(l_Result.m_Message);
        return l_Result;
    }

    std::string l_SceneFileName;
    if (!currentScenePath.empty() && currentScenePath.has_filename())
    {
        l_SceneFileName = currentScenePath.filename().string();
    }
    else
    {
        l_SceneFileName = scene.GetName().empty() ? "ExportedScene.trident" : scene.GetName() + ".trident";
    }

    std::filesystem::path l_SceneOutputPath = l_ContentDirectory / l_SceneFileName;
    if (l_SceneOutputPath.extension() != ".trident")
    {
        l_SceneOutputPath.replace_extension(".trident");
    }

    scene.Save(l_SceneOutputPath.string());
    TR_CORE_INFO("Scene serialised to '{}' for export.", l_SceneOutputPath.string());

    std::string l_CameraMessage;
    if (!WriteRuntimeCameraFile(l_ContentDirectory, runtimeCamera, l_CameraMessage))
    {
        l_Result.m_Message = l_CameraMessage;
        return l_Result;
    }

    std::string l_BuildMessage;
    if (!BuildRuntimeProject(options.m_BuildConfiguration, l_BuildMessage))
    {
        l_Result.m_Message = l_BuildMessage;
        return l_Result;
    }

    std::string l_CopyMessage;
    if (!CopyRuntimeOutputs(l_OutputDirectory, options.m_BuildConfiguration, l_CopyMessage))
    {
        l_Result.m_Message = l_CopyMessage;
        return l_Result;
    }

    std::ostringstream l_Stream;
    l_Stream << "Export complete. Packaged scene written to '" << l_OutputDirectory.string() << "'.";
    if (!l_CameraMessage.empty())
    {
        l_Stream << " " << l_CameraMessage;
    }

    l_Result.m_Succeeded = true;
    l_Result.m_Message = l_Stream.str();
    TR_CORE_INFO("{}", l_Result.m_Message);

    return l_Result;
}

bool EditorExportService::BuildRuntimeProject(const std::string& configuration, std::string& outMessage) const
{
    const std::filesystem::path l_ProjectFile = ResolveRuntimeProjectFile();
    if (l_ProjectFile.empty())
    {
        outMessage = "Runtime project file not found. Generate Visual Studio files before exporting.";
        TR_CORE_WARN(outMessage);
        return false;
    }

#if defined(_WIN32)
    std::string l_Command = "msbuild \"" + l_ProjectFile.string() + "\"";
    if (!configuration.empty())
    {
        l_Command += " /p:Configuration=" + configuration;
    }
    l_Command += " /p:Platform=x64";

    TR_CORE_INFO("Invoking runtime build command: {}", l_Command);
    const int l_Result = std::system(l_Command.c_str());
    if (l_Result != 0)
    {
        std::ostringstream l_Stream;
        l_Stream << "msbuild exited with code " << l_Result << ". Check Visual Studio 2022 installation.";
        outMessage = l_Stream.str();
        TR_CORE_ERROR(outMessage);
        return false;
    }
#else
    outMessage = "msbuild/devenv unavailable on this platform. Copying existing binaries.";
    TR_CORE_WARN(outMessage);
#endif

    return true;
}

bool EditorExportService::CopyRuntimeOutputs(const std::filesystem::path& destination, const std::string& configuration, std::string& outMessage) const
{
    const std::filesystem::path l_BinarySource = ResolveRuntimeBinaryDirectory(configuration);
    const std::filesystem::path l_AssetSource = ResolveRuntimeAssetsDirectory();

    if (l_BinarySource.empty() || !std::filesystem::exists(l_BinarySource))
    {
        outMessage = "Runtime binaries were not found. Build the Trident project before exporting.";
        TR_CORE_ERROR(outMessage);
        return false;
    }

    if (l_AssetSource.empty() || !std::filesystem::exists(l_AssetSource))
    {
        outMessage = "Runtime assets directory missing. Ensure Trident/Assets is available.";
        TR_CORE_ERROR(outMessage);
        return false;
    }

    const auto CopyDirectory = [](const std::filesystem::path& source, const std::filesystem::path& target, std::string_view description) -> bool
        {
            std::error_code l_Error{};
            std::filesystem::create_directories(target, l_Error);
            if (l_Error)
            {
                std::ostringstream l_Stream;
                l_Stream << "Failed to create " << description << " directory '" << target.string() << "': " << l_Error.message();
                TR_CORE_ERROR(l_Stream.str());
                return false;
            }

            for (std::filesystem::recursive_directory_iterator it_Source{ source, l_Error }; it_Source != std::filesystem::recursive_directory_iterator{}; ++it_Source)
            {
                if (l_Error)
                {
                    std::ostringstream l_Stream;
                    l_Stream << "Failed to enumerate " << description << " files: " << l_Error.message();
                    TR_CORE_ERROR(l_Stream.str());
                    return false;
                }

                const std::filesystem::path l_Relative = std::filesystem::relative(it_Source->path(), source, l_Error);
                if (l_Error)
                {
                    std::ostringstream l_Stream;
                    l_Stream << "Failed to compute relative path for '" << it_Source->path().string() << "': " << l_Error.message();
                    TR_CORE_ERROR(l_Stream.str());
                    return false;
                }

                const std::filesystem::path l_TargetPath = target / l_Relative;
                if (it_Source->is_directory())
                {
                    std::filesystem::create_directories(l_TargetPath, l_Error);
                    if (l_Error)
                    {
                        std::ostringstream l_Stream;
                        l_Stream << "Failed to create directory '" << l_TargetPath.string() << "': " << l_Error.message();
                        TR_CORE_ERROR(l_Stream.str());
                        return false;
                    }
                    continue;
                }

                std::filesystem::create_directories(l_TargetPath.parent_path(), l_Error);
                if (l_Error)
                {
                    std::ostringstream l_Stream;
                    l_Stream << "Failed to create parent directory for '" << l_TargetPath.string() << "': " << l_Error.message();
                    TR_CORE_ERROR(l_Stream.str());
                    return false;
                }

                std::filesystem::copy_file(it_Source->path(), l_TargetPath, std::filesystem::copy_options::update_existing, l_Error);
                if (l_Error)
                {
                    std::ostringstream l_Stream;
                    l_Stream << "Failed to copy '" << it_Source->path().string() << "' to '" << l_TargetPath.string() << "': " << l_Error.message();
                    TR_CORE_ERROR(l_Stream.str());
                    return false;
                }
            }

            return true;
        };

    const std::filesystem::path l_BinaryDestination = destination / "Bin";
    if (!CopyDirectory(l_BinarySource, l_BinaryDestination, "runtime binaries"))
    {
        outMessage = "Failed to copy runtime binaries.";
        return false;
    }

    const std::filesystem::path l_AssetDestination = destination / "Assets";
    if (!CopyDirectory(l_AssetSource, l_AssetDestination, "runtime assets"))
    {
        outMessage = "Failed to copy runtime assets.";
        return false;
    }

    outMessage.clear();
    return true;
}

bool EditorExportService::WriteRuntimeCameraFile(const std::filesystem::path& contentDirectory,
    const Trident::RuntimeCamera& runtimeCamera,
    std::string& outMessage) const
{
    const std::filesystem::path l_CameraFile = contentDirectory / "runtime_camera.txt";
    std::ofstream l_Stream(l_CameraFile, std::ios::trunc);
    if (!l_Stream.is_open())
    {
        outMessage = "Unable to write runtime camera description.";
        TR_CORE_ERROR("Failed to open '{}' for writing runtime camera data.", l_CameraFile.string());
        return false;
    }

    const glm::vec3 l_Position = runtimeCamera.GetPosition();
    const glm::vec3 l_Rotation = runtimeCamera.GetRotation();

    l_Stream << "# Trident Runtime Camera Export\n";
    l_Stream << std::setprecision(9);
    l_Stream << "Position " << l_Position.x << ' ' << l_Position.y << ' ' << l_Position.z << "\n";
    l_Stream << "Rotation " << l_Rotation.x << ' ' << l_Rotation.y << ' ' << l_Rotation.z << "\n";

    outMessage = "Runtime camera transform captured.";
    return true;
}

std::filesystem::path EditorExportService::ResolveRuntimeProjectFile() const
{
    const std::filesystem::path l_Solution = m_ProjectRoot / "Trident" / "Trident.sln";
    if (std::filesystem::exists(l_Solution))
    {
        return l_Solution;
    }

    const std::filesystem::path l_Project = m_ProjectRoot / "Trident" / "Trident.vcxproj";
    if (std::filesystem::exists(l_Project))
    {
        return l_Project;
    }

    return {};
}

std::filesystem::path EditorExportService::ResolveRuntimeBinaryDirectory(const std::string& configuration) const
{
    if (configuration.empty())
    {
        return m_ProjectRoot / "Trident" / "bin";
    }

    return m_ProjectRoot / "Trident" / "bin" / configuration;
}

std::filesystem::path EditorExportService::ResolveRuntimeAssetsDirectory() const
{
    return m_ProjectRoot / "Trident" / "Assets";
}