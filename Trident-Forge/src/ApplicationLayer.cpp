#include "ApplicationLayer.h"

#include <imgui.h>

#include <string>

#include "UI/FileDialog.h"
#include "Loader/ModelLoader.h"
#include "Loader/TextureLoader.h"
#include "Renderer/RenderCommand.h"

ApplicationLayer::ApplicationLayer()
{
    // Initialize logging and the Forge window
    Trident::Utilities::Log::Init();

    m_Window = std::make_unique<Trident::Window>(1920, 1080, "Trident-Forge");
    m_Engine = std::make_unique<Trident::Application>(*m_Window);

    // Start the engine
    m_Engine->Init();

    // Set up the ImGui layer
    m_ImGuiLayer = std::make_unique<Trident::UI::ImGuiLayer>();
    m_ImGuiLayer->Init(m_Window->GetNativeWindow(), Trident::Application::GetInstance(), Trident::Application::GetPhysicalDevice(), Trident::Application::GetDevice(),
        Trident::Application::GetQueueFamilyIndices().GraphicsFamily.value(), Trident::Application::GetGraphicsQueue(), Trident::Application::GetRenderer().GetRenderPass(),
        Trident::Application::GetRenderer().GetImageCount(), Trident::Application::GetRenderer().GetCommandPool());
    
    Trident::Application::GetRenderer().SetImGuiLayer(m_ImGuiLayer.get());
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
    static std::string l_ModelPath{};
    static std::string l_TexturePath{};
    static std::string l_OnnxPath{};
    static bool l_OpenModelDialog = false;
    static bool l_OpenTextureDialog = false;
    static bool l_OpenOnnxDialog = false;
    static bool l_OnnxLoaded = false;

    // Main application loop
    while (!m_Window->ShouldClose())
    {
        // Update the engine and render the UI
        m_Engine->Update();
        m_ImGuiLayer->BeginFrame();
        
        ImGui::Begin("Stats");
        ImGui::Text("FPS: %.2f", Trident::Utilities::Time::GetFPS());
        ImGui::Text("Allocations: %zu", Trident::Application::GetRenderer().GetLastFrameAllocationCount());
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
            if (Trident::UI::FileDialog::Open("ModelDialog", l_ModelPath, ".obj"))
            {
                auto l_Meshes = Trident::Loader::ModelLoader::Load(l_ModelPath);
                Trident::Application::GetRenderer().UploadMesh(l_Meshes);
                
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

        ImGui::Text("ONNX: %s", l_OnnxPath.c_str());
        if (ImGui::Button("Load ONNX"))
        {
            l_OpenOnnxDialog = true;
            ImGui::OpenPopup("ONNXDialog");
        }

        if (l_OpenOnnxDialog)
        {
            if (Trident::UI::FileDialog::Open("ONNXDialog", l_OnnxPath, ".onnx"))
            {
                l_OnnxLoaded = m_ONNX.LoadModel(l_OnnxPath);
                l_OpenOnnxDialog = false;
            }

            if (!ImGui::IsPopupOpen("ONNXDialog"))
            {
                l_OpenOnnxDialog = false;
            }
        }

        if (l_OnnxLoaded && ImGui::Button("Run Inference"))
        {
            std::vector<float> l_Input{ 0.0f };
            std::vector<int64_t> l_Shape{ 1 };
            auto l_Output = m_ONNX.Run(l_Input, l_Shape);
            if (!l_Output.empty())
            {
                TR_INFO("Inference result: {}", l_Output[0]);
            }
        }

        ImGui::End();

        m_ImGuiLayer->EndFrame();

        m_Engine->RenderScene();
    }
}