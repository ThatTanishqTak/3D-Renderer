#pragma once

#include "Application.h"

#include "Core/Utilities.h"
#include "Window/Window.h"

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

private:
    void RenderUI();
};