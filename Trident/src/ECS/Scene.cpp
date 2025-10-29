#include "Scene.h"

#include "Core/Utilities.h"

#include "ECS/Components/TransformComponent.h"
#include "ECS/Components/CameraComponent.h"
#include "ECS/Components/MeshComponent.h"
#include "ECS/Components/LightComponent.h"
#include "ECS/Components/TagComponent.h"
#include "ECS/Components/ScriptComponent.h"
#include "ECS/Components/TextureComponent.h"
#include "ECS/Components/AnimationComponent.h"
#include "Animation/AnimationAssetService.h"

#include <fstream>
#include <sstream>
#include <iomanip>
#include <vector>
#include <memory>

#include <glm/gtc/type_ptr.hpp>

namespace Trident
{
    Scene::Scene(ECS::Registry& registry) : m_Registry(&registry), m_EditorRegistry(&registry)
    {
        // Mirror the editor registry pointer up-front so play mode can swap without expensive lookups.
        m_AnimationSystem = std::make_unique<ECS::AnimationSystem>();
        // TODO: Allow dependency injection so specialised animation systems can be swapped during testing.

        // Seed empty scenes with a camera entity so users can immediately view their work.
        InitialiseDefaultEntities();
    }

    void Scene::InitialiseDefaultEntities()
    {
        ECS::Registry& l_EditorRegistry = GetEditorRegistry();

        // Avoid injecting defaults when a scene is being rehydrated from disk or duplicated at runtime.
        const bool l_HasEntities = !l_EditorRegistry.GetEntities().empty();
        if (l_HasEntities)
        {
            return;
        }

        const ECS::Entity l_CameraEntity = l_EditorRegistry.CreateEntity();

        // Authoring tools expect a transform for spatial manipulation, so create it up-front.
        l_EditorRegistry.AddComponent<Transform>(l_CameraEntity, Transform{});
        l_EditorRegistry.GetComponent<Transform>(l_CameraEntity).Position = { 0.0f, 100.0f, 255.0f };

        // Assign a friendly label that matches the viewport hierarchy naming convention.
        TagComponent& l_Tag = l_EditorRegistry.AddComponent<TagComponent>(l_CameraEntity);
        l_Tag.m_Tag = "Camera";

        // Promote the default camera to primary so renderers immediately lock onto it.
        CameraComponent l_CameraComponent{};
        l_CameraComponent.m_Primary = true;
        l_EditorRegistry.AddComponent<CameraComponent>(l_CameraEntity, l_CameraComponent);

        // Future enhancement: expose presets so users can pick between orthographic and perspective defaults.
    }

    void Scene::SetName(const std::string& name)
    {
        m_SceneName = name;
    }

    const std::string& Scene::GetName() const
    {
        return m_SceneName;
    }

    void Scene::Save(const std::string& path) const
    {
        std::ofstream l_Stream(path, std::ios::trunc);
        if (!l_Stream.is_open())
        {
            TR_CORE_ERROR("Failed to open scene file '{}' for writing", path);

            return;
        }

        // Emit a simple header so manual edits remain approachable.
        l_Stream << "# Trident Scene\n";
        l_Stream << "Scene \"" << EscapeString(m_SceneName) << "\"\n";
        l_Stream << std::boolalpha;

        ECS::Registry& l_Registry = GetActiveRegistry();
        const std::vector<ECS::Entity>& l_Entities = l_Registry.GetEntities();
        for (ECS::Entity it_Entity : l_Entities)
        {
            SerializeEntity(l_Stream, it_Entity);
        }

        TR_CORE_INFO("Saved scene '{}' to '{}' ({} entities)", m_SceneName, path, l_Entities.size());
    }

    bool Scene::Load(const std::string& path)
    {
        std::ifstream l_Stream(path);
        if (!l_Stream.is_open())
        {
            TR_CORE_ERROR("Failed to open scene file '{}' for reading", path);
            return false;
        }

        ECS::Registry& l_EditorRegistry = GetEditorRegistry();
        l_EditorRegistry.Clear();
        m_Registry = m_EditorRegistry;
        m_RuntimeRegistry.reset();
        m_IsPlaying = false;
        m_LoadedEntityCount = 0;

        std::string l_Line;
        while (std::getline(l_Stream, l_Line))
        {
            if (l_Line.empty() || l_Line.front() == '#')
            {
                continue;
            }

            if (l_Line.rfind("Scene ", 0) == 0)
            {
                const std::string l_NameToken = ExtractQuotedToken(l_Line);
                if (!l_NameToken.empty())
                {
                    m_SceneName = l_NameToken;
                }
                continue;
            }

            if (l_Line.rfind("Entity", 0) == 0)
            {
                DeserializeEntity(l_Stream, l_Line);
            }
        }

        TR_CORE_INFO("Loaded scene '{}' from '{}' ({} entities)", m_SceneName, path, m_LoadedEntityCount);

        return true;
    }

    void Scene::Play()
    {
        if (m_IsPlaying)
        {
            return;
        }

        // Clone the editor registry so gameplay can mutate components without touching authoring data.
        m_RuntimeRegistry = std::make_unique<ECS::Registry>();
        m_RuntimeRegistry->CopyFrom(GetEditorRegistry());
        m_Registry = m_RuntimeRegistry.get();

        ECS::Registry& l_RuntimeRegistry = GetActiveRegistry();
        Animation::AnimationAssetService& l_AnimationService = Animation::AnimationAssetService::Get();
        const std::vector<ECS::Entity>& l_Entities = l_RuntimeRegistry.GetEntities();
        for (ECS::Entity it_Entity : l_Entities)
        {
            if (!l_RuntimeRegistry.HasComponent<AnimationComponent>(it_Entity))
            {
                continue;
            }

            AnimationComponent& l_Animation = l_RuntimeRegistry.GetComponent<AnimationComponent>(it_Entity);
            l_Animation.m_CurrentTime = 0.0f;
            l_Animation.m_IsPlaying = true;
            l_Animation.InvalidateCachedAssets();

            // Resolve the cloned component's runtime handles and upload an initial pose for the renderer.
            ECS::AnimationSystem::RefreshCachedHandles(l_Animation, l_AnimationService);
            ECS::AnimationSystem::InitialisePose(l_Animation);
        }

        m_IsPlaying = true;
        for (ECS::Entity it_Entity : l_Entities)
        {
            if (l_RuntimeRegistry.HasComponent<ScriptComponent>(it_Entity))
            {
                ScriptComponent& l_Script = l_RuntimeRegistry.GetComponent<ScriptComponent>(it_Entity);
                l_Script.m_IsRunning = l_Script.m_AutoStart;
                if (l_Script.m_IsRunning)
                {
                    // Scripts currently emit lifecycle notifications; a scripting VM can hook in later.
                    TR_CORE_INFO("Starting script '{}' for entity {}", l_Script.m_ScriptPath, it_Entity);
                }
            }
        }

        // Future optimisation: support component-type filters so enormous scenes avoid cloning unused authoring data.
    }

    void Scene::Stop()
    {
        if (!m_IsPlaying)
        {
            return;
        }

        ECS::Registry& l_RuntimeRegistry = GetActiveRegistry();
        const std::vector<ECS::Entity>& l_Entities = l_RuntimeRegistry.GetEntities();
        for (ECS::Entity it_Entity : l_Entities)
        {
            if (l_RuntimeRegistry.HasComponent<ScriptComponent>(it_Entity))
            {
                ScriptComponent& l_Script = l_RuntimeRegistry.GetComponent<ScriptComponent>(it_Entity);
                if (l_Script.m_IsRunning)
                {
                    TR_CORE_INFO("Stopping script '{}' for entity {}", l_Script.m_ScriptPath, it_Entity);
                }
                l_Script.m_IsRunning = false;
            }
        }

        m_Registry = m_EditorRegistry;
        m_RuntimeRegistry.reset();
        m_IsPlaying = false;

        // Ensure editor-side components never inherit transient runtime state like running scripts.
        ECS::Registry& l_EditorRegistry = GetEditorRegistry();
        const std::vector<ECS::Entity>& l_EditorEntities = l_EditorRegistry.GetEntities();
        for (ECS::Entity it_Entity : l_EditorEntities)
        {
            if (l_EditorRegistry.HasComponent<ScriptComponent>(it_Entity))
            {
                ScriptComponent& l_Script = l_EditorRegistry.GetComponent<ScriptComponent>(it_Entity);
                l_Script.m_IsRunning = false;
            }
        }
    }

    void Scene::Update(float deltaTime)
    {
        if (!m_IsPlaying)
        {
            return;
        }

        ECS::Registry& l_RuntimeRegistry = GetActiveRegistry();
        const std::vector<ECS::Entity>& l_Entities = l_RuntimeRegistry.GetEntities();
        for (ECS::Entity it_Entity : l_Entities)
        {
            if (l_RuntimeRegistry.HasComponent<ScriptComponent>(it_Entity))
            {
                ScriptComponent& l_Script = l_RuntimeRegistry.GetComponent<ScriptComponent>(it_Entity);
                if (l_Script.m_IsRunning)
                {
                    // Placeholder behaviour until an actual scripting backend is integrated.
                    // Animations and scripts can consume delta time once the runtime is expanded.
                    TR_CORE_TRACE("Updating script '{}' (entity {}, dt={})", l_Script.m_ScriptPath, it_Entity, deltaTime);
                }
            }
        }

        if (m_AnimationSystem)
        {
            // Advance skeletal animations after scripts so gameplay can modify playback states first.
            m_AnimationSystem->Update(l_RuntimeRegistry, deltaTime);
        }
    }

    bool Scene::IsPlaying() const
    {
        return m_IsPlaying;
    }

    ECS::Registry& Scene::GetActiveRegistry() const
    {
        // m_Registry is guaranteed to be valid after construction; this helper keeps the callsites tidy.
        return *m_Registry;
    }

    ECS::Registry& Scene::GetEditorRegistry() const
    {
        return *m_EditorRegistry;
    }

    void Scene::SerializeEntity(std::ostream& stream, ECS::Entity entity) const
    {
        ECS::Registry& l_ActiveRegistry = GetActiveRegistry();
        stream << "Entity " << entity << "\n";

        if (l_ActiveRegistry.HasComponent<TagComponent>(entity))
        {
            const TagComponent& l_Tag = l_ActiveRegistry.GetComponent<TagComponent>(entity);
            stream << "Tag \"" << EscapeString(l_Tag.m_Tag) << "\"\n";
        }

        if (l_ActiveRegistry.HasComponent<Transform>(entity))
        {
            const Transform& l_Transform = l_ActiveRegistry.GetComponent<Transform>(entity);
            stream << std::setprecision(6);
            stream << "Transform "
                << l_Transform.Position.x << ' ' << l_Transform.Position.y << ' ' << l_Transform.Position.z << ' '
                << l_Transform.Rotation.x << ' ' << l_Transform.Rotation.y << ' ' << l_Transform.Rotation.z << ' '
                << l_Transform.Scale.x << ' ' << l_Transform.Scale.y << ' ' << l_Transform.Scale.z << "\n";
        }

        if (l_ActiveRegistry.HasComponent<CameraComponent>(entity))
        {
            const CameraComponent& l_Camera = l_ActiveRegistry.GetComponent<CameraComponent>(entity);
            stream << "Camera "
                << static_cast<uint32_t>(l_Camera.m_ProjectionType) << ' ' << l_Camera.m_FieldOfView << ' ' << l_Camera.m_OrthographicSize << ' '
                << l_Camera.m_NearClip << ' ' << l_Camera.m_FarClip << ' ' << l_Camera.m_Primary << ' ' << l_Camera.m_FixedAspectRatio << ' '
                << l_Camera.m_AspectRatio << "\n";
        }

        if (l_ActiveRegistry.HasComponent<MeshComponent>(entity))
        {
            const MeshComponent& l_Mesh = l_ActiveRegistry.GetComponent<MeshComponent>(entity);
            // Persist the renderer-facing indices; future iterations can enrich this with asset references.
            // The primitive flag trails the legacy fields so pre-update files continue to deserialize cleanly.
            stream << "Mesh "
                << l_Mesh.m_MeshIndex << ' ' << l_Mesh.m_MaterialIndex << ' ' << l_Mesh.m_FirstIndex << ' '
                << l_Mesh.m_IndexCount << ' ' << l_Mesh.m_BaseVertex << ' ' << l_Mesh.m_Visible << ' ' << static_cast<int>(l_Mesh.m_Primitive) << "\n";
        }

        if (l_ActiveRegistry.HasComponent<TextureComponent>(entity))
        {
            const TextureComponent& l_Texture = l_ActiveRegistry.GetComponent<TextureComponent>(entity);
            // Store slot and dirty state so texture reloads can be deferred across sessions. Future work: persist sampler state.
            stream << "Texture \"" << EscapeString(l_Texture.m_TexturePath) << "\" Slot=" << l_Texture.m_TextureSlot
                << " Dirty=" << l_Texture.m_IsDirty << "\n";
        }

        if (l_ActiveRegistry.HasComponent<AnimationComponent>(entity))
        {
            const AnimationComponent& l_Animation = l_ActiveRegistry.GetComponent<AnimationComponent>(entity);
            // Persist high level playback data so runtime components resume in the same state after reloads.
            stream << std::setprecision(6);
            stream << "Animation "
                << "Skeleton=\"" << EscapeString(l_Animation.m_SkeletonAssetId) << "\" "
                << "Animation=\"" << EscapeString(l_Animation.m_AnimationAssetId) << "\" "
                << "Clip=\"" << EscapeString(l_Animation.m_CurrentClip) << "\" "
                << "Time=" << l_Animation.m_CurrentTime << ' '
                << "Speed=" << l_Animation.m_PlaybackSpeed << ' '
                << "Playing=" << l_Animation.m_IsPlaying << ' '
                << "Looping=" << l_Animation.m_IsLooping;

            const size_t l_BoneCount = l_Animation.m_BoneMatrices.size();
            stream << " BoneCount=" << l_BoneCount << "\n";

            if (l_BoneCount > 0)
            {
                stream << "AnimationBones";
                for (size_t it_BoneIndex = 0; it_BoneIndex < l_BoneCount; ++it_BoneIndex)
                {
                    const glm::mat4& l_BoneMatrix = l_Animation.m_BoneMatrices[it_BoneIndex];
                    const float* l_RawValues = glm::value_ptr(l_BoneMatrix);
                    for (int it_ComponentIndex = 0; it_ComponentIndex < 16; ++it_ComponentIndex)
                    {
                        stream << ' ' << l_RawValues[it_ComponentIndex];
                    }
                }
                stream << "\n";
            }
        }


        if (l_ActiveRegistry.HasComponent<LightComponent>(entity))
        {
            const LightComponent& l_Light = l_ActiveRegistry.GetComponent<LightComponent>(entity);
            stream << "Light "
                << static_cast<uint32_t>(l_Light.m_Type) << ' '
                << l_Light.m_Color.r << ' ' << l_Light.m_Color.g << ' ' << l_Light.m_Color.b << ' '
                << l_Light.m_Intensity << ' '
                << l_Light.m_Direction.x << ' ' << l_Light.m_Direction.y << ' ' << l_Light.m_Direction.z << ' '
                << l_Light.m_Range << ' '
                << l_Light.m_Enabled << ' ' << l_Light.m_ShadowCaster << ' ' << l_Light.m_Reserved0 << ' ' << l_Light.m_Reserved1 << "\n";
        }

        if (l_ActiveRegistry.HasComponent<ScriptComponent>(entity))
        {
            const ScriptComponent& l_Script = l_ActiveRegistry.GetComponent<ScriptComponent>(entity);
            stream << "Script \"" << EscapeString(l_Script.m_ScriptPath) << "\" AutoStart=" << l_Script.m_AutoStart << "\n";
        }

        stream << "EndEntity\n";
    }

    void Scene::DeserializeEntity(std::istream& stream, const std::string& headerLine)
    {
        (void)headerLine; // The persisted entity identifier is currently ignored.
        ECS::Registry& l_TargetRegistry = GetActiveRegistry();
        const ECS::Entity l_Entity = l_TargetRegistry.CreateEntity();
        ++m_LoadedEntityCount;

        std::string l_Line;
        while (std::getline(stream, l_Line))
        {
            if (l_Line.empty() || l_Line.front() == '#')
            {
                continue;
            }

            if (l_Line == "EndEntity")
            {
                break;
            }

            if (l_Line.rfind("Tag ", 0) == 0)
            {
                TagComponent& l_Tag = l_TargetRegistry.AddComponent<TagComponent>(l_Entity);
                l_Tag.m_Tag = ExtractQuotedToken(l_Line);

                continue;
            }

            if (l_Line.rfind("Transform ", 0) == 0)
            {
                Transform l_Transform{};
                std::istringstream l_TokenStream(l_Line.substr(10));
                l_TokenStream
                    >> l_Transform.Position.x >> l_Transform.Position.y >> l_Transform.Position.z
                    >> l_Transform.Rotation.x >> l_Transform.Rotation.y >> l_Transform.Rotation.z
                    >> l_Transform.Scale.x >> l_Transform.Scale.y >> l_Transform.Scale.z;
                l_TargetRegistry.AddComponent<Transform>(l_Entity, l_Transform);

                continue;
            }

            if (l_Line.rfind("Camera ", 0) == 0)
            {
                CameraComponent l_Camera{};
                std::istringstream l_TokenStream(l_Line.substr(7));
                uint32_t l_ProjectionValue = 0;
                l_TokenStream >> l_ProjectionValue;
                l_Camera.m_ProjectionType = static_cast<Camera::ProjectionType>(l_ProjectionValue);
                l_TokenStream >> l_Camera.m_FieldOfView >> l_Camera.m_OrthographicSize;
                l_TokenStream >> l_Camera.m_NearClip >> l_Camera.m_FarClip;
                l_TokenStream >> std::boolalpha >> l_Camera.m_Primary >> l_Camera.m_FixedAspectRatio;
                l_TokenStream >> l_Camera.m_AspectRatio;
                l_TargetRegistry.AddComponent<CameraComponent>(l_Entity, l_Camera);

                continue;
            }

            if (l_Line.rfind("Mesh ", 0) == 0)
            {
                MeshComponent l_Mesh{};
                std::istringstream l_TokenStream(l_Line.substr(5));
                l_TokenStream >> l_Mesh.m_MeshIndex >> l_Mesh.m_MaterialIndex >> l_Mesh.m_FirstIndex >> l_Mesh.m_IndexCount >> l_Mesh.m_BaseVertex;
                l_TokenStream >> std::boolalpha >> l_Mesh.m_Visible;

                int l_PrimitiveValue = static_cast<int>(MeshComponent::PrimitiveType::None);
                if (l_TokenStream >> l_PrimitiveValue)
                {
                    // Clamp unknown enum values to "None" so corrupted data does not trip assertions later.
                    if (l_PrimitiveValue >= static_cast<int>(MeshComponent::PrimitiveType::None) &&
                        l_PrimitiveValue <= static_cast<int>(MeshComponent::PrimitiveType::Quad))
                    {
                        l_Mesh.m_Primitive = static_cast<MeshComponent::PrimitiveType>(l_PrimitiveValue);
                    }
                    else
                    {
                        l_Mesh.m_Primitive = MeshComponent::PrimitiveType::None;
                    }
                }
                l_TargetRegistry.AddComponent<MeshComponent>(l_Entity, l_Mesh);

                continue;
            }

            if (l_Line.rfind("Texture ", 0) == 0)
            {
                TextureComponent l_Texture{};
                l_Texture.m_TexturePath = ExtractQuotedToken(l_Line);

                const size_t l_SlotToken = l_Line.find("Slot=");
                if (l_SlotToken != std::string::npos)
                {
                    std::istringstream l_TokenStream(l_Line.substr(l_SlotToken + 5));
                    l_TokenStream >> l_Texture.m_TextureSlot;
                }

                const size_t l_DirtyToken = l_Line.find("Dirty=");
                if (l_DirtyToken != std::string::npos)
                {
                    std::istringstream l_TokenStream(l_Line.substr(l_DirtyToken + 6));
                    l_TokenStream >> std::boolalpha >> l_Texture.m_IsDirty;
                }

                l_TargetRegistry.AddComponent<TextureComponent>(l_Entity, l_Texture);

                continue;
            }

            if (l_Line.rfind("Animation ", 0) == 0)
            {
                AnimationComponent l_Animation{};

                auto a_ParseQuotedValue = [&](const std::string& a_Line, const std::string& a_Key) -> std::string
                    {
                        const size_t l_KeyPos = a_Line.find(a_Key);
                        if (l_KeyPos == std::string::npos)
                        {
                            return {};
                        }

                        const size_t l_FirstQuote = a_Line.find('\"', l_KeyPos);
                        if (l_FirstQuote == std::string::npos)
                        {
                            return {};
                        }

                        const size_t l_SecondQuote = a_Line.find('\"', l_FirstQuote + 1);
                        if (l_SecondQuote == std::string::npos)
                        {
                            return {};
                        }

                        const std::string l_RawValue = a_Line.substr(l_FirstQuote + 1, l_SecondQuote - l_FirstQuote - 1);
                        return UnescapeString(l_RawValue);
                    };

                auto a_ParseFloatValue = [&](const std::string& a_Line, const std::string& a_Key, float a_Default) -> float
                    {
                        const size_t l_KeyPos = a_Line.find(a_Key);
                        if (l_KeyPos == std::string::npos)
                        {
                            return a_Default;
                        }

                        std::istringstream l_TokenStream(a_Line.substr(l_KeyPos + a_Key.length()));
                        float l_Value = a_Default;
                        l_TokenStream >> l_Value;
                        return l_Value;
                    };

                auto a_ParseBoolValue = [&](const std::string& a_Line, const std::string& a_Key, bool a_Default) -> bool
                    {
                        const size_t l_KeyPos = a_Line.find(a_Key);
                        if (l_KeyPos == std::string::npos)
                        {
                            return a_Default;
                        }

                        std::istringstream l_TokenStream(a_Line.substr(l_KeyPos + a_Key.length()));
                        bool l_Value = a_Default;
                        l_TokenStream >> std::boolalpha >> l_Value;
                        return l_Value;
                    };

                l_Animation.m_SkeletonAssetId = a_ParseQuotedValue(l_Line, "Skeleton=");
                l_Animation.m_AnimationAssetId = a_ParseQuotedValue(l_Line, "Animation=");
                l_Animation.m_CurrentClip = a_ParseQuotedValue(l_Line, "Clip=");
                l_Animation.m_CurrentTime = a_ParseFloatValue(l_Line, "Time=", 0.0f);
                l_Animation.m_PlaybackSpeed = a_ParseFloatValue(l_Line, "Speed=", 1.0f);
                l_Animation.m_IsPlaying = a_ParseBoolValue(l_Line, "Playing=", true);
                l_Animation.m_IsLooping = a_ParseBoolValue(l_Line, "Looping=", true);

                size_t l_BoneCount = 0;
                const size_t l_BoneCountPos = l_Line.find("BoneCount=");
                if (l_BoneCountPos != std::string::npos)
                {
                    std::istringstream l_BoneCountStream(l_Line.substr(l_BoneCountPos + 10));
                    l_BoneCountStream >> l_BoneCount;
                }

                if (l_BoneCount > 0)
                {
                    std::string l_BonesLine;
                    if (std::getline(stream, l_BonesLine))
                    {
                        const std::string l_BonePrefix = "AnimationBones";
                        if (l_BonesLine.rfind(l_BonePrefix, 0) == 0)
                        {
                            std::vector<float> l_FlatValues;
                            l_FlatValues.reserve(l_BoneCount * 16);

                            std::istringstream l_BonesStream(l_BonesLine.substr(l_BonePrefix.length()));
                            float l_ComponentValue = 0.0f;
                            while (l_BonesStream >> l_ComponentValue)
                            {
                                l_FlatValues.push_back(l_ComponentValue);
                            }

                            if (l_FlatValues.size() == l_BoneCount * 16)
                            {
                                l_Animation.m_BoneMatrices.resize(l_BoneCount);
                                for (size_t it_BoneIndex = 0; it_BoneIndex < l_BoneCount; ++it_BoneIndex)
                                {
                                    glm::mat4& l_BoneMatrix = l_Animation.m_BoneMatrices[it_BoneIndex];
                                    float* l_BoneValues = glm::value_ptr(l_BoneMatrix);
                                    for (int it_ComponentIndex = 0; it_ComponentIndex < 16; ++it_ComponentIndex)
                                    {
                                        l_BoneValues[it_ComponentIndex] = l_FlatValues[it_BoneIndex * 16 + static_cast<size_t>(it_ComponentIndex)];
                                    }
                                }
                            }
                            else
                            {
                                TR_CORE_WARN("AnimationBoneData size mismatch (expected {} values, found {})", l_BoneCount * 16, l_FlatValues.size());
                            }
                        }
                        else
                        {
                            TR_CORE_WARN("Expected AnimationBones line while deserialising animation component, received '{}'", l_BonesLine);
                        }
                    }
                }

                l_Animation.InvalidateCachedAssets();

                l_TargetRegistry.AddComponent<AnimationComponent>(l_Entity, l_Animation);

                continue;
            }

            if (l_Line.rfind("Light ", 0) == 0)
            {
                LightComponent l_Light{};
                std::istringstream l_TokenStream(l_Line.substr(6));
                uint32_t l_TypeValue = 0;
                l_TokenStream >> l_TypeValue;
                l_Light.m_Type = static_cast<LightComponent::Type>(l_TypeValue);
                l_TokenStream >> l_Light.m_Color.r >> l_Light.m_Color.g >> l_Light.m_Color.b;
                l_TokenStream >> l_Light.m_Intensity;
                l_TokenStream >> l_Light.m_Direction.x >> l_Light.m_Direction.y >> l_Light.m_Direction.z;
                l_TokenStream >> l_Light.m_Range;
                l_TokenStream >> std::boolalpha >> l_Light.m_Enabled >> l_Light.m_ShadowCaster >> l_Light.m_Reserved0 >> l_Light.m_Reserved1;
                l_TargetRegistry.AddComponent<LightComponent>(l_Entity, l_Light);

                continue;
            }

            if (l_Line.rfind("Script ", 0) == 0)
            {
                ScriptComponent l_Script{};
                l_Script.m_ScriptPath = ExtractQuotedToken(l_Line);
                const size_t l_AutoStartPos = l_Line.find("AutoStart=");
                if (l_AutoStartPos != std::string::npos)
                {
                    std::istringstream l_TokenStream(l_Line.substr(l_AutoStartPos + 10));
                    l_TokenStream >> std::boolalpha >> l_Script.m_AutoStart;
                }
                l_TargetRegistry.AddComponent<ScriptComponent>(l_Entity, l_Script);

                continue;
            }

            TR_CORE_WARN("Encountered unknown token while deserialising entity: '{}'", l_Line);
        }
    }

    std::string Scene::EscapeString(const std::string& value)
    {
        std::string l_Result;
        l_Result.reserve(value.size());
        for (char it_Character : value)
        {
            switch (it_Character)
            {
            case '\\':
                l_Result += "\\\\";
                break;
            case '\"':
                l_Result += "\\\"";
                break;
            case '\n':
                l_Result += "\\n";
                break;
            case '\r':
                l_Result += "\\r";
                break;
            case '\t':
                l_Result += "\\t";
                break;
            default:
                l_Result += it_Character;
                break;
            }
        }

        return l_Result;
    }

    std::string Scene::UnescapeString(const std::string& value)
    {
        std::string l_Result;
        l_Result.reserve(value.size());
        for (size_t it_Index = 0; it_Index < value.size(); ++it_Index)
        {
            const char l_Character = value[it_Index];
            if (l_Character == '\\' && it_Index + 1 < value.size())
            {
                const char l_Next = value[it_Index + 1];
                switch (l_Next)
                {
                case '\\':
                    l_Result += '\\';
                    break;
                case '\"':
                    l_Result += '\"';
                    break;
                case 'n':
                    l_Result += '\n';
                    break;
                case 'r':
                    l_Result += '\r';
                    break;
                case 't':
                    l_Result += '\t';
                    break;
                default:
                    l_Result += l_Next;
                    break;
                }
                ++it_Index;
            }
            else
            {
                l_Result += l_Character;
            }
        }

        return l_Result;
    }

    std::string Scene::ExtractQuotedToken(const std::string& line)
    {
        const size_t l_FirstQuote = line.find('\"');
        const size_t l_LastQuote = line.find_last_of('\"');
        if (l_FirstQuote == std::string::npos || l_LastQuote == std::string::npos || l_LastQuote <= l_FirstQuote)
        {
            return {};
        }

        const std::string l_Token = line.substr(l_FirstQuote + 1, l_LastQuote - l_FirstQuote - 1);

        return UnescapeString(l_Token);
    }
}