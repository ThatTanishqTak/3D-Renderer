#include "ApplicationLayer.h"

#include <imgui.h>
#include <ImGuizmo.h>

#include <string>
#include <vector>
#include <limits>

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include "UI/FileDialog.h"
#include "Loader/ModelLoader.h"
#include "Loader/TextureLoader.h"
#include "Renderer/RenderCommand.h"
#include "Renderer/Renderer.h"

namespace
{
    // Compose a transform matrix from the ECS component for ImGuizmo consumption.
    glm::mat4 ComposeTransform(const Trident::Transform& a_Transform)
    {
        glm::mat4 l_ModelMatrix{ 1.0f };
        l_ModelMatrix = glm::translate(l_ModelMatrix, a_Transform.Position);
        l_ModelMatrix = glm::rotate(l_ModelMatrix, glm::radians(a_Transform.Rotation.x), glm::vec3{ 1.0f, 0.0f, 0.0f });
        l_ModelMatrix = glm::rotate(l_ModelMatrix, glm::radians(a_Transform.Rotation.y), glm::vec3{ 0.0f, 1.0f, 0.0f });
        l_ModelMatrix = glm::rotate(l_ModelMatrix, glm::radians(a_Transform.Rotation.z), glm::vec3{ 0.0f, 0.0f, 1.0f });
        l_ModelMatrix = glm::scale(l_ModelMatrix, a_Transform.Scale);

        return l_ModelMatrix;
    }

    // Convert an ImGuizmo-manipulated matrix back to the engine's transform component.
    Trident::Transform DecomposeTransform(const glm::mat4& a_ModelMatrix, const Trident::Transform& a_Default)
    {
        glm::vec3 l_Scale{};
        glm::quat l_Rotation{};
        glm::vec3 l_Translation{};
        glm::vec3 l_Skew{};
        glm::vec4 l_Perspective{};

        if (!glm::decompose(a_ModelMatrix, l_Scale, l_Rotation, l_Translation, l_Skew, l_Perspective))
        {
            // Preserve the previous values if decomposition fails, avoiding sudden jumps.
            return a_Default;
        }

        Trident::Transform l_Result = a_Default;
        l_Result.Position = l_Translation;
        l_Result.Scale = l_Scale;
        l_Result.Rotation = glm::degrees(glm::eulerAngles(glm::normalize(l_Rotation)));
        return l_Result;
    }

    // Editor-wide gizmo state stored at TU scope so every panel references the same configuration.
    ImGuizmo::OPERATION s_GizmoOperation = ImGuizmo::TRANSLATE;
    ImGuizmo::MODE s_GizmoMode = ImGuizmo::LOCAL;

    // Dedicated sentinel used when no entity is highlighted inside the inspector.
    constexpr Trident::ECS::Entity s_InvalidEntity = std::numeric_limits<Trident::ECS::Entity>::max();

    // Persistently tracked selection so the gizmo can render even when the inspector panel is closed.
    Trident::ECS::Entity s_SelectedEntity = s_InvalidEntity;
}

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
        
        // Surface GPU/CPU timing information gathered inside the renderer so tools users can monitor stability.
        const Trident::Renderer::FrameTimingStats& l_PerfStats = Trident::Application::GetRenderer().GetFrameTimingStats();
        const size_t l_PerfCount = Trident::Application::GetRenderer().GetFrameTimingHistoryCount();
        if (l_PerfCount > 0)
        {
            ImGui::Separator();
            ImGui::Text("Frame Avg: %.3f ms", l_PerfStats.AverageMilliseconds);
            ImGui::Text("Frame Min: %.3f ms", l_PerfStats.MinimumMilliseconds);
            ImGui::Text("Frame Max: %.3f ms", l_PerfStats.MaximumMilliseconds);
            ImGui::Text("Average FPS: %.2f", l_PerfStats.AverageFPS);
        }
        else
        {
            ImGui::Separator();
            ImGui::TextUnformatted("Collecting frame metrics...");
        }

        bool l_PerformanceCapture = Trident::Application::GetRenderer().IsPerformanceCaptureEnabled();
        if (ImGui::Checkbox("Capture Performance", &l_PerformanceCapture))
        {
            Trident::Application::GetRenderer().SetPerformanceCaptureEnabled(l_PerformanceCapture);
        }

        if (Trident::Application::GetRenderer().IsPerformanceCaptureEnabled())
        {
            ImGui::Text("Captured Samples: %zu", Trident::Application::GetRenderer().GetPerformanceCaptureSampleCount());
        }

        ImGui::End();

        // Camera control panel allows artists to align shots without diving into engine code.
        if (ImGui::Begin("Camera"))
        {
            Trident::Camera& l_Camera = Trident::Application::GetRenderer().GetCamera();

            glm::vec3 l_CameraPosition = l_Camera.GetPosition();
            if (ImGui::DragFloat3("Position", glm::value_ptr(l_CameraPosition), 0.1f))
            {
                l_Camera.SetPosition(l_CameraPosition);
            }

            float l_YawDegrees = l_Camera.GetYaw();
            if (ImGui::DragFloat("Yaw", &l_YawDegrees, 0.1f, -360.0f, 360.0f))
            {
                l_Camera.SetYaw(l_YawDegrees);
            }

            float l_PitchDegrees = l_Camera.GetPitch();
            if (ImGui::DragFloat("Pitch", &l_PitchDegrees, 0.1f, -89.0f, 89.0f))
            {
                l_Camera.SetPitch(l_PitchDegrees);
            }

            float l_FieldOfView = l_Camera.GetFOV();
            if (ImGui::SliderFloat("Field of View", &l_FieldOfView, 1.0f, 120.0f))
            {
                l_Camera.SetFOV(l_FieldOfView);
            }

            float l_FarClipValue = l_Camera.GetFarClip();
            float l_NearClipValue = l_Camera.GetNearClip();
            if (ImGui::DragFloat("Near Clip", &l_NearClipValue, 0.01f, 0.001f, l_FarClipValue - 0.01f))
            {
                l_Camera.SetNearClip(l_NearClipValue);
                l_FarClipValue = l_Camera.GetFarClip();
            }

            float l_MinFarClip = l_Camera.GetNearClip() + 0.01f;
            if (ImGui::DragFloat("Far Clip", &l_FarClipValue, 1.0f, l_MinFarClip, 20000.0f))
            {
                l_Camera.SetFarClip(l_FarClipValue);
            }
        }
        ImGui::End();

        // Material panel exposes glTF shading terms for lightweight look-dev.
        if (ImGui::Begin("Materials"))
        {
            std::vector<Trident::Geometry::Material>& l_Materials = Trident::Application::GetRenderer().GetMaterials();
            if (l_Materials.empty())
            {
                ImGui::TextUnformatted("No materials loaded.");
            }
            else
            {
                for (size_t it_Index = 0; it_Index < l_Materials.size(); ++it_Index)
                {
                    Trident::Geometry::Material& l_Material = l_Materials[it_Index];
                    ImGui::PushID(static_cast<int>(it_Index));

                    ImGui::Text("Material %zu", it_Index);
                    glm::vec4 l_BaseColor = l_Material.BaseColorFactor;
                    if (ImGui::ColorEdit4("Albedo", glm::value_ptr(l_BaseColor)))
                    {
                        l_Material.BaseColorFactor = l_BaseColor;
                    }

                    float l_Roughness = l_Material.RoughnessFactor;
                    if (ImGui::SliderFloat("Roughness", &l_Roughness, 0.0f, 1.0f))
                    {
                        l_Material.RoughnessFactor = l_Roughness;
                    }

                    float l_Metallic = l_Material.MetallicFactor;
                    if (ImGui::SliderFloat("Metallic", &l_Metallic, 0.0f, 1.0f))
                    {
                        l_Material.MetallicFactor = l_Metallic;
                    }

                    ImGui::PopID();

                    if (it_Index + 1 < l_Materials.size())
                    {
                        ImGui::Separator();
                    }
                }
            }
        }
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

        // Scene inspector offers basic ECS debugging hooks for the currently loaded scene.
        if (ImGui::Begin("Scene Inspector"))
        {
            Trident::ECS::Registry& l_Registry = Trident::Application::GetRegistry();
            const std::vector<Trident::ECS::Entity>& l_Entities = l_Registry.GetEntities();

            if (l_Entities.empty())
            {
                ImGui::TextUnformatted("No entities in the active scene.");
                s_SelectedEntity = s_InvalidEntity;
            }
            else
            {
                ImGui::Text("Entities (%zu)", l_Entities.size());
                if (ImGui::BeginListBox("##EntityList"))
                {
                    for (Trident::ECS::Entity it_Entity : l_Entities)
                    {
                        bool l_IsSelected = it_Entity == s_SelectedEntity;
                        std::string l_Label = "Entity " + std::to_string(static_cast<unsigned int>(it_Entity));
                        if (ImGui::Selectable(l_Label.c_str(), l_IsSelected))
                        {
                            s_SelectedEntity = it_Entity;
                        }

                        if (l_IsSelected)
                        {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndListBox();
                }

                if (s_SelectedEntity != s_InvalidEntity)
                {
                    ImGui::Separator();
                    ImGui::Text("Selected Entity: %u", static_cast<unsigned int>(s_SelectedEntity));

                    if (l_Registry.HasComponent<Trident::Transform>(s_SelectedEntity))
                    {
                        Trident::Transform& l_Transform = l_Registry.GetComponent<Trident::Transform>(s_SelectedEntity);
                        bool l_TransformChanged = false;

                        glm::vec3 l_Position = l_Transform.Position;
                        if (ImGui::DragFloat3("Position", glm::value_ptr(l_Position), 0.1f))
                        {
                            l_Transform.Position = l_Position;
                            l_TransformChanged = true;
                        }

                        glm::vec3 l_Rotation = l_Transform.Rotation;
                        if (ImGui::DragFloat3("Rotation", glm::value_ptr(l_Rotation), 0.1f))
                        {
                            l_Transform.Rotation = l_Rotation;
                            l_TransformChanged = true;
                        }

                        glm::vec3 l_Scale = l_Transform.Scale;
                        if (ImGui::DragFloat3("Scale", glm::value_ptr(l_Scale), 0.01f, 0.01f, 100.0f))
                        {
                            l_Transform.Scale = l_Scale;
                            l_TransformChanged = true;
                        }

                        if (l_TransformChanged)
                        {
                            Trident::Application::GetRenderer().SetTransform(l_Transform);
                        }

                        ImGui::Separator();
                        ImGui::TextUnformatted("Gizmo Operation");
                        if (ImGui::RadioButton("Translate", s_GizmoOperation == ImGuizmo::TRANSLATE))
                        {
                            s_GizmoOperation = ImGuizmo::TRANSLATE;
                        }
                        ImGui::SameLine();
                        if (ImGui::RadioButton("Rotate", s_GizmoOperation == ImGuizmo::ROTATE))
                        {
                            s_GizmoOperation = ImGuizmo::ROTATE;
                        }
                        ImGui::SameLine();
                        if (ImGui::RadioButton("Scale", s_GizmoOperation == ImGuizmo::SCALE))
                        {
                            s_GizmoOperation = ImGuizmo::SCALE;
                        }

                        ImGui::TextUnformatted("Gizmo Space");
                        if (ImGui::RadioButton("Local", s_GizmoMode == ImGuizmo::LOCAL))
                        {
                            s_GizmoMode = ImGuizmo::LOCAL;
                        }
                        ImGui::SameLine();
                        if (ImGui::RadioButton("World", s_GizmoMode == ImGuizmo::WORLD))
                        {
                            s_GizmoMode = ImGuizmo::WORLD;
                        }
                    }
                    else
                    {
                        ImGui::TextUnformatted("Transform component not present.");
                    }
                }
            }
        }
        ImGui::End();

        // Render the gizmo on top of the viewport once all inspector edits are applied.
        DrawTransformGizmo(s_SelectedEntity);

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

void ApplicationLayer::DrawTransformGizmo(Trident::ECS::Entity a_SelectedEntity)
{
    // Even when no entity is bound we start a frame so ImGuizmo can clear any persistent state.
    ImGuizmo::BeginFrame();

    if (a_SelectedEntity == s_InvalidEntity)
    {
        return;
    }

    Trident::ECS::Registry& l_Registry = Trident::Application::GetRegistry();
    if (!l_Registry.HasComponent<Trident::Transform>(a_SelectedEntity))
    {
        return;
    }

    // Fetch the camera matrices used by the renderer so the gizmo aligns with the actual scene view.
    Trident::Camera& l_Camera = Trident::Application::GetRenderer().GetCamera();
    glm::mat4 l_ViewMatrix = l_Camera.GetViewMatrix();

    const ImGuiViewport* l_MainViewport = ImGui::GetMainViewport();
    ImVec2 l_RectPosition = l_MainViewport->Pos;
    ImVec2 l_RectSize = l_MainViewport->Size;

    const Trident::ViewportInfo l_ViewportInfo = Trident::Application::GetRenderer().GetViewport();
    if (l_ViewportInfo.Size.x > 0.0f && l_ViewportInfo.Size.y > 0.0f)
    {
        // Offset the gizmo rectangle by the viewport position to support docked scene windows in the future.
        l_RectPosition.x += l_ViewportInfo.Position.x;
        l_RectPosition.y += l_ViewportInfo.Position.y;
        l_RectSize = ImVec2{ l_ViewportInfo.Size.x, l_ViewportInfo.Size.y };
    }

    const float l_AspectRatio = l_RectSize.y > 0.0f ? l_RectSize.x / l_RectSize.y : 1.0f;
    glm::mat4 l_ProjectionMatrix = glm::perspective(glm::radians(l_Camera.GetFOV()), l_AspectRatio, l_Camera.GetNearClip(), l_Camera.GetFarClip());
    l_ProjectionMatrix[1][1] *= -1.0f;

    Trident::Transform& l_Transform = l_Registry.GetComponent<Trident::Transform>(a_SelectedEntity);
    glm::mat4 l_ModelMatrix = ComposeTransform(l_Transform);

    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist();
    ImGuizmo::SetRect(l_RectPosition.x, l_RectPosition.y, l_RectSize.x, l_RectSize.y);

    if (ImGuizmo::Manipulate(glm::value_ptr(l_ViewMatrix), glm::value_ptr(l_ProjectionMatrix), s_GizmoOperation, s_GizmoMode, glm::value_ptr(l_ModelMatrix)))
    {
        // Sync the manipulated matrix back into the ECS so gameplay systems stay authoritative.
        Trident::Transform l_UpdatedTransform = DecomposeTransform(l_ModelMatrix, l_Transform);
        l_Transform = l_UpdatedTransform;
        Trident::Application::GetRenderer().SetTransform(l_Transform);
    }

    // Potential enhancement: expose snapping increments for translation/rotation/scale so artists can toggle grid alignment.
}