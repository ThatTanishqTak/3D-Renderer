#pragma once

#include "ECS/Entity.h"

#include <unordered_map>
#include <typeindex>
#include <memory>

namespace Trident
{
    namespace ECS
    {
        class Registry;

        class IComponentStorage
        {
        public:
            virtual ~IComponentStorage() = default;
            virtual void Remove(Entity entity) = 0;
        };

        template<typename T>
        class ComponentStorage : public IComponentStorage
        {
        public:
            template<typename... Args>
            T& Emplace(Entity entity, Args&&... args)
            {
                return m_Components.emplace(entity, T{ std::forward<Args>(args)... }).first->second;
            }

            bool Has(Entity entity) const
            {
                return m_Components.find(entity) != m_Components.end();
            }

            T& Get(Entity entity)
            {
                return m_Components.at(entity);
            }

            void Remove(Entity entity) override
            {
                m_Components.erase(entity);
            }

        private:
            std::unordered_map<Entity, T> m_Components;
        };

        class Registry
        {
        public:
            Entity CreateEntity()
            {
                return m_NextEntity++;
            }

            void DestroyEntity(Entity entity)
            {
                for (auto& [type, storage] : m_Storages)
                {
                    storage->Remove(entity);
                }
            }

            template<typename T, typename... Args>
            T& AddComponent(Entity entity, Args&&... args)
            {
                auto* storage = GetStorage<T>();
                return storage->Emplace(entity, std::forward<Args>(args)...);
            }

            template<typename T>
            bool HasComponent(Entity entity) const
            {
                auto* storage = GetStorageConst<T>();
                return storage && storage->Has(entity);
            }

            template<typename T>
            T& GetComponent(Entity entity)
            {
                return GetStorage<T>()->Get(entity);
            }

            template<typename T>
            void RemoveComponent(Entity entity)
            {
                if (auto* storage = GetStorage<T>())
                {
                    storage->Remove(entity);
                }
            }

        private:
            template<typename T>
            ComponentStorage<T>* GetStorage()
            {
                std::type_index index(typeid(T));
                auto it = m_Storages.find(index);
                if (it == m_Storages.end())
                {
                    auto storage = std::make_unique<ComponentStorage<T>>();
                    auto* ptr = storage.get();
                    m_Storages.emplace(index, std::move(storage));
                    return ptr;
                }
                return static_cast<ComponentStorage<T>*>(it->second.get());
            }

            template<typename T>
            const ComponentStorage<T>* GetStorageConst() const
            {
                std::type_index index(typeid(T));
                auto it = m_Storages.find(index);
                if (it == m_Storages.end())
                    return nullptr;
                return static_cast<const ComponentStorage<T>*>(it->second.get());
            }

        private:
            std::unordered_map<std::type_index, std::unique_ptr<IComponentStorage>> m_Storages;
            Entity m_NextEntity = 0;
        };
    }
}