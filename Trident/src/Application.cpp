#include "Application.h"

#include "Renderer/RenderCommand.h"
#include "Events/ApplicationEvents.h"
#include "Application/Input.h"

#include <utility>
#include <stdexcept>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <regex>
#include <sstream>
#include <string_view>

#include <imgui.h>

namespace
{
    std::optional<std::string> ExtractJsonObject(const std::string& a_Content, std::string_view a_Key)
    {
        const std::string l_SearchToken = std::string("\"") + std::string(a_Key) + std::string("\"");
        size_t l_KeyPosition = a_Content.find(l_SearchToken);
        if (l_KeyPosition == std::string::npos)
        {
            return std::nullopt;
        }

        size_t l_ObjectStart = a_Content.find('{', l_KeyPosition);
        if (l_ObjectStart == std::string::npos)
        {
            return std::nullopt;
        }

        size_t l_Depth = 0;
        for (size_t it_Index = l_ObjectStart; it_Index < a_Content.size(); ++it_Index)
        {
            const char l_Character = a_Content[it_Index];
            if (l_Character == '{')
            {
                ++l_Depth;
            }
            else if (l_Character == '}')
            {
                if (l_Depth == 0)
                {
                    break;
                }

                --l_Depth;
                if (l_Depth == 0)
                {
                    return a_Content.substr(l_ObjectStart, it_Index - l_ObjectStart + 1);
                }
            }
        }

        return std::nullopt;
    }

    std::optional<bool> ExtractJsonBool(const std::string& a_Content, std::string_view a_Key)
    {
        const std::string l_Pattern = std::string("\"") + std::string(a_Key) + std::string("\"\\s*:\\s*(true|false)");
        std::regex l_Regex(l_Pattern, std::regex::icase);
        std::smatch l_Match;
        if (std::regex_search(a_Content, l_Match, l_Regex) && l_Match.size() > 1)
        {
            std::string l_Value = l_Match[1].str();
            std::transform(l_Value.begin(), l_Value.end(), l_Value.begin(), [](unsigned char a_Char)
                {
                    return static_cast<char>(std::tolower(a_Char));
                });
            return l_Value == "true";
        }

        return std::nullopt;
    }

    std::optional<std::string> ExtractJsonString(const std::string& a_Content, std::string_view a_Key)
    {
        const std::string l_Pattern = std::string("\"") + std::string(a_Key) + std::string("\"\\s*:\\s*\"([^\"]*)\"");
        std::regex l_Regex(l_Pattern);
        std::smatch l_Match;
        if (std::regex_search(a_Content, l_Match, l_Regex) && l_Match.size() > 1)
        {
            return l_Match[1].str();
        }

        return std::nullopt;
    }
}

namespace Trident
{
    Application::Application() : Application(nullptr)
    {

    }

    Application::Application(std::unique_ptr<Layer> layer) : m_ActiveLayer(std::move(layer))
    {
        Trident::Utilities::Log::Init();
        Trident::Utilities::Time::Init();

        Inititialize();
    }

    Application::~Application()
    {
        // Ensure editor and UI resources tear down cleanly even if the host forgets to call Shutdown explicitly.
        Shutdown();
    }

    void Application::Inititialize()
    {
        m_Specifications.Width = 1920;
        m_Specifications.Height = 1080;
        m_Specifications.Title = "Trident-Forge";

        m_Window = std::make_unique<Window>(m_Specifications);
        m_Window->SetEventCallback([this](Events& event)
            {
                // Route every GLFW callback through the Application entry point so systems can react.
                OnEvent(event);
            });
        m_Startup = std::make_unique<Startup>(*m_Window);

        RenderCommand::Init();

        // Load AI configuration immediately after the renderer bootstraps so the runtime can initialise providers early.
        m_AISettings = LoadAIConfiguration();
        RenderCommand::ConfigureAIFrameGeneration(m_AISettings);

        // Bootstrap the ImGui layer once the renderer is ready so editor widgets can access the graphics context safely.
        m_ImGuiLayer = std::make_unique<UI::ImGuiLayer>();

        const QueueFamilyIndices l_QueueFamilyIndices = Startup::GetQueueFamilyIndices();
        if (!l_QueueFamilyIndices.GraphicsFamily.has_value() || !l_QueueFamilyIndices.PresentFamily.has_value())
        {
            throw std::runtime_error("Queue family indices are not initialised before ImGui setup.");
        }

        const VkQueue l_GraphicsQueue = Startup::Get().GetGraphicsQueue();
        const VkQueue l_PresentQueue = Startup::Get().GetPresentQueue();
        (void)l_PresentQueue; // Present queue reserved for future multi-queue UI work.

        m_ImGuiLayer->Init(m_Window->GetNativeWindow(), Startup::GetInstance(), Startup::GetPhysicalDevice(), Startup::GetDevice(), l_QueueFamilyIndices.GraphicsFamily.value(),
            l_GraphicsQueue, Startup::GetRenderer().GetRenderPass(), static_cast<uint32_t>(Startup::GetRenderer().GetImageCount()), Startup::GetRenderer().GetCommandPool());

        // Share the ImGui layer with the renderer so it can route draw commands and lifetime events appropriately.
        Startup::GetRenderer().SetImGuiLayer(m_ImGuiLayer.get());

        // Once the renderer is configured, the active layer can allocate gameplay/editor resources safely.
        if (m_ActiveLayer)
        {
            m_ActiveLayer->Initialize();
        }
    }

    void Application::Run()
    {
        while (m_IsRunning && !m_Window->ShouldClose())
        {
            Update();

            Render();
        }
    }

    void Application::Update()
    {
        Utilities::Time::Update();

        Input::Get().BeginFrame();

        m_Window->PollEvents();

        // Update the active layer after input/events so it can react to the latest state.
        if (m_ActiveLayer)
        {
            m_ActiveLayer->Update();
        }
        // Reset one-shot input edges so the next tick starts with a clean slate while
        // keeping the held state active. Future controller or text helpers can share this hook.
        Input::Get().EndFrame();
    }

    void Application::Render()
    {
        if (m_ImGuiLayer)
        {
            m_ImGuiLayer->BeginFrame();
        }

        // Allow the gameplay/editor layer to submit draw data before the UI finalises the frame.
        if (m_ActiveLayer)
        {
            m_ActiveLayer->Render();
        }

        if (m_ImGuiLayer)
        {
            DrawAIFrameGenerationWindow();
        }

        if (m_ImGuiLayer)
        {
            m_ImGuiLayer->EndFrame();
        }

        RenderCommand::DrawFrame();
    }

    void Application::DrawAIFrameGenerationWindow()
    {
        if (ImGui::Begin("AI Frame Generation"))
        {
            AIFrameGenerationStatus l_Status = RenderCommand::GetAIFrameGenerationStatus();

            bool l_CurrentlyEnabled = RenderCommand::IsAIFrameGenerationEnabled();
            if (ImGui::Checkbox("Enable inference", &l_CurrentlyEnabled))
            {
                // Allow developers to toggle expensive inference without digging into config files mid-session.
                RenderCommand::SetAIFrameGenerationEnabled(l_CurrentlyEnabled);
            }

            ImGui::Separator();
            ImGui::TextUnformatted("Configuration");
            ImGui::Text("Model Path: %s", l_Status.m_ModelPath.empty() ? "<unset>" : l_Status.m_ModelPath.c_str());
            ImGui::Text("Config Loaded: %s", m_AISettingsLoaded ? "Yes" : "No");
            ImGui::Text("Startup Enabled: %s", m_AISettings.m_EnableOnStartup ? "Yes" : "No");
            ImGui::Text("CUDA Requested: %s", l_Status.m_CUDARequested ? "Yes" : "No");
            ImGui::Text("CUDA Active: %s", l_Status.m_CUDAAvailable ? "Yes" : "No");
            ImGui::Text("CPU Fallback Requested: %s", l_Status.m_CPUFallbackRequested ? "Yes" : "No");
            ImGui::Text("CPU Fallback Active: %s", l_Status.m_CPUAvailable ? "Yes" : "No");
            ImGui::Text("Model Loaded: %s", l_Status.m_ModelLoaded ? "Yes" : "No");

            ImGui::Separator();
            ImGui::TextUnformatted("Latency Metrics");
            const double l_LastInferenceMs = RenderCommand::GetAILastInferenceMilliseconds();
            const double l_LastQueueMs = RenderCommand::GetAIQueueLatencyMilliseconds();
            const double l_ExpectedLatency = RenderCommand::GetAIExpectedLatencyMilliseconds();
            ImGui::Text("Last Inference: %.2f ms", l_LastInferenceMs);
            ImGui::Text("Queue Latency: %.2f ms", l_LastQueueMs);
            ImGui::Text("Expected Overlap: %.2f ms", l_ExpectedLatency);
            if (!l_Status.m_Enabled)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.0f, 1.0f), "AI frame generation currently disabled.");
            }

            ImGui::Spacing();
            ImGui::TextWrapped("TODO: Surface rolling averages and GPU metrics once the telemetry service lands.");
        }
        ImGui::End();
    }

    AIFrameGenerationSettings Application::LoadAIConfiguration()
    {
        AIFrameGenerationSettings l_Settings{};
        m_AISettingsLoaded = false;

        const std::filesystem::path l_ConfigPath = std::filesystem::path("Trident/Config/AISettings.json");
        const std::filesystem::path l_ConfigDirectory = l_ConfigPath.parent_path();

        if (!std::filesystem::exists(l_ConfigPath))
        {
            TR_CORE_WARN("Application: AI configuration file '{}' was not found. Using defaults.", l_ConfigPath.string());
            return l_Settings;
        }

        std::ifstream l_ConfigStream(l_ConfigPath);
        if (!l_ConfigStream)
        {
            TR_CORE_ERROR("Application: Failed to open AI configuration file '{}' for reading.", l_ConfigPath.string());
            return l_Settings;
        }

        std::stringstream l_Buffer;
        l_Buffer << l_ConfigStream.rdbuf();
        const std::string l_Content = l_Buffer.str();
        if (l_Content.empty())
        {
            TR_CORE_WARN("Application: AI configuration file '{}' is empty. Falling back to defaults.", l_ConfigPath.string());
            return l_Settings;
        }

        std::optional<std::string> l_AIObject = ExtractJsonObject(l_Content, "ai");
        if (!l_AIObject.has_value())
        {
            TR_CORE_WARN("Application: AI configuration file '{}' does not contain an 'ai' section.", l_ConfigPath.string());
            return l_Settings;
        }

        m_AISettingsLoaded = true;

        if (std::optional<bool> l_Enabled = ExtractJsonBool(l_AIObject.value(), "enabled"))
        {
            l_Settings.m_EnableOnStartup = l_Enabled.value();
        }

        if (std::optional<std::string> l_ModelPath = ExtractJsonString(l_AIObject.value(), "modelPath"))
        {
            std::filesystem::path l_ResolvedModelPath = std::filesystem::path(l_ModelPath.value());
            if (l_ResolvedModelPath.is_relative())
            {
                l_ResolvedModelPath = (l_ConfigDirectory / l_ResolvedModelPath).lexically_normal();
            }
            l_Settings.m_ModelPath = l_ResolvedModelPath.string();
        }

        if (std::optional<std::string> l_ProvidersObject = ExtractJsonObject(l_AIObject.value(), "providers"))
        {
            if (std::optional<bool> l_PreferCUDA = ExtractJsonBool(l_ProvidersObject.value(), "preferCUDA"))
            {
                l_Settings.m_EnableCUDA = l_PreferCUDA.value();
            }

            if (std::optional<bool> l_CPUFallback = ExtractJsonBool(l_ProvidersObject.value(), "allowCPUFallback"))
            {
                l_Settings.m_EnableCPUFallback = l_CPUFallback.value();
            }
        }

        TR_CORE_INFO("Application: Loaded AI settings from '{}' (startup={}, CUDA={}, CPU fallback={}).", l_ConfigPath.string(),
            l_Settings.m_EnableOnStartup, l_Settings.m_EnableCUDA, l_Settings.m_EnableCPUFallback);
        if (l_Settings.m_ModelPath.empty())
        {
            TR_CORE_WARN("Application: AI model path is empty; AI frame generation will stay disabled until configured.");
        }

        // TODO: Migrate to a shared configuration service once multiple subsystems consume runtime settings.
        return l_Settings;
    }

    void Application::OnEvent(Events& event)
    {
        const std::string l_EventDescription = event.ToString();
        TR_CORE_TRACE("Received event: {}", l_EventDescription);

        // Dispatch events by type so only the relevant handler executes and other listeners remain extendable.
        EventDispatcher l_Dispatcher(event);

        l_Dispatcher.Dispatch<WindowCloseEvent>([this](WindowCloseEvent& event)
            {
                (void)event;

                m_IsRunning = false;

                return true;
            });

        if (m_ActiveLayer)
        {
            // Forward the event to the active layer so editor tooling and gameplay can react to callbacks such as file drops.
            m_ActiveLayer->OnEvent(event);
        }

        // Future event types (input, window focus, etc.) can be dispatched here without modifying the callback wiring.
    }

    void Application::Shutdown()
    {
        TR_CORE_INFO("-------SHUTTING DOWN APPLICATION-------");

        if (m_HasShutdown)
        {
            return;
        }

        m_HasShutdown = true;

        // Ask the active layer to release its resources while the renderer context is still valid.
        if (m_ActiveLayer)
        {
            m_ActiveLayer->Shutdown();
            m_ActiveLayer.reset();
        }

        // Tear down ImGui and detach it from the renderer so command buffers do not try
        // to access freed UI state.
        if (m_ImGuiLayer)
        {
            Startup::GetRenderer().SetImGuiLayer(nullptr);
            m_ImGuiLayer->Shutdown();
            m_ImGuiLayer.reset();
        }

        RenderCommand::Shutdown();

        // Release window and startup scaffolding last so Vulkan resources are already flushed.
        m_Startup.reset();
        m_Window.reset();

        TR_CORE_INFO("-------APPLICATION SHUTDOWN COMPLETE-------");
    }

    void Application::SetActiveLayer(std::unique_ptr<Layer> layer)
    {
        // Ensure any previous layer unwinds before we replace it to avoid dangling GPU handles.
        if (m_ActiveLayer && m_Startup)
        {
            m_ActiveLayer->Shutdown();
        }

        m_ActiveLayer = std::move(layer);

        // If the engine is already initialised boot the new layer immediately.
        if (m_ActiveLayer && m_Startup)
        {
            m_ActiveLayer->Initialize();
        }
    }
}