#pragma once

#include "ECS/Registry.h"

#include <string>
#include <iosfwd>
#include <memory>

namespace Trident
{
    /**
     * @brief High level scene container providing save/load and play-state controls.
     *
     * The scene acts as a facade around the central registry so tools can persist
     * entity state to disk and toggle runtime execution. The current implementation
     * serialises a curated subset of components in a simple line-based text format
     * with the ".trident" extension. Future revisions can extend the format to
     * include asset dependencies, animation clips, and scripting bytecode.
     */
    class Scene
    {
    public:
        explicit Scene(ECS::Registry& registry);

        void SetName(const std::string& name);
        [[nodiscard]] const std::string& GetName() const;

        void Save(const std::string& path) const;
        bool Load(const std::string& path);

        void Play();
        void Stop();
        void Update(float deltaTime);

        [[nodiscard]] bool IsPlaying() const;
        [[nodiscard]] ECS::Registry& GetActiveRegistry() const;
        [[nodiscard]] ECS::Registry& GetEditorRegistry() const;

    private:
        void SerializeEntity(std::ostream& stream, ECS::Entity entity) const;
        void DeserializeEntity(std::istream& stream, const std::string& headerLine);

        static std::string EscapeString(const std::string& value);
        static std::string UnescapeString(const std::string& value);
        static std::string ExtractQuotedToken(const std::string& line);

    private:
        ECS::Registry* m_Registry{ nullptr };          ///< Points to either the editor or runtime registry depending on play state.
        ECS::Registry* m_EditorRegistry{ nullptr };    ///< Non-owning pointer used to restore the editor registry when leaving play mode.
        std::unique_ptr<ECS::Registry> m_RuntimeRegistry; ///< Owns the transient runtime registry while the scene is playing.
        std::string m_SceneName{ "Untitled" };         ///< Friendly label persisted inside the .trident file header.
        bool m_IsPlaying{ false };                      ///< Indicates whether the scene is currently in play mode.
        size_t m_LoadedEntityCount{ 0 };                ///< Helper counter used for logging during deserialisation.
    };
}