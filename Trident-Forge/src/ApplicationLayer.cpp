#include "ApplicationLayer.h"

#include <imgui.h>

#include <string>

#include "UI/FileDialog.h"
#include "Loader/ModelLoader.h"
#include "Loader/TextureLoader.h"

ApplicationLayer::ApplicationLayer()
{
    Trident::Utilities::Log::Init();

    m_Window = std::make_unique<Trident::Window>(1920, 1080, "Trident-Forge");
    m_Engine = std::make_unique<Trident::Application>(*m_Window);

    m_Engine->Init();

    m_ImGuiLayer = std::make_unique<Trident::UI::ImGuiLayer>();
    m_ImGuiLayer->Init(m_Window->GetNativeWindow(), Trident::Application::GetInstance(), Trident::Application::GetPhysicalDevice(), Trident::Application::GetDevice(),
        Trident::Application::GetQueueFamilyIndices().GraphicsFamily.value(), Trident::Application::GetGraphicsQueue(), Trident::Application::GetRenderer().GetRenderPass(),
        Trident::Application::GetRenderer().GetImageCount(), Trident::Application::GetRenderer().GetCommandPool());
    
    Trident::Application::GetRenderer().SetImGuiLayer(m_ImGuiLayer.get());
}

ApplicationLayer::~ApplicationLayer()
{
    TR_INFO("-------SHUTING DOWN APPLICATION-------");

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
    static std::string l_ModelPath;
    static std::string l_TexturePath;
    static bool l_OpenModelDialog = false;
    static bool l_OpenTextureDialog = false;

    while (!m_Window->ShouldClose())
    {
        m_Engine->Update();

        m_ImGuiLayer->BeginFrame();
        
        ImGui::Begin("Stats");
        ImGui::Text("FPS: %.2f", Trident::Utilities::Time::GetFPS());
        ImGui::End();

        ImGui::Begin("Content");
        ImGui::Text("Model: %s", l_ModelPath.c_str());
        if (ImGui::Button("Load Model"))
        {
            l_OpenModelDialog = true;
            ImGui::OpenPopup("ModelDialog");
        }

        if (l_OpenModelDialog)
        {
            if (Trident::UI::FileDialog::Open("ModelDialog", l_ModelPath))
            {
                auto l_Mesh = Trident::Loader::ModelLoader::Load(l_ModelPath);
                Trident::Application::GetRenderer().UploadMesh(l_Mesh);
                l_OpenModelDialog = false;
            }

            if (!ImGui::IsPopupOpen("ModelDialog"))
            {
                l_OpenModelDialog = false;
            }
        }

        ImGui::Text("Texture: %s", l_TexturePath.c_str());
        if (ImGui::Button("Load Texture"))
        {
            l_OpenTextureDialog = true;
            ImGui::OpenPopup("TextureDialog");
        }

        if (l_OpenTextureDialog)
        {
            if (Trident::UI::FileDialog::Open("TextureDialog", l_TexturePath))
            {
                auto a_Texture = Trident::Loader::TextureLoader::Load(l_TexturePath);
                Trident::Application::GetRenderer().UploadTexture(a_Texture);

                l_OpenTextureDialog = false;
            }

            if (!ImGui::IsPopupOpen("TextureDialog"))
            {
                l_OpenTextureDialog = false;
            }
        }
        ImGui::End();

        m_ImGuiLayer->EndFrame();

        m_Engine->RenderScene();
    }
}