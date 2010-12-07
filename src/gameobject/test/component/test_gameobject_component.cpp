#include <gtest/gtest.h>

#include <map>

#include <dlib/hash.h>

#include <resource/resource.h>

#include "../gameobject.h"

#include "gameobject/test/component/test_gameobject_component_ddf.h"

class ComponentTest : public ::testing::Test
{
protected:
    virtual void SetUp()
    {
        dmGameObject::Initialize();

        m_UpdateCount = 0;
        m_UpdateContext.m_DT = 1.0f / 60.0f;

        dmResource::NewFactoryParams params;
        params.m_MaxResources = 16;
        params.m_Flags = RESOURCE_FACTORY_FLAGS_EMPTY;
        m_Factory = dmResource::NewFactory(&params, "build/default/src/gameobject/test/component");
        m_Register = dmGameObject::NewRegister(0, 0);
        dmGameObject::RegisterResourceTypes(m_Factory, m_Register);
        dmGameObject::RegisterComponentTypes(m_Factory, m_Register);
        m_Collection = dmGameObject::NewCollection(m_Factory, m_Register, 1024);

        // Register dummy physical resource type
        dmResource::FactoryResult e;
        e = dmResource::RegisterType(m_Factory, "a", this, ACreate, ADestroy, 0);
        ASSERT_EQ(dmResource::FACTORY_RESULT_OK, e);
        e = dmResource::RegisterType(m_Factory, "b", this, BCreate, BDestroy, 0);
        ASSERT_EQ(dmResource::FACTORY_RESULT_OK, e);
        e = dmResource::RegisterType(m_Factory, "c", this, CCreate, CDestroy, 0);
        ASSERT_EQ(dmResource::FACTORY_RESULT_OK, e);

        uint32_t resource_type;
        dmGameObject::Result result;

        // A has component_user_data
        e = dmResource::GetTypeFromExtension(m_Factory, "a", &resource_type);
        ASSERT_EQ(dmResource::FACTORY_RESULT_OK, e);
        dmGameObject::ComponentType a_type;
        a_type.m_Name = "a";
        a_type.m_ResourceType = resource_type;
        a_type.m_Context = this;
        a_type.m_CreateFunction = AComponentCreate;
        a_type.m_InitFunction = AComponentInit;
        a_type.m_DestroyFunction = AComponentDestroy;
        a_type.m_UpdateFunction = AComponentsUpdate;
        a_type.m_InstanceHasUserData = true;
        result = dmGameObject::RegisterComponentType(m_Register, a_type);
        dmGameObject::SetUpdateOrderPrio(m_Register, resource_type, 2);
        ASSERT_EQ(dmGameObject::RESULT_OK, result);

        // B has *not* component_user_data
        e = dmResource::GetTypeFromExtension(m_Factory, "b", &resource_type);
        ASSERT_EQ(dmResource::FACTORY_RESULT_OK, e);
        dmGameObject::ComponentType b_type;
        b_type.m_Name = "b";
        b_type.m_ResourceType = resource_type;
        b_type.m_Context = this;
        b_type.m_CreateFunction = BComponentCreate;
        b_type.m_InitFunction = BComponentInit;
        b_type.m_DestroyFunction = BComponentDestroy;
        b_type.m_UpdateFunction = BComponentsUpdate;
        result = dmGameObject::RegisterComponentType(m_Register, b_type);
        dmGameObject::SetUpdateOrderPrio(m_Register, resource_type, 1);
        ASSERT_EQ(dmGameObject::RESULT_OK, result);

        // C has component_user_data
        e = dmResource::GetTypeFromExtension(m_Factory, "c", &resource_type);
        ASSERT_EQ(dmResource::FACTORY_RESULT_OK, e);
        dmGameObject::ComponentType c_type;
        c_type.m_Name = "c";
        c_type.m_ResourceType = resource_type;
        c_type.m_Context = this;
        c_type.m_CreateFunction = CComponentCreate;
        c_type.m_InitFunction = CComponentInit;
        c_type.m_DestroyFunction = CComponentDestroy;
        c_type.m_UpdateFunction = CComponentsUpdate;
        c_type.m_InstanceHasUserData = true;
        result = dmGameObject::RegisterComponentType(m_Register, c_type);
        dmGameObject::SetUpdateOrderPrio(m_Register, resource_type, 0);
        ASSERT_EQ(dmGameObject::RESULT_OK, result);

        m_MaxComponentCreateCountMap[TestGameObjectDDF::AResource::m_DDFHash] = 1000000;
    }

    virtual void TearDown()
    {
        dmGameObject::DeleteCollection(m_Collection);
        dmResource::DeleteFactory(m_Factory);
        dmGameObject::DeleteRegister(m_Register);
        dmGameObject::Finalize();
    }

    static dmResource::FResourceCreate    ACreate;
    static dmResource::FResourceDestroy   ADestroy;
    static dmGameObject::ComponentCreate  AComponentCreate;
    static dmGameObject::ComponentInit    AComponentInit;
    static dmGameObject::ComponentDestroy AComponentDestroy;
    static dmGameObject::ComponentsUpdate AComponentsUpdate;

    static dmResource::FResourceCreate    BCreate;
    static dmResource::FResourceDestroy   BDestroy;
    static dmGameObject::ComponentCreate  BComponentCreate;
    static dmGameObject::ComponentInit    BComponentInit;
    static dmGameObject::ComponentDestroy BComponentDestroy;
    static dmGameObject::ComponentsUpdate BComponentsUpdate;

    static dmResource::FResourceCreate    CCreate;
    static dmResource::FResourceDestroy   CDestroy;
    static dmGameObject::ComponentCreate  CComponentCreate;
    static dmGameObject::ComponentInit    CComponentInit;
    static dmGameObject::ComponentDestroy CComponentDestroy;
    static dmGameObject::ComponentsUpdate CComponentsUpdate;

public:
    uint32_t                     m_UpdateCount;
    std::map<uint64_t, uint32_t> m_CreateCountMap;
    std::map<uint64_t, uint32_t> m_DestroyCountMap;

    std::map<uint64_t, uint32_t> m_ComponentCreateCountMap;
    std::map<uint64_t, uint32_t> m_ComponentInitCountMap;
    std::map<uint64_t, uint32_t> m_ComponentDestroyCountMap;
    std::map<uint64_t, uint32_t> m_ComponentUpdateCountMap;
    std::map<uint64_t, uint32_t> m_MaxComponentCreateCountMap;

    std::map<uint64_t, uint32_t> m_ComponentUpdateOrderMap;

    std::map<uint64_t, int>      m_ComponentUserDataAcc;

    dmGameObject::UpdateContext m_UpdateContext;
    dmGameObject::HRegister m_Register;
    dmGameObject::HCollection m_Collection;
    dmResource::HFactory m_Factory;
};

template <typename T>
dmResource::CreateResult GenericDDFCreate(dmResource::HFactory factory, void* context, const void* buffer, uint32_t buffer_size, dmResource::SResourceDescriptor* resource, const char* filename)
{
    ComponentTest* game_object_test = (ComponentTest*) context;
    game_object_test->m_CreateCountMap[T::m_DDFHash]++;

    T* obj;
    dmDDF::Result e = dmDDF::LoadMessage<T>(buffer, buffer_size, &obj);
    if (e == dmDDF::RESULT_OK)
    {
        resource->m_Resource = (void*) obj;
        return dmResource::CREATE_RESULT_OK;
    }
    else
    {
        return dmResource::CREATE_RESULT_UNKNOWN;
    }
}

template <typename T>
dmResource::CreateResult GenericDDFDestory(dmResource::HFactory factory, void* context, dmResource::SResourceDescriptor* resource)
{
    ComponentTest* game_object_test = (ComponentTest*) context;
    game_object_test->m_DestroyCountMap[T::m_DDFHash]++;

    dmDDF::FreeMessage((void*) resource->m_Resource);
    return dmResource::CREATE_RESULT_OK;
}

template <typename T, int add_to_user_data>
static dmGameObject::CreateResult GenericComponentCreate(dmGameObject::HCollection collection,
                                                         dmGameObject::HInstance instance,
                                                         void* resource,
                                                         void* world,
                                                         void* context,
                                                         uintptr_t* user_data)
{
    ComponentTest* game_object_test = (ComponentTest*) context;

    if (user_data && add_to_user_data != -1)
    {
        *user_data += add_to_user_data;
    }

    if (game_object_test->m_MaxComponentCreateCountMap.find(T::m_DDFHash) != game_object_test->m_MaxComponentCreateCountMap.end())
    {
        if (game_object_test->m_ComponentCreateCountMap[T::m_DDFHash] >= game_object_test->m_MaxComponentCreateCountMap[T::m_DDFHash])
        {
            return dmGameObject::CREATE_RESULT_UNKNOWN_ERROR;
        }
    }

    game_object_test->m_ComponentCreateCountMap[T::m_DDFHash]++;
    return dmGameObject::CREATE_RESULT_OK;
}

template <typename T>
static dmGameObject::CreateResult GenericComponentInit(dmGameObject::HCollection collection,
                                                        dmGameObject::HInstance instance,
                                                        void* context,
                                                        uintptr_t* user_data)
{
    ComponentTest* game_object_test = (ComponentTest*) context;
    game_object_test->m_ComponentInitCountMap[T::m_DDFHash]++;
    return dmGameObject::CREATE_RESULT_OK;
}

template <typename T>
static dmGameObject::UpdateResult GenericComponentsUpdate(dmGameObject::HCollection collection,
                                    const dmGameObject::UpdateContext* update_context,
                                    void* world,
                                    void* context)
{
    ComponentTest* game_object_test = (ComponentTest*) context;
    game_object_test->m_ComponentUpdateCountMap[T::m_DDFHash]++;
    game_object_test->m_ComponentUpdateOrderMap[T::m_DDFHash] = game_object_test->m_UpdateCount++;
    return dmGameObject::UPDATE_RESULT_OK;
}


template <typename T>
static dmGameObject::CreateResult GenericComponentDestroy(dmGameObject::HCollection collection,
                                                          dmGameObject::HInstance instance,
                                                          void* world,
                                                          void* context,
                                                          uintptr_t* user_data)
{
    ComponentTest* game_object_test = (ComponentTest*) context;
    if (user_data)
    {
        game_object_test->m_ComponentUserDataAcc[T::m_DDFHash] += *user_data;
    }

    game_object_test->m_ComponentDestroyCountMap[T::m_DDFHash]++;
    return dmGameObject::CREATE_RESULT_OK;
}

dmResource::FResourceCreate ComponentTest::ACreate              = GenericDDFCreate<TestGameObjectDDF::AResource>;
dmResource::FResourceDestroy ComponentTest::ADestroy            = GenericDDFDestory<TestGameObjectDDF::AResource>;
dmGameObject::ComponentCreate ComponentTest::AComponentCreate   = GenericComponentCreate<TestGameObjectDDF::AResource, 1>;
dmGameObject::ComponentInit ComponentTest::AComponentInit       = GenericComponentInit<TestGameObjectDDF::AResource>;
dmGameObject::ComponentDestroy ComponentTest::AComponentDestroy = GenericComponentDestroy<TestGameObjectDDF::AResource>;
dmGameObject::ComponentsUpdate ComponentTest::AComponentsUpdate = GenericComponentsUpdate<TestGameObjectDDF::AResource>;

dmResource::FResourceCreate ComponentTest::BCreate              = GenericDDFCreate<TestGameObjectDDF::BResource>;
dmResource::FResourceDestroy ComponentTest::BDestroy            = GenericDDFDestory<TestGameObjectDDF::BResource>;
dmGameObject::ComponentCreate ComponentTest::BComponentCreate   = GenericComponentCreate<TestGameObjectDDF::BResource, -1>;
dmGameObject::ComponentInit ComponentTest::BComponentInit       = GenericComponentInit<TestGameObjectDDF::BResource>;
dmGameObject::ComponentDestroy ComponentTest::BComponentDestroy = GenericComponentDestroy<TestGameObjectDDF::BResource>;
dmGameObject::ComponentsUpdate ComponentTest::BComponentsUpdate = GenericComponentsUpdate<TestGameObjectDDF::BResource>;

dmResource::FResourceCreate ComponentTest::CCreate              = GenericDDFCreate<TestGameObjectDDF::CResource>;
dmResource::FResourceDestroy ComponentTest::CDestroy            = GenericDDFDestory<TestGameObjectDDF::CResource>;
dmGameObject::ComponentCreate ComponentTest::CComponentCreate   = GenericComponentCreate<TestGameObjectDDF::CResource, 10>;
dmGameObject::ComponentInit ComponentTest::CComponentInit       = GenericComponentInit<TestGameObjectDDF::CResource>;
dmGameObject::ComponentDestroy ComponentTest::CComponentDestroy = GenericComponentDestroy<TestGameObjectDDF::CResource>;
dmGameObject::ComponentsUpdate ComponentTest::CComponentsUpdate = GenericComponentsUpdate<TestGameObjectDDF::CResource>;

TEST_F(ComponentTest, TestUpdate)
{
    dmGameObject::HInstance go = dmGameObject::New(m_Collection, "go1.goc");
    ASSERT_NE((void*) 0, (void*) go);
    bool ret = dmGameObject::Update(&m_Collection, &m_UpdateContext, 1);
    ASSERT_TRUE(ret);
    ret = dmGameObject::PostUpdate(&m_Collection, 1);
    ASSERT_TRUE(ret);
    ASSERT_EQ((uint32_t) 1, m_ComponentUpdateCountMap[TestGameObjectDDF::AResource::m_DDFHash]);

    dmGameObject::Delete(m_Collection, go);
    ret = dmGameObject::PostUpdate(&m_Collection, 1);
    ASSERT_TRUE(ret);
    ASSERT_EQ((uint32_t) 1, m_CreateCountMap[TestGameObjectDDF::AResource::m_DDFHash]);
    ASSERT_EQ((uint32_t) 1, m_DestroyCountMap[TestGameObjectDDF::AResource::m_DDFHash]);
    ASSERT_EQ((uint32_t) 1, m_ComponentCreateCountMap[TestGameObjectDDF::AResource::m_DDFHash]);
    ASSERT_EQ((uint32_t) 1, m_ComponentDestroyCountMap[TestGameObjectDDF::AResource::m_DDFHash]);
}

TEST_F(ComponentTest, TestPostDeleteUpdate)
{
    dmGameObject::HInstance go = dmGameObject::New(m_Collection, "go1.goc");
    ASSERT_NE((void*) 0, (void*) go);

    uint32_t message_id = dmHashString32("test");

    dmGameObject::InstanceMessageData data;
    data.m_MessageId = message_id;
    data.m_Component = 0xff;
    data.m_Instance = go;
    data.m_DDFDescriptor = 0x0;
    dmMessage::Post(dmGameObject::GetReplyMessageSocket(m_Register), message_id, (void*)&data, sizeof(dmGameObject::InstanceMessageData));

    dmGameObject::Delete(m_Collection, go);

    bool ret = dmGameObject::Update(&m_Collection, &m_UpdateContext, 1);
    ASSERT_TRUE(ret);

    ASSERT_TRUE(dmGameObject::PostUpdate(&m_Collection, 1));
}

TEST_F(ComponentTest, TestNonexistingComponent)
{
    dmGameObject::HInstance go = dmGameObject::New(m_Collection, "go2.goc");
    ASSERT_EQ((void*) 0, (void*) go);
    ASSERT_EQ((uint32_t) 0, m_CreateCountMap[TestGameObjectDDF::AResource::m_DDFHash]);
    ASSERT_EQ((uint32_t) 0, m_DestroyCountMap[TestGameObjectDDF::AResource::m_DDFHash]);

    ASSERT_EQ((uint32_t) 0, m_ComponentCreateCountMap[TestGameObjectDDF::AResource::m_DDFHash]);
    ASSERT_EQ((uint32_t) 0, m_ComponentDestroyCountMap[TestGameObjectDDF::AResource::m_DDFHash]);
}

TEST_F(ComponentTest, TestPartialNonexistingComponent1)
{
    dmGameObject::HInstance go = dmGameObject::New(m_Collection, "go3.goc");
    ASSERT_EQ((void*) 0, (void*) go);

    // First one exists
    ASSERT_EQ((uint32_t) 1, m_CreateCountMap[TestGameObjectDDF::AResource::m_DDFHash]);
    ASSERT_EQ((uint32_t) 1, m_DestroyCountMap[TestGameObjectDDF::AResource::m_DDFHash]);
    // Even though the first a-component exits the prototype creation should fail before creating components
    ASSERT_EQ((uint32_t) 0, m_ComponentCreateCountMap[TestGameObjectDDF::AResource::m_DDFHash]);
    ASSERT_EQ((uint32_t) 0, m_ComponentDestroyCountMap[TestGameObjectDDF::AResource::m_DDFHash]);
}

TEST_F(ComponentTest, TestPartialFailingComponent)
{
    // Only succeed creating the first component
    m_MaxComponentCreateCountMap[TestGameObjectDDF::AResource::m_DDFHash] = 1;
    dmGameObject::HInstance go = dmGameObject::New(m_Collection, "go4.goc");
    ASSERT_EQ((void*) 0, (void*) go);

    ASSERT_EQ((uint32_t) 1, m_CreateCountMap[TestGameObjectDDF::AResource::m_DDFHash]);
    ASSERT_EQ((uint32_t) 1, m_DestroyCountMap[TestGameObjectDDF::AResource::m_DDFHash]);

    // One component should get created
    ASSERT_EQ((uint32_t) 1, m_ComponentCreateCountMap[TestGameObjectDDF::AResource::m_DDFHash]);
    ASSERT_EQ((uint32_t) 1, m_ComponentDestroyCountMap[TestGameObjectDDF::AResource::m_DDFHash]);
}

TEST_F(ComponentTest, TestComponentUserdata)
{
    dmGameObject::HInstance go = dmGameObject::New(m_Collection, "go5.goc");
    ASSERT_NE((void*) 0, (void*) go);

    dmGameObject::Delete(m_Collection, go);
    bool ret = dmGameObject::PostUpdate(&m_Collection, 1);
    ASSERT_TRUE(ret);
    // Two a:s
    ASSERT_EQ(2, m_ComponentUserDataAcc[TestGameObjectDDF::AResource::m_DDFHash]);
    // Zero c:s
    ASSERT_EQ(0, m_ComponentUserDataAcc[TestGameObjectDDF::BResource::m_DDFHash]);
    // Three c:s
    ASSERT_EQ(30, m_ComponentUserDataAcc[TestGameObjectDDF::CResource::m_DDFHash]);
}

TEST_F(ComponentTest, TestUpdateOrder)
{
    dmGameObject::HInstance go = dmGameObject::New(m_Collection, "go1.goc");
    ASSERT_NE((void*) 0, (void*) go);
    bool ret = dmGameObject::Update(&m_Collection, &m_UpdateContext, 1);
    ASSERT_TRUE(ret);
    ASSERT_EQ((uint32_t) 2, m_ComponentUpdateOrderMap[TestGameObjectDDF::AResource::m_DDFHash]);
    ASSERT_EQ((uint32_t) 1, m_ComponentUpdateOrderMap[TestGameObjectDDF::BResource::m_DDFHash]);
    ASSERT_EQ((uint32_t) 0, m_ComponentUpdateOrderMap[TestGameObjectDDF::CResource::m_DDFHash]);
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);

    int ret = RUN_ALL_TESTS();
    return ret;
}
