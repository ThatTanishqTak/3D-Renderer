#pragma once

#include "ECS/Entity.h"
#include "ECS/Components/RelationshipComponent.h"
#include "ECS/Components/TransformComponent.h"

#include <unordered_map>
#include <typeindex>
#include <memory>
#include <vector>
#include <algorithm>

#include <glm/gtc/matrix_transform.hpp>

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
            virtual void Clear() = 0;
            virtual std::unique_ptr<IComponentStorage> Clone() const = 0;
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

            void Clear() override
            {
                m_Components.clear();
            }

            std::unique_ptr<IComponentStorage> Clone() const override
            {
                auto l_Copy = std::make_unique<ComponentStorage<T>>();
                l_Copy->m_Components = m_Components;

                return l_Copy;
            }

        private:
            std::unordered_map<Entity, T> m_Components;
        };

        class Registry
        {
        public:
            Entity CreateEntity()
            {
                Entity l_Entity = m_NextEntity++;
                m_ActiveEntities.push_back(l_Entity);

                return l_Entity;
            }

            void DestroyEntity(Entity entity)
            {
                if (HasComponent<RelationshipComponent>(entity))
                {
                    RelationshipComponent& l_Relationship = GetComponent<RelationshipComponent>(entity);
                    const Entity l_Parent = l_Relationship.m_Parent;
                    if (l_Parent != RelationshipComponent::GetInvalidEntity() && HasComponent<RelationshipComponent>(l_Parent))
                    {
                        RelationshipComponent& l_ParentRelationship = GetComponent<RelationshipComponent>(l_Parent);
                        auto l_RemoveIt = std::remove(l_ParentRelationship.m_Children.begin(), l_ParentRelationship.m_Children.end(), entity);
                        l_ParentRelationship.m_Children.erase(l_RemoveIt, l_ParentRelationship.m_Children.end());
                    }

                    for (Entity it_Child : l_Relationship.m_Children)
                    {
                        if (HasComponent<RelationshipComponent>(it_Child))
                        {
                            RelationshipComponent& l_ChildRelationship = GetComponent<RelationshipComponent>(it_Child);
                            l_ChildRelationship.m_Parent = RelationshipComponent::GetInvalidEntity();
                        }
                    }
                }

                for (auto& it_Pair : m_Storages)
                {
                    it_Pair.second->Remove(entity);
                }

                auto l_It = std::remove(m_ActiveEntities.begin(), m_ActiveEntities.end(), entity);
                if (l_It != m_ActiveEntities.end())
                {
                    m_ActiveEntities.erase(l_It, m_ActiveEntities.end());
                }
            }

            void Clear()
            {
                for (auto& it_Pair : m_Storages)
                {
                    it_Pair.second->Clear();
                }

                m_ActiveEntities.clear();
                m_NextEntity = 0;
            }

            void CopyFrom(const Registry& source)
            {
                if (this == &source)
                {
                    return;
                }

                // Rebuild the destination from scratch so stale components never leak between play sessions.
                m_Storages.clear();

                for (const auto& it_Pair : source.m_Storages)
                {
                    if (it_Pair.second)
                    {
                        m_Storages.emplace(it_Pair.first, it_Pair.second->Clone());
                    }
                }

                m_ActiveEntities = source.m_ActiveEntities;
                m_NextEntity = source.m_NextEntity;

                // Future work: allow callers to request only specific component types to reduce copy costs for huge scenes.
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

            const std::vector<Entity>& GetEntities() const
            {
                // Provide read-only access for editor tooling to enumerate active entities.
                return m_ActiveEntities;
            }

            void AttachChild(Entity parent, Entity child)
            {
                if (parent == child)
                {
                    return;
                }

                RelationshipComponent& l_ChildRelationship = EnsureRelationshipComponent(child);
                if (l_ChildRelationship.m_Parent == parent)
                {
                    return;
                }

                if (l_ChildRelationship.m_Parent != RelationshipComponent::GetInvalidEntity())
                {
                    DetachChild(child);
                }

                RelationshipComponent& l_ParentRelationship = EnsureRelationshipComponent(parent);
                auto l_FoundIt = std::find(l_ParentRelationship.m_Children.begin(), l_ParentRelationship.m_Children.end(), child);
                if (l_FoundIt == l_ParentRelationship.m_Children.end())
                {
                    l_ParentRelationship.m_Children.push_back(child);
                }

                l_ChildRelationship.m_Parent = parent;
            }

            void DetachChild(Entity child)
            {
                if (!HasComponent<RelationshipComponent>(child))
                {
                    return;
                }

                RelationshipComponent& l_ChildRelationship = GetComponent<RelationshipComponent>(child);
                const Entity l_Parent = l_ChildRelationship.m_Parent;
                if (l_Parent != RelationshipComponent::GetInvalidEntity() && HasComponent<RelationshipComponent>(l_Parent))
                {
                    RelationshipComponent& l_ParentRelationship = GetComponent<RelationshipComponent>(l_Parent);
                    auto l_RemoveIt = std::remove(l_ParentRelationship.m_Children.begin(), l_ParentRelationship.m_Children.end(), child);
                    l_ParentRelationship.m_Children.erase(l_RemoveIt, l_ParentRelationship.m_Children.end());
                }

                l_ChildRelationship.m_Parent = RelationshipComponent::GetInvalidEntity();
            }

            void DetachChild(Entity parent, Entity child)
            {
                if (!HasComponent<RelationshipComponent>(parent))
                {
                    return;
                }

                RelationshipComponent& l_ParentRelationship = GetComponent<RelationshipComponent>(parent);
                auto l_RemoveIt = std::remove(l_ParentRelationship.m_Children.begin(), l_ParentRelationship.m_Children.end(), child);
                if (l_RemoveIt != l_ParentRelationship.m_Children.end())
                {
                    l_ParentRelationship.m_Children.erase(l_RemoveIt, l_ParentRelationship.m_Children.end());
                }

                if (HasComponent<RelationshipComponent>(child))
                {
                    RelationshipComponent& l_ChildRelationship = GetComponent<RelationshipComponent>(child);
                    if (l_ChildRelationship.m_Parent == parent)
                    {
                        l_ChildRelationship.m_Parent = RelationshipComponent::GetInvalidEntity();
                    }
                }
            }

            void UpdateWorldTransforms()
            {
                const glm::mat4 l_Identity{ 1.0f };
                for (Entity it_Entity : m_ActiveEntities)
                {
                    if (!HasComponent<Transform>(it_Entity))
                    {
                        continue;
                    }

                    bool l_IsRoot = true;
                    if (HasComponent<RelationshipComponent>(it_Entity))
                    {
                        const RelationshipComponent& l_Relationship = GetComponent<RelationshipComponent>(it_Entity);
                        if (l_Relationship.m_Parent != RelationshipComponent::GetInvalidEntity() && HasComponent<RelationshipComponent>(l_Relationship.m_Parent))
                        {
                            l_IsRoot = false;
                        }
                    }

                    if (l_IsRoot)
                    {
                        UpdateWorldTransformRecursive(it_Entity, l_Identity);
                    }
                }
            }

            glm::mat4 GetWorldTransform(Entity entity)
            {
                if (!HasComponent<Transform>(entity))
                {
                    return glm::mat4{ 1.0f };
                }

                return GetComponent<Transform>(entity).m_WorldMatrix;
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
            RelationshipComponent& EnsureRelationshipComponent(Entity entity)
            {
                if (!HasComponent<RelationshipComponent>(entity))
                {
                    return AddComponent<RelationshipComponent>(entity);
                }

                return GetComponent<RelationshipComponent>(entity);
            }

            glm::mat4 ComposeLocalMatrix(const Transform& transform) const
            {
                glm::mat4 l_Matrix{ 1.0f };
                l_Matrix = glm::translate(l_Matrix, transform.Position);
                l_Matrix = glm::rotate(l_Matrix, glm::radians(transform.Rotation.x), glm::vec3{ 1.0f, 0.0f, 0.0f });
                l_Matrix = glm::rotate(l_Matrix, glm::radians(transform.Rotation.y), glm::vec3{ 0.0f, 1.0f, 0.0f });
                l_Matrix = glm::rotate(l_Matrix, glm::radians(transform.Rotation.z), glm::vec3{ 0.0f, 0.0f, 1.0f });
                l_Matrix = glm::scale(l_Matrix, transform.Scale);

                return l_Matrix;
            }

            void UpdateWorldTransformRecursive(Entity entity, const glm::mat4& parentWorld)
            {
                glm::mat4 l_CurrentWorld = parentWorld;
                if (HasComponent<Transform>(entity))
                {
                    Transform& l_Transform = GetComponent<Transform>(entity);
                    l_Transform.m_LocalMatrix = ComposeLocalMatrix(l_Transform);
                    l_Transform.m_WorldMatrix = parentWorld * l_Transform.m_LocalMatrix;
                    l_CurrentWorld = l_Transform.m_WorldMatrix;
                }

                if (!HasComponent<RelationshipComponent>(entity))
                {
                    return;
                }

                RelationshipComponent& l_Relationship = GetComponent<RelationshipComponent>(entity);
                for (Entity it_Child : l_Relationship.m_Children)
                {
                    UpdateWorldTransformRecursive(it_Child, l_CurrentWorld);
                }
            }

            std::unordered_map<std::type_index, std::unique_ptr<IComponentStorage>> m_Storages;
            Entity m_NextEntity = 0;

            // Tracks every live entity so debug UIs can iterate without poking into storage internals.
            std::vector<Entity> m_ActiveEntities;
        };
    }
}