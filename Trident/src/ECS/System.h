#pragma once

namespace Trident
{
    namespace ECS
    {
        class Registry; // Forward declaration

        class System
        {
        public:
            virtual ~System() = default;
            virtual void Update(Registry& registry, float deltaTime) = 0;
        };
    }
}