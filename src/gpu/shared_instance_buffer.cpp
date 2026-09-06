#include <gpu/shared_instance_buffer.hpp>

#include <rlog.h>

#include <sokol_gfx.h>
#include <vulkan/vulkan.h>

namespace gpu {

std::unique_ptr<SharedInstanceBuffer> SharedInstanceBuffer::create(const Desc& desc) {
    if (sg_query_backend() != SG_BACKEND_VULKAN) {
        RLOG(LL_ERROR, "SharedInstanceBuffer: Vulkan backend only");
        return nullptr;
    }
    if (desc.instance_bytes == 0 || desc.max_instances == 0) {
        RLOG(LL_ERROR, "SharedInstanceBuffer: instance_bytes and max_instances must be > 0");
        return nullptr;
    }

    auto out = std::make_unique<SharedInstanceBuffer>();
    out->m_sync = desc.sync;
    out->m_max_instances = desc.max_instances;
    out->m_instance_bytes = desc.instance_bytes;
    out->m_total_bytes = desc.instance_bytes * desc.max_instances;
    out->m_buffer_count = (desc.sync == Sync::DoubleBuffer) ? 2u : 1u;

    for (u32 i = 0; i < out->m_buffer_count; i++) {
        std::optional<ExternalBuffer> buf =
            ExternalBuffer::create(out->m_total_bytes, i == 0 ? "shared instances 0" : "shared instances 1");
        if (!buf) {
            RLOG(LL_ERROR, "SharedInstanceBuffer: ExternalBuffer::create failed");
            return nullptr;
        }
        out->m_buffers[i] = std::move(*buf);
    }

    out->m_render_done = CrossSemaphore::create("render_done");
    if (!out->m_render_done) {
        return nullptr;
    }
    if (desc.sync == Sync::Semaphore) {
        out->m_sim_ready = CrossSemaphore::create("sim_ready");
        if (!out->m_sim_ready) {
            return nullptr;
        }
    }
    return out;
}

BufferHandle SharedInstanceBuffer::consumeBegin() {
    if (m_sync == Sync::Semaphore) {
        // Block the graphics queue until the compute thread's latest tick has landed.
        m_sim_ready->queueWait(m_published_sim.load(std::memory_order_acquire), VK_PIPELINE_STAGE_VERTEX_INPUT_BIT);
        m_frame.fetch_add(1, std::memory_order_acq_rel);
        return m_buffers[0].handle();
    }
    // DoubleBuffer: draw whatever is current; never wait on the compute thread.
    m_frame.fetch_add(1, std::memory_order_acq_rel);
    return m_buffers[m_read_index.load(std::memory_order_acquire)].handle();
}

void SharedInstanceBuffer::consumeEnd() {
    // Signalled after sokol's own frame submit (same queue) -- marks this frame's GPU reads
    // of the buffer as retired once it completes.
    m_render_done->queueSignal(m_frame.load(std::memory_order_acquire));
}

#ifdef GAMEENGINE_CUDA

bool SharedInstanceBuffer::attachCuda() {
    m_cuda = CudaInterop::create();
    if (!m_cuda) {
        return false;
    }
    for (u32 i = 0; i < m_buffer_count; i++) {
        m_device_ptr[i] = m_cuda->mapBuffer(m_buffers[i]);
        if (m_device_ptr[i] == 0) {
            RLOG(LL_ERROR, "SharedInstanceBuffer::attachCuda: mapBuffer failed");
            return false;
        }
    }
    m_render_done_sem = m_cuda->importSemaphore(*m_render_done);
    if (m_render_done_sem < 0) {
        return false;
    }
    if (m_sync == Sync::Semaphore) {
        m_sim_ready_sem = m_cuda->importSemaphore(*m_sim_ready);
        if (m_sim_ready_sem < 0) {
            return false;
        }
        m_write_index = 0;
    } else {
        // Front starts at 0, so the compute thread writes 1 first.
        m_write_index = 1;
    }
    return true;
}

void SharedInstanceBuffer::detachCuda() {
    m_cuda.reset();
    m_device_ptr = {0, 0};
    m_sim_ready_sem = -1;
    m_render_done_sem = -1;
}

CUdeviceptr SharedInstanceBuffer::produceBegin() {
    if (m_sync == Sync::Semaphore) {
        // Wait until every render frame started so far has retired -> the single buffer is
        // nobody's to read. The compute thread then holds it for as many sub-steps as it
        // wants before produceEnd().
        const u64 target = m_frame.load(std::memory_order_acquire);
        m_cuda->waitAsync(m_render_done_sem, target);
        return m_device_ptr[0];
    }
    // DoubleBuffer: the buffer we're about to write was the front until m_flip_frame; wait
    // for the last frame that could have sampled it.
    m_cuda->waitAsync(m_render_done_sem, m_flip_frame);
    return m_device_ptr[m_write_index];
}

void SharedInstanceBuffer::produceEnd() {
    if (m_sync == Sync::Semaphore) {
        m_sim_tick++;
        m_cuda->signalAsync(m_sim_ready_sem, m_sim_tick);
        m_published_sim.store(m_sim_tick, std::memory_order_release);
        return;
    }
    // DoubleBuffer: there is no CUDA->render semaphore, so the kernel writes must be fully
    // landed before the buffer becomes the front the render thread will draw.
    m_cuda->sync();
    m_flip_frame = m_frame.load(std::memory_order_acquire);
    m_read_index.store(m_write_index, std::memory_order_release);
    m_write_index = 1u - m_write_index;
}

#endif // GAMEENGINE_CUDA

} // namespace gpu
