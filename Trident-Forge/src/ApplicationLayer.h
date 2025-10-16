#pragma once

#include "Layer/Layer.h"

#include <filesystem>
#include <limits>
#include <vector>

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

private:
    static constexpr float s_TitleBarHeight = 36.0f;
    static constexpr Trident::ECS::Entity s_InvalidEntity = std::numeric_limits<Trident::ECS::Entity>::max();

    Trident::ECS::Registry* m_Registry = nullptr; ///< Non-owning pointer to the engine registry driving the scene.
    Trident::ECS::Entity m_SelectedEntity = s_InvalidEntity; ///< Tracks which entity panels should display.

    std::filesystem::path m_ContentRoot{}; ///< Root directory surfaced in the content browser.
    std::vector<std::filesystem::directory_entry> m_ContentEntries; ///< Cached listing to avoid touching the disk every frame.

    ImGuizmo::OPERATION m_CurrentGizmoOperation{}; ///< Active transformation mode used by ImGuizmo in the viewport.
};