#pragma once

#include "Application.h"

#include "Core/Utilities.h"
#include "Window/Window.h"
#include "Renderer/Renderer.h"
#include "UI/ImGuiLayer.h"
#include "AI/ONNXRuntime.h"

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <memory>

class ApplicationLayer
{
public:
    ApplicationLayer();
    ~ApplicationLayer();

    void Run();

private:
    void DrawViewportPanel();
    void DrawWorldOutlinerPanel();
    void DrawDetailsPanel();
    void DrawContentBrowserPanel();
    void DrawOutputLogPanel();
    void DrawTransformGizmo(Trident::ECS::Entity selectedEntity);

private:
    std::unique_ptr<Trident::Window> m_Window; // OS window wrapper
    std::unique_ptr<Trident::Application> m_Engine; // Core engine instance
    std::unique_ptr<Trident::UI::ImGuiLayer> m_ImGuiLayer; // ImGui interface layer

    Trident::AI::ONNXRuntime m_ONNX; // Runtime for ONNX models
};