#include "ECS/AnimationSystem.h"

#include "ECS/Registry.h"
#include "ECS/Components/MeshComponent.h"

#include <cmath>

namespace Trident
{
    namespace ECS
    {
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
            if (!l_Component.m_IsPlaying)
            {
                return;
            }

            const float l_ScaledDeltaTime = deltaTime * l_Component.m_PlaybackSpeed;
            l_Component.m_CurrentTime += l_ScaledDeltaTime;

            const float l_ClipDuration = ResolveClipDuration(l_Component);
            if (l_ClipDuration > 0.0f)
            {
                if (l_Component.m_CurrentTime > l_ClipDuration)
                {
                    if (l_Component.m_IsLooping)
                    {
                        l_Component.m_CurrentTime = std::fmod(l_Component.m_CurrentTime, l_ClipDuration);
                    }
                    else
                    {
                        l_Component.m_CurrentTime = l_ClipDuration;
                        l_Component.m_IsPlaying = false;
                    }
                }
            }

            SampleClipPose(l_Component, l_Component.m_CurrentTime, l_Component.m_BoneMatrices);
        }

        float AnimationSystem::ResolveClipDuration(const AnimationComponent& component)
        {
            // TODO: Query the animation asset database once it is available in the runtime.
            // For now we assume a one second loop so the editor can preview playback behaviour.
            (void)component;
            return 1.0f;
        }

        size_t AnimationSystem::ResolveSkeletonBoneCount(const AnimationComponent& component)
        {
            // Until the asset pipeline exposes bone counts we derive the size from the cache and provide a minimum of one.
            if (!component.m_BoneMatrices.empty())
            {
                return component.m_BoneMatrices.size();
            }

            return 1;
        }

        void AnimationSystem::SampleClipPose(const AnimationComponent& component, float sampleTime, std::vector<glm::mat4>& outBoneMatrices)
        {
            const size_t l_BoneCount = ResolveSkeletonBoneCount(component);
            outBoneMatrices.resize(l_BoneCount, glm::mat4{ 1.0f });

            // TODO: Replace with real animation sampling logic once clip data is integrated.
            // The placeholder keeps identities to avoid distorting meshes while still exercising the runtime system.
            (void)sampleTime;
        }
    }
}