#include <gpu/shared_instance_buffer.hpp>

#include <rlog.h>

#include <sokol_gfx.h>
#include <vulkan/vulkan.h>

namespace gpu {

SharedInstanceBuffer::~SharedInstanceBuffer() {
    // consumeBegin/consumeEnd leave empty vkQueueSubmits in flight that wait on / signal
    // m_sim_ready / m_render_done / m_publish_done. Those semaphores are destroyed right
    // after this body (member teardown), so the queue must be idle first -- otherwise the
    // validation layer flags "semaphore currently in use by VkQueue".
    if (sg_query_backend() == SG_BACKEND_VULKAN && sg_isvalid()) {
        const sg_vk_device_info dev = sg_vk_query_device_info();
        if (dev.device != 0) {
            vkDeviceWaitIdle(reinterpret_cast<VkDevice>(static_cast<uintptr_t>(dev.device)));
        }
    }
}

std::unique_ptr<SharedInstanceBuffer> SharedInstanceBuffer::create(const Desc& desc) {
#ifndef GAMEENGINE_CUDA
    (void)desc;
    RLOG(LL_ERROR, "SharedInstanceBuffer: built without CUDA support");
    return nullptr;
#else
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
    switch (desc.sync) {
        case Sync::Semaphore:
            out->m_buffer_count = 1;
            break;
        case Sync::DoubleBuffer:
            out->m_buffer_count = 2;
            break;
        case Sync::SimPriority:
            out->m_buffer_count = 3; // ring the renderer samples; the sim state is separate
            break;
    }

    static const char* kLabels[3] = {"shared instances 0", "shared instances 1", "shared instances 2"};
    for (u32 i = 0; i < out->m_buffer_count; i++) {
        std::optional<ExternalBuffer> buf = ExternalBuffer::create(out->m_total_bytes, kLabels[i]);
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
    if (desc.sync == Sync::SimPriority) {
        out->m_publish_done = CrossSemaphore::create("publish_done");
        if (!out->m_publish_done) {
            return nullptr;
        }
    }
    return out;
#endif // GAMEENGINE_CUDA
}

BufferHandle SharedInstanceBuffer::consumeBegin() {
    if (m_sync == Sync::Semaphore) {
        // Block the graphics queue until the compute thread's latest tick has landed.
        m_sim_ready->queueWait(m_published_sim.load(std::memory_order_acquire), VK_PIPELINE_STAGE_VERTEX_INPUT_BIT);
        m_frame.fetch_add(1, std::memory_order_acq_rel);
        return m_buffers[0].handle();
    }
    if (m_sync == Sync::SimPriority) {
        // Ask for a fresh snapshot for the NEXT frame; draw the newest ready one now,
        // waiting only for the publish copy that filled it -- never for the sim.
        m_want_snapshot.store(true, std::memory_order_release);
        const u32 slot = m_ready_slot.load(std::memory_order_acquire);
        const u64 wait_value = m_published_value.load(std::memory_order_acquire);
        m_publish_done->queueWait(wait_value, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT);
        m_frame.fetch_add(1, std::memory_order_acq_rel);
        return m_buffers[slot].handle();
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
    } else if (m_sync == Sync::DoubleBuffer) {
        m_write_index = 1; // front starts at 0
    } else { // SimPriority
        if (cuMemAlloc(&m_sim_state, m_total_bytes) != CUDA_SUCCESS) {
            RLOG(LL_ERROR, "SharedInstanceBuffer::attachCuda: cuMemAlloc failed");
            return false;
        }
        m_publish_done_sem = m_cuda->importSemaphore(*m_publish_done);
        if (m_publish_done_sem < 0) {
            return false;
        }
        m_sim_stream = m_cuda->makeStream(true);
        if (m_sim_stream == nullptr) {
            return false;
        }
        m_last_slot = 0;
        m_publish_value = 0;
    }
    return true;
}

void SharedInstanceBuffer::detachCuda() {
    if (m_sim_state != 0) {
        cuMemFree(m_sim_state);
        m_sim_state = 0;
    }
    m_sim_stream = nullptr; // owned + destroyed by CudaInterop
    m_pack = nullptr;       // may capture CUfunction handles from the (now-dead) module
    m_cuda.reset();
    m_device_ptr = {0, 0, 0};
    m_sim_ready_sem = -1;
    m_render_done_sem = -1;
    m_publish_done_sem = -1;
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
    if (m_sync == Sync::DoubleBuffer) {
        // The buffer we're about to write was the front until m_flip_frame; wait for the
        // last frame that could have sampled it.
        m_cuda->waitAsync(m_render_done_sem, m_flip_frame);
        return m_device_ptr[m_write_index];
    }
    RLOG(LL_ERROR, "SharedInstanceBuffer::produceBegin: not valid in SimPriority mode");
    return 0;
}

void SharedInstanceBuffer::produceEnd() {
    if (m_sync == Sync::Semaphore) {
        m_sim_tick++;
        m_cuda->signalAsync(m_sim_ready_sem, m_sim_tick);
        m_published_sim.store(m_sim_tick, std::memory_order_release);
        return;
    }
    if (m_sync == Sync::DoubleBuffer) {
        // No CUDA->render semaphore, so the kernel writes must be fully landed before the
        // buffer becomes the front the render thread will draw.
        m_cuda->sync();
        m_flip_frame = m_frame.load(std::memory_order_acquire);
        m_read_index.store(m_write_index, std::memory_order_release);
        m_write_index = 1u - m_write_index;
        return;
    }
    RLOG(LL_ERROR, "SharedInstanceBuffer::produceEnd: not valid in SimPriority mode");
}

bool SharedInstanceBuffer::primeSimState(const void* seed, usize bytes) {
    if (m_sync != Sync::SimPriority || m_sim_state == 0) {
        return false;
    }
    if (bytes > m_total_bytes) {
        bytes = m_total_bytes;
    }
    if (cuMemcpyHtoD(m_sim_state, seed, bytes) != CUDA_SUCCESS) {
        return false;
    }
    // Seed ring slot 0 too so the first frames before any publish aren't garbage.
    if (m_pack) {
        m_pack(m_sim_state, m_device_ptr[0], m_max_instances, m_sim_stream);
        cuStreamSynchronize(m_sim_stream);
    } else if (cuMemcpyDtoD(m_device_ptr[0], m_sim_state, bytes) != CUDA_SUCCESS) {
        return false;
    }
    m_ready_slot.store(0, std::memory_order_release);
    m_published_value.store(0, std::memory_order_release); // publish_done starts at 0 -> first wait passes
    return true;
}

void SharedInstanceBuffer::publishIfRequested() {
    if (!m_want_snapshot.exchange(false, std::memory_order_acq_rel)) {
        return;
    }
    const u32 next = (m_last_slot + 1u) % 3u;
    // Don't overwrite a slot a render frame might still be reading. hostValue() is a plain
    // vkGetSemaphoreCounterValue -- no blocking; a busy ring just re-arms the request.
    if (m_render_done->hostValue() < m_slot_release_frame[next].load(std::memory_order_acquire)) {
        m_want_snapshot.store(true, std::memory_order_release);
        return;
    }

    // Turn the current sim state into `next` on the sim stream (race-free with the in-place
    // kernels queued before it), then signal that slot's readiness.
    if (m_pack) {
        m_pack(m_sim_state, m_device_ptr[next], m_max_instances, m_sim_stream);
    } else {
        cuMemcpyDtoDAsync(m_device_ptr[next], m_sim_state, m_total_bytes, m_sim_stream);
    }
    m_publish_value++;
    m_cuda->signalAsync(m_publish_done_sem, m_publish_value, m_sim_stream);

    // The slot that was ready is recyclable once every frame started so far retires.
    m_slot_release_frame[m_last_slot].store(m_frame.load(std::memory_order_acquire), std::memory_order_release);
    m_last_slot = next;
    m_ready_slot.store(next, std::memory_order_release);
    m_published_value.store(m_publish_value, std::memory_order_release);
}

#endif // GAMEENGINE_CUDA

} // namespace gpu
