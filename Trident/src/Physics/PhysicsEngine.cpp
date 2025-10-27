#include "PhysicsEngine.h"

#include <algorithm>
#include <stdexcept>
#include <thread>

namespace Trident
{
    namespace Physics
    {
        FoundationWrapper::~FoundationWrapper()
        {
            Shutdown();
        }

        void FoundationWrapper::Initialize()
        {
            if (m_Foundation != nullptr)
            {
                // Avoid reinitialising the PhysX foundation while keeping deterministic behaviour.
                TR_CORE_WARN("PhysX foundation already initialised");
                return;
            }

            m_Foundation = PxCreateFoundation(PX_PHYSICS_VERSION, m_Allocator, m_ErrorCallback);

            if (m_Foundation == nullptr)
            {
                TR_CORE_CRITICAL("Failed to create PhysX foundation");
                throw std::runtime_error("PxCreateFoundation returned null");
            }
        }

        void FoundationWrapper::Shutdown()
        {
            if (m_Foundation != nullptr)
            {
                m_Foundation->release();
                m_Foundation = nullptr;
            }
        }

        physx::PxFoundation& FoundationWrapper::Get() const
        {
            if (m_Foundation == nullptr)
            {
                throw std::runtime_error("PhysX foundation not initialised");
            }

            return *m_Foundation;
        }

        PvdWrapper::~PvdWrapper()
        {
            Shutdown();
        }

        void PvdWrapper::Initialize(physx::PxFoundation& foundation)
        {
            if (m_Pvd != nullptr)
            {
                // Repeated initialisation attempts are ignored intentionally so debug tooling can call freely.
                TR_CORE_WARN("PhysX PVD already initialised");
                return;
            }

            m_Pvd = PxCreatePvd(foundation);
            if (m_Pvd == nullptr)
            {
                TR_CORE_ERROR("Failed to create PhysX PVD instance");
                return;
            }

            physx::PxPvdTransport* l_Transport = physx::PxDefaultPvdSocketTransportCreate("127.0.0.1", 5425, 10);
            if (l_Transport == nullptr)
            {
                TR_CORE_WARN("PhysX PVD transport creation failed");
                m_Pvd->release();
                m_Pvd = nullptr;
                return;
            }

            if (!m_Pvd->connect(*l_Transport, physx::PxPvdInstrumentationFlag::eALL))
            {
                TR_CORE_WARN("PhysX PVD failed to connect to remote debugger");
                l_Transport->release();
                m_Pvd->release();
                m_Pvd = nullptr;
                return;
            }

            m_Transport = l_Transport;
        }

        void PvdWrapper::Shutdown()
        {
            if (m_Pvd != nullptr)
            {
                if (m_Transport != nullptr)
                {
                    if (m_Pvd->isConnected())
                    {
                        m_Pvd->disconnect();
                    }

                    m_Transport->release();
                    m_Transport = nullptr;
                }

                m_Pvd->release();
                m_Pvd = nullptr;
            }
        }

        PhysicsWrapper::~PhysicsWrapper()
        {
            Shutdown();
        }

        void PhysicsWrapper::Initialize(physx::PxFoundation& foundation, physx::PxPvd* pvd)
        {
            if (m_Physics != nullptr)
            {
                TR_CORE_WARN("PhysX physics core already initialised");
                return;
            }

            physx::PxTolerancesScale l_Scale;
            // PhysX expects sensible tolerances before creating the SDK; defaults are tuned for metres/kilograms.
            l_Scale.length = 1.0f;
            l_Scale.speed = 9.81f;

            m_Physics = PxCreatePhysics(PX_PHYSICS_VERSION, foundation, l_Scale, true, pvd);
            if (m_Physics == nullptr)
            {
                TR_CORE_CRITICAL("Failed to create PhysX SDK");
                throw std::runtime_error("PxCreatePhysics returned null");
            }
        }

        void PhysicsWrapper::Shutdown()
        {
            if (m_Physics != nullptr)
            {
                m_Physics->release();
                m_Physics = nullptr;
            }
        }

        physx::PxPhysics& PhysicsWrapper::Get() const
        {
            if (m_Physics == nullptr)
            {
                throw std::runtime_error("PhysX SDK not initialised");
            }

            return *m_Physics;
        }

        CookingWrapper::~CookingWrapper()
        {
            Shutdown();
        }

        void CookingWrapper::Initialize(physx::PxFoundation& foundation, physx::PxPhysics& physics)
        {
            PX_UNUSED(foundation); // The standalone cooking entry points do not require the foundation directly.

            if (m_CookingParams.has_value())
            {
                TR_CORE_WARN("PhysX cooking already initialised");
                return;
            }

            physx::PxCookingParams l_Params(physics.getTolerancesScale());
            // Enable mesh pre-processing so cooked assets work well with runtime scene queries.
            l_Params.meshPreprocessParams = physx::PxMeshPreprocessingFlags(physx::PxMeshPreprocessingFlag::eWELD_VERTICES | physx::PxMeshPreprocessingFlag::eFORCE_32BIT_INDICES);
            // Store the configuration so future cooking calls can use the PhysX helper functions directly.
            m_CookingParams = l_Params;
        }

        void CookingWrapper::Shutdown()
        {
            // Reset the optional parameters to indicate that the wrapper is inactive.
            m_CookingParams.reset();
        }

        const physx::PxCookingParams& CookingWrapper::GetParams() const
        {
            if (!m_CookingParams.has_value())
            {
                throw std::runtime_error("PhysX cooking not initialised");
            }

            return *m_CookingParams;
        }

        physx::PxInsertionCallback& CookingWrapper::GetStandaloneInsertionCallback() const
        {
            physx::PxInsertionCallback* l_Callback = PxGetStandaloneInsertionCallback();
            if (l_Callback == nullptr)
            {
                throw std::runtime_error("PxGetStandaloneInsertionCallback returned null");
            }

            return *l_Callback;
        }

        DispatcherWrapper::~DispatcherWrapper()
        {
            Shutdown();
        }

        void DispatcherWrapper::Initialize(uint32_t threadCount)
        {
            if (m_Dispatcher != nullptr)
            {
                TR_CORE_WARN("PhysX dispatcher already initialised");
                return;
            }

            uint32_t l_ThreadCount = threadCount > 0 ? threadCount : 1;
            m_Dispatcher = physx::PxDefaultCpuDispatcherCreate(l_ThreadCount);
            if (m_Dispatcher == nullptr)
            {
                TR_CORE_CRITICAL("Failed to create PhysX CPU dispatcher");
                throw std::runtime_error("PxDefaultCpuDispatcherCreate returned null");
            }
        }

        void DispatcherWrapper::Shutdown()
        {
            if (m_Dispatcher != nullptr)
            {
                m_Dispatcher = nullptr;
            }
        }

        physx::PxCpuDispatcher& DispatcherWrapper::Get() const
        {
            if (m_Dispatcher == nullptr)
            {
                throw std::runtime_error("PhysX dispatcher not initialised");
            }

            return *m_Dispatcher;
        }

        PhysicsEngine::PhysicsEngine(bool enablePvd)
        {
            Initialize(enablePvd);
        }

        PhysicsEngine::~PhysicsEngine()
        {
            Shutdown();
        }

        void PhysicsEngine::Initialize(bool enablePvd)
        {
            if (m_Initialized)
            {
                TR_CORE_WARN("Physics engine already initialised");
                return;
            }

            m_Foundation.Initialize();

            if (enablePvd)
            {
                m_Pvd.Initialize(m_Foundation.Get());
                m_EnablePvd = m_Pvd.IsValid();
            }
            else
            {
                m_EnablePvd = false;
            }

            m_Physics.Initialize(m_Foundation.Get(), m_EnablePvd ? m_Pvd.Get() : nullptr);
            m_Cooking.Initialize(m_Foundation.Get(), m_Physics.Get());

            uint32_t l_ThreadCount = std::max(1u, static_cast<uint32_t>(std::thread::hardware_concurrency()));
            m_Dispatcher.Initialize(l_ThreadCount);

            m_Initialized = true;

            TR_CORE_INFO("PhysX engine initialised (Threads: {}, PVD: {})", l_ThreadCount, m_EnablePvd ? "Enabled" : "Disabled");
        }

        void PhysicsEngine::Shutdown()
        {
            if (!m_Initialized)
            {
                return;
            }

            TR_CORE_TRACE("Shutting down PhysX engine");

            m_Dispatcher.Shutdown();
            m_Cooking.Shutdown();
            m_Physics.Shutdown();
            m_Pvd.Shutdown();
            m_Foundation.Shutdown();

            m_Initialized = false;
            m_EnablePvd = false;
        }
    }
}