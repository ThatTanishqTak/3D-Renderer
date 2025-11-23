#include "Animation/AnimationStateMachine.h"

#include "Animation/AnimationBlendTree.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/common.hpp>

namespace Trident
{
    namespace Animation
    {
        AnimationState::AnimationState(std::string name, std::unique_ptr<AnimationBlendNode> rootNode) : m_Name(std::move(name)), m_RootNode(std::move(rootNode))
        {

        }

        float AnimationParameter::AsFloat(float defaultValue) const
        {
            switch (m_Type)
            {
            case AnimationParameterType::Float:
                return m_FloatValue;
            case AnimationParameterType::Integer:
                return static_cast<float>(m_IntValue);
            case AnimationParameterType::Bool:
                return m_BoolValue ? 1.0f : 0.0f;
            case AnimationParameterType::Trigger:
                return m_TriggerValue ? 1.0f : 0.0f;
            default:
                return defaultValue;
            }
        }

        bool AnimationParameter::AsBool(bool defaultValue) const
        {
            switch (m_Type)
            {
            case AnimationParameterType::Bool:
                return m_BoolValue;
            case AnimationParameterType::Float:
                return m_FloatValue > 0.0f;
            case AnimationParameterType::Integer:
                return m_IntValue != 0;
            case AnimationParameterType::Trigger:
                return m_TriggerValue;
            default:
                return defaultValue;
            }
        }

        int AnimationParameter::AsInt(int defaultValue) const
        {
            switch (m_Type)
            {
            case AnimationParameterType::Integer:
                return m_IntValue;
            case AnimationParameterType::Float:
                return static_cast<int>(m_FloatValue);
            case AnimationParameterType::Bool:
                return m_BoolValue ? 1 : 0;
            case AnimationParameterType::Trigger:
                return m_TriggerValue ? 1 : 0;
            default:
                return defaultValue;
            }
        }

        bool AnimationParameter::ConsumeTrigger()
        {
            if (m_Type != AnimationParameterType::Trigger)
            {
                return false;
            }

            const bool l_WasActive = m_TriggerValue;
            m_TriggerValue = false;

            return l_WasActive;
        }

        void AnimationParameter::ResetTrigger()
        {
            if (m_Type == AnimationParameterType::Trigger)
            {
                m_TriggerValue = false;
            }
        }

        AnimationStateMachine::AnimationStateMachine(AnimationAssetService& assetService): m_AssetService(&assetService)
        {

        }

        void AnimationStateMachine::SetSkeletonHandle(size_t skeletonHandle)
        {
            m_SkeletonHandle = skeletonHandle;
            EnsureRestPose();
        }

        void AnimationStateMachine::SetAnimationLibraryHandle(size_t animationLibraryHandle)
        {
            m_AnimationLibraryHandle = animationLibraryHandle;
        }

        void AnimationStateMachine::AddFloatParameter(const std::string& name, float defaultValue)
        {
            AnimationParameter& l_Parameter = m_Parameters[name];
            l_Parameter.m_Type = AnimationParameterType::Float;
            l_Parameter.m_FloatValue = defaultValue;
        }

        void AnimationStateMachine::AddBoolParameter(const std::string& name, bool defaultValue)
        {
            AnimationParameter& l_Parameter = m_Parameters[name];
            l_Parameter.m_Type = AnimationParameterType::Bool;
            l_Parameter.m_BoolValue = defaultValue;
        }

        void AnimationStateMachine::AddIntegerParameter(const std::string& name, int defaultValue)
        {
            AnimationParameter& l_Parameter = m_Parameters[name];
            l_Parameter.m_Type = AnimationParameterType::Integer;
            l_Parameter.m_IntValue = defaultValue;
        }

        void AnimationStateMachine::AddTriggerParameter(const std::string& name)
        {
            AnimationParameter& l_Parameter = m_Parameters[name];
            l_Parameter.m_Type = AnimationParameterType::Trigger;
            l_Parameter.m_TriggerValue = false;
        }

        void AnimationStateMachine::SetFloatParameter(const std::string& name, float value)
        {
            AnimationParameter& l_Parameter = m_Parameters[name];
            l_Parameter.m_Type = AnimationParameterType::Float;
            l_Parameter.m_FloatValue = value;
        }

        void AnimationStateMachine::SetBoolParameter(const std::string& name, bool value)
        {
            AnimationParameter& l_Parameter = m_Parameters[name];
            l_Parameter.m_Type = AnimationParameterType::Bool;
            l_Parameter.m_BoolValue = value;
        }

        void AnimationStateMachine::SetIntegerParameter(const std::string& name, int value)
        {
            AnimationParameter& l_Parameter = m_Parameters[name];
            l_Parameter.m_Type = AnimationParameterType::Integer;
            l_Parameter.m_IntValue = value;
        }

        void AnimationStateMachine::FireTrigger(const std::string& name)
        {
            AnimationParameter& l_Parameter = m_Parameters[name];
            l_Parameter.m_Type = AnimationParameterType::Trigger;
            l_Parameter.m_TriggerValue = true;
        }

        void AnimationStateMachine::ResetTrigger(const std::string& name)
        {
            const auto a_Found = m_Parameters.find(name);
            if (a_Found != m_Parameters.end())
            {
                a_Found->second.ResetTrigger();
            }
        }

        size_t AnimationStateMachine::AddLayer(const std::string& name, float weight, bool isAdditive)
        {
            AnimationLayer l_Layer{};
            l_Layer.m_Name = name;
            l_Layer.m_Weight = weight;
            l_Layer.m_IsAdditive = isAdditive;
            // Initialise the mask so callers can opt-in to per-bone weighting immediately.
            l_Layer.m_Mask.Resize(m_RestPose.m_Translations.size());
            m_Layers.push_back(std::move(l_Layer));

            return m_Layers.size() - 1;
        }

        void AnimationStateMachine::SetLayerMask(size_t layerIndex, AnimationMask mask)
        {
            if (layerIndex >= m_Layers.size())
            {
                return;
            }

            m_Layers[layerIndex].m_Mask = std::move(mask);
        }

        void AnimationStateMachine::SetLayerWeight(size_t layerIndex, float weight)
        {
            if (layerIndex >= m_Layers.size())
            {
                return;
            }

            m_Layers[layerIndex].m_Weight = weight;
        }

        void AnimationStateMachine::SetLayerEntryState(size_t layerIndex, const std::string& stateName)
        {
            if (layerIndex >= m_Layers.size())
            {
                return;
            }

            m_Layers[layerIndex].m_EntryState = stateName;
        }

        AnimationState& AnimationStateMachine::AddState(size_t layerIndex, const std::string& stateName, std::unique_ptr<AnimationBlendNode> rootNode)
        {
            AnimationLayer& l_Layer = m_Layers.at(layerIndex);
            auto a_State = std::make_unique<AnimationState>(stateName, std::move(rootNode));
            AnimationState& l_Reference = *a_State;
            l_Layer.m_States[stateName] = std::move(a_State);

            return l_Reference;
        }

        AnimationTransition& AnimationStateMachine::AddTransition(size_t layerIndex, const std::string& fromState, const AnimationTransition& transition)
        {
            AnimationLayer& l_Layer = m_Layers.at(layerIndex);
            AnimationState* l_State = FindState(l_Layer, fromState);
            if (l_State == nullptr)
            {
                throw std::runtime_error("AnimationStateMachine::AddTransition - state not found");
            }

            l_State->m_Transitions.push_back(transition);
            return l_State->m_Transitions.back();
        }

        void AnimationStateMachine::Update(float deltaSeconds)
        {
            const Skeleton* l_Skeleton = m_AssetService->GetSkeleton(m_SkeletonHandle);
            if (l_Skeleton == nullptr)
            {
                return;
            }

            EnsureRestPose();
            m_FinalPose = m_RestPose;

            AnimationGraphContext l_Context{ *m_AssetService, m_SkeletonHandle, m_AnimationLibraryHandle, &m_Parameters };

            for (AnimationLayer& it_Layer : m_Layers)
            {
                // Refresh runtime state to ensure transitions and entry states are honoured before sampling.
                UpdateLayer(it_Layer, deltaSeconds, *l_Skeleton);

                if (it_Layer.m_CurrentState != nullptr && it_Layer.m_CurrentState->m_RootNode != nullptr)
                {
                    it_Layer.m_LayerPose = it_Layer.m_CurrentState->m_RootNode->Evaluate(l_Context, deltaSeconds);
                }
                else
                {
                    it_Layer.m_LayerPose = m_RestPose;
                }

                if (it_Layer.m_NextState != nullptr && it_Layer.m_TransitionDuration > 0.0f)
                {
                    // Blend towards the target state when an in-flight transition is active.
                    AnimationPose l_TargetPose = it_Layer.m_NextState->m_RootNode->Evaluate(l_Context, deltaSeconds);
                    const float l_T = glm::clamp(it_Layer.m_TransitionElapsed / it_Layer.m_TransitionDuration, 0.0f, 1.0f);
                    AnimationPoseUtilities::BlendPose(it_Layer.m_LayerPose, l_TargetPose, l_T, nullptr);
                }

                if (it_Layer.m_IsAdditive)
                {
                    // Additive layers contribute offsets on top of the accumulated pose.
                    AnimationPoseUtilities::AdditivePose(m_FinalPose, it_Layer.m_LayerPose, it_Layer.m_Weight, &it_Layer.m_Mask);
                }
                else
                {
                    // Override layers blend towards their authored motion using the configured mask.
                    AnimationPoseUtilities::BlendPose(m_FinalPose, it_Layer.m_LayerPose, it_Layer.m_Weight, &it_Layer.m_Mask);
                }
            }

            m_SkinningMatrices = AnimationPoseUtilities::ComposeSkinningMatrices(*l_Skeleton, m_FinalPose);
        }

        void AnimationStateMachine::CopyPose(std::vector<glm::mat4>& outMatrices) const
        {
            outMatrices = m_SkinningMatrices;
        }

        AnimationState* AnimationStateMachine::FindState(AnimationLayer& layer, const std::string& stateName) const
        {
            const auto a_Found = layer.m_States.find(stateName);
            if (a_Found != layer.m_States.end())
            {
                return a_Found->second.get();
            }

            return nullptr;
        }

        void AnimationStateMachine::EnsureRestPose()
        {
            if (m_AssetService == nullptr)
            {
                return;
            }

            const Skeleton* l_Skeleton = m_AssetService->GetSkeleton(m_SkeletonHandle);
            if (l_Skeleton == nullptr)
            {
                return;
            }

            if (m_RestPose.m_Translations.size() != l_Skeleton->m_Bones.size())
            {
                // Cache the rest pose so layers always have a deterministic baseline for blending.
                m_RestPose = AnimationPoseUtilities::BuildRestPose(*l_Skeleton);
                m_FinalPose = m_RestPose;
                m_SkinningMatrices = AnimationPoseUtilities::ComposeSkinningMatrices(*l_Skeleton, m_RestPose);
            }
        }

        void AnimationStateMachine::UpdateLayer(AnimationLayer& layer, float deltaSeconds, const Skeleton& skeleton)
        {
            if (layer.m_CurrentState == nullptr)
            {
                // Activate the configured entry state the first time the layer updates.
                layer.m_CurrentState = FindState(layer, layer.m_EntryState);
                if (layer.m_CurrentState != nullptr)
                {
                    layer.m_CurrentState->m_RootNode->Reset();
                    layer.m_TimeInState = 0.0f;
                }
            }

            if (layer.m_CurrentState == nullptr)
            {
                return;
            }

            layer.m_TimeInState += deltaSeconds;

            if (layer.m_NextState != nullptr)
            {
                // Progress active crossfades.
                layer.m_TransitionElapsed += deltaSeconds;
                if (layer.m_TransitionDuration <= 0.0f || layer.m_TransitionElapsed >= layer.m_TransitionDuration)
                {
                    layer.m_CurrentState = layer.m_NextState;
                    layer.m_NextState = nullptr;
                    layer.m_TransitionElapsed = 0.0f;
                    layer.m_TransitionDuration = 0.0f;
                    layer.m_TimeInState = 0.0f;
                }
            }

            for (const AnimationTransition& it_Transition : layer.m_CurrentState->m_Transitions)
            {
                if (layer.m_NextState != nullptr)
                {
                    break;
                }

                if (it_Transition.m_HasExitTime && layer.m_TimeInState < it_Transition.m_ExitTimeSeconds)
                {
                    continue;
                }

                if (!EvaluateTransitionConditions(it_Transition))
                {
                    continue;
                }

                AnimationState* l_TargetState = FindState(layer, it_Transition.m_TargetState);
                if (l_TargetState == nullptr)
                {
                    continue;
                }

                if (l_TargetState->m_RootNode != nullptr)
                {
                    // Restart the new state so transitions always begin from the authored start pose.
                    l_TargetState->m_RootNode->Reset();
                }

                layer.m_NextState = l_TargetState;
                layer.m_TransitionDuration = it_Transition.m_FadeDurationSeconds;
                layer.m_TransitionElapsed = 0.0f;

                if (layer.m_TransitionDuration <= 0.0f)
                {
                    // Zero duration transitions snap immediately to the target state.
                    layer.m_CurrentState = layer.m_NextState;
                    layer.m_NextState = nullptr;
                    layer.m_TimeInState = 0.0f;
                }
            }

            layer.m_Mask.Resize(skeleton.m_Bones.size());
        }

        bool AnimationStateMachine::EvaluateTransitionConditions(const AnimationTransition& transition)
        {
            for (const AnimationTransitionCondition& it_Condition : transition.m_Conditions)
            {
                const auto a_FoundParameter = m_Parameters.find(it_Condition.m_ParameterName);
                if (a_FoundParameter == m_Parameters.end())
                {
                    return false;
                }

                AnimationParameter& l_Parameter = a_FoundParameter->second;

                switch (it_Condition.m_Comparison)
                {
                case AnimationConditionComparison::Equals:
                    if (l_Parameter.m_Type == AnimationParameterType::Bool)
                    {
                        if (l_Parameter.AsBool(false) != it_Condition.m_BoolValue)
                        {
                            return false;
                        }
                    }
                    else if (l_Parameter.m_Type == AnimationParameterType::Integer)
                    {
                        if (l_Parameter.AsInt(0) != it_Condition.m_IntValue)
                        {
                            return false;
                        }
                    }
                    else
                    {
                        if (std::abs(l_Parameter.AsFloat(0.0f) - it_Condition.m_FloatValue) > std::numeric_limits<float>::epsilon())
                        {
                            return false;
                        }
                    }
                    break;
                case AnimationConditionComparison::NotEquals:
                    if (l_Parameter.m_Type == AnimationParameterType::Bool)
                    {
                        if (l_Parameter.AsBool(false) == it_Condition.m_BoolValue)
                        {
                            return false;
                        }
                    }
                    else if (l_Parameter.m_Type == AnimationParameterType::Integer)
                    {
                        if (l_Parameter.AsInt(0) == it_Condition.m_IntValue)
                        {
                            return false;
                        }
                    }
                    else
                    {
                        if (std::abs(l_Parameter.AsFloat(0.0f) - it_Condition.m_FloatValue) <= std::numeric_limits<float>::epsilon())
                        {
                            return false;
                        }
                    }
                    break;
                case AnimationConditionComparison::GreaterThan:
                    if (!(l_Parameter.AsFloat(0.0f) > it_Condition.m_FloatValue))
                    {
                        return false;
                    }
                    break;
                case AnimationConditionComparison::LessThan:
                    if (!(l_Parameter.AsFloat(0.0f) < it_Condition.m_FloatValue))
                    {
                        return false;
                    }
                    break;
                case AnimationConditionComparison::GreaterOrEqual:
                    if (!(l_Parameter.AsFloat(0.0f) >= it_Condition.m_FloatValue))
                    {
                        return false;
                    }
                    break;
                case AnimationConditionComparison::LessOrEqual:
                    if (!(l_Parameter.AsFloat(0.0f) <= it_Condition.m_FloatValue))
                    {
                        return false;
                    }
                    break;
                case AnimationConditionComparison::Triggered:
                    if (!l_Parameter.ConsumeTrigger())
                    {
                        // The trigger was either unset or already consumed by another transition this frame.
                        return false;
                    }
                    break;
                }
            }

            return true;
        }
    }
}