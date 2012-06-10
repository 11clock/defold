#include <gtest/gtest.h>

#include <resource/resource.h>

#include <hid/hid.h>

#include <sound/sound.h>
#include <gameobject/gameobject.h>

#include "gamesys/gamesys.h"

struct Params
{
    const char* m_ValidResource;
    const char* m_InvalidResource;
    const char* m_TempResource;
};

template<typename T>
class GamesysTest : public ::testing::TestWithParam<T>
{
protected:
    virtual void SetUp();
    virtual void TearDown();

    dmGameObject::UpdateContext m_UpdateContext;
    dmGameObject::HRegister m_Register;
    dmGameObject::HCollection m_Collection;
    dmResource::HFactory m_Factory;

    dmScript::HContext m_ScriptContext;
    dmGraphics::HContext m_GraphicsContext;
    dmRender::HRenderContext m_RenderContext;
    dmGameSystem::PhysicsContext m_PhysicsContext;
    dmGameSystem::EmitterContext m_EmitterContext;
    dmGameSystem::GuiContext m_GuiContext;
    dmHID::HContext m_HidContext;
    dmInput::HContext m_InputContext;
    dmInputDDF::GamepadMaps* m_GamepadMapsDDF;
    dmGameSystem::SpriteContext m_SpriteContext;
    dmGameSystem::CollectionProxyContext m_CollectionProxyContext;
};

class ResourceTest : public GamesysTest<const char*>
{
public:
    virtual ~ResourceTest() {}
};

struct ResourceFailParams
{
    const char* m_ValidResource;
    const char* m_InvalidResource;
};

class ResourceFailTest : public GamesysTest<ResourceFailParams>
{
public:
    virtual ~ResourceFailTest() {}
};

class ComponentTest : public GamesysTest<const char*>
{
public:
    virtual ~ComponentTest() {}
};

class ComponentFailTest : public GamesysTest<const char*>
{
public:
    virtual ~ComponentFailTest() {}
};

bool CopyResource(const char* src, const char* dst);
bool UnlinkResource(const char* name);

template<typename T>
void GamesysTest<T>::SetUp()
{
    dmSound::Initialize(0x0, 0x0);
    m_ScriptContext = dmScript::NewContext(0);
    dmGameObject::Initialize(m_ScriptContext);

    m_UpdateContext.m_DT = 1.0f / 60.0f;

    dmResource::NewFactoryParams params;
    params.m_MaxResources = 16;
    params.m_Flags = RESOURCE_FACTORY_FLAGS_RELOAD_SUPPORT;
    m_Factory = dmResource::NewFactory(&params, "build/default/src/gamesys/test");
    m_Register = dmGameObject::NewRegister();
    dmGameObject::RegisterResourceTypes(m_Factory, m_Register);
    dmGameObject::RegisterComponentTypes(m_Factory, m_Register);

    m_GraphicsContext = dmGraphics::NewContext(dmGraphics::ContextParams());
    dmRender::RenderContextParams render_params;
    render_params.m_MaxRenderTypes = 10;
    render_params.m_MaxInstances = 1000;
    render_params.m_MaxRenderTargets = 10;
    m_RenderContext = dmRender::NewRenderContext(m_GraphicsContext, render_params);
    m_GuiContext.m_RenderContext = m_RenderContext;
    dmGui::NewContextParams gui_params;
    gui_params.m_ScriptContext = m_ScriptContext;
    gui_params.m_GetURLCallback = dmGameSystem::GuiGetURLCallback;
    gui_params.m_GetUserDataCallback = dmGameSystem::GuiGetUserDataCallback;
    gui_params.m_ResolvePathCallback = dmGameSystem::GuiResolvePathCallback;
    m_GuiContext.m_GuiContext = dmGui::NewContext(&gui_params);

    m_HidContext = dmHID::NewContext(dmHID::NewContextParams());
    dmHID::Init(m_HidContext);
    dmInput::NewContextParams input_params;
    input_params.m_HidContext = m_HidContext;
    input_params.m_RepeatDelay = 0.3f;
    input_params.m_RepeatInterval = 0.1f;
    m_InputContext = dmInput::NewContext(input_params);

    memset(&m_PhysicsContext, 0, sizeof(m_PhysicsContext));
    m_PhysicsContext.m_3D = false;
    m_PhysicsContext.m_Context2D = dmPhysics::NewContext2D(dmPhysics::NewContextParams());

    memset(&m_EmitterContext, 0, sizeof(m_EmitterContext));
    m_EmitterContext.m_RenderContext = m_RenderContext;

    m_SpriteContext.m_RenderContext = m_RenderContext;
    m_SpriteContext.m_MaxSpriteCount = 32;

    m_CollectionProxyContext.m_Factory = m_Factory;
    m_CollectionProxyContext.m_MaxCollectionProxyCount = 8;

    assert(dmResource::RESULT_OK == dmGameSystem::RegisterResourceTypes(m_Factory, m_RenderContext, &m_GuiContext, m_InputContext, &m_PhysicsContext));

    dmResource::Get(m_Factory, "/input/valid.gamepadsc", (void**)&m_GamepadMapsDDF);
    assert(m_GamepadMapsDDF);
    dmInput::RegisterGamepads(m_InputContext, m_GamepadMapsDDF);

    assert(dmGameObject::RESULT_OK == dmGameSystem::RegisterComponentTypes(m_Factory, m_Register, m_RenderContext, &m_PhysicsContext, &m_EmitterContext, &m_GuiContext, &m_SpriteContext, &m_CollectionProxyContext));

    m_Collection = dmGameObject::NewCollection("collection", m_Factory, m_Register, 1024);
}

template<typename T>
void GamesysTest<T>::TearDown()
{
    dmGameObject::DeleteCollection(m_Collection);
    dmResource::Release(m_Factory, m_GamepadMapsDDF);
    dmGui::DeleteContext(m_GuiContext.m_GuiContext);
    dmRender::DeleteRenderContext(m_RenderContext);
    dmGraphics::DeleteContext(m_GraphicsContext);
    dmResource::DeleteFactory(m_Factory);
    dmGameObject::DeleteRegister(m_Register);
    dmGameObject::Finalize();
    dmSound::Finalize();
    dmInput::DeleteContext(m_InputContext);
    dmHID::Final(m_HidContext);
    dmHID::DeleteContext(m_HidContext);
    dmPhysics::DeleteContext2D(m_PhysicsContext.m_Context2D);
    dmScript::DeleteContext(m_ScriptContext);
}
