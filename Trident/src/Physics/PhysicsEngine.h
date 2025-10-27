#pragma once

#include "Core/Utilities.h"

#include <PxPhysicsAPI.h>
#include <cooking/PxCooking.h>

#include <cstdint>
#include <optional>

namespace Trident
{
    namespace Physics
    {
        // RAII helper responsible for owning the PhysX foundation.
        // The foundation must outlive all other PhysX allocations created through it.
        class FoundationWrapper
        {
        public:
            FoundationWrapper() = default;
            ~FoundationWrapper();

            FoundationWrapper(const FoundationWrapper&) = delete;
            FoundationWrapper& operator=(const FoundationWrapper&) = delete;
            FoundationWrapper(FoundationWrapper&&) = delete;
            FoundationWrapper& operator=(FoundationWrapper&&) = delete;

            void Initialize();
            void Shutdown();

            [[nodiscard]] bool IsValid() const { return m_Foundation != nullptr; }
            [[nodiscard]] physx::PxFoundation& Get() const;

        private:
            physx::PxDefaultAllocator m_Allocator;
            physx::PxDefaultErrorCallback m_ErrorCallback;
            physx::PxFoundation* m_Foundation = nullptr;
        };

        // Optional PhysX Visual Debugger bridge.
        // When initialised this keeps the transport and PxPvd instance alive until Shutdown().
        class PvdWrapper
        {
        public:
            PvdWrapper() = default;
            ~PvdWrapper();

            PvdWrapper(const PvdWrapper&) = delete;
            PvdWrapper& operator=(const PvdWrapper&) = delete;
            PvdWrapper(PvdWrapper&&) = delete;
            PvdWrapper& operator=(PvdWrapper&&) = delete;

            void Initialize(physx::PxFoundation& foundation);
            void Shutdown();

            [[nodiscard]] bool IsValid() const { return m_Pvd != nullptr; }
            [[nodiscard]] physx::PxPvd* Get() const { return m_Pvd; }

        private:
            physx::PxPvd* m_Pvd = nullptr;
            physx::PxPvdTransport* m_Transport = nullptr;
        };

        // Primary physics SDK wrapper which owns PxPhysics.
        // The physics core depends on the foundation and must be destroyed after scenes and cooking data.
        class PhysicsWrapper
        {
        public:
            PhysicsWrapper() = default;
            ~PhysicsWrapper();

            PhysicsWrapper(const PhysicsWrapper&) = delete;
            PhysicsWrapper& operator=(const PhysicsWrapper&) = delete;
            PhysicsWrapper(PhysicsWrapper&&) = delete;
            PhysicsWrapper& operator=(PhysicsWrapper&&) = delete;

            void Initialize(physx::PxFoundation& foundation, physx::PxPvd* pvd);
            void Shutdown();

            [[nodiscard]] bool IsValid() const { return m_Physics != nullptr; }
            [[nodiscard]] physx::PxPhysics& Get() const;

        private:
            physx::PxPhysics* m_Physics = nullptr;
        };

        // Cooking service used to bake meshes for efficient runtime consumption.
        // Depends on the foundation and uses tolerances derived from the core physics instance.
        class CookingWrapper
        {
        public:
            CookingWrapper() = default;
            ~CookingWrapper();

            CookingWrapper(const CookingWrapper&) = delete;
            CookingWrapper& operator=(const CookingWrapper&) = delete;
            CookingWrapper(CookingWrapper&&) = delete;
            CookingWrapper& operator=(CookingWrapper&&) = delete;

            void Initialize(physx::PxFoundation& foundation, physx::PxPhysics& physics);
            void Shutdown();

            [[nodiscard]] bool IsValid() const { return m_CookingParams.has_value(); }
            [[nodiscard]] const physx::PxCookingParams& GetParams() const;
            [[nodiscard]] physx::PxInsertionCallback& GetStandaloneInsertionCallback() const;

        private:
            std::optional<physx::PxCookingParams> m_CookingParams;
        };

        // Default CPU dispatcher for PhysX task execution.
        // The dispatcher must be destroyed after scenes stop submitting work.
        class DispatcherWrapper
        {
        public:
            DispatcherWrapper() = default;
            ~DispatcherWrapper();

            DispatcherWrapper(const DispatcherWrapper&) = delete;
            DispatcherWrapper& operator=(const DispatcherWrapper&) = delete;
            DispatcherWrapper(DispatcherWrapper&&) = delete;
            DispatcherWrapper& operator=(DispatcherWrapper&&) = delete;

            void Initialize(uint32_t threadCount);
            void Shutdown();

            [[nodiscard]] bool IsValid() const { return m_Dispatcher != nullptr; }
            [[nodiscard]] physx::PxCpuDispatcher& Get() const;

        private:
            physx::PxCpuDispatcher* m_Dispatcher = nullptr;
        };

        // Aggregates all PhysX core services behind a single bootstrap point so engine code can
        // remain agnostic about low level initialisation order. This service owns all wrappers and
        // is safe to construct on the stack inside Startup.
        class PhysicsEngine
        {
        public:
            PhysicsEngine() = default;
            explicit PhysicsEngine(bool enablePvd);
            ~PhysicsEngine();

            PhysicsEngine(const PhysicsEngine&) = delete;
            PhysicsEngine& operator=(const PhysicsEngine&) = delete;
            PhysicsEngine(PhysicsEngine&&) = delete;
            PhysicsEngine& operator=(PhysicsEngine&&) = delete;

            void Initialize(bool enablePvd = false);
            void Shutdown();

            [[nodiscard]] bool IsInitialized() const { return m_Initialized; }

            [[nodiscard]] FoundationWrapper& GetFoundation() { return m_Foundation; }
            [[nodiscard]] const FoundationWrapper& GetFoundation() const { return m_Foundation; }
            [[nodiscard]] PhysicsWrapper& GetPhysics() { return m_Physics; }
            [[nodiscard]] const PhysicsWrapper& GetPhysics() const { return m_Physics; }
            [[nodiscard]] CookingWrapper& GetCooking() { return m_Cooking; }
            [[nodiscard]] const CookingWrapper& GetCooking() const { return m_Cooking; }
            [[nodiscard]] DispatcherWrapper& GetDispatcher() { return m_Dispatcher; }
            [[nodiscard]] const DispatcherWrapper& GetDispatcher() const { return m_Dispatcher; }
            [[nodiscard]] PvdWrapper& GetPvd() { return m_Pvd; }
            [[nodiscard]] const PvdWrapper& GetPvd() const { return m_Pvd; }

        private:
            bool m_Initialized = false;
            bool m_EnablePvd = false;

            FoundationWrapper m_Foundation;
            PvdWrapper m_Pvd;
            PhysicsWrapper m_Physics;
            CookingWrapper m_Cooking;
            DispatcherWrapper m_Dispatcher;
        };
    }
}