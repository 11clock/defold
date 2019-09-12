#ifndef DM_GAMESYS_RES_MESH_H
#define DM_GAMESYS_RES_MESH_H

#include <stdint.h>

#include <resource/resource.h>
#include <render/render.h>
#include "mesh_ddf.h"

#include "res_buffer.h"

namespace dmGameSystem
{
    struct MeshResource
    {
        MeshResource() {
            // nop?
        }

        dmMeshDDF::MeshDesc*    m_MeshDDF;
        BufferResource*         m_BufferResource;
        dmRender::HMaterial     m_Material;
        // dmGraphics::HVertexBuffer m_VertexBuffer;
        // dmGraphics::HIndexBuffer m_IndexBuffer;
        dmGraphics::HTexture    m_Textures[dmRender::RenderObject::MAX_TEXTURE_COUNT];
        dmhash_t                m_TexturePaths[dmRender::RenderObject::MAX_TEXTURE_COUNT];
        // dmGraphics::Type        m_IndexBufferElementType;

        dmGraphics::HVertexDeclaration  m_VertexDeclaration;
        dmGraphics::HVertexBuffer       m_VertexBuffer;
        // void*                           m_VertexBufferData;
        uint32_t                        m_ElementCount;
    };

    dmResource::Result ResMeshPreload(const dmResource::ResourcePreloadParams& params);

    dmResource::Result ResMeshCreate(const dmResource::ResourceCreateParams& params);

    dmResource::Result ResMeshDestroy(const dmResource::ResourceDestroyParams& params);

    dmResource::Result ResMeshRecreate(const dmResource::ResourceRecreateParams& params);
}

#endif // DM_GAMESYS_RES_MESH_H
