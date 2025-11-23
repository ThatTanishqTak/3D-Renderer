#pragma once

#include "Animation/AnimationAssetService.h"

#include <glm/gtc/quaternion.hpp>

#include <vector>

namespace Trident
{
    namespace Animation
    {
        /**
         * @brief Represents a pose expressed as decomposed TRS values for each bone.
         */
        struct AnimationPose
        {
            void Resize(size_t boneCount);

            std::vector<glm::vec3> m_Translations{}; //!< Per-bone translation values.
            std::vector<glm::quat> m_Rotations{};    //!< Per-bone rotation values.
            std::vector<glm::vec3> m_Scales{};       //!< Per-bone scale values.
        };

        /**
         * @brief Lightweight container storing per-bone blend weights to support masking.
         */
        struct AnimationMask
        {
            void Resize(size_t boneCount);
            float GetWeight(size_t boneIndex) const;

            std::vector<float> m_BoneWeights{}; //!< Weight per bone (1.0f means fully influenced).
        };

        /**
         * @brief Utility namespace exposing sampling and blending helpers shared across the animation graph.
         */
        namespace AnimationPoseUtilities
        {
            AnimationPose BuildRestPose(const Skeleton& skeleton);
            AnimationPose SampleClipPose(const Skeleton& skeleton, const AnimationClip& clip, float sampleTimeSeconds);

            void BlendPose(AnimationPose& basePose, const AnimationPose& targetPose, float blendWeight, const AnimationMask* mask);
            void AdditivePose(AnimationPose& basePose, const AnimationPose& additivePose, float additiveWeight, const AnimationMask* mask);

            std::vector<glm::mat4> ComposeSkinningMatrices(const Skeleton& skeleton, const AnimationPose& pose);
        }
    }
}