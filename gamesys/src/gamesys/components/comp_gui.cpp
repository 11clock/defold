#include <string.h>

#include <dlib/array.h>
#include <dlib/hash.h>
#include <dlib/log.h>
#include <dlib/message.h>
#include <dlib/profile.h>
#include <dlib/dstrings.h>
#include <graphics/graphics.h>
#include <render/render.h>
#include <render/font_renderer.h>

#include "comp_gui.h"

#include "../resources/res_gui.h"
#include "../gamesys.h"

extern unsigned char GUI_VPC[];
extern uint32_t GUI_VPC_SIZE;

extern unsigned char GUI_FPC[];
extern uint32_t GUI_FPC_SIZE;

namespace dmGameSystem
{
    dmRender::HRenderType g_GuiRenderType = dmRender::INVALID_RENDER_TYPE_HANDLE;

    struct GuiWorld;
    struct GuiRenderNode
    {
        dmGui::HNode m_GuiNode;
        GuiWorld*   m_GuiWorld;
        GuiRenderNode(dmGui::HNode node, GuiWorld* gui_world) : m_GuiNode(node), m_GuiWorld(gui_world) {}
    };

    dmGameObject::CreateResult CompGuiNewWorld(const dmGameObject::ComponentNewWorldParams& params)
    {
        GuiContext* gui_context = (GuiContext*)params.m_Context;
        GuiWorld* gui_world = new GuiWorld();
        if (!gui_context->m_Worlds.Full())
        {
            gui_context->m_Worlds.Push(gui_world);
        }
        else
        {
            dmLogWarning("The gui world could not be stored since the buffer is full (%d). Reload will not work for the scenes in this world.", gui_context->m_Worlds.Size());
        }

        gui_world->m_Components.SetCapacity(16);

        // TODO: Everything below here should be move to the "universe" when available
        // and hence shared among all the worlds
        gui_world->m_VertexProgram = dmGraphics::NewVertexProgram(dmRender::GetGraphicsContext(gui_context->m_RenderContext), GUI_VPC, GUI_VPC_SIZE);
        gui_world->m_FragmentProgram = dmGraphics::NewFragmentProgram(dmRender::GetGraphicsContext(gui_context->m_RenderContext), GUI_FPC, GUI_FPC_SIZE);

        gui_world->m_Material = dmRender::NewMaterial(gui_context->m_RenderContext, gui_world->m_VertexProgram, gui_world->m_FragmentProgram);
        SetMaterialProgramConstantType(gui_world->m_Material, dmHashString64("view_proj"), dmRenderDDF::MaterialDesc::CONSTANT_TYPE_VIEWPROJ);
        SetMaterialProgramConstantType(gui_world->m_Material, dmHashString64("world"), dmRenderDDF::MaterialDesc::CONSTANT_TYPE_WORLD);

        dmRender::AddMaterialTag(gui_world->m_Material, dmHashString32("gui"));

        dmGraphics::VertexElement ve[] =
        {
                {"position", 0, 3, dmGraphics::TYPE_FLOAT},
                {"texcoord0", 1, 2, dmGraphics::TYPE_FLOAT},
        };

        gui_world->m_VertexDeclaration = dmGraphics::NewVertexDeclaration(dmRender::GetGraphicsContext(gui_context->m_RenderContext), ve, sizeof(ve) / sizeof(dmGraphics::VertexElement));

        float quad[] = { 0, 0, 0, 0, 1,
                         0, 1, 0, 0, 0,
                         1, 0, 0, 1, 1,
                         1, 1, 0, 1, 0 };

        gui_world->m_QuadVertexBuffer = dmGraphics::NewVertexBuffer(dmRender::GetGraphicsContext(gui_context->m_RenderContext), sizeof(quad), (void*) quad, dmGraphics::BUFFER_USAGE_STATIC_DRAW);

        uint8_t white_texture[] = { 0xff, 0xff, 0xff, 0xff,
                                    0xff, 0xff, 0xff, 0xff,
                                    0xff, 0xff, 0xff, 0xff,
                                    0xff, 0xff, 0xff, 0xff };

        dmGraphics::TextureParams tex_params;
        tex_params.m_Format = dmGraphics::TEXTURE_FORMAT_RGBA;
        tex_params.m_Data = white_texture;
        tex_params.m_DataSize = sizeof(white_texture);
        tex_params.m_Width = 2;
        tex_params.m_Height = 2;
        gui_world->m_WhiteTexture = dmGraphics::NewTexture(dmRender::GetGraphicsContext(gui_context->m_RenderContext), tex_params);

        // TODO: Configurable
        gui_world->m_GuiRenderObjects.SetCapacity(32);

        *params.m_World = gui_world;
        return dmGameObject::CREATE_RESULT_OK;
    }

    dmGameObject::CreateResult CompGuiDeleteWorld(const dmGameObject::ComponentDeleteWorldParams& params)
    {
        GuiWorld* gui_world = (GuiWorld*)params.m_World;
        GuiContext* gui_context = (GuiContext*)params.m_Context;
        for (uint32_t i = 0; i < gui_context->m_Worlds.Size(); ++i)
        {
            if (gui_world == gui_context->m_Worlds[i])
            {
                gui_context->m_Worlds.EraseSwap(i);
            }
        }
        if (0 < gui_world->m_Components.Size())
        {
            dmLogWarning("%d gui component(s) were not destroyed at gui context destruction.", gui_world->m_Components.Size());
            for (uint32_t i = 0; i < gui_world->m_Components.Size(); ++i)
            {
                delete gui_world->m_Components[i];
            }
        }
        dmRender::DeleteMaterial(gui_context->m_RenderContext, gui_world->m_Material);
        dmGraphics::DeleteVertexProgram(gui_world->m_VertexProgram);
        dmGraphics::DeleteFragmentProgram(gui_world->m_FragmentProgram);
        dmGraphics::DeleteVertexDeclaration(gui_world->m_VertexDeclaration);
        dmGraphics::DeleteVertexBuffer(gui_world->m_QuadVertexBuffer);
        dmGraphics::DeleteTexture(gui_world->m_WhiteTexture);

        delete gui_world;
        return dmGameObject::CREATE_RESULT_OK;
    }

    bool SetupGuiScene(dmGui::HScene scene, GuiSceneResource* scene_resource)
    {
        dmGuiDDF::SceneDesc* scene_desc = scene_resource->m_SceneDesc;
        dmGui::SetSceneScript(scene, scene_resource->m_Script);

        bool result = true;

        for (uint32_t i = 0; i < scene_resource->m_FontMaps.Size(); ++i)
        {
            dmGui::AddFont(scene, scene_desc->m_Fonts[i].m_Name, (void*)scene_resource->m_FontMaps[i]);
        }

        for (uint32_t i = 0; i < scene_resource->m_Textures.Size(); ++i)
        {
            dmGui::AddTexture(scene, scene_desc->m_Textures[i].m_Name, (void*)scene_resource->m_Textures[i]);
        }

        for (uint32_t i = 0; i < scene_desc->m_Nodes.m_Count; ++i)
        {
            const dmGuiDDF::NodeDesc* node_desc = &scene_desc->m_Nodes[i];

            // NOTE: We assume that the enums in dmGui and dmGuiDDF have the same values
            dmGui::NodeType type = (dmGui::NodeType) node_desc->m_Type;
            dmGui::BlendMode blend_mode = (dmGui::BlendMode) node_desc->m_BlendMode;

            Vector4 position = node_desc->m_Position;
            Vector4 size = node_desc->m_Size;
            dmGui::HNode n = dmGui::NewNode(scene, Point3(position.getXYZ()), Vector3(size.getXYZ()), type);
            if (n)
            {
                if (node_desc->m_Type == dmGuiDDF::NodeDesc::TYPE_TEXT)
                {
                    dmGui::SetNodeText(scene, n, node_desc->m_Text);
                    dmGui::SetNodeFont(scene, n, node_desc->m_Font);
                }
                if (node_desc->m_Id)
                {
                    dmGui::SetNodeId(scene, n, node_desc->m_Id);
                }
                if (node_desc->m_Texture != 0x0 && *node_desc->m_Texture != '\0')
                {
                    dmGui::Result gui_result = dmGui::SetNodeTexture(scene, n, node_desc->m_Texture);
                    if (gui_result != dmGui::RESULT_OK)
                    {
                        dmLogError("The texture '%s' could not be set for the '%s', result: %d.", node_desc->m_Texture, node_desc->m_Id != 0x0 ? node_desc->m_Id : "unnamed", gui_result);
                        result = false;
                    }
                }

                dmGui::SetNodeProperty(scene, n, dmGui::PROPERTY_ROTATION, node_desc->m_Rotation);
                dmGui::SetNodeProperty(scene, n, dmGui::PROPERTY_SCALE, node_desc->m_Scale);
                dmGui::SetNodeProperty(scene, n, dmGui::PROPERTY_COLOR, node_desc->m_Color);
                dmGui::SetNodeBlendMode(scene, n, blend_mode);
                dmGui::SetNodePivot(scene, n, (dmGui::Pivot) node_desc->m_Pivot);
                dmGui::SetNodeXAnchor(scene, n, (dmGui::XAnchor) node_desc->m_Xanchor);
                dmGui::SetNodeYAnchor(scene, n, (dmGui::YAnchor) node_desc->m_Yanchor);
            }
            else
            {
                result = false;
            }
        }
        return result;
    }

    dmGameObject::CreateResult CompGuiCreate(const dmGameObject::ComponentCreateParams& params)
    {
        GuiWorld* gui_world = (GuiWorld*)params.m_World;

        GuiSceneResource* scene_resource = (GuiSceneResource*) params.m_Resource;

        Component* gui_component = new Component();
        gui_component->m_Instance = params.m_Instance;
        gui_component->m_ComponentIndex = params.m_ComponentIndex;
        gui_component->m_Enabled = 1;

        dmGui::NewSceneParams scene_params;
        scene_params.m_MaxNodes = 256;
        scene_params.m_MaxAnimations = 1024;
        scene_params.m_UserData = gui_component;
        gui_component->m_Scene = dmGui::NewScene(scene_resource->m_GuiContext, &scene_params);
        dmGui::HScene scene = gui_component->m_Scene;

        if (!SetupGuiScene(scene, scene_resource))
        {
            dmGui::DeleteScene(gui_component->m_Scene);
            delete gui_component;
            return dmGameObject::CREATE_RESULT_UNKNOWN_ERROR;
        }
        else
        {
            *params.m_UserData = (uintptr_t)gui_component;
            gui_world->m_Components.Push(gui_component);
            return dmGameObject::CREATE_RESULT_OK;
        }
    }

    dmGameObject::CreateResult CompGuiDestroy(const dmGameObject::ComponentDestroyParams& params)
    {
        GuiWorld* gui_world = (GuiWorld*)params.m_World;
        Component* gui_component = (Component*)*params.m_UserData;
        for (uint32_t i = 0; i < gui_world->m_Components.Size(); ++i)
        {
            if (gui_world->m_Components[i] == gui_component)
            {
                dmGui::DeleteScene(gui_component->m_Scene);
                delete gui_component;
                gui_world->m_Components.EraseSwap(i);
                break;
            }
        }
        return dmGameObject::CREATE_RESULT_OK;
    }

    dmGameObject::CreateResult CompGuiInit(const dmGameObject::ComponentInitParams& params)
    {
        Component* gui_component = (Component*)*params.m_UserData;
        dmGui::Result result = dmGui::InitScene(gui_component->m_Scene);
        if (result != dmGui::RESULT_OK)
        {
            // TODO: Translate result
            dmLogError("Error when initializing gui component: %d.", result);
            return dmGameObject::CREATE_RESULT_UNKNOWN_ERROR;
        }
        return dmGameObject::CREATE_RESULT_OK;
    }

    dmGameObject::CreateResult CompGuiFinal(const dmGameObject::ComponentFinalParams& params)
    {
        Component* gui_component = (Component*)*params.m_UserData;
        dmGui::Result result = dmGui::FinalScene(gui_component->m_Scene);
        if (result != dmGui::RESULT_OK)
        {
            // TODO: Translate result
            dmLogError("Error when finalizing gui component: %d.", result);
            return dmGameObject::CREATE_RESULT_UNKNOWN_ERROR;
        }
        return dmGameObject::CREATE_RESULT_OK;
    }

    struct RenderGuiContext
    {
        dmRender::HRenderContext m_RenderContext;
        GuiWorld*                m_GuiWorld;
        uint32_t                 m_NextZ;
    };

    static dmhash_t DIFFUSE_COLOR_HASH = dmHashString64("diffuse_color");

    void RenderNodes(dmGui::HScene scene,
                    dmGui::HNode* nodes,
                    const Matrix4* node_transforms,
                    uint32_t node_count,
                    void* context)
    {
        RenderGuiContext* gui_context = (RenderGuiContext*) context;
        GuiWorld* gui_world = gui_context->m_GuiWorld;

        for (uint32_t i = 0; i < node_count; ++i)
        {
            dmGui::HNode node = nodes[i];

            const Vector4& color = dmGui::GetNodeProperty(scene, node, dmGui::PROPERTY_COLOR);

            dmRender::RenderObject ro;

            dmGui::BlendMode blend_mode = dmGui::GetNodeBlendMode(scene, node);
            switch (blend_mode)
            {
                case dmGui::BLEND_MODE_ALPHA:
                    ro.m_SourceBlendFactor = dmGraphics::BLEND_FACTOR_SRC_ALPHA;
                    ro.m_DestinationBlendFactor = dmGraphics::BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
                break;

                case dmGui::BLEND_MODE_ADD:
                    ro.m_SourceBlendFactor = dmGraphics::BLEND_FACTOR_ONE;
                    ro.m_DestinationBlendFactor = dmGraphics::BLEND_FACTOR_ONE;
                break;

                case dmGui::BLEND_MODE_ADD_ALPHA:
                    ro.m_SourceBlendFactor = dmGraphics::BLEND_FACTOR_ONE;
                    ro.m_DestinationBlendFactor = dmGraphics::BLEND_FACTOR_SRC_ALPHA;
                break;

                case dmGui::BLEND_MODE_MULT:
                    ro.m_SourceBlendFactor = dmGraphics::BLEND_FACTOR_ZERO;
                    ro.m_DestinationBlendFactor = dmGraphics::BLEND_FACTOR_SRC_COLOR;
                break;

                default:
                    dmLogError("Unknown blend mode: %d\n", blend_mode);
                    assert(0);
                break;
            }
            ro.m_SetBlendFactors = 1;

            ro.m_VertexDeclaration = gui_world->m_VertexDeclaration;
            ro.m_VertexBuffer = gui_world->m_QuadVertexBuffer;
            ro.m_PrimitiveType = dmGraphics::PRIMITIVE_TRIANGLE_STRIP;
            ro.m_VertexStart = 0;
            ro.m_VertexCount = 4;
            ro.m_Material = gui_world->m_Material;

            // Set default texture
            void* texture = dmGui::GetNodeTexture(scene, node);
            if (texture)
                ro.m_Textures[0] = (dmGraphics::HTexture) texture;
            else
                ro.m_Textures[0] = gui_world->m_WhiteTexture;

            if (dmGui::GetNodeType(scene, node) == dmGui::NODE_TYPE_BOX)
            {
                ro.m_WorldTransform = node_transforms[i];
                ro.m_RenderKey.m_Depth = gui_context->m_NextZ;
                dmRender::EnableRenderObjectConstant(&ro, DIFFUSE_COLOR_HASH, color);
                gui_world->m_GuiRenderObjects.Push(ro);

                dmRender::AddToRender(gui_context->m_RenderContext, &gui_world->m_GuiRenderObjects[gui_world->m_GuiRenderObjects.Size()-1]);
            }
            else if (dmGui::GetNodeType(scene, node) == dmGui::NODE_TYPE_TEXT)
            {
                dmRender::DrawTextParams params;
                params.m_FaceColor = color;
                params.m_Text = dmGui::GetNodeText(scene, node);
                params.m_WorldTransform = node_transforms[i];
                params.m_Depth = gui_context->m_NextZ;
                dmRender::DrawText(gui_context->m_RenderContext, (dmRender::HFontMap) dmGui::GetNodeFont(scene, node), params);
            }
            gui_context->m_NextZ++;
        }
    }

    dmGameObject::UpdateResult CompGuiUpdate(const dmGameObject::ComponentsUpdateParams& params)
    {
        GuiWorld* gui_world = (GuiWorld*)params.m_World;
        GuiContext* gui_context = (GuiContext*)params.m_Context;

        // update
        for (uint32_t i = 0; i < gui_world->m_Components.Size(); ++i)
        {
            if (gui_world->m_Components[i]->m_Enabled)
                dmGui::UpdateScene(gui_world->m_Components[i]->m_Scene, params.m_UpdateContext->m_DT);
        }

        RenderGuiContext render_gui_context;
        render_gui_context.m_RenderContext = gui_context->m_RenderContext;
        render_gui_context.m_GuiWorld = gui_world;
        render_gui_context.m_NextZ = 0;


        uint32_t total_node_count = 0;
        for (uint32_t i = 0; i < gui_world->m_Components.Size(); ++i)
        {
            Component* c = gui_world->m_Components[i];
            if (c->m_Enabled)
            {
                total_node_count += dmGui::GetNodeCount(c->m_Scene);
            }
        }

        if (gui_world->m_GuiRenderObjects.Capacity() < total_node_count)
        {
            gui_world->m_GuiRenderObjects.SetCapacity(total_node_count);
        }
        gui_world->m_GuiRenderObjects.SetSize(0);
        for (uint32_t i = 0; i < gui_world->m_Components.Size(); ++i)
        {
            Component* c = gui_world->m_Components[i];
            if (c->m_Enabled)
                dmGui::RenderScene(c->m_Scene, &RenderNodes, &render_gui_context);
        }

        return dmGameObject::UPDATE_RESULT_OK;
    }

    dmGameObject::UpdateResult CompGuiOnMessage(const dmGameObject::ComponentOnMessageParams& params)
    {
        Component* gui_component = (Component*)*params.m_UserData;
        if (params.m_Message->m_Id == dmHashString64("enable"))
        {
            gui_component->m_Enabled = 1;
        }
        else if (params.m_Message->m_Id == dmHashString64("disable"))
        {
            gui_component->m_Enabled = 0;
        }
        dmGui::Result result = dmGui::DispatchMessage(gui_component->m_Scene, params.m_Message);
        if (result != dmGui::RESULT_OK)
        {
            // TODO: Proper error message
            dmLogError("Error when dispatching message to gui scene: %d", result);
        }
        return dmGameObject::UPDATE_RESULT_OK;
    }

    dmGameObject::InputResult CompGuiOnInput(const dmGameObject::ComponentOnInputParams& params)
    {
        Component* gui_component = (Component*)*params.m_UserData;

        if (gui_component->m_Enabled)
        {
            dmGui::HScene scene = gui_component->m_Scene;
            dmGui::InputAction gui_input_action;
            gui_input_action.m_ActionId = params.m_InputAction->m_ActionId;
            gui_input_action.m_Value = params.m_InputAction->m_Value;
            gui_input_action.m_Pressed = params.m_InputAction->m_Pressed;
            gui_input_action.m_Released = params.m_InputAction->m_Released;
            gui_input_action.m_Repeated = params.m_InputAction->m_Repeated;
            gui_input_action.m_PositionSet = params.m_InputAction->m_PositionSet;
            gui_input_action.m_X = params.m_InputAction->m_X;
            gui_input_action.m_Y = params.m_InputAction->m_Y;
            gui_input_action.m_DX = params.m_InputAction->m_DX;
            gui_input_action.m_DY = params.m_InputAction->m_DY;
            bool consumed;
            dmGui::Result gui_result = dmGui::DispatchInput(scene, &gui_input_action, 1, &consumed);
            if (gui_result != dmGui::RESULT_OK)
            {
                return dmGameObject::INPUT_RESULT_UNKNOWN_ERROR;
            }
            else if (consumed)
            {
                return dmGameObject::INPUT_RESULT_CONSUMED;
            }
        }
        return dmGameObject::INPUT_RESULT_IGNORED;
    }

    void CompGuiOnReload(const dmGameObject::ComponentOnReloadParams& params)
    {
        GuiSceneResource* scene_resource = (GuiSceneResource*) params.m_Resource;
        Component* gui_component = (Component*)*params.m_UserData;
        dmGui::Result result = dmGui::FinalScene(gui_component->m_Scene);
        if (result != dmGui::RESULT_OK)
        {
            // TODO: Translate result
            dmLogError("Error when finalizing gui component: %d.", result);
        }
        dmGui::ClearTextures(gui_component->m_Scene);
        dmGui::ClearFonts(gui_component->m_Scene);
        dmGui::ClearNodes(gui_component->m_Scene);
        if (SetupGuiScene(gui_component->m_Scene, scene_resource))
        {
            result = dmGui::InitScene(gui_component->m_Scene);
            if (result != dmGui::RESULT_OK)
            {
                // TODO: Translate result
                dmLogError("Error when initializing gui component: %d.", result);
            }
        }
        else
        {
            dmLogError("Could not reload scene '%s' because of errors in the resource.", scene_resource->m_Path);
        }
    }

    void GuiGetURLCallback(dmGui::HScene scene, dmMessage::URL* url)
    {
        Component* component = (Component*)dmGui::GetSceneUserData(scene);
        url->m_Socket = dmGameObject::GetMessageSocket(dmGameObject::GetCollection(component->m_Instance));
        url->m_Path = dmGameObject::GetIdentifier(component->m_Instance);
        dmGameObject::Result result = dmGameObject::GetComponentId(component->m_Instance, component->m_ComponentIndex, &url->m_Fragment);
        if (result != dmGameObject::RESULT_OK)
        {
            dmLogError("Could not find gui component: %d", result);
        }
    }

    uintptr_t GuiGetUserDataCallback(dmGui::HScene scene)
    {
        Component* component = (Component*)dmGui::GetSceneUserData(scene);
        return (uintptr_t)component->m_Instance;
    }

    dmhash_t GuiResolvePathCallback(dmGui::HScene scene, const char* path, uint32_t path_size)
    {
        Component* component = (Component*)dmGui::GetSceneUserData(scene);
        if (path_size > 0)
        {
            return dmGameObject::GetAbsoluteIdentifier(component->m_Instance, path, path_size);
        }
        else
        {
            return dmGameObject::GetIdentifier(component->m_Instance);
        }
    }

    void GuiGetTextMetricsCallback(const void* font, const char* text, dmGui::TextMetrics* out_metrics)
    {
        dmRender::TextMetrics metrics;
        dmRender::GetTextMetrics((dmRender::HFontMap)font, text, &metrics);
        out_metrics->m_Width = metrics.m_Width;
        out_metrics->m_MaxAscent = metrics.m_MaxAscent;
        out_metrics->m_MaxDescent = metrics.m_MaxDescent;
    }
}
