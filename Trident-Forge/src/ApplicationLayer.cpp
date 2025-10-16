#include "ApplicationLayer.h"

#include "Application/Startup.h"
#include "Renderer/RenderCommand.h"

#include "ECS/Registry.h"
#include "ECS/Components/TransformComponent.h"
#include "ECS/Components/TagComponent.h"

#include <imgui.h>
#include <ImGuizmo.h>

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <string>

namespace
{
    /**
     * Compose a model matrix from an ECS transform component so ImGuizmo can manipulate it.
     */
    glm::mat4 ComposeTransformMatrix(const Trident::Transform& transform)
    {
        float l_Translation[3]{ transform.Position.x, transform.Position.y, transform.Position.z };
        float l_Rotation[3]{ transform.Rotation.x, transform.Rotation.y, transform.Rotation.z };
        float l_Scale[3]{ transform.Scale.x, transform.Scale.y, transform.Scale.z };

        glm::mat4 l_Matrix{ 1.0f };
        ImGuizmo::RecomposeMatrixFromComponents(l_Translation, l_Rotation, l_Scale, glm::value_ptr(l_Matrix));
        return l_Matrix;
    }

    /**
     * Decompose a manipulated matrix back into the ECS transform representation.
     */
    void ApplyMatrixToTransform(const glm::mat4& matrix, Trident::Transform& transform)
    {
        float l_Translation[3]{};
        float l_Rotation[3]{};
        float l_Scale[3]{};

        ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(matrix), l_Translation, l_Rotation, l_Scale);

        transform.Position = { l_Translation[0], l_Translation[1], l_Translation[2] };
        transform.Rotation = { l_Rotation[0], l_Rotation[1], l_Rotation[2] };
        transform.Scale = { l_Scale[0], l_Scale[1], l_Scale[2] };
    }

    /**
     * Helper that renders a friendly name for entities even when the tag component is missing.
     */
    std::string BuildEntityLabel(Trident::ECS::Registry& registry, Trident::ECS::Entity entity)
    {
        if (registry.HasComponent<Trident::TagComponent>(entity))
        {
            const auto& l_TagComponent = registry.GetComponent<Trident::TagComponent>(entity);
            return l_TagComponent.m_Tag;
        }

        return "Entity " + std::to_string(entity);
    }
}

void ApplicationLayer::Initialize()
{
    // Capture the engine registry so editor panels can query scene data without touching singletons repeatedly.
    m_Registry = &Trident::Startup::GetRegistry();

    // Default to translate so gizmos behave predictably when the editor opens.
    m_CurrentGizmoOperation = ImGuizmo::TRANSLATE;

    // Resolve the content root relative to the project folder to keep assets accessible in development builds.
    const std::filesystem::path l_EditorRoot = std::filesystem::current_path();
    const std::filesystem::path l_DefaultContent = l_EditorRoot / "Trident-Forge" / "Assets";
    if (std::filesystem::exists(l_DefaultContent))
    {
        m_ContentRoot = l_DefaultContent;
    }
    else
    {
        // Fall back to the working directory so standalone builds still show something useful.
        m_ContentRoot = l_EditorRoot;
    }

    RefreshDirectoryCache();

    // Grab the first entity as the starting selection to provide immediate context in the inspector.
    if (m_Registry != nullptr)
    {
        const auto& l_Entities = m_Registry->GetEntities();
        if (!l_Entities.empty())
        {
            m_SelectedEntity = l_Entities.front();
        }
    }

    // Future improvement: deserialize persisted editor sessions so camera positions and selections survive restarts.
}

void ApplicationLayer::Shutdown()
{
    // Currently the layer only references engine-owned state, so there is nothing explicit to release.
    // Keeping the override lets us expand with editor-owned allocations later (e.g., async asset thumbnails).
    m_ContentEntries.clear();
    m_SelectedEntity = s_InvalidEntity;
}

void ApplicationLayer::Update()
{
    ImGuiIO& l_IO = ImGui::GetIO();

    // Shortcut handling is skipped while ImGui wants the keyboard to avoid interfering with text boxes.
    if (!l_IO.WantCaptureKeyboard)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_T))
        {
            m_CurrentGizmoOperation = ImGuizmo::TRANSLATE;
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_R))
        {
            m_CurrentGizmoOperation = ImGuizmo::ROTATE;
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_S))
        {
            m_CurrentGizmoOperation = ImGuizmo::SCALE;
        }

        if (ImGui::IsKeyPressed(ImGuiKey_F5))
        {
            // Allow artists to refresh the asset listing after external changes without restarting the editor.
            RefreshDirectoryCache();
        }
    }

    // TODO: Integrate camera controls and snapping toggles once the editor exposes more runtime context.
}

void ApplicationLayer::Render()
{
    DrawTitleBar(s_TitleBarHeight);

    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            ImGui::MenuItem("Open Scene", nullptr, false, false);
            ImGui::MenuItem("Save Scene", nullptr, false, false);
            // TODO: Hook into real scene serialization commands once implemented.
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View"))
        {
            ImGui::MenuItem("Reset Layout", nullptr, false, false);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help"))
        {
            ImGui::MenuItem("Documentation", nullptr, false, false);
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    const ImGuiViewport* l_MainViewport = ImGui::GetMainViewport();
    const ImVec2 l_DockPos{ l_MainViewport->Pos.x, l_MainViewport->Pos.y + s_TitleBarHeight };
    const ImVec2 l_DockSize{ l_MainViewport->Size.x, l_MainViewport->Size.y - s_TitleBarHeight };

    ImGui::SetNextWindowPos(l_DockPos);
    ImGui::SetNextWindowSize(l_DockSize);
    ImGui::SetNextWindowViewport(l_MainViewport->ID);

    ImGuiWindowFlags l_WindowFlags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);

    if (ImGui::Begin("DockSpaceRoot", nullptr, l_WindowFlags))
    {
        ImGuiID l_DockspaceID = ImGui::GetID("ApplicationLayerDockSpace");
        ImGui::DockSpace(l_DockspaceID, ImVec2(0.0f, 0.0f));
    }
    ImGui::End();

    ImGui::PopStyleVar(2);

    DrawSceneHierarchy();
    DrawSceneViewport();
    DrawInspector();
    DrawContentBrowser();
}

void ApplicationLayer::DrawTitleBar(float a_TitleBarHeight)
{
    const ImGuiViewport* l_MainViewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(l_MainViewport->Pos);
    ImGui::SetNextWindowSize(ImVec2(l_MainViewport->Size.x, a_TitleBarHeight));
    ImGui::SetNextWindowViewport(l_MainViewport->ID);

    ImGuiWindowFlags l_TitleFlags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);

    if (ImGui::Begin("ApplicationLayerTitleBar", nullptr, l_TitleFlags))
    {
        ImGui::TextUnformatted("Trident Forge");
        ImGui::SameLine();
        ImGui::TextDisabled("// Prototype");

        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();

        std::string l_EntitySummary = "No entity selected";
        if (m_Registry != nullptr && m_SelectedEntity != s_InvalidEntity)
        {
            l_EntitySummary = BuildEntityLabel(*m_Registry, m_SelectedEntity);
        }
        ImGui::TextUnformatted(l_EntitySummary.c_str());

        ImGui::SameLine(ImGui::GetWindowWidth() - 180.0f);
        ImGui::TextDisabled("Frame %llu", static_cast<unsigned long long>(Trident::RenderCommand::GetCurrentFrame()));
    }
    ImGui::End();

    ImGui::PopStyleVar(2);
}

void ApplicationLayer::DrawSceneHierarchy()
{
    if (!ImGui::Begin("Scene Hierarchy"))
    {
        ImGui::End();
        return;
    }

    ImGui::TextWrapped("Double-click to focus an entity. Future revisions can add drag-and-drop reparenting here.");
    ImGui::Separator();

    if (m_Registry == nullptr)
    {
        ImGui::TextUnformatted("Registry unavailable.");
        ImGui::End();
        return;
    }

    const auto& l_Entities = m_Registry->GetEntities();
    for (Trident::ECS::Entity l_Entity : l_Entities)
    {
        const std::string l_Label = BuildEntityLabel(*m_Registry, l_Entity);
        if (ImGui::Selectable(l_Label.c_str(), m_SelectedEntity == l_Entity))
        {
            m_SelectedEntity = l_Entity;
        }
    }

    ImGui::End();
}

void ApplicationLayer::DrawSceneViewport()
{
    if (!ImGui::Begin("Scene"))
    {
        ImGui::End();
        return;
    }

    const ImVec2 l_Available = ImGui::GetContentRegionAvail();
    if (l_Available.x > 0.0f && l_Available.y > 0.0f)
    {
        Trident::ViewportInfo l_ViewportInfo = Trident::RenderCommand::GetViewport();
        const glm::vec2 l_NewSize{ l_Available.x, l_Available.y };
        const glm::vec2 l_NewPosition{ ImGui::GetWindowPos().x, ImGui::GetWindowPos().y };

        if (l_ViewportInfo.Size != l_NewSize || l_ViewportInfo.Position != l_NewPosition)
        {
            l_ViewportInfo.Size = l_NewSize;
            l_ViewportInfo.Position = l_NewPosition;
            Trident::RenderCommand::SetViewport(l_ViewportInfo);
        }

        VkDescriptorSet l_RenderedTexture = Trident::RenderCommand::GetViewportTexture();
        const ImTextureID l_TextureID = reinterpret_cast<ImTextureID>(l_RenderedTexture);
        ImGui::Image(l_TextureID, l_Available, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));

        // Synchronise the gizmo rect with the viewport so user input maps correctly to the rendered image.
        ImGuizmo::SetDrawlist();
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetRect(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y, l_Available.x, l_Available.y);

        if (m_Registry != nullptr && m_SelectedEntity != s_InvalidEntity &&
            m_Registry->HasComponent<Trident::Transform>(m_SelectedEntity))
        {
            Trident::Transform& l_Transform = m_Registry->GetComponent<Trident::Transform>(m_SelectedEntity);
            glm::mat4 l_Model = ComposeTransformMatrix(l_Transform);

            Trident::Camera& l_Camera = Trident::Startup::GetRenderer().GetCamera();
            const glm::mat4 l_View = l_Camera.GetViewMatrix();
            const float l_AspectRatio = l_Available.x / std::max(l_Available.y, 1.0f);
            glm::mat4 l_Projection = glm::perspective(glm::radians(l_Camera.GetFOV()), l_AspectRatio, l_Camera.GetNearClip(), l_Camera.GetFarClip());
            l_Projection[1][1] *= -1.0f; // Adjust for Vulkan's inverted Y clip space to keep gizmos aligned.

            if (ImGuizmo::Manipulate(glm::value_ptr(l_View), glm::value_ptr(l_Projection), m_CurrentGizmoOperation,
                ImGuizmo::LOCAL, glm::value_ptr(l_Model)))
            {
                // Commit the edited transform back to the registry so rendering picks up the changes immediately.
                ApplyMatrixToTransform(l_Model, l_Transform);
            }
        }
    }

    ImGui::End();
}

void ApplicationLayer::DrawInspector()
{
    if (!ImGui::Begin("Inspector"))
    {
        ImGui::End();
        return;
    }

    if (m_Registry == nullptr || m_SelectedEntity == s_InvalidEntity)
    {
        ImGui::TextUnformatted("Select an entity from the hierarchy to edit its components.");
        ImGui::End();
        return;
    }

    if (m_Registry->HasComponent<Trident::Transform>(m_SelectedEntity))
    {
        Trident::Transform& l_Transform = m_Registry->GetComponent<Trident::Transform>(m_SelectedEntity);
        ImGui::TextUnformatted("Transform");
        ImGui::Separator();
        ImGui::DragFloat3("Position", glm::value_ptr(l_Transform.Position), 0.1f, -1000.0f, 1000.0f);
        ImGui::DragFloat3("Rotation", glm::value_ptr(l_Transform.Rotation), 0.5f, -360.0f, 360.0f);
        ImGui::DragFloat3("Scale", glm::value_ptr(l_Transform.Scale), 0.05f, 0.001f, 1000.0f);
    }
    else
    {
        ImGui::TextUnformatted("Transform component missing.");
    }

    ImGui::Separator();
    ImGui::TextWrapped("Future work: expose additional component editors and support undo/redo stacks.");

    ImGui::End();
}

void ApplicationLayer::DrawContentBrowser()
{
    if (!ImGui::Begin("Content Browser"))
    {
        ImGui::End();
        return;
    }

    ImGui::TextWrapped("Content cached from: %s", m_ContentRoot.string().c_str());
    if (ImGui::Button("Refresh"))
    {
        RefreshDirectoryCache();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Assets update live when pressing F5.");

    ImGui::Separator();

    for (const std::filesystem::directory_entry& l_Entry : m_ContentEntries)
    {
        const std::string l_Label = l_Entry.path().filename().string();
        ImGui::Selectable(l_Label.c_str(), false);
    }

    ImGui::TextWrapped("TODO: Thumbnails and drag-and-drop asset instancing would streamline level creation.");

    ImGui::End();
}

void ApplicationLayer::RefreshDirectoryCache()
{
    m_ContentEntries.clear();

    if (m_ContentRoot.empty())
    {
        return;
    }

    std::error_code l_Error{};
    for (const std::filesystem::directory_entry& l_Entry : std::filesystem::directory_iterator(m_ContentRoot, l_Error))
    {
        m_ContentEntries.push_back(l_Entry);
    }

    if (l_Error)
    {
        // Logically we would forward this to the engine logger, but avoiding dependencies keeps this layer self-contained for now.
        return;
    }

    std::sort(m_ContentEntries.begin(), m_ContentEntries.end(),
        [](const std::filesystem::directory_entry& a_Lhs, const std::filesystem::directory_entry& a_Rhs)
        {
            if (a_Lhs.is_directory() == a_Rhs.is_directory())
            {
                return a_Lhs.path().filename().string() < a_Rhs.path().filename().string();
            }
            return a_Lhs.is_directory() && !a_Rhs.is_directory();
        });
}