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
    // Dedicated sentinel used when no entity is highlighted inside the inspector.
    constexpr Trident::ECS::Entity s_InvalidEntity = std::numeric_limits<Trident::ECS::Entity>::max();
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

    // Allow the dedicated ImGuizmo layer to expose its state to UI controls.
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

        // Sync the viewport's selection state so downstream panels (e.g., inspector) respect viewport-driven deselection.

        m_InspectorPanel.SetSelectedEntity(m_SelectedEntity);
        m_InspectorPanel.Render();

        m_OutputPanel.Render();

        m_ImGuiLayer->EndFrame();
        m_Engine->RenderScene();
    }
}