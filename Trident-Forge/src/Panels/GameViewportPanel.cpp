#include "GameViewportPanel.h"

#include "Renderer/RenderCommand.h"

#include <string>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <filesystem>
#include <system_error>

namespace EditorPanels
{
    GameViewportPanel::GameViewportPanel()
    {
        m_ViewportInfo.ViewportID = 2U;
        // Seed the export path so the UI presents a writable destination before recording begins.
        m_CurrentOutputPath = "Assets/Export/ViewportCapture.mp4";
        m_ExportStatusMessage = "Ready to export a clip.";
    }

    void GameViewportPanel::Render()
    {
        const bool l_WindowVisible = ImGui::Begin("Game Viewport");
        (void)l_WindowVisible;

        ImVec2 l_Available = ImGui::GetContentRegionAvail();

        // [FIX 1] Force Even Dimensions immediately
        // This ensures the Render Target, Readback Buffer, and Encoder all use the exact same resolution.
        if ((static_cast<uint32_t>(l_Available.x) % 2) != 0) l_Available.x += 1.0f;
        if ((static_cast<uint32_t>(l_Available.y) % 2) != 0) l_Available.y += 1.0f;

        m_ViewportInfo.Size = { l_Available.x, l_Available.y };
        Trident::RenderCommand::SetViewport(m_ViewportInfo.ViewportID, m_ViewportInfo);

        // [FIX 2] Update State BEFORE submission to prevent "Use-After-Free" validation crash
        UpdateExportState();

        // Submit the texture (ImGui will scale it slightly if it's 1px larger than the window, which is fine)
        SubmitViewportTexture(l_Available);
        RenderFrameRateOverlay();

        // UpdateExportState(); <-- Remove this old call

        m_IsHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
        m_IsFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);

        ImGui::End();
    }

    void GameViewportPanel::Update()
    {
        // Surface asset drops routed through ImGui (e.g., dragging levels from the content browser).
        if (m_OnAssetsDropped && ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* l_Payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
            {
                const std::string l_Path(reinterpret_cast<const char*>(l_Payload->Data), l_Payload->DataSize);
                m_OnAssetsDropped({ l_Path });
            }

            ImGui::EndDragDropTarget();
        }
    }

    void GameViewportPanel::SubmitViewportTexture(const ImVec2& viewportSize)
    {
        const VkDescriptorSet l_DescriptorSet = Trident::RenderCommand::GetViewportTexture(m_ViewportInfo.ViewportID);
        const ImTextureID l_TextureId = reinterpret_cast<ImTextureID>(l_DescriptorSet);

        // Respect ImGui's Vulkan backend expectations by treating zero as the invalid sentinel regardless of the
        // underlying ImTextureID typedef. This keeps runtime output aligned with the renderer's descriptor ownership.
        if (l_TextureId != ImTextureID{ 0 } && viewportSize.x > 0.0f && viewportSize.y > 0.0f)
        {
            ImGui::Image(l_TextureId, viewportSize, ImVec2(0, 0), ImVec2(1, 1));
        }
        else
        {
            ImGui::TextWrapped("Runtime viewport unavailable");
        }
    }

    bool GameViewportPanel::IsHovered() const
    {
        return m_IsHovered;
    }

    bool GameViewportPanel::IsFocused() const
    {
        return m_IsFocused;
    }

    void GameViewportPanel::SetGizmoState(Trident::GizmoState* gizmoState)
    {
        m_GizmoState = gizmoState;
    }

    void GameViewportPanel::SetAssetDropHandler(const std::function<void(const std::vector<std::string>&)>& onAssetsDropped)
    {
        m_OnAssetsDropped = onAssetsDropped;
    }

    void GameViewportPanel::SetRegistry(Trident::ECS::Registry* registry)
    {
        m_Registry = registry;
    }

    GameViewportPanel::ExportStatus GameViewportPanel::GetExportStatus() const
    {
        GameViewportPanel::ExportStatus l_Status{};
        l_Status.m_HasRuntimeCamera = Trident::RenderCommand::HasRuntimeCamera();
        l_Status.m_HasValidViewport = m_ViewportInfo.Size.x > 0.0f && m_ViewportInfo.Size.y > 0.0f;
        l_Status.m_RawExtent = { static_cast<uint32_t>(m_ViewportInfo.Size.x), static_cast<uint32_t>(m_ViewportInfo.Size.y) };
        l_Status.m_SanitizedExtent = CalculateSanitizedExtent();
        l_Status.m_IsRecording = m_IsRecording;
        l_Status.m_RecordingProgress = m_RecordingProgress;
        l_Status.m_OutputPath = m_CurrentOutputPath;
        l_Status.m_StatusMessage = m_ExportStatusMessage;

        return l_Status;
    }

    void GameViewportPanel::SetExportPath(const std::string& exportPath)
    {
        // Mirror the toolbar path locally so recording requests always have a target destination.
        m_CurrentOutputPath = exportPath;

        if (m_CurrentOutputPath.empty())
        {
            m_CurrentOutputPath = "Assets/Export/ViewportCapture.mp4";
        }
    }

    const std::string& GameViewportPanel::GetExportPath() const
    {
        return m_CurrentOutputPath;
    }

    void GameViewportPanel::RequestExportStart()
    {
        m_StartExportRequested = true;
    }

    void GameViewportPanel::RequestExportStop()
    {
        m_StopExportRequested = true;
    }

    void GameViewportPanel::RenderFrameRateOverlay()
    {
        // Pull averaged frame timing data from the renderer so the runtime viewport can surface the current FPS.
        const Trident::Renderer::FrameTimingStats l_FrameTimingStats = Trident::RenderCommand::GetFrameTimingStats();

        // Present the overlay only when the renderer has reported a valid timing sample.
        if (l_FrameTimingStats.AverageFPS <= 0.0)
        {
            return;
        }

        std::ostringstream l_FpsLabel{};
        l_FpsLabel << std::fixed << std::setprecision(1) << "FPS: " << l_FrameTimingStats.AverageFPS;

        const glm::vec2 l_TextPosition{ 10.0f, 30.0f };
        const glm::vec4 l_TextColor{ 1.0f, 1.0f, 1.0f, 1.0f };

        // Route the overlay through the renderer so text batching aligns with existing viewport submissions.
        Trident::RenderCommand::SubmitText(m_ViewportInfo.ViewportID, l_TextPosition, l_TextColor, l_FpsLabel.str());
    }

    void GameViewportPanel::UpdateExportState()
    {
        const bool l_HasRuntimeCamera = Trident::RenderCommand::HasRuntimeCamera();
        const bool l_HasValidViewport = m_ViewportInfo.Size.x > 0.0f && m_ViewportInfo.Size.y > 0.0f;

        if (!l_HasRuntimeCamera)
        {
            m_ExportStatusMessage = "No runtime camera available for capture.";
        }
        else if (!l_HasValidViewport)
        {
            m_ExportStatusMessage = "Viewport size is invalid for capture.";
        }
        else if (!m_IsRecording)
        {
            m_ExportStatusMessage = "Ready to export a clip.";
        }

        if (m_StartExportRequested)
        {
            m_StartExportRequested = false;

            if (!l_HasRuntimeCamera || !l_HasValidViewport)
            {
                return;
            }

            m_TargetClipDuration = QueryClipDurationSeconds();
            if (m_TargetClipDuration <= 0.0f)
            {
                m_TargetClipDuration = 3.0f; // Fallback duration when no clip data is available.
            }

            if (m_CurrentOutputPath.empty())
            {
                m_CurrentOutputPath = "Assets/Export/ViewportCapture.mp4";
            }

            // Ensure the parent directory exists before asking the renderer to start recording.
            std::filesystem::path l_OutputPath(m_CurrentOutputPath);
            std::filesystem::path l_ParentDirectory = l_OutputPath.parent_path();
            if (l_ParentDirectory.empty())
            {
                l_ParentDirectory = std::filesystem::path("Assets/Export");
                l_OutputPath = l_ParentDirectory / l_OutputPath;
                m_CurrentOutputPath = l_OutputPath.string();
            }

            std::error_code l_DirectoryError{};
            const bool l_DirectoryReady = std::filesystem::exists(l_ParentDirectory) || std::filesystem::create_directories(l_ParentDirectory, l_DirectoryError);
            if (!l_DirectoryReady)
            {
                m_ExportStatusMessage = "Unable to prepare export directory.";
                return;
            }

            m_RecordingStartTime = std::chrono::steady_clock::now();
            m_RecordingProgress = 0.0f;

            const VkExtent2D l_SanitizedExtent = CalculateSanitizedExtent();
            const bool l_RecordingStarted = Trident::RenderCommand::SetViewportRecordingEnabled(true, m_ViewportInfo.ViewportID,
                l_SanitizedExtent, m_CurrentOutputPath);
            if (!l_RecordingStarted)
            {
                m_ExportStatusMessage = "Viewport recording could not start. Verify swapchain and encoder readiness.";
                m_IsRecording = false;
            }
            else
            {
                m_IsRecording = true;
                m_ExportStatusMessage = "Export started.";
            }
        }

        if (m_IsRecording)
        {
            const auto l_Now = std::chrono::steady_clock::now();
            const float l_Elapsed = std::chrono::duration<float>(l_Now - m_RecordingStartTime).count();
            m_RecordingProgress = std::min(1.0f, l_Elapsed / m_TargetClipDuration);

            if (l_Elapsed >= m_TargetClipDuration)
            {
                m_StopExportRequested = true;
            }
            else
            {
                m_ExportStatusMessage = "Exporting clip...";
            }
        }

        if (m_StopExportRequested)
        {
            m_StopExportRequested = false;

            if (!m_IsRecording)
            {
                return;
            }

            const VkExtent2D l_Extent{ static_cast<uint32_t>(m_ViewportInfo.Size.x), static_cast<uint32_t>(m_ViewportInfo.Size.y) };
            const bool l_StopRequested = Trident::RenderCommand::SetViewportRecordingEnabled(false, m_ViewportInfo.ViewportID,
                l_Extent, m_CurrentOutputPath);
            if (l_StopRequested)
            {
                m_IsRecording = false;
                m_ExportStatusMessage = "Export stopped.";
            }
            else
            {
                m_ExportStatusMessage = "Viewport recording could not stop.";
            }
        }
    }

    VkExtent2D GameViewportPanel::CalculateSanitizedExtent() const
    {
        // YUV420P/H.264 encoding requires even-sized planes. Round the viewport size up to the next even pixel so the
        // renderer and encoder agree on a supported resolution before the user starts recording.
        const VkExtent2D l_RawExtent{ static_cast<uint32_t>(m_ViewportInfo.Size.x), static_cast<uint32_t>(m_ViewportInfo.Size.y) };
        VkExtent2D l_SanitizedExtent = l_RawExtent;
        if ((l_SanitizedExtent.width % 2U) != 0U)
        {
            ++l_SanitizedExtent.width;
        }

        if ((l_SanitizedExtent.height % 2U) != 0U)
        {
            ++l_SanitizedExtent.height;
        }

        return l_SanitizedExtent;
    }

    float GameViewportPanel::QueryClipDurationSeconds() const
    {
        // Placeholder: access to the runtime animation context is not yet exposed to the panel.
        // A future change can query AnimationPlayer::GetClipDuration once the runtime player becomes accessible here.
        (void)m_Registry;

        return 0.0f;
    }
}