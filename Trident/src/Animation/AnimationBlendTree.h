#pragma once

#include "Animation/AnimationAssetService.h"
#include "Animation/AnimationPose.h"

#include <glm/vec2.hpp>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Trident
{
    namespace Animation
    {
        struct AnimationParameter;
        struct AnimationGraphContext
        {
            AnimationAssetService& m_AssetService;
            size_t m_SkeletonHandle = AnimationAssetService::s_InvalidHandle;
            size_t m_AnimationHandle = AnimationAssetService::s_InvalidHandle;
            const std::unordered_map<std::string, AnimationParameter>* m_Parameters = nullptr;
        };

        class AnimationBlendNode
        {
        public:
            virtual ~AnimationBlendNode() = default;

            virtual void Reset();
            virtual AnimationPose Evaluate(const AnimationGraphContext& context, float deltaSeconds) = 0;
        };

        class ClipNode : public AnimationBlendNode
        {
        public:
            ClipNode(size_t clipIndex, bool isLooping, float playbackSpeed);

            void SetSpeedParameter(const std::string& parameterName);

            void Reset() override;
            AnimationPose Evaluate(const AnimationGraphContext& context, float deltaSeconds) override;

        private:
            float ResolveSpeed(const AnimationGraphContext& context) const;

            size_t m_ClipIndex = AnimationAssetService::s_InvalidHandle;
            bool m_IsLooping = true;
            float m_PlaybackSpeed = 1.0f;
            float m_CurrentTime = 0.0f;
            std::string m_SpeedParameter{};
        };

        class BlendNode : public AnimationBlendNode
        {
        public:
            BlendNode(std::unique_ptr<AnimationBlendNode> first, std::unique_ptr<AnimationBlendNode> second, float weight);

            void SetWeightParameter(const std::string& parameterName);

            void Reset() override;
            AnimationPose Evaluate(const AnimationGraphContext& context, float deltaSeconds) override;

        private:
            float ResolveWeight(const AnimationGraphContext& context) const;

            std::unique_ptr<AnimationBlendNode> m_First;
            std::unique_ptr<AnimationBlendNode> m_Second;
            float m_Weight = 0.5f;
            std::string m_WeightParameter{};
        };

        class BlendSpace1DNode : public AnimationBlendNode
        {
        public:
            struct Sample
            {
                size_t m_ClipIndex = AnimationAssetService::s_InvalidHandle;
                float m_Position = 0.0f;
            };

            BlendSpace1DNode(std::vector<Sample> samples, float parameterDefault);

            void SetParameterName(const std::string& parameterName);

            void Reset() override;
            AnimationPose Evaluate(const AnimationGraphContext& context, float deltaSeconds) override;

        private:
            float ResolveParameter(const AnimationGraphContext& context) const;

            std::vector<Sample> m_Samples{};
            float m_ParameterValue = 0.0f;
            float m_CurrentTime = 0.0f;
            std::string m_ParameterName{};
        };
    }
}