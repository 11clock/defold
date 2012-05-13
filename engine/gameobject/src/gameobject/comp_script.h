#ifndef DM_GAMEOBJECT_COMP_SCRIPT_H
#define DM_GAMEOBJECT_COMP_SCRIPT_H

#include <stdint.h>

#include "gameobject.h"

namespace dmGameObject
{
    CreateResult CompScriptNewWorld(const ComponentNewWorldParams& params);

    CreateResult CompScriptDeleteWorld(const ComponentDeleteWorldParams& params);

    CreateResult CompScriptCreate(const ComponentCreateParams& params);

    CreateResult CompScriptDestroy(const ComponentDestroyParams& params);

    CreateResult CompScriptInit(const ComponentInitParams& params);

    CreateResult CompScriptFinal(const ComponentFinalParams& params);

    UpdateResult CompScriptUpdate(const ComponentsUpdateParams& params);

    UpdateResult CompScriptOnMessage(const ComponentOnMessageParams& params);

    InputResult CompScriptOnInput(const ComponentOnInputParams& params);

    void CompScriptOnReload(const ComponentOnReloadParams& params);

    void CompScriptSetProperties(const ComponentSetPropertiesParams& params);
}

#endif // DM_GAMEOBJECT_COMP_SCRIPT_H
