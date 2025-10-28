#include "Animation/AnimationPlayer.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/common.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace Trident
{
    namespace Animation
    {
        AnimationPlayer::AnimationPlayer(AnimationAssetService& assetService) : m_AssetService(&assetService)
        {

        }

        void AnimationPlayer::SetSkeletonHandle(size_t skeletonHandle)
        {
            m_SkeletonHandle = skeletonHandle;
        }

        void AnimationPlayer::SetAnimationHandle(size_t animationHandle)
        {
            m_AnimationHandle = animationHandle;
        }

        void AnimationPlayer::SetClipIndex(size_t clipIndex)
        {
            m_ClipIndex = clipIndex;
        }

        void AnimationPlayer::SetPlaybackSpeed(float playbackSpeed)
        {
            m_PlaybackSpeed = playbackSpeed;
        }

        void AnimationPlayer::SetLooping(bool isLooping)
        {
            m_IsLooping = isLooping;
        }

        void AnimationPlayer::SetIsPlaying(bool isPlaying)
        {
            m_IsPlaying = isPlaying;
        }

        void AnimationPlayer::SetCurrentTime(float timeSeconds)
        {
            m_CurrentTimeSeconds = timeSeconds;
        }

        void AnimationPlayer::Update(float deltaSeconds)
        {
            if (!m_IsPlaying)
            {
                // Even when paused the current pose must be refreshed for editor previews.
                EvaluatePose(m_CurrentTimeSeconds);

                return;
            }

            const float l_ScaledDelta = deltaSeconds * m_PlaybackSpeed;
            m_CurrentTimeSeconds += l_ScaledDelta;

            const float l_Duration = GetClipDuration();
            if (l_Duration > 0.0f)
            {
                if (m_CurrentTimeSeconds > l_Duration)
                {
                    if (m_IsLooping)
                    {
                        const float l_Mod = std::fmod(m_CurrentTimeSeconds, l_Duration);
                        m_CurrentTimeSeconds = (l_Mod < 0.0f) ? l_Duration + l_Mod : l_Mod;
                    }
                    else
                    {
                        m_CurrentTimeSeconds = l_Duration;
                        m_IsPlaying = false;
                    }
                }
                else if (m_CurrentTimeSeconds < 0.0f)
                {
                    if (m_IsLooping)
                    {
                        const float l_Mod = std::fmod(m_CurrentTimeSeconds, l_Duration);
                        m_CurrentTimeSeconds = (l_Mod < 0.0f) ? l_Duration + l_Mod : l_Mod;
                    }
                    else
                    {
                        m_CurrentTimeSeconds = 0.0f;
                        m_IsPlaying = false;
                    }
                }
            }

            EvaluatePose(m_CurrentTimeSeconds);
        }

        void AnimationPlayer::EvaluateAt(float sampleTimeSeconds)
        {
            m_CurrentTimeSeconds = sampleTimeSeconds;
            EvaluatePose(sampleTimeSeconds);
        }

        float AnimationPlayer::GetCurrentTime() const
        {
            return m_CurrentTimeSeconds;
        }

        bool AnimationPlayer::IsPlaying() const
        {
            return m_IsPlaying;
        }

        float AnimationPlayer::GetClipDuration() const
        {
            if (m_AssetService == nullptr)
            {
                return 0.0f;
            }

            const AnimationClip* l_Clip = m_AssetService->GetClip(m_AnimationHandle, m_ClipIndex);
            if (l_Clip != nullptr)
            {
                return l_Clip->m_DurationSeconds;
            }

            return 0.0f;
        }

        void AnimationPlayer::CopyPoseTo(std::vector<glm::mat4>& outMatrices) const
        {
            outMatrices = m_PoseMatrices;
        }

        void AnimationPlayer::EvaluatePose(float sampleTimeSeconds)
        {
            if (m_AssetService == nullptr)
            {
                // Without a valid asset service fall back to identity matrices to avoid undefined data.
                ResolveFallbackPose();
                return;
            }

            const Skeleton* l_Skeleton = m_AssetService->GetSkeleton(m_SkeletonHandle);
            const AnimationClip* l_Clip = m_AssetService->GetClip(m_AnimationHandle, m_ClipIndex);

            if (l_Skeleton == nullptr || l_Skeleton->m_Bones.empty())
            {
                // Invalid skeletons produce a unit pose so downstream systems can continue running safely.
                ResolveFallbackPose();

                return;
            }

            const size_t l_BoneCount = l_Skeleton->m_Bones.size();
            m_PoseMatrices.resize(l_BoneCount, glm::mat4{ 1.0f });
            m_TranslationScratch.resize(l_BoneCount);
            m_RotationScratch.resize(l_BoneCount);
            m_ScaleScratch.resize(l_BoneCount);
            m_LocalTransforms.resize(l_BoneCount, glm::mat4{ 1.0f });
            m_GlobalTransforms.resize(l_BoneCount, glm::mat4{ 1.0f });

            for (size_t it_Bone = 0; it_Bone < l_BoneCount; ++it_Bone)
            {
                const Bone& l_Bone = l_Skeleton->m_Bones[it_Bone];
                const TransformDecomposition l_Default = DecomposeBindTransform(l_Bone);
                m_TranslationScratch[it_Bone] = l_Default.m_Translation;
                m_RotationScratch[it_Bone] = l_Default.m_Rotation;
                m_ScaleScratch[it_Bone] = l_Default.m_Scale;
            }

            if (l_Clip != nullptr)
            {
                for (const TransformChannel& it_Channel : l_Clip->m_Channels)
                {
                    if (it_Channel.m_BoneIndex < 0)
                    {
                        continue;
                    }

                    const size_t l_BoneIndex = static_cast<size_t>(it_Channel.m_BoneIndex);
                    if (l_BoneIndex >= l_BoneCount)
                    {
                        continue;
                    }

                    // Interpolate authored keyframes so the pose remains smooth regardless of frame rate.
                    m_TranslationScratch[l_BoneIndex] = SampleVectorKeys(it_Channel.m_TranslationKeys, sampleTimeSeconds, m_TranslationScratch[l_BoneIndex]);
                    m_RotationScratch[l_BoneIndex] = SampleQuaternionKeys(it_Channel.m_RotationKeys, sampleTimeSeconds, m_RotationScratch[l_BoneIndex]);
                    m_ScaleScratch[l_BoneIndex] = SampleVectorKeys(it_Channel.m_ScaleKeys, sampleTimeSeconds, m_ScaleScratch[l_BoneIndex]);
                }
            }

            for (size_t it_Bone = 0; it_Bone < l_BoneCount; ++it_Bone)
            {
                const glm::mat4 l_TranslationMatrix = glm::translate(glm::mat4{ 1.0f }, m_TranslationScratch[it_Bone]);
                const glm::mat4 l_RotationMatrix = glm::toMat4(glm::normalize(m_RotationScratch[it_Bone]));
                const glm::mat4 l_ScaleMatrix = glm::scale(glm::mat4{ 1.0f }, m_ScaleScratch[it_Bone]);
                m_LocalTransforms[it_Bone] = l_TranslationMatrix * l_RotationMatrix * l_ScaleMatrix;
            }

            struct WorkItem
            {
                int m_BoneIndex = -1;
                glm::mat4 m_ParentMatrix{ 1.0f };
            };

            std::vector<WorkItem> l_Worklist{};
            l_Worklist.reserve(l_BoneCount);

            if (l_Skeleton->m_RootBoneIndex >= 0 && static_cast<size_t>(l_Skeleton->m_RootBoneIndex) < l_BoneCount)
            {
                // Begin traversal from the known root bone.
                l_Worklist.push_back({ l_Skeleton->m_RootBoneIndex, glm::mat4{ 1.0f } });
            }
            else
            {
                for (size_t it_Bone = 0; it_Bone < l_BoneCount; ++it_Bone)
                {
                    const Bone& l_Bone = l_Skeleton->m_Bones[it_Bone];
                    if (l_Bone.m_ParentIndex < 0)
                    {
                        l_Worklist.push_back({ static_cast<int>(it_Bone), glm::mat4{ 1.0f } });
                    }
                }

                if (l_Worklist.empty())
                {
                    // Degenerate skeletons fall back to the first bone as an artificial root.
                    l_Worklist.push_back({ 0, glm::mat4{ 1.0f } });
                }
            }

            while (!l_Worklist.empty())
            {
                WorkItem l_Item = l_Worklist.back();
                l_Worklist.pop_back();

                if (l_Item.m_BoneIndex < 0)
                {
                    continue;
                }

                const size_t l_Index = static_cast<size_t>(l_Item.m_BoneIndex);
                if (l_Index >= l_BoneCount)
                {
                    continue;
                }

                // Accumulate the transform hierarchy to respect skeletal parenting.
                const glm::mat4 l_Global = l_Item.m_ParentMatrix * m_LocalTransforms[l_Index];
                m_GlobalTransforms[l_Index] = l_Global;

                const Bone& l_Bone = l_Skeleton->m_Bones[l_Index];
                for (int it_Child : l_Bone.m_Children)
                {
                    l_Worklist.push_back({ it_Child, l_Global });
                }
            }

            for (size_t it_Bone = 0; it_Bone < l_BoneCount; ++it_Bone)
            {
                if (m_GlobalTransforms[it_Bone] == glm::mat4{ 1.0f })
                {
                    m_GlobalTransforms[it_Bone] = m_LocalTransforms[it_Bone];
                }

                const glm::mat4& l_InverseBind = l_Skeleton->m_Bones[it_Bone].m_InverseBindMatrix;
                m_PoseMatrices[it_Bone] = m_GlobalTransforms[it_Bone] * l_InverseBind;
            }
        }

        void AnimationPlayer::ResolveFallbackPose()
        {
            // Maintain a stable identity pose when assets are missing so consumers never read garbage.
            const size_t l_FallbackCount = std::max<size_t>(m_PoseMatrices.empty() ? 1 : m_PoseMatrices.size(), 1);
            m_PoseMatrices.assign(l_FallbackCount, glm::mat4{ 1.0f });
        }

        glm::vec3 AnimationPlayer::SampleVectorKeys(const std::vector<VectorKeyframe>& keys, float sampleTime, const glm::vec3& defaultValue)
        {
            if (keys.empty())
            {
                return defaultValue;
            }

            if (keys.size() == 1 || sampleTime <= keys.front().m_TimeSeconds)
            {
                return keys.front().m_Value;
            }

            for (size_t it_Index = 0; it_Index + 1 < keys.size(); ++it_Index)
            {
                const VectorKeyframe& l_Current = keys[it_Index];
                const VectorKeyframe& l_Next = keys[it_Index + 1];
                if (sampleTime < l_Next.m_TimeSeconds)
                {
                    const float l_Denominator = l_Next.m_TimeSeconds - l_Current.m_TimeSeconds;
                    const float l_T = (l_Denominator > std::numeric_limits<float>::epsilon()) ? (sampleTime - l_Current.m_TimeSeconds) / l_Denominator : 0.0f;

                    return glm::mix(l_Current.m_Value, l_Next.m_Value, glm::clamp(l_T, 0.0f, 1.0f));
                }
            }

            return keys.back().m_Value;
        }

        glm::quat AnimationPlayer::SampleQuaternionKeys(const std::vector<QuaternionKeyframe>& keys, float sampleTime, const glm::quat& defaultValue)
        {
            if (keys.empty())
            {
                return defaultValue;
            }

            if (keys.size() == 1 || sampleTime <= keys.front().m_TimeSeconds)
            {
                return glm::normalize(keys.front().m_Value);
            }

            for (size_t it_Index = 0; it_Index + 1 < keys.size(); ++it_Index)
            {
                const QuaternionKeyframe& l_Current = keys[it_Index];
                const QuaternionKeyframe& l_Next = keys[it_Index + 1];
                if (sampleTime < l_Next.m_TimeSeconds)
                {
                    const float l_Denominator = l_Next.m_TimeSeconds - l_Current.m_TimeSeconds;
                    const float l_T = (l_Denominator > std::numeric_limits<float>::epsilon()) ? (sampleTime - l_Current.m_TimeSeconds) / l_Denominator : 0.0f;

                    return glm::normalize(glm::slerp(l_Current.m_Value, l_Next.m_Value, glm::clamp(l_T, 0.0f, 1.0f)));
                }
            }

            return glm::normalize(keys.back().m_Value);
        }

        AnimationPlayer::TransformDecomposition AnimationPlayer::DecomposeBindTransform(const Bone& bone)
        {
            TransformDecomposition l_Result{};
            const glm::mat4& l_Local = bone.m_LocalBindTransform;
            l_Result.m_Translation = glm::vec3(l_Local[3]);

            const glm::vec3 l_Column0 = glm::vec3(l_Local[0]);
            const glm::vec3 l_Column1 = glm::vec3(l_Local[1]);
            const glm::vec3 l_Column2 = glm::vec3(l_Local[2]);

            glm::vec3 l_Scale{};
            l_Scale.x = glm::length(l_Column0);
            l_Scale.y = glm::length(l_Column1);
            l_Scale.z = glm::length(l_Column2);

            glm::mat3 l_RotationMatrix{ 1.0f };
            if (l_Scale.x > std::numeric_limits<float>::epsilon())
            {
                l_RotationMatrix[0] = l_Column0 / l_Scale.x;
            }
            else
            {
                l_RotationMatrix[0] = l_Column0;
            }

            if (l_Scale.y > std::numeric_limits<float>::epsilon())
            {
                l_RotationMatrix[1] = l_Column1 / l_Scale.y;
            }
            else
            {
                l_RotationMatrix[1] = l_Column1;
            }

            if (l_Scale.z > std::numeric_limits<float>::epsilon())
            {
                l_RotationMatrix[2] = l_Column2 / l_Scale.z;
            }
            else
            {
                l_RotationMatrix[2] = l_Column2;
            }

            l_Result.m_Rotation = glm::normalize(glm::quat_cast(l_RotationMatrix));
            l_Result.m_Scale = l_Scale;

            return l_Result;
        }
    }
}