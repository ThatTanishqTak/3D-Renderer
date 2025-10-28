#pragma once

#include "Animation/AnimationAssetService.h"
#include "Animation/AnimationBlendTree.h"
#include "Animation/AnimationPose.h"

#include <glm/mat4x4.hpp>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Trident
{
    namespace Animation
    {
        enum class AnimationParameterType
        {
            Float,
            Bool,
            Integer,
            Trigger
        };

        struct AnimationParameter
        {
            AnimationParameterType m_Type = AnimationParameterType::Float;
            float m_FloatValue = 0.0f;
            bool m_BoolValue = false;
            int m_IntValue = 0;
            bool m_TriggerValue = false;

            float AsFloat(float defaultValue) const;
            bool AsBool(bool defaultValue) const;
            int AsInt(int defaultValue) const;
            bool ConsumeTrigger();
            void ResetTrigger();
        };

        enum class AnimationConditionComparison
        {
            Equals,
            NotEquals,
            GreaterThan,
            LessThan,
            GreaterOrEqual,
            LessOrEqual,
            Triggered
        };

        struct AnimationTransitionCondition
        {
            std::string m_ParameterName;
            AnimationConditionComparison m_Comparison = AnimationConditionComparison::Equals;
            float m_FloatValue = 0.0f;
            int m_IntValue = 0;
            bool m_BoolValue = false;
        };

        struct AnimationTransition
        {
            std::string m_TargetState;
            bool m_HasExitTime = false;
            float m_ExitTimeSeconds = 0.0f;
            float m_FadeDurationSeconds = 0.2f;
            std::vector<AnimationTransitionCondition> m_Conditions{};
        };

        struct AnimationState
        {
            explicit AnimationState(std::string name, std::unique_ptr<AnimationBlendNode> rootNode);

            std::string m_Name;
            std::unique_ptr<AnimationBlendNode> m_RootNode;
            std::vector<AnimationTransition> m_Transitions{};
        };

        struct AnimationLayer
        {
            std::string m_Name;
            float m_Weight = 1.0f;
            bool m_IsAdditive = false;
            AnimationMask m_Mask{};
            std::unordered_map<std::string, std::unique_ptr<AnimationState>> m_States{};
            std::string m_EntryState;
            AnimationState* m_CurrentState = nullptr;
            AnimationState* m_NextState = nullptr;
            float m_TimeInState = 0.0f;
            float m_TransitionElapsed = 0.0f;
            float m_TransitionDuration = 0.0f;
            AnimationPose m_LayerPose{};
        };

        class AnimationStateMachine
        {
        public:
            explicit AnimationStateMachine(AnimationAssetService& assetService);

            void SetSkeletonHandle(size_t skeletonHandle);
            void SetAnimationLibraryHandle(size_t animationLibraryHandle);

            void AddFloatParameter(const std::string& name, float defaultValue);
            void AddBoolParameter(const std::string& name, bool defaultValue);
            void AddIntegerParameter(const std::string& name, int defaultValue);
            void AddTriggerParameter(const std::string& name);

            void SetFloatParameter(const std::string& name, float value);
            void SetBoolParameter(const std::string& name, bool value);
            void SetIntegerParameter(const std::string& name, int value);
            void FireTrigger(const std::string& name);
            void ResetTrigger(const std::string& name);

            size_t AddLayer(const std::string& name, float weight, bool isAdditive);
            void SetLayerMask(size_t layerIndex, AnimationMask mask);
            void SetLayerWeight(size_t layerIndex, float weight);
            void SetLayerEntryState(size_t layerIndex, const std::string& stateName);

            AnimationState& AddState(size_t layerIndex, const std::string& stateName, std::unique_ptr<AnimationBlendNode> rootNode);
            AnimationTransition& AddTransition(size_t layerIndex, const std::string& fromState, const AnimationTransition& transition);

            void Update(float deltaSeconds);
            void CopyPose(std::vector<glm::mat4>& outMatrices) const;

        private:
            AnimationState* FindState(AnimationLayer& layer, const std::string& stateName) const;
            void EnsureRestPose();
            void UpdateLayer(AnimationLayer& layer, float deltaSeconds, const Skeleton& skeleton);
            bool EvaluateTransitionConditions(const AnimationTransition& transition);

            AnimationAssetService* m_AssetService = nullptr;
            size_t m_SkeletonHandle = AnimationAssetService::s_InvalidHandle;
            size_t m_AnimationLibraryHandle = AnimationAssetService::s_InvalidHandle;

            std::unordered_map<std::string, AnimationParameter> m_Parameters{};
            std::vector<AnimationLayer> m_Layers{};

            AnimationPose m_RestPose{};
            AnimationPose m_FinalPose{};
            std::vector<glm::mat4> m_SkinningMatrices{};
        };
    }
}