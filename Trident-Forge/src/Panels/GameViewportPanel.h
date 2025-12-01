#pragma once

#include "Renderer/Renderer.h"
#include "GizmoState.h"
#include "ECS/Registry.h"

#include <imgui.h>
#include <functional>
#include <vector>
#include <chrono>
#include <string>
#include <filesystem>
#include <vulkan/vulkan.h>

namespace EditorPanels
{
    /**
     * @brief Displays the runtime camera output while respecting renderer viewport routing.
     */
    class GameViewportPanel
    {
    public:
        GameViewportPanel();

        void Render();
        void Update();

        [[nodiscard]] bool IsHovered() const;
        [[nodiscard]] bool IsFocused() const;

        void SetGizmoState(Trident::GizmoState* gizmoState);
        void SetAssetDropHandler(const std::function<void(const std::vector<std::string>&)>& onAssetsDropped);
        void SetRegistry(Trident::ECS::Registry* registry);

        /**
         * @brief Records the most recent export configuration so other panels can drive clip capture.
         */
        struct ExportStatus
        {
            bool m_HasRuntimeCamera = false;
            bool m_HasValidViewport = false;
            VkExtent2D m_RawExtent{ 0U, 0U };
            VkExtent2D m_SanitizedExtent{ 0U, 0U };
            bool m_IsRecording = false;
            float m_RecordingProgress = 0.0f;
            std::string m_OutputPath;
            std::string m_StatusMessage;
        };

        [[nodiscard]] ExportStatus GetExportStatus() const;
        void SetExportPath(const std::string& exportPath);
        [[nodiscard]] const std::string& GetExportPath() const;
        void RequestExportStart();
        void RequestExportStop();

    private:
        void SubmitViewportTexture(const ImVec2& viewportSize);
        void RenderFrameRateOverlay();
        void UpdateExportState();
        [[nodiscard]] VkExtent2D CalculateSanitizedExtent() const;
        float QueryClipDurationSeconds() const;

    private:
        Trident::ViewportInfo m_ViewportInfo{}; // Renderer viewport metadata for the runtime view.
        bool m_IsHovered = false; // Hover tracking used to gate runtime-focused shortcuts.
        bool m_IsFocused = false; // Focus tracking used to route keyboard input to the runtime viewport.
        Trident::GizmoState* m_GizmoState = nullptr; // Shared gizmo state pointer for future runtime gizmo support.
        Trident::ECS::Registry* m_Registry = nullptr; // Registry pointer retained for future runtime entity queries.
        std::function<void(const std::vector<std::string>&)> m_OnAssetsDropped; // Callback invoked when assets are dropped.
        bool m_IsRecording = false; // Tracks whether the panel has initiated a clip export.
        float m_TargetClipDuration = 0.0f; // Duration requested for the current export.
        std::chrono::steady_clock::time_point m_RecordingStartTime{}; // Clock used to track progress.
        std::string m_CurrentOutputPath; // Destination path reported to the user.
        float m_RecordingProgress = 0.0f; // Normalised progress for the UI.
        bool m_StartExportRequested = false; // Flags a start request driven by the toolbar.
        bool m_StopExportRequested = false; // Flags a stop request driven by the toolbar.
        std::string m_ExportStatusMessage; // Surfaces the most recent export status to the toolbar.
    };
}