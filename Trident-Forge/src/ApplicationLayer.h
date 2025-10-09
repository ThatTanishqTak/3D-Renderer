#pragma once

#include "Application.h"

#include "Core/Utilities.h"
#include "Window/Window.h"
#include "Renderer/Renderer.h"
#include "UI/ImGuiLayer.h"
#include "AI/ONNXRuntime.h"

#include "Panels/ContentBrowserPanel.h"
#include "Panels/ViewportPanel.h"
#include "Panels/SceneHierarchyPanel.h"
#include "Panels/InspectorPanel.h"

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <ImGuizmo.h>

#include <memory>

class ApplicationLayer
{
public:
    ApplicationLayer();
    ~ApplicationLayer();

    void Run();

private:
    void DrawOutputLogPanel();
    void DrawTransformGizmo(Trident::ECS::Entity selectedEntity);

private:
    std::unique_ptr<Trident::Window> m_Window; // OS window wrapper
    std::unique_ptr<Trident::Application> m_Engine; // Core engine instance
    std::unique_ptr<Trident::UI::ImGuiLayer> m_ImGuiLayer; // ImGui interface layer
    
    UI::ViewportPanel m_ViewportPanel; // Scene viewport UI wrapper
    UI::ContentBrowserPanel m_ContentBrowserPanel; // Content browser panel
    UI::SceneHierarchyPanel m_SceneHierarchyPanel; // Scene hierarchy browser
    UI::InspectorPanel m_InspectorPanel; // Inspector for entity components

    Trident::ECS::Entity m_SelectedEntity; // Currently highlighted entity in the editor
    ImGuizmo::OPERATION m_GizmoOperation; // Current gizmo operation mode
    ImGuizmo::MODE m_GizmoMode; // Current gizmo orientation mode

    Trident::AI::ONNXRuntime m_ONNX; // Runtime for ONNX models
};