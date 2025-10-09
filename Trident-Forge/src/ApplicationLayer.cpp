#include "ApplicationLayer.h"

#include <imgui.h>
#include <ImGuizmo.h>

#include <string>
#include <vector>
#include <limits>
#include <algorithm>

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include "Core/Utilities.h"
#include "Camera/CameraComponent.h"
#include "Renderer/Renderer.h"
#include "ECS/Components/TransformComponent.h"
#include "ECS/Components/SpriteComponent.h"
#include "ECS/Components/LightComponent.h"

namespace
{
    // Compose a transform matrix from the ECS component for ImGuizmo consumption.
    glm::mat4 ComposeTransform(const Trident::Transform& transform)
    {
        glm::mat4 l_ModelMatrix{ 1.0f };
        l_ModelMatrix = glm::translate(l_ModelMatrix, transform.Position);
        l_ModelMatrix = glm::rotate(l_ModelMatrix, glm::radians(transform.Rotation.x), glm::vec3{ 1.0f, 0.0f, 0.0f });
        l_ModelMatrix = glm::rotate(l_ModelMatrix, glm::radians(transform.Rotation.y), glm::vec3{ 0.0f, 1.0f, 0.0f });
        l_ModelMatrix = glm::rotate(l_ModelMatrix, glm::radians(transform.Rotation.z), glm::vec3{ 0.0f, 0.0f, 1.0f });
        l_ModelMatrix = glm::scale(l_ModelMatrix, transform.Scale);

        return l_ModelMatrix;
    }

    // Convert an ImGuizmo-manipulated matrix back to the engine's transform component.
    Trident::Transform DecomposeTransform(const glm::mat4& modelMatrix, const Trident::Transform& defaultTransform)
    {
        glm::vec3 l_Scale{};
        glm::quat l_Rotation{};
        glm::vec3 l_Translation{};
        glm::vec3 l_Skew{};
        glm::vec4 l_Perspective{};

        if (!glm::decompose(modelMatrix, l_Scale, l_Rotation, l_Translation, l_Skew, l_Perspective))
        {
            // Preserve the previous values if decomposition fails, avoiding sudden jumps.
            return defaultTransform;
        }

        Trident::Transform l_Result = defaultTransform;
        l_Result.Position = l_Translation;
        l_Result.Scale = l_Scale;
        l_Result.Rotation = glm::degrees(glm::eulerAngles(glm::normalize(l_Rotation)));

        return l_Result;
    }

    // Produce a projection matrix based on the camera component settings and desired viewport aspect ratio.
    glm::mat4 BuildCameraProjectionMatrix(const Trident::CameraComponent& cameraComponent, float viewportAspect)
    {
        float l_Aspect = cameraComponent.OverrideAspectRatio ? cameraComponent.AspectRatio : viewportAspect;
        l_Aspect = std::max(l_Aspect, 0.0001f);

        if (cameraComponent.UseCustomProjection)
        {
            // Advanced users can inject a bespoke matrix; the editor relays it without modification.
            return cameraComponent.CustomProjection;
        }

        if (cameraComponent.Projection == Trident::ProjectionType::Orthographic)
        {
            // Orthographic size represents the vertical span; derive width from the resolved aspect ratio.
            const float l_HalfHeight = cameraComponent.OrthographicSize * 0.5f;
            const float l_HalfWidth = l_HalfHeight * l_Aspect;
            glm::mat4 l_Projection = glm::ortho(-l_HalfWidth, l_HalfWidth, -l_HalfHeight, l_HalfHeight, cameraComponent.NearClip, cameraComponent.FarClip);
            l_Projection[1][1] *= -1.0f;
            
            return l_Projection;
        }

        glm::mat4 l_Projection = glm::perspective(glm::radians(cameraComponent.FieldOfView), l_Aspect, cameraComponent.NearClip, cameraComponent.FarClip);
        l_Projection[1][1] *= -1.0f;
        
        return l_Projection;
    }

    // Dedicated sentinel used when no entity is highlighted inside the inspector.
    constexpr Trident::ECS::Entity s_InvalidEntity = std::numeric_limits<Trident::ECS::Entity>::max();

    // Console window configuration toggles stored across frames for a consistent user experience.
    bool s_ShowConsoleErrors = true;
    bool s_ShowConsoleWarnings = true;
    bool s_ShowConsoleLogs = true;
    bool s_ConsoleAutoScroll = true;
    size_t s_LastConsoleEntryCount = 0;

    // Convert a timestamp to a human readable clock string that fits in the console.
    std::string FormatConsoleTimestamp(const std::chrono::system_clock::time_point& a_TimePoint)
    {
        std::time_t l_TimeT = std::chrono::system_clock::to_time_t(a_TimePoint);
        std::tm l_LocalTime{};

#if defined(_MSC_VER)
        localtime_s(&l_LocalTime, &l_TimeT);
#else
        localtime_r(&l_TimeT, &l_LocalTime);
#endif

        std::ostringstream l_Stream;
        l_Stream << std::put_time(&l_LocalTime, "%H:%M:%S");
        return l_Stream.str();
    }

    // Decide whether an entry should be shown given the active severity toggles.
    bool ShouldDisplayConsoleEntry(spdlog::level::level_enum a_Level)
    {
        switch (a_Level)
        {
        case spdlog::level::critical:
        case spdlog::level::err:
            return s_ShowConsoleErrors;
        case spdlog::level::warn:
            return s_ShowConsoleWarnings;
        default:
            return s_ShowConsoleLogs;
        }
    }

    // Pick a colour for a log entry so important events stand out while browsing history.
    ImVec4 GetConsoleColour(spdlog::level::level_enum a_Level)
    {
        switch (a_Level)
        {
        case spdlog::level::critical:
        case spdlog::level::err:
            return { 0.94f, 0.33f, 0.33f, 1.0f };
        case spdlog::level::warn:
            return { 0.97f, 0.78f, 0.26f, 1.0f };
        case spdlog::level::debug:
        case spdlog::level::trace:
            return { 0.60f, 0.80f, 0.98f, 1.0f };
        default:
            return { 0.85f, 0.85f, 0.85f, 1.0f };
        }
    }
}

ApplicationLayer::ApplicationLayer()
{
    // Initialize logging and the Forge window
    Trident::Utilities::Log::Init();

    m_Window = std::make_unique<Trident::Window>(1920, 1080, "Trident-Forge");
    m_Engine = std::make_unique<Trident::Application>(*m_Window);

    // Start the engine
    m_Engine->Init();

    // Wire editor panels to the freshly initialised engine and supporting systems.
    m_ContentBrowserPanel.SetEngine(m_Engine.get());
    m_ContentBrowserPanel.SetOnnxRuntime(&m_ONNX);

    // Set up the ImGui layer
    m_ImGuiLayer = std::make_unique<Trident::UI::ImGuiLayer>();
    m_ImGuiLayer->Init(m_Window->GetNativeWindow(), Trident::Application::GetInstance(), Trident::Application::GetPhysicalDevice(), Trident::Application::GetDevice(),
        Trident::Application::GetQueueFamilyIndices().GraphicsFamily.value(), Trident::Application::GetGraphicsQueue(), Trident::Application::GetRenderer().GetRenderPass(),
        Trident::Application::GetRenderer().GetImageCount(), Trident::Application::GetRenderer().GetCommandPool());
    
    Trident::Application::GetRenderer().SetImGuiLayer(m_ImGuiLayer.get());

    // Initialise selection state so panels correctly reflect the lack of focus on startup.
    m_SelectedEntity = s_InvalidEntity;
    m_GizmoOperation = ImGuizmo::TRANSLATE;
    m_GizmoMode = ImGuizmo::LOCAL;

    m_InspectorPanel.SetGizmoState(&m_GizmoOperation, &m_GizmoMode);
}

ApplicationLayer::~ApplicationLayer()
{
    // Gracefully shut down engine and UI
    TR_INFO("-------SHUTTING DOWN APPLICATION-------");

    if (m_ImGuiLayer)
    {
        m_ImGuiLayer->Shutdown();
    }

    if (m_Engine)
    {
        m_Engine->Shutdown();
    }

    TR_INFO("-------APPLICATION SHUTDOWN-------");
}

void ApplicationLayer::Run()
{
    // Main application loop aligning editor windows with Unreal terminology for future docking profiles.
    while (!m_Window->ShouldClose())
    {
        m_Engine->Update();
        m_ImGuiLayer->BeginFrame();

        // Surface layout persistence controls so designers can intentionally snapshot or restore workspace arrangements.
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("Layout"))
            {
                if (ImGui::MenuItem("Save Current Layout"))
                {
                    m_ImGuiLayer->SaveLayoutToDisk();
                }

                if (ImGui::MenuItem("Load Layout From Disk"))
                {
                    if (!m_ImGuiLayer->LoadLayoutFromDisk())
                    {
                        // If loading fails we rebuild the default so the dockspace stays valid.
                        m_ImGuiLayer->ResetLayoutToDefault();
                    }
                }

                if (ImGui::MenuItem("Reset Layout To Default"))
                {
                    m_ImGuiLayer->ResetLayoutToDefault();
                }

                ImGui::EndMenu();
            }

            ImGui::EndMainMenuBar();
        }

        m_ContentBrowserPanel.Render();
        m_SceneHierarchyPanel.SetSelectedEntity(m_SelectedEntity);
        m_SceneHierarchyPanel.Render();
        m_SelectedEntity = m_SceneHierarchyPanel.GetSelectedEntity();

        m_ViewportPanel.SetSelectedEntity(m_SelectedEntity);
        m_ViewportPanel.Render();

        m_InspectorPanel.SetSelectedEntity(m_SelectedEntity);
        m_InspectorPanel.Render();
        DrawOutputLogPanel();

        // Render the gizmo on top of the viewport once all inspector edits are applied.
        DrawTransformGizmo(m_SelectedEntity);

        m_ImGuiLayer->EndFrame();
        m_Engine->RenderScene();
    }
}

void ApplicationLayer::DrawOutputLogPanel()
{
    // The output log combines frame statistics with filtered logging controls for a single dockable surface.
    if (!ImGui::Begin("Output Log"))
    {
        ImGui::End();
        return;
    }

    ImGui::TextUnformatted("Frame Statistics");
    ImGui::Text("FPS: %.2f", Trident::Utilities::Time::GetFPS());
    ImGui::Text("Allocations: %zu", Trident::Application::GetRenderer().GetLastFrameAllocationCount());
    ImGui::Text("Models: %zu", Trident::Application::GetRenderer().GetModelCount());
    ImGui::Text("Triangles: %zu", Trident::Application::GetRenderer().GetTriangleCount());

    const Trident::Renderer::FrameTimingStats& l_PerfStats = Trident::Application::GetRenderer().GetFrameTimingStats();
    const size_t l_PerfCount = Trident::Application::GetRenderer().GetFrameTimingHistoryCount();
    if (l_PerfCount > 0)
    {
        ImGui::Text("Frame Avg: %.3f ms", l_PerfStats.AverageMilliseconds);
        ImGui::Text("Frame Min: %.3f ms", l_PerfStats.MinimumMilliseconds);
        ImGui::Text("Frame Max: %.3f ms", l_PerfStats.MaximumMilliseconds);
        ImGui::Text("Average FPS: %.2f", l_PerfStats.AverageFPS);
    }
    else
    {
        ImGui::TextUnformatted("Collecting frame metrics...");
    }

    bool l_PerformanceCapture = Trident::Application::GetRenderer().IsPerformanceCaptureEnabled();
    if (ImGui::Checkbox("Capture Performance", &l_PerformanceCapture))
    {
        Trident::Application::GetRenderer().SetPerformanceCaptureEnabled(l_PerformanceCapture);
    }

    if (Trident::Application::GetRenderer().IsPerformanceCaptureEnabled())
    {
        ImGui::Text("Captured Samples: %zu", Trident::Application::GetRenderer().GetPerformanceCaptureSampleCount());
    }

    ImGui::Separator();

    std::vector<Trident::Utilities::ConsoleLog::Entry> l_LogEntries = Trident::Utilities::ConsoleLog::GetSnapshot();

    if (ImGui::Button("Clear"))
    {
        Trident::Utilities::ConsoleLog::Clear();
        s_LastConsoleEntryCount = 0;
    }

    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &s_ConsoleAutoScroll);

    ImGui::Separator();

    ImGui::Checkbox("Errors", &s_ShowConsoleErrors);
    ImGui::SameLine();
    ImGui::Checkbox("Warnings", &s_ShowConsoleWarnings);
    ImGui::SameLine();
    ImGui::Checkbox("Logs", &s_ShowConsoleLogs);

    ImGui::Separator();

    ImGui::BeginChild("OutputLogScroll", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_HorizontalScrollbar);

    for (const Trident::Utilities::ConsoleLog::Entry& it_Entry : l_LogEntries)
    {
        if (!ShouldDisplayConsoleEntry(it_Entry.Level))
        {
            continue;
        }

        const std::string l_Timestamp = FormatConsoleTimestamp(it_Entry.Timestamp);
        const ImVec4 l_Colour = GetConsoleColour(it_Entry.Level);

        ImGui::PushStyleColor(ImGuiCol_Text, l_Colour);
        ImGui::Text("[%s] %s", l_Timestamp.c_str(), it_Entry.Message.c_str());
        ImGui::PopStyleColor();
    }

    if (s_ConsoleAutoScroll && !l_LogEntries.empty() && l_LogEntries.size() != s_LastConsoleEntryCount)
    {
        ImGui::SetScrollHereY(1.0f);
    }

    ImGui::EndChild();

    s_LastConsoleEntryCount = l_LogEntries.size();

    // Future addition: surface shader compiler errors and asset validation summaries alongside runtime logs.

    ImGui::End();
}

void ApplicationLayer::DrawTransformGizmo(Trident::ECS::Entity a_SelectedEntity)
{
    // Even when no entity is bound we start a frame so ImGuizmo can clear any persistent state.
    ImGuizmo::BeginFrame();

    if (a_SelectedEntity == s_InvalidEntity)
    {
        return;
    }

    Trident::ECS::Registry& l_Registry = Trident::Application::GetRegistry();
    if (!l_Registry.HasComponent<Trident::Transform>(a_SelectedEntity))
    {
        return;
    }

    // Fetch the viewport rectangle published by the renderer so the gizmo aligns with the active scene view.

    const Trident::ViewportInfo l_ViewportInfo = Trident::Application::GetRenderer().GetViewport();
    ImVec2 l_RectPosition{};
    ImVec2 l_RectSize{};

    if (l_ViewportInfo.Size.x > 0.0f && l_ViewportInfo.Size.y > 0.0f)
    {
        // Use the viewport data directly so the gizmo tracks the rendered scene even with multi-viewport enabled.
        l_RectPosition = ImVec2{ l_ViewportInfo.Position.x, l_ViewportInfo.Position.y };
        l_RectSize = ImVec2{ l_ViewportInfo.Size.x, l_ViewportInfo.Size.y };
    }
    else
    {
        // Fall back to the main viewport bounds when the renderer has not published explicit viewport information yet.
        const ImGuiViewport* l_MainViewport = ImGui::GetMainViewport();
        l_RectPosition = l_MainViewport->Pos;
        l_RectSize = l_MainViewport->Size;
    }

    // Mirror the Scene panel camera selection logic so the gizmo uses whichever camera the user targeted.
    glm::mat4 l_ViewMatrix{ 1.0f };
    glm::mat4 l_ProjectionMatrix{ 1.0f };
    bool l_UseOrthographicGizmo = false;

    const Trident::ECS::Entity l_SelectedViewportCamera = m_ViewportPanel.GetSelectedCamera();

    if (l_SelectedViewportCamera != s_InvalidEntity
        && l_Registry.HasComponent<Trident::CameraComponent>(l_SelectedViewportCamera)
        && l_Registry.HasComponent<Trident::Transform>(l_SelectedViewportCamera))
    {
        // A scene camera is actively selected; derive view/projection parameters from the ECS components.
        const Trident::CameraComponent& l_CameraComponent = l_Registry.GetComponent<Trident::CameraComponent>(l_SelectedViewportCamera);
        const Trident::Transform& l_CameraTransform = l_Registry.GetComponent<Trident::Transform>(l_SelectedViewportCamera);

        const glm::mat4 l_ModelMatrix = ComposeTransform(l_CameraTransform);
        l_ViewMatrix = glm::inverse(l_ModelMatrix);
        const float l_AspectRatio = l_RectSize.y > 0.0f ? l_RectSize.x / l_RectSize.y : 1.0f;
        l_ProjectionMatrix = BuildCameraProjectionMatrix(l_CameraComponent, l_AspectRatio);
        l_UseOrthographicGizmo = !l_CameraComponent.UseCustomProjection && l_CameraComponent.Projection == Trident::ProjectionType::Orthographic;
    }
    else
    {
        // Fall back to the editor camera when no ECS-driven viewport camera is active.
        Trident::Camera& l_Camera = Trident::Application::GetRenderer().GetCamera();
        l_ViewMatrix = l_Camera.GetViewMatrix();
        const float l_AspectRatio = l_RectSize.y > 0.0f ? l_RectSize.x / l_RectSize.y : 1.0f;
        l_ProjectionMatrix = glm::perspective(glm::radians(l_Camera.GetFOV()), l_AspectRatio, l_Camera.GetNearClip(), l_Camera.GetFarClip());
        l_ProjectionMatrix[1][1] *= -1.0f;
        l_UseOrthographicGizmo = false;
    }

    Trident::Transform& l_EntityTransform = l_Registry.GetComponent<Trident::Transform>(a_SelectedEntity);
    glm::mat4 l_ModelMatrix = ComposeTransform(l_EntityTransform);

    ImGuizmo::SetOrthographic(l_UseOrthographicGizmo);
    ImGuizmo::SetDrawlist();
    ImGuizmo::SetRect(l_RectPosition.x, l_RectPosition.y, l_RectSize.x, l_RectSize.y);

    if (ImGuizmo::Manipulate(glm::value_ptr(l_ViewMatrix), glm::value_ptr(l_ProjectionMatrix), m_GizmoOperation, m_GizmoMode, glm::value_ptr(l_ModelMatrix)))
    {
        // Sync the manipulated matrix back into the ECS so gameplay systems stay authoritative.
        Trident::Transform l_UpdatedTransform = DecomposeTransform(l_ModelMatrix, l_EntityTransform);
        l_EntityTransform = l_UpdatedTransform;
        Trident::Application::GetRenderer().SetTransform(l_EntityTransform);
    }

    // Potential enhancement: expose snapping increments for translation/rotation/scale so artists can toggle grid alignment.
}