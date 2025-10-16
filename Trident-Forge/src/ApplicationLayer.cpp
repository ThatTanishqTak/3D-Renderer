#include "ApplicationLayer.h"

#include "Application/Startup.h"
#include "Core/Utilities.h"
#include "Renderer/RenderCommand.h"

#include "ECS/Registry.h"
#include "ECS/Components/TransformComponent.h"
#include "ECS/Components/TagComponent.h"
#include "ECS/Components/MeshComponent.h"
#include "Loader/AssimpExtensions.h"
#include "Loader/ModelLoader.h"

#include <imgui.h>
#include <ImGuizmo.h>

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cctype>
#include <system_error>
#include <string>
#include <utility>

namespace
{
    constexpr char s_ModelPayloadId[] = "TRIDENT_MODEL_FILE"; ///< Drag and drop payload identifier shared between panels.

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

    std::error_code l_CreateDirectoryError{};
    std::filesystem::create_directories(m_ContentRoot, l_CreateDirectoryError);
    if (l_CreateDirectoryError)
    {
        TR_CORE_WARN("Unable to ensure content root '{}' exists: {}", m_ContentRoot.string(), l_CreateDirectoryError.message());
    }

    std::error_code l_CanonicalError{};
    const std::filesystem::path l_CanonicalRoot = std::filesystem::weakly_canonical(m_ContentRoot, l_CanonicalError);
    if (!l_CanonicalError)
    {
        m_ContentRoot = l_CanonicalRoot;
    }

    m_CurrentContentDirectory = m_ContentRoot;

    const std::vector<std::string>& l_SupportedExtensions = Trident::Loader::AssimpExtensions::GetNormalizedExtensions();
    m_ModelExtensions.clear();
    m_ModelExtensions.insert(l_SupportedExtensions.begin(), l_SupportedExtensions.end());

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

void ApplicationLayer::DrawTitleBar(float titleBarHeight)
{
    const ImGuiViewport* l_MainViewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(l_MainViewport->Pos);
    ImGui::SetNextWindowSize(ImVec2(l_MainViewport->Size.x, titleBarHeight));
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

        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* l_Payload = ImGui::AcceptDragDropPayload(s_ModelPayloadId))
            {
                const char* l_PathData = static_cast<const char*>(l_Payload->Data);
                if (l_PathData != nullptr)
                {
                    // Double-clicking a model in the browser mirrors the drag/drop behaviour by reusing the same handler.
                    HandleModelDrop(std::filesystem::path{ l_PathData });
                }
            }
            ImGui::EndDragDropTarget();
        }

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

    ImGui::TextWrapped("Content root: %s", m_ContentRoot.string().c_str());
    if (ImGui::Button("Refresh"))
    {
        RefreshDirectoryCache();
    }

    ImGui::SameLine();
    const bool l_CanNavigateUp = !m_CurrentContentDirectory.empty() && m_CurrentContentDirectory != m_ContentRoot;
    if (!l_CanNavigateUp)
    {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Up"))
    {
        NavigateToDirectory(m_CurrentContentDirectory.parent_path());
    }
    if (!l_CanNavigateUp)
    {
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", m_CurrentContentDirectory.string().c_str());

    ImGui::Separator();
    ImGui::TextWrapped("Double-click a folder to browse it or drag supported models into the viewport to import them.");

    for (const std::filesystem::directory_entry& l_Entry : m_ContentEntries)
    {
        const std::filesystem::path& l_Path = l_Entry.path();
        const bool l_IsDirectory = l_Entry.is_directory();
        const bool l_IsModelFile = !l_IsDirectory && IsModelFile(l_Path);
        const std::string l_PathString = l_Path.string();
        std::string l_Label = l_Path.filename().string();
        if (l_IsDirectory)
        {
            l_Label += "/";
        }

        ImGui::PushID(l_PathString.c_str());
        const bool l_Selected = ImGui::Selectable(l_Label.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick);
        if (l_Selected)
        {
            if (l_IsDirectory)
            {
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                {
                    NavigateToDirectory(l_Path);
                }
            }
            else if (l_IsModelFile && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                HandleModelDrop(l_Path);
            }
        }

        if (l_IsModelFile && ImGui::BeginDragDropSource())
        {
            ImGui::SetDragDropPayload(s_ModelPayloadId, l_PathString.c_str(), l_PathString.size() + 1);
            ImGui::TextUnformatted(l_Label.c_str());
            ImGui::EndDragDropSource();
        }
        ImGui::PopID();
    }

    ImGui::TextWrapped("Future work: async thumbnail generation and asset metadata will make browsing richer.");

    ImGui::End();
}

void ApplicationLayer::RefreshDirectoryCache()
{
    m_ContentEntries.clear();

    if (m_CurrentContentDirectory.empty())
    {
        return;
    }

    std::error_code l_StatusError{};
    if (!std::filesystem::exists(m_CurrentContentDirectory, l_StatusError) || l_StatusError)
    {
        if (!m_ContentRoot.empty() && m_CurrentContentDirectory != m_ContentRoot)
        {
            m_CurrentContentDirectory = m_ContentRoot;
            RefreshDirectoryCache();
        }
        else
        {
            TR_CORE_WARN("Content directory '{}' is unavailable: {}", m_CurrentContentDirectory.string(), l_StatusError.message());
        }
        return;
    }

    std::error_code l_Error{};
    for (const std::filesystem::directory_entry& l_Entry : std::filesystem::directory_iterator(m_CurrentContentDirectory, l_Error))
    {
        m_ContentEntries.push_back(l_Entry);
    }

    if (l_Error)
    {
        TR_CORE_WARN("Failed to enumerate '{}': {}", m_CurrentContentDirectory.string(), l_Error.message());

        return;
    }

    std::sort(m_ContentEntries.begin(), m_ContentEntries.end(),
        [](const std::filesystem::directory_entry& lhs, const std::filesystem::directory_entry& rhs)
        {
            if (lhs.is_directory() == rhs.is_directory())
            {
                return lhs.path().filename().string() < rhs.path().filename().string();
            }
            return lhs.is_directory() && !rhs.is_directory();
        });
}

void ApplicationLayer::NavigateToDirectory(const std::filesystem::path& directory)
{
    if (directory.empty())
    {
        return;
    }

    std::error_code l_StatusError{};
    if (!std::filesystem::exists(directory, l_StatusError) || l_StatusError)
    {
        TR_CORE_WARN("Cannot navigate to missing directory '{}': {}", directory.string(), l_StatusError.message());
        return;
    }

    std::error_code l_TypeError{};
    if (!std::filesystem::is_directory(directory, l_TypeError) || l_TypeError)
    {
        TR_CORE_WARN("Navigation target '{}' is not a directory: {}", directory.string(), l_TypeError.message());
        return;
    }

    if (!IsPathInsideContentRoot(directory))
    {
        TR_CORE_WARN("Blocked navigation outside of content root: {}", directory.string());
        return;
    }

    std::error_code l_CanonicalError{};
    std::filesystem::path l_Target = std::filesystem::weakly_canonical(directory, l_CanonicalError);
    if (l_CanonicalError)
    {
        l_Target = directory;
    }

    if (m_CurrentContentDirectory == l_Target)
    {
        return;
    }

    m_CurrentContentDirectory = l_Target;
    RefreshDirectoryCache();
}

bool ApplicationLayer::IsModelFile(const std::filesystem::path& filePath) const
{
    if (m_ModelExtensions.empty())
    {
        return false;
    }

    std::string l_Extension = filePath.extension().string();
    std::transform(l_Extension.begin(), l_Extension.end(), l_Extension.begin(), [](unsigned char a_Character)
        {
            return static_cast<char>(std::tolower(a_Character));
        });

    return m_ModelExtensions.find(l_Extension) != m_ModelExtensions.end();
}

void ApplicationLayer::HandleModelDrop(const std::filesystem::path& modelPath)
{
    if (modelPath.empty())
    {
        return;
    }

    if (!IsModelFile(modelPath))
    {
        TR_CORE_WARN("Ignored unsupported asset '{}'", modelPath.string());
        return;
    }

    std::error_code l_ExistsError{};
    if (!std::filesystem::exists(modelPath, l_ExistsError) || l_ExistsError)
    {
        TR_CORE_WARN("Dropped model '{}' is unavailable: {}", modelPath.string(), l_ExistsError.message());
        return;
    }

    const std::string l_ModelPathString = modelPath.string();
    Trident::Loader::ModelData l_ModelData = Trident::Loader::ModelLoader::Load(l_ModelPathString);
    if (l_ModelData.Meshes.empty())
    {
        TR_CORE_WARN("Model '{}' did not produce any meshes", l_ModelPathString);
        return;
    }

    const size_t l_FirstNewMeshIndex = m_LoadedMeshes.size();
    const size_t l_MaterialOffset = m_LoadedMaterials.size();

    for (auto& l_Mesh : l_ModelData.Meshes)
    {
        if (l_Mesh.MaterialIndex >= 0)
        {
            l_Mesh.MaterialIndex += static_cast<int>(l_MaterialOffset);
        }
    }

    m_LoadedMeshes.reserve(m_LoadedMeshes.size() + l_ModelData.Meshes.size());
    for (auto& l_Mesh : l_ModelData.Meshes)
    {
        m_LoadedMeshes.push_back(std::move(l_Mesh));
    }

    m_LoadedMaterials.reserve(m_LoadedMaterials.size() + l_ModelData.Materials.size());
    for (auto& l_Material : l_ModelData.Materials)
    {
        m_LoadedMaterials.push_back(std::move(l_Material));
    }

    Trident::ECS::Entity l_FirstNewEntity = s_InvalidEntity;
    if (m_Registry != nullptr)
    {
        const std::string l_BaseName = modelPath.stem().string();
        const size_t l_NewMeshCount = m_LoadedMeshes.size() - l_FirstNewMeshIndex;

        for (size_t l_Index = 0; l_Index < l_NewMeshCount; ++l_Index)
        {
            const size_t l_GlobalMeshIndex = l_FirstNewMeshIndex + l_Index;
            Trident::ECS::Entity l_Entity = m_Registry->CreateEntity();
            m_Registry->AddComponent<Trident::Transform>(l_Entity, Trident::Transform{});
            Trident::MeshComponent& l_MeshComponent = m_Registry->AddComponent<Trident::MeshComponent>(l_Entity);
            l_MeshComponent.m_MeshIndex = l_GlobalMeshIndex;

            Trident::TagComponent& l_TagComponent = m_Registry->AddComponent<Trident::TagComponent>(l_Entity);
            if (l_NewMeshCount == 1)
            {
                l_TagComponent.m_Tag = l_BaseName;
            }
            else
            {
                l_TagComponent.m_Tag = l_BaseName + " [" + std::to_string(l_Index) + "]";
            }

            if (l_FirstNewEntity == s_InvalidEntity)
            {
                l_FirstNewEntity = l_Entity;
            }
        }
    }

    Trident::Startup::GetRenderer().UploadMesh(m_LoadedMeshes, m_LoadedMaterials);

    if (l_FirstNewEntity != s_InvalidEntity)
    {
        m_SelectedEntity = l_FirstNewEntity;
    }

    TR_CORE_INFO("Imported model '{}' ({} meshes)", l_ModelPathString, l_ModelData.Meshes.size());

    // Future improvement: stream geometry and textures incrementally so complex scenes avoid redundant uploads.
}

bool ApplicationLayer::IsPathInsideContentRoot(const std::filesystem::path& directory) const
{
    if (m_ContentRoot.empty())
    {
        return true;
    }

    std::error_code l_RootError{};
    const std::filesystem::path l_RootCanonical = std::filesystem::weakly_canonical(m_ContentRoot, l_RootError);
    if (l_RootError)
    {
        return false;
    }

    std::error_code l_TargetError{};
    const std::filesystem::path l_TargetCanonical = std::filesystem::weakly_canonical(directory, l_TargetError);
    if (l_TargetError)
    {
        return false;
    }

    auto l_TargetIt = l_TargetCanonical.begin();
    for (auto l_RootIt = l_RootCanonical.begin(); l_RootIt != l_RootCanonical.end(); ++l_RootIt, ++l_TargetIt)
    {
        if (l_TargetIt == l_TargetCanonical.end() || *l_TargetIt != *l_RootIt)
        {
            return false;
        }
    }

    return true;
}