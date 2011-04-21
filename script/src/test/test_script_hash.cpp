#include <gtest/gtest.h>

#include "script.h"
#include "script_hash.h"

#include <dlib/dstrings.h>
#include <dlib/hash.h>
#include <dlib/log.h>

extern "C"
{
#include <lua/lauxlib.h>
#include <lua/lualib.h>
}

#define PATH_FORMAT "build/default/src/test/%s"

class ScriptHashTest : public ::testing::Test
{
protected:
    virtual void SetUp()
    {
        L = lua_open();

        luaopen_base(L);
        luaopen_table(L);
        luaopen_string(L);
        luaopen_math(L);
        luaopen_debug(L);

        dmScript::Initialize(L, dmScript::ScriptParams());
    }

    virtual void TearDown()
    {
        dmScript::Finalize(L);
        lua_close(L);
    }

    lua_State* L;
};

bool RunFile(lua_State* L, const char* filename)
{
    char path[64];
    DM_SNPRINTF(path, 64, PATH_FORMAT, filename);
    if (luaL_dofile(L, path) != 0)
    {
        dmLogError("%s", lua_tolstring(L, -1, 0));
        return false;
    }
    return true;
}

bool RunString(lua_State* L, const char* script)
{
    if (luaL_dostring(L, script) != 0)
    {
        dmLogError("%s", lua_tolstring(L, -1, 0));
        return false;
    }
    return true;
}

TEST_F(ScriptHashTest, TestHash)
{
    int top = lua_gettop(L);

    const char* s = "test_value";
    dmhash_t hash = dmHashString64(s);
    dmScript::PushHash(L, hash);
    ASSERT_EQ(hash, dmScript::CheckHash(L, -1));
    lua_pop(L, 1);

    ASSERT_EQ(top, lua_gettop(L));

    ASSERT_TRUE(RunFile(L, "test_hash.luac"));

    lua_getglobal(L, "functions");
    ASSERT_EQ(LUA_TTABLE, lua_type(L, -1));
    lua_getfield(L, -1, "test_hash");
    ASSERT_EQ(LUA_TFUNCTION, lua_type(L, -1));
    dmScript::PushHash(L, hash);
    int result = lua_pcall(L, 1, LUA_MULTRET, 0);
    if (result == LUA_ERRRUN)
    {
        dmLogError("Error running script: %s", lua_tostring(L,-1));
        ASSERT_TRUE(false);
        lua_pop(L, 1);
    }
    else
    {
        ASSERT_EQ(0, result);
    }
    lua_pop(L, 1);

    dmScript::PushHash(L, dmHashString64("test"));
    ASSERT_TRUE(dmScript::IsHash(L, -1));
    lua_pop(L, 1);
    ASSERT_FALSE(dmScript::IsHash(L, -1));

    ASSERT_EQ(top, lua_gettop(L));
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);

    int ret = RUN_ALL_TESTS();
    return ret;
}
