#pragma once

#include "Animation/AnimationAssetService.h"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstddef>
#include <vector>

namespace Trident
{
    namespace Animation
    {
        /**
         * @brief Deterministic animation player responsible for advancing clip time and producing poses.
         *
         * The player keeps minimal internal state so it can be reused by the ECS runtime as well as
         * editor preview tools. The class resolves skeleton and clip data through the shared
         * AnimationAssetService, performs interpolation between authored keyframes, and exposes the
         * evaluated pose as a cache of matrices ready for GPU upload.
         */
        class AnimationPlayer
        {
        public:
            explicit AnimationPlayer(AnimationAssetService& assetService);

            void SetSkeletonHandle(size_t skeletonHandle);
            void SetAnimationHandle(size_t animationHandle);
            void SetClipIndex(size_t clipIndex);
            void SetPlaybackSpeed(float playbackSpeed);
            void SetLooping(bool isLooping);
            void SetIsPlaying(bool isPlaying);
            void SetCurrentTime(float timeSeconds);

            void Update(float deltaSeconds);
            void EvaluateAt(float sampleTimeSeconds);

            [[nodiscard]] float GetCurrentTime() const;
            [[nodiscard]] bool IsPlaying() const;
            [[nodiscard]] float GetClipDuration() const;
            void CopyPoseTo(std::vector<glm::mat4>& outMatrices) const;

        private:
            void EvaluatePose(float sampleTimeSeconds);
            void ResolveFallbackPose();

            static glm::vec3 SampleVectorKeys(const std::vector<VectorKeyframe>& keys, float sampleTime, const glm::vec3& defaultValue);
            static glm::quat SampleQuaternionKeys(const std::vector<QuaternionKeyframe>& keys, float sampleTime, const glm::quat& defaultValue);

            struct TransformDecomposition
            {
                glm::vec3 m_Translation{ 0.0f };
                glm::quat m_Rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
                glm::vec3 m_Scale{ 1.0f };
            };

            static TransformDecomposition DecomposeBindTransform(const Bone& bone);

            AnimationAssetService* m_AssetService = nullptr; ///< Shared service resolving skeletons and animation clips.
            size_t m_SkeletonHandle = AnimationAssetService::s_InvalidHandle; ///< Runtime skeleton handle.
            size_t m_AnimationHandle = AnimationAssetService::s_InvalidHandle; ///< Runtime animation library handle.
            size_t m_ClipIndex = AnimationAssetService::s_InvalidHandle; ///< Active clip index within the animation library.

            float m_CurrentTimeSeconds = 0.0f; ///< Normalised playback position within the clip.
            float m_PlaybackSpeed = 1.0f; ///< Scalar applied to incoming delta time before advancing playback.
            bool m_IsLooping = true; ///< Indicates whether playback should wrap around when reaching the clip end.
            bool m_IsPlaying = true; ///< Flag toggled by callers to pause or resume animation advancement.

            std::vector<glm::mat4> m_PoseMatrices{}; ///< Cached matrices representing the evaluated pose.
            std::vector<glm::vec3> m_TranslationScratch{}; ///< Scratch buffer storing per-bone translations during evaluation.
            std::vector<glm::quat> m_RotationScratch{}; ///< Scratch buffer storing per-bone rotations during evaluation.
            std::vector<glm::vec3> m_ScaleScratch{}; ///< Scratch buffer storing per-bone scales during evaluation.
            std::vector<glm::mat4> m_LocalTransforms{}; ///< Scratch buffer holding local transforms for each bone.
            std::vector<glm::mat4> m_GlobalTransforms{}; ///< Scratch buffer holding hierarchical global transforms.
        };
    }
}