#include "AnimationGraphPanel.h"

#include "Animation/AnimationAssetService.h"
#include "ECS/AnimationSystem.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <numeric>
#include <utility>

namespace
{
    constexpr ImVec2 s_GraphCanvasPadding{ 16.0f, 16.0f }; ///< Outer padding applied to the canvas background.
    constexpr float s_NodeWidth = 180.0f;                  ///< Width used for each state node.
    constexpr float s_NodeHeight = 72.0f;                  ///< Height used for each state node.
    constexpr float s_NodeSpacing = 80.0f;                 ///< Horizontal spacing between sequential nodes.
    constexpr float s_ParameterWidth = 160.0f;             ///< Width of the parameter boxes.
    constexpr float s_ParameterHeight = 52.0f;             ///< Height of the parameter boxes.
    constexpr float s_ParameterSpacing = 80.0f;            ///< Vertical spacing between parameter boxes.
    constexpr float s_TimelineHeight = 52.0f;              ///< Reserved height beneath the viewport for the playback slider.
    constexpr ImU32 s_NodeColor = IM_COL32(60, 135, 198, 255);
    constexpr ImU32 s_NodeActiveColor = IM_COL32(255, 193, 7, 255);
    constexpr ImU32 s_NodeOutlineColor = IM_COL32(15, 34, 48, 255);
    constexpr ImU32 s_ParameterColor = IM_COL32(120, 144, 156, 200);
    constexpr ImU32 s_ParameterActiveColor = IM_COL32(76, 175, 80, 255);
    constexpr ImU32 s_ConnectionColor = IM_COL32(200, 200, 200, 220);
    constexpr float s_ConnectionThickness = 2.5f;
}

void AnimationGraphPanel::SetRegistry(Trident::ECS::Registry* registry)
{
    m_Registry = registry;
}

void AnimationGraphPanel::SetSelectedEntity(Trident::ECS::Entity selectedEntity)
{
    m_SelectedEntity = selectedEntity;
}

void AnimationGraphPanel::Update()
{
    m_HasValidSelection = false;
    m_ActiveClipDuration = 0.0f;
    m_ActiveClipName.clear();
    m_ActiveNodeIndex.reset();

    if (m_Registry == nullptr)
    {
        return;
    }

    if (m_SelectedEntity == std::numeric_limits<Trident::ECS::Entity>::max())
    {
        return;
    }

    if (!m_Registry->HasComponent<Trident::AnimationComponent>(m_SelectedEntity))
    {
        return;
    }

    m_HasValidSelection = true;

    Trident::AnimationComponent& l_Component = m_Registry->GetComponent<Trident::AnimationComponent>(m_SelectedEntity);

    // Ensure cached handles remain valid so clip queries succeed even when the author changes asset identifiers mid-session.
    Trident::Animation::AnimationAssetService& l_Service = Trident::Animation::AnimationAssetService::Get();
    Trident::ECS::AnimationSystem::RefreshCachedHandles(l_Component, l_Service);

    const std::vector<Trident::Animation::AnimationClip>* a_Clips = l_Service.GetAnimationClips(l_Component.m_AnimationAssetHandle);
    if (a_Clips != nullptr)
    {
        const size_t l_ClipHash = ComputeClipHash(*a_Clips);
        if (l_ClipHash != m_CachedClipHash)
        {
            m_CachedClipHash = l_ClipHash;
            RefreshClipLayout(*a_Clips);
        }

        const Trident::Animation::AnimationClip* a_ActiveClip = l_Service.GetClip(l_Component.m_AnimationAssetHandle, l_Component.m_CurrentClipIndex);
        if (a_ActiveClip != nullptr)
        {
            m_ActiveClipDuration = a_ActiveClip->m_DurationSeconds;
            m_ActiveClipName = a_ActiveClip->m_Name;
        }
    }
    else
    {
        m_GraphNodes.clear();
        m_GraphTransitions.clear();
        m_CachedClipHash = 0;
    }

    EnsureParameterLayout();
    UpdateNodeActivation();
    UpdateParameterActivation(l_Component);
    RebuildConnections();
}

void AnimationGraphPanel::Render()
{
    if (!ImGui::Begin("Animation Graph"))
    {
        ImGui::End();
        return;
    }

    if (!m_HasValidSelection)
    {
        ImGui::TextDisabled("Select an entity with an Animation Component to preview its state machine.");
        ImGui::End();
        return;
    }

    Trident::AnimationComponent& l_Component = m_Registry->GetComponent<Trident::AnimationComponent>(m_SelectedEntity);
    Trident::Animation::AnimationAssetService& l_Service = Trident::Animation::AnimationAssetService::Get();
    const Trident::Animation::AnimationClip* a_ActiveClip = l_Service.GetClip(l_Component.m_AnimationAssetHandle, l_Component.m_CurrentClipIndex);

    DrawViewportSection();
    DrawPlaybackControls(l_Component, a_ActiveClip);
    DrawParameterControls(l_Component);

    // Refresh highlights immediately after author input so the graph reflects the latest state without waiting for Update().
    UpdateNodeActivation();
    UpdateParameterActivation(l_Component);
    RebuildConnections();

    DrawGraphCanvas();

    ImGui::End();
}

void AnimationGraphPanel::RefreshClipLayout(const std::vector<Trident::Animation::AnimationClip>& clips)
{
    m_GraphNodes.clear();
    m_GraphTransitions.clear();

    const float l_StartX = 260.0f; // Offset nodes to the right so parameter boxes fit on the left.
    const float l_StartY = 120.0f;

    for (size_t it_Index = 0; it_Index < clips.size(); ++it_Index)
    {
        GraphNode l_Node{};
        l_Node.m_Label = clips[it_Index].m_Name;
        l_Node.m_Size = ImVec2(s_NodeWidth, s_NodeHeight);
        l_Node.m_Position = ImVec2(l_StartX + static_cast<float>(it_Index) * (s_NodeWidth + s_NodeSpacing), l_StartY);
        m_GraphNodes.push_back(std::move(l_Node));
    }

    if (m_GraphNodes.size() > 1)
    {
        for (size_t it_Index = 0; it_Index < m_GraphNodes.size() - 1; ++it_Index)
        {
            GraphTransition l_Transition{};
            l_Transition.m_FromIndex = it_Index;
            l_Transition.m_ToIndex = it_Index + 1;
            m_GraphTransitions.push_back(l_Transition);
        }
    }
}

void AnimationGraphPanel::EnsureParameterLayout()
{
    if (!m_Parameters.empty())
    {
        return;
    }

    GraphParameter l_PlayParameter{};
    l_PlayParameter.m_Label = "Playback";
    l_PlayParameter.m_Size = ImVec2(s_ParameterWidth, s_ParameterHeight);
    l_PlayParameter.m_Position = ImVec2(32.0f, 100.0f);

    GraphParameter l_LoopParameter{};
    l_LoopParameter.m_Label = "Looping";
    l_LoopParameter.m_Size = ImVec2(s_ParameterWidth, s_ParameterHeight);
    l_LoopParameter.m_Position = ImVec2(32.0f, 100.0f + s_ParameterSpacing);

    GraphParameter l_SpeedParameter{};
    l_SpeedParameter.m_Label = "Speed";
    l_SpeedParameter.m_Size = ImVec2(s_ParameterWidth, s_ParameterHeight);
    l_SpeedParameter.m_Position = ImVec2(32.0f, 100.0f + (s_ParameterSpacing * 2.0f));

    m_Parameters.emplace_back(std::move(l_PlayParameter));
    m_Parameters.emplace_back(std::move(l_LoopParameter));
    m_Parameters.emplace_back(std::move(l_SpeedParameter));
}

void AnimationGraphPanel::UpdateNodeActivation()
{
    m_ActiveNodeIndex.reset();

    for (size_t it_Index = 0; it_Index < m_GraphNodes.size(); ++it_Index)
    {
        GraphNode& l_Node = m_GraphNodes[it_Index];
        l_Node.m_IsActive = (l_Node.m_Label == m_ActiveClipName);
        if (l_Node.m_IsActive)
        {
            m_ActiveNodeIndex = it_Index;
        }
    }
}

void AnimationGraphPanel::UpdateParameterActivation(const Trident::AnimationComponent& component)
{
    if (m_Parameters.size() < 3)
    {
        return;
    }

    m_Parameters[0].m_IsActive = component.m_IsPlaying;
    m_Parameters[1].m_IsActive = component.m_IsLooping;
    m_Parameters[2].m_IsActive = std::abs(component.m_PlaybackSpeed - 1.0f) > 0.01f;
}

void AnimationGraphPanel::RebuildConnections()
{
    m_GraphConnections.clear();

    if (!m_ActiveNodeIndex.has_value())
    {
        return;
    }

    for (size_t it_Index = 0; it_Index < m_Parameters.size(); ++it_Index)
    {
        GraphConnection l_Connection{};
        l_Connection.m_ParameterIndex = it_Index;
        l_Connection.m_NodeIndex = m_ActiveNodeIndex.value();
        m_GraphConnections.push_back(l_Connection);
    }
}

void AnimationGraphPanel::DrawViewportSection()
{
    const ImVec2 l_ViewportSize = ImVec2(ImGui::GetContentRegionAvail().x, 280.0f);
    if (ImGui::BeginChild("AnimationPreviewViewport", l_ViewportSize, true))
    {
        const ImVec2 l_ImageSize = ImGui::GetContentRegionAvail();
        const glm::vec2 l_NewSize{ l_ImageSize.x, std::max(0.0f, l_ImageSize.y - s_TimelineHeight) };
        const ImVec2 l_ImageAreaSize{ l_ImageSize.x, l_NewSize.y };

        const ImVec2 l_ImagePos = ImGui::GetCursorScreenPos();
        m_ViewportBoundsMin = l_ImagePos;
        m_ViewportBoundsMax = ImVec2(l_ImagePos.x + l_ImageAreaSize.x, l_ImagePos.y + l_ImageAreaSize.y);

        if (l_NewSize.x > 1.0f && l_NewSize.y > 1.0f)
        {
            if (l_NewSize != m_CachedViewportSize)
            {
                Trident::ViewportInfo l_Info{};
                l_Info.ViewportID = m_ViewportID;
                l_Info.Position = { m_ViewportBoundsMin.x, m_ViewportBoundsMin.y };
                l_Info.Size = l_NewSize;
                Trident::RenderCommand::SetViewport(m_ViewportID, l_Info);
                m_CachedViewportSize = l_NewSize;
            }

            const VkDescriptorSet l_Descriptor = Trident::RenderCommand::GetViewportTexture(m_ViewportID);
            if (l_Descriptor != VK_NULL_HANDLE)
            {
                ImGui::Image(reinterpret_cast<ImTextureID>(l_Descriptor), l_ImageAreaSize);
            }
            else
            {
                ImGui::Dummy(l_ImageAreaSize);
            }
        }
        else
        {
            ImGui::Dummy(l_ImageAreaSize);
        }

        ImGui::Dummy(ImVec2(0.0f, s_TimelineHeight));
    }
    ImGui::EndChild();
}

void AnimationGraphPanel::DrawPlaybackControls(Trident::AnimationComponent& component, const Trident::Animation::AnimationClip* activeClip)
{
    ImGui::Separator();

    if (ImGui::Button(component.m_IsPlaying ? "Pause" : "Play"))
    {
        component.m_IsPlaying = !component.m_IsPlaying;
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop"))
    {
        component.m_IsPlaying = false;
        component.m_CurrentTime = 0.0f;
    }
    ImGui::SameLine();
    if (ImGui::Button("Frame Start"))
    {
        component.m_CurrentTime = 0.0f;
    }

    if (activeClip != nullptr && activeClip->m_DurationSeconds > 0.0f)
    {
        const float l_Duration = activeClip->m_DurationSeconds;
        float l_CurrentTime = std::clamp(component.m_CurrentTime, 0.0f, l_Duration);
        if (std::abs(l_CurrentTime - component.m_CurrentTime) > std::numeric_limits<float>::epsilon())
        {
            component.m_CurrentTime = l_CurrentTime;
        }

        ImGui::Text("%.2f / %.2f s", component.m_CurrentTime, l_Duration);
        ImGui::PushItemWidth(-1.0f);
        if (ImGui::SliderFloat("##AnimationTimeline", &l_CurrentTime, 0.0f, l_Duration, "%.2fs"))
        {
            component.m_CurrentTime = l_CurrentTime;
            component.m_IsPlaying = false;
        }
        ImGui::PopItemWidth();
    }
    else
    {
        ImGui::TextDisabled("No animation clip resolved. Configure the component to enable scrubbing.");
        ImGui::PushItemWidth(-1.0f);
        float l_DisabledProgress = 0.0f;
        ImGui::ProgressBar(l_DisabledProgress, ImVec2(-1.0f, 0.0f));
        ImGui::PopItemWidth();
    }
}

void AnimationGraphPanel::DrawParameterControls(Trident::AnimationComponent& component)
{
    ImGui::Separator();

    ImGui::Checkbox("Loop", &component.m_IsLooping);
    ImGui::SameLine();
    ImGui::Checkbox("Preview", &component.m_IsPlaying);
    ImGui::DragFloat("Playback Speed", &component.m_PlaybackSpeed, 0.01f, -5.0f, 5.0f, "%.2f");
}

void AnimationGraphPanel::DrawGraphCanvas()
{
    const ImVec2 l_CanvasSize = ImGui::GetContentRegionAvail();
    if (l_CanvasSize.x <= 1.0f || l_CanvasSize.y <= 1.0f)
    {
        return;
    }

    const ImVec2 l_CanvasOrigin = ImGui::GetCursorScreenPos();

    ImDrawList* a_DrawList = ImGui::GetWindowDrawList();
    DrawGraphBackground(a_DrawList, l_CanvasOrigin, l_CanvasSize);
    DrawTransitions(a_DrawList, l_CanvasOrigin);
    DrawParameterConnections(a_DrawList, l_CanvasOrigin);
    DrawParameters(a_DrawList, l_CanvasOrigin);
    DrawNodes(a_DrawList, l_CanvasOrigin);

    ImGui::InvisibleButton("AnimationGraphCanvas", l_CanvasSize);
}

void AnimationGraphPanel::DrawGraphBackground(ImDrawList* drawList, const ImVec2& origin, const ImVec2& size) const
{
    const ImVec2 l_PaddedMin = ImVec2(origin.x - s_GraphCanvasPadding.x, origin.y - s_GraphCanvasPadding.y);
    const ImVec2 l_PaddedMax = ImVec2(origin.x + size.x + s_GraphCanvasPadding.x, origin.y + size.y + s_GraphCanvasPadding.y);
    drawList->AddRectFilled(l_PaddedMin, l_PaddedMax, IM_COL32(30, 34, 43, 255), 12.0f);
    drawList->AddRect(l_PaddedMin, l_PaddedMax, IM_COL32(12, 16, 24, 255), 12.0f);
}

void AnimationGraphPanel::DrawTransitions(ImDrawList* drawList, const ImVec2& origin) const
{
    for (const GraphTransition& it_Transition : m_GraphTransitions)
    {
        if (it_Transition.m_FromIndex >= m_GraphNodes.size() || it_Transition.m_ToIndex >= m_GraphNodes.size())
        {
            continue;
        }

        const GraphNode& l_From = m_GraphNodes[it_Transition.m_FromIndex];
        const GraphNode& l_To = m_GraphNodes[it_Transition.m_ToIndex];

        const ImVec2 l_FromPoint = ImVec2(origin.x + l_From.m_Position.x + l_From.m_Size.x, origin.y + l_From.m_Position.y + (l_From.m_Size.y * 0.5f));
        const ImVec2 l_ToPoint = ImVec2(origin.x + l_To.m_Position.x, origin.y + l_To.m_Position.y + (l_To.m_Size.y * 0.5f));

        const ImVec2 l_ControlA = ImVec2(l_FromPoint.x + 40.0f, l_FromPoint.y);
        const ImVec2 l_ControlB = ImVec2(l_ToPoint.x - 40.0f, l_ToPoint.y);

        drawList->AddBezierCubic(l_FromPoint, l_ControlA, l_ControlB, l_ToPoint, s_ConnectionColor, s_ConnectionThickness);
    }
}

void AnimationGraphPanel::DrawParameterConnections(ImDrawList* drawList, const ImVec2& origin) const
{
    for (const GraphConnection& it_Connection : m_GraphConnections)
    {
        if (it_Connection.m_ParameterIndex >= m_Parameters.size() || it_Connection.m_NodeIndex >= m_GraphNodes.size())
        {
            continue;
        }

        const GraphParameter& l_Param = m_Parameters[it_Connection.m_ParameterIndex];
        const GraphNode& l_Node = m_GraphNodes[it_Connection.m_NodeIndex];

        const ImVec2 l_ParamPoint = ImVec2(origin.x + l_Param.m_Position.x + l_Param.m_Size.x, origin.y + l_Param.m_Position.y + (l_Param.m_Size.y * 0.5f));
        const ImVec2 l_NodePoint = ImVec2(origin.x + l_Node.m_Position.x, origin.y + l_Node.m_Position.y + (l_Node.m_Size.y * 0.5f));

        const ImVec2 l_ControlA = ImVec2(l_ParamPoint.x + 30.0f, l_ParamPoint.y);
        const ImVec2 l_ControlB = ImVec2(l_NodePoint.x - 30.0f, l_NodePoint.y);

        drawList->AddBezierCubic(l_ParamPoint, l_ControlA, l_ControlB, l_NodePoint, s_ConnectionColor, s_ConnectionThickness);
    }
}

void AnimationGraphPanel::DrawNodes(ImDrawList* drawList, const ImVec2& origin) const
{
    for (const GraphNode& it_Node : m_GraphNodes)
    {
        const ImVec2 l_Min = ImVec2(origin.x + it_Node.m_Position.x, origin.y + it_Node.m_Position.y);
        const ImVec2 l_Max = ImVec2(l_Min.x + it_Node.m_Size.x, l_Min.y + it_Node.m_Size.y);

        const ImU32 l_Fill = it_Node.m_IsActive ? s_NodeActiveColor : s_NodeColor;
        drawList->AddRectFilled(l_Min, l_Max, l_Fill, 10.0f);
        drawList->AddRect(l_Min, l_Max, s_NodeOutlineColor, 10.0f);

        const ImVec2 l_TextSize = ImGui::CalcTextSize(it_Node.m_Label.c_str());
        const ImVec2 l_TextPos = ImVec2(l_Min.x + (it_Node.m_Size.x - l_TextSize.x) * 0.5f, l_Min.y + (it_Node.m_Size.y - l_TextSize.y) * 0.5f);
        drawList->AddText(l_TextPos, IM_COL32(20, 20, 20, 255), it_Node.m_Label.c_str());
    }
}

void AnimationGraphPanel::DrawParameters(ImDrawList* drawList, const ImVec2& origin) const
{
    for (const GraphParameter& it_Param : m_Parameters)
    {
        const ImVec2 l_Min = ImVec2(origin.x + it_Param.m_Position.x, origin.y + it_Param.m_Position.y);
        const ImVec2 l_Max = ImVec2(l_Min.x + it_Param.m_Size.x, l_Min.y + it_Param.m_Size.y);

        const ImU32 l_Color = it_Param.m_IsActive ? s_ParameterActiveColor : s_ParameterColor;
        drawList->AddRectFilled(l_Min, l_Max, l_Color, 10.0f);
        drawList->AddRect(l_Min, l_Max, IM_COL32(45, 45, 45, 255), 10.0f);

        const ImVec2 l_TextSize = ImGui::CalcTextSize(it_Param.m_Label.c_str());
        const ImVec2 l_TextPos = ImVec2(l_Min.x + (it_Param.m_Size.x - l_TextSize.x) * 0.5f, l_Min.y + (it_Param.m_Size.y - l_TextSize.y) * 0.5f);
        drawList->AddText(l_TextPos, IM_COL32(20, 20, 20, 255), it_Param.m_Label.c_str());
    }
}

size_t AnimationGraphPanel::ComputeClipHash(const std::vector<Trident::Animation::AnimationClip>& clips) const
{
    size_t l_Hash = clips.size();
    std::hash<std::string> l_StringHasher{};

    for (const Trident::Animation::AnimationClip& it_Clip : clips)
    {
        size_t l_NameHash = l_StringHasher(it_Clip.m_Name);
        l_Hash ^= l_NameHash + 0x9e3779b97f4a7c15ULL + (l_Hash << 6) + (l_Hash >> 2);
    }

    return l_Hash;
}