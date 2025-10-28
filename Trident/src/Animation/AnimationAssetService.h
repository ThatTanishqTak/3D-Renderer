#pragma once

#include "Animation/AnimationData.h"

#include <cstddef>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

namespace Trident
{
    namespace Animation
    {
        /**
         * @brief Lightweight cache providing access to skeleton and animation clip assets.
         *
         * The service keeps a persistent mapping between high level asset identifiers and the
         * baked data produced by Loader::ModelLoader. Runtime systems query the service to
         * resolve handles up front and then reuse those handles every frame, avoiding repeated
         * string lookups or disk access. A simple singleton keeps the implementation compact
         * while leaving the door open to future hot-reload or streaming behaviour.
         */
        class AnimationAssetService
        {
        public:
            /// Sentinel value representing an invalid handle or index.
            static constexpr size_t s_InvalidHandle = std::numeric_limits<size_t>::max();

            /// @brief Access the global service instance.
            static AnimationAssetService& Get();

            /// @brief Request a skeleton asset be loaded and return a lightweight handle to it.
            size_t AcquireSkeleton(const std::string& skeletonAssetId);

            /// @brief Request an animation library be loaded and return a lightweight handle to it.
            size_t AcquireAnimationLibrary(const std::string& animationAssetId);

            /// @brief Resolve a clip index inside an animation library using a cached handle.
            size_t ResolveClipIndex(size_t animationHandle, const std::string& clipName) const;

            /// @brief Fetch a skeleton pointer from an acquired handle.
            const Skeleton* GetSkeleton(size_t skeletonHandle) const;

            /// @brief Fetch a vector of clips from an acquired handle.
            const std::vector<AnimationClip>* GetAnimationClips(size_t animationHandle) const;

            /// @brief Resolve a single clip pointer from a handle/index pair.
            const AnimationClip* GetClip(size_t animationHandle, size_t clipIndex) const;

        private:
            AnimationAssetService() = default;

            struct AssetRecord
            {
                std::string m_AssetId;                                 ///< Original identifier used as the lookup key.
                size_t m_Handle = s_InvalidHandle;                     ///< Unique handle handed back to ECS components.
                Skeleton m_Skeleton{};                                 ///< Skeleton hierarchy baked from the asset.
                std::vector<AnimationClip> m_Clips{};                  ///< Animation clips authored in the asset.
                std::unordered_map<std::string, size_t> m_ClipLookup;  ///< Mapping from clip name to clip index.
            };

            AssetRecord* LoadAssetIfNeeded(const std::string& assetId);

            size_t m_NextHandle = 1;                                   ///< Incrementing counter to keep handles stable.
            std::unordered_map<std::string, size_t> m_IdToHandle{};    ///< Mapping from asset identifier to cached handle.
            std::unordered_map<size_t, AssetRecord> m_Assets{};        ///< Storage for loaded skeleton/clip data.
        };
    }
}