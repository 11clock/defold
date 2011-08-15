#ifndef RENDER_H
#define RENDER_H

#include <string.h>
#include <stdint.h>
#include <vectormath/cpp/vectormath_aos.h>
#include <dlib/hash.h>
#include <script/script.h>
#include <graphics/graphics.h>
#include "render/material_ddf.h"

namespace dmRender
{
    using namespace Vectormath::Aos;

    extern const char* RENDER_SOCKET_NAME;

    typedef struct RenderContext*           HRenderContext;
    typedef struct RenderTargetSetup*       HRenderTargetSetup;
    typedef uint32_t                        HRenderType;
    typedef struct NamedConstantBuffer*     HNamedConstantBuffer;
    typedef struct RenderScript*            HRenderScript;
    typedef struct RenderScriptInstance*    HRenderScriptInstance;
    typedef struct Material*                HMaterial;

    /**
     * Font map handle
     */
    typedef struct FontMap* HFontMap;

    enum Result
    {
        RESULT_OK = 0,
        RESULT_INVALID_CONTEXT = -1,
        RESULT_OUT_OF_RESOURCES = -2,
        RESULT_BUFFER_IS_FULL = -3,
    };

    enum RenderScriptResult
    {
        RENDER_SCRIPT_RESULT_FAILED = -1,
        RENDER_SCRIPT_RESULT_NO_FUNCTION = 0,
        RENDER_SCRIPT_RESULT_OK = 1
    };

    struct Predicate
    {
        static const uint32_t MAX_TAG_COUNT = 32;
        uint32_t m_Tags[MAX_TAG_COUNT];
        uint32_t m_TagCount;
    };

    struct RenderKey
    {
        RenderKey()
        {
            m_Key = 0;
        }

        union
        {
            struct
            {
                uint64_t    m_Depth:32;
                uint64_t    m_Order:3;
                uint64_t    m_Translucency:1;
                uint64_t    m_MaterialId:28;
            };
            uint64_t        m_Key;
        };
    };

    struct Constant
    {
        Vectormath::Aos::Vector4                m_Value;
        dmhash_t                                m_NameHash;
        dmRenderDDF::MaterialDesc::ConstantType m_Type;
        int32_t                                 m_Location;

        Constant() {}
        Constant(dmhash_t name_hash, int32_t location)
            : m_Value(Vectormath::Aos::Vector4(0)), m_NameHash(name_hash), m_Type(dmRenderDDF::MaterialDesc::CONSTANT_TYPE_USER), m_Location(location)
        {
        }
    };

    struct RenderObject
    {
        RenderObject();

        static const uint32_t MAX_TEXTURE_COUNT = 32;
        static const uint32_t MAX_CONSTANT_COUNT = 4;
        RenderKey                       m_RenderKey;
        Constant                        m_Constants[MAX_CONSTANT_COUNT];
        Matrix4                         m_WorldTransform;
        Matrix4                         m_TextureTransform;
        dmGraphics::HVertexBuffer       m_VertexBuffer;
        dmGraphics::HVertexDeclaration  m_VertexDeclaration;
        dmGraphics::HIndexBuffer        m_IndexBuffer;
        HMaterial                       m_Material;
        dmGraphics::HTexture            m_Textures[MAX_TEXTURE_COUNT];
        dmGraphics::PrimitiveType       m_PrimitiveType;
        dmGraphics::Type                m_IndexType;
        dmGraphics::BlendFactor         m_SourceBlendFactor;
        dmGraphics::BlendFactor         m_DestinationBlendFactor;
        uint32_t                        m_VertexStart;
        uint32_t                        m_VertexCount;
        uint8_t                         m_VertexConstantMask;
        uint8_t                         m_FragmentConstantMask;
        uint8_t                         m_SetBlendFactors : 1;
        // Set to true if RenderKey.m_Depth should be filled in
        uint8_t                         m_CalculateDepthKey : 1;
    };

    struct RenderContextParams
    {
        RenderContextParams();

        dmScript::HContext              m_ScriptContext;
        HFontMap                        m_SystemFontMap;
        void*                           m_VertexProgramData;
        void*                           m_FragmentProgramData;
        uint32_t                        m_MaxRenderTypes;
        uint32_t                        m_MaxInstances;
        uint32_t                        m_MaxRenderTargets;
        uint32_t                        m_VertexProgramDataSize;
        uint32_t                        m_FragmentProgramDataSize;
        uint32_t                        m_MaxCharacters;
        uint32_t                        m_CommandBufferSize;
    };

    static const HRenderType INVALID_RENDER_TYPE_HANDLE = ~0;

    HRenderContext NewRenderContext(dmGraphics::HContext graphics_context, const RenderContextParams& params);
    Result DeleteRenderContext(HRenderContext render_context);

    void SetSystemFontMap(HRenderContext render_context, HFontMap font_map);

    Result RegisterRenderTarget(HRenderContext render_context, dmGraphics::HRenderTarget rendertarget, dmhash_t hash);
    dmGraphics::HRenderTarget GetRenderTarget(HRenderContext render_context, dmhash_t hash);

    dmGraphics::HContext GetGraphicsContext(HRenderContext render_context);

    const Matrix4& GetViewProjectionMatrix(HRenderContext render_context);
    void SetViewMatrix(HRenderContext render_context, const Matrix4& view);
    void SetProjectionMatrix(HRenderContext render_context, const Matrix4& projection);

    Result AddToRender(HRenderContext context, RenderObject* ro);
    Result ClearRenderObjects(HRenderContext context);

    Result Draw(HRenderContext context, Predicate* predicate, HNamedConstantBuffer constant_buffer);
    Result DrawDebug3d(HRenderContext context);
    Result DrawDebug2d(HRenderContext context);

    void EnableRenderObjectConstant(RenderObject* ro, dmhash_t name_hash, const Vectormath::Aos::Vector4& value);
    void DisableRenderObjectConstant(RenderObject* ro, dmhash_t name_hash);

    /**
     * Render debug square. The upper left corner of the screen is (-1,-1) and the bottom right is (1,1).
     * @param context Render context handle
     * @param x0 x coordinate of the left edge of the square
     * @param y0 y coordinate of the upper edge of the square
     * @param x1 x coordinate of the right edge of the square
     * @param y1 y coordinate of the bottom edge of the square
     * @param color Color
     */
    void Square2d(HRenderContext context, float x0, float y0, float x1, float y1, Vector4 color);

    /**
     * Render debug line. The upper left corner of the screen is (-1,-1) and the bottom right is (1,1).
     * @param context Render context handle
     * @param x0 x coordinate of the start of the line
     * @param y0 y coordinate of the start of the line
     * @param x1 x coordinate of the end of the line
     * @param y1 y coordinate of the end of the line
     * @param color0 Color of the start of the line
     * @param color1 Color of the end of the line
     */
    void Line2D(HRenderContext context, float x0, float y0, float x1, float y1, Vector4 color0, Vector4 color1);

    /**
     * Line3D Render debug line
     * @param context Render context handle
     * @param start Start point
     * @param end End point
     * @param color Color
     */
    void Line3D(HRenderContext context, Point3 start, Point3 end, Vector4 start_color, Vector4 end_color);

    HRenderScript   NewRenderScript(HRenderContext render_context, const void* buffer, uint32_t buffer_size, const char* filename);
    bool            ReloadRenderScript(HRenderContext render_context, HRenderScript render_script, const void* buffer, uint32_t buffer_size, const char* filename);
    void            DeleteRenderScript(HRenderContext render_context, HRenderScript render_script);

    HRenderScriptInstance   NewRenderScriptInstance(HRenderContext render_context, HRenderScript render_script);
    void                    DeleteRenderScriptInstance(HRenderScriptInstance render_script_instance);
    void                    SetRenderScriptInstanceRenderScript(HRenderScriptInstance render_script_instance, HRenderScript render_script);
    void                    AddRenderScriptInstanceMaterial(HRenderScriptInstance render_script_instance, const char* material_name, dmRender::HMaterial material);
    void                    ClearRenderScriptInstanceMaterials(HRenderScriptInstance render_script_instance);
    RenderScriptResult      InitRenderScriptInstance(HRenderScriptInstance render_script_instance);
    RenderScriptResult      UpdateRenderScriptInstance(HRenderScriptInstance render_script_instance);
    void                    OnReloadRenderScriptInstance(HRenderScriptInstance render_script_instance);

    // Material
    HMaterial                       NewMaterial(dmRender::HRenderContext render_context, dmGraphics::HVertexProgram vertex_program, dmGraphics::HFragmentProgram fragment_program);
    void                            DeleteMaterial(HMaterial material);
    void                            ApplyMaterialConstants(dmRender::HRenderContext render_context, HMaterial material, const RenderObject* ro);
    void                            ApplyMaterialSamplers(dmRender::HRenderContext render_context, HMaterial material);

    dmGraphics::HProgram            GetMaterialProgram(HMaterial material);
    dmGraphics::HVertexProgram      GetMaterialVertexProgram(HMaterial material);
    dmGraphics::HFragmentProgram    GetMaterialFragmentProgram(HMaterial material);
    void                            SetMaterialProgramConstantType(HMaterial material, dmhash_t name_hash, dmRenderDDF::MaterialDesc::ConstantType type);
    void                            SetMaterialProgramConstant(HMaterial material, dmhash_t name_hash, Vectormath::Aos::Vector4 constant);
    int32_t                         GetMaterialConstantLocation(HMaterial material, dmhash_t name_hash);
    void                            SetMaterialSampler(HMaterial material, dmhash_t name_hash, int16_t unit);

    HNamedConstantBuffer            NewNamedConstantBuffer();
    void                            DeleteNamedConstantBuffer(HNamedConstantBuffer buffer);
    void                            SetNamedConstant(HNamedConstantBuffer buffer, const char* name, Vectormath::Aos::Vector4 value);
    bool                            GetNamedConstant(HNamedConstantBuffer buffer, const char* name, Vectormath::Aos::Vector4& value);
    void                            ApplyNamedConstantBuffer(dmRender::HRenderContext render_context, HMaterial material, HNamedConstantBuffer buffer);

    uint32_t                        GetMaterialTagMask(HMaterial material);
    void                            AddMaterialTag(HMaterial material, uint32_t tag);
    void                            ClearMaterialTags(HMaterial material);
    uint32_t                        ConvertMaterialTagsToMask(uint32_t* tags, uint32_t tag_count);

}

#endif /* RENDER_H */
