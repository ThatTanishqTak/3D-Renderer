#include "Scene.h"

#include "Core/Utilities.h"

#include "ECS/Components/TransformComponent.h"
#include "ECS/Components/CameraComponent.h"
#include "ECS/Components/MeshComponent.h"
#include "ECS/Components/LightComponent.h"
#include "ECS/Components/TagComponent.h"
#include "ECS/Components/ScriptComponent.h"
#include "ECS/Components/TextureComponent.h"

#include <fstream>
#include <sstream>
#include <iomanip>
#include <vector>

namespace Trident
{
    Scene::Scene(ECS::Registry& registry) : m_Registry(&registry)
    {

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

        const std::vector<ECS::Entity>& l_Entities = m_Registry->GetEntities();
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

        m_Registry->Clear();
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

        m_IsPlaying = true;

        const std::vector<ECS::Entity>& l_Entities = m_Registry->GetEntities();
        for (ECS::Entity it_Entity : l_Entities)
        {
            if (m_Registry->HasComponent<ScriptComponent>(it_Entity))
            {
                ScriptComponent& l_Script = m_Registry->GetComponent<ScriptComponent>(it_Entity);
                l_Script.m_IsRunning = l_Script.m_AutoStart;
                if (l_Script.m_IsRunning)
                {
                    // Scripts currently emit lifecycle notifications; a scripting VM can hook in later.
                    TR_CORE_INFO("Starting script '{}' for entity {}", l_Script.m_ScriptPath, it_Entity);
                }
            }
        }
    }

    void Scene::Stop()
    {
        if (!m_IsPlaying)
        {
            return;
        }

        const std::vector<ECS::Entity>& l_Entities = m_Registry->GetEntities();
        for (ECS::Entity it_Entity : l_Entities)
        {
            if (m_Registry->HasComponent<ScriptComponent>(it_Entity))
            {
                ScriptComponent& l_Script = m_Registry->GetComponent<ScriptComponent>(it_Entity);
                if (l_Script.m_IsRunning)
                {
                    TR_CORE_INFO("Stopping script '{}' for entity {}", l_Script.m_ScriptPath, it_Entity);
                }
                l_Script.m_IsRunning = false;
            }
        }

        m_IsPlaying = false;
    }

    void Scene::Update(float deltaTime)
    {
        if (!m_IsPlaying)
        {
            return;
        }

        const std::vector<ECS::Entity>& l_Entities = m_Registry->GetEntities();
        for (ECS::Entity it_Entity : l_Entities)
        {
            if (m_Registry->HasComponent<ScriptComponent>(it_Entity))
            {
                ScriptComponent& l_Script = m_Registry->GetComponent<ScriptComponent>(it_Entity);
                if (l_Script.m_IsRunning)
                {
                    // Placeholder behaviour until an actual scripting backend is integrated.
                    // Animations and scripts can consume delta time once the runtime is expanded.
                    TR_CORE_TRACE("Updating script '{}' (entity {}, dt={})", l_Script.m_ScriptPath, it_Entity, deltaTime);
                }
            }
        }
    }

    bool Scene::IsPlaying() const
    {
        return m_IsPlaying;
    }

    void Scene::SerializeEntity(std::ostream& stream, ECS::Entity entity) const
    {
        stream << "Entity " << entity << "\n";

        if (m_Registry->HasComponent<TagComponent>(entity))
        {
            const TagComponent& l_Tag = m_Registry->GetComponent<TagComponent>(entity);
            stream << "Tag \"" << EscapeString(l_Tag.m_Tag) << "\"\n";
        }

        if (m_Registry->HasComponent<Transform>(entity))
        {
            const Transform& l_Transform = m_Registry->GetComponent<Transform>(entity);
            stream << std::setprecision(6);
            stream << "Transform "
                << l_Transform.Position.x << ' ' << l_Transform.Position.y << ' ' << l_Transform.Position.z << ' '
                << l_Transform.Rotation.x << ' ' << l_Transform.Rotation.y << ' ' << l_Transform.Rotation.z << ' '
                << l_Transform.Scale.x << ' ' << l_Transform.Scale.y << ' ' << l_Transform.Scale.z << "\n";
        }

        if (m_Registry->HasComponent<CameraComponent>(entity))
        {
            const CameraComponent& l_Camera = m_Registry->GetComponent<CameraComponent>(entity);
            stream << "Camera "
                << static_cast<uint32_t>(l_Camera.m_ProjectionType) << ' ' << l_Camera.m_FieldOfView << ' ' << l_Camera.m_OrthographicSize << ' '
                << l_Camera.m_NearClip << ' ' << l_Camera.m_FarClip << ' ' << l_Camera.m_Primary << ' ' << l_Camera.m_FixedAspectRatio << ' '
                << l_Camera.m_AspectRatio << "\n";
        }

        if (m_Registry->HasComponent<MeshComponent>(entity))
        {
            const MeshComponent& l_Mesh = m_Registry->GetComponent<MeshComponent>(entity);
            // Persist the renderer-facing indices; future iterations can enrich this with asset references.
            // The primitive flag trails the legacy fields so pre-update files continue to deserialize cleanly.
            stream << "Mesh "
                << l_Mesh.m_MeshIndex << ' ' << l_Mesh.m_MaterialIndex << ' ' << l_Mesh.m_FirstIndex << ' '
                << l_Mesh.m_IndexCount << ' ' << l_Mesh.m_BaseVertex << ' ' << l_Mesh.m_Visible << ' ' << static_cast<int>(l_Mesh.m_Primitive) << "\n";
        }

        if (m_Registry->HasComponent<TextureComponent>(entity))
        {
            const TextureComponent& l_Texture = m_Registry->GetComponent<TextureComponent>(entity);
            // Store slot and dirty state so texture reloads can be deferred across sessions. Future work: persist sampler state.
            stream << "Texture \"" << EscapeString(l_Texture.m_TexturePath) << "\" Slot=" << l_Texture.m_TextureSlot
                << " Dirty=" << l_Texture.m_IsDirty << "\n";
        }

        if (m_Registry->HasComponent<LightComponent>(entity))
        {
            const LightComponent& l_Light = m_Registry->GetComponent<LightComponent>(entity);
            stream << "Light "
                << static_cast<uint32_t>(l_Light.m_Type) << ' '
                << l_Light.m_Color.r << ' ' << l_Light.m_Color.g << ' ' << l_Light.m_Color.b << ' '
                << l_Light.m_Intensity << ' '
                << l_Light.m_Direction.x << ' ' << l_Light.m_Direction.y << ' ' << l_Light.m_Direction.z << ' '
                << l_Light.m_Range << ' '
                << l_Light.m_Enabled << ' ' << l_Light.m_ShadowCaster << ' ' << l_Light.m_Reserved0 << ' ' << l_Light.m_Reserved1 << "\n";
        }

        if (m_Registry->HasComponent<ScriptComponent>(entity))
        {
            const ScriptComponent& l_Script = m_Registry->GetComponent<ScriptComponent>(entity);
            stream << "Script \"" << EscapeString(l_Script.m_ScriptPath) << "\" AutoStart=" << l_Script.m_AutoStart << "\n";
        }

        stream << "EndEntity\n";
    }

    void Scene::DeserializeEntity(std::istream& stream, const std::string& headerLine)
    {
        (void)headerLine; // The persisted entity identifier is currently ignored.
        const ECS::Entity l_Entity = m_Registry->CreateEntity();
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
                TagComponent& l_Tag = m_Registry->AddComponent<TagComponent>(l_Entity);
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
                m_Registry->AddComponent<Transform>(l_Entity, l_Transform);

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
                m_Registry->AddComponent<CameraComponent>(l_Entity, l_Camera);

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
                m_Registry->AddComponent<MeshComponent>(l_Entity, l_Mesh);

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

                m_Registry->AddComponent<TextureComponent>(l_Entity, l_Texture);

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
                m_Registry->AddComponent<LightComponent>(l_Entity, l_Light);

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
                m_Registry->AddComponent<ScriptComponent>(l_Entity, l_Script);

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