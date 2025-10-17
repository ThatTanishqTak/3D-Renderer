#include "ViewportPanel.h"

#include <ImGuizmo.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/common.hpp>
#include <glm/vec4.hpp>

#include "Application/Startup.h"
#include "Camera/CameraComponent.h"
#include "ECS/Registry.h"
#include "ECS/Components/TransformComponent.h"

#include <utility>
#include <algorithm>
#include <cmath>
#include <array>
#include <numeric>
#include <limits>

namespace
{
    glm::mat4 ComposeMatrixFromTransform(const Trident::Transform& a_Transform)
    {
        // Translate the component vectors into a matrix ImGuizmo understands.
        float l_Translation[3]{ a_Transform.Position.x, a_Transform.Position.y, a_Transform.Position.z };
        float l_Rotation[3]{ a_Transform.Rotation.x, a_Transform.Rotation.y, a_Transform.Rotation.z };
        float l_Scale[3]{ a_Transform.Scale.x, a_Transform.Scale.y, a_Transform.Scale.z };

        glm::mat4 l_Model{ 1.0f };
        ImGuizmo::RecomposeMatrixFromComponents(l_Translation, l_Rotation, l_Scale, glm::value_ptr(l_Model));

        return l_Model;
    }

    Trident::Transform DecomposeMatrixToTransform(const glm::mat4& a_ModelMatrix)
    {
        // Break down the manipulated matrix so we can feed the renderer component data again.
        float l_Translation[3]{};
        float l_Rotation[3]{};
        float l_Scale[3]{};
        ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(a_ModelMatrix), l_Translation, l_Rotation, l_Scale);

        Trident::Transform l_Result{};
        l_Result.Position = { l_Translation[0], l_Translation[1], l_Translation[2] };
        l_Result.Rotation = { l_Rotation[0], l_Rotation[1], l_Rotation[2] };
        l_Result.Scale = { l_Scale[0], l_Scale[1], l_Scale[2] };

        return l_Result;
    }

    constexpr std::array<glm::vec3, 8> s_SceneGizmoVertices
    {
        glm::vec3{ -0.5f, -0.5f, -0.5f },
        glm::vec3{ 0.5f, -0.5f, -0.5f },
        glm::vec3{ 0.5f, 0.5f, -0.5f },
        glm::vec3{ -0.5f, 0.5f, -0.5f },
        glm::vec3{ -0.5f, -0.5f, 0.5f },
        glm::vec3{ 0.5f, -0.5f, 0.5f },
        glm::vec3{ 0.5f, 0.5f, 0.5f },
        glm::vec3{ -0.5f, 0.5f, 0.5f }
    };

    struct SceneGizmoFace
    {
        std::array<int32_t, 4> Indices{};
        glm::vec3 Normal{ 0.0f };
        glm::vec3 Direction{ 0.0f };
        ImU32 Color = IM_COL32_WHITE;
        const char* Label = nullptr;
    };

    constexpr std::array<SceneGizmoFace, 6> s_SceneGizmoFaces
    {
        SceneGizmoFace{ { 4, 5, 6, 7 }, glm::vec3{ 0.0f, 1.0f, 0.0f }, glm::vec3{ 0.0f, 1.0f, 0.0f }, IM_COL32(129, 199, 132, 255), "+Y" },
        SceneGizmoFace{ { 0, 1, 2, 3 }, glm::vec3{ 0.0f, -1.0f, 0.0f }, glm::vec3{ 0.0f, -1.0f, 0.0f }, IM_COL32(56, 142, 60, 200), "-Y" },
        SceneGizmoFace{ { 1, 5, 6, 2 }, glm::vec3{ 1.0f, 0.0f, 0.0f }, glm::vec3{ 1.0f, 0.0f, 0.0f }, IM_COL32(229, 115, 115, 255), "+X" },
        SceneGizmoFace{ { 0, 3, 7, 4 }, glm::vec3{ -1.0f, 0.0f, 0.0f }, glm::vec3{ -1.0f, 0.0f, 0.0f }, IM_COL32(183, 28, 28, 220), "-X" },
        SceneGizmoFace{ { 3, 2, 6, 7 }, glm::vec3{ 0.0f, 0.0f, 1.0f }, glm::vec3{ 0.0f, 0.0f, 1.0f }, IM_COL32(100, 181, 246, 255), "+Z" },
        SceneGizmoFace{ { 0, 1, 5, 4 }, glm::vec3{ 0.0f, 0.0f, -1.0f }, glm::vec3{ 0.0f, 0.0f, -1.0f }, IM_COL32(21, 101, 192, 220), "-Z" }
    };

    struct SceneGizmoCorner
    {
        int32_t VertexIndex = 0;
        glm::vec3 Direction{ 0.0f };
    };

    constexpr std::array<SceneGizmoCorner, 8> s_SceneGizmoCorners
    {
        SceneGizmoCorner{ 6, glm::normalize(glm::vec3{ 1.0f, 1.0f, 1.0f }) },
        SceneGizmoCorner{ 5, glm::normalize(glm::vec3{ 1.0f, -1.0f, 1.0f }) },
        SceneGizmoCorner{ 7, glm::normalize(glm::vec3{ -1.0f, 1.0f, 1.0f }) },
        SceneGizmoCorner{ 4, glm::normalize(glm::vec3{ -1.0f, -1.0f, 1.0f }) },
        SceneGizmoCorner{ 2, glm::normalize(glm::vec3{ 1.0f, 1.0f, -1.0f }) },
        SceneGizmoCorner{ 1, glm::normalize(glm::vec3{ 1.0f, -1.0f, -1.0f }) },
        SceneGizmoCorner{ 3, glm::normalize(glm::vec3{ -1.0f, 1.0f, -1.0f }) },
        SceneGizmoCorner{ 0, glm::normalize(glm::vec3{ -1.0f, -1.0f, -1.0f }) }
    };

    bool IsPointInsideTriangle(const ImVec2& a_Point, const ImVec2& a_A, const ImVec2& a_B, const ImVec2& c)
    {
        const float l_Denominator = ((a_B.y - c.y) * (a_A.x - c.x)) + ((c.x - a_B.x) * (a_A.y - c.y));
        const float l_MinArea = std::numeric_limits<float>::epsilon();
        if (std::abs(l_Denominator) <= l_MinArea)
        {
            return false;
        }

        const float l_W1 = ((a_B.y - c.y) * (a_Point.x - c.x) + (c.x - a_B.x) * (a_Point.y - c.y)) / l_Denominator;
        const float l_W2 = ((c.y - a_A.y) * (a_Point.x - c.x) + (a_A.x - c.x) * (a_Point.y - c.y)) / l_Denominator;
        const float l_W3 = 1.0f - l_W1 - l_W2;

        const float l_Epsilon = -0.0001f;
        return (l_W1 >= l_Epsilon) && (l_W2 >= l_Epsilon) && (l_W3 >= l_Epsilon);
    }
}

ViewportPanel::ViewportPanel()
{
    // Gizmo configuration is now shared, so default setup occurs when the inspector connects.
}

// Update prepares the viewport for rendering by reading ImGui input state and
// pushing the currently selected camera down to the renderer.
void ViewportPanel::Update()
{
    // Query ImGui for the current IO state so we can decide if camera controls should run.
    ImGuiIO& l_IO = ImGui::GetIO();

    // The viewport only wants exclusive controls when the window is focused/hovered and
    // ImGui is not already consuming mouse or keyboard input. Additional tools can use
    // m_IsCameraControlEnabled to decide if they should respond this frame.
    m_IsCameraControlEnabled = m_IsFocused && m_IsHovered && !l_IO.WantCaptureMouse && !l_IO.WantCaptureKeyboard;

    // Keep the orbit pivot aligned with the active selection so navigation feels anchored.
    UpdateCameraPivotFromSelection();

    // Drive the editor camera using the gathered input state for this frame.
    HandleCameraInput(l_IO);

    // Forward the active camera entity to the renderer so off-screen render targets stay in sync.
    if (m_ActiveCameraEntity != 0)
    {
        Trident::RenderCommand::SetViewportCamera(m_ActiveCameraEntity);
    }
}

// Render draws the ImGui viewport window and displays the renderer-provided texture.
void ViewportPanel::Render()
{
    // Build the main viewport window. Additional dockspace integration can rename this later.
    if (!ImGui::Begin("Scene"))
    {
        // If the window is collapsed we still need to clear state so camera controls disable correctly.
        m_IsFocused = false;
        m_IsHovered = false;
        ImGui::End();
        return;
    }

    // Record whether the viewport wants mouse/keyboard focus so other editor systems stay polite.
    m_IsFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
    m_IsHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);

    // Surface camera utility controls before drawing the viewport texture.
    if (ImGui::Button("Sync to Runtime Camera"))
    {
        // Copy the runtime camera pose into the editor controller without mutating the ECS entity.
        SyncRuntimeCameraToEditor();
    }
    ImGui::SameLine();
    ImGui::Text("Fly Speed: %.2f", m_CameraController.GetFlySpeed());

    // Determine how much space remains for the scene texture after toolbar controls are rendered.
    const ImVec2 l_ContentRegion = ImGui::GetContentRegionAvail();
    const glm::vec2 l_NewViewportSize{ l_ContentRegion.x, l_ContentRegion.y };

    // Skip rendering when the panel is hidden or sized to zero (docking transitions, layout edits, etc.).
    if (l_NewViewportSize.x > 0.0f && l_NewViewportSize.y > 0.0f)
    {
        const ImVec2 l_ViewportPos = ImGui::GetCursorScreenPos();
        
        // Persist the on-screen bounds of the viewport so drag-and-drop handlers can
        // later determine whether a file drop landed inside the rendered image.
        m_ViewportBoundsMin = l_ViewportPos;
        m_ViewportBoundsMax = ImVec2(l_ViewportPos.x + l_ContentRegion.x, l_ViewportPos.y + l_ContentRegion.y);

        // Reconfigure the renderer whenever the viewport size changes.
        if (l_NewViewportSize != m_CachedViewportSize)
        {
            Trident::ViewportInfo l_Info{};
            l_Info.ViewportID = m_ViewportID;
            l_Info.Position = { l_ViewportPos.x, l_ViewportPos.y };
            l_Info.Size = l_NewViewportSize;
            Trident::RenderCommand::SetViewport(l_Info);

            m_CachedViewportSize = l_NewViewportSize;
        }

        // Pull the Vulkan descriptor that ImGui understands so we can blit the scene into the panel.
        const VkDescriptorSet l_Descriptor = Trident::RenderCommand::GetViewportTexture();
        if (l_Descriptor != VK_NULL_HANDLE)
        {
            // Reset ImGuizmo's per-frame state before drawing the viewport so overlays start cleanly.
            ImGuizmo::BeginFrame();
            // Draw the viewport texture. UVs are flipped vertically so the render target appears correct.
            ImGui::Image(reinterpret_cast<ImTextureID>(l_Descriptor), l_ContentRegion, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
            if (ImGui::BeginDragDropTarget())
            {
                const ImGuiPayload* l_Payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM");
                if (l_Payload != nullptr && l_Payload->Data != nullptr && l_Payload->DataSize > 0)
                {
                    const char* l_PathData = static_cast<const char*>(l_Payload->Data);
                    std::string l_PathString{ l_PathData, l_PathData + (l_Payload->DataSize - 1) };

                    if (m_OnAssetDrop)
                    {
                        std::vector<std::string> l_DroppedPaths{};
                        l_DroppedPaths.emplace_back(std::move(l_PathString));
                        m_OnAssetDrop(l_DroppedPaths);
                    }
                    // TODO: Support batched payloads once the content browser exposes multi-selection drags.
                }
                ImGui::EndDragDropTarget();
            }
            // Configure ImGuizmo so it renders directly on top of the viewport image.
            ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
            ImGuizmo::SetRect(l_ViewportPos.x, l_ViewportPos.y, l_ContentRegion.x, l_ContentRegion.y);

            const bool l_CanDisplayGizmo = (m_GizmoState != nullptr) && m_GizmoState->HasSelection();
            if (l_CanDisplayGizmo)
            {
                // Fetch the camera matrices used for the rendered image so the gizmo aligns perfectly.
                const glm::mat4 l_ViewMatrix = Trident::RenderCommand::GetViewportViewMatrix();
                const glm::mat4 l_ProjectionMatrix = Trident::RenderCommand::GetViewportProjectionMatrix();

                // Retrieve the active selection transform and hand it to ImGuizmo for manipulation.
                const Trident::Transform l_CurrentTransform = Trident::RenderCommand::GetTransform();
                glm::mat4 l_ModelMatrix = ComposeMatrixFromTransform(l_CurrentTransform);

                // Drive the gizmo and push edits back into the renderer when the user drags the handles.
                if (ImGuizmo::Manipulate(glm::value_ptr(l_ViewMatrix), glm::value_ptr(l_ProjectionMatrix), m_GizmoState->GetOperation(), m_GizmoState->GetMode(), 
                    glm::value_ptr(l_ModelMatrix)))
                {
                    const Trident::Transform l_UpdatedTransform = DecomposeMatrixToTransform(l_ModelMatrix);
                    Trident::RenderCommand::SetTransform(l_UpdatedTransform);
                }

                // While the gizmo is active we suspend camera controls so inputs are not double-consumed.
                if (ImGuizmo::IsUsing())
                {
                    m_IsCameraControlEnabled = false;
                }
            }
            const bool l_SceneGizmoCapturedInput = RenderSceneGizmoOverlay(l_ViewportPos, l_ContentRegion);
            if (l_SceneGizmoCapturedInput)
            {
                m_IsCameraControlEnabled = false;
            }
        }
        else
        {
            // TODO: Provide a debug overlay here to explain missing render data.
        }
    }
    else
    {
        // When the viewport is hidden or collapsed we clear the cached bounds so stale
        // rectangles do not report false positives for drag-and-drop hit tests.
        m_ViewportBoundsMin = ImVec2(0.0f, 0.0f);
        m_ViewportBoundsMax = ImVec2(0.0f, 0.0f);
        // TODO: Account for multi-viewport/HiDPI scaling adjustments before submitting viewport info.
    }

    // Future contributors can add overlay or metrics widgets below before closing the window.
    ImGui::End();
}

// SetCameraEntity stores the active camera and notifies the renderer immediately so the viewport updates.
void ViewportPanel::SetCameraEntity(Trident::ECS::Entity cameraEntity)
{
    m_ActiveCameraEntity = cameraEntity;

    if (m_ActiveCameraEntity != 0)
    {
        Trident::RenderCommand::SetViewportCamera(m_ActiveCameraEntity);
    }
    // TODO: Allow clearing the camera when switching to runtime play mode.
}

void ViewportPanel::SetGizmoState(GizmoState* gizmoState)
{
    // Cache the shared gizmo configuration so the viewport can react to inspector changes.
    m_GizmoState = gizmoState;
}

void ViewportPanel::SetSelectedEntity(Trident::ECS::Entity selectedEntity)
{
    // Store the hierarchy/inspector selection so navigation can track the same pivot.
    m_SelectedEntity = selectedEntity;
}

void ViewportPanel::SetAssetDropHandler(std::function<void(const std::vector<std::string>&)> assetDropHandler)
{
    // Store the callback so the application layer can process accepted payloads.
    m_OnAssetDrop = std::move(assetDropHandler);
}

bool ViewportPanel::RenderSceneGizmoOverlay(const ImVec2& viewportPos, const ImVec2& viewportSize)
{
    ImDrawList* l_DrawList = ImGui::GetWindowDrawList();
    if (l_DrawList == nullptr)
    {
        return false;
    }

    const float l_Margin = 16.0f;
    const float l_MaxHorizontal = viewportSize.x - (l_Margin * 2.0f);
    const float l_MaxVertical = viewportSize.y - (l_Margin * 2.0f);
    const float l_MinimumSize = 60.0f;
    if (l_MaxHorizontal < l_MinimumSize || l_MaxVertical < l_MinimumSize)
    {
        return false;
    }

    const float l_TotalSize = std::min(110.0f, std::min(l_MaxHorizontal, l_MaxVertical));
    const ImVec2 l_GizmoMin{ viewportPos.x + viewportSize.x - l_TotalSize - l_Margin, viewportPos.y + l_Margin };
    const ImVec2 l_GizmoMax{ l_GizmoMin.x + l_TotalSize, l_GizmoMin.y + l_TotalSize };

    const float l_ModeReservedHeight = std::min(22.0f, l_TotalSize * 0.3f);
    const float l_ModeSpacing = 4.0f;
    const float l_CubeHeight = std::max(l_TotalSize - l_ModeReservedHeight - l_ModeSpacing, 10.0f);
    const ImVec2 l_CubeMin{ l_GizmoMin.x, l_GizmoMin.y };
    const ImVec2 l_CubeMax{ l_GizmoMax.x, l_GizmoMin.y + l_CubeHeight };
    const ImVec2 l_Center{ (l_CubeMin.x + l_CubeMax.x) * 0.5f, (l_CubeMin.y + l_CubeMax.y) * 0.5f };
    const float l_Radius = std::max((l_CubeHeight * 0.5f) - 8.0f, 4.0f);

    // Background panel to keep the cube readable regardless of the underlying scene.
    l_DrawList->AddRectFilled(l_GizmoMin, l_GizmoMax, IM_COL32(24, 26, 30, 220), 8.0f);
    l_DrawList->AddRect(l_GizmoMin, l_GizmoMax, IM_COL32(180, 180, 190, 80), 8.0f);

    const glm::vec3 l_Right = glm::normalize(m_CameraController.GetRight());
    const glm::vec3 l_Up = glm::normalize(m_CameraController.GetUp());
    const glm::vec3 l_Forward = glm::normalize(m_CameraController.GetForward());
    const glm::mat3 l_CameraBasis{ l_Right, l_Up, l_Forward };
    const glm::mat3 l_WorldToCamera = glm::transpose(l_CameraBasis);

    std::array<ImVec2, s_SceneGizmoVertices.size()> l_Project{};
    std::array<float, s_SceneGizmoVertices.size()> l_Depths{};
    for (size_t it_Index = 0; it_Index < s_SceneGizmoVertices.size(); ++it_Index)
    {
        const glm::vec3& l_Vertex = s_SceneGizmoVertices[it_Index];
        const glm::vec3 l_CameraSpace = l_WorldToCamera * l_Vertex;
        const glm::vec3 l_Normalised = l_CameraSpace * 2.0f; // Normalise cube extents to [-1, 1].
        const float l_Depth = glm::clamp(l_Normalised.z, -1.0f, 1.0f);
        const float l_DepthScale = 0.65f + (l_Depth * 0.35f);
        const ImVec2 l_Offset{ l_Normalised.x * l_Radius * l_DepthScale, -l_Normalised.y * l_Radius * l_DepthScale };

        l_Project[it_Index] = ImVec2(l_Center.x + l_Offset.x, l_Center.y + l_Offset.y);
        l_Depths[it_Index] = l_Depth;
    }

    struct FaceRenderState
    {
        const SceneGizmoFace* Face = nullptr;
        std::array<ImVec2, 4> Points{};
        float Depth = 0.0f;
        ImVec2 Center{ 0.0f, 0.0f };
        bool FacingCamera = false;
    };

    std::array<FaceRenderState, s_SceneGizmoFaces.size()> l_Faces{};
    for (size_t it_Index = 0; it_Index < s_SceneGizmoFaces.size(); ++it_Index)
    {
        const SceneGizmoFace& l_Face = s_SceneGizmoFaces[it_Index];
        FaceRenderState& l_State = l_Faces[it_Index];
        l_State.Face = &l_Face;

        float l_DepthSum = 0.0f;
        ImVec2 l_CenterAccumulator{ 0.0f, 0.0f };
        for (size_t it_Corner = 0; it_Corner < l_Face.Indices.size(); ++it_Corner)
        {
            const int32_t l_VertexIndex = l_Face.Indices[it_Corner];
            l_State.Points[it_Corner] = l_Project[static_cast<size_t>(l_VertexIndex)];
            l_DepthSum += l_Depths[static_cast<size_t>(l_VertexIndex)];
            l_CenterAccumulator.x += l_State.Points[it_Corner].x;
            l_CenterAccumulator.y += l_State.Points[it_Corner].y;
        }

        l_State.Depth = l_DepthSum / static_cast<float>(l_Face.Indices.size());
        l_State.Center.x = l_CenterAccumulator.x / static_cast<float>(l_Face.Indices.size());
        l_State.Center.y = l_CenterAccumulator.y / static_cast<float>(l_Face.Indices.size());
        l_State.FacingCamera = glm::dot(l_Face.Normal, l_Forward) >= 0.0f;
    }

    std::array<size_t, s_SceneGizmoFaces.size()> l_DrawOrder{};
    std::iota(l_DrawOrder.begin(), l_DrawOrder.end(), 0);
    std::sort(l_DrawOrder.begin(), l_DrawOrder.end(), [&l_Faces](size_t a_Left, size_t a_Right)
        {
            return l_Faces[a_Left].Depth < l_Faces[a_Right].Depth;
        });

    const ImGuiIO& l_IO = ImGui::GetIO();
    const ImVec2 l_MousePos = l_IO.MousePos;
    const bool l_MouseInside = (l_MousePos.x >= l_GizmoMin.x) && (l_MousePos.x <= l_GizmoMax.x) && (l_MousePos.y >= l_GizmoMin.y) && (l_MousePos.y <= l_GizmoMax.y);

    const SceneGizmoFace* l_HoveredFace = nullptr;
    if (l_MouseInside)
    {
        for (auto it_Iterator = l_DrawOrder.rbegin(); it_Iterator != l_DrawOrder.rend(); ++it_Iterator)
        {
            FaceRenderState& l_State = l_Faces[*it_Iterator];
            if (!l_State.FacingCamera)
            {
                continue;
            }

            const bool l_InFirstTriangle = IsPointInsideTriangle(l_MousePos, l_State.Points[0], l_State.Points[1], l_State.Points[2]);
            const bool l_InSecondTriangle = IsPointInsideTriangle(l_MousePos, l_State.Points[0], l_State.Points[2], l_State.Points[3]);
            if (l_InFirstTriangle || l_InSecondTriangle)
            {
                l_HoveredFace = l_State.Face;
                break;
            }
        }
    }

    const SceneGizmoCorner* l_HoveredCorner = nullptr;
    if (l_MouseInside)
    {
        const float l_CornerRadius = 10.0f;
        float l_BestDistance = l_CornerRadius * l_CornerRadius;
        for (const SceneGizmoCorner& it_Corner : s_SceneGizmoCorners)
        {
            const ImVec2 l_CornerPos = l_Project[static_cast<size_t>(it_Corner.VertexIndex)];
            const float l_DeltaX = l_MousePos.x - l_CornerPos.x;
            const float l_DeltaY = l_MousePos.y - l_CornerPos.y;
            const float l_Distance = (l_DeltaX * l_DeltaX) + (l_DeltaY * l_DeltaY);
            if (l_Distance < l_BestDistance)
            {
                l_BestDistance = l_Distance;
                l_HoveredCorner = &it_Corner;
            }
        }
    }

    bool l_InputCaptured = false;

    for (size_t it_Index : l_DrawOrder)
    {
        FaceRenderState& l_State = l_Faces[it_Index];
        const bool l_IsHovered = (l_State.Face == l_HoveredFace) && (l_HoveredCorner == nullptr);

        ImVec4 l_FaceColor = ImGui::ColorConvertU32ToFloat4(l_State.Face->Color);
        const float l_DepthInfluence = 0.6f + (glm::clamp(l_State.Depth, -1.0f, 1.0f) * 0.25f);
        l_FaceColor.x *= l_DepthInfluence;
        l_FaceColor.y *= l_DepthInfluence;
        l_FaceColor.z *= l_DepthInfluence;
        l_FaceColor.w = l_State.FacingCamera ? 0.90f : 0.30f;
        if (l_IsHovered)
        {
            l_FaceColor.x = std::min(l_FaceColor.x * 1.2f, 1.0f);
            l_FaceColor.y = std::min(l_FaceColor.y * 1.2f, 1.0f);
            l_FaceColor.z = std::min(l_FaceColor.z * 1.2f, 1.0f);
            l_FaceColor.w = 1.0f;
        }

        const ImU32 l_FillColor = ImGui::ColorConvertFloat4ToU32(l_FaceColor);
        l_DrawList->AddConvexPolyFilled(l_State.Points.data(), static_cast<int>(l_State.Points.size()), l_FillColor);
        l_DrawList->AddPolyline(l_State.Points.data(), static_cast<int>(l_State.Points.size()), IM_COL32(15, 15, 18, 200), true, 1.2f);

        if (l_State.Face->Label != nullptr)
        {
            const ImVec2 l_TextSize = ImGui::CalcTextSize(l_State.Face->Label);
            const ImVec2 l_TextPos{ l_State.Center.x - (l_TextSize.x * 0.5f), l_State.Center.y - (l_TextSize.y * 0.5f) };
            const ImU32 l_TextColor = l_IsHovered ? IM_COL32(255, 255, 255, 255) : (l_State.FacingCamera ? IM_COL32(235, 235, 240, 235) : IM_COL32(180, 180, 190, 100));
            l_DrawList->AddText(l_TextPos, l_TextColor, l_State.Face->Label);
        }
    }

    for (const SceneGizmoCorner& it_Corner : s_SceneGizmoCorners)
    {
        const ImVec2 l_CornerPos = l_Project[static_cast<size_t>(it_Corner.VertexIndex)];
        const float l_Depth = glm::clamp(l_Depths[static_cast<size_t>(it_Corner.VertexIndex)], -1.0f, 1.0f);
        ImVec4 l_Color = ImVec4(0.75f, 0.75f, 0.78f, 0.45f + (std::max(0.0f, l_Depth) * 0.45f));
        const bool l_IsHovered = (&it_Corner == l_HoveredCorner);
        if (l_IsHovered)
        {
            l_Color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        }

        const ImU32 l_FillColor = ImGui::ColorConvertFloat4ToU32(l_Color);
        l_DrawList->AddCircleFilled(l_CornerPos, 5.5f, l_FillColor, 12);
        l_DrawList->AddCircle(l_CornerPos, 5.5f, IM_COL32(20, 22, 26, 220), 12, 1.0f);
    }

    if (l_HoveredCorner != nullptr || l_HoveredFace != nullptr)
    {
        l_InputCaptured = true;
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            if (l_HoveredCorner != nullptr)
            {
                m_CameraController.SnapToDirection(l_HoveredCorner->Direction, glm::vec3{ 0.0f, 0.0f, 1.0f });
            }
            else if (l_HoveredFace != nullptr)
            {
                const glm::vec3 l_UpHint = (std::abs(l_HoveredFace->Direction.z) > 0.9f) ? glm::vec3{ 0.0f, 1.0f, 0.0f } : glm::vec3{ 0.0f, 0.0f, 1.0f };
                m_CameraController.SnapToDirection(l_HoveredFace->Direction, l_UpHint);
            }
        }
    }

    const ImVec2 l_ModeMin{ l_GizmoMin.x + 8.0f, l_CubeMax.y + 4.0f };
    const ImVec2 l_ModeMax{ l_GizmoMax.x - 8.0f, l_GizmoMax.y - 6.0f };
    const bool l_ModeHovered = (l_MousePos.x >= l_ModeMin.x) && (l_MousePos.x <= l_ModeMax.x) && (l_MousePos.y >= l_ModeMin.y) && (l_MousePos.y <= l_ModeMax.y);
    if (l_ModeHovered)
    {
        l_InputCaptured = true;
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            m_CameraController.ToggleProjection();
        }
    }

    ImVec4 l_ModeColor = m_CameraController.IsOrthographic() ? ImVec4(0.22f, 0.44f, 0.90f, 0.85f) : ImVec4(0.28f, 0.30f, 0.36f, 0.85f);
    if (l_ModeHovered)
    {
        l_ModeColor.x = std::min(l_ModeColor.x * 1.1f, 1.0f);
        l_ModeColor.y = std::min(l_ModeColor.y * 1.1f, 1.0f);
        l_ModeColor.z = std::min(l_ModeColor.z * 1.1f, 1.0f);
        l_ModeColor.w = 1.0f;
    }

    l_DrawList->AddRectFilled(l_ModeMin, l_ModeMax, ImGui::ColorConvertFloat4ToU32(l_ModeColor), 4.0f);
    l_DrawList->AddRect(l_ModeMin, l_ModeMax, IM_COL32(200, 200, 210, 120), 4.0f);

    const char* l_ModeLabel = m_CameraController.IsOrthographic() ? "Orthographic" : "Perspective";
    const ImVec2 l_ModeTextSize = ImGui::CalcTextSize(l_ModeLabel);
    const ImVec2 l_ModeTextPos{ (l_ModeMin.x + l_ModeMax.x - l_ModeTextSize.x) * 0.5f, (l_ModeMin.y + l_ModeMax.y - l_ModeTextSize.y) * 0.5f };
    const ImU32 l_ModeTextColor = IM_COL32(245, 245, 250, 240);
    l_DrawList->AddText(l_ModeTextPos, l_ModeTextColor, l_ModeLabel);

    return l_InputCaptured;
}

bool ViewportPanel::ContainsPoint(const ImVec2& point) const
{
    // Use the cached ImGui rectangle to verify if a screen-space point lies within the viewport.
    const bool l_HasValidBounds = (m_ViewportBoundsMax.x > m_ViewportBoundsMin.x) && (m_ViewportBoundsMax.y > m_ViewportBoundsMin.y);
    if (!l_HasValidBounds)
    {
        return false;
    }

    const bool l_WithinHorizontal = (point.x >= m_ViewportBoundsMin.x) && (point.x <= m_ViewportBoundsMax.x);
    const bool l_WithinVertical = (point.y >= m_ViewportBoundsMin.y) && (point.y <= m_ViewportBoundsMax.y);

    return l_WithinHorizontal && l_WithinVertical;
}

void ViewportPanel::UpdateCameraPivotFromSelection()
{
    if (m_SelectedEntity == m_PreviousSelectedEntity)
    {
        // No change in selection; keep the existing pivot untouched.
        return;
    }

    m_PreviousSelectedEntity = m_SelectedEntity;

    glm::vec3 l_PivotPosition{ 0.0f, 0.0f, 0.0f };
    if (m_SelectedEntity != std::numeric_limits<Trident::ECS::Entity>::max())
    {
        Trident::ECS::Registry& l_Registry = Trident::Startup::GetRegistry();
        const bool l_HasTransform = l_Registry.HasComponent<Trident::Transform>(m_SelectedEntity);
        if (l_HasTransform)
        {
            const Trident::Transform& l_Transform = l_Registry.GetComponent<Trident::Transform>(m_SelectedEntity);
            l_PivotPosition = l_Transform.Position;
        }
    }

    // Relocate the orbit pivot so orbit/zoom operations stay centred on the selection.
    m_CameraController.SetOrbitPivot(l_PivotPosition);
}

void ViewportPanel::HandleCameraInput(const ImGuiIO& io)
{
    const float l_MinDelta = std::numeric_limits<float>::epsilon();
    const float l_DeltaTime = std::max(io.DeltaTime, l_MinDelta);

    if (m_IsCameraControlEnabled)
    {
        const bool l_IsAltDown = io.KeyAlt;
        const bool l_LeftMouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
        const bool l_MiddleMouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
        const bool l_RightMouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Right);
        const glm::vec2 l_MouseDelta{ io.MouseDelta.x, io.MouseDelta.y };

        if (l_IsAltDown && l_LeftMouseDown)
        {
            // Alt + LMB orbits the camera around the cached pivot point.
            m_CameraController.UpdateOrbit(l_MouseDelta, l_DeltaTime);
        }
        else if (l_IsAltDown && l_MiddleMouseDown)
        {
            // Alt + MMB pans laterally to reposition the pivot and camera together.
            m_CameraController.UpdatePan(l_MouseDelta, l_DeltaTime);
        }
        else if (l_IsAltDown && l_RightMouseDown)
        {
            // Alt + RMB performs a dolly based on vertical mouse motion.
            m_CameraController.UpdateDolly(l_MouseDelta.y, l_DeltaTime);
        }

        if (l_RightMouseDown && !l_IsAltDown)
        {
            // RMB alone switches to free-look fly mode.
            m_CameraController.UpdateMouseLook(l_MouseDelta, l_DeltaTime);

            glm::vec3 l_LocalMovement{ 0.0f, 0.0f, 0.0f };
            if (ImGui::IsKeyDown(ImGuiKey_W))
            {
                l_LocalMovement.y += 1.0f;
            }
            if (ImGui::IsKeyDown(ImGuiKey_S))
            {
                l_LocalMovement.y -= 1.0f;
            }
            if (ImGui::IsKeyDown(ImGuiKey_D))
            {
                l_LocalMovement.x += 1.0f;
            }
            if (ImGui::IsKeyDown(ImGuiKey_A))
            {
                l_LocalMovement.x -= 1.0f;
            }
            if (ImGui::IsKeyDown(ImGuiKey_E))
            {
                l_LocalMovement.z += 1.0f;
            }
            if (ImGui::IsKeyDown(ImGuiKey_Q))
            {
                l_LocalMovement.z -= 1.0f;
            }

            if (glm::length(l_LocalMovement) > 0.0f)
            {
                l_LocalMovement = glm::normalize(l_LocalMovement);
            }

            const bool l_BoostActive = ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift);
            m_CameraController.UpdateFly(l_LocalMovement, l_DeltaTime, l_BoostActive);

            const float l_WheelDelta = io.MouseWheel;
            if (l_WheelDelta != 0.0f)
            {
                // In fly mode the scroll wheel adjusts the base speed instead of zooming.
                m_CameraController.AdjustFlySpeed(l_WheelDelta);
            }
        }
        else
        {
            const float l_WheelDelta = io.MouseWheel;
            if (l_WheelDelta != 0.0f)
            {
                // When not flying, the mouse wheel continues to dolly toward/away from the pivot.
                m_CameraController.UpdateDolly(-l_WheelDelta, l_DeltaTime);
            }
        }
    }
    else if (m_IsHovered)
    {
        const float l_WheelDelta = io.MouseWheel;
        const bool l_RightMouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Right);
        if (!l_RightMouseDown && l_WheelDelta != 0.0f)
        {
            // Allow lightweight dolly even when ImGui has keyboard capture, matching DCC expectations.
            m_CameraController.UpdateDolly(-l_WheelDelta, io.DeltaTime);
        }
    }

    if (m_IsFocused && ImGui::IsKeyPressed(ImGuiKey_F, false))
    {
        // Focus key frames the active selection (or origin when none is selected).
        FrameSelection();
    }

    // Apply the accumulated edits so the renderer receives the latest view transform.
    m_CameraController.UpdateRenderCamera();
}

void ViewportPanel::FrameSelection()
{
    glm::vec3 l_Target{ 0.0f, 0.0f, 0.0f };
    float l_Distance = 5.0f;

    if (m_SelectedEntity != std::numeric_limits<Trident::ECS::Entity>::max())
    {
        Trident::ECS::Registry& l_Registry = Trident::Startup::GetRegistry();
        const bool l_HasTransform = l_Registry.HasComponent<Trident::Transform>(m_SelectedEntity);
        if (l_HasTransform)
        {
            const Trident::Transform& l_Transform = l_Registry.GetComponent<Trident::Transform>(m_SelectedEntity);
            l_Target = l_Transform.Position;

            const float l_MaxScale = std::max(std::max(std::abs(l_Transform.Scale.x), std::abs(l_Transform.Scale.y)), std::abs(l_Transform.Scale.z));
            const float l_NormalizedScale = std::max(l_MaxScale, 1.0f);
            l_Distance = std::max(l_NormalizedScale * 2.5f, 0.5f);
        }
    }

    m_CameraController.FrameTarget(l_Target, l_Distance);
    m_CameraController.UpdateRenderCamera();
}

void ViewportPanel::SyncRuntimeCameraToEditor()
{
    if (m_ActiveCameraEntity == 0)
    {
        return;
    }

    Trident::ECS::Registry& l_Registry = Trident::Startup::GetRegistry();
    const bool l_HasTransform = l_Registry.HasComponent<Trident::Transform>(m_ActiveCameraEntity);
    const bool l_HasCamera = l_Registry.HasComponent<Trident::CameraComponent>(m_ActiveCameraEntity);
    if (!l_HasTransform || !l_HasCamera)
    {
        return;
    }

    const Trident::Transform& l_Transform = l_Registry.GetComponent<Trident::Transform>(m_ActiveCameraEntity);
    const Trident::CameraComponent& l_Camera = l_Registry.GetComponent<Trident::CameraComponent>(m_ActiveCameraEntity);

    const glm::mat4 l_ModelMatrix = ComposeMatrixFromTransform(l_Transform);
    const glm::vec3 l_Forward = glm::normalize(glm::vec3(l_ModelMatrix * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f)));

    const float l_Yaw = glm::degrees(std::atan2(l_Forward.y, l_Forward.x));
    const float l_Pitch = glm::degrees(std::asin(glm::clamp(l_Forward.z, -1.0f, 1.0f)));

    m_CameraController.SyncToRuntimeCamera(l_Transform.Position, l_Yaw, l_Pitch, l_Camera.FieldOfView);
}