#ifndef DM_GAMEOBJECT_RES_LUA_H
#define DM_GAMEOBJECT_RES_LUA_H

#include <stdint.h>

#include <resource/resource.h>
#include "../proto/lua_ddf.h"

namespace dmGameObject
{
    struct LuaScript
    {
        LuaScript(dmLuaDDF::LuaModule* lua_module) :
            m_LuaModule(lua_module), m_NameHash(0), m_ModuleHash(0) {}

        dmLuaDDF::LuaModule* m_LuaModule;
        uint64_t             m_NameHash;
        uint64_t             m_ModuleHash;
    };

    dmResource::Result ResLuaCreate(dmResource::HFactory factory,
                                    void* context,
                                    const void* buffer, uint32_t buffer_size,
                                    dmResource::SResourceDescriptor* resource,
                                    const char* filename);

    dmResource::Result ResLuaDestroy(dmResource::HFactory factory,
                                     void* context,
                                     dmResource::SResourceDescriptor* resource);

    dmResource::Result ResLuaRecreate(dmResource::HFactory factory,
                                      void* context,
                                      const void* buffer, uint32_t buffer_size,
                                      dmResource::SResourceDescriptor* resource,
                                      const char* filename);
}

#endif // DM_GAMEOBJECT_RES_LUA_H
