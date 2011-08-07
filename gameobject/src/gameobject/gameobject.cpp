#include <new>
#include <algorithm>
#include <stdio.h>
#include <dlib/dstrings.h>
#include <dlib/log.h>
#include <dlib/hashtable.h>
#include <dlib/message.h>
#include <dlib/hash.h>
#include <dlib/array.h>
#include <dlib/index_pool.h>
#include <dlib/profile.h>
#include <dlib/math.h>
#include <dlib/mutex.h>
#include <ddf/ddf.h>
#include "gameobject.h"
#include "gameobject_script.h"
#include "gameobject_private.h"
#include "res_collection.h"
#include "res_prototype.h"
#include "res_script.h"

#include "../proto/gameobject_ddf.h"

namespace dmGameObject
{
    const uint32_t UNNAMED_IDENTIFIER = dmHashBuffer64("__unnamed__", strlen("__unnamed__"));
    const char* ID_SEPARATOR = "/";

    Register::Register()
    {
        m_ComponentTypeCount = 0;
        m_Mutex = dmMutex::New();
        m_CurrentIdentifierPath[0] = '\0';
        // If m_CurrentCollection != 0 => loading sub-collection
        m_CurrentCollection = 0;
        // Accumulated position for child collections
        m_AccumulatedTranslation = Vector3(0,0,0);
        m_AccumulatedRotation = Quat::identity();
    }

    Register::~Register()
    {
        dmMutex::Delete(m_Mutex);
    }

    ComponentType::ComponentType()
    {
        memset(this, 0, sizeof(*this));
    }

    void Initialize(dmScript::HContext context)
    {
        InitializeScript(context);
    }

    void Finalize()
    {
        FinalizeScript();
    }

    HRegister NewRegister()
    {
        return new Register();
    }

    void DeleteRegister(HRegister regist)
    {
        delete regist;
    }

    void ResourceReloadedCallback(void* user_data, dmResource::SResourceDescriptor* descriptor, const char* name);

    HCollection NewCollection(const char* name, dmResource::HFactory factory, HRegister regist, uint32_t max_instances)
    {
        if (max_instances > INVALID_INSTANCE_INDEX)
        {
            dmLogError("max_instances must be less or equal to %d", INVALID_INSTANCE_INDEX);
            return 0;
        }
        Collection* collection = new Collection(factory, regist, max_instances);

        for (uint32_t i = 0; i < regist->m_ComponentTypeCount; ++i)
        {
            if (regist->m_ComponentTypes[i].m_NewWorldFunction)
            {
                ComponentNewWorldParams params;
                params.m_Context = regist->m_ComponentTypes[i].m_Context;
                params.m_World = &collection->m_ComponentWorlds[i];
                regist->m_ComponentTypes[i].m_NewWorldFunction(params);
            }
        }

        collection->m_NameHash = dmHashString64(name);

        dmMutex::Lock(regist->m_Mutex);
        if (regist->m_Collections.Full())
        {
            regist->m_Collections.OffsetCapacity(4);
        }
        regist->m_Collections.Push(collection);
        dmMutex::Unlock(regist->m_Mutex);

        dmResource::RegisterResourceReloadedCallback(factory, ResourceReloadedCallback, collection);

        dmMessage::Result result = dmMessage::NewSocket(name, &collection->m_Socket);
        if (result != dmMessage::RESULT_OK)
        {
            if (result == dmMessage::RESULT_SOCKET_EXISTS)
            {
                dmLogError("The collection '%s' could not be created since there is already a socket with the same name.", name);
            }
            else if (result == dmMessage::RESULT_INVALID_SOCKET_NAME)
            {
                dmLogError("The collection '%s' could not be created since the name is invalid for sockets.", name);
            }
            DeleteCollection(collection);
            return 0;
        }

        return collection;
    }

    void DoDeleteAll(HCollection collection);

    void DeleteCollection(HCollection collection)
    {
        HRegister regist = collection->m_Register;
        DoDeleteAll(collection);
        for (uint32_t i = 0; i < regist->m_ComponentTypeCount; ++i)
        {
            ComponentDeleteWorldParams params;
            params.m_Context = regist->m_ComponentTypes[i].m_Context;
            params.m_World = collection->m_ComponentWorlds[i];
            if (regist->m_ComponentTypes[i].m_DeleteWorldFunction)
                regist->m_ComponentTypes[i].m_DeleteWorldFunction(params);
        }

        dmMutex::Lock(regist->m_Mutex);
        bool found = false;
        for (uint32_t i = 0; i < regist->m_Collections.Size(); ++i)
        {
            if (regist->m_Collections[i] == collection)
            {
                regist->m_Collections.EraseSwap(i);
                found = true;
                break;
            }
        }
        assert(found);
        dmMutex::Unlock(regist->m_Mutex);

        dmResource::UnregisterResourceReloadedCallback(collection->m_Factory, ResourceReloadedCallback, collection);

        if (collection->m_Socket)
        {
            dmMessage::Consume(collection->m_Socket);
            dmMessage::DeleteSocket(collection->m_Socket);
        }

        delete collection;
    }

    void* GetWorld(HCollection collection, uint32_t component_index)
    {
        if (component_index < MAX_COMPONENT_TYPES)
        {
            return collection->m_ComponentWorlds[component_index];
        }
        else
        {
            return 0x0;
        }
    }

    ComponentType* FindComponentType(Register* regist, uint32_t resource_type, uint32_t* index)
    {
        for (uint32_t i = 0; i < regist->m_ComponentTypeCount; ++i)
        {
            ComponentType* ct = &regist->m_ComponentTypes[i];
            if (ct->m_ResourceType == resource_type)
            {
                if (index != 0x0)
                    *index = i;
                return ct;
            }
        }
        return 0;
    }

    struct ComponentTypeSortPred
    {
        HRegister m_Register;
        ComponentTypeSortPred(HRegister regist) : m_Register(regist) {}

        bool operator ()(const uint16_t& a, const uint16_t& b) const
        {
            return m_Register->m_ComponentTypes[a].m_UpdateOrderPrio < m_Register->m_ComponentTypes[b].m_UpdateOrderPrio;
        }
    };

    Result RegisterComponentType(HRegister regist, const ComponentType& type)
    {
        if (regist->m_ComponentTypeCount == MAX_COMPONENT_TYPES)
            return RESULT_OUT_OF_RESOURCES;

        if (FindComponentType(regist, type.m_ResourceType, 0x0) != 0)
            return RESULT_ALREADY_REGISTERED;

        regist->m_ComponentTypes[regist->m_ComponentTypeCount] = type;
        regist->m_ComponentTypesOrder[regist->m_ComponentTypeCount] = regist->m_ComponentTypeCount;
        regist->m_ComponentTypeCount++;
        return RESULT_OK;
    }

    Result SetUpdateOrderPrio(HRegister regist, uint32_t resource_type, uint16_t prio)
    {
        bool found = false;
        for (uint32_t i = 0; i < regist->m_ComponentTypeCount; ++i)
        {
            if (regist->m_ComponentTypes[i].m_ResourceType == resource_type)
            {
                regist->m_ComponentTypes[i].m_UpdateOrderPrio = prio;
                found = true;
                break;
            }
        }
        if (!found)
        {
            return RESULT_RESOURCE_TYPE_NOT_FOUND;
        }

        std::sort(regist->m_ComponentTypesOrder, regist->m_ComponentTypesOrder + regist->m_ComponentTypeCount, ComponentTypeSortPred(regist));

        return RESULT_OK;
    }

    dmResource::FactoryResult RegisterResourceTypes(dmResource::HFactory factory, HRegister regist)
    {
        dmResource::FactoryResult ret = dmResource::FACTORY_RESULT_OK;
        ret = dmResource::RegisterType(factory, "goc", (void*)regist, &ResPrototypeCreate, &ResPrototypeDestroy, 0);
        if (ret != dmResource::FACTORY_RESULT_OK)
            return ret;

        ret = dmResource::RegisterType(factory, "scriptc", 0, &ResScriptCreate, &ResScriptDestroy, &ResScriptRecreate);
        if (ret != dmResource::FACTORY_RESULT_OK)
            return ret;

        ret = dmResource::RegisterType(factory, "collectionc", regist, &ResCollectionCreate, &ResCollectionDestroy, 0);
        if (ret != dmResource::FACTORY_RESULT_OK)
            return ret;

        return ret;
    }

    static void EraseSwapLevelIndex(HCollection collection, HInstance instance)
    {
        /*
         * Remove instance from m_LevelIndices using an erase-swap operation
         */

        assert(collection->m_LevelInstanceCount[instance->m_Depth] > 0);
        assert(instance->m_LevelIndex < collection->m_LevelInstanceCount[instance->m_Depth]);

        uint32_t abs_level_index = instance->m_Depth * collection->m_MaxInstances + instance->m_LevelIndex;
        uint32_t abs_level_index_last = instance->m_Depth * collection->m_MaxInstances + (collection->m_LevelInstanceCount[instance->m_Depth]-1);

        assert(collection->m_LevelIndices[abs_level_index] != INVALID_INSTANCE_INDEX);

        uint32_t swap_in_index = collection->m_LevelIndices[abs_level_index_last];
        HInstance swap_in_instance = collection->m_Instances[swap_in_index];
        assert(swap_in_instance->m_Index == swap_in_index);
        // Remove index in m_LevelIndices using an "erase-swap operation"
        collection->m_LevelIndices[abs_level_index] = collection->m_LevelIndices[abs_level_index_last];
        collection->m_LevelIndices[abs_level_index_last] = INVALID_INSTANCE_INDEX;
        collection->m_LevelInstanceCount[instance->m_Depth]--;
        swap_in_instance->m_LevelIndex = abs_level_index - instance->m_Depth * collection->m_MaxInstances;
    }

    static void InsertInstanceInLevelIndex(HCollection collection, HInstance instance)
    {
        /*
         * Insert instance in m_LevelIndices at level set in instance->m_Depth
         */

        instance->m_LevelIndex = collection->m_LevelInstanceCount[instance->m_Depth];
        assert(instance->m_LevelIndex < collection->m_MaxInstances);

        assert(collection->m_LevelInstanceCount[instance->m_Depth] < collection->m_MaxInstances);
        uint32_t abs_level_index = instance->m_Depth * collection->m_MaxInstances + collection->m_LevelInstanceCount[instance->m_Depth];
        assert(collection->m_LevelIndices[abs_level_index] == INVALID_INSTANCE_INDEX);
        collection->m_LevelIndices[abs_level_index] = instance->m_Index;

        collection->m_LevelInstanceCount[instance->m_Depth]++;
    }

    HInstance New(HCollection collection, const char* prototype_name)
    {
        assert(collection->m_InUpdate == 0 && "Creating new instances during Update(.) is not permitted");
        Prototype* proto;
        dmResource::HFactory factory = collection->m_Factory;
        dmResource::FactoryResult error = dmResource::Get(factory, prototype_name, (void**)&proto);
        if (error != dmResource::FACTORY_RESULT_OK)
        {
            return 0;
        }

        if (collection->m_InstanceIndices.Remaining() == 0)
        {
            dmLogError("Unable to create instance. Out of resources");
            return 0;
        }

        // Count number of component userdata fields required
        uint32_t component_instance_userdata_count = 0;
        for (uint32_t i = 0; i < proto->m_Components.Size(); ++i)
        {
            Prototype::Component* component = &proto->m_Components[i];
            ComponentType* component_type = component->m_Type;
            if (!component_type)
            {
                dmLogError("Internal error. Component type #%d for '%s' not found.", i, prototype_name);
                assert(false);
            }
            if (component_type->m_InstanceHasUserData)
                component_instance_userdata_count++;
        }

        uint32_t component_userdata_size = sizeof(((Instance*)0)->m_ComponentInstanceUserData[0]);
        // NOTE: Allocate actual Instance with *all* component instance user-data accounted
        void* instance_memory = ::operator new (sizeof(Instance) + component_instance_userdata_count * component_userdata_size);
        Instance* instance = new(instance_memory) Instance(proto);
        instance->m_ComponentInstanceUserDataCount = component_instance_userdata_count;
        instance->m_Collection = collection;
        uint16_t instance_index = collection->m_InstanceIndices.Pop();
        instance->m_Index = instance_index;
        assert(collection->m_Instances[instance_index] == 0);
        collection->m_Instances[instance_index] = instance;

        uint32_t components_created = 0;
        uint32_t next_component_instance_data = 0;
        bool ok = true;
        for (uint32_t i = 0; i < proto->m_Components.Size(); ++i)
        {
            Prototype::Component* component = &proto->m_Components[i];
            ComponentType* component_type = component->m_Type;
            assert(component_type);

            uintptr_t* component_instance_data = 0;
            if (component_type->m_InstanceHasUserData)
            {
                component_instance_data = &instance->m_ComponentInstanceUserData[next_component_instance_data++];
                *component_instance_data = 0;
            }
            assert(next_component_instance_data <= instance->m_ComponentInstanceUserDataCount);

            ComponentCreateParams params;
            params.m_Collection = collection;
            params.m_Instance = instance;
            params.m_ComponentIndex = (uint8_t)i;
            params.m_Resource = component->m_Resource;
            params.m_World = collection->m_ComponentWorlds[component->m_TypeIndex];
            params.m_Context = component_type->m_Context;
            params.m_UserData = component_instance_data;
            CreateResult create_result =  component_type->m_CreateFunction(params);
            if (create_result == CREATE_RESULT_OK)
            {
                collection->m_ComponentInstanceCount[component->m_TypeIndex]++;
                components_created++;
            }
            else
            {
                ok = false;
                break;
            }
        }

        if (!ok)
        {
            uint32_t next_component_instance_data = 0;
            for (uint32_t i = 0; i < components_created; ++i)
            {
                Prototype::Component* component = &proto->m_Components[i];
                ComponentType* component_type = component->m_Type;
                assert(component_type);
                uintptr_t* component_instance_data = 0;
                if (component_type->m_InstanceHasUserData)
                {
                    component_instance_data = &instance->m_ComponentInstanceUserData[next_component_instance_data++];
                }
                assert(next_component_instance_data <= instance->m_ComponentInstanceUserDataCount);

                collection->m_ComponentInstanceCount[component->m_TypeIndex]--;
                ComponentDestroyParams params;
                params.m_Collection = collection;
                params.m_Instance = instance;
                params.m_World = collection->m_ComponentWorlds[component->m_TypeIndex];
                params.m_Context = component_type->m_Context;
                params.m_UserData = component_instance_data;
                component_type->m_DestroyFunction(params);
            }

            // We can not call Delete here. Delete call DestroyFunction for every component
            dmResource::Release(factory, instance->m_Prototype);
            operator delete (instance_memory);
            collection->m_Instances[instance_index] = 0x0;
            collection->m_InstanceIndices.Push(instance_index);
            return 0;
        }

        InsertInstanceInLevelIndex(collection, instance);

        return instance;
    }

    bool Init(HCollection collection, HInstance instance);

    void Spawn(HCollection collection, const char* prototype_name, const char* id, const Point3& position, const Quat& rotation)
    {
        if (collection->m_InUpdate)
        {
            dmLogError("Spawning during update is not allowed, %s was never spawned.", prototype_name);
            return;
        }
        HInstance instance = New(collection, prototype_name);
        if (instance != 0)
        {
            SetPosition(instance, position);
            SetRotation(instance, rotation);
            Result result = SetIdentifier(collection, instance, id);
            if (result == RESULT_IDENTIFIER_IN_USE)
            {
                dmLogError("The identifier '%s' is already in use.", id);
                Delete(collection, instance);
                instance = 0;
            }
            else
            {
                Init(collection, instance);
            }
        }
        if (instance == 0)
        {
            dmLogError("Could not spawn an instance of prototype %s.", prototype_name);
        }
    }

    static void Unlink(Collection* collection, Instance* instance)
    {
        // Unlink "me" from parent
        if (instance->m_Parent != INVALID_INSTANCE_INDEX)
        {
            assert(instance->m_Depth > 0);
            Instance* parent = collection->m_Instances[instance->m_Parent];
            uint32_t index = parent->m_FirstChildIndex;
            Instance* prev_child = 0;
            while (index != INVALID_INSTANCE_INDEX)
            {
                Instance* child = collection->m_Instances[index];
                if (child == instance)
                {
                    if (prev_child)
                        prev_child->m_SiblingIndex = child->m_SiblingIndex;
                    else
                        parent->m_FirstChildIndex = child->m_SiblingIndex;

                    break;
                }

                prev_child = child;
                index = collection->m_Instances[index]->m_SiblingIndex;
            }
        }
    }

    static void MoveUp(Collection* collection, Instance* instance)
    {
        /*
         * Move instance up in hierarchy
         */

        assert(instance->m_Depth > 0);
        EraseSwapLevelIndex(collection, instance);
        instance->m_Depth--;
        InsertInstanceInLevelIndex(collection, instance);
    }

    static void MoveAllUp(Collection* collection, Instance* instance)
    {
        /*
         * Move all children up in hierarchy
         */

        uint32_t index = instance->m_FirstChildIndex;
        while (index != INVALID_INSTANCE_INDEX)
        {
            Instance* child = collection->m_Instances[index];
            // NOTE: This assertion is only valid if we processes the tree depth first
            // The order of MoveAllUp and MoveUp below is imperative
            // NOTE: This assert is not possible when moving more than a single step. TODO: ?
            //assert(child->m_Depth == instance->m_Depth + 1);
            MoveAllUp(collection, child);
            MoveUp(collection, child);
            index = collection->m_Instances[index]->m_SiblingIndex;
        }
    }

    static void MoveDown(Collection* collection, Instance* instance)
    {
        /*
         * Move instance down in hierarchy
         */

        assert(instance->m_Depth < MAX_HIERARCHICAL_DEPTH - 1);
        EraseSwapLevelIndex(collection, instance);
        instance->m_Depth++;
        InsertInstanceInLevelIndex(collection, instance);
    }

    static void MoveAllDown(Collection* collection, Instance* instance)
    {
        /*
         * Move all children down in hierarchy
         */

        uint32_t index = instance->m_FirstChildIndex;
        while (index != INVALID_INSTANCE_INDEX)
        {
            Instance* child = collection->m_Instances[index];
            // NOTE: This assertion is only valid if we processes the tree depth first
            // The order of MoveAllUp and MoveUp below is imperative
            // NOTE: This assert is not possible when moving more than a single step. TODO: ?
            //assert(child->m_Depth == instance->m_Depth + 1);
            MoveAllDown(collection, child);
            MoveDown(collection, child);
            index = collection->m_Instances[index]->m_SiblingIndex;
        }
    }

    void UpdateTransforms(HCollection collection);

    bool Init(HCollection collection, HInstance instance)
    {
        if (instance)
        {
            if (instance->m_Initialized)
            {
                dmLogWarning("%s", "Instance is initialized twice, this may lead to undefined behaviour.");
            }
            else
            {
                instance->m_Initialized = 1;
            }

            assert(collection->m_Instances[instance->m_Index] == instance);

            // Update world transforms since some components might need them in their init-callback
            Transform* trans = &collection->m_WorldTransforms[instance->m_Index];
            if (instance->m_Parent == INVALID_INSTANCE_INDEX)
            {
                trans->m_Translation = instance->m_Position;
                trans->m_Rotation = instance->m_Rotation;
            }
            else
            {
                const Transform* parent_trans = &collection->m_WorldTransforms[instance->m_Parent];
                trans->m_Rotation = parent_trans->m_Rotation * instance->m_Rotation;
                trans->m_Translation = rotate(parent_trans->m_Rotation, Vector3(instance->m_Position))
                                      + parent_trans->m_Translation;
            }

            uint32_t next_component_instance_data = 0;
            Prototype* prototype = instance->m_Prototype;
            for (uint32_t i = 0; i < prototype->m_Components.Size(); ++i)
            {
                Prototype::Component* component = &prototype->m_Components[i];
                ComponentType* component_type = component->m_Type;

                uintptr_t* component_instance_data = 0;
                if (component_type->m_InstanceHasUserData)
                {
                    component_instance_data = &instance->m_ComponentInstanceUserData[next_component_instance_data++];
                }
                assert(next_component_instance_data <= instance->m_ComponentInstanceUserDataCount);

                if (component_type->m_InitFunction)
                {
                    ComponentInitParams params;
                    params.m_Collection = collection;
                    params.m_Instance = instance;
                    params.m_World = collection->m_ComponentWorlds[component->m_TypeIndex];
                    params.m_Context = component_type->m_Context;
                    params.m_UserData = component_instance_data;
                    CreateResult result = component_type->m_InitFunction(params);
                    if (result != CREATE_RESULT_OK)
                    {
                        return false;
                    }
                }
            }
        }

        return true;
    }

    bool Init(HCollection collection)
    {
        DM_PROFILE(GameObject, "Init");

        assert(collection->m_InUpdate == 0 && "Initializing instances during Update(.) is not permitted");

        // Update trasform cache
        UpdateTransforms(collection);

        bool result = true;
        // Update scripts
        uint32_t n_objects = collection->m_Instances.Size();
        for (uint32_t i = 0; i < n_objects; ++i)
        {
            Instance* instance = collection->m_Instances[i];
            if ( ! Init(collection, instance) )
            {
                result = false;
            }
        }

        return result;
    }

    bool Final(HCollection collection, HInstance instance)
    {
        if (instance)
        {
            if (instance->m_Initialized)
                instance->m_Initialized = 0;
            else
                dmLogWarning("%s", "Instance is finalized without being initialized, this may lead to undefined behaviour.");

            assert(collection->m_Instances[instance->m_Index] == instance);

            uint32_t next_component_instance_data = 0;
            Prototype* prototype = instance->m_Prototype;
            for (uint32_t i = 0; i < prototype->m_Components.Size(); ++i)
            {
                Prototype::Component* component = &prototype->m_Components[i];
                ComponentType* component_type = component->m_Type;
                assert(component_type);

                uintptr_t* component_instance_data = 0;
                if (component_type->m_InstanceHasUserData)
                {
                    component_instance_data = &instance->m_ComponentInstanceUserData[next_component_instance_data++];
                }
                assert(next_component_instance_data <= instance->m_ComponentInstanceUserDataCount);

                if (component_type->m_FinalFunction)
                {
                    ComponentFinalParams params;
                    params.m_Collection = collection;
                    params.m_Instance = instance;
                    params.m_World = collection->m_ComponentWorlds[component->m_TypeIndex];
                    params.m_Context = component_type->m_Context;
                    params.m_UserData = component_instance_data;
                    CreateResult result = component_type->m_FinalFunction(params);
                    if (result != CREATE_RESULT_OK)
                    {
                        return false;
                    }
                }
            }
        }

        return true;
    }

    bool Final(HCollection collection)
    {
        DM_PROFILE(GameObject, "Final");

        assert(collection->m_InUpdate == 0 && "Finalizing instances during Update(.) is not permitted");

        bool result = true;
        uint32_t n_objects = collection->m_Instances.Size();
        for (uint32_t i = 0; i < n_objects; ++i)
        {
            Instance* instance = collection->m_Instances[i];
            if ( ! Final(collection, instance) )
            {
                result = false;
            }
        }

        return result;
    }

    void Delete(HCollection collection, HInstance instance)
    {
        assert(collection->m_Instances[instance->m_Index] == instance);
        assert(instance->m_Collection == collection);

        // NOTE: Do not add for delete twice.
        if (instance->m_ToBeDeleted)
            return;

        instance->m_ToBeDeleted = 1;
        collection->m_InstancesToDelete.Push(instance->m_Index);
    }

    void DoDelete(HCollection collection, HInstance instance)
    {
        dmResource::HFactory factory = collection->m_Factory;
        uint32_t next_component_instance_data = 0;
        Prototype* prototype = instance->m_Prototype;
        for (uint32_t i = 0; i < prototype->m_Components.Size(); ++i)
        {
            Prototype::Component* component = &prototype->m_Components[i];
            ComponentType* component_type = component->m_Type;

            uintptr_t* component_instance_data = 0;
            if (component_type->m_InstanceHasUserData)
            {
                component_instance_data = &instance->m_ComponentInstanceUserData[next_component_instance_data++];
            }
            assert(next_component_instance_data <= instance->m_ComponentInstanceUserDataCount);

            collection->m_ComponentInstanceCount[component->m_TypeIndex]--;
            ComponentDestroyParams params;
            params.m_Collection = collection;
            params.m_Instance = instance;
            params.m_World = collection->m_ComponentWorlds[component->m_TypeIndex];
            params.m_Context = component_type->m_Context;
            params.m_UserData = component_instance_data;
            component_type->m_DestroyFunction(params);
        }

        if (instance->m_Identifier != UNNAMED_IDENTIFIER)
            collection->m_IDToInstance.Erase(instance->m_Identifier);

        assert(collection->m_LevelInstanceCount[instance->m_Depth] > 0);
        assert(instance->m_LevelIndex < collection->m_LevelInstanceCount[instance->m_Depth]);

        // Reparent child nodes
        uint32_t index = instance->m_FirstChildIndex;
        while (index != INVALID_INSTANCE_INDEX)
        {
            Instance* child = collection->m_Instances[index];
            assert(child->m_Parent == instance->m_Index);
            child->m_Parent = instance->m_Parent;
            index = collection->m_Instances[index]->m_SiblingIndex;
        }

        // Add child nodes to parent
        if (instance->m_Parent != INVALID_INSTANCE_INDEX)
        {
            Instance* parent = collection->m_Instances[instance->m_Parent];
            uint32_t index = parent->m_FirstChildIndex;
            Instance* child = 0;
            while (index != INVALID_INSTANCE_INDEX)
            {
                child = collection->m_Instances[index];
                index = collection->m_Instances[index]->m_SiblingIndex;
            }

            // Child is last child if present
            if (child)
            {
                assert(child->m_SiblingIndex == INVALID_INSTANCE_INDEX);
                child->m_SiblingIndex = instance->m_FirstChildIndex;
            }
            else
            {
                assert(parent->m_FirstChildIndex == INVALID_INSTANCE_INDEX);
                parent->m_FirstChildIndex = instance->m_FirstChildIndex;
            }
        }

        // Unlink "me" from parent
        Unlink(collection, instance);
        EraseSwapLevelIndex(collection, instance);
        MoveAllUp(collection, instance);

        dmResource::Release(factory, instance->m_Prototype);
        collection->m_InstanceIndices.Push(instance->m_Index);
        collection->m_Instances[instance->m_Index] = 0;

        // Erase from input stack
        bool found_instance = false;
        for (uint32_t i = 0; i < collection->m_InputFocusStack.Size(); ++i)
        {
            if (collection->m_InputFocusStack[i] == instance)
            {
                found_instance = true;
            }
            if (found_instance)
            {
                if (i < collection->m_InputFocusStack.Size() - 1)
                {
                    collection->m_InputFocusStack[i] = collection->m_InputFocusStack[i+1];
                }
            }
        }
        if (found_instance)
        {
            collection->m_InputFocusStack.Pop();
        }

        instance->~Instance();
        void* instance_memory = (void*) instance;

        // This is required for failing test
        // TODO: #ifdef on something...?
        // Clear all memory excluding ComponentInstanceUserData
        memset(instance_memory, 0xcc, sizeof(Instance));
        operator delete (instance_memory);
    }

    void DeleteAll(HCollection collection)
    {
        for (uint32_t i = 0; i < collection->m_Instances.Size(); ++i)
        {
            Instance* instance = collection->m_Instances[i];
            if (instance)
            {
                Delete(collection, instance);
            }
        }
    }

    void DoDeleteAll(HCollection collection)
    {
        // This will perform tons of unnecessary work to resolve and reorder
        // the hierarchies and other things but will serve as a nice test case
        for (uint32_t i = 0; i < collection->m_Instances.Size(); ++i)
        {
            Instance* instance = collection->m_Instances[i];
            if (instance)
            {
                DoDelete(collection, instance);
            }
        }
    }

    Result SetIdentifier(HCollection collection, HInstance instance, const char* identifier)
    {
        dmhash_t id = dmHashBuffer64(identifier, strlen(identifier));
        if (collection->m_IDToInstance.Get(id))
            return RESULT_IDENTIFIER_IN_USE;

        if (instance->m_Identifier != UNNAMED_IDENTIFIER)
            return RESULT_IDENTIFIER_ALREADY_SET;

        instance->m_Identifier = id;
        collection->m_IDToInstance.Put(id, instance);

        return RESULT_OK;
    }

    dmhash_t GetIdentifier(HInstance instance)
    {
        return instance->m_Identifier;
    }

    dmhash_t GetAbsoluteIdentifier(HInstance instance, const char* id, uint32_t id_size)
    {
        // check for global id (/foo/bar)
        if (*id == *ID_SEPARATOR)
        {
            return dmHashBuffer64(id, id_size);
        }
        else
        {
            // Make a copy of the state.
            HashState64 tmp_state = instance->m_CollectionPathHashState;
            dmHashUpdateBuffer64(&tmp_state, id, id_size);
            return dmHashFinal64(&tmp_state);
        }
    }

    HInstance GetInstanceFromIdentifier(HCollection collection, dmhash_t identifier)
    {
        Instance** instance = collection->m_IDToInstance.Get(identifier);
        if (instance)
            return *instance;
        else
            return 0;
    }

    Result GetComponentIndex(HInstance instance, dmhash_t component_id, uint8_t* component_index)
    {
        assert(instance != 0x0);
        for (uint32_t i = 0; i < instance->m_Prototype->m_Components.Size(); ++i)
        {
            Prototype::Component* component = &instance->m_Prototype->m_Components[i];
            if (component->m_Id == component_id)
            {
                *component_index = (uint8_t)i & 0xff;
                return RESULT_OK;
            }
        }
        return RESULT_COMPONENT_NOT_FOUND;
    }

    Result GetComponentId(HInstance instance, uint8_t component_index, dmhash_t* component_id)
    {
        assert(instance != 0x0);
        if (component_index < instance->m_Prototype->m_Components.Size())
        {
            *component_id = instance->m_Prototype->m_Components[component_index].m_Id;
            return RESULT_OK;
        }
        return RESULT_COMPONENT_NOT_FOUND;
    }

    struct DispatchMessagesContext
    {
        HCollection m_Collection;
        bool m_Success;
    };

    void DispatchMessagesFunction(dmMessage::Message *message, void* user_ptr)
    {
        DispatchMessagesContext* context = (DispatchMessagesContext*) user_ptr;

        Instance* instance = 0x0;
        if (message->m_UserData != 0)
        {
            Instance* user_data_instance = (Instance*)message->m_UserData;
            if (message->m_Receiver.m_Path == user_data_instance->m_Identifier)
            {
                instance = user_data_instance;
            }
        }
        if (instance == 0x0)
        {
            instance = GetInstanceFromIdentifier(context->m_Collection, message->m_Receiver.m_Path);
        }
        if (instance == 0x0)
        {
            dmLogError("Instance '%s' could not be found when dispatching message '%s'.",
                        (const char*) dmHashReverse64(message->m_Receiver.m_Path, 0),
                        (const char*) dmHashReverse64(message->m_Id, 0));

            context->m_Success = false;
            return;
        }
        if (message->m_Descriptor != 0)
        {
            dmDDF::Descriptor* descriptor = (dmDDF::Descriptor*)message->m_Descriptor;
            if (descriptor == dmGameObjectDDF::AcquireInputFocus::m_DDFDescriptor)
            {
                dmGameObject::AcquireInputFocus(context->m_Collection, instance);
                return;
            }
            else if (descriptor == dmGameObjectDDF::ReleaseInputFocus::m_DDFDescriptor)
            {
                dmGameObject::ReleaseInputFocus(context->m_Collection, instance);
                return;
            }
            else if (descriptor == dmGameObjectDDF::RequestTransform::m_DDFDescriptor)
            {
                dmGameObjectDDF::TransformResponse response;
                response.m_Position = dmGameObject::GetPosition(instance);
                response.m_Rotation = dmGameObject::GetRotation(instance);
                response.m_WorldPosition = dmGameObject::GetWorldPosition(instance);
                response.m_WorldRotation = dmGameObject::GetWorldRotation(instance);
                dmhash_t message_id = dmHashString64(dmGameObjectDDF::TransformResponse::m_DDFDescriptor->m_Name);
                uintptr_t gotr_descriptor = (uintptr_t)dmGameObjectDDF::TransformResponse::m_DDFDescriptor;
                uint32_t data_size = sizeof(dmGameObjectDDF::TransformResponse);
                if (dmMessage::IsSocketValid(message->m_Sender.m_Socket))
                {
                    dmMessage::Result message_result = dmMessage::Post(&message->m_Receiver, &message->m_Sender, message_id, message->m_UserData, gotr_descriptor, &response, data_size);
                    if (message_result != dmMessage::RESULT_OK)
                    {
                        dmLogError("Could not send message '%s' to sender: %d.", dmGameObjectDDF::TransformResponse::m_DDFDescriptor->m_Name, message_result);
                    }
                }
                return;
            }
            else if (descriptor == dmGameObjectDDF::SetParent::m_DDFDescriptor)
            {
                dmGameObjectDDF::SetParent* sp = (dmGameObjectDDF::SetParent*)message->m_Data;
                dmGameObject::HInstance parent = 0;
                if (sp->m_ParentId != 0)
                {
                    parent = dmGameObject::GetInstanceFromIdentifier(context->m_Collection, sp->m_ParentId);
                    if (parent == 0)
                        dmLogWarning("Could not find parent instance with id '%s'.", (const char*) dmHashReverse64(sp->m_ParentId, 0));

                }
                Point3 parent_wp(0.0f, 0.0f, 0.0f);
                Quat parent_wr(0.0f, 0.0f, 0.0f, 1.0f);
                if (parent)
                {
                    parent_wp = GetWorldPosition(parent);
                    parent_wr = GetWorldRotation(parent);
                }
                if (sp->m_KeepWorldTransform == 0)
                {
                    Transform& world = context->m_Collection->m_WorldTransforms[instance->m_Index];
                    world.m_Rotation = parent_wr * GetRotation(instance);
                    world.m_Translation = rotate(parent_wr, Vector3(GetPosition(instance))) + parent_wp;
                }
                else
                {
                    Quat conj_parent_wr = conj(parent_wr);
                    dmGameObject::SetPosition(instance, Point3(rotate(conj_parent_wr, GetWorldPosition(instance) - parent_wp)));
                    dmGameObject::SetRotation(instance, conj_parent_wr * GetWorldRotation(instance));
                }
                dmGameObject::Result result = dmGameObject::SetParent(instance, parent);

                if (result != dmGameObject::RESULT_OK)
                    dmLogWarning("Error when setting parent of '%s' to '%s', error: %i.",
                                 (const char*) dmHashReverse64(instance->m_Identifier, 0),
                                 (const char*) dmHashReverse64(sp->m_ParentId, 0),
                                 result);
                return;
            }
        }
        Prototype* prototype = instance->m_Prototype;

        if (message->m_Receiver.m_Fragment != 0)
        {
            uint8_t component_index;
            Result result = GetComponentIndex(instance, message->m_Receiver.m_Fragment, &component_index);
            if (result != RESULT_OK)
            {
                dmLogError("Component '%s#%s' could not be found when dispatching message '%s'",
                            (const char*) dmHashReverse64(message->m_Receiver.m_Path, 0),
                            (const char*) dmHashReverse64(message->m_Receiver.m_Fragment, 0),
                            (const char*) dmHashReverse64(message->m_Id, 0));
                context->m_Success = false;
                return;
            }
            Prototype::Component* component = &prototype->m_Components[component_index];
            ComponentType* component_type = component->m_Type;
            assert(component_type);
            uint32_t component_type_index = component->m_TypeIndex;

            if (component_type->m_OnMessageFunction)
            {
                // TODO: Not optimal way to find index of component instance data
                uint32_t next_component_instance_data = 0;
                for (uint32_t i = 0; i < component_index; ++i)
                {
                    ComponentType* ct = prototype->m_Components[i].m_Type;
                    assert(component_type);
                    if (ct->m_InstanceHasUserData)
                    {
                        next_component_instance_data++;
                    }
                }

                uintptr_t* component_instance_data = 0;
                if (component_type->m_InstanceHasUserData)
                {
                    component_instance_data = &instance->m_ComponentInstanceUserData[next_component_instance_data];
                }
                {
                    DM_PROFILE(GameObject, "OnMessageFunction");
                    ComponentOnMessageParams params;
                    params.m_Instance = instance;
                    params.m_World = context->m_Collection->m_ComponentWorlds[component_type_index];
                    params.m_Context = component_type->m_Context;
                    params.m_UserData = component_instance_data;
                    params.m_Message = message;
                    UpdateResult res = component_type->m_OnMessageFunction(params);
                    if (res != UPDATE_RESULT_OK)
                        context->m_Success = false;
                }
            }
            else
            {
                // TODO User-friendly error message here...
                dmLogWarning("Component type is missing OnMessage function");
            }
        }
        else // broadcast
        {
            uint32_t next_component_instance_data = 0;
            for (uint32_t i = 0; i < prototype->m_Components.Size(); ++i)
            {
                Prototype::Component* component = &prototype->m_Components[i];
                ComponentType* component_type = component->m_Type;
                assert(component_type);
                uint32_t component_type_index = component->m_TypeIndex;

                if (component_type->m_OnMessageFunction)
                {
                    uintptr_t* component_instance_data = 0;
                    if (component_type->m_InstanceHasUserData)
                    {
                        component_instance_data = &instance->m_ComponentInstanceUserData[next_component_instance_data++];
                    }
                    {
                        DM_PROFILE(GameObject, "OnMessageFunction");
                        ComponentOnMessageParams params;
                        params.m_Instance = instance;
                        params.m_World = context->m_Collection->m_ComponentWorlds[component_type_index];
                        params.m_Context = component_type->m_Context;
                        params.m_UserData = component_instance_data;
                        params.m_Message = message;
                        UpdateResult res = component_type->m_OnMessageFunction(params);
                        if (res != UPDATE_RESULT_OK)
                            context->m_Success = false;
                    }
                }
                else
                {
                    if (component_type->m_InstanceHasUserData)
                    {
                        ++next_component_instance_data;
                    }
                }
            }
        }
    }

    bool DispatchMessages(HCollection collection)
    {
        DM_PROFILE(GameObject, "DispatchMessages");

        DispatchMessagesContext ctx;
        ctx.m_Collection = collection;
        ctx.m_Success = true;
        (void) dmMessage::Dispatch(collection->m_Socket, &DispatchMessagesFunction, (void*) &ctx);

        return ctx.m_Success;
    }

    void UpdateTransforms(HCollection collection)
    {
        DM_PROFILE(GameObject, "UpdateTransforms");

        // Calculate world transforms
        // First root-level instances
        for (uint32_t i = 0; i < collection->m_LevelInstanceCount[0]; ++i)
        {
            uint16_t index = collection->m_LevelIndices[i];
            Transform* trans = &collection->m_WorldTransforms[index];
            Instance* instance = collection->m_Instances[index];
            uint16_t parent_index = instance->m_Parent;
            assert(parent_index == INVALID_INSTANCE_INDEX);
            trans->m_Translation = instance->m_Position;
            trans->m_Rotation = instance->m_Rotation;
        }

        // World-transform for levels 1..MAX_HIERARCHICAL_DEPTH-1
        for (uint32_t level = 1; level < MAX_HIERARCHICAL_DEPTH; ++level)
        {
            uint32_t max_instance = collection->m_Instances.Size();
            for (uint32_t i = 0; i < collection->m_LevelInstanceCount[level]; ++i)
            {
                uint16_t index = collection->m_LevelIndices[level * max_instance + i];
                Instance* instance = collection->m_Instances[index];
                Transform* trans = &collection->m_WorldTransforms[index];

                uint16_t parent_index = instance->m_Parent;
                assert(parent_index != INVALID_INSTANCE_INDEX);

                Transform* parent_trans = &collection->m_WorldTransforms[parent_index];

                /*
                 * Quaternion + Translation transform:
                 *
                 *   x' = q * x * q^-1 + t
                 *
                 *
                 * The compound transform:
                 * The first transform is given by:
                 *
                 *    x' = q1 * x * q1^-1 + t1
                 *
                 * apply the second transform
                 *
                 *   x'' = q2 ( q1 * x * q1^-1 + t1 ) q2^-1 + t2
                 *   x'' = q2 * q1 * x * q1^-1 * q2^-1 + q2 * t1 * q2^-1 + t2
                 *
                 * by inspection the following holds:
                 *
                 * Compound rotation: q2 * q1
                 * Compound translation: q2 * t1 * q2^-1 + t2
                 *
                 */

                trans->m_Rotation = parent_trans->m_Rotation * instance->m_Rotation;
                trans->m_Translation = rotate(parent_trans->m_Rotation, Vector3(instance->m_Position))
                                      + parent_trans->m_Translation;
            }
        }
    }

    bool Update(HCollection collection, const UpdateContext* update_context)
    {
        DM_PROFILE(GameObject, "Update");

        assert(collection != 0x0);

        collection->m_InUpdate = 1;

        bool ret = true;

        uint32_t component_types = collection->m_Register->m_ComponentTypeCount;
        for (uint32_t i = 0; i < component_types; ++i)
        {
            uint16_t update_index = collection->m_Register->m_ComponentTypesOrder[i];
            ComponentType* component_type = &collection->m_Register->m_ComponentTypes[update_index];

            DM_COUNTER(component_type->m_Name, collection->m_ComponentInstanceCount[update_index]);

            if (component_type->m_UpdateFunction)
            {
                DM_PROFILE(GameObject, component_type->m_Name);
                ComponentsUpdateParams params;
                params.m_Collection = collection;
                params.m_UpdateContext = update_context;
                params.m_World = collection->m_ComponentWorlds[update_index];
                params.m_Context = component_type->m_Context;
                UpdateResult res = component_type->m_UpdateFunction(params);
                if (res != UPDATE_RESULT_OK)
                    ret = false;
            }
            // TODO: Solve this better! Right now the worst is assumed, which is that every component updates some transforms as well as
            // demands updated child-transforms. Many redundant calculations. This could be solved by splitting the component Update-callback
            // into UpdateTrasform, then Update or similar
            UpdateTransforms(collection);

            if (!DispatchMessages(collection))
                ret = false;
        }

        collection->m_InUpdate = 0;

        return ret;
    }

    bool PostUpdate(HCollection collection)
    {
        DM_PROFILE(GameObject, "PostUpdate");

        assert(collection != 0x0);
        HRegister reg = collection->m_Register;
        assert(reg);

        bool result = true;

        if (collection->m_InstancesToDelete.Size() > 0)
        {
            uint32_t n_to_delete = collection->m_InstancesToDelete.Size();
            for (uint32_t j = 0; j < n_to_delete; ++j)
            {
                uint16_t index = collection->m_InstancesToDelete[j];
                Instance* instance = collection->m_Instances[index];

                assert(collection->m_Instances[instance->m_Index] == instance);
                assert(instance->m_ToBeDeleted);
                if (instance->m_Initialized)
                    if (!Final(collection, instance) && result)
                        result = false;
            }
        }

        if (!DispatchMessages(collection) && result)
            result = false;

        uint32_t component_types = reg->m_ComponentTypeCount;
        for (uint32_t i = 0; i < component_types; ++i)
        {
            uint16_t update_index = reg->m_ComponentTypesOrder[i];
            ComponentType* component_type = &reg->m_ComponentTypes[update_index];

            if (component_type->m_PostUpdateFunction)
            {
                DM_PROFILE(GameObject, component_type->m_Name);
                ComponentsPostUpdateParams params;
                params.m_Collection = collection;
                params.m_World = collection->m_ComponentWorlds[update_index];
                params.m_Context = component_type->m_Context;
                UpdateResult res = component_type->m_PostUpdateFunction(params);
                if (res != UPDATE_RESULT_OK && result)
                    result = false;
            }
        }

        if (!DispatchMessages(collection) && result)
            result = false;

        if (collection->m_InstancesToDelete.Size() > 0)
        {
            uint32_t n_to_delete = collection->m_InstancesToDelete.Size();
            for (uint32_t j = 0; j < n_to_delete; ++j)
            {
                uint16_t index = collection->m_InstancesToDelete[j];
                Instance* instance = collection->m_Instances[index];

                assert(collection->m_Instances[instance->m_Index] == instance);
                assert(instance->m_ToBeDeleted);
                DoDelete(collection, instance);
            }
            collection->m_InstancesToDelete.SetSize(0);
        }

        return result;
    }

    UpdateResult DispatchInput(HCollection collection, InputAction* input_actions, uint32_t input_action_count)
    {
        DM_PROFILE(GameObject, "DispatchInput");
        // iterate stacks from top to bottom
        for (uint32_t i = 0; i < input_action_count; ++i)
        {
            InputAction& input_action = input_actions[i];
            if (input_action.m_ActionId != 0)
            {
                uint32_t stack_size = collection->m_InputFocusStack.Size();
                for (uint32_t k = 0; k < stack_size && input_action.m_ActionId != 0; ++k)
                {
                    HInstance instance = collection->m_InputFocusStack[stack_size - 1 - k];
                    Prototype* prototype = instance->m_Prototype;
                    uint32_t components_size = prototype->m_Components.Size();

                    InputResult res = INPUT_RESULT_IGNORED;
                    uint32_t next_component_instance_data = 0;
                    for (uint32_t l = 0; l < components_size; ++l)
                    {
                        ComponentType* component_type = prototype->m_Components[l].m_Type;
                        assert(component_type);
                        if (component_type->m_OnInputFunction)
                        {
                            uintptr_t* component_instance_data = 0;
                            if (component_type->m_InstanceHasUserData)
                            {
                                component_instance_data = &instance->m_ComponentInstanceUserData[next_component_instance_data];
                            }
                            ComponentOnInputParams params;
                            params.m_Instance = instance;
                            params.m_InputAction = &input_action;
                            params.m_Context = component_type->m_Context;
                            params.m_UserData = component_instance_data;
                            InputResult comp_res = component_type->m_OnInputFunction(params);
                            if (comp_res == INPUT_RESULT_CONSUMED)
                                res = comp_res;
                            else if (comp_res == INPUT_RESULT_UNKNOWN_ERROR)
                                return UPDATE_RESULT_UNKNOWN_ERROR;
                        }
                        if (component_type->m_InstanceHasUserData)
                        {
                            next_component_instance_data++;
                        }
                    }
                    if (res == INPUT_RESULT_CONSUMED)
                    {
                        memset(&input_action, 0, sizeof(InputAction));
                    }
                }
            }
        }
        return UPDATE_RESULT_OK;
    }

    void AcquireInputFocus(HCollection collection, HInstance instance)
    {
        bool found = false;
        for (uint32_t i = 0; i < collection->m_InputFocusStack.Size(); ++i)
        {
            if (collection->m_InputFocusStack[i] == instance)
            {
                found = true;
            }
            if (found && i < collection->m_InputFocusStack.Size() - 1)
            {
                collection->m_InputFocusStack[i] = collection->m_InputFocusStack[i + 1];
            }
        }
        if (found)
        {
            collection->m_InputFocusStack.Pop();
        }
        if (!collection->m_InputFocusStack.Full())
        {
            collection->m_InputFocusStack.Push(instance);
        }
        else
        {
            dmLogWarning("Input focus could not be acquired since the buffer is full (%d).", collection->m_InputFocusStack.Size());
        }
    }

    void ReleaseInputFocus(HCollection collection, HInstance instance)
    {
        bool found = false;
        for (uint32_t i = 0; i < collection->m_InputFocusStack.Size(); ++i)
        {
            if (collection->m_InputFocusStack[i] == instance)
            {
                found = true;
            }
            if (found && i < collection->m_InputFocusStack.Size() - 1)
            {
                collection->m_InputFocusStack[i] = collection->m_InputFocusStack[i + 1];
            }
        }
        if (found)
        {
            collection->m_InputFocusStack.Pop();
        }
    }

    HCollection GetCollection(HInstance instance)
    {
        return instance->m_Collection;
    }

    dmResource::HFactory GetFactory(HCollection collection)
    {
        if (collection != 0x0)
            return collection->m_Factory;
        else
            return 0x0;
    }

    HRegister GetRegister(HCollection collection)
    {
        if (collection != 0x0)
            return collection->m_Register;
        else
            return 0x0;
    }

    dmMessage::HSocket GetMessageSocket(HCollection collection)
    {
        if (collection)
            return collection->m_Socket;
        else
            return 0;
    }

    void SetPosition(HInstance instance, Point3 position)
    {
        instance->m_Position = position;
    }

    Point3 GetPosition(HInstance instance)
    {
        return instance->m_Position;
    }

    void SetRotation(HInstance instance, Quat rotation)
    {
        instance->m_Rotation = rotation;
    }

    Quat GetRotation(HInstance instance)
    {
        return instance->m_Rotation;
    }

    Point3 GetWorldPosition(HInstance instance)
    {
        HCollection collection = instance->m_Collection;
        return collection->m_WorldTransforms[instance->m_Index].m_Translation;
    }

    Quat GetWorldRotation(HInstance instance)
    {
        HCollection collection = instance->m_Collection;
        return collection->m_WorldTransforms[instance->m_Index].m_Rotation;
    }

    Result SetParent(HInstance child, HInstance parent)
    {
        if (parent == 0 && child->m_Parent == INVALID_INSTANCE_INDEX)
            return RESULT_OK;

        if (parent != 0 && parent->m_Depth >= MAX_HIERARCHICAL_DEPTH-1)
        {
            dmLogError("Unable to set parent to child. Parent at maximum depth %d", MAX_HIERARCHICAL_DEPTH-1);
            return RESULT_MAXIMUM_HIEARCHICAL_DEPTH;
        }

        HCollection collection = child->m_Collection;

        if (parent != 0)
        {
            uint32_t index = parent->m_Index;
            while (index != INVALID_INSTANCE_INDEX)
            {
                Instance* i = collection->m_Instances[index];

                if (i == child)
                {
                    dmLogError("Unable to set parent to child. Child is present in tree above parent. Unsupported");
                    return RESULT_INVALID_OPERATION;

                }
                index = i->m_Parent;
            }
            assert(child->m_Collection == parent->m_Collection);
            assert(collection->m_LevelInstanceCount[child->m_Depth+1] < collection->m_MaxInstances);
        }
        else
        {
            assert(collection->m_LevelInstanceCount[0] < collection->m_MaxInstances);
        }

        if (child->m_Parent != INVALID_INSTANCE_INDEX)
        {
            Unlink(collection, child);
        }

        EraseSwapLevelIndex(collection, child);

        // Add child to parent
        if (parent != 0)
        {
            if (parent->m_FirstChildIndex == INVALID_INSTANCE_INDEX)
            {
                parent->m_FirstChildIndex = child->m_Index;
            }
            else
            {
                Instance* first_child = collection->m_Instances[parent->m_FirstChildIndex];
                assert(parent->m_Depth == first_child->m_Depth - 1);

                child->m_SiblingIndex = first_child->m_Index;
                parent->m_FirstChildIndex = child->m_Index;
            }
        }

        int original_child_depth = child->m_Depth;
        if (parent != 0)
        {
            child->m_Parent = parent->m_Index;
            child->m_Depth = parent->m_Depth + 1;
        }
        else
        {
            child->m_Parent = INVALID_INSTANCE_INDEX;
            child->m_Depth = 0;
        }
        InsertInstanceInLevelIndex(collection, child);

        int32_t n_steps =  (int32_t) original_child_depth - (int32_t) child->m_Depth;
        if (n_steps < 0)
        {
            for (int i = 0; i < -n_steps; ++i)
            {
                MoveAllDown(collection, child);
            }
        }
        else
        {
            for (int i = 0; i < n_steps; ++i)
            {
                MoveAllUp(collection, child);
            }
        }

        return RESULT_OK;
    }

    HInstance GetParent(HInstance instance)
    {
        if (instance->m_Parent == INVALID_INSTANCE_INDEX)
        {
            return 0;
        }
        else
        {
            return instance->m_Collection->m_Instances[instance->m_Parent];
        }
    }

    uint32_t GetDepth(HInstance instance)
    {
        return instance->m_Depth;
    }

    uint32_t GetChildCount(HInstance instance)
    {
        Collection* c = instance->m_Collection;
        uint32_t count = 0;
        uint32_t index = instance->m_FirstChildIndex;
        while (index != INVALID_INSTANCE_INDEX)
        {
            ++count;
            index = c->m_Instances[index]->m_SiblingIndex;
        }

        return count;
    }

    bool IsChildOf(HInstance child, HInstance parent)
    {
        Collection* c = parent->m_Collection;
        uint32_t index = parent->m_FirstChildIndex;
        while (index != INVALID_INSTANCE_INDEX)
        {
            Instance*i = c->m_Instances[index];
            if (i == child)
                return true;
            index = c->m_Instances[index]->m_SiblingIndex;
        }

        return false;
    }

    void ResourceReloadedCallback(void* user_data, dmResource::SResourceDescriptor* descriptor, const char* name)
    {
        Collection* collection = (Collection*)user_data;
        for (uint32_t level = 0; level < MAX_HIERARCHICAL_DEPTH; ++level)
        {
            uint32_t max_instance = collection->m_Instances.Size();
            for (uint32_t i = 0; i < collection->m_LevelInstanceCount[level]; ++i)
            {
                uint16_t index = collection->m_LevelIndices[level * max_instance + i];
                Instance* instance = collection->m_Instances[index];
                uint32_t next_component_instance_data = 0;
                for (uint32_t j = 0; j < instance->m_Prototype->m_Components.Size(); ++j)
                {
                    Prototype::Component& component = instance->m_Prototype->m_Components[j];
                    ComponentType* type = component.m_Type;
                    if (component.m_ResourceId == descriptor->m_NameHash)
                    {
                        if (type->m_OnReloadFunction)
                        {
                            uintptr_t* user_data = 0;
                            if (type->m_InstanceHasUserData)
                            {
                                user_data = &instance->m_ComponentInstanceUserData[next_component_instance_data];
                            }
                            ComponentOnReloadParams params;
                            params.m_Instance = instance;
                            params.m_Resource = descriptor->m_Resource;
                            params.m_World = collection->m_ComponentWorlds[component.m_TypeIndex];
                            params.m_Context = type->m_Context;
                            params.m_UserData = user_data;
                            type->m_OnReloadFunction(params);
                        }
                    }
                    if (type->m_InstanceHasUserData)
                    {
                        next_component_instance_data++;
                    }
                }
            }
        }
    }

    lua_State* GetLuaState()
    {
        return g_LuaState;
    }
}
