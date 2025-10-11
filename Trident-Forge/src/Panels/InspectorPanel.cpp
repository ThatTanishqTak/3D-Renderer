#include "InspectorPanel.h"

#include <imgui.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <vector>
#include <string>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "Application.h"
#include "Renderer/Renderer.h"
#include "Camera/Camera.h"
#include "Camera/CameraComponent.h"
#include "ECS/Components/TransformComponent.h"
#include "ECS/Components/SpriteComponent.h"
#include "ECS/Components/LightComponent.h"
#include "UI/FileDialog.h"
#include "Core/Utilities.h"
#include "Geometry/Material.h"

namespace UI
{
    namespace
    {
        constexpr Trident::ECS::Entity s_InvalidEntity = std::numeric_limits<Trident::ECS::Entity>::max();
    }

    InspectorPanel::InspectorPanel(): m_SelectedEntity(s_InvalidEntity), m_CameraNameBuffer{}, m_CachedCameraEntity(s_InvalidEntity), m_SpriteTextureBuffer{}, m_SpriteMaterialBuffer{},
        m_SpriteTexturePath{}, m_OpenSpriteTextureDialog(false), m_CachedSpriteEntity(s_InvalidEntity), m_GizmoOperation(nullptr), m_GizmoMode(nullptr)
    {
        // Initialise caches so the panel behaves predictably when selection changes.
    }

    void InspectorPanel::SetSelectedEntity(Trident::ECS::Entity a_SelectedEntity)
    {
        m_SelectedEntity = a_SelectedEntity;
    }

    void InspectorPanel::SetGizmoState(ImGuizmo::OPERATION* a_Operation, ImGuizmo::MODE* a_Mode)
    {
        m_GizmoOperation = a_Operation;
        m_GizmoMode = a_Mode;
    }

    void InspectorPanel::Render()
    {
        if (!ImGui::Begin("Details"))
        {
            ImGui::End();
            return;
        }

        Trident::ECS::Registry& l_Registry = Trident::Application::GetRegistry();

        const bool l_HasSelection = m_SelectedEntity != s_InvalidEntity;
        if (l_HasSelection)
        {
            ImGui::Text("Selected Entity: %u", static_cast<unsigned int>(m_SelectedEntity));

            DrawTransformSection(l_Registry);
            DrawCameraSection(l_Registry);
            DrawSpriteSection(l_Registry);
            DrawLightSection(l_Registry);
        }
        else
        {
            ImGui::TextUnformatted("Select an entity to edit its properties.");
            m_CachedCameraEntity = s_InvalidEntity;
            m_CachedSpriteEntity = s_InvalidEntity;
        }

        ImGui::Separator();

        DrawGizmoControls();
        DrawEditorCameraSection();
        DrawMaterialsSection();
        DrawLiveReloadSection();

        ImGui::End();
    }

    void InspectorPanel::DrawTransformSection(Trident::ECS::Registry& a_Registry)
    {
        if (!a_Registry.HasComponent<Trident::Transform>(m_SelectedEntity))
        {
            ImGui::TextUnformatted("Transform component not present.");
            return;
        }

        Trident::Transform& l_EntityTransform = a_Registry.GetComponent<Trident::Transform>(m_SelectedEntity);
        bool l_TransformChanged = false;

        // Use a scoped ID so entity transform controls never collide with editor camera widgets.
        ImGui::PushID("EntityTransform");
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
        ImGui::PopID();

        if (l_TransformChanged)
        {
            Trident::Application::GetRenderer().SetTransform(l_EntityTransform);
        }
    }

    void InspectorPanel::ResetCameraCacheIfNeeded(Trident::ECS::Entity a_SelectedEntity, Trident::ECS::Registry& a_Registry)
    {
        if (m_CachedCameraEntity == a_SelectedEntity)
        {
            return;
        }

        std::fill(m_CameraNameBuffer.begin(), m_CameraNameBuffer.end(), '\0');
        if (a_Registry.HasComponent<Trident::CameraComponent>(a_SelectedEntity))
        {
            Trident::CameraComponent& l_CameraComponent = a_Registry.GetComponent<Trident::CameraComponent>(a_SelectedEntity);
            std::strncpy(m_CameraNameBuffer.data(), l_CameraComponent.Name.c_str(), m_CameraNameBuffer.size() - 1);
        }

        m_CachedCameraEntity = a_SelectedEntity;
    }

    void InspectorPanel::DrawCameraSection(Trident::ECS::Registry& a_Registry)
    {
        if (!a_Registry.HasComponent<Trident::CameraComponent>(m_SelectedEntity))
        {
            m_CachedCameraEntity = s_InvalidEntity;
            if (ImGui::Button("Add Camera Component"))
            {
                Trident::CameraComponent& l_CameraComponent = a_Registry.AddComponent<Trident::CameraComponent>(m_SelectedEntity);
                l_CameraComponent.Name = "Camera";
                m_CachedCameraEntity = s_InvalidEntity;
            }
            return;
        }

        Trident::CameraComponent& l_CameraComponent = a_Registry.GetComponent<Trident::CameraComponent>(m_SelectedEntity);
        ResetCameraCacheIfNeeded(m_SelectedEntity, a_Registry);

        if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
        {
            // Camera component controls are isolated from editor camera sliders via an ID scope.
            ImGui::PushID("EntityCameraComponent");
            if (ImGui::InputText("Label", m_CameraNameBuffer.data(), m_CameraNameBuffer.size()))
            {
                l_CameraComponent.Name = m_CameraNameBuffer.data();
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
                // Checkbox toggles whether the aspect ratio field is shown.
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
                    for (int it_Column = 0; it_Column < 4; ++it_Column)
                    {
                        ImGui::PushID(it_Row * 4 + it_Column);
                        ImGui::DragFloat("##matrix", &l_MatrixData[it_Row * 4 + it_Column], 0.01f, -10.0f, 10.0f, "%.3f");
                        ImGui::PopID();
                        if (it_Column < 3)
                        {
                            ImGui::SameLine();
                        }
                    }
                }
                ImGui::PopID();
            }
            ImGui::PopID();
        }
    }

    void InspectorPanel::ResetSpriteCacheIfNeeded(Trident::ECS::Entity a_SelectedEntity, Trident::ECS::Registry& a_Registry)
    {
        if (m_CachedSpriteEntity == a_SelectedEntity)
        {
            return;
        }

        std::fill(m_SpriteTextureBuffer.begin(), m_SpriteTextureBuffer.end(), '\0');
        std::fill(m_SpriteMaterialBuffer.begin(), m_SpriteMaterialBuffer.end(), '\0');

        if (a_Registry.HasComponent<Trident::SpriteComponent>(a_SelectedEntity))
        {
            Trident::SpriteComponent& l_Sprite = a_Registry.GetComponent<Trident::SpriteComponent>(a_SelectedEntity);
            if (!l_Sprite.m_TextureId.empty())
            {
                std::strncpy(m_SpriteTextureBuffer.data(), l_Sprite.m_TextureId.c_str(), m_SpriteTextureBuffer.size() - 1);
            }
            if (!l_Sprite.m_MaterialOverrideId.empty())
            {
                std::strncpy(m_SpriteMaterialBuffer.data(), l_Sprite.m_MaterialOverrideId.c_str(), m_SpriteMaterialBuffer.size() - 1);
            }
            m_SpriteTexturePath = l_Sprite.m_TextureId;
            m_OpenSpriteTextureDialog = false;
        }

        m_CachedSpriteEntity = a_SelectedEntity;
    }

    void InspectorPanel::DrawSpriteSection(Trident::ECS::Registry& a_Registry)
    {
        if (!a_Registry.HasComponent<Trident::SpriteComponent>(m_SelectedEntity))
        {
            m_CachedSpriteEntity = s_InvalidEntity;
            if (ImGui::Button("Add Sprite Component"))
            {
                a_Registry.AddComponent<Trident::SpriteComponent>(m_SelectedEntity);
                m_CachedSpriteEntity = s_InvalidEntity;
            }
            return;
        }

        Trident::SpriteComponent& l_Sprite = a_Registry.GetComponent<Trident::SpriteComponent>(m_SelectedEntity);
        ResetSpriteCacheIfNeeded(m_SelectedEntity, a_Registry);

        if (ImGui::CollapsingHeader("Sprite", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Checkbox("Visible", &l_Sprite.m_Visible);

            if (ImGui::InputText("Texture Asset", m_SpriteTextureBuffer.data(), m_SpriteTextureBuffer.size()))
            {
                l_Sprite.m_TextureId = m_SpriteTextureBuffer.data();
                m_SpriteTexturePath = l_Sprite.m_TextureId;
            }
            ImGui::SameLine();
            if (ImGui::Button("Browse##SpriteTexture"))
            {
                m_OpenSpriteTextureDialog = true;
                m_SpriteTexturePath = l_Sprite.m_TextureId;
                ImGui::OpenPopup("SpriteTextureDialog");
            }

            if (m_OpenSpriteTextureDialog)
            {
                if (Trident::UI::FileDialog::Open("SpriteTextureDialog", m_SpriteTexturePath))
                {
                    l_Sprite.m_TextureId = m_SpriteTexturePath;
                    std::fill(m_SpriteTextureBuffer.begin(), m_SpriteTextureBuffer.end(), '\0');
                    std::strncpy(m_SpriteTextureBuffer.data(), l_Sprite.m_TextureId.c_str(), m_SpriteTextureBuffer.size() - 1);
                    m_OpenSpriteTextureDialog = false;
                }

                if (!ImGui::IsPopupOpen("SpriteTextureDialog"))
                {
                    m_OpenSpriteTextureDialog = false;
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
                if (ImGui::InputText("Material ID", m_SpriteMaterialBuffer.data(), m_SpriteMaterialBuffer.size()))
                {
                    l_Sprite.m_MaterialOverrideId = m_SpriteMaterialBuffer.data();
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

    void InspectorPanel::DrawLightSection(Trident::ECS::Registry& a_Registry)
    {
        if (!a_Registry.HasComponent<Trident::LightComponent>(m_SelectedEntity))
        {
            if (ImGui::Button("Add Light Component"))
            {
                Trident::LightComponent& l_Light = a_Registry.AddComponent<Trident::LightComponent>(m_SelectedEntity);
                l_Light.m_Type = Trident::LightComponent::Type::Point;
            }
            return;
        }

        Trident::LightComponent& l_LightComponent = a_Registry.GetComponent<Trident::LightComponent>(m_SelectedEntity);
        if (ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen))
        {
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

                //ImGui::DragFloat("Falloff", &l_LightComponent.m_Falloff, 0.01f, 0.0f, 10.0f, "%.2f");
                ImGui::TextUnformatted("Future work: expose IES profiles and shadow maps.");
            }
        }
    }

    void InspectorPanel::DrawGizmoControls()
    {
        ImGui::TextUnformatted("Gizmo Controls");

        if (!m_GizmoOperation || !m_GizmoMode)
        {
            ImGui::TextUnformatted("Gizmo state unavailable.");
            return;
        }

        if (ImGui::RadioButton("Translate", *m_GizmoOperation == ImGuizmo::TRANSLATE))
        {
            *m_GizmoOperation = ImGuizmo::TRANSLATE;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Rotate", *m_GizmoOperation == ImGuizmo::ROTATE))
        {
            *m_GizmoOperation = ImGuizmo::ROTATE;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Scale", *m_GizmoOperation == ImGuizmo::SCALE))
        {
            *m_GizmoOperation = ImGuizmo::SCALE;
        }

        if (ImGui::RadioButton("Local", *m_GizmoMode == ImGuizmo::LOCAL))
        {
            *m_GizmoMode = ImGuizmo::LOCAL;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("World", *m_GizmoMode == ImGuizmo::WORLD))
        {
            *m_GizmoMode = ImGuizmo::WORLD;
        }
    }

    void InspectorPanel::DrawEditorCameraSection()
    {
        if (!ImGui::CollapsingHeader("Editor Camera", ImGuiTreeNodeFlags_DefaultOpen))
        {
            return;
        }

        Trident::Camera& l_Camera = Trident::Application::GetRenderer().GetCamera();

        // Editor camera controls gain their own ID namespace to prevent conflicts with entity controls.
        ImGui::PushID("EditorCamera");
        glm::vec3 l_CameraPosition = l_Camera.GetPosition();
        if (ImGui::DragFloat3("Position##EditorCamera", glm::value_ptr(l_CameraPosition), 0.1f))
        {
            l_Camera.SetPosition(l_CameraPosition);
        }

        float l_YawDegrees = l_Camera.GetYaw();
        if (ImGui::DragFloat("Yaw##EditorCamera", &l_YawDegrees, 0.1f, -360.0f, 360.0f))
        {
            l_Camera.SetYaw(l_YawDegrees);
        }

        float l_PitchDegrees = l_Camera.GetPitch();
        if (ImGui::DragFloat("Pitch##EditorCamera", &l_PitchDegrees, 0.1f, -89.0f, 89.0f))
        {
            l_Camera.SetPitch(l_PitchDegrees);
        }

        float l_FieldOfView = l_Camera.GetFOV();
        if (ImGui::SliderFloat("Field of View##EditorCamera", &l_FieldOfView, 1.0f, 120.0f))
        {
            l_Camera.SetFOV(l_FieldOfView);
        }

        float l_FarClipValue = l_Camera.GetFarClip();
        float l_NearClipValue = l_Camera.GetNearClip();
        if (ImGui::DragFloat("Near Clip##EditorCamera", &l_NearClipValue, 0.01f, 0.001f, l_FarClipValue - 0.01f))
        {
            l_Camera.SetNearClip(l_NearClipValue);
            l_FarClipValue = l_Camera.GetFarClip();
        }

        const float l_MinFarClip = l_Camera.GetNearClip() + 0.01f;
        if (ImGui::DragFloat("Far Clip##EditorCamera", &l_FarClipValue, 1.0f, l_MinFarClip, 20000.0f))
        {
            l_Camera.SetFarClip(l_FarClipValue);
        }
        ImGui::PopID();
    }

    void InspectorPanel::DrawMaterialsSection()
    {
        if (!ImGui::CollapsingHeader("Materials", ImGuiTreeNodeFlags_DefaultOpen))
        {
            return;
        }

        std::vector<Trident::Geometry::Material>& l_Materials = Trident::Application::GetRenderer().GetMaterials();
        if (l_Materials.empty())
        {
            ImGui::TextUnformatted("No materials loaded.");
            return;
        }

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

    void InspectorPanel::DrawLiveReloadSection()
    {
        if (!ImGui::CollapsingHeader("Live Reload", ImGuiTreeNodeFlags_DefaultOpen))
        {
            return;
        }

        Trident::Utilities::FileWatcher& l_Watcher = Trident::Utilities::FileWatcher::Get();
        bool l_AutoReload = l_Watcher.IsAutoReloadEnabled();
        if (ImGui::Checkbox("Automatic Reload", &l_AutoReload))
        {
            l_Watcher.EnableAutoReload(l_AutoReload);
        }

        const auto l_StatusToString = [](Trident::Utilities::FileWatcher::ReloadStatus a_Status) -> const char*
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

        const auto l_TypeToString = [](Trident::Utilities::FileWatcher::WatchType a_Type) -> const char*
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
                ImGui::TextUnformatted(l_TypeToString(it_Event.Type));

                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(l_StatusToString(it_Event.Status));

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
}