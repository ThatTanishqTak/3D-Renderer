#pragma once

#include "Application.h"

#include "Core/Utilities.h"
#include "Window/Window.h"
#include "Renderer/Renderer.h"

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
    std::unique_ptr<Trident::Window> m_Window;
    std::unique_ptr<Trident::Application> m_Engine;

    Trident::CubeProperties m_CubeProps{};
    Trident::ViewportInfo m_Viewport{};

private:
    void RenderUI();
};