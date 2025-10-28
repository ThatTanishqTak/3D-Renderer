#pragma once

#include "Animation/AnimationPlayer.h"
#include "ECS/System.h"
#include "ECS/Entity.h"
#include "ECS/Components/AnimationComponent.h"


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
            AnimationSystem();
            void Update(Registry& registry, float deltaTime) override;

            static void RefreshCachedHandles(AnimationComponent& component, Animation::AnimationAssetService& service);
            static void InitialisePose(AnimationComponent& component);

        private:
            void UpdateComponent(Registry& registry, Entity entity, float deltaTime);

            Animation::AnimationPlayer m_Player; ///< Shared player reusing scratch buffers for deterministic sampling.
        };
    }
}