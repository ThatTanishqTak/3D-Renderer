#pragma once

#include <glm/mat4x4.hpp>

#include <cstddef>
#include <limits>
#include <string>
#include <vector>

namespace Trident
{
    /**
     * @brief Stores high level animation state for a skinned entity.
     *
     * The component keeps string identifiers for both the skeleton definition and
     * the animation collection so runtime systems can lazily resolve assets without
     * hard dependencies on import-time data structures. Playback controls mirror
     * the tooling terminology to make debugging in the editor intuitive. The bone
     * matrix cache allows the renderer to reuse the most recent pose without
     * recomputing it each frame. Future work can expand this data to support
     * layered animation blending and state machines.
     */
    struct AnimationComponent
    {
        /// Asset identifier describing which skeleton the entity should bind to.
        std::string m_SkeletonAssetId{};
        /// Asset identifier pointing to the animation collection or clip library.
        std::string m_AnimationAssetId{};
        /// Identifier for the animation clip currently playing on this entity.
        std::string m_CurrentClip{};
        /// Normalised playback position measured in seconds within the active clip.
        float m_CurrentTime{ 0.0f };
        /// Scalar multiplier allowing slow motion or fast forward style effects.
        float m_PlaybackSpeed{ 1.0f };
        /// Indicates whether the system should wrap the clip when it reaches the end.
        bool m_IsLooping{ true };
        /// Gate toggled by gameplay to pause or resume animation playback.
        bool m_IsPlaying{ true };
        /// Cached pose matrices representing the final transform of each skeleton bone.
        std::vector<glm::mat4> m_BoneMatrices{};

        /// Cached handle resolving the skeleton asset through the AnimationAssetService.
        size_t m_SkeletonAssetHandle{ std::numeric_limits<size_t>::max() };
        /// Cached handle resolving the animation library through the AnimationAssetService.
        size_t m_AnimationAssetHandle{ std::numeric_limits<size_t>::max() };
        /// Cached index pointing at the resolved clip inside the active animation library.
        size_t m_CurrentClipIndex{ std::numeric_limits<size_t>::max() };

        /// Hash of the last skeleton identifier used to determine whether the cache must refresh.
        size_t m_SkeletonAssetHash{ 0 };
        /// Hash of the last animation identifier used to determine whether the cache must refresh.
        size_t m_AnimationAssetHash{ 0 };
        /// Hash of the last clip identifier used to determine whether the cache must refresh.
        size_t m_CurrentClipHash{ 0 };

        /// @brief Reset cached handles forcing the system to refresh on the next update.
        void InvalidateCachedAssets()
        {
            m_SkeletonAssetHandle = std::numeric_limits<size_t>::max();
            m_AnimationAssetHandle = std::numeric_limits<size_t>::max();
            m_CurrentClipIndex = std::numeric_limits<size_t>::max();
            m_SkeletonAssetHash = 0;
            m_AnimationAssetHash = 0;
            m_CurrentClipHash = 0;
        }
    };
}