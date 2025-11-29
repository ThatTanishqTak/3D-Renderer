#include "GameViewportPanel.h"

#include "Renderer/RenderCommand.h"

#include <string>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <filesystem>
#include <system_error>
#include <cstdio>
#include <cstring>

namespace EditorPanels
{
    GameViewportPanel::GameViewportPanel()
    {
        m_ViewportInfo.ViewportID = 2U;
        // Seed the export path so the UI presents a writable destination before recording begins.
        m_CurrentOutputPath = "Assets/Export/ViewportCapture.mp4";
        std::memset(m_OutputPathBuffer.data(), 0, m_OutputPathBuffer.size());
        std::snprintf(m_OutputPathBuffer.data(), m_OutputPathBuffer.size(), "%s", m_CurrentOutputPath.c_str());
    }

    void GameViewportPanel::Render()
    {
        const bool l_WindowVisible = ImGui::Begin("Game Viewport");
        (void)l_WindowVisible;
        // Keep submission unconditional so dockspace stress tests retain the runtime viewport node consistently.

        const ImVec2 l_Available = ImGui::GetContentRegionAvail();
        m_ViewportInfo.Size = { l_Available.x, l_Available.y };
        Trident::RenderCommand::SetViewport(m_ViewportInfo.ViewportID, m_ViewportInfo);

        SubmitViewportTexture(l_Available);
        RenderFrameRateOverlay();
        RenderExportControls();

        // Keep hover/focus state in sync with the render path so runtime shortcuts can respect ImGui focus rules.
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
            ImGui::TextUnformatted("Runtime viewport unavailable");
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

    void GameViewportPanel::RenderExportControls()
    {
        const bool l_HasRuntimeCamera = Trident::RenderCommand::HasRuntimeCamera();
        const bool l_ViewValid = m_ViewportInfo.Size.x > 0.0f && m_ViewportInfo.Size.y > 0.0f;

        if (!l_HasRuntimeCamera)
        {
            ImGui::TextUnformatted("No runtime camera available for capture.");
            return;
        }

        if (!l_ViewValid)
        {
            ImGui::TextUnformatted("Viewport size is invalid for capture.");
            return;
        }

        if (!m_IsRecording)
        {
            ImGui::InputText("Export Path", m_OutputPathBuffer.data(), m_OutputPathBuffer.size());
            ImGui::TextWrapped("Choose where the exported clip will be written before starting the capture. Missing folders are created automatically.");

            if (ImGui::Button("Start Clip Export"))
            {
                m_TargetClipDuration = QueryClipDurationSeconds();
                if (m_TargetClipDuration <= 0.0f)
                {
                    m_TargetClipDuration = 3.0f; // Fallback duration when no clip data is available.
                }

                // Capture the path from the UI buffer so the renderer writes to the user-selected destination.
                m_CurrentOutputPath = std::string(m_OutputPathBuffer.data());

                // Fall back to the default asset export folder when the user has not provided a path.
                if (m_CurrentOutputPath.empty())
                {
                    m_CurrentOutputPath = "Assets/Export/ViewportCapture.mp4";
                    std::snprintf(m_OutputPathBuffer.data(), m_OutputPathBuffer.size(), "%s", m_CurrentOutputPath.c_str());
                }

                // Ensure the parent directory exists before asking the renderer to start recording.
                std::filesystem::path l_OutputPath(m_CurrentOutputPath);
                std::filesystem::path l_ParentDirectory = l_OutputPath.parent_path();
                if (l_ParentDirectory.empty())
                {
                    l_ParentDirectory = std::filesystem::path("Assets/Export");
                    l_OutputPath = l_ParentDirectory / l_OutputPath;
                    m_CurrentOutputPath = l_OutputPath.string();
                    std::snprintf(m_OutputPathBuffer.data(), m_OutputPathBuffer.size(), "%s", m_CurrentOutputPath.c_str());
                }

                std::error_code l_DirectoryError{};
                const bool l_DirectoryReady = std::filesystem::exists(l_ParentDirectory) || std::filesystem::create_directories(l_ParentDirectory, l_DirectoryError);
                if (!l_DirectoryReady)
                {
                    ImGui::TextUnformatted("Unable to prepare export directory.");
                    return;
                }

                m_RecordingStartTime = std::chrono::steady_clock::now();
                m_RecordingProgress = 0.0f;

                Trident::RenderCommand::SetViewportRecordingEnabled(true, m_ViewportInfo.ViewportID,
                    { static_cast<uint32_t>(m_ViewportInfo.Size.x), static_cast<uint32_t>(m_ViewportInfo.Size.y) }, m_CurrentOutputPath);
                m_IsRecording = true;
            }
        }
        else
        {
            const auto l_Now = std::chrono::steady_clock::now();
            const float l_Elapsed = std::chrono::duration<float>(l_Now - m_RecordingStartTime).count();
            m_RecordingProgress = std::min(1.0f, l_Elapsed / m_TargetClipDuration);

            ImGui::Text("Exporting clip... %.0f%%", m_RecordingProgress * 100.0f);
            ImGui::Text("Output: %s", m_CurrentOutputPath.c_str());

            if (l_Elapsed >= m_TargetClipDuration)
            {
                Trident::RenderCommand::SetViewportRecordingEnabled(false, m_ViewportInfo.ViewportID,
                    { static_cast<uint32_t>(m_ViewportInfo.Size.x), static_cast<uint32_t>(m_ViewportInfo.Size.y) }, m_CurrentOutputPath);
                m_IsRecording = false;
            }

            if (ImGui::Button("Stop Export"))
            {
                Trident::RenderCommand::SetViewportRecordingEnabled(false, m_ViewportInfo.ViewportID,
                    { static_cast<uint32_t>(m_ViewportInfo.Size.x), static_cast<uint32_t>(m_ViewportInfo.Size.y) }, m_CurrentOutputPath);
                m_IsRecording = false;
            }
        }
    }

    float GameViewportPanel::QueryClipDurationSeconds() const
    {
        // Placeholder: access to the runtime animation context is not yet exposed to the panel.
        // A future change can query AnimationPlayer::GetClipDuration once the runtime player becomes accessible here.
        (void)m_Registry;

        return 0.0f;
    }
}