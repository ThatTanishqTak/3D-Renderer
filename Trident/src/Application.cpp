#include "Application.h"

#include "Renderer/RenderCommand.h"

#include "Loader/SceneLoader.h"
#include "Loader/ModelLoader.h"

#include "ECS/Scene.h"
#include "ECS/Components/MeshComponent.h"
#include "ECS/Components/TransformComponent.h"

#include <string>
#include <limits>
#include <utility>
#include <cstdint>
#include <filesystem>

namespace Trident
{
    Application::Application()
    {
        Trident::Utilities::Log::Init();

        Inititialize();
    }

    void Application::Inititialize()
    {
        m_Specifications.Width = 1920;
        m_Specifications.Height = 1080;
        m_Specifications.Title = "Trident-Forge";

        m_Window = std::make_unique<Window>(m_Specifications);
        m_Startup = std::make_unique<Startup>(*m_Window);

        RenderCommand::Init();
    }

    void Application::Run()
    {
        while (!m_Window->ShouldClose())
        {
            Update();

            Render();
        }
    }

    void Application::Update()
    {
        Utilities::Time::Update();

        m_Window->PollEvents();
    }

    void Application::Render()
    {
        RenderCommand::DrawFrame();
    }

    void Application::Shutdown()
    {
        RenderCommand::Shutdown();
    }
}