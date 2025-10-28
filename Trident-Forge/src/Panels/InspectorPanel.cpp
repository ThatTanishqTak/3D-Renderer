#include "InspectorPanel.h"

#include "Renderer/RenderCommand.h"
#include "ECS/Registry.h"
#include "ECS/Components/TagComponent.h"
#include "ECS/Components/TransformComponent.h"
#include "ECS/Components/CameraComponent.h"
#include "ECS/Components/LightComponent.h"
#include "ECS/Components/MeshComponent.h"
#include "ECS/Components/TextureComponent.h"
#include "ECS/Components/SpriteComponent.h"
#include "ECS/Components/ScriptComponent.h"
#include "ECS/Components/AnimationComponent.h"
#include "Core/Utilities.h"

#include <imgui.h>
#include <ImGuizmo.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

#include <glm/gtc/type_ptr.hpp>

void InspectorPanel::SetSelectedEntity(Trident::ECS::Entity entity)
{
    m_SelectedEntity = entity;
    const bool l_HasSelection = m_SelectedEntity != std::numeric_limits<Trident::ECS::Entity>::max();

    // Forward the current selection to the renderer so its gizmo logic targets the active inspector entity.
    Trident::RenderCommand::SetSelectedEntity(m_SelectedEntity);
    if (m_GizmoState != nullptr)
    {
        m_GizmoState->SetSelectionActive(l_HasSelection);
    }
}

void InspectorPanel::SetRegistry(Trident::ECS::Registry* registry)
{
    // Cache the pointer so the inspector continues observing the editor registry while runtime simulation uses a clone.
    m_Registry = registry;
}

void InspectorPanel::SetGizmoState(GizmoState* gizmoState)
{
    // Hold onto the shared gizmo state so radio buttons can drive the viewport overlay.
    m_GizmoState = gizmoState;

    if (m_GizmoState != nullptr)
    {
        const bool l_HasSelection = m_SelectedEntity != std::numeric_limits<Trident::ECS::Entity>::max();
        m_GizmoState->SetSelectionActive(l_HasSelection);
    }
}

void InspectorPanel::Update()
{
    if (m_SelectedEntity == std::numeric_limits<Trident::ECS::Entity>::max())
    {
        // Nothing to validate when no entity is selected.
        return;
    }

    if (m_Registry == nullptr)
    {
        // Without a registry there is nothing to validate; the application layer wires this up during initialization.
        return;
    }

    Trident::ECS::Registry& l_Registry = *m_Registry;
    const std::vector<Trident::ECS::Entity>& l_Entities = l_Registry.GetEntities();
    const bool l_SelectionStillExists = std::find(l_Entities.begin(), l_Entities.end(), m_SelectedEntity) != l_Entities.end();
    if (!l_SelectionStillExists)
    {
        // Clear the cached selection so the inspector avoids dereferencing stale components.
        m_SelectedEntity = std::numeric_limits<Trident::ECS::Entity>::max();
        Trident::RenderCommand::SetSelectedEntity(m_SelectedEntity);
        if (m_GizmoState != nullptr)
        {
            m_GizmoState->SetSelectionActive(false);
        }
    }
}

void InspectorPanel::Render()
{
    if (!ImGui::Begin("Inspector"))
    {
        ImGui::End();

        return;
    }

    if (m_SelectedEntity == std::numeric_limits<Trident::ECS::Entity>::max())
    {
        ImGui::TextWrapped("Select an entity from the Scene Hierarchy to inspect its components.");
        if (m_GizmoState != nullptr)
        {
            // Ensure the viewport hides the gizmo if the selection was cleared while the window was collapsed.
            m_GizmoState->SetSelectionActive(false);
        }
        ImGui::End();

        return;
    }

    if (m_Registry == nullptr)
    {
        ImGui::TextWrapped("Inspector awaiting registry assignment. This hooks up during ApplicationLayer::Initialize().");
        ImGui::End();

        return;
    }

    Trident::ECS::Registry& l_Registry = *m_Registry;

    DrawAddComponentMenu(l_Registry);
    DrawTagComponent(l_Registry);
    DrawTransformComponent(l_Registry);
    DrawCameraComponent(l_Registry);
    DrawLightComponent(l_Registry);
    DrawMeshComponent(l_Registry);
    DrawTextureComponent(l_Registry);
    DrawSpriteComponent(l_Registry);
    DrawAnimationComponent(l_Registry);
    DrawScriptComponent(l_Registry);

    // Future improvement: reflect over registered component types to avoid manual draw helpers.

    ImGui::End();
}

void InspectorPanel::DrawAddComponentMenu(Trident::ECS::Registry& registry)
{
    // Surface a familiar entry point that allows designers to add new behaviour to the entity.
    if (ImGui::Button("Add Component"))
    {
        m_AddComponentSearchBuffer.fill('\0');
        m_ShouldFocusAddComponentSearch = true;
        ImGui::OpenPopup("AddComponentPopup");
    }

    if (ImGui::BeginPopup("AddComponentPopup"))
    {
        // Keep the workflow quick by focusing the search box whenever the popup reopens.
        if (m_ShouldFocusAddComponentSearch)
        {
            ImGui::SetKeyboardFocusHere();
            m_ShouldFocusAddComponentSearch = false;
        }

        ImGui::InputTextWithHint("##AddComponentSearch", "Search components...", m_AddComponentSearchBuffer.data(), m_AddComponentSearchBuffer.size());
        ImGui::Separator();

        bool l_DisplayedAnyComponent = false;
        bool l_ComponentAdded = false;

        // Tag component is optional for runtime entities, so keep it available in the menu.
        if (!l_ComponentAdded && !registry.HasComponent<Trident::TagComponent>(m_SelectedEntity) && PassesAddComponentFilter("Tag"))
        {
            l_DisplayedAnyComponent = true;
            if (ImGui::Selectable("Tag"))
            {
                registry.AddComponent<Trident::TagComponent>(m_SelectedEntity, Trident::TagComponent{});
                l_ComponentAdded = true;
                ImGui::CloseCurrentPopup();
            }
        }

        if (!l_ComponentAdded && !registry.HasComponent<Trident::Transform>(m_SelectedEntity) && PassesAddComponentFilter("Transform"))
        {
            l_DisplayedAnyComponent = true;
            if (ImGui::Selectable("Transform"))
            {
                registry.AddComponent<Trident::Transform>(m_SelectedEntity, Trident::Transform{});
                l_ComponentAdded = true;
                ImGui::CloseCurrentPopup();
            }
        }

        if (!l_ComponentAdded && !registry.HasComponent<Trident::CameraComponent>(m_SelectedEntity) && PassesAddComponentFilter("Camera"))
        {
            l_DisplayedAnyComponent = true;
            if (ImGui::Selectable("Camera"))
            {
                registry.AddComponent<Trident::CameraComponent>(m_SelectedEntity, Trident::CameraComponent{});
                l_ComponentAdded = true;
                ImGui::CloseCurrentPopup();
            }
        }

        if (!l_ComponentAdded && !registry.HasComponent<Trident::MeshComponent>(m_SelectedEntity) && PassesAddComponentFilter("Mesh"))
        {
            l_DisplayedAnyComponent = true;
            if (ImGui::Selectable("Mesh"))
            {
                registry.AddComponent<Trident::MeshComponent>(m_SelectedEntity, Trident::MeshComponent{});
                l_ComponentAdded = true;
                ImGui::CloseCurrentPopup();
            }
        }

        if (!l_ComponentAdded && !registry.HasComponent<Trident::AnimationComponent>(m_SelectedEntity) && PassesAddComponentFilter("Animation"))
        {
            l_DisplayedAnyComponent = true;
            if (ImGui::Selectable("Animation"))
            {
                Trident::AnimationComponent l_DefaultAnimation{};
                l_DefaultAnimation.InvalidateCachedAssets();
                registry.AddComponent<Trident::AnimationComponent>(m_SelectedEntity, l_DefaultAnimation);
                l_ComponentAdded = true;
                ImGui::CloseCurrentPopup();
            }
        }

        if (!l_ComponentAdded && !registry.HasComponent<Trident::TextureComponent>(m_SelectedEntity) && PassesAddComponentFilter("Texture"))
        {
            l_DisplayedAnyComponent = true;
            if (ImGui::Selectable("Texture"))
            {
                registry.AddComponent<Trident::TextureComponent>(m_SelectedEntity, Trident::TextureComponent{});
                l_ComponentAdded = true;
                ImGui::CloseCurrentPopup();
            }
        }

        if (!l_ComponentAdded && !registry.HasComponent<Trident::LightComponent>(m_SelectedEntity) && PassesAddComponentFilter("Light"))
        {
            l_DisplayedAnyComponent = true;
            if (ImGui::Selectable("Light"))
            {
                registry.AddComponent<Trident::LightComponent>(m_SelectedEntity, Trident::LightComponent{});
                l_ComponentAdded = true;
                ImGui::CloseCurrentPopup();
            }
        }

        if (!l_ComponentAdded && !registry.HasComponent<Trident::SpriteComponent>(m_SelectedEntity) && PassesAddComponentFilter("Sprite"))
        {
            l_DisplayedAnyComponent = true;
            if (ImGui::Selectable("Sprite"))
            {
                registry.AddComponent<Trident::SpriteComponent>(m_SelectedEntity, Trident::SpriteComponent{});
                l_ComponentAdded = true;
                ImGui::CloseCurrentPopup();
            }
        }

        if (!l_ComponentAdded && !registry.HasComponent<Trident::ScriptComponent>(m_SelectedEntity) && PassesAddComponentFilter("Script"))
        {
            l_DisplayedAnyComponent = true;
            if (ImGui::Selectable("Script"))
            {
                registry.AddComponent<Trident::ScriptComponent>(m_SelectedEntity, Trident::ScriptComponent{});
                l_ComponentAdded = true;
                ImGui::CloseCurrentPopup();
            }
        }

        if (!l_DisplayedAnyComponent)
        {
            ImGui::TextDisabled("No components match the current search.");
        }

        ImGui::EndPopup();
    }
}

bool InspectorPanel::PassesAddComponentFilter(const char* componentName) const
{
    const std::string l_Query = m_AddComponentSearchBuffer.data();
    if (l_Query.empty())
    {
        return true;
    }

    std::string l_LowerCandidate = componentName;
    std::string l_LowerQuery = l_Query;

    for (char& it_Character : l_LowerCandidate)
    {
        it_Character = static_cast<char>(std::tolower(static_cast<unsigned char>(it_Character)));
    }

    for (char& it_Character : l_LowerQuery)
    {
        it_Character = static_cast<char>(std::tolower(static_cast<unsigned char>(it_Character)));
    }

    return l_LowerCandidate.find(l_LowerQuery) != std::string::npos;
}

void InspectorPanel::DrawTagComponent(Trident::ECS::Registry& registry)
{
    if (!registry.HasComponent<Trident::TagComponent>(m_SelectedEntity))
    {
        return;
    }

    ImGui::PushID("TagComponent");
    const bool l_IsOpen = ImGui::CollapsingHeader("Tag", ImGuiTreeNodeFlags_DefaultOpen);

    if (ImGui::BeginPopupContextItem("TagComponentContext"))
    {
        // Keep removal disabled so the hierarchy always has a readable label.
        ImGui::MenuItem("Remove Component", nullptr, false, false);
        ImGui::EndPopup();
    }

    if (l_IsOpen)
    {
        Trident::TagComponent& l_TagComponent = registry.GetComponent<Trident::TagComponent>(m_SelectedEntity);

        std::array<char, 256> l_TagBuffer{};
        std::strncpy(l_TagBuffer.data(), l_TagComponent.m_Tag.c_str(), l_TagBuffer.size() - 1);

        if (ImGui::InputText("Label", l_TagBuffer.data(), l_TagBuffer.size()))
        {
            // Persist the edited label back onto the component. Rely on std::string to trim trailing nulls.
            l_TagComponent.m_Tag = l_TagBuffer.data();
        }
    }

    ImGui::PopID();
}

void InspectorPanel::DrawTransformComponent(Trident::ECS::Registry& registry)
{
    if (!registry.HasComponent<Trident::Transform>(m_SelectedEntity))
    {
        return;
    }

    ImGui::PushID("TransformComponent");
    const bool l_IsOpen = ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen);

    if (ImGui::BeginPopupContextItem("TransformComponentContext"))
    {
        // Keep transform removal disabled while the renderer assumes every entity has one.
        ImGui::MenuItem("Remove Component", nullptr, false, false);
        ImGui::EndPopup();
    }

    if (l_IsOpen)
    {
        Trident::Transform& l_Transform = registry.GetComponent<Trident::Transform>(m_SelectedEntity);

        // Provide ergonomic controls tuned for editor-like precision.
        ImGui::DragFloat3("Position", glm::value_ptr(l_Transform.Position), 0.1f, -10000.0f, 10000.0f, "%.2f");
        ImGui::DragFloat3("Rotation", glm::value_ptr(l_Transform.Rotation), 0.1f, -360.0f, 360.0f, "%.2f");
        ImGui::DragFloat3("Scale", glm::value_ptr(l_Transform.Scale), 0.01f, 0.0f, 1000.0f, "%.2f");

        if (m_GizmoState != nullptr)
        {
            ImGui::Separator();
            ImGui::TextDisabled("Gizmo Controls");

            // Mirror the viewport radio buttons here so users can change the active operation from the inspector.
            if (ImGui::RadioButton("Translate", m_GizmoState->GetOperation() == ImGuizmo::TRANSLATE))
            {
                m_GizmoState->SetOperation(ImGuizmo::TRANSLATE);
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("Rotate", m_GizmoState->GetOperation() == ImGuizmo::ROTATE))
            {
                m_GizmoState->SetOperation(ImGuizmo::ROTATE);
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("Scale", m_GizmoState->GetOperation() == ImGuizmo::SCALE))
            {
                m_GizmoState->SetOperation(ImGuizmo::SCALE);
            }

            ImGui::SameLine();
            ImGui::TextDisabled("TODO: Snapping / Pivot");

            if (ImGui::RadioButton("Local", m_GizmoState->GetMode() == ImGuizmo::LOCAL))
            {
                m_GizmoState->SetMode(ImGuizmo::LOCAL);
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("World", m_GizmoState->GetMode() == ImGuizmo::WORLD))
            {
                m_GizmoState->SetMode(ImGuizmo::WORLD);
            }

            // Future opportunity: expose snapping controls once the engine supports grid-aligned editing.
        }
    }
    ImGui::PopID();
}

void InspectorPanel::DrawCameraComponent(Trident::ECS::Registry& registry)
{
    if (!registry.HasComponent<Trident::CameraComponent>(m_SelectedEntity))
    {
        return;
    }

    ImGui::PushID("CameraComponent");
    bool l_ShouldRemove = false;
    // Present the component foldout and expose a context menu for removal.
    const bool l_IsOpen = ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen);

    if (ImGui::BeginPopupContextItem("CameraComponentContext"))
    {
        if (ImGui::MenuItem("Remove Component"))
        {
            l_ShouldRemove = true;
        }
        ImGui::EndPopup();
    }

    if (l_IsOpen)
    {
        Trident::CameraComponent& l_CameraComponent = registry.GetComponent<Trident::CameraComponent>(m_SelectedEntity);

        // Switch between projection modes using a familiar combo box.
        int l_ProjectionIndex = static_cast<int>(l_CameraComponent.m_ProjectionType);
        const char* l_ProjectionItems = "Perspective\0Orthographic\0";
        if (ImGui::Combo("Projection", &l_ProjectionIndex, l_ProjectionItems))
        {
            l_CameraComponent.m_ProjectionType = static_cast<Trident::Camera::ProjectionType>(l_ProjectionIndex);
        }

        // Expose common projection properties so designers can tune frustums quickly.
        ImGui::DragFloat("Field of View", &l_CameraComponent.m_FieldOfView, 0.1f, 1.0f, 179.0f, "%.2f");
        ImGui::DragFloat("Orthographic Size", &l_CameraComponent.m_OrthographicSize, 0.1f, 0.0f, 1000.0f, "%.2f");
        ImGui::DragFloat("Near Clip", &l_CameraComponent.m_NearClip, 0.01f, 0.001f, 1000.0f, "%.3f");
        ImGui::DragFloat("Far Clip", &l_CameraComponent.m_FarClip, 1.0f, 0.1f, 10000.0f, "%.2f");
        ImGui::Checkbox("Primary", &l_CameraComponent.m_Primary);
        ImGui::Checkbox("Fixed Aspect Ratio", &l_CameraComponent.m_FixedAspectRatio);
        if (l_CameraComponent.m_FixedAspectRatio)
        {
            ImGui::DragFloat("Aspect Ratio", &l_CameraComponent.m_AspectRatio, 0.01f, 0.1f, 10.0f, "%.2f");
        }
    }

    ImGui::PopID();

    if (l_ShouldRemove)
    {
        registry.RemoveComponent<Trident::CameraComponent>(m_SelectedEntity);
    }
}

void InspectorPanel::DrawLightComponent(Trident::ECS::Registry& registry)
{
    if (!registry.HasComponent<Trident::LightComponent>(m_SelectedEntity))
    {
        return;
    }

    ImGui::PushID("LightComponent");
    bool l_ShouldRemove = false;
    // Provide a foldout for the light settings and offer a removal entry.
    const bool l_IsOpen = ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen);

    if (ImGui::BeginPopupContextItem("LightComponentContext"))
    {
        if (ImGui::MenuItem("Remove Component"))
        {
            l_ShouldRemove = true;
        }
        ImGui::EndPopup();
    }

    if (l_IsOpen)
    {
        Trident::LightComponent& l_LightComponent = registry.GetComponent<Trident::LightComponent>(m_SelectedEntity);

        // Toggle between directional and point lights.
        int l_TypeIndex = static_cast<int>(l_LightComponent.m_Type);
        const char* l_TypeLabels[] = { "Directional", "Point" };
        if (ImGui::Combo("Type", &l_TypeIndex, l_TypeLabels, IM_ARRAYSIZE(l_TypeLabels)))
        {
            l_LightComponent.m_Type = static_cast<Trident::LightComponent::Type>(l_TypeIndex);
        }

        // Provide colour and intensity controls to match common DCC workflows.
        ImGui::ColorEdit3("Color", glm::value_ptr(l_LightComponent.m_Color));
        ImGui::DragFloat("Intensity", &l_LightComponent.m_Intensity, 0.1f, 0.0f, 1000.0f, "%.2f");

        if (l_LightComponent.m_Type == Trident::LightComponent::Type::Directional)
        {
            ImGui::TextDisabled("Directional Settings");
            ImGui::DragFloat3("Direction", glm::value_ptr(l_LightComponent.m_Direction), 0.01f, -1.0f, 1.0f, "%.2f");
        }
        else
        {
            ImGui::TextDisabled("Point Settings");
            ImGui::DragFloat("Range", &l_LightComponent.m_Range, 0.1f, 0.0f, 1000.0f, "%.2f");
        }

        ImGui::Checkbox("Enabled", &l_LightComponent.m_Enabled);
        ImGui::Checkbox("Casts Shadows", &l_LightComponent.m_ShadowCaster);
    }

    ImGui::PopID();

    if (l_ShouldRemove)
    {
        registry.RemoveComponent<Trident::LightComponent>(m_SelectedEntity);
    }
}

void InspectorPanel::DrawMeshComponent(Trident::ECS::Registry& registry)
{
    if (!registry.HasComponent<Trident::MeshComponent>(m_SelectedEntity))
    {
        return;
    }

    ImGui::PushID("MeshComponent");
    bool l_ShouldRemove = false;
    // Mesh entities support removal via the header context menu.
    const bool l_IsOpen = ImGui::CollapsingHeader("Mesh", ImGuiTreeNodeFlags_DefaultOpen);

    if (ImGui::BeginPopupContextItem("MeshComponentContext"))
    {
        if (ImGui::MenuItem("Remove Component"))
        {
            l_ShouldRemove = true;
        }
        ImGui::EndPopup();
    }

    if (l_IsOpen)
    {
        Trident::MeshComponent& l_MeshComponent = registry.GetComponent<Trident::MeshComponent>(m_SelectedEntity);

        // Allow quick toggles for visibility and procedural primitive selection.
        ImGui::Checkbox("Visible", &l_MeshComponent.m_Visible);

        const char* l_PrimitiveLabels[] = { "None", "Cube", "Sphere", "Quad" };
        int l_PrimitiveIndex = static_cast<int>(l_MeshComponent.m_Primitive);
        if (ImGui::Combo("Primitive", &l_PrimitiveIndex, l_PrimitiveLabels, IM_ARRAYSIZE(l_PrimitiveLabels)))
        {
            l_MeshComponent.m_Primitive = static_cast<Trident::MeshComponent::PrimitiveType>(l_PrimitiveIndex);
        }

        // Surface renderer indices for debugging while better asset pickers are pending.
        ImGui::TextDisabled("Mesh Index");
        ImGui::Text("%zu", l_MeshComponent.m_MeshIndex);

        ImGui::TextDisabled("Material Index");
        ImGui::Text("%d", l_MeshComponent.m_MaterialIndex);
    }

    ImGui::PopID();

    if (l_ShouldRemove)
    {
        registry.RemoveComponent<Trident::MeshComponent>(m_SelectedEntity);
    }
}

void InspectorPanel::DrawTextureComponent(Trident::ECS::Registry& registry)
{
    if (!registry.HasComponent<Trident::TextureComponent>(m_SelectedEntity))
    {
        return;
    }

    ImGui::PushID("TextureComponent");
    bool l_ShouldRemove = false;
    const bool l_IsOpen = ImGui::CollapsingHeader("Texture", ImGuiTreeNodeFlags_DefaultOpen);

    if (ImGui::BeginPopupContextItem("TextureComponentContext"))
    {
        if (ImGui::MenuItem("Remove Component"))
        {
            l_ShouldRemove = true;
        }
        ImGui::EndPopup();
    }

    if (l_IsOpen)
    {
        Trident::TextureComponent& l_TextureComponent = registry.GetComponent<Trident::TextureComponent>(m_SelectedEntity);

        std::array<char, 260> l_PathBuffer{};
        std::strncpy(l_PathBuffer.data(), l_TextureComponent.m_TexturePath.c_str(), l_PathBuffer.size() - 1);
        if (ImGui::InputText("Texture Path", l_PathBuffer.data(), l_PathBuffer.size()))
        {
            l_TextureComponent.m_TexturePath = l_PathBuffer.data();
            l_TextureComponent.m_IsDirty = true;
        }

        ImGui::SameLine();
        if (ImGui::Button("Clear"))
        {
            // Clearing the path allows designers to quickly remove an assignment without hunting for the asset.
            l_TextureComponent.m_TexturePath.clear();
            l_TextureComponent.m_TextureSlot = -1;
            l_TextureComponent.m_IsDirty = true;
        }

        // The content browser publishes drag payloads using the CONTENT_BROWSER_ITEM identifier.
        // Future improvement: surface a thumbnail preview once the asset system exposes metadata.
        const ImVec2 l_DropTargetSize = ImVec2(ImGui::GetContentRegionAvail().x, 0.0f);
        ImGui::Button("Drop Texture Asset Here", l_DropTargetSize);

        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* l_Payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
            {
                const char* l_RawData = static_cast<const char*>(l_Payload->Data);
                std::string l_RawPath(l_RawData, l_Payload->DataSize);
                l_RawPath = l_RawPath.c_str();

                std::string l_NormalizedPath = Trident::Utilities::FileManagement::NormalizePath(l_RawPath);
                const std::string l_AssetsPrefix = "Assets/";
                if (l_NormalizedPath.rfind(l_AssetsPrefix, 0) == 0)
                {
                    l_NormalizedPath = l_NormalizedPath.substr(l_AssetsPrefix.length());
                }

                // Store the relative path so the renderer can resolve the asset regardless of working directory.
                l_TextureComponent.m_TexturePath = l_NormalizedPath;
                l_TextureComponent.m_TextureSlot = -1;
                l_TextureComponent.m_IsDirty = true;
            }
            ImGui::EndDragDropTarget();
        }

        if (!l_TextureComponent.m_TexturePath.empty())
        {
            const std::filesystem::path l_PathPreview = l_TextureComponent.m_TexturePath;
            ImGui::TextDisabled("Assigned Relative Path");
            ImGui::TextUnformatted(l_TextureComponent.m_TexturePath.c_str());
            ImGui::TextDisabled("Filename Preview");
            ImGui::TextUnformatted(l_PathPreview.filename().generic_string().c_str());
        }
        else
        {
            ImGui::TextDisabled("No texture selected. Drop an asset or type a path.");
        }

        ImGui::Separator();

        ImGui::TextDisabled("Resolved Slot");
        ImGui::Text("%d", l_TextureComponent.m_TextureSlot);

        // Expose the dirty toggle so artists can request manual reloads while iterating on assets.
        ImGui::Checkbox("Pending Reload", &l_TextureComponent.m_IsDirty);

        if (ImGui::Button("Reload Now"))
        {
            if (!l_TextureComponent.m_TexturePath.empty())
            {
                const int32_t l_NewSlot = Trident::RenderCommand::ResolveTextureSlot(l_TextureComponent.m_TexturePath);
                l_TextureComponent.m_TextureSlot = l_NewSlot;
            }
            else
            {
                l_TextureComponent.m_TextureSlot = 0;
            }

            l_TextureComponent.m_IsDirty = false;
        }

        ImGui::SameLine();
        ImGui::TextDisabled("Future: reclaim unused slots");
    }

    ImGui::PopID();

    if (l_ShouldRemove)
    {
        registry.RemoveComponent<Trident::TextureComponent>(m_SelectedEntity);
    }
}

void InspectorPanel::DrawSpriteComponent(Trident::ECS::Registry& registry)
{
    if (!registry.HasComponent<Trident::SpriteComponent>(m_SelectedEntity))
    {
        return;
    }

    ImGui::PushID("SpriteComponent");
    bool l_ShouldRemove = false;
    // Sprite data is editable and removable from the header context menu.
    const bool l_IsOpen = ImGui::CollapsingHeader("Sprite", ImGuiTreeNodeFlags_DefaultOpen);

    if (ImGui::BeginPopupContextItem("SpriteComponentContext"))
    {
        if (ImGui::MenuItem("Remove Component"))
        {
            l_ShouldRemove = true;
        }
        ImGui::EndPopup();
    }

    if (l_IsOpen)
    {
        Trident::SpriteComponent& l_SpriteComponent = registry.GetComponent<Trident::SpriteComponent>(m_SelectedEntity);

        // Expose texture and colour properties so 2D entities can be tuned in-place.
        std::array<char, 256> l_TextureBuffer{};
        std::strncpy(l_TextureBuffer.data(), l_SpriteComponent.m_TextureId.c_str(), l_TextureBuffer.size() - 1);
        if (ImGui::InputText("Texture", l_TextureBuffer.data(), l_TextureBuffer.size()))
        {
            l_SpriteComponent.m_TextureId = l_TextureBuffer.data();
        }

        ImGui::ColorEdit4("Tint", glm::value_ptr(l_SpriteComponent.m_TintColor));
        ImGui::DragFloat2("UV Scale", glm::value_ptr(l_SpriteComponent.m_UVScale), 0.01f, 0.0f, 10.0f, "%.2f");
        ImGui::DragFloat2("UV Offset", glm::value_ptr(l_SpriteComponent.m_UVOffset), 0.01f, -10.0f, 10.0f, "%.2f");
        ImGui::DragFloat("Tiling", &l_SpriteComponent.m_TilingFactor, 0.01f, 0.0f, 100.0f, "%.2f");
        ImGui::Checkbox("Visible", &l_SpriteComponent.m_Visible);
        ImGui::Checkbox("Use Material Override", &l_SpriteComponent.m_UseMaterialOverride);

        // Store overrides using temporary buffers so ImGui edits remain safe.
        std::array<char, 256> l_MaterialBuffer{};
        std::strncpy(l_MaterialBuffer.data(), l_SpriteComponent.m_MaterialOverrideId.c_str(), l_MaterialBuffer.size() - 1);
        if (ImGui::InputText("Material Override", l_MaterialBuffer.data(), l_MaterialBuffer.size()))
        {
            l_SpriteComponent.m_MaterialOverrideId = l_MaterialBuffer.data();
        }

        ImGui::DragInt2("Atlas Tiles", glm::value_ptr(l_SpriteComponent.m_AtlasTiles), 1, 1, 32);
        ImGui::DragInt("Atlas Index", &l_SpriteComponent.m_AtlasIndex, 1, 0, 1024);
        ImGui::DragFloat("Animation Speed", &l_SpriteComponent.m_AnimationSpeed, 0.01f, 0.0f, 60.0f, "%.2f");
        ImGui::DragFloat("Sort Offset", &l_SpriteComponent.m_SortOffset, 0.01f, -10.0f, 10.0f, "%.2f");
    }

    ImGui::PopID();

    if (l_ShouldRemove)
    {
        registry.RemoveComponent<Trident::SpriteComponent>(m_SelectedEntity);
    }
}

void InspectorPanel::DrawAnimationComponent(Trident::ECS::Registry& registry)
{
    if (!registry.HasComponent<Trident::AnimationComponent>(m_SelectedEntity))
    {
        return;
    }

    ImGui::PushID("AnimationComponent");
    bool l_ShouldRemove = false;
    const bool l_IsOpen = ImGui::CollapsingHeader("Animation", ImGuiTreeNodeFlags_DefaultOpen);

    if (ImGui::BeginPopupContextItem("AnimationComponentContext"))
    {
        if (ImGui::MenuItem("Remove Component"))
        {
            l_ShouldRemove = true;
        }
        ImGui::EndPopup();
    }

    if (l_IsOpen)
    {
        Trident::AnimationComponent& l_AnimationComponent = registry.GetComponent<Trident::AnimationComponent>(m_SelectedEntity);

        // Cache editable fields inside buffers so ImGui can safely manipulate the string data.
        std::array<char, 256> l_SkeletonBuffer{};
        std::strncpy(l_SkeletonBuffer.data(), l_AnimationComponent.m_SkeletonAssetId.c_str(), l_SkeletonBuffer.size() - 1);
        if (ImGui::InputText("Skeleton Asset", l_SkeletonBuffer.data(), l_SkeletonBuffer.size()))
        {
            const std::string l_PreviousSkeleton = l_AnimationComponent.m_SkeletonAssetId;
            l_AnimationComponent.m_SkeletonAssetId = l_SkeletonBuffer.data();
            if (l_AnimationComponent.m_SkeletonAssetId != l_PreviousSkeleton)
            {
                // Reset caches so the runtime resolves the new skeleton cleanly.
                l_AnimationComponent.m_BoneMatrices.clear();
                l_AnimationComponent.m_CurrentTime = 0.0f;
                l_AnimationComponent.InvalidateCachedAssets();
            }
        }

        std::array<char, 256> l_AnimationBuffer{};
        std::strncpy(l_AnimationBuffer.data(), l_AnimationComponent.m_AnimationAssetId.c_str(), l_AnimationBuffer.size() - 1);
        if (ImGui::InputText("Animation Asset", l_AnimationBuffer.data(), l_AnimationBuffer.size()))
        {
            const std::string l_PreviousAnimation = l_AnimationComponent.m_AnimationAssetId;
            l_AnimationComponent.m_AnimationAssetId = l_AnimationBuffer.data();
            if (l_AnimationComponent.m_AnimationAssetId != l_PreviousAnimation)
            {
                l_AnimationComponent.m_BoneMatrices.clear();
                l_AnimationComponent.m_CurrentTime = 0.0f;
                l_AnimationComponent.InvalidateCachedAssets();
            }
        }

        std::array<char, 256> l_ClipBuffer{};
        std::strncpy(l_ClipBuffer.data(), l_AnimationComponent.m_CurrentClip.c_str(), l_ClipBuffer.size() - 1);
        if (ImGui::InputText("Clip", l_ClipBuffer.data(), l_ClipBuffer.size()))
        {
            const std::string l_PreviousClip = l_AnimationComponent.m_CurrentClip;
            l_AnimationComponent.m_CurrentClip = l_ClipBuffer.data();
            if (l_AnimationComponent.m_CurrentClip != l_PreviousClip)
            {
                // Scrub back to the start of the clip and clear cached pose data.
                l_AnimationComponent.m_BoneMatrices.clear();
                l_AnimationComponent.m_CurrentTime = 0.0f;
                l_AnimationComponent.InvalidateCachedAssets();
            }
        }

        // Allow designers to quickly reset the cached pose when the animation looks incorrect.
        if (ImGui::Button("Clear Cached Pose"))
        {
            l_AnimationComponent.m_BoneMatrices.clear();
            l_AnimationComponent.m_CurrentTime = 0.0f;
            l_AnimationComponent.InvalidateCachedAssets();
        }

        float l_PlaybackTime = l_AnimationComponent.m_CurrentTime;
        if (ImGui::DragFloat("Playback Time", &l_PlaybackTime, 0.01f, 0.0f, 10000.0f, "%.2f"))
        {
            l_AnimationComponent.m_CurrentTime = std::max(0.0f, l_PlaybackTime);
        }

        ImGui::DragFloat("Playback Speed", &l_AnimationComponent.m_PlaybackSpeed, 0.01f, -5.0f, 5.0f, "%.2f");
        ImGui::Checkbox("Playing", &l_AnimationComponent.m_IsPlaying);
        ImGui::Checkbox("Looping", &l_AnimationComponent.m_IsLooping);

        ImGui::TextDisabled("Cached Bones");
        ImGui::Text("%zu", l_AnimationComponent.m_BoneMatrices.size());

        ImGui::Separator();
        ImGui::TextDisabled("Future: asset pickers, clip previews, and blend tree editors will streamline this workflow.");
    }

    ImGui::PopID();

    if (l_ShouldRemove)
    {
        registry.RemoveComponent<Trident::AnimationComponent>(m_SelectedEntity);
    }
}

void InspectorPanel::DrawScriptComponent(Trident::ECS::Registry& registry)
{
    if (!registry.HasComponent<Trident::ScriptComponent>(m_SelectedEntity))
    {
        return;
    }

    ImGui::PushID("ScriptComponent");
    bool l_ShouldRemove = false;
    // Allow scripts to be removed via the context menu while editing fields inline.
    const bool l_IsOpen = ImGui::CollapsingHeader("Script", ImGuiTreeNodeFlags_DefaultOpen);

    if (ImGui::BeginPopupContextItem("ScriptComponentContext"))
    {
        if (ImGui::MenuItem("Remove Component"))
        {
            l_ShouldRemove = true;
        }
        ImGui::EndPopup();
    }

    if (l_IsOpen)
    {
        Trident::ScriptComponent& l_ScriptComponent = registry.GetComponent<Trident::ScriptComponent>(m_SelectedEntity);

        // Mirror asset editing workflows via text fields and toggles.
        std::array<char, 256> l_ScriptPathBuffer{};
        std::strncpy(l_ScriptPathBuffer.data(), l_ScriptComponent.m_ScriptPath.c_str(), l_ScriptPathBuffer.size() - 1);
        if (ImGui::InputText("Script Path", l_ScriptPathBuffer.data(), l_ScriptPathBuffer.size()))
        {
            l_ScriptComponent.m_ScriptPath = l_ScriptPathBuffer.data();
        }

        ImGui::Checkbox("Auto Start", &l_ScriptComponent.m_AutoStart);
        ImGui::Checkbox("Running", &l_ScriptComponent.m_IsRunning);
    }

    ImGui::PopID();

    if (l_ShouldRemove)
    {
        registry.RemoveComponent<Trident::ScriptComponent>(m_SelectedEntity);
    }
}