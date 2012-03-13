#ifndef GAMEOBJECT_COMMON_H
#define GAMEOBJECT_COMMON_H

#include <dlib/hash.h>
#include <dlib/hashtable.h>
#include <dlib/index_pool.h>
#include <dlib/math.h>
#include <dlib/mutex.h>

#include "gameobject.h"

extern "C"
{
#include <lua/lua.h>
}

namespace dmGameObject
{
#define SCRIPT_INSTANCE_NAME "__script_instance__"

    extern const char* ID_SEPARATOR;

    extern lua_State* g_LuaState;

    // TODO: Configurable?
    const uint32_t MAX_MESSAGE_DATA_SIZE = 256;

    struct Prototype
    {
        struct Component
        {
            Component(void* resource,
                      uint32_t resource_type,
                      dmhash_t id,
                      dmhash_t resource_id,
                      ComponentType* type,
                      uint32_t type_index,
                      const Point3& position,
                      const Quat& rotation) :
                m_Id(id),
                m_ResourceId(resource_id),
                m_Type(type),
                m_TypeIndex(type_index),
                m_Resource(resource),
                m_ResourceType(resource_type),
                m_Position(position),
                m_Rotation(rotation)
            {
            }

            dmhash_t        m_Id;
            dmhash_t        m_ResourceId;
            ComponentType*  m_Type;
            uint32_t        m_TypeIndex;
            void*           m_Resource;
            uint32_t        m_ResourceType;
            Point3          m_Position;
            Quat            m_Rotation;
        };

        dmArray<Component>     m_Components;
    };

    // Invalid instance index. Implies that maximum number of instances is 32766 (ie 0x7fff - 1)
    const uint32_t INVALID_INSTANCE_INDEX = 0x7fff;

    // NOTE: Actual size of Instance is sizeof(Instance) + sizeof(uintptr_t) * m_UserDataCount
    struct Instance
    {
        Instance(Prototype* prototype)
        {
            m_Collection = 0;
            m_Rotation = Quat::identity();
            m_Position = Point3(0,0,0);
            m_Prototype = prototype;
            m_Identifier = UNNAMED_IDENTIFIER;
            dmHashInit64(&m_CollectionPathHashState, true);
            m_Depth = 0;
            m_Initialized = 0;
            m_Parent = INVALID_INSTANCE_INDEX;
            m_Index = INVALID_INSTANCE_INDEX;
            m_LevelIndex = INVALID_INSTANCE_INDEX;
            m_SiblingIndex = INVALID_INSTANCE_INDEX;
            m_FirstChildIndex = INVALID_INSTANCE_INDEX;
            m_ToBeDeleted = 0;
        }

        ~Instance()
        {
        }

        // Collection this instances belongs to. Added for GetWorldPosition.
        // We should consider to remove this (memory footprint)
        HCollection     m_Collection;
        Quat            m_Rotation;
        Point3          m_Position;
        Prototype*      m_Prototype;
        dmhash_t        m_Identifier;

        // Collection path hash-state. Used for calculating global identifiers. Contains the hash-state for the collection-path to the instance.
        // We might, in the future, for memory reasons, move this hash-state to a data-structure shared among all instances from the same collection.
        HashState64     m_CollectionPathHashState;

        // Hierarchical depth
        uint16_t        m_Depth : 4;
        // If the instance was initialized or not (Init())
        uint16_t        m_Initialized : 1;
        // Padding
        uint16_t        m_Pad : 11;

        // Index to parent
        uint16_t        m_Parent : 16;

        // Index to Collection::m_Instances
        uint16_t        m_Index : 15;
        // Used for deferred deletion
        uint16_t        m_ToBeDeleted : 1;

        // Index to Collection::m_LevelIndex. Index is relative to current level (m_Depth), eg first object in level L always has level-index 0
        // Level-index is used to reorder Collection::m_LevelIndex entries in O(1). Given an instance we need to find where the
        // instance index is located in Collection::m_LevelIndex
        uint16_t        m_LevelIndex : 15;
        uint16_t        m_Pad2 : 1;

        // Next sibling index. Index to Collection::m_Instances
        uint16_t        m_SiblingIndex : 15;
        uint16_t        m_Pad3 : 1;

        // First child index. Index to Collection::m_Instances
        uint16_t        m_FirstChildIndex : 15;
        uint16_t        m_Pad4 : 1;

        uint32_t        m_ComponentInstanceUserDataCount;
        uintptr_t       m_ComponentInstanceUserData[0];
    };

    // Internal representation of a transform
    struct Transform
    {
        Point3 m_Translation;
        Quat   m_Rotation;
    };

    // Max component types could not be larger than 255 since the index is stored as a uint8_t
    const uint32_t MAX_COMPONENT_TYPES = 255;

    #define DM_GAMEOBJECT_CURRENT_IDENTIFIER_PATH_MAX (512)
    struct Register
    {
        uint32_t                    m_ComponentTypeCount;
        ComponentType               m_ComponentTypes[MAX_COMPONENT_TYPES];
        uint16_t                    m_ComponentTypesOrder[MAX_COMPONENT_TYPES];
        dmMutex::Mutex              m_Mutex;
        // Current identifier path. Used during loading of collections and specifically collections in collections.
        // Contains the current path and is *protected* by m_Mutex.
        char                        m_CurrentIdentifierPath[DM_GAMEOBJECT_CURRENT_IDENTIFIER_PATH_MAX];

        // All collections. Protected by m_Mutex
        dmArray<HCollection>        m_Collections;

        // Pointer to current collection. Protected by m_Mutex. Related to m_CurrentIdentifierPath above.
        Collection*                 m_CurrentCollection;

        Vector3                     m_AccumulatedTranslation;
        Quat                        m_AccumulatedRotation;

        Register();
        ~Register();
    };

    // Max hierarchical depth
    // 4 is interpreted as up to four levels of child nodes including root-nodes
    // Must be greater than zero
    const uint32_t MAX_HIERARCHICAL_DEPTH = 4;
    struct Collection
    {
        Collection(dmResource::HFactory factory, HRegister regist, uint32_t max_instances)
        {
            m_Factory = factory;
            m_Register = regist;
            m_MaxInstances = max_instances;
            m_Instances.SetCapacity(max_instances);
            m_Instances.SetSize(max_instances);
            m_InstanceIndices.SetCapacity(max_instances);
            m_LevelIndices.SetCapacity(max_instances * MAX_HIERARCHICAL_DEPTH);
            m_LevelIndices.SetSize(max_instances * MAX_HIERARCHICAL_DEPTH);
            m_WorldTransforms.SetCapacity(max_instances);
            m_WorldTransforms.SetSize(max_instances);
            m_InstancesToDelete.SetCapacity(max_instances);
            m_IDToInstance.SetCapacity(dmMath::Max(1U, max_instances/3), max_instances);
            // TODO: Un-hard-code
            m_InputFocusStack.SetCapacity(16);
            m_NameHash = 0;
            m_ComponentSocket = 0;
            m_FrameSocket = 0;
            m_GenInstanceCounter = 0;
            m_InUpdate = 0;

            for (uint32_t i = 0; i < m_LevelIndices.Size(); ++i)
            {
                m_LevelIndices[i] = INVALID_INSTANCE_INDEX;
            }

            memset(&m_Instances[0], 0, sizeof(Instance*) * max_instances);
            memset(&m_WorldTransforms[0], 0xcc, sizeof(Transform) * max_instances);
            memset(&m_LevelInstanceCount[0], 0, sizeof(m_LevelInstanceCount));
            memset(&m_ComponentInstanceCount[0], 0, sizeof(uint32_t) * MAX_COMPONENT_TYPES);
        }
        // Resource factory
        dmResource::HFactory     m_Factory;

        // GameObject component register
        HRegister                m_Register;

        // Component type specific worlds
        void*                    m_ComponentWorlds[MAX_COMPONENT_TYPES];
        // Component type specific instance counters
        uint32_t                 m_ComponentInstanceCount[MAX_COMPONENT_TYPES];

        // Maximum number of instances
        uint32_t                 m_MaxInstances;

        // Array of instances. Zero values for free slots. Order must
        // always be preserved. Slots are allocated using index-pool
        // m_InstanceIndices below
        // Size if always = max_instances (at least for now)
        dmArray<Instance*>       m_Instances;

        // Index pool for mapping Instance::m_Index to m_Instances
        dmIndexPool16            m_InstanceIndices;

        // Index array of size m_Instances.Size() * MAX_HIERARCHICAL_DEPTH
        // Array of instance indices for each hierarchical level
        // Used for calculating transforms in scene-graph
        // Two dimensional table of indices with stride "max_instances"
        // Level 0 contains root-nodes in [0..m_LevelInstanceCount[0]-1]
        // Level 1 contains level 1 indices in [max_instances..max_instances+m_LevelInstanceCount[1]-1], ...
        dmArray<uint16_t>        m_LevelIndices;

        // Number of instances in each level
        uint16_t                 m_LevelInstanceCount[MAX_HIERARCHICAL_DEPTH];

        // Array of world transforms. Calculated using m_LevelIndices above
        dmArray<Transform>       m_WorldTransforms;

        // NOTE: Be *very* careful about m_InstancesToDelete
        // m_InstancesToDelete is an array of instances flagged for delete during Update(.)
        // Related code is Delete(.) and Update(.)
        dmArray<uint16_t>        m_InstancesToDelete;

        // Identifier to Instance mapping
        dmHashTable64<Instance*> m_IDToInstance;

        // Stack keeping track of which instance has the input focus
        dmArray<Instance*>       m_InputFocusStack;

        // Name-hash of the collection.
        dmhash_t                 m_NameHash;

        // Socket for sending to instances, dispatched between every component update
        dmMessage::HSocket       m_ComponentSocket;
        // Socket for sending to instances, dispatched once each update
        dmMessage::HSocket       m_FrameSocket;

        dmMutex::Mutex           m_Mutex;

        // Counter for generating instance ids, protected by m_Mutex
        uint32_t                 m_GenInstanceCounter;

        // Set to 1 if in update-loop
        uint32_t                 m_InUpdate : 1;
    };

    ComponentType* FindComponentType(Register* regist, uint32_t resource_type, uint32_t* index);
}

#endif // GAMEOBJECT_COMMON_H
