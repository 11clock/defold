#ifndef DM_PARTICLE_H
#define DM_PARTICLE_H

#include <vectormath/cpp/vectormath_aos.h>
#include <dlib/configfile.h>
#include <ddf/ddf.h>
#include "particle/particle_ddf.h"

using namespace Vectormath::Aos;

/**
 * System to handle emitters, which spawn particles.
 */
namespace dmParticle
{
    /**
     * Context handle
     */
    typedef struct Context* HContext;
    /**
     * Emitter handle
     */
    typedef uint32_t HEmitter;

    /**
     * Invalid context handle
     */
    const HContext INVALID_CONTEXT = 0;
    /**
     * Invalid emitter handle
     */
    const HEmitter INVALID_EMITTER = 0;

    /// Config key to use for tweaking maximum number of emitters in a context.
    extern const char* MAX_EMITTER_COUNT_KEY;
    /// Config key to use for tweaking the total maximum number of particles in a context.
    extern const char* MAX_PARTICLE_COUNT_KEY;

    /**
     * Callback to handle rendering of emitters
     */
    typedef void (*RenderEmitterCallback)(void* usercontext, void* material, void* texture, uint32_t vertex_index, uint32_t vertex_count);
    /**
     * Callback to handle rendering of lines for debug purposes
     */
    typedef void (*RenderLineCallback)(void* usercontext, Vectormath::Aos::Point3 start, Vectormath::Aos::Point3 end, Vectormath::Aos::Vector4 color);
    /**
     * Representation of an emitter resource.
     */
    struct Prototype
    {
        Prototype()
        : m_DDF(0x0)
        , m_Texture(0)
        , m_Material(0)
        {
        }

        /// DDF structure read from the resource.
        dmParticleDDF::Emitter* m_DDF;
        /// Texture to use when rendering particles.
        void*                   m_Texture;
        /// Material to use when rendering particles.
        void*                   m_Material;
    };

    /**
     * Create a context.
     * @param render_context Context for use when rendering.
     * @param max_emitter_count Max number of emitters
     * @param max_particle_count Max number of particles
     * @return Context handle, or INVALID_CONTEXT when out of memory.
     */
    HContext                CreateContext(uint32_t max_emitter_count, uint32_t max_particle_count);
    /**
     * Destroy a context.
     * @param context Context to destroy. This will also destroy any remaining emitters.
     */
    void                    DestroyContext(HContext context);

    /**
     * Create an emitter from the supplied path and fetch resources using the supplied factory.
     * @param context Context in which to create the emitter, must be valid.
     * @param prototype Prototype of the emitter to be created
     * @return Emitter handle, or INVALID_EMITTER when the resource is broken or the context is full.
     */
    HEmitter                CreateEmitter(HContext context, Prototype* prototype);
    /**
     * Destroy emitter in the specified context.
     * @param context Context handle, must be valid.
     * @param emitter Emitter to destroy, must not be valid.
     */
    void                    DestroyEmitter(HContext context, HEmitter emitter);
    /**
     * Start the specified emitter, which means it will start spawning particles.
     * @param context Context in which the emitter exists.
     * @param emitter Emitter to start, must not be valid.
     */
    void                    StartEmitter(HContext context, HEmitter emitter);
    /**
     * Stop the specified emitter, which means it will stop spawning particles.
     * Any spawned particles will still be simulated until they die.
     * @param context Context in which the emitter exists.
     * @param emitter Emitter to start, must not be valid.
     */
    void                    StopEmitter(HContext context, HEmitter emitter);
    /**
     * Restart the specified emitter, which means it will start spawning particles again.
     * Any already living particles will remain after this call.
     * @param context Context in which the emitter exists.
     * @param emitter Emitter to restart, must not be valid.
     */
    void                    RestartEmitter(HContext context, HEmitter emitter);
    /**
     * Create and start a new emitter. Once the emitter is sleeping (@see IsSleeping), the particle system will automatically destroy it.
     * Looping emitters can not be created this way since they would never be destroyed in that case.
     * @param context Context in which the emitter exists.
     * @param prototype Prototype of the emitter to be created.
     * @param path Path of the emitter resource.
     * @param position Position of the created emitter.
     * @param rotation Rotation of the created emitter.
     */
    void                    FireAndForget(HContext context, Prototype* prototype, Point3 position, Quat rotation);

    /**
     * Set the position of the specified emitter.
     * @param context Context in which the emitter exists.
     * @param emitter Emitter to set position for.
     * @param position Position in world space.
     */
    void                    SetPosition(HContext context, HEmitter emitter, Point3 position);
    /**
     * Set the position of the specified emitter.
     * @param context Context in which the emitter exists.
     * @param emitter Emitter to set rotation for.
     * @param rotation Rotation in world space.
     */
    void                    SetRotation(HContext context, HEmitter emitter, Quat rotation);
    /**
     * Set the gain signal of the specified emitter. The signal is used to modify properties when spawning particles.
     * @param context Context in which the emitter exists.
     * @param emitter Emitter to set gain for.
     * @param gain Gain to set, truncated to [0,1].
     */
    void                    SetGain(HContext context, HEmitter emitter, float gain);

    /**
     * Returns if the specified emitter is spawning particles or not.
     * A looping emitter is always spawning particles, other emitters are spawning until their duration has elapsed.
     */
    bool                    IsSpawning(HContext context, HEmitter emitter);
    /**
     * Returns if the specified emitter is spawning particles or not.
     * A looping emitter is never sleeping, other emitters are sleeping when they are not spawning and have no remaining living particles.
     */
    bool                    IsSleeping(HContext context, HEmitter emitter);

    /**
     * Update the emitters within the specified context.
     * @param context Context of the emitters to update.
     * @param dt Time step.
     * @param vertex_buffer Vertex buffer into which to store the particle vertex data. If this is 0x0, no rendering will occur.
     * @param vertex_buffer_size Size in bytes of the supplied vertex buffer.
     */
    void                    Update(HContext context, float dt, float* vertex_buffer, uint32_t vertex_buffer_size);

    /**
     * Render the emitters within the specified context.
     * @param context Context of the emitters to render.
     */
    void                    Render(HContext context, void* user_context, RenderEmitterCallback render_emitter_callback);
    /**
     * Debug render the status of the emitters within the specified context.
     * @param context Context of the emitters to render.
     * @param RenderLine Function pointer to use to render the lines.
     */
    void                    DebugRender(HContext context, void* user_context, RenderLineCallback render_line_callback);
}

#endif // DM_PARTICLE_H
