#include "Animation/AnimationBlendTree.h"

#include "Animation/AnimationPose.h"
#include "Animation/AnimationStateMachine.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/common.hpp>

namespace Trident
{
    namespace Animation
    {
        void AnimationBlendNode::Reset()
        {

        }

        ClipNode::ClipNode(size_t clipIndex, bool isLooping, float playbackSpeed) : m_ClipIndex(clipIndex), m_IsLooping(isLooping), m_PlaybackSpeed(playbackSpeed)
        {

        }

        void ClipNode::SetSpeedParameter(const std::string& parameterName)
        {
            m_SpeedParameter = parameterName;
        }

        void ClipNode::Reset()
        {
            m_CurrentTime = 0.0f;
        }

        AnimationPose ClipNode::Evaluate(const AnimationGraphContext& context, float deltaSeconds)
        {
            const Skeleton* l_Skeleton = context.m_AssetService.GetSkeleton(context.m_SkeletonHandle);
            if (l_Skeleton == nullptr)
            {
                AnimationPose l_EmptyPose{};
                return l_EmptyPose;
            }

            const AnimationClip* l_Clip = context.m_AssetService.GetClip(context.m_AnimationHandle, m_ClipIndex);
            if (l_Clip == nullptr)
            {
                return AnimationPoseUtilities::BuildRestPose(*l_Skeleton);
            }

            const float l_Speed = ResolveSpeed(context);
            const float l_Delta = deltaSeconds * l_Speed;
            // Advance the node's local time and wrap or clamp according to the requested mode.
            m_CurrentTime += l_Delta;

            if (l_Clip->m_DurationSeconds > 0.0f)
            {
                if (m_IsLooping)
                {
                    const float l_Mod = std::fmod(m_CurrentTime, l_Clip->m_DurationSeconds);
                    m_CurrentTime = (l_Mod < 0.0f) ? l_Clip->m_DurationSeconds + l_Mod : l_Mod;
                }
                else
                {
                    m_CurrentTime = std::clamp(m_CurrentTime, 0.0f, l_Clip->m_DurationSeconds);
                }
            }

            return AnimationPoseUtilities::SampleClipPose(*l_Skeleton, *l_Clip, m_CurrentTime);
        }

        float ClipNode::ResolveSpeed(const AnimationGraphContext& context) const
        {
            if (m_SpeedParameter.empty() || context.m_Parameters == nullptr)
            {
                return m_PlaybackSpeed;
            }

            const auto a_Found = context.m_Parameters->find(m_SpeedParameter);
            if (a_Found == context.m_Parameters->end())
            {
                return m_PlaybackSpeed;
            }

            return a_Found->second.AsFloat(m_PlaybackSpeed);
        }

        BlendNode::BlendNode(std::unique_ptr<AnimationBlendNode> first, std::unique_ptr<AnimationBlendNode> second, float weight) : m_First(std::move(first)), 
            m_Second(std::move(second)), m_Weight(weight)
        {

        }

        void BlendNode::SetWeightParameter(const std::string& parameterName)
        {
            m_WeightParameter = parameterName;
        }

        void BlendNode::Reset()
        {
            if (m_First)
            {
                m_First->Reset();
            }
            if (m_Second)
            {
                m_Second->Reset();
            }
        }

        AnimationPose BlendNode::Evaluate(const AnimationGraphContext& context, float deltaSeconds)
        {
            AnimationPose l_BasePose{};
            if (m_First)
            {
                l_BasePose = m_First->Evaluate(context, deltaSeconds);
            }

            if (m_Second)
            {
                // Blend towards the secondary node using the resolved weight.
                AnimationPose l_TargetPose = m_Second->Evaluate(context, deltaSeconds);
                AnimationPoseUtilities::BlendPose(l_BasePose, l_TargetPose, ResolveWeight(context), nullptr);
            }

            return l_BasePose;
        }

        float BlendNode::ResolveWeight(const AnimationGraphContext& context) const
        {
            if (m_WeightParameter.empty() || context.m_Parameters == nullptr)
            {
                return glm::clamp(m_Weight, 0.0f, 1.0f);
            }

            const auto a_Found = context.m_Parameters->find(m_WeightParameter);
            if (a_Found == context.m_Parameters->end())
            {
                return glm::clamp(m_Weight, 0.0f, 1.0f);
            }

            return glm::clamp(a_Found->second.AsFloat(m_Weight), 0.0f, 1.0f);
        }

        BlendSpace1DNode::BlendSpace1DNode(std::vector<Sample> samples, float parameterDefault)
            : m_Samples(std::move(samples)), m_ParameterValue(parameterDefault)
        {
            std::sort(m_Samples.begin(), m_Samples.end(), [](const Sample& lhs, const Sample& rhs)
                {
                    return lhs.m_Position < rhs.m_Position;
                });
        }

        void BlendSpace1DNode::SetParameterName(const std::string& parameterName)
        {
            m_ParameterName = parameterName;
        }

        void BlendSpace1DNode::Reset()
        {
            m_CurrentTime = 0.0f;
        }

        AnimationPose BlendSpace1DNode::Evaluate(const AnimationGraphContext& context, float deltaSeconds)
        {
            const Skeleton* l_Skeleton = context.m_AssetService.GetSkeleton(context.m_SkeletonHandle);
            if (l_Skeleton == nullptr || m_Samples.empty())
            {
                AnimationPose l_EmptyPose{};

                return l_EmptyPose;
            }

            m_CurrentTime += deltaSeconds;

            const float l_Parameter = ResolveParameter(context);

            const Sample* l_Left = &m_Samples.front();
            const Sample* l_Right = &m_Samples.back();

            for (size_t it_Index = 0; it_Index + 1 < m_Samples.size(); ++it_Index)
            {
                const Sample& l_Current = m_Samples[it_Index];
                const Sample& l_Next = m_Samples[it_Index + 1];
                if (l_Parameter >= l_Current.m_Position && l_Parameter <= l_Next.m_Position)
                {
                    l_Left = &l_Current;
                    l_Right = &l_Next;

                    break;
                }
            }

            const AnimationClip* l_LeftClip = context.m_AssetService.GetClip(context.m_AnimationHandle, l_Left->m_ClipIndex);
            const AnimationClip* l_RightClip = context.m_AssetService.GetClip(context.m_AnimationHandle, l_Right->m_ClipIndex);
            if (l_LeftClip == nullptr || l_RightClip == nullptr)
            {
                return AnimationPoseUtilities::BuildRestPose(*l_Skeleton);
            }

            AnimationPose l_LeftPose = AnimationPoseUtilities::SampleClipPose(*l_Skeleton, *l_LeftClip, m_CurrentTime);
            AnimationPose l_RightPose = AnimationPoseUtilities::SampleClipPose(*l_Skeleton, *l_RightClip, m_CurrentTime);

            const float l_Denominator = std::max(l_Right->m_Position - l_Left->m_Position, std::numeric_limits<float>::epsilon());
            const float l_T = glm::clamp((l_Parameter - l_Left->m_Position) / l_Denominator, 0.0f, 1.0f);

            // Linearly interpolate between the neighbouring samples to form a continuous blend space.
            AnimationPoseUtilities::BlendPose(l_LeftPose, l_RightPose, l_T, nullptr);

            return l_LeftPose;
        }

        float BlendSpace1DNode::ResolveParameter(const AnimationGraphContext& context) const
        {
            if (m_ParameterName.empty() || context.m_Parameters == nullptr)
            {
                return m_ParameterValue;
            }

            const auto a_Found = context.m_Parameters->find(m_ParameterName);
            if (a_Found == context.m_Parameters->end())
            {
                return m_ParameterValue;
            }

            return a_Found->second.AsFloat(m_ParameterValue);
        }
    }
}