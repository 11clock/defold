#include "comp_collision_object.h"

#include <dlib/dstrings.h>
#include <dlib/hash.h>
#include <dlib/log.h>
#include <dlib/math.h>

#include <physics/physics.h>

#include <gameobject/gameobject_ddf.h>

#include "gamesys.h"
#include "../resources/res_collision_object.h"

#include "../proto/physics_ddf.h"

namespace dmGameSystem
{
    using namespace Vectormath::Aos;

    static const uint32_t MAX_COLLISION_COUNT = 64;
    static const uint32_t MAX_CONTACT_COUNT = 128;

    struct World
    {
        uint64_t m_Groups[16];
        union
        {
            dmPhysics::HWorld2D m_World2D;
            dmPhysics::HWorld3D m_World3D;
        };
    };

    struct Component
    {
        CollisionObjectResource* m_Resource;
        dmGameObject::HInstance m_Instance;
        union
        {
            dmPhysics::HCollisionObject3D m_Object3D;
            dmPhysics::HCollisionObject2D m_Object2D;
        };
        uint8_t m_ComponentIndex;
    };

    void GetWorldTransform(void* user_data, Vectormath::Aos::Point3& position, Vectormath::Aos::Quat& rotation)
    {
        if (!user_data)
            return;
        Component* component = (Component*)user_data;
        dmGameObject::HInstance instance = component->m_Instance;
        position = dmGameObject::GetWorldPosition(instance);
        rotation = dmGameObject::GetWorldRotation(instance);
    }

    void SetWorldTransform(void* user_data, const Vectormath::Aos::Point3& position, const Vectormath::Aos::Quat& rotation)
    {
        if (!user_data)
            return;
        Component* component = (Component*)user_data;
        dmGameObject::HInstance instance = component->m_Instance;
        dmGameObject::SetPosition(instance, position);
        dmGameObject::SetRotation(instance, rotation);
    }

    dmGameObject::CreateResult CompCollisionObjectNewWorld(const dmGameObject::ComponentNewWorldParams& params)
    {
        PhysicsContext* physics_context = (PhysicsContext*)params.m_Context;
        dmPhysics::NewWorldParams world_params;
        world_params.m_GetWorldTransformCallback = GetWorldTransform;
        world_params.m_SetWorldTransformCallback = SetWorldTransform;
        World* world = new World();
        memset(world, 0, sizeof(World));
        if (physics_context->m_3D)
            world->m_World3D = dmPhysics::NewWorld3D(physics_context->m_Context3D, world_params);
        else
            world->m_World2D = dmPhysics::NewWorld2D(physics_context->m_Context2D, world_params);
        *params.m_World = world;
        return dmGameObject::CREATE_RESULT_OK;
    }

    dmGameObject::CreateResult CompCollisionObjectDeleteWorld(const dmGameObject::ComponentDeleteWorldParams& params)
    {
        PhysicsContext* physics_context = (PhysicsContext*)params.m_Context;
        World* world = (World*)params.m_World;
        if (physics_context->m_3D)
            dmPhysics::DeleteWorld3D(physics_context->m_Context3D, world->m_World3D);
        else
            dmPhysics::DeleteWorld2D(physics_context->m_Context2D, world->m_World2D);
        delete world;
        return dmGameObject::CREATE_RESULT_OK;
    }

    dmGameObject::CreateResult CompCollisionObjectCreate(const dmGameObject::ComponentCreateParams& params)
    {
        CollisionObjectResource* co_res = (CollisionObjectResource*)params.m_Resource;
        if (co_res == 0x0 || co_res->m_DDF == 0x0)
            return dmGameObject::CREATE_RESULT_UNKNOWN_ERROR;
        if ((co_res->m_DDF->m_Mass == 0.0f && co_res->m_DDF->m_Type == dmPhysicsDDF::COLLISION_OBJECT_TYPE_DYNAMIC)
            || (co_res->m_DDF->m_Mass > 0.0f && co_res->m_DDF->m_Type != dmPhysicsDDF::COLLISION_OBJECT_TYPE_DYNAMIC))
        {
            dmLogError("Invalid mass %f for shape type %d", co_res->m_DDF->m_Mass, co_res->m_DDF->m_Type);
            return dmGameObject::CREATE_RESULT_UNKNOWN_ERROR;
        }
        Component* component = new Component();
        component->m_Resource = (CollisionObjectResource*)params.m_Resource;
        component->m_Instance = params.m_Instance;
        component->m_Object2D = 0;
        component->m_ComponentIndex = params.m_ComponentIndex;
        *params.m_UserData = (uintptr_t)component;
        return dmGameObject::CREATE_RESULT_OK;
    }

    dmGameObject::CreateResult CompCollisionObjectDestroy(const dmGameObject::ComponentDestroyParams& params)
    {
        delete (Component*)*params.m_UserData;
        return dmGameObject::CREATE_RESULT_OK;
    }

    uint16_t GetGroupBitIndex(World* world, uint64_t group_hash)
    {
        if (group_hash != 0)
        {
            for (uint32_t i = 0; i < 16; ++i)
            {
                if (world->m_Groups[i] != 0)
                {
                    if (world->m_Groups[i] == group_hash)
                    {
                        return 1 << i;
                    }
                }
                else
                {
                    world->m_Groups[i] = group_hash;
                    return 1 << i;
                }
            }
            // When we get here, there are no more group bits available
            const void* group = dmHashReverse64(group_hash, 0x0);
            if (group != 0x0)
            {
                dmLogWarning("The collision group '%s' could not be used since the maximum group count has been reached (16).", (const char*)group);
            }
        }
        return 0;
    }

    uint64_t GetLSBGroupHash(World* world, uint16_t mask)
    {
        if (mask > 0)
        {
            uint32_t index = 0;
            while ((mask & 1) == 0)
            {
                mask >>= 1;
                ++index;
            }
            return world->m_Groups[index];
        }
        return 0;
    }

    bool CreateCollisionObject(PhysicsContext* physics_context, World* world, dmGameObject::HInstance instance, Component* component)
    {
        CollisionObjectResource* resource = component->m_Resource;
        dmPhysicsDDF::CollisionObjectDesc* ddf = resource->m_DDF;
        dmPhysics::CollisionObjectData data;
        data.m_UserData = component;
        data.m_Type = (dmPhysics::CollisionObjectType)ddf->m_Type;
        data.m_Mass = ddf->m_Mass;
        data.m_Friction = ddf->m_Friction;
        data.m_Restitution = ddf->m_Restitution;
        data.m_Group = GetGroupBitIndex(world, resource->m_Group);
        data.m_Mask = 0;
        for (uint32_t i = 0; i < 16 && resource->m_Mask[i] != 0; ++i)
        {
            data.m_Mask |= GetGroupBitIndex(world, resource->m_Mask[i]);
        }
        if (physics_context->m_3D)
        {
            if (resource->m_TileGrid)
            {
                dmLogError("Collision objects in 3D can not have a tile grid as shape.");
                return false;
            }
            dmPhysics::HWorld3D physics_world = world->m_World3D;
            dmPhysics::HCollisionObject3D collision_object =
                    dmPhysics::NewCollisionObject3D(physics_world, data,
                                                    resource->m_Shapes3D,
                                                    resource->m_ShapeTranslation,
                                                    resource->m_ShapeRotation,
                                                    resource->m_ShapeCount);

            if (collision_object != 0x0)
            {
                if (component->m_Object3D != 0x0)
                    dmPhysics::DeleteCollisionObject3D(physics_world, component->m_Object3D);
                component->m_Object3D = collision_object;
            }
            else
            {
                return false;
            }
        }
        else
        {
            dmPhysics::HWorld2D physics_world = world->m_World2D;
            dmPhysics::HCollisionObject2D collision_object = 0;
            if (resource->m_TileGrid)
            {
                dmPhysics::HCollisionShape2D shape = resource->m_TileGridResource->m_GridShape;
                collision_object = dmPhysics::NewCollisionObject2D(physics_world, data, &shape, 1);
            }
            else
            {
                collision_object = dmPhysics::NewCollisionObject2D(physics_world, data,
                                                                   resource->m_Shapes2D,
                                                                   resource->m_ShapeTranslation,
                                                                   resource->m_ShapeRotation,
                                                                   resource->m_ShapeCount);
            }

            if (collision_object != 0x0)
            {
                if (component->m_Object2D != 0x0)
                    dmPhysics::DeleteCollisionObject2D(physics_world, component->m_Object2D);
                component->m_Object2D = collision_object;
                if (component->m_Resource->m_TileGrid)
                {
                    dmPhysics::HCollisionShape2D shape = resource->m_TileGridResource->m_GridShape;
                    TileGridResource* tile_grid_resource = component->m_Resource->m_TileGridResource;
                    dmGameSystemDDF::TileGrid* tile_grid = tile_grid_resource->m_TileGrid;
                    TileSetResource* tile_set_resource = tile_grid_resource->m_TileSet;
                    dmGameSystemDDF::TileSet* tile_set = tile_set_resource->m_TileSet;
                    uint32_t layer_count = tile_grid->m_Layers.m_Count;
                    for (uint32_t i = 0; i < layer_count; ++i)
                    {
                        dmGameSystemDDF::TileLayer* layer = &tile_grid->m_Layers[i];
                        uint32_t cell_count = layer->m_Cell.m_Count;
                        for (uint32_t j = 0; j < cell_count; ++j)
                        {
                            dmGameSystemDDF::TileCell* cell = &layer->m_Cell[j];
                            uint32_t tile = cell->m_Tile;

                            if (tile < tile_set->m_ConvexHulls.m_Count && tile_set->m_ConvexHulls[tile].m_Count > 0)
                            {
                                uint32_t cell_x = cell->m_X - tile_grid_resource->m_MinCellX;
                                uint32_t cell_y = cell->m_Y - tile_grid_resource->m_MinCellY;
                                dmPhysics::SetGridShapeHull(component->m_Object2D, shape, cell_y, cell_x, tile);
                                uint16_t child = cell_x + tile_grid_resource->m_ColumnCount * cell_y;
                                uint16_t group = GetGroupBitIndex(world, tile_set_resource->m_HullCollisionGroups[tile]);
                                dmPhysics::SetCollisionObjectFilter(component->m_Object2D, 0, child, group, data.m_Mask);
                            }
                        }
                    }
                }
            }
            else
            {
                return false;
            }
        }
        return true;
    }

    dmGameObject::CreateResult CompCollisionObjectInit(const dmGameObject::ComponentInitParams& params)
    {
        PhysicsContext* physics_context = (PhysicsContext*)params.m_Context;
        Component* component = (Component*) *params.m_UserData;
        if (component->m_Object2D == 0)
        {
            World* world = (World*)params.m_World;
            if (!CreateCollisionObject(physics_context, world, params.m_Instance, component))
            {
                return dmGameObject::CREATE_RESULT_UNKNOWN_ERROR;
            }
        }
        return dmGameObject::CREATE_RESULT_OK;
    }

    dmGameObject::CreateResult CompCollisionObjectFinal(const dmGameObject::ComponentFinalParams& params)
    {
        PhysicsContext* physics_context = (PhysicsContext*)params.m_Context;
        Component* component = (Component*)*params.m_UserData;
        World* world = (World*)params.m_World;
        if (physics_context->m_3D)
        {
            if (component->m_Object3D != 0)
            {
                dmPhysics::HWorld3D physics_world = world->m_World3D;
                dmPhysics::DeleteCollisionObject3D(physics_world, component->m_Object3D);
                component->m_Object3D = 0;
            }
        }
        else
        {
            if (component->m_Object2D != 0)
            {
                dmPhysics::HWorld2D physics_world = world->m_World2D;
                dmPhysics::DeleteCollisionObject2D(physics_world, component->m_Object2D);
                component->m_Object2D = 0;
            }
        }
        return dmGameObject::CREATE_RESULT_OK;
    }

    struct CollisionUserData
    {
        World* m_World;
        uint32_t m_Count;
    };

    bool CollisionCallback(void* user_data_a, uint16_t group_a, void* user_data_b, uint16_t group_b, void* user_data)
    {
        CollisionUserData* cud = (CollisionUserData*)user_data;
        if (cud->m_Count < MAX_COLLISION_COUNT)
        {
            cud->m_Count += 1;

            Component* component_a = (Component*)user_data_a;
            Component* component_b = (Component*)user_data_b;
            dmGameObject::HInstance instance_a = component_a->m_Instance;
            dmGameObject::HInstance instance_b = component_b->m_Instance;
            dmhash_t instance_a_id = dmGameObject::GetIdentifier(instance_a);
            dmhash_t instance_b_id = dmGameObject::GetIdentifier(instance_b);
            dmhash_t message_id = dmHashString64(dmPhysicsDDF::CollisionResponse::m_DDFDescriptor->m_Name);
            uintptr_t descriptor = (uintptr_t)dmPhysicsDDF::CollisionResponse::m_DDFDescriptor;
            uint32_t data_size = sizeof(dmPhysicsDDF::CollisionResponse);
            dmPhysicsDDF::CollisionResponse ddf;
            dmMessage::URL sender;
            dmMessage::URL receiver;

            // Broadcast to A components
            ddf.m_Group = GetLSBGroupHash(cud->m_World, group_b);
            ddf.m_OtherId = instance_b_id;
            receiver.m_Socket = dmGameObject::GetMessageSocket(dmGameObject::GetCollection(instance_a));
            receiver.m_Path = instance_a_id;
            // sender is the same as receiver, but with the specific collision object as fragment
            sender = receiver;
            dmGameObject::Result r = dmGameObject::GetComponentId(instance_a, component_a->m_ComponentIndex, &sender.m_Fragment);
            if (r != dmGameObject::RESULT_OK)
            {
                dmLogError("Could not retrieve sender component when reporting collisions: %d", r);
            }
            dmMessage::Result result = dmMessage::Post(&sender, &receiver, message_id, 0, descriptor, &ddf, data_size);
            if (result != dmMessage::RESULT_OK)
            {
                dmLogError("Could not send collision callback to component: %d", result);
            }

            // Broadcast to B components
            ddf.m_Group = GetLSBGroupHash(cud->m_World, group_a);
            ddf.m_OtherId = dmGameObject::GetIdentifier(instance_a);
            receiver.m_Socket = dmGameObject::GetMessageSocket(dmGameObject::GetCollection(instance_b));
            receiver.m_Path = instance_b_id;
            // sender is the same as receiver, but with the specific collision object as fragment
            sender = receiver;
            r = dmGameObject::GetComponentId(instance_b, component_b->m_ComponentIndex, &sender.m_Fragment);
            if (r != dmGameObject::RESULT_OK)
            {
                dmLogError("Could not retrieve sender component when reporting collisions: %d", result);
            }
            result = dmMessage::Post(&sender, &receiver, message_id, 0, descriptor, &ddf, data_size);
            if (result != dmMessage::RESULT_OK)
            {
                dmLogError("Could not send collision callback to component: %d", result);
            }

            return true;
        }
        else
        {
            return false;
        }
    }

    bool ContactPointCallback(const dmPhysics::ContactPoint& contact_point, void* user_data)
    {
        CollisionUserData* cud = (CollisionUserData*)user_data;
        if (cud->m_Count < MAX_CONTACT_COUNT)
        {
            cud->m_Count += 1;

            Component* component_a = (Component*)contact_point.m_UserDataA;
            Component* component_b = (Component*)contact_point.m_UserDataB;
            dmGameObject::HInstance instance_a = component_a->m_Instance;
            dmGameObject::HInstance instance_b = component_b->m_Instance;
            dmhash_t instance_a_id = dmGameObject::GetIdentifier(instance_a);
            dmhash_t instance_b_id = dmGameObject::GetIdentifier(instance_b);

            dmPhysicsDDF::ContactPointResponse ddf;
            float mass_a = dmMath::Select(-contact_point.m_InvMassA, 0.0f, 1.0f / contact_point.m_InvMassA);
            float mass_b = dmMath::Select(-contact_point.m_InvMassB, 0.0f, 1.0f / contact_point.m_InvMassB);

            dmhash_t message_id = dmHashString64(dmPhysicsDDF::ContactPointResponse::m_DDFDescriptor->m_Name);
            uintptr_t descriptor = (uintptr_t)dmPhysicsDDF::ContactPointResponse::m_DDFDescriptor;
            uint32_t data_size = sizeof(dmPhysicsDDF::ContactPointResponse);
            dmMessage::URL sender;
            dmMessage::URL receiver;

            // Broadcast to A components
            ddf.m_Position = contact_point.m_PositionA;
            ddf.m_Normal = -contact_point.m_Normal;
            ddf.m_RelativeVelocity = -contact_point.m_RelativeVelocity;
            ddf.m_Distance = contact_point.m_Distance;
            ddf.m_AppliedImpulse = contact_point.m_AppliedImpulse;
            ddf.m_Mass = mass_a;
            ddf.m_OtherMass = mass_b;
            ddf.m_OtherId = dmGameObject::GetIdentifier(instance_b);
            ddf.m_OtherPosition = dmGameObject::GetWorldPosition(instance_b);
            ddf.m_Group = GetLSBGroupHash(cud->m_World, contact_point.m_GroupB);
            receiver.m_Socket = dmGameObject::GetMessageSocket(dmGameObject::GetCollection(instance_a));
            receiver.m_Path = instance_a_id;
            // sender is the same as receiver, but with the specific collision object as fragment
            sender = receiver;
            dmGameObject::Result r = dmGameObject::GetComponentId(instance_a, component_a->m_ComponentIndex, &sender.m_Fragment);
            if (r != dmGameObject::RESULT_OK)
            {
                dmLogError("Could not retrieve sender component when reporting collisions: %d", r);
            }
            dmMessage::Result result = dmMessage::Post(&sender, &receiver, message_id, 0, descriptor, &ddf, data_size);
            if (result != dmMessage::RESULT_OK)
            {
                dmLogError("Could not send collision callback to component: %d", result);
            }

            // Broadcast to B components
            ddf.m_Position = contact_point.m_PositionB;
            ddf.m_Normal = contact_point.m_Normal;
            ddf.m_RelativeVelocity = contact_point.m_RelativeVelocity;
            ddf.m_Distance = contact_point.m_Distance;
            ddf.m_AppliedImpulse = contact_point.m_AppliedImpulse;
            ddf.m_Mass = mass_b;
            ddf.m_OtherMass = mass_a;
            ddf.m_OtherId = dmGameObject::GetIdentifier(instance_a);
            ddf.m_OtherPosition = dmGameObject::GetWorldPosition(instance_a);
            ddf.m_Group = GetLSBGroupHash(cud->m_World, contact_point.m_GroupA);
            receiver.m_Socket = dmGameObject::GetMessageSocket(dmGameObject::GetCollection(instance_b));
            receiver.m_Path = instance_b_id;
            // sender is the same as receiver, but with the specific collision object as fragment
            sender = receiver;
            r = dmGameObject::GetComponentId(instance_b, component_b->m_ComponentIndex, &sender.m_Fragment);
            if (r != dmGameObject::RESULT_OK)
            {
                dmLogError("Could not retrieve sender component when reporting collisions: %d", r);
            }
            result = dmMessage::Post(&sender, &receiver, message_id, 0, descriptor, &ddf, data_size);
            if (result != dmMessage::RESULT_OK)
            {
                dmLogError("Could not send collision callback to component: %d", result);
            }

            return true;
        }
        else
        {
            return false;
        }
    }

    static bool g_CollisionOverflowWarning = false;
    static bool g_ContactOverflowWarning = false;

    struct RayCastUserData
    {
        dmGameObject::HInstance m_Instance;
        World* m_World;
    };

    static void RayCastCallback(const dmPhysics::RayCastResponse& response, const dmPhysics::RayCastRequest& request, void* user_data)
    {
        if (response.m_Hit)
        {
            dmGameObject::HInstance instance = (dmGameObject::HInstance)request.m_UserData;
            World* world = (World*)user_data;
            Component* component = (Component*)response.m_CollisionObjectUserData;

            dmPhysicsDDF::RayCastResponse ddf;
            ddf.m_Fraction = response.m_Fraction;
            ddf.m_Id = dmGameObject::GetIdentifier(component->m_Instance);
            ddf.m_Group = GetLSBGroupHash(world, response.m_CollisionObjectGroup);
            ddf.m_Position = response.m_Position;
            ddf.m_Normal = response.m_Normal;
            ddf.m_RequestId = request.m_UserId & 0xff;
            dmhash_t message_id = dmHashString64(dmPhysicsDDF::RayCastResponse::m_DDFDescriptor->m_Name);
            uintptr_t descriptor = (uintptr_t)dmPhysicsDDF::RayCastResponse::m_DDFDescriptor;
            uint32_t data_size = sizeof(dmPhysicsDDF::RayCastResponse);
            dmMessage::URL receiver;
            receiver.m_Socket = dmGameObject::GetMessageSocket(dmGameObject::GetCollection(instance));
            receiver.m_Path = dmGameObject::GetIdentifier(instance);
            uint8_t component_index = request.m_UserId >> 8;
            dmGameObject::Result result = dmGameObject::GetComponentId(instance, component_index, &receiver.m_Fragment);
            if (result != dmGameObject::RESULT_OK)
            {
                dmLogError("Error when sending ray cast response: %d", result);
            }
            else
            {
                dmMessage::Result message_result = dmMessage::Post(0x0, &receiver, message_id, 0, descriptor, &ddf, data_size);
                if (message_result != dmMessage::RESULT_OK)
                {
                    dmLogError("Error when sending ray cast response: %d", message_result);
                }
            }
        }
    }

    struct DispatchContext
    {
        PhysicsContext* m_PhysicsContext;
        bool m_Success;
        dmGameObject::HCollection m_Collection;
        World* m_World;
    };

    void DispatchCallback(dmMessage::Message *message, void* user_ptr)
    {
        DispatchContext* context = (DispatchContext*)user_ptr;
        if (message->m_Descriptor != 0)
        {
            dmDDF::Descriptor* descriptor = (dmDDF::Descriptor*)message->m_Descriptor;
            if (descriptor == dmPhysicsDDF::RequestRayCast::m_DDFDescriptor)
            {
                dmPhysicsDDF::RequestRayCast* ddf = (dmPhysicsDDF::RequestRayCast*)message->m_Data;
                dmGameObject::HInstance sender_instance = (dmGameObject::HInstance)message->m_UserData;
                uint8_t component_index;
                dmGameObject::Result go_result = dmGameObject::GetComponentIndex(sender_instance, message->m_Sender.m_Fragment, &component_index);
                if (go_result != dmGameObject::RESULT_OK)
                {
                    dmLogError("Component index could not be retrieved when handling '%s': %d.", dmPhysicsDDF::RequestRayCast::m_DDFDescriptor->m_Name, go_result);
                    context->m_Success = false;
                }
                else
                {
                    // Request ray cast
                    dmPhysics::RayCastRequest request;
                    request.m_From = ddf->m_From;
                    request.m_To = ddf->m_To;
                    request.m_IgnoredUserData = sender_instance;
                    request.m_Mask = ddf->m_Mask;
                    request.m_UserId = ((uint16_t)component_index << 8) | (ddf->m_RequestId & 0xff);
                    request.m_UserData = (void*)sender_instance;

                    dmGameObject::HCollection collection = dmGameObject::GetCollection(sender_instance);
                    // Make sure no external component has sent a ray cast message during the update of this collection
                    assert(collection == context->m_Collection);
                    World* world = context->m_World;
                    if (context->m_PhysicsContext->m_3D)
                    {
                        dmPhysics::RequestRayCast3D(world->m_World3D, request);
                    }
                    else
                    {
                        dmPhysics::RequestRayCast2D(world->m_World2D, request);
                    }
                }
            }
        }
    }

    dmGameObject::UpdateResult CompCollisionObjectUpdate(const dmGameObject::ComponentsUpdateParams& params)
    {
        if (params.m_World == 0x0)
            return dmGameObject::UPDATE_RESULT_OK;
        PhysicsContext* physics_context = (PhysicsContext*)params.m_Context;

        dmGameObject::UpdateResult result = dmGameObject::UPDATE_RESULT_OK;
        World* world = (World*)params.m_World;

        // Dispatch messages
        DispatchContext dispatch_context;
        dispatch_context.m_PhysicsContext = physics_context;
        dispatch_context.m_Success = true;
        dispatch_context.m_World = world;
        dispatch_context.m_Collection = params.m_Collection;
        dmMessage::HSocket physics_socket = 0;
        if (physics_context->m_3D)
        {
            physics_socket = dmPhysics::GetSocket3D(physics_context->m_Context3D);
        }
        else
        {
            physics_socket = dmPhysics::GetSocket2D(physics_context->m_Context2D);
        }
        dmMessage::Dispatch(physics_socket, DispatchCallback, (void*)&dispatch_context);
        if (!dispatch_context.m_Success)
        {
            result = dmGameObject::UPDATE_RESULT_UNKNOWN_ERROR;
        }

        CollisionUserData collision_user_data;
        collision_user_data.m_World = world;
        collision_user_data.m_Count = 0;
        CollisionUserData contact_user_data;
        contact_user_data.m_World = world;
        contact_user_data.m_Count = 0;

        dmPhysics::StepWorldContext step_world_context;
        step_world_context.m_DT = params.m_UpdateContext->m_DT;
        step_world_context.m_CollisionCallback = CollisionCallback;
        step_world_context.m_CollisionUserData = &collision_user_data;
        step_world_context.m_ContactPointCallback = ContactPointCallback;
        step_world_context.m_ContactPointUserData = &contact_user_data;
        step_world_context.m_RayCastCallback = RayCastCallback;
        step_world_context.m_RayCastUserData = world;

        if (physics_context->m_3D)
        {
            dmPhysics::StepWorld3D(world->m_World3D, step_world_context);
        }
        else
        {
            dmPhysics::StepWorld2D(world->m_World2D, step_world_context);
        }
        if (collision_user_data.m_Count >= 128)
        {
            if (!g_CollisionOverflowWarning)
            {
                dmLogWarning("Maximum number of collisions (%d) reached, messages have been lost.", MAX_COLLISION_COUNT);
                g_CollisionOverflowWarning = true;
            }
        }
        else
        {
            g_CollisionOverflowWarning = false;
        }
        if (contact_user_data.m_Count >= 128)
        {
            if (!g_ContactOverflowWarning)
            {
                dmLogWarning("Maximum number of contacts (%d) reached, messages have been lost.", MAX_CONTACT_COUNT);
                g_ContactOverflowWarning = true;
            }
        }
        else
        {
            g_ContactOverflowWarning = false;
        }
        if (physics_context->m_Debug)
        {
            if (physics_context->m_3D)
                dmPhysics::DrawDebug3D(world->m_World3D);
            else
                dmPhysics::DrawDebug2D(world->m_World2D);
        }
        return result;
    }

    dmGameObject::UpdateResult CompCollisionObjectOnMessage(const dmGameObject::ComponentOnMessageParams& params)
    {
        PhysicsContext* physics_context = (PhysicsContext*)params.m_Context;
        Component* component = (Component*) *params.m_UserData;

        if (params.m_Message->m_Id == dmHashString64(dmGameObjectDDF::Enable::m_DDFDescriptor->m_Name)
                || params.m_Message->m_Id == dmHashString64(dmGameObjectDDF::Disable::m_DDFDescriptor->m_Name))
        {
            bool enable = false;
            if (params.m_Message->m_Id == dmHashString64(dmGameObjectDDF::Enable::m_DDFDescriptor->m_Name))
            {
                enable = true;
            }
            World* world = (World*)params.m_World;
           if (physics_context->m_3D)
            {
                dmPhysics::SetEnabled3D(world->m_World3D, component->m_Object3D, enable);
            }
            else
            {
                dmPhysics::SetEnabled2D(world->m_World2D, component->m_Object2D, enable);
            }
        }
        else if (params.m_Message->m_Id == dmHashString64(dmPhysicsDDF::ApplyForce::m_DDFDescriptor->m_Name))
        {
            dmPhysicsDDF::ApplyForce* af = (dmPhysicsDDF::ApplyForce*) params.m_Message->m_Data;
            if (physics_context->m_3D)
            {
                dmPhysics::ApplyForce3D(physics_context->m_Context3D, component->m_Object3D, af->m_Force, af->m_Position);
            }
            else
            {
                dmPhysics::ApplyForce2D(physics_context->m_Context2D, component->m_Object2D, af->m_Force, af->m_Position);
            }
        }
        else if (params.m_Message->m_Id == dmHashString64(dmPhysicsDDF::RequestVelocity::m_DDFDescriptor->m_Name))
        {
            dmPhysicsDDF::VelocityResponse response;
            if (physics_context->m_3D)
            {
                response.m_LinearVelocity = dmPhysics::GetLinearVelocity3D(physics_context->m_Context3D, component->m_Object3D);
                response.m_AngularVelocity = dmPhysics::GetAngularVelocity3D(physics_context->m_Context3D, component->m_Object3D);
            }
            else
            {
                response.m_LinearVelocity = dmPhysics::GetLinearVelocity2D(physics_context->m_Context2D, component->m_Object2D);
                response.m_AngularVelocity = dmPhysics::GetAngularVelocity2D(physics_context->m_Context2D, component->m_Object2D);
            }
            dmhash_t message_id = dmHashString64(dmPhysicsDDF::VelocityResponse::m_DDFDescriptor->m_Name);
            uintptr_t descriptor = (uintptr_t)dmPhysicsDDF::VelocityResponse::m_DDFDescriptor;
            uint32_t data_size = sizeof(dmPhysicsDDF::VelocityResponse);
            dmMessage::Result result = dmMessage::Post(&params.m_Message->m_Receiver, &params.m_Message->m_Sender, message_id, 0, descriptor, &response, data_size);
            if (result != dmMessage::RESULT_OK)
            {
                dmLogError("Could not send %s to component, result: %d.", dmPhysicsDDF::VelocityResponse::m_DDFDescriptor->m_Name, result);
                return dmGameObject::UPDATE_RESULT_UNKNOWN_ERROR;
            }
        }
        else if (params.m_Message->m_Id == dmHashString64(dmPhysicsDDF::SetGridShapeHull::m_DDFDescriptor->m_Name))
        {
            if (physics_context->m_3D)
            {
                dmLogError("Grid shape hulls can only be set for 2D physics.");
                return dmGameObject::UPDATE_RESULT_UNKNOWN_ERROR;
            }
            if (component->m_Resource->m_TileGrid == 0)
            {
                dmLogError("Hulls can only be set for collision objects with tile grids as shape.");
                return dmGameObject::UPDATE_RESULT_UNKNOWN_ERROR;
            }
            dmPhysicsDDF::SetGridShapeHull* ddf = (dmPhysicsDDF::SetGridShapeHull*) params.m_Message->m_Data;
            uint32_t column = ddf->m_Column;
            uint32_t row = ddf->m_Row;
            uint32_t hull = ddf->m_Hull;
            dmPhysics::SetGridShapeHull(component->m_Object2D, component->m_Resource->m_TileGridResource->m_GridShape, row, column, hull);
        }
        return dmGameObject::UPDATE_RESULT_OK;
    }

    void CompCollisionObjectOnReload(const dmGameObject::ComponentOnReloadParams& params)
    {
        PhysicsContext* physics_context = (PhysicsContext*)params.m_Context;
        World* world = (World*)params.m_World;
        Component* component = (Component*)*params.m_UserData;
        component->m_Resource = (CollisionObjectResource*)params.m_Resource;
        if (!CreateCollisionObject(physics_context, world, params.m_Instance, component))
        {
            dmLogError("%s", "Could not recreate collision object component, not reloaded.");
        }
    }

    uint16_t CompCollisionGetGroupBitIndex(void* world, uint64_t group_hash)
    {
        return GetGroupBitIndex((World*)world, group_hash);
    }
}
