#include "ECS/AnimationSystem.h"

#include "Animation/AnimationAssetService.h"
#include "ECS/Registry.h"
#include "ECS/Components/MeshComponent.h"

#include <functional>
#include <vector>

namespace Trident
{
    namespace ECS
    {
        AnimationSystem::AnimationSystem() : m_Player(Animation::AnimationAssetService::Get())
        {

        }

        void AnimationSystem::RefreshCachedHandles(AnimationComponent& component, Animation::AnimationAssetService& service)
        {
            std::hash<std::string> l_Hasher{};

            const bool l_HasSkeletonId = !component.m_SkeletonAssetId.empty();
            const size_t l_SkeletonHash = l_HasSkeletonId ? l_Hasher(component.m_SkeletonAssetId) : 0;
            if (!l_HasSkeletonId)
            {
                component.m_SkeletonAssetHandle = Animation::AnimationAssetService::s_InvalidHandle;
                component.m_SkeletonAssetHash = 0;
            }
            else if (component.m_SkeletonAssetHandle == Animation::AnimationAssetService::s_InvalidHandle || l_SkeletonHash != component.m_SkeletonAssetHash)
            {
                component.m_SkeletonAssetHash = l_SkeletonHash;
                component.m_SkeletonAssetHandle = service.AcquireSkeleton(component.m_SkeletonAssetId);
            }

            const bool l_HasAnimationId = !component.m_AnimationAssetId.empty();
            const size_t l_AnimationHash = l_HasAnimationId ? l_Hasher(component.m_AnimationAssetId) : 0;
            if (!l_HasAnimationId)
            {
                component.m_AnimationAssetHandle = Animation::AnimationAssetService::s_InvalidHandle;
                component.m_AnimationAssetHash = 0;
                component.m_CurrentClipIndex = Animation::AnimationAssetService::s_InvalidHandle;
            }
            else if (component.m_AnimationAssetHandle == Animation::AnimationAssetService::s_InvalidHandle || l_AnimationHash != component.m_AnimationAssetHash)
            {
                component.m_AnimationAssetHash = l_AnimationHash;
                component.m_AnimationAssetHandle = service.AcquireAnimationLibrary(component.m_AnimationAssetId);
                component.m_CurrentClipIndex = Animation::AnimationAssetService::s_InvalidHandle;
            }

            const bool l_HasClipName = !component.m_CurrentClip.empty();
            const size_t l_ClipHash = l_HasClipName ? l_Hasher(component.m_CurrentClip) : 0;
            if (!l_HasClipName || component.m_AnimationAssetHandle == Animation::AnimationAssetService::s_InvalidHandle)
            {
                component.m_CurrentClipIndex = Animation::AnimationAssetService::s_InvalidHandle;
                component.m_CurrentClipHash = 0;
            }
            else if (l_ClipHash != component.m_CurrentClipHash)
            {
                component.m_CurrentClipHash = l_ClipHash;
                component.m_CurrentClipIndex = service.ResolveClipIndex(component.m_AnimationAssetHandle, component.m_CurrentClip);
            }
        }

        void AnimationSystem::InitialisePose(AnimationComponent& component)
        {
            // Initialise the pose so both runtime rendering and editor preview tools receive a deterministic bind pose.
            component.m_BoneMatrices.clear();

            Animation::AnimationAssetService& l_Service = Animation::AnimationAssetService::Get();
            AnimationSystem::RefreshCachedHandles(component, l_Service);

            if (component.m_StateMachine)
            {
                // Allow authoring tools to warm the pose directly from the configured state machine.
                component.m_StateMachine->SetSkeletonHandle(component.m_SkeletonAssetHandle);
                component.m_StateMachine->SetAnimationLibraryHandle(component.m_AnimationAssetHandle);
                component.m_StateMachine->Update(0.0f);
                component.m_StateMachine->CopyPose(component.m_BoneMatrices);

                return;
            }

            Animation::AnimationPlayer l_Player(l_Service);
            l_Player.SetSkeletonHandle(component.m_SkeletonAssetHandle);
            l_Player.SetAnimationHandle(component.m_AnimationAssetHandle);
            l_Player.SetClipIndex(component.m_CurrentClipIndex);
            l_Player.SetPlaybackSpeed(component.m_PlaybackSpeed);
            l_Player.SetLooping(component.m_IsLooping);
            l_Player.SetIsPlaying(true);
            l_Player.EvaluateAt(0.0f);
            l_Player.CopyPoseTo(component.m_BoneMatrices);
        }

        void AnimationSystem::Update(Registry& registry, float deltaTime)
        {
            const std::vector<Entity>& l_Entities = registry.GetEntities();
            for (Entity it_Entity : l_Entities)
            {
                if (!registry.HasComponent<AnimationComponent>(it_Entity))
                {
                    continue;
                }

                if (!registry.HasComponent<MeshComponent>(it_Entity))
                {
                    // TODO: Extend support for skinned decals or other render paths once the renderer exposes them.
                    continue;
                }

                UpdateComponent(registry, it_Entity, deltaTime);
            }
        }

        void AnimationSystem::UpdateComponent(Registry& registry, Entity entity, float deltaTime)
        {
            AnimationComponent& l_Component = registry.GetComponent<AnimationComponent>(entity);

            Animation::AnimationAssetService& l_AssetService = Animation::AnimationAssetService::Get();
            AnimationSystem::RefreshCachedHandles(l_Component, l_AssetService);

            if (l_Component.m_StateMachine)
            {
                // Drive authored graphs when a state machine is present instead of falling back to raw clip playback.
                l_Component.m_StateMachine->SetSkeletonHandle(l_Component.m_SkeletonAssetHandle);
                l_Component.m_StateMachine->SetAnimationLibraryHandle(l_Component.m_AnimationAssetHandle);

                const float l_DeltaSeconds = l_Component.m_IsPlaying ? deltaTime : 0.0f;
                l_Component.m_StateMachine->Update(l_DeltaSeconds);
                l_Component.m_StateMachine->CopyPose(l_Component.m_BoneMatrices);

                return;
            }

            // Mirror the component state onto the reusable player instance before evaluating the clip.
            m_Player.SetSkeletonHandle(l_Component.m_SkeletonAssetHandle);
            m_Player.SetAnimationHandle(l_Component.m_AnimationAssetHandle);
            m_Player.SetClipIndex(l_Component.m_CurrentClipIndex);
            m_Player.SetPlaybackSpeed(l_Component.m_PlaybackSpeed);
            m_Player.SetLooping(l_Component.m_IsLooping);
            m_Player.SetIsPlaying(l_Component.m_IsPlaying);
            m_Player.SetCurrentTime(l_Component.m_CurrentTime);

            if (l_Component.m_IsPlaying)
            {
                // Deterministically advance the clip using the frame delta.
                m_Player.Update(deltaTime);
            }
            else
            {
                // Maintain the requested sample time for paused previews.
                m_Player.EvaluateAt(l_Component.m_CurrentTime);
            }

            l_Component.m_CurrentTime = m_Player.GetCurrentTime();
            l_Component.m_IsPlaying = m_Player.IsPlaying();

            // The renderer consumes the updated pose directly from the component cache.
            m_Player.CopyPoseTo(l_Component.m_BoneMatrices);
        }
    }
}