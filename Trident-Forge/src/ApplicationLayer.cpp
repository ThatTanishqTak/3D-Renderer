#include "ApplicationLayer.h"

#include <imgui.h>
#include <ImGuizmo.h>

#include <string>
#include <vector>
#include <limits>
#include <algorithm>
#include <array>
#include <cstring>

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include "Core/Utilities.h"
#include "UI/FileDialog.h"
#include "Loader/ModelLoader.h"
#include "Loader/TextureLoader.h"
#include "Camera/CameraComponent.h"
#include "Renderer/Renderer.h"
#include "ECS/Components/TransformComponent.h"
#include "ECS/Components/SpriteComponent.h"
#include "ECS/Components/LightComponent.h"

namespace
{
    // Compose a transform matrix from the ECS component for ImGuizmo consumption.
    glm::mat4 ComposeTransform(const Trident::Transform& transform)
    {
        glm::mat4 l_ModelMatrix{ 1.0f };
        l_ModelMatrix = glm::translate(l_ModelMatrix, transform.Position);
        l_ModelMatrix = glm::rotate(l_ModelMatrix, glm::radians(transform.Rotation.x), glm::vec3{ 1.0f, 0.0f, 0.0f });
        l_ModelMatrix = glm::rotate(l_ModelMatrix, glm::radians(transform.Rotation.y), glm::vec3{ 0.0f, 1.0f, 0.0f });
        l_ModelMatrix = glm::rotate(l_ModelMatrix, glm::radians(transform.Rotation.z), glm::vec3{ 0.0f, 0.0f, 1.0f });
        l_ModelMatrix = glm::scale(l_ModelMatrix, transform.Scale);

        return l_ModelMatrix;
    }

    // Convert an ImGuizmo-manipulated matrix back to the engine's transform component.
    Trident::Transform DecomposeTransform(const glm::mat4& modelMatrix, const Trident::Transform& defaultTransform)
    {
        glm::vec3 l_Scale{};
        glm::quat l_Rotation{};
        glm::vec3 l_Translation{};
        glm::vec3 l_Skew{};
        glm::vec4 l_Perspective{};

        if (!glm::decompose(modelMatrix, l_Scale, l_Rotation, l_Translation, l_Skew, l_Perspective))
        {
            // Preserve the previous values if decomposition fails, avoiding sudden jumps.
            return defaultTransform;
        }

        Trident::Transform l_Result = defaultTransform;
        l_Result.Position = l_Translation;
        l_Result.Scale = l_Scale;
        l_Result.Rotation = glm::degrees(glm::eulerAngles(glm::normalize(l_Rotation)));

        return l_Result;
    }

    // Produce a projection matrix based on the camera component settings and desired viewport aspect ratio.
    glm::mat4 BuildCameraProjectionMatrix(const Trident::CameraComponent& cameraComponent, float viewportAspect)
    {
        float l_Aspect = cameraComponent.OverrideAspectRatio ? cameraComponent.AspectRatio : viewportAspect;
        l_Aspect = std::max(l_Aspect, 0.0001f);

        if (cameraComponent.UseCustomProjection)
        {
            // Advanced users can inject a bespoke matrix; the editor relays it without modification.
            return cameraComponent.CustomProjection;
        }

        if (cameraComponent.Projection == Trident::ProjectionType::Orthographic)
        {
            // Orthographic size represents the vertical span; derive width from the resolved aspect ratio.
            const float l_HalfHeight = cameraComponent.OrthographicSize * 0.5f;
            const float l_HalfWidth = l_HalfHeight * l_Aspect;
            glm::mat4 l_Projection = glm::ortho(-l_HalfWidth, l_HalfWidth, -l_HalfHeight, l_HalfHeight, cameraComponent.NearClip, cameraComponent.FarClip);
            l_Projection[1][1] *= -1.0f;
            
            return l_Projection;
        }

        glm::mat4 l_Projection = glm::perspective(glm::radians(cameraComponent.FieldOfView), l_Aspect, cameraComponent.NearClip, cameraComponent.FarClip);
        l_Projection[1][1] *= -1.0f;
        
        return l_Projection;
    }

    // Editor-wide gizmo state stored at TU scope so every panel references the same configuration.
    ImGuizmo::OPERATION s_GizmoOperation = ImGuizmo::TRANSLATE;
    ImGuizmo::MODE s_GizmoMode = ImGuizmo::LOCAL;

    // Dedicated sentinel used when no entity is highlighted inside the inspector.
    constexpr Trident::ECS::Entity s_InvalidEntity = std::numeric_limits<Trident::ECS::Entity>::max();

    // Persistently tracked selection so the gizmo can render even when the inspector panel is closed.
    Trident::ECS::Entity s_SelectedEntity = s_InvalidEntity;
    // Camera inspector caches to keep text fields responsive while editing labels.
    std::array<char, 128> s_CameraNameBuffer{};
    Trident::ECS::Entity s_CameraCacheEntity = s_InvalidEntity;

    // Sprite inspector caches to keep text fields responsive while editing asset paths.
    std::array<char, 512> s_SpriteTextureBuffer{};
    std::array<char, 256> s_SpriteMaterialBuffer{};
    std::string s_SpriteTexturePath{};
    bool s_OpenSpriteTextureDialog = false;
    Trident::ECS::Entity s_SpriteCacheEntity = s_InvalidEntity;

    // Console window configuration toggles stored across frames for a consistent user experience.
    bool s_ShowConsoleErrors = true;
    bool s_ShowConsoleWarnings = true;
    bool s_ShowConsoleLogs = true;
    bool s_ConsoleAutoScroll = true;
    size_t s_LastConsoleEntryCount = 0;

    // Convert a timestamp to a human readable clock string that fits in the console.
    std::string FormatConsoleTimestamp(const std::chrono::system_clock::time_point& a_TimePoint)
    {
        std::time_t l_TimeT = std::chrono::system_clock::to_time_t(a_TimePoint);
        std::tm l_LocalTime{};

#if defined(_MSC_VER)
        localtime_s(&l_LocalTime, &l_TimeT);
#else
        localtime_r(&l_TimeT, &l_LocalTime);
#endif

        std::ostringstream l_Stream;
        l_Stream << std::put_time(&l_LocalTime, "%H:%M:%S");
        return l_Stream.str();
    }

    // Decide whether an entry should be shown given the active severity toggles.
    bool ShouldDisplayConsoleEntry(spdlog::level::level_enum a_Level)
    {
        switch (a_Level)
        {
        case spdlog::level::critical:
        case spdlog::level::err:
            return s_ShowConsoleErrors;
        case spdlog::level::warn:
            return s_ShowConsoleWarnings;
        default:
            return s_ShowConsoleLogs;
        }
    }

    // Pick a colour for a log entry so important events stand out while browsing history.
    ImVec4 GetConsoleColour(spdlog::level::level_enum a_Level)
    {
        switch (a_Level)
        {
        case spdlog::level::critical:
        case spdlog::level::err:
            return { 0.94f, 0.33f, 0.33f, 1.0f };
        case spdlog::level::warn:
            return { 0.97f, 0.78f, 0.26f, 1.0f };
        case spdlog::level::debug:
        case spdlog::level::trace:
            return { 0.60f, 0.80f, 0.98f, 1.0f };
        default:
            return { 0.85f, 0.85f, 0.85f, 1.0f };
        }
    }
}

ApplicationLayer::ApplicationLayer()
{
    // Initialize logging and the Forge window
    Trident::Utilities::Log::Init();

    m_Window = std::make_unique<Trident::Window>(1920, 1080, "Trident-Forge");
    m_Engine = std::make_unique<Trident::Application>(*m_Window);

    // Start the engine
    m_Engine->Init();

    // Wire editor panels to the freshly initialised engine and supporting systems.
    m_ContentBrowserPanel.SetEngine(m_Engine.get());
    m_ContentBrowserPanel.SetOnnxRuntime(&m_ONNX);

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
    // Main application loop aligning editor windows with Unreal terminology for future docking profiles.
    while (!m_Window->ShouldClose())
    {
        m_Engine->Update();
        m_ImGuiLayer->BeginFrame();

        // Surface layout persistence controls so designers can intentionally snapshot or restore workspace arrangements.
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("Layout"))
            {
                if (ImGui::MenuItem("Save Current Layout"))
                {
                    m_ImGuiLayer->SaveLayoutToDisk();
                }

                if (ImGui::MenuItem("Load Layout From Disk"))
                {
                    if (!m_ImGuiLayer->LoadLayoutFromDisk())
                    {
                        // If loading fails we rebuild the default so the dockspace stays valid.
                        m_ImGuiLayer->ResetLayoutToDefault();
                    }
                }

                if (ImGui::MenuItem("Reset Layout To Default"))
                {
                    m_ImGuiLayer->ResetLayoutToDefault();
                }

                ImGui::EndMenu();
            }

            ImGui::EndMainMenuBar();
        }

        m_ContentBrowserPanel.Render();
        m_ViewportPanel.SetSelectedEntity(s_SelectedEntity);
        m_ViewportPanel.Render();

        DrawWorldOutlinerPanel();
        DrawDetailsPanel();
        DrawOutputLogPanel();

        // Render the gizmo on top of the viewport once all inspector edits are applied.
        DrawTransformGizmo(s_SelectedEntity);

        m_ImGuiLayer->EndFrame();
        m_Engine->RenderScene();
    }
}

void ApplicationLayer::DrawWorldOutlinerPanel()
{
    // The world outliner focuses on scene hierarchy management; future work can add folders and search.
    if (!ImGui::Begin("World Outliner"))
    {
        ImGui::End();
        return;
    }

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
        if (ImGui::BeginListBox("##WorldOutlinerList"))
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
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Create Light Entity");

    if (ImGui::Button("Directional Light"))
    {
        // Spawn a fresh entity configured as a sun light so the renderer can immediately consume it.
        Trident::ECS::Entity l_NewEntity = l_Registry.CreateEntity();
        Trident::Transform& l_EntityTransform = l_Registry.AddComponent<Trident::Transform>(l_NewEntity);
        l_EntityTransform.Position = { 0.0f, 5.0f, 0.0f };

        Trident::LightComponent& l_Light = l_Registry.AddComponent<Trident::LightComponent>(l_NewEntity);
        l_Light.m_Type = Trident::LightComponent::Type::Directional;
        l_Light.m_Direction = glm::normalize(glm::vec3(-0.5f, -1.0f, -0.3f));
        l_Light.m_Intensity = 5.0f;

        s_SelectedEntity = l_NewEntity;
    }

    if (ImGui::Button("Point Light"))
    {
        // Instantiate a point light with a practical radius so scene authors can tweak it immediately.
        Trident::ECS::Entity l_NewEntity = l_Registry.CreateEntity();
        Trident::Transform& l_EntityTransform = l_Registry.AddComponent<Trident::Transform>(l_NewEntity);
        l_EntityTransform.Position = { 0.0f, 2.0f, 0.0f };

        Trident::LightComponent& l_Light = l_Registry.AddComponent<Trident::LightComponent>(l_NewEntity);
        l_Light.m_Type = Trident::LightComponent::Type::Point;
        l_Light.m_Range = 10.0f;
        l_Light.m_Intensity = 25.0f;

        s_SelectedEntity = l_NewEntity;
    }

    // Placeholder: integrate search filters or layer visibility toggles alongside the hierarchy in later passes.

    ImGui::End();
}

void ApplicationLayer::DrawDetailsPanel()
{
    // The details panel aggregates selection editing, scene controls, and tooling such as live-reload.
    if (!ImGui::Begin("Details"))
    {
        ImGui::End();
        return;
    }

    Trident::ECS::Registry& l_Registry = Trident::Application::GetRegistry();

    if (s_SelectedEntity != s_InvalidEntity)
    {
        ImGui::Text("Selected Entity: %u", static_cast<unsigned int>(s_SelectedEntity));

        if (l_Registry.HasComponent<Trident::Transform>(s_SelectedEntity))
        {
            Trident::Transform& l_EntityTransform = l_Registry.GetComponent<Trident::Transform>(s_SelectedEntity);
            bool l_TransformChanged = false;

            glm::vec3 l_Position = l_EntityTransform.Position;
            if (ImGui::DragFloat3("Position", glm::value_ptr(l_Position), 0.1f))
            {
                l_EntityTransform.Position = l_Position;
                l_TransformChanged = true;
            }

            glm::vec3 l_Rotation = l_EntityTransform.Rotation;
            if (ImGui::DragFloat3("Rotation", glm::value_ptr(l_Rotation), 0.1f))
            {
                l_EntityTransform.Rotation = l_Rotation;
                l_TransformChanged = true;
            }

            glm::vec3 l_Scale = l_EntityTransform.Scale;
            if (ImGui::DragFloat3("Scale", glm::value_ptr(l_Scale), 0.01f, 0.01f, 100.0f))
            {
                l_EntityTransform.Scale = l_Scale;
                l_TransformChanged = true;
            }

            if (l_TransformChanged)
            {
                Trident::Application::GetRenderer().SetTransform(l_EntityTransform);
            }
        }
        else
        {
            ImGui::TextUnformatted("Transform component not present.");
        }

        if (l_Registry.HasComponent<Trident::CameraComponent>(s_SelectedEntity))
        {
            Trident::CameraComponent& l_CameraComponent = l_Registry.GetComponent<Trident::CameraComponent>(s_SelectedEntity);

            if (s_CameraCacheEntity != s_SelectedEntity)
            {
                std::fill(s_CameraNameBuffer.begin(), s_CameraNameBuffer.end(), '\0');
                std::strncpy(s_CameraNameBuffer.data(), l_CameraComponent.Name.c_str(), s_CameraNameBuffer.size() - 1);
                s_CameraCacheEntity = s_SelectedEntity;
            }

            if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
            {
                if (ImGui::InputText("Label", s_CameraNameBuffer.data(), s_CameraNameBuffer.size()))
                {
                    l_CameraComponent.Name = s_CameraNameBuffer.data();
                }

                int l_SelectedProjection = static_cast<int>(l_CameraComponent.Projection);
                const char* l_ProjectionLabels[] = { "Perspective", "Orthographic" };
                if (ImGui::Combo("Projection Type", &l_SelectedProjection, l_ProjectionLabels, IM_ARRAYSIZE(l_ProjectionLabels)))
                {
                    l_CameraComponent.Projection = static_cast<Trident::ProjectionType>(l_SelectedProjection);
                }

                if (l_CameraComponent.Projection == Trident::ProjectionType::Perspective || l_CameraComponent.UseCustomProjection)
                {
                    float l_FieldOfView = l_CameraComponent.FieldOfView;
                    if (ImGui::SliderFloat("Field of View", &l_FieldOfView, 1.0f, 170.0f, "%.1f"))
                    {
                        l_CameraComponent.FieldOfView = l_FieldOfView;
                    }
                }

                if (l_CameraComponent.Projection == Trident::ProjectionType::Orthographic && !l_CameraComponent.UseCustomProjection)
                {
                    float l_OrthoSize = l_CameraComponent.OrthographicSize;
                    if (ImGui::DragFloat("Ortho Size", &l_OrthoSize, 0.1f, 0.01f, 1000.0f, "%.2f"))
                    {
                        l_CameraComponent.OrthographicSize = std::max(0.01f, l_OrthoSize);
                    }
                }

                float l_NearClip = l_CameraComponent.NearClip;
                if (ImGui::DragFloat("Near Clip", &l_NearClip, 0.01f, 0.001f, l_CameraComponent.FarClip - 0.01f, "%.3f"))
                {
                    l_CameraComponent.NearClip = std::max(0.001f, std::min(l_NearClip, l_CameraComponent.FarClip - 0.01f));
                }

                float l_MinFarClip = l_CameraComponent.NearClip + 0.01f;
                float l_FarClip = l_CameraComponent.FarClip;
                if (ImGui::DragFloat("Far Clip", &l_FarClip, 1.0f, l_MinFarClip, 20000.0f, "%.2f"))
                {
                    l_CameraComponent.FarClip = std::max(l_MinFarClip, l_FarClip);
                }

                if (ImGui::Checkbox("Override Aspect Ratio", &l_CameraComponent.OverrideAspectRatio))
                {
                    // Toggle allows the next section to appear or hide dynamically.
                }
                if (l_CameraComponent.OverrideAspectRatio)
                {
                    float l_AspectRatio = l_CameraComponent.AspectRatio;
                    if (ImGui::DragFloat("Aspect Ratio", &l_AspectRatio, 0.01f, 0.10f, 10.0f, "%.2f"))
                    {
                        l_CameraComponent.AspectRatio = std::max(0.10f, l_AspectRatio);
                    }
                }

                if (ImGui::Checkbox("Use Custom Projection", &l_CameraComponent.UseCustomProjection))
                {
                    // Future: add presets that populate the matrix for common cinematic looks.
                }
                if (l_CameraComponent.UseCustomProjection)
                {
                    ImGui::TextUnformatted("Custom Projection Matrix");
                    ImGui::PushID("CustomProjectionMatrix");
                    float* l_MatrixData = glm::value_ptr(l_CameraComponent.CustomProjection);
                    for (int it_Row = 0; it_Row < 4; ++it_Row)
                    {
                        ImGui::PushID(it_Row);
                        ImGui::InputFloat4("##Row", l_MatrixData + (it_Row * 4));
                        ImGui::PopID();
                    }
                    ImGui::PopID();
                    ImGui::TextWrapped("Note: Custom matrices bypass the automatic Vulkan clip-space adjustments.");
                }

                ImGui::Separator();
                ImGui::TextUnformatted("Future Camera Effects");
                ImGui::TextUnformatted("Exposure, depth of field, and motion blur controls will be introduced in later updates.");
            }
        }
        else
        {
            if (s_CameraCacheEntity == s_SelectedEntity)
            {
                s_CameraCacheEntity = s_InvalidEntity;
            }

            if (ImGui::Button("Add Camera Component"))
            {
                l_Registry.AddComponent<Trident::CameraComponent>(s_SelectedEntity);
                s_CameraCacheEntity = s_InvalidEntity;
            }
        }

        // Sprite inspector exposed after transform editing so spatial adjustments happen first.
        if (l_Registry.HasComponent<Trident::SpriteComponent>(s_SelectedEntity))
        {
            Trident::SpriteComponent& l_Sprite = l_Registry.GetComponent<Trident::SpriteComponent>(s_SelectedEntity);

            if (s_SpriteCacheEntity != s_SelectedEntity)
            {
                std::fill(s_SpriteTextureBuffer.begin(), s_SpriteTextureBuffer.end(), '\0');
                std::fill(s_SpriteMaterialBuffer.begin(), s_SpriteMaterialBuffer.end(), '\0');
                if (!l_Sprite.m_TextureId.empty())
                {
                    std::strncpy(s_SpriteTextureBuffer.data(), l_Sprite.m_TextureId.c_str(), s_SpriteTextureBuffer.size() - 1);
                }
                if (!l_Sprite.m_MaterialOverrideId.empty())
                {
                    std::strncpy(s_SpriteMaterialBuffer.data(), l_Sprite.m_MaterialOverrideId.c_str(), s_SpriteMaterialBuffer.size() - 1);
                }
                s_SpriteTexturePath = l_Sprite.m_TextureId;
                s_OpenSpriteTextureDialog = false;
                s_SpriteCacheEntity = s_SelectedEntity;
            }

            if (ImGui::CollapsingHeader("Sprite", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Checkbox("Visible", &l_Sprite.m_Visible);

                if (ImGui::InputText("Texture Asset", s_SpriteTextureBuffer.data(), s_SpriteTextureBuffer.size()))
                {
                    l_Sprite.m_TextureId = s_SpriteTextureBuffer.data();
                    s_SpriteTexturePath = l_Sprite.m_TextureId;
                }
                ImGui::SameLine();
                if (ImGui::Button("Browse##SpriteTexture"))
                {
                    s_OpenSpriteTextureDialog = true;
                    s_SpriteTexturePath = l_Sprite.m_TextureId;
                    ImGui::OpenPopup("SpriteTextureDialog");
                }

                if (s_OpenSpriteTextureDialog)
                {
                    if (Trident::UI::FileDialog::Open("SpriteTextureDialog", s_SpriteTexturePath))
                    {
                        l_Sprite.m_TextureId = s_SpriteTexturePath;
                        std::fill(s_SpriteTextureBuffer.begin(), s_SpriteTextureBuffer.end(), '\0');
                        std::strncpy(s_SpriteTextureBuffer.data(), l_Sprite.m_TextureId.c_str(), s_SpriteTextureBuffer.size() - 1);
                        s_OpenSpriteTextureDialog = false;
                    }

                    if (!ImGui::IsPopupOpen("SpriteTextureDialog"))
                    {
                        s_OpenSpriteTextureDialog = false;
                    }
                }

                ImGui::ColorEdit4("Tint", glm::value_ptr(l_Sprite.m_TintColor));
                ImGui::DragFloat("Tiling Factor", &l_Sprite.m_TilingFactor, 0.01f, 0.0f, 100.0f);
                ImGui::DragFloat2("UV Scale", glm::value_ptr(l_Sprite.m_UVScale), 0.01f, 0.0f, 16.0f);
                ImGui::DragFloat2("UV Offset", glm::value_ptr(l_Sprite.m_UVOffset), 0.01f, -16.0f, 16.0f);
                ImGui::DragFloat("Sort Bias", &l_Sprite.m_SortOffset, 0.001f, -10.0f, 10.0f);

                if (ImGui::Checkbox("Override Material", &l_Sprite.m_UseMaterialOverride))
                {
                    // Checkbox toggles override usage; field below is conditionally exposed.
                }

                if (l_Sprite.m_UseMaterialOverride)
                {
                    if (ImGui::InputText("Material ID", s_SpriteMaterialBuffer.data(), s_SpriteMaterialBuffer.size()))
                    {
                        l_Sprite.m_MaterialOverrideId = s_SpriteMaterialBuffer.data();
                    }
                }

                if (ImGui::DragInt2("Atlas Tiles", glm::value_ptr(l_Sprite.m_AtlasTiles), 1.0f, 1, 256))
                {
                    l_Sprite.m_AtlasTiles.x = std::max(1, l_Sprite.m_AtlasTiles.x);
                    l_Sprite.m_AtlasTiles.y = std::max(1, l_Sprite.m_AtlasTiles.y);
                }

                if (ImGui::InputInt("Atlas Index", &l_Sprite.m_AtlasIndex))
                {
                    l_Sprite.m_AtlasIndex = std::max(0, l_Sprite.m_AtlasIndex);
                }

                ImGui::DragFloat("Animation FPS", &l_Sprite.m_AnimationSpeed, 0.1f, 0.0f, 240.0f);
                ImGui::TextUnformatted("Future work: expose flipbook preview and per-frame events.");
            }
        }
        else
        {
            s_SpriteCacheEntity = s_InvalidEntity;
            if (ImGui::Button("Add Sprite Component"))
            {
                l_Registry.AddComponent<Trident::SpriteComponent>(s_SelectedEntity);
                s_SpriteCacheEntity = s_InvalidEntity;
            }
        }

        if (l_Registry.HasComponent<Trident::LightComponent>(s_SelectedEntity))
        {
            Trident::LightComponent& l_LightComponent = l_Registry.GetComponent<Trident::LightComponent>(s_SelectedEntity);
            if (ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen))
            {
                // Surface common light properties with clear labelling for artists.
                int l_SelectedType = static_cast<int>(l_LightComponent.m_Type);
                const char* l_TypeLabels[] = { "Directional", "Point" };
                if (ImGui::Combo("Type", &l_SelectedType, l_TypeLabels, IM_ARRAYSIZE(l_TypeLabels)))
                {
                    l_LightComponent.m_Type = static_cast<Trident::LightComponent::Type>(l_SelectedType);
                }

                ImGui::Checkbox("Enabled", &l_LightComponent.m_Enabled);
                ImGui::ColorEdit3("Colour", glm::value_ptr(l_LightComponent.m_Color));
                ImGui::DragFloat("Intensity", &l_LightComponent.m_Intensity, 0.1f, 0.0f, 1000.0f, "%.2f");

                if (l_LightComponent.m_Type == Trident::LightComponent::Type::Directional)
                {
                    glm::vec3 l_Direction = l_LightComponent.m_Direction;
                    if (ImGui::DragFloat3("Direction", glm::value_ptr(l_Direction), 0.01f, -1.0f, 1.0f))
                    {
                        if (glm::length(l_Direction) > 0.0001f)
                        {
                            l_LightComponent.m_Direction = glm::normalize(l_Direction);
                        }
                    }

                    ImGui::TextUnformatted("Shadow controls will land here in a future milestone.");
                }
                else if (l_LightComponent.m_Type == Trident::LightComponent::Type::Point)
                {
                    if (ImGui::DragFloat("Range", &l_LightComponent.m_Range, 0.1f, 0.0f, 500.0f, "%.2f"))
                    {
                        l_LightComponent.m_Range = std::max(0.0f, l_LightComponent.m_Range);
                    }

                    ImGui::TextUnformatted("Clustered lighting parameters can be exposed here later.");
                }

                ImGui::Separator();
                ImGui::BeginDisabled(true);
                ImGui::Checkbox("Cast Shadows", &l_LightComponent.m_ShadowCaster);
                ImGui::EndDisabled();
                ImGui::TextUnformatted("Shadow toggles are placeholder UI for upcoming passes.");
            }
        }
        else
        {
            if (ImGui::Button("Add Light Component"))
            {
                // Default to a point light so new lights immediately influence the scene.
                Trident::LightComponent& l_Light = l_Registry.AddComponent<Trident::LightComponent>(s_SelectedEntity);
                l_Light.m_Type = Trident::LightComponent::Type::Point;
                l_Light.m_Range = 10.0f;
            }
        }
    }
    else
    {
        ImGui::TextUnformatted("Select an entity to edit its components.");
    }

    ImGui::Separator();

    ImGui::TextUnformatted("Gizmo Controls");
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

    if (ImGui::RadioButton("Local", s_GizmoMode == ImGuizmo::LOCAL))
    {
        s_GizmoMode = ImGuizmo::LOCAL;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("World", s_GizmoMode == ImGuizmo::WORLD))
    {
        s_GizmoMode = ImGuizmo::WORLD;
    }

    ImGui::Separator();

    if (ImGui::CollapsingHeader("Editor Camera", ImGuiTreeNodeFlags_DefaultOpen))
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

        const float l_MinFarClip = l_Camera.GetNearClip() + 0.01f;
        if (ImGui::DragFloat("Far Clip", &l_FarClipValue, 1.0f, l_MinFarClip, 20000.0f))
        {
            l_Camera.SetFarClip(l_FarClipValue);
        }
    }

    if (ImGui::CollapsingHeader("Materials", ImGuiTreeNodeFlags_DefaultOpen))
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

    if (ImGui::CollapsingHeader("Live Reload", ImGuiTreeNodeFlags_DefaultOpen))
    {
        Trident::Utilities::FileWatcher& l_Watcher = Trident::Utilities::FileWatcher::Get();
        bool l_AutoReload = l_Watcher.IsAutoReloadEnabled();
        if (ImGui::Checkbox("Automatic Reload", &l_AutoReload))
        {
            l_Watcher.EnableAutoReload(l_AutoReload);
        }

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
                const bool l_Disabled = it_Event.Status == Trident::Utilities::FileWatcher::ReloadStatus::Queued;
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
    }

    // Placeholder for upcoming tabs such as animation blueprints or sequencer integration.

    ImGui::End();
}

void ApplicationLayer::DrawOutputLogPanel()
{
    // The output log combines frame statistics with filtered logging controls for a single dockable surface.
    if (!ImGui::Begin("Output Log"))
    {
        ImGui::End();
        return;
    }

    ImGui::TextUnformatted("Frame Statistics");
    ImGui::Text("FPS: %.2f", Trident::Utilities::Time::GetFPS());
    ImGui::Text("Allocations: %zu", Trident::Application::GetRenderer().GetLastFrameAllocationCount());
    ImGui::Text("Models: %zu", Trident::Application::GetRenderer().GetModelCount());
    ImGui::Text("Triangles: %zu", Trident::Application::GetRenderer().GetTriangleCount());

    const Trident::Renderer::FrameTimingStats& l_PerfStats = Trident::Application::GetRenderer().GetFrameTimingStats();
    const size_t l_PerfCount = Trident::Application::GetRenderer().GetFrameTimingHistoryCount();
    if (l_PerfCount > 0)
    {
        ImGui::Text("Frame Avg: %.3f ms", l_PerfStats.AverageMilliseconds);
        ImGui::Text("Frame Min: %.3f ms", l_PerfStats.MinimumMilliseconds);
        ImGui::Text("Frame Max: %.3f ms", l_PerfStats.MaximumMilliseconds);
        ImGui::Text("Average FPS: %.2f", l_PerfStats.AverageFPS);
    }
    else
    {
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

    ImGui::Separator();

    std::vector<Trident::Utilities::ConsoleLog::Entry> l_LogEntries = Trident::Utilities::ConsoleLog::GetSnapshot();

    if (ImGui::Button("Clear"))
    {
        Trident::Utilities::ConsoleLog::Clear();
        s_LastConsoleEntryCount = 0;
    }

    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &s_ConsoleAutoScroll);

    ImGui::Separator();

    ImGui::Checkbox("Errors", &s_ShowConsoleErrors);
    ImGui::SameLine();
    ImGui::Checkbox("Warnings", &s_ShowConsoleWarnings);
    ImGui::SameLine();
    ImGui::Checkbox("Logs", &s_ShowConsoleLogs);

    ImGui::Separator();

    ImGui::BeginChild("OutputLogScroll", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_HorizontalScrollbar);

    for (const Trident::Utilities::ConsoleLog::Entry& it_Entry : l_LogEntries)
    {
        if (!ShouldDisplayConsoleEntry(it_Entry.Level))
        {
            continue;
        }

        const std::string l_Timestamp = FormatConsoleTimestamp(it_Entry.Timestamp);
        const ImVec4 l_Colour = GetConsoleColour(it_Entry.Level);

        ImGui::PushStyleColor(ImGuiCol_Text, l_Colour);
        ImGui::Text("[%s] %s", l_Timestamp.c_str(), it_Entry.Message.c_str());
        ImGui::PopStyleColor();
    }

    if (s_ConsoleAutoScroll && !l_LogEntries.empty() && l_LogEntries.size() != s_LastConsoleEntryCount)
    {
        ImGui::SetScrollHereY(1.0f);
    }

    ImGui::EndChild();

    s_LastConsoleEntryCount = l_LogEntries.size();

    // Future addition: surface shader compiler errors and asset validation summaries alongside runtime logs.

    ImGui::End();
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

    // Fetch the viewport rectangle published by the renderer so the gizmo aligns with the active scene view.

    const Trident::ViewportInfo l_ViewportInfo = Trident::Application::GetRenderer().GetViewport();
    ImVec2 l_RectPosition{};
    ImVec2 l_RectSize{};

    if (l_ViewportInfo.Size.x > 0.0f && l_ViewportInfo.Size.y > 0.0f)
    {
        // Use the viewport data directly so the gizmo tracks the rendered scene even with multi-viewport enabled.
        l_RectPosition = ImVec2{ l_ViewportInfo.Position.x, l_ViewportInfo.Position.y };
        l_RectSize = ImVec2{ l_ViewportInfo.Size.x, l_ViewportInfo.Size.y };
    }
    else
    {
        // Fall back to the main viewport bounds when the renderer has not published explicit viewport information yet.
        const ImGuiViewport* l_MainViewport = ImGui::GetMainViewport();
        l_RectPosition = l_MainViewport->Pos;
        l_RectSize = l_MainViewport->Size;
    }

    // Mirror the Scene panel camera selection logic so the gizmo uses whichever camera the user targeted.
    glm::mat4 l_ViewMatrix{ 1.0f };
    glm::mat4 l_ProjectionMatrix{ 1.0f };
    bool l_UseOrthographicGizmo = false;

    const Trident::ECS::Entity l_SelectedViewportCamera = m_ViewportPanel.GetSelectedCamera();

    if (l_SelectedViewportCamera != s_InvalidEntity
        && l_Registry.HasComponent<Trident::CameraComponent>(l_SelectedViewportCamera)
        && l_Registry.HasComponent<Trident::Transform>(l_SelectedViewportCamera))
    {
        // A scene camera is actively selected; derive view/projection parameters from the ECS components.
        const Trident::CameraComponent& l_CameraComponent = l_Registry.GetComponent<Trident::CameraComponent>(l_SelectedViewportCamera);
        const Trident::Transform& l_CameraTransform = l_Registry.GetComponent<Trident::Transform>(l_SelectedViewportCamera);

        const glm::mat4 l_ModelMatrix = ComposeTransform(l_CameraTransform);
        l_ViewMatrix = glm::inverse(l_ModelMatrix);
        const float l_AspectRatio = l_RectSize.y > 0.0f ? l_RectSize.x / l_RectSize.y : 1.0f;
        l_ProjectionMatrix = BuildCameraProjectionMatrix(l_CameraComponent, l_AspectRatio);
        l_UseOrthographicGizmo = !l_CameraComponent.UseCustomProjection && l_CameraComponent.Projection == Trident::ProjectionType::Orthographic;
    }
    else
    {
        // Fall back to the editor camera when no ECS-driven viewport camera is active.
        Trident::Camera& l_Camera = Trident::Application::GetRenderer().GetCamera();
        l_ViewMatrix = l_Camera.GetViewMatrix();
        const float l_AspectRatio = l_RectSize.y > 0.0f ? l_RectSize.x / l_RectSize.y : 1.0f;
        l_ProjectionMatrix = glm::perspective(glm::radians(l_Camera.GetFOV()), l_AspectRatio, l_Camera.GetNearClip(), l_Camera.GetFarClip());
        l_ProjectionMatrix[1][1] *= -1.0f;
        l_UseOrthographicGizmo = false;
    }

    Trident::Transform& l_EntityTransform = l_Registry.GetComponent<Trident::Transform>(a_SelectedEntity);
    glm::mat4 l_ModelMatrix = ComposeTransform(l_EntityTransform);

    ImGuizmo::SetOrthographic(l_UseOrthographicGizmo);
    ImGuizmo::SetDrawlist();
    ImGuizmo::SetRect(l_RectPosition.x, l_RectPosition.y, l_RectSize.x, l_RectSize.y);

    if (ImGuizmo::Manipulate(glm::value_ptr(l_ViewMatrix), glm::value_ptr(l_ProjectionMatrix), s_GizmoOperation, s_GizmoMode, glm::value_ptr(l_ModelMatrix)))
    {
        // Sync the manipulated matrix back into the ECS so gameplay systems stay authoritative.
        Trident::Transform l_UpdatedTransform = DecomposeTransform(l_ModelMatrix, l_EntityTransform);
        l_EntityTransform = l_UpdatedTransform;
        Trident::Application::GetRenderer().SetTransform(l_EntityTransform);
    }

    // Potential enhancement: expose snapping increments for translation/rotation/scale so artists can toggle grid alignment.
}