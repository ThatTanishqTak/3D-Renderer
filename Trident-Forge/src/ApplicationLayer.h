//----------------------------------------------------------------------------------//
// ApplicationLayer.h
// High level layer managing the Forge window, rendering engine and ImGui interface.
// m_Window     - platform window wrapped by Trident.
// m_Engine     - main Trident application instance.
// m_ImGuiLayer - user interface layer built with ImGui.
//----------------------------------------------------------------------------------//

#pragma once

#include "Application.h"

#include "Core/Utilities.h"
#include "Window/Window.h"
#include "Renderer/Renderer.h"
#include "UI/ImGuiLayer.h"

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
    std::unique_ptr<Trident::Window> m_Window; // OS window wrapper
    std::unique_ptr<Trident::Application> m_Engine; // Core engine instance
    std::unique_ptr<Trident::UI::ImGuiLayer> m_ImGuiLayer; // ImGui interface layer
};