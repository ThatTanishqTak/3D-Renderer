#pragma once

#include "ECS/System.h"
#include "ECS/Entity.h"
#include "ECS/Components/AnimationComponent.h"

#include <glm/mat4x4.hpp>

#include <cstddef>
#include <vector>

namespace Trident
{
    namespace Animation
    {
        class AnimationAssetService;
    }

    namespace ECS
    {
        class AnimationSystem final : public System
        {
        public:
            AnimationSystem() = default;
            void Update(Registry& registry, float deltaTime) override;

            static void RefreshCachedHandles(AnimationComponent& component, Animation::AnimationAssetService& service);
            static void InitialisePose(AnimationComponent& component);

        private:
            void UpdateComponent(Registry& registry, Entity entity, float deltaTime);
            static float ResolveClipDuration(const AnimationComponent& component);
            static size_t ResolveSkeletonBoneCount(const AnimationComponent& component);
            static void SampleClipPose(const AnimationComponent& component, float sampleTime, std::vector<glm::mat4>& outBoneMatrices);
        };
    }
}