#include "AIDebugPanel.h"

#include "Renderer/RenderCommand.h"

#include <algorithm>
#include <cfloat>

AIDebugPanel::AIDebugPanel()
{
    // Reserving construction hooks for future preferences such as persisted visibility toggles.
}

void AIDebugPanel::Update()
{
    // Pull the latest renderer metrics so the UI can render without blocking on queries.
    m_CachedStats = Trident::RenderCommand::GetAiDebugStats();
    m_CachedBlendStrength = Trident::RenderCommand::GetAiBlendStrength();

    RefreshDerivedState();
    // TODO: Capture history samples here to unlock trend graphs or alerts when inference latency spikes.
}

void AIDebugPanel::Render()
{
    if (!ImGui::Begin("AI Debug"))
    {
        ImGui::End();
        return;
    }

    const bool l_ModelReady = m_ModelReady;
    const bool l_TextureReady = m_TextureReady;
    const bool l_DataStale = m_DataStale;

    // Present a concise status summary so teams can verify the generator's readiness at a glance.
    const char* l_ModelStatusLabel = l_ModelReady ? "Loaded" : "Loading";
    ImGui::Text("AI Model: %s | Queue: %zu pending | Resolution: %ux%u", l_ModelStatusLabel, m_CachedStats.m_PendingJobCount, m_CachedStats.m_TextureExtent.width,
        m_CachedStats.m_TextureExtent.height);

    ImGui::Text("Last %.2f ms | Average %.2f ms | Completed %llu", m_CachedStats.m_LastInferenceMilliseconds, m_CachedStats.m_AverageInferenceMilliseconds,
        static_cast<unsigned long long>(m_CachedStats.m_CompletedInferenceCount));

    // Visualise the pending queue depth to hint when inference throughput might become a bottleneck.
    const float l_QueueDepth = static_cast<float>(m_CachedStats.m_PendingJobCount);
    const float l_NormalisedDepth = l_QueueDepth > 0.0f ? std::min(l_QueueDepth / 8.0f, 1.0f) : 0.0f;
    const char* l_ProgressLabel = l_DataStale ? "Awaiting first inference" : "Inference throughput";
    ImGui::ProgressBar(l_NormalisedDepth, ImVec2(-FLT_MIN, 0.0f), l_ProgressLabel);

    if (l_DataStale)
    {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.15f, 1.0f), "Stale data");
        ImGui::SetItemTooltip("No AI frames have completed yet. Future builds can expose timestamps to refine this alert.");
    }
    else if (!l_ModelReady)
    {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.95f, 1.0f), "Initialising");
        ImGui::SetItemTooltip("Model weights are still loading. Follow-up tooling can show download progress here.");
    }

    // Blend slider allows artists to adjust the contribution of AI pixels in the final frame.
    ImGui::BeginDisabled(!l_ModelReady);
    float l_BlendStrength = m_CachedBlendStrength;
    if (ImGui::SliderFloat("AI Blend", &l_BlendStrength, 0.0f, 1.0f, "%.2f"))
    {
        Trident::RenderCommand::SetAiBlendStrength(l_BlendStrength);
        m_CachedBlendStrength = l_BlendStrength;
    }
    ImGui::EndDisabled();

    if (!l_ModelReady)
    {
        ImGui::SetItemTooltip("The AI blend factor unlocks once the model is ready. Future UX can surface reasons for delays.");
    }
    else
    {
        ImGui::SetItemTooltip("Blend between rasterised and AI generated frames. Future versions may expose presets here.");
    }

    // Reserve an area for visual comparisons without allocating textures before the renderer exposes them.
    if (l_TextureReady)
    {
        ImGui::Text("AI Preview");
        ImGui::Image(Trident::RenderCommand::GetAiTextureDescriptor(), ImVec2(96.0f, 54.0f));
    }
    else
    {
        ImGui::Text("AI Preview (reserved)");
        ImGui::Dummy(ImVec2(96.0f, 54.0f));
    }
    ImGui::SetItemTooltip("This space will host richer split-view comparisons once descriptors become available.");

    // TODO: Add iconography, hotkeys, and historical graphs to deepen insights as the AI pipeline matures.

    ImGui::End();
}

void AIDebugPanel::RefreshDerivedState()
{
    m_ModelReady = m_CachedStats.m_ModelInitialised;
    m_TextureReady = m_CachedStats.m_TextureReady;
    m_DataStale = m_ModelReady && !m_TextureReady && (m_CachedStats.m_CompletedInferenceCount == 0);
}