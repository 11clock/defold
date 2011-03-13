#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <dlib/hash.h>
#include <dlib/message.h>
#include <dlib/dstrings.h>
#include <dlib/time.h>
#include <dlib/log.h>
#include <resource/resource.h>
#include "../gameobject.h"
#include "../gameobject_private.h"
#include "../proto/gameobject_ddf.h"

using namespace Vectormath::Aos;

class HierarchyTest : public ::testing::Test
{
protected:
    virtual void SetUp()
    {
        dmGameObject::Initialize();

        m_UpdateContext.m_DT = 1.0f / 60.0f;

        dmResource::NewFactoryParams params;
        params.m_MaxResources = 16;
        params.m_Flags = RESOURCE_FACTORY_FLAGS_EMPTY;
        m_Factory = dmResource::NewFactory(&params, "build/default/src/gameobject/test/hierarchy");
        m_Register = dmGameObject::NewRegister(0, 0);
        dmGameObject::RegisterResourceTypes(m_Factory, m_Register);
        dmGameObject::RegisterComponentTypes(m_Factory, m_Register);
        m_Collection = dmGameObject::NewCollection(m_Factory, m_Register, 1024);
    }

    virtual void TearDown()
    {
        dmGameObject::DeleteCollection(m_Collection);
        dmResource::DeleteFactory(m_Factory);
        dmGameObject::DeleteRegister(m_Register);
        dmGameObject::Finalize();
    }

public:

    dmGameObject::UpdateContext m_UpdateContext;
    dmGameObject::HRegister m_Register;
    dmGameObject::HCollection m_Collection;
    dmResource::HFactory m_Factory;
};

TEST_F(HierarchyTest, TestHierarchy1)
{
    for (int i = 0; i < 2; ++i)
    {
        dmGameObject::HInstance parent = dmGameObject::New(m_Collection, "go.goc");
        dmGameObject::HInstance child = dmGameObject::New(m_Collection, "go.goc");

        const float parent_rot = 3.14159265f / 4.0f;

        Point3 parent_pos(1, 2, 0);
        Point3 child_pos(4, 5, 0);

        Matrix4 parent_m = Matrix4::rotationZ(parent_rot);
        parent_m.setCol3(Vector4(parent_pos));

        Matrix4 child_m = Matrix4::identity();
        child_m.setCol3(Vector4(child_pos));

        dmGameObject::SetPosition(parent, parent_pos);
        dmGameObject::SetRotation(parent, Quat::rotationZ(parent_rot));
        dmGameObject::SetPosition(child, child_pos);

        ASSERT_EQ(0U, dmGameObject::GetDepth(child));
        ASSERT_EQ(0U, dmGameObject::GetDepth(parent));

        dmGameObject::SetParent(child, parent);

        ASSERT_EQ(0U, dmGameObject::GetChildCount(child));
        ASSERT_EQ(1U, dmGameObject::GetChildCount(parent));

        ASSERT_EQ(1U, dmGameObject::GetDepth(child));
        ASSERT_EQ(0U, dmGameObject::GetDepth(parent));

        bool ret;
        ret = dmGameObject::Update(m_Collection, 0);
        ASSERT_TRUE(ret);
        ret = dmGameObject::PostUpdate(m_Collection);
        ASSERT_TRUE(ret);

        Point3 expected_child_pos = Point3((parent_m * child_pos).getXYZ());

        ASSERT_NEAR(0.0f, length(dmGameObject::GetWorldPosition(parent) - parent_pos), 0.001f);
        ASSERT_NEAR(0.0f, length(dmGameObject::GetWorldPosition(child) - expected_child_pos), 0.001f);

        if (i % 2 == 0)
        {
            dmGameObject::Delete(m_Collection, parent);
            ret = dmGameObject::PostUpdate(m_Collection);
            ASSERT_TRUE(ret);
            ASSERT_EQ(0U, dmGameObject::GetDepth(child));
            ASSERT_EQ(0U, dmGameObject::GetChildCount(child));
            dmGameObject::Delete(m_Collection, child);
            ret = dmGameObject::PostUpdate(m_Collection);
            ASSERT_TRUE(ret);
        }
        else
        {
            dmGameObject::Delete(m_Collection, child);
            ret = dmGameObject::PostUpdate(m_Collection);
            ASSERT_TRUE(ret);
            ASSERT_EQ(0U, dmGameObject::GetDepth(parent));
            ASSERT_EQ(0U, dmGameObject::GetChildCount(parent));
            dmGameObject::Delete(m_Collection, parent);
            ret = dmGameObject::PostUpdate(m_Collection);
            ASSERT_TRUE(ret);
        }
    }
}

TEST_F(HierarchyTest, TestHierarchy2)
{
    // Test transform
    dmGameObject::HInstance parent = dmGameObject::New(m_Collection, "go.goc");
    dmGameObject::HInstance child = dmGameObject::New(m_Collection, "go.goc");
    dmGameObject::HInstance child_child = dmGameObject::New(m_Collection, "go.goc");

    const float parent_rot = 3.14159265f / 4.0f;
    const float child_rot = 3.14159265f / 5.0f;

    Point3 parent_pos(1, 1, 0);
    Point3 child_pos(0, 1, 0);
    Point3 child_child_pos(7, 2, 0);

    Matrix4 parent_m = Matrix4::rotationZ(parent_rot);
    parent_m.setCol3(Vector4(parent_pos));

    Matrix4 child_m = Matrix4::rotationZ(child_rot);
    child_m.setCol3(Vector4(child_pos));

    Matrix4 child_child_m = Matrix4::identity();
    child_child_m.setCol3(Vector4(child_child_pos));

    dmGameObject::SetPosition(parent, parent_pos);
    dmGameObject::SetRotation(parent, Quat::rotationZ(parent_rot));
    dmGameObject::SetPosition(child, child_pos);
    dmGameObject::SetRotation(child, Quat::rotationZ(child_rot));
    dmGameObject::SetPosition(child_child, child_child_pos);

    dmGameObject::SetParent(child, parent);
    dmGameObject::SetParent(child_child, child);

    ASSERT_EQ(1U, dmGameObject::GetChildCount(child));
    ASSERT_EQ(1U, dmGameObject::GetChildCount(parent));

    ASSERT_EQ(0U, dmGameObject::GetDepth(parent));
    ASSERT_EQ(1U, dmGameObject::GetDepth(child));
    ASSERT_EQ(2U, dmGameObject::GetDepth(child_child));

    bool ret;
    ret = dmGameObject::Update(m_Collection, 0);
    ASSERT_TRUE(ret);

    Point3 expected_child_pos = Point3((parent_m * child_pos).getXYZ());
    Point3 expected_child_child_pos = Point3(((parent_m * child_m) * child_child_pos).getXYZ());
    Point3 expected_child_child_pos2 = Point3(((parent_m * child_m) * child_child_m).getCol3().getXYZ());

    ASSERT_NEAR(0.0f, length(dmGameObject::GetWorldPosition(parent) - parent_pos), 0.001f);
    ASSERT_NEAR(0.0f, length(dmGameObject::GetWorldPosition(child) - expected_child_pos), 0.001f);
    ASSERT_NEAR(0.0f, length(dmGameObject::GetWorldPosition(child_child) - expected_child_child_pos), 0.001f);
    ASSERT_NEAR(0.0f, length(dmGameObject::GetWorldPosition(child_child) - expected_child_child_pos2), 0.001f);

    dmGameObject::Delete(m_Collection, parent);
    dmGameObject::Delete(m_Collection, child);
    dmGameObject::Delete(m_Collection, child_child);
}

TEST_F(HierarchyTest, TestHierarchy3)
{
    // Test with siblings
    for (int i = 0; i < 3; ++i)
    {
        dmGameObject::HInstance parent = dmGameObject::New(m_Collection, "go.goc");
        dmGameObject::HInstance child1 = dmGameObject::New(m_Collection, "go.goc");
        dmGameObject::HInstance child2 = dmGameObject::New(m_Collection, "go.goc");

        ASSERT_EQ(0U, dmGameObject::GetDepth(child1));
        ASSERT_EQ(0U, dmGameObject::GetDepth(child2));
        ASSERT_EQ(0U, dmGameObject::GetDepth(parent));

        ASSERT_EQ(0U, dmGameObject::GetChildCount(child1));
        ASSERT_EQ(0U, dmGameObject::GetChildCount(child2));
        ASSERT_EQ(0U, dmGameObject::GetChildCount(parent));

        dmGameObject::SetParent(child1, parent);

        ASSERT_EQ(0U, dmGameObject::GetChildCount(child1));
        ASSERT_EQ(0U, dmGameObject::GetChildCount(child2));
        ASSERT_EQ(1U, dmGameObject::GetChildCount(parent));

        dmGameObject::SetParent(child2, parent);

        ASSERT_EQ(0U, dmGameObject::GetChildCount(child1));
        ASSERT_EQ(0U, dmGameObject::GetChildCount(child2));
        ASSERT_EQ(2U, dmGameObject::GetChildCount(parent));

        ASSERT_EQ(1U, dmGameObject::GetDepth(child1));
        ASSERT_EQ(1U, dmGameObject::GetDepth(child2));
        ASSERT_EQ(0U, dmGameObject::GetDepth(parent));

        bool ret;
        ret = dmGameObject::Update(m_Collection, 0);
        ASSERT_TRUE(ret);
        ret = dmGameObject::PostUpdate(m_Collection);
        ASSERT_TRUE(ret);

        // Test all possible delete orders in this configuration
        if (i == 0)
        {
            dmGameObject::Delete(m_Collection, parent);
            ret = dmGameObject::PostUpdate(m_Collection);
            ASSERT_TRUE(ret);

            ASSERT_EQ(0U, dmGameObject::GetDepth(child1));
            ASSERT_EQ(0U, dmGameObject::GetDepth(child2));

            dmGameObject::Delete(m_Collection, child1);
            ret = dmGameObject::PostUpdate(m_Collection);
            ASSERT_TRUE(ret);

            ASSERT_EQ(0U, dmGameObject::GetDepth(child2));

            dmGameObject::Delete(m_Collection, child2);
            ret = dmGameObject::PostUpdate(m_Collection);
            ASSERT_TRUE(ret);
        }
        else if (i == 1)
        {
            dmGameObject::Delete(m_Collection, child1);
            ret = dmGameObject::PostUpdate(m_Collection);
            ASSERT_TRUE(ret);

            ASSERT_EQ(1U, dmGameObject::GetChildCount(parent));
            ASSERT_EQ(0U, dmGameObject::GetDepth(parent));
            ASSERT_EQ(1U, dmGameObject::GetDepth(child2));

            dmGameObject::Delete(m_Collection, parent);
            ret = dmGameObject::PostUpdate(m_Collection);
            ASSERT_TRUE(ret);

            ASSERT_EQ(0U, dmGameObject::GetDepth(child2));

            dmGameObject::Delete(m_Collection, child2);
            ret = dmGameObject::PostUpdate(m_Collection);
            ASSERT_TRUE(ret);
        }
        else if (i == 2)
        {
            dmGameObject::Delete(m_Collection, child2);
            ret = dmGameObject::PostUpdate(m_Collection);
            ASSERT_TRUE(ret);

            ASSERT_EQ(1U, dmGameObject::GetChildCount(parent));
            ASSERT_EQ(0U, dmGameObject::GetDepth(parent));
            ASSERT_EQ(1U, dmGameObject::GetDepth(child1));

            dmGameObject::Delete(m_Collection, parent);
            ret = dmGameObject::PostUpdate(m_Collection);
            ASSERT_TRUE(ret);

            ASSERT_EQ(0U, dmGameObject::GetDepth(child1));

            dmGameObject::Delete(m_Collection, child1);
            ret = dmGameObject::PostUpdate(m_Collection);
            ASSERT_TRUE(ret);
        }
        else
        {
            assert(false);
        }
    }
}

TEST_F(HierarchyTest, TestHierarchy4)
{
    // Test RESULT_MAXIMUM_HIEARCHICAL_DEPTH

    dmGameObject::HInstance parent = dmGameObject::New(m_Collection, "go.goc");
    dmGameObject::HInstance child1 = dmGameObject::New(m_Collection, "go.goc");
    dmGameObject::HInstance child2 = dmGameObject::New(m_Collection, "go.goc");
    dmGameObject::HInstance child3 = dmGameObject::New(m_Collection, "go.goc");
    dmGameObject::HInstance child4 = dmGameObject::New(m_Collection, "go.goc");

    dmGameObject::Result r;

    r = dmGameObject::SetParent(child1, parent);
    ASSERT_EQ(dmGameObject::RESULT_OK, r);

    r = dmGameObject::SetParent(child2, child1);
    ASSERT_EQ(dmGameObject::RESULT_OK, r);

    r = dmGameObject::SetParent(child3, child2);
    ASSERT_EQ(dmGameObject::RESULT_OK, r);

    r = dmGameObject::SetParent(child4, child3);
    ASSERT_EQ(dmGameObject::RESULT_MAXIMUM_HIEARCHICAL_DEPTH, r);

    ASSERT_EQ(0U, dmGameObject::GetChildCount(child3));

    dmGameObject::Delete(m_Collection, parent);
    dmGameObject::Delete(m_Collection, child1);
    dmGameObject::Delete(m_Collection, child2);
    dmGameObject::Delete(m_Collection, child3);
    dmGameObject::Delete(m_Collection, child4);
}

TEST_F(HierarchyTest, TestHierarchy5)
{
    // Test parent subtree

    dmGameObject::HInstance parent = dmGameObject::New(m_Collection, "go.goc");
    dmGameObject::HInstance child1 = dmGameObject::New(m_Collection, "go.goc");
    dmGameObject::HInstance child2 = dmGameObject::New(m_Collection, "go.goc");
    dmGameObject::HInstance child3 = dmGameObject::New(m_Collection, "go.goc");

    dmGameObject::SetParent(child1, parent);
    dmGameObject::SetParent(child3, child2);

    dmGameObject::SetParent(child2, child1);

    ASSERT_EQ(parent, dmGameObject::GetParent(child1));
    ASSERT_EQ(child1, dmGameObject::GetParent(child2));
    ASSERT_EQ(child2, dmGameObject::GetParent(child3));

    dmGameObject::Delete(m_Collection, parent);
    dmGameObject::Delete(m_Collection, child1);
    dmGameObject::Delete(m_Collection, child2);
    dmGameObject::Delete(m_Collection, child3);
}

TEST_F(HierarchyTest, TestHierarchy6)
{
    // Test invalid reparent.
    // Test that the child node is not present in the upward trace from parent

    dmGameObject::HInstance parent = dmGameObject::New(m_Collection, "go.goc");
    dmGameObject::HInstance child1 = dmGameObject::New(m_Collection, "go.goc");

    // parent -> child1
    ASSERT_EQ(dmGameObject::RESULT_OK, dmGameObject::SetParent(child1, parent));

    ASSERT_EQ(parent, dmGameObject::GetParent(child1));

    // child1 -> parent
    ASSERT_EQ(dmGameObject::RESULT_INVALID_OPERATION, dmGameObject::SetParent(parent, child1));

    ASSERT_EQ(parent, dmGameObject::GetParent(child1));

    dmGameObject::Delete(m_Collection, parent);
    dmGameObject::Delete(m_Collection, child1);
}

TEST_F(HierarchyTest, TestHierarchy7)
{
    // Test remove interior node

    dmGameObject::HInstance parent = dmGameObject::New(m_Collection, "go.goc");
    dmGameObject::HInstance child1 = dmGameObject::New(m_Collection, "go.goc");
    dmGameObject::HInstance child2 = dmGameObject::New(m_Collection, "go.goc");

    dmGameObject::SetParent(child1, parent);
    dmGameObject::SetParent(child2, child1);

    ASSERT_EQ(parent, dmGameObject::GetParent(child1));
    ASSERT_EQ(child1, dmGameObject::GetParent(child2));

    dmGameObject::Delete(m_Collection, child1);
    bool ret = dmGameObject::PostUpdate(m_Collection);
    ASSERT_TRUE(ret);
    ASSERT_EQ(parent, dmGameObject::GetParent(child2));
    ASSERT_TRUE(dmGameObject::IsChildOf(child2, parent));

    dmGameObject::Delete(m_Collection, parent);
    dmGameObject::Delete(m_Collection, child2);
}

TEST_F(HierarchyTest, TestHierarchy8)
{
    /*
        A1
      B2  C2
     D3

     Rearrange tree to:

        A1
          C2
            B2
              D3
     */

    for (int i = 0; i < 2; ++i)
    {
        dmGameObject::HInstance a1 = dmGameObject::New(m_Collection, "go.goc");
        dmGameObject::HInstance b2 = dmGameObject::New(m_Collection, "go.goc");
        dmGameObject::HInstance c2 = dmGameObject::New(m_Collection, "go.goc");
        dmGameObject::HInstance d3 = dmGameObject::New(m_Collection, "go.goc");

        ASSERT_EQ(dmGameObject::RESULT_OK, dmGameObject::SetParent(d3, b2));
        ASSERT_EQ(dmGameObject::RESULT_OK, dmGameObject::SetParent(b2, a1));
        ASSERT_EQ(dmGameObject::RESULT_OK, dmGameObject::SetParent(c2, a1));

        ASSERT_EQ(a1, dmGameObject::GetParent(b2));
        ASSERT_EQ(a1, dmGameObject::GetParent(c2));
        ASSERT_EQ(b2, dmGameObject::GetParent(d3));

        bool ret;
        ret = dmGameObject::Update(m_Collection, 0);
        ASSERT_TRUE(ret);
        ret = dmGameObject::PostUpdate(m_Collection);
        ASSERT_TRUE(ret);

        ASSERT_EQ(dmGameObject::RESULT_OK, dmGameObject::SetParent(b2, c2));

        ASSERT_EQ(a1, dmGameObject::GetParent(c2));
        ASSERT_EQ(c2, dmGameObject::GetParent(b2));
        ASSERT_EQ(b2, dmGameObject::GetParent(d3));

        ASSERT_EQ(1U, dmGameObject::GetChildCount(a1));
        ASSERT_EQ(1U, dmGameObject::GetChildCount(c2));
        ASSERT_EQ(1U, dmGameObject::GetChildCount(b2));
        ASSERT_EQ(0U, dmGameObject::GetChildCount(d3));

        ASSERT_EQ(0U, dmGameObject::GetDepth(a1));
        ASSERT_EQ(1U, dmGameObject::GetDepth(c2));
        ASSERT_EQ(2U, dmGameObject::GetDepth(b2));
        ASSERT_EQ(3U, dmGameObject::GetDepth(d3));

        ASSERT_TRUE(dmGameObject::IsChildOf(c2, a1));
        ASSERT_TRUE(dmGameObject::IsChildOf(b2, c2));
        ASSERT_TRUE(dmGameObject::IsChildOf(d3, b2));

        if (i == 0)
        {
            ASSERT_EQ(0U, dmGameObject::GetDepth(a1));
            dmGameObject::Delete(m_Collection, a1);
            ret = dmGameObject::PostUpdate(m_Collection);
            ASSERT_TRUE(ret);

            ASSERT_EQ(0U, dmGameObject::GetDepth(c2));
            dmGameObject::Delete(m_Collection, c2);
            ret = dmGameObject::PostUpdate(m_Collection);
            ASSERT_TRUE(ret);

            ASSERT_EQ(0U, dmGameObject::GetDepth(b2));
            dmGameObject::Delete(m_Collection, b2);
            ret = dmGameObject::PostUpdate(m_Collection);
            ASSERT_TRUE(ret);

            ASSERT_EQ(0U, dmGameObject::GetDepth(d3));
            dmGameObject::Delete(m_Collection, d3);
            ret = dmGameObject::PostUpdate(m_Collection);
            ASSERT_TRUE(ret);
        }
        else
        {
            ASSERT_EQ(0U, dmGameObject::GetDepth(a1));
            ASSERT_EQ(3U, dmGameObject::GetDepth(d3));
            dmGameObject::Delete(m_Collection, a1);
            ret = dmGameObject::PostUpdate(m_Collection);
            ASSERT_TRUE(ret);

            ASSERT_EQ(1U, dmGameObject::GetDepth(b2));
            ASSERT_EQ(2U, dmGameObject::GetDepth(d3));
            dmGameObject::Delete(m_Collection, b2);
            ret = dmGameObject::PostUpdate(m_Collection);
            ASSERT_TRUE(ret);
            ASSERT_EQ(c2, dmGameObject::GetParent(d3));
            ASSERT_TRUE(dmGameObject::IsChildOf(d3, c2));

            ASSERT_EQ(0U, dmGameObject::GetDepth(c2));
            ASSERT_EQ(1U, dmGameObject::GetDepth(d3));
            dmGameObject::Delete(m_Collection, c2);
            ret = dmGameObject::PostUpdate(m_Collection);
            ASSERT_TRUE(ret);

            ASSERT_EQ(0U, dmGameObject::GetDepth(d3));
            dmGameObject::Delete(m_Collection, d3);
            ret = dmGameObject::PostUpdate(m_Collection);
            ASSERT_TRUE(ret);
        }
    }
}

TEST_F(HierarchyTest, TestHierarchy9)
{
    // Test unparent

    dmGameObject::HInstance parent = dmGameObject::New(m_Collection, "go.goc");
    dmGameObject::HInstance child1 = dmGameObject::New(m_Collection, "go.goc");
    dmGameObject::HInstance child2 = dmGameObject::New(m_Collection, "go.goc");

    dmGameObject::SetParent(child1, parent);
    dmGameObject::SetParent(child2, child1);

    ASSERT_EQ(parent, dmGameObject::GetParent(child1));
    ASSERT_EQ(child1, dmGameObject::GetParent(child2));

    ASSERT_EQ(1U, dmGameObject::GetDepth(child1));
    ASSERT_EQ(2U, dmGameObject::GetDepth(child2));

    dmGameObject::SetParent(child1, 0);

    ASSERT_EQ((void*)0, dmGameObject::GetParent(child1));
    ASSERT_EQ(child1, dmGameObject::GetParent(child2));

    ASSERT_EQ(0U, dmGameObject::GetDepth(child1));
    ASSERT_EQ(1U, dmGameObject::GetDepth(child2));

    dmGameObject::Delete(m_Collection, parent);
    dmGameObject::Delete(m_Collection, child1);
    dmGameObject::Delete(m_Collection, child2);
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);

    int ret = RUN_ALL_TESTS();
    return ret;
}
