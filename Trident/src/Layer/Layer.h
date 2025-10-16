#pragma once

namespace Trident
{
    /**
     * Base interface that lets the engine drive user-defined layers without exposing heavy systems headers.
     * Implementations can hook into the lifecycle stages below to allocate resources, update state, and render.
     */
    class Layer
    {
    public:
        virtual ~Layer() = default;

        /**
         * Called once when the engine finishes bootstrapping so the layer can acquire resources.
         */
        virtual void Initialize() = 0;

        /**
         * Called once when the engine shuts down, mirroring Initialize for cleanup.
         */
        virtual void Shutdown() = 0;

        /**
         * Called every frame before rendering so the layer can advance gameplay or editor logic.
         */
        virtual void Update() = 0;

        /**
         * Called every frame after Update so the layer can record draw commands.
         */
        virtual void Render() = 0;
    };
}