#pragma once

#include "Layer/Layer.h"

#include <filesystem>
#include <limits>
#include <unordered_set>
#include <vector>

#include "Geometry/Mesh.h"

#include <string>

// Forward declarations keep the header light and avoid leaking editor-only dependencies.
namespace ImGuizmo
{
    enum OPERATION;
}

namespace Trident
{
    namespace ECS
    {
        class Registry;
        using Entity = unsigned int;
    }
}

class ApplicationLayer : public Trident::Layer
{
public:
    /**
     * Prepare application-specific resources (scenes, editor state, etc.).
     */
    void Initialize() override;
    /**
     * Release resources acquired during Initialize().
     */
    void Shutdown() override;

    /**
     * Execute per-frame simulation such as tools or gameplay logic.
     */
    void Update() override;
    /**
     * Submit draw calls and UI for the current frame.
     */
    void Render() override;

private:
    void DrawTitleBar(float a_TitleBarHeight);
    void DrawSceneHierarchy();
    void DrawSceneViewport();
    void DrawInspector();
    void DrawContentBrowser();
    void RefreshDirectoryCache();
    void NavigateToDirectory(const std::filesystem::path& directory);
    bool IsModelFile(const std::filesystem::path& filePath) const;
    void HandleModelDrop(const std::filesystem::path& modelPath);
    bool IsPathInsideContentRoot(const std::filesystem::path& directory) const;

private:
    static constexpr float s_TitleBarHeight = 36.0f;
    static constexpr Trident::ECS::Entity s_InvalidEntity = std::numeric_limits<Trident::ECS::Entity>::max();

    Trident::ECS::Registry* m_Registry = nullptr; ///< Non-owning pointer to the engine registry driving the scene.
    Trident::ECS::Entity m_SelectedEntity = s_InvalidEntity; ///< Tracks which entity panels should display.

    std::filesystem::path m_ContentRoot{}; ///< Root directory surfaced in the content browser.
    std::filesystem::path m_CurrentContentDirectory{}; ///< Directory currently displayed in the content browser.
    std::vector<std::filesystem::directory_entry> m_ContentEntries; ///< Cached listing to avoid touching the disk every frame.
    std::unordered_set<std::string> m_ModelExtensions; ///< Normalised set of extensions accepted by the model importer.
    std::vector<Trident::Geometry::Mesh> m_LoadedMeshes; ///< CPU-side cache of meshes uploaded to the renderer.
    std::vector<Trident::Geometry::Material> m_LoadedMaterials; ///< CPU-side cache of materials paired with the meshes.

    ImGuizmo::OPERATION m_CurrentGizmoOperation{}; ///< Active transformation mode used by ImGuizmo in the viewport.
};