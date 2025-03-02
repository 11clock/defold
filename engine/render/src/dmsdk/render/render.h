// Copyright 2020 The Defold Foundation
// Licensed under the Defold License version 1.0 (the "License"); you may not use
// this file except in compliance with the License.
//
// You may obtain a copy of the License, together with FAQs at
// https://www.defold.com/license
//
// Unless required by applicable law or agreed to in writing, software distributed
// under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
// CONDITIONS OF ANY KIND, either express or implied. See the License for the
// specific language governing permissions and limitations under the License.

#ifndef DMSDK_RENDER_H
#define DMSDK_RENDER_H

#include <stdint.h>
#include <render/material_ddf.h>
#include <dmsdk/dlib/hash.h>
#include <dmsdk/dlib/vmath.h>
#include <dmsdk/graphics/graphics.h>

/*# Render API documentation
 * [file:<dmsdk/render/render.h>]
 *
 * Api for render specific data
 *
 * @document
 * @name Render
 * @namespace dmRender
 */

namespace dmRender
{
    /*#
     * The render context
     * @typedef
     * @name HRenderContext
     */
    typedef struct RenderContext* HRenderContext;

    /*#
     * Material instance handle
     * @typedef
     * @name HMaterial
     */
    typedef struct Material* HMaterial;

    /*#
     * Font map handle
     * @typedef
     * @name HFontMap
     */
    typedef struct FontMap* HFontMap;

    /*#
     * @enum
     * @name Result
     * @member RESULT_OK
     * @member RESULT_INVALID_CONTEXT
     * @member RESULT_OUT_OF_RESOURCES
     * @member RESULT_BUFFER_IS_FULL
     * @member RESULT_INVALID_PARAMETER
     */
    enum Result
    {
        RESULT_OK = 0,
        RESULT_INVALID_CONTEXT = -1,
        RESULT_OUT_OF_RESOURCES = -2,
        RESULT_BUFFER_IS_FULL = -3,
        RESULT_INVALID_PARAMETER = -4,
    };

    /*#
     * Get the vertex space (local or world)
     * @name dmRender::GetMaterialVertexSpace
     * @param material [type: dmRender::HMaterial] the material
     * @return vertex_space [type: dmRenderDDF::MaterialDesc::VertexSpace] the vertex space
     */
    dmRenderDDF::MaterialDesc::VertexSpace GetMaterialVertexSpace(HMaterial material);

    /*#
     * URL specifying a sender/receiver of messages
     * @note Currently has a hard limit of 32 bytes
     * @struct
     * @name Constant
     */
    struct Constant
    {
        dmVMath::Vector4                        m_Value;
        dmhash_t                                m_NameHash;
        dmRenderDDF::MaterialDesc::ConstantType m_Type;
        int32_t                                 m_Location;

    // Private
        Constant();
        Constant(dmhash_t name_hash, int32_t location);
    };

    /*#
     * Struct holding stencil operation setup
     * @struct
     * @name StencilTestParams
     * @member m_Func [type: dmGraphics::CompareFunc] the compare function
     * @member m_OpSFail [type: dmGraphics::StencilOp] the stencil fail operation
     * @member m_OpDPFail [type: dmGraphics::StencilOp] the depth pass fail operation
     * @member m_OpDPPass [type: dmGraphics::StencilOp] the depth pass pass operation
     * @member m_Ref [type: uint8_t]
     * @member m_RefMask [type: uint8_t]
     * @member m_BufferMask [type: uint8_t]
     * @member m_ColorBufferMask [type: uint8_t:4]
     * @member m_ClearBuffer [type: uint8_t:1]
     */
    struct StencilTestParams
    {
        StencilTestParams();
        void Init();

        struct
        {
            dmGraphics::CompareFunc m_Func;
            dmGraphics::StencilOp   m_OpSFail;
            dmGraphics::StencilOp   m_OpDPFail;
            dmGraphics::StencilOp   m_OpDPPass;
        } m_Front;

        struct
        {
            dmGraphics::CompareFunc m_Func;
            dmGraphics::StencilOp   m_OpSFail;
            dmGraphics::StencilOp   m_OpDPFail;
            dmGraphics::StencilOp   m_OpDPPass;
        } m_Back;

        uint8_t m_Ref;
        uint8_t m_RefMask;
        uint8_t m_BufferMask;
        uint8_t m_ColorBufferMask : 4;
        uint8_t m_ClearBuffer : 1;
        uint8_t m_SeparateFaceStates : 1;
        uint8_t : 2;
    };


    /*#
     * The maximum number of textures the render object can hold (currently 8)
     * @constant
     * @name dmRender::RenderObject::MAX_TEXTURE_COUNT
     */

    /*#
     * The maximum number of shader constants the render object can hold (currently 16)
     * @constant
     * @name dmRender::RenderObject::MAX_CONSTANT_COUNT
     */

    /*#
     * Render objects represent an actual draw call
     * @struct
     * @name RenderObject
     * @member m_Constants [type: dmRender::Constant[]] the shader constants
     * @member m_WorldTransform [type: dmVMath::Matrix4] the world transform (usually identity for batched objects)
     * @member m_TextureTransform [type: dmVMath::Matrix4] the texture transform
     * @member m_VertexBuffer [type: dmGraphics::HVertexBuffer] the vertex buffer
     * @member m_VertexDeclaration [type: dmGraphics::HVertexDeclaration] the vertex declaration
     * @member m_IndexBuffer [type: dmGraphics::HIndexBuffer] the index buffer
     * @member m_Material [type: dmRender::HMaterial] the material
     * @member m_Textures [type: dmGraphics::HTexture[]] the textures
     * @member m_PrimitiveType [type: dmGraphics::PrimitiveType] the primitive type
     * @member m_IndexType [type: dmGraphics::Type] the index type (16/32 bit)
     * @member m_SourceBlendFactor [type: dmGraphics::BlendFactor] the source blend factor
     * @member m_DestinationBlendFactor [type: dmGraphics::BlendFactor] the destination blend factor
     * @member m_StencilTestParams [type: dmRender::StencilTestParams] the stencil test params
     * @member m_VertexStart [type: uint32_t] the vertex start
     * @member m_VertexCount [type: uint32_t] the vertex count
     * @member m_SetBlendFactors [type: uint8_t:1] use the blend factors
     * @member m_SetStencilTest [type: uint8_t:1] use the stencil test
     */
    struct RenderObject
    {
        RenderObject();
        void Init();
        void ClearConstants();

        static const uint32_t MAX_TEXTURE_COUNT = 8;
        static const uint32_t MAX_CONSTANT_COUNT = 16;
        Constant                        m_Constants[MAX_CONSTANT_COUNT];
        dmVMath::Matrix4                m_WorldTransform;
        dmVMath::Matrix4                m_TextureTransform;
        dmGraphics::HVertexBuffer       m_VertexBuffer;
        dmGraphics::HVertexDeclaration  m_VertexDeclaration;
        dmGraphics::HIndexBuffer        m_IndexBuffer;
        HMaterial                       m_Material;
        dmGraphics::HTexture            m_Textures[MAX_TEXTURE_COUNT];
        dmGraphics::PrimitiveType       m_PrimitiveType;
        dmGraphics::Type                m_IndexType;
        dmGraphics::BlendFactor         m_SourceBlendFactor;
        dmGraphics::BlendFactor         m_DestinationBlendFactor;
        dmGraphics::FaceWinding         m_FaceWinding;
        StencilTestParams               m_StencilTestParams;
        uint32_t                        m_VertexStart;
        uint32_t                        m_VertexCount;
        uint8_t                         m_SetBlendFactors : 1;
        uint8_t                         m_SetStencilTest : 1;
        uint8_t                         m_SetFaceWinding : 1;
    };

    /*#
     * Represents a renderable object (e.g. a single sprite)
     * The renderer will (each frame) collect all entries with the current material tag, then batch these objects together.
     * Batching is done based on the batch key and Z value (or order for GUI nodes)
     * The caller will also register a callback function where the batched entries will be returned.
     * Each callback then represents a draw call, and will register a RenderObject
     * @name RenderListEntry
     * @param m_WorldPosition [type: dmVMath::Point3] the world position of the object
     * @param m_Order [type: uint32_t] the order to sort on (used if m_MajorOrder != RENDER_ORDER_WORLD)
     * @param m_BatchKey [type: uint32_t] the batch key to sort on (note: only 48 bits are currently used by renderer)
     * @param m_TagListKey [type: uint32_t] the key to the list of material tags
     * @param m_UserData [type: uint64_t] user data (available in the render dispatch callback)
     * @param m_MinorOrder [type: uint32_t:4] used to sort within a batch
     * @param m_MajorOrder [type: uint32_t:2] If RENDER_ORDER_WORLD, then sorting is done based on the world position.
                                              Otherwise the sorting uses the m_Order value directly.
     * @param m_Dispatch [type: uint32_t:8] The dispatch function callback (dmRender::HRenderListDispatch)
     */
    struct RenderListEntry
    {
        dmVMath::Point3 m_WorldPosition;
        uint32_t m_Order;
        uint32_t m_BatchKey;
        uint32_t m_TagListKey;
        uint64_t m_UserData;
        uint32_t m_MinorOrder:4;
        uint32_t m_MajorOrder:2;
        uint32_t m_Dispatch:8;
    };

    /*#
     * Render batch callback states
     * @enum
     * @name RenderListOperation
     * @member RENDER_LIST_OPERATION_BEGIN
     * @member RENDER_LIST_OPERATION_BATCH
     * @member RENDER_LIST_OPERATION_END
     */
    enum RenderListOperation
    {
        RENDER_LIST_OPERATION_BEGIN,
        RENDER_LIST_OPERATION_BATCH,
        RENDER_LIST_OPERATION_END
    };

    /*#
     * Render order
     * @enum
     * @name RenderOrder
     * @member RENDER_ORDER_WORLD           Used by game objects
     * @member RENDER_ORDER_AFTER_WORLD     Used by gui
     */
    enum RenderOrder
    {
        RENDER_ORDER_BEFORE_WORLD = 0, // not currently used, so let's keep it non documented
        RENDER_ORDER_WORLD        = 1,
        RENDER_ORDER_AFTER_WORLD  = 2,
    };

    /*#
     * Render dispatch function callback.
     * @struct
     * @name RenderListDispatchParams
     * @member m_Context [type: dmRender::HRenderContext] the context
     * @member m_UserData [type: void*] the callback user data (registered with RenderListMakeDispatch())
     * @member m_Operation [type: dmRender::RenderListOperation] the operation
     * @member m_Buf [type: dmRender::RenderListEntry] the render entry array
     * @member m_Begin [type: uint32_t*] the start of the render batch. contains index into the m_Buf array
     * @member m_End [type: uint32_t*] the end of the render batch. Loop while "m_Begin != m_End"
     */
    struct RenderListDispatchParams
    {
        HRenderContext m_Context;
        void* m_UserData;
        RenderListOperation m_Operation;
        RenderListEntry* m_Buf;
        uint32_t* m_Begin;
        uint32_t* m_End;
    };

    /*#
     * Render dispatch function handle.
     * @typedef
     * @name HRenderListDispatch
     */
    typedef uint8_t HRenderListDispatch;

    /*#
     * Render dispatch function callback.
     * @typedef
     * @name RenderListDispatchFn
     * @param params [type: dmRender::RenderListDispatchParams] the params
     */
    typedef void (*RenderListDispatchFn)(RenderListDispatchParams const &params);

    /*#
     * Register a render dispatch function
     * @name RenderListMakeDispatch
     * @param context [type: dmRender::HRenderContext] the context
     * @param fn [type: dmRender::RenderListDispatchFn] the render batch callback function
     * @param user_data [type: void*] userdata to the callback
     * @return dispatch [type: dmRender::HRenderListDispatch] the render dispatch function handle
     */
    HRenderListDispatch RenderListMakeDispatch(HRenderContext context, RenderListDispatchFn fn, void* user_data);

    /*#
     * Allocates an array of render entries
     * @note Do not store a pointer into this array, as they're reused next frame
     * @name RenderListAlloc
     * @param context [type: dmRender::HRenderContext] the context
     * @param entries [type: uint32_t] the number of entries to allocate
     * @return array [type: dmRender::RenderListEntry*] the render list entry array
     */
    RenderListEntry* RenderListAlloc(HRenderContext context, uint32_t entries);

    /*#
     * Adds a render object to the current render frame
     * @name RenderListSubmit
     * @param context [type: dmRender::HRenderContext] the context
     * @param begin [type: dmRender::RenderListEntry*] the start of the array
     * @param end [type: dmRender::RenderListEntry*] the end of the array (i.e. "while begin!=end: *begin ..."")
     */
    void RenderListSubmit(HRenderContext context, RenderListEntry* begin, RenderListEntry* end);

    /*#
     * Adds a render object to the current render frame
     * @name AddToRender
     * @param context [type: dmRender::HRenderContext] the context
     * @param ro [type: dmRender::RenderObject*] the render object
     * @return result [type: dmRender::Result] the result
     */
    Result AddToRender(HRenderContext context, RenderObject* ro);

    /*#
     * Gets the key to the material tag list
     * @name GetMaterialTagListKey
     * @param material [type: dmGraphics::HMaterial] the material
     * @return listkey [type: uint32_t] the list key
     */
    uint32_t GetMaterialTagListKey(HMaterial material);

    /*#
     * Sets a render constant on a render object
     * @name EnableRenderObjectConstant
     * @param ro [type: dmRender::RenderObject*] the render object
     * @param name_hash [type: dmhash_t] the name of the material constant
     * @param value [type: dmVMath::Vector4] the constant
     */
    void EnableRenderObjectConstant(RenderObject* ro, dmhash_t name_hash, const Vectormath::Aos::Vector4& value);

    /*#
     * Disables a previously set render constant on a render object
     * @name DisableRenderObjectConstant
     * @param ro [type: dmRender::RenderObject*] the render object
     * @param name_hash [type: dmhash_t] the name of the material constant
    */
    void DisableRenderObjectConstant(RenderObject* ro, dmhash_t name_hash);
}

#endif /* DMSDK_RENDER_H */
