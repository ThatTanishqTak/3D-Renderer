#include "ApplicationLayer.h"

#include <imgui.h>

#include <string>
#include <vector>

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
        ImGui::Text("Models: %zu", Trident::Application::GetRenderer().GetModelCount());
        ImGui::Text("Triangles: %zu", Trident::Application::GetRenderer().GetTriangleCount());
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
            if (Trident::UI::FileDialog::Open("ModelDialog", l_ModelPath, ".fbx"))
            {
                auto a_ModelData = Trident::Loader::ModelLoader::Load(l_ModelPath);
                Trident::Application::GetRenderer().UploadMesh(a_ModelData.Meshes, a_ModelData.Materials);
                
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

        static std::string l_ScenePath{};
        static bool l_OpenSceneDialog = false;

        ImGui::Text("Scene: %s", l_ScenePath.c_str());
        if (ImGui::Button("Load Scene"))
        {
            l_OpenSceneDialog = true;
            ImGui::OpenPopup("SceneDialog");
        }

        if (l_OpenSceneDialog)
        {
            if (Trident::UI::FileDialog::Open("SceneDialog", l_ScenePath, ".scene"))
            {
                m_Engine->LoadScene(l_ScenePath);
                l_OpenSceneDialog = false;
            }

            if (!ImGui::IsPopupOpen("SceneDialog"))
            {
                l_OpenSceneDialog = false;
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

        // Provide visibility into the background hot-reload system so developers can diagnose issues quickly.
        ImGui::Begin("Live Reload");
        Trident::Utilities::FileWatcher& l_Watcher = Trident::Utilities::FileWatcher::Get();
        bool l_AutoReload = l_Watcher.IsAutoReloadEnabled();
        if (ImGui::Checkbox("Automatic Reload", &l_AutoReload))
        {
            l_Watcher.EnableAutoReload(l_AutoReload);
        }

        ImGui::Separator();

        const auto a_StatusToString = [](Trident::Utilities::FileWatcher::ReloadStatus a_Status) -> const char*
            {
                switch (a_Status)
                {
                case Trident::Utilities::FileWatcher::ReloadStatus::Detected: return "Detected";
                case Trident::Utilities::FileWatcher::ReloadStatus::Queued: return "Queued";
                case Trident::Utilities::FileWatcher::ReloadStatus::Success: return "Success";
                case Trident::Utilities::FileWatcher::ReloadStatus::Failed: return "Failed";
                default: return "Unknown";
                }
            };

        const auto a_TypeToString = [](Trident::Utilities::FileWatcher::WatchType a_Type) -> const char*
            {
                switch (a_Type)
                {
                case Trident::Utilities::FileWatcher::WatchType::Shader: return "Shader";
                case Trident::Utilities::FileWatcher::WatchType::Model: return "Model";
                case Trident::Utilities::FileWatcher::WatchType::Texture: return "Texture";
                default: return "Unknown";
                }
            };

        const std::vector<Trident::Utilities::FileWatcher::ReloadEvent>& l_Events = l_Watcher.GetEvents();
        if (ImGui::BeginTable("ReloadEvents", 5, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter))
        {
            ImGui::TableSetupColumn("Type");
            ImGui::TableSetupColumn("Status");
            ImGui::TableSetupColumn("File");
            ImGui::TableSetupColumn("Details");
            ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableHeadersRow();

            for (const Trident::Utilities::FileWatcher::ReloadEvent& it_Event : l_Events)
            {
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(a_TypeToString(it_Event.Type));

                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(a_StatusToString(it_Event.Status));

                ImGui::TableSetColumnIndex(2);
                ImGui::TextWrapped("%s", it_Event.Path.c_str());

                ImGui::TableSetColumnIndex(3);
                if (it_Event.Message.empty())
                {
                    ImGui::TextUnformatted("Awaiting result...");
                }
                else
                {
                    ImGui::TextWrapped("%s", it_Event.Message.c_str());
                }

                ImGui::TableSetColumnIndex(4);
                bool l_Disabled = it_Event.Status == Trident::Utilities::FileWatcher::ReloadStatus::Queued;
                ImGui::BeginDisabled(l_Disabled);
                ImGui::PushID(static_cast<int>(it_Event.Id));
                const char* l_Label = it_Event.Status == Trident::Utilities::FileWatcher::ReloadStatus::Failed ? "Retry" : "Queue";
                if (ImGui::Button(l_Label))
                {
                    l_Watcher.QueueEvent(it_Event.Id);
                }
                ImGui::PopID();
                ImGui::EndDisabled();
            }

            ImGui::EndTable();
        }
        else
        {
            ImGui::TextUnformatted("No reload events captured yet.");
        }

        ImGui::End();

        m_ImGuiLayer->EndFrame();

        m_Engine->RenderScene();
    }
}