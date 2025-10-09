#include "ContentBrowserPanel.h"

#include <imgui.h>

#include <cstdint>
#include <vector>

#include "Application.h"
#include "Core/Utilities.h"
#include "Loader/ModelLoader.h"
#include "Loader/TextureLoader.h"
#include "AI/ONNXRuntime.h"
#include "Renderer/Renderer.h"
#include "UI/FileDialog.h"

namespace UI
{
    ContentBrowserPanel::ContentBrowserPanel(): m_Engine(nullptr), m_OnnxRuntime(nullptr), m_ModelPath(), m_TexturePath(), m_ScenePath(), m_OnnxPath(), m_OpenModelDialog(false),
        m_OpenTextureDialog(false), m_OpenSceneDialog(false), m_OpenOnnxDialog(false), m_OnnxLoaded(false)
    {
        // Constructor prepares default state so the panel can lazily bind dependencies.
    }

    ContentBrowserPanel::~ContentBrowserPanel()
    {
        // Destructor intentionally left blank; panel does not own external resources.
    }

    void ContentBrowserPanel::SetEngine(Trident::Application* a_Engine)
    {
        // Cache the engine pointer so we can trigger scene loads from the browser.
        m_Engine = a_Engine;
    }

    void ContentBrowserPanel::SetOnnxRuntime(Trident::AI::ONNXRuntime* a_OnnxRuntime)
    {
        // Store the ONNX runtime so inference controls are aware of availability.
        m_OnnxRuntime = a_OnnxRuntime;
    }

    void ContentBrowserPanel::Render()
    {
        // The content browser groups asset import controls into logical sections for clarity.
        if (!ImGui::Begin("Content Browser"))
        {
            ImGui::End();
            return;
        }

        DrawModelSection();
        ImGui::Separator();

        DrawTextureSection();
        ImGui::Separator();

        DrawSceneSection();
        ImGui::Separator();

        DrawOnnxSection();

        // TODO: Integrate a directory tree with thumbnails to more closely mirror AAA editors.

        ImGui::End();
    }

    void ContentBrowserPanel::DrawModelSection()
    {
        // Provide an entry point for artists to load geometry assets.
        ImGui::Text("Model: %s", m_ModelPath.c_str());
        if (ImGui::Button("Load Model"))
        {
            m_OpenModelDialog = true;
            ImGui::OpenPopup("ModelDialog");
        }

        if (!m_OpenModelDialog)
        {
            return;
        }

        if (Trident::UI::FileDialog::Open("ModelDialog", m_ModelPath, ".fbx"))
        {
            auto a_ModelData = Trident::Loader::ModelLoader::Load(m_ModelPath);
            Trident::Application::GetRenderer().UploadMesh(a_ModelData.Meshes, a_ModelData.Materials);
            m_OpenModelDialog = false;
        }

        if (!ImGui::IsPopupOpen("ModelDialog"))
        {
            m_OpenModelDialog = false;
        }
    }

    void ContentBrowserPanel::DrawTextureSection()
    {
        // Allow users to stage textures for material authoring.
        ImGui::Text("Texture: %s", m_TexturePath.c_str());
        if (ImGui::Button("Load Texture"))
        {
            m_OpenTextureDialog = true;
            ImGui::OpenPopup("TextureDialog");
        }

        if (!m_OpenTextureDialog)
        {
            return;
        }

        if (Trident::UI::FileDialog::Open("TextureDialog", m_TexturePath))
        {
            auto a_Texture = Trident::Loader::TextureLoader::Load(m_TexturePath);
            Trident::Application::GetRenderer().UploadTexture(a_Texture);
            m_OpenTextureDialog = false;
        }

        if (!ImGui::IsPopupOpen("TextureDialog"))
        {
            m_OpenTextureDialog = false;
        }
    }

    void ContentBrowserPanel::DrawSceneSection()
    {
        // Gate scene loading through the engine pointer so we avoid dereferencing null.
        const bool l_HasEngine = m_Engine != nullptr;

        ImGui::BeginDisabled(!l_HasEngine);
        ImGui::Text("Scene: %s", m_ScenePath.c_str());
        if (ImGui::Button("Load Scene"))
        {
            m_OpenSceneDialog = true;
            ImGui::OpenPopup("SceneDialog");
        }
        ImGui::EndDisabled();

        if (m_OpenSceneDialog)
        {
            if (Trident::UI::FileDialog::Open("SceneDialog", m_ScenePath, ".scene"))
            {
                if (m_Engine != nullptr)
                {
                    m_Engine->LoadScene(m_ScenePath);
                }
                m_OpenSceneDialog = false;
            }

            if (!ImGui::IsPopupOpen("SceneDialog"))
            {
                m_OpenSceneDialog = false;
            }
        }

        if (!l_HasEngine)
        {
            ImGui::TextUnformatted("Scene loading is disabled until the engine is initialised.");
        }
    }

    void ContentBrowserPanel::DrawOnnxSection()
    {
        // Present AI tooling while ensuring controls remain safe when runtime support is absent.
        const bool l_HasRuntime = m_OnnxRuntime != nullptr;

        ImGui::BeginDisabled(!l_HasRuntime);
        ImGui::Text("ONNX: %s", m_OnnxPath.c_str());
        if (ImGui::Button("Load ONNX"))
        {
            m_OpenOnnxDialog = true;
            ImGui::OpenPopup("ONNXDialog");
        }
        ImGui::EndDisabled();

        if (m_OpenOnnxDialog)
        {
            if (Trident::UI::FileDialog::Open("ONNXDialog", m_OnnxPath, ".onnx"))
            {
                if (m_OnnxRuntime != nullptr)
                {
                    m_OnnxLoaded = m_OnnxRuntime->LoadModel(m_OnnxPath);
                }
                m_OpenOnnxDialog = false;
            }

            if (!ImGui::IsPopupOpen("ONNXDialog"))
            {
                m_OpenOnnxDialog = false;
            }
        }
        else if (!l_HasRuntime)
        {
            ImGui::TextUnformatted("ONNX runtime is unavailable; plug-in support will enable this later.");
        }

        if (!l_HasRuntime)
        {
            return;
        }

        if (m_OnnxLoaded)
        {
            if (ImGui::Button("Run Inference"))
            {
                std::vector<float> l_Input{ 0.0f };
                std::vector<int64_t> l_Shape{ 1 };
                auto a_Output = m_OnnxRuntime->Run(l_Input, l_Shape);
                if (!a_Output.empty())
                {
                    TR_INFO("Inference result: {}", a_Output[0]);
                }
            }
        }
        else
        {
            ImGui::TextUnformatted("Load an ONNX model to enable inference testing.");
        }
    }
}