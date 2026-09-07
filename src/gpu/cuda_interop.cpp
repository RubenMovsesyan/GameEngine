#ifdef GAMEENGINE_CUDA

#include <gpu/cuda_interop.hpp>

#include <rlog.h>

#include <sokol_gfx.h>
#include <vulkan/vulkan.h>

#include <cstring>
#include <fcntl.h>
#include <unistd.h> // dup()
#include <utility>

namespace gpu {

namespace {

bool cuOk(CUresult r, const char* what) {
    if (r == CUDA_SUCCESS) {
        return true;
    }
    const char* name = nullptr;
    const char* msg = nullptr;
    cuGetErrorName(r, &name);
    cuGetErrorString(r, &msg);
    RLOG(LL_ERROR, "cuda: %s failed: %s -- %s", what, name ? name : "?", msg ? msg : "?");
    return false;
}

// Match the CUDA device to sokol's VkPhysicalDevice by UUID. Returns -1 on no match.
CUdevice pickMatchingDevice() {
    const sg_vk_device_info dev = sg_vk_query_device_info();
    VkPhysicalDevice phys = reinterpret_cast<VkPhysicalDevice>(static_cast<uintptr_t>(dev.physical_device));

    VkPhysicalDeviceIDProperties id_props = {};
    id_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
    VkPhysicalDeviceProperties2 props2 = {};
    props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    props2.pNext = &id_props;
    vkGetPhysicalDeviceProperties2(phys, &props2);

    int count = 0;
    if (!cuOk(cuDeviceGetCount(&count), "cuDeviceGetCount") || count <= 0) {
        return -1;
    }
    for (int i = 0; i < count; i++) {
        CUdevice d = 0;
        if (cuDeviceGet(&d, i) != CUDA_SUCCESS) {
            continue;
        }
        CUuuid uuid = {};
        if (cuDeviceGetUuid_v2(&uuid, d) != CUDA_SUCCESS) {
            continue;
        }
        if (std::memcmp(uuid.bytes, id_props.deviceUUID, VK_UUID_SIZE) == 0) {
            return d;
        }
    }
    return -1;
}

} // namespace

CudaInterop::~CudaInterop() {
    for (CUexternalSemaphore s : m_external_semaphores) {
        cuDestroyExternalSemaphore(s);
    }
    for (CUexternalMemory m : m_external_memory) {
        cuDestroyExternalMemory(m);
    }
    for (CUstream s : m_extra_streams) {
        cuStreamDestroy(s);
    }
    if (m_stream != nullptr) {
        cuStreamDestroy(m_stream);
    }
    if (m_context != nullptr) {
        cuDevicePrimaryCtxRelease(m_device);
    }
}

CudaInterop::CudaInterop(CudaInterop&& o) noexcept
    : m_device(o.m_device), m_context(o.m_context), m_stream(o.m_stream), m_extra_streams(std::move(o.m_extra_streams)),
      m_external_memory(std::move(o.m_external_memory)), m_external_semaphores(std::move(o.m_external_semaphores)) {
    o.m_context = nullptr;
    o.m_stream = nullptr;
    o.m_device = 0;
}

CudaInterop& CudaInterop::operator=(CudaInterop&& o) noexcept {
    if (this != &o) {
        this->~CudaInterop();
        m_device = o.m_device;
        m_context = o.m_context;
        m_stream = o.m_stream;
        m_extra_streams = std::move(o.m_extra_streams);
        m_external_memory = std::move(o.m_external_memory);
        m_external_semaphores = std::move(o.m_external_semaphores);
        o.m_context = nullptr;
        o.m_stream = nullptr;
        o.m_device = 0;
    }
    return *this;
}

std::optional<CudaInterop> CudaInterop::create() {
    if (!cuOk(cuInit(0), "cuInit")) {
        return std::nullopt;
    }
    CUdevice device = pickMatchingDevice();
    if (device < 0) {
        RLOG(LL_ERROR, "cuda: no CUDA device matches the Vulkan physical device UUID");
        return std::nullopt;
    }

    CudaInterop out;
    out.m_device = device;
    if (!cuOk(cuDevicePrimaryCtxRetain(&out.m_context, device), "cuDevicePrimaryCtxRetain")) {
        return std::nullopt;
    }
    if (!cuOk(cuCtxSetCurrent(out.m_context), "cuCtxSetCurrent")) {
        return std::nullopt;
    }
    if (!cuOk(cuStreamCreate(&out.m_stream, CU_STREAM_NON_BLOCKING), "cuStreamCreate")) {
        return std::nullopt;
    }
    return out;
}

CUdeviceptr CudaInterop::mapBuffer(const ExternalBuffer& buffer) {
    const int fd = ::dup(buffer.memoryFd()); // CUDA takes ownership of the fd it's handed
    if (fd < 0) {
        RLOG(LL_ERROR, "cuda: dup(memoryFd) failed");
        return 0;
    }

    CUDA_EXTERNAL_MEMORY_HANDLE_DESC mem_desc = {};
    mem_desc.type = CU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD;
    mem_desc.handle.fd = fd;
    mem_desc.size = buffer.allocationSize();

    CUexternalMemory ext_mem = nullptr;
    if (!cuOk(cuImportExternalMemory(&ext_mem, &mem_desc), "cuImportExternalMemory")) {
        ::close(fd);
        return 0;
    }
    m_external_memory.push_back(ext_mem);

    CUDA_EXTERNAL_MEMORY_BUFFER_DESC buf_desc = {};
    buf_desc.offset = 0;
    buf_desc.size = buffer.sizeBytes();
    buf_desc.flags = 0;

    CUdeviceptr ptr = 0;
    if (!cuOk(cuExternalMemoryGetMappedBuffer(&ptr, ext_mem, &buf_desc), "cuExternalMemoryGetMappedBuffer")) {
        return 0;
    }
    return ptr;
}

int CudaInterop::importSemaphore(const CrossSemaphore& semaphore) {
    const int fd = ::dup(semaphore.fd());
    if (fd < 0) {
        RLOG(LL_ERROR, "cuda: dup(semaphore fd) failed");
        return -1;
    }

    CUDA_EXTERNAL_SEMAPHORE_HANDLE_DESC desc = {};
    desc.type = CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_TIMELINE_SEMAPHORE_FD;
    desc.handle.fd = fd;

    CUexternalSemaphore ext = nullptr;
    if (!cuOk(cuImportExternalSemaphore(&ext, &desc), "cuImportExternalSemaphore")) {
        ::close(fd);
        return -1;
    }
    m_external_semaphores.push_back(ext);
    return static_cast<int>(m_external_semaphores.size()) - 1;
}

void CudaInterop::waitAsync(int semaphore, u64 value) {
    waitAsync(semaphore, value, m_stream);
}

void CudaInterop::signalAsync(int semaphore, u64 value) {
    signalAsync(semaphore, value, m_stream);
}

void CudaInterop::waitAsync(int semaphore, u64 value, CUstream stream) {
    CUDA_EXTERNAL_SEMAPHORE_WAIT_PARAMS params = {};
    params.params.fence.value = value;
    CUexternalSemaphore s = m_external_semaphores[static_cast<usize>(semaphore)];
    cuOk(cuWaitExternalSemaphoresAsync(&s, &params, 1, stream), "cuWaitExternalSemaphoresAsync");
}

void CudaInterop::signalAsync(int semaphore, u64 value, CUstream stream) {
    CUDA_EXTERNAL_SEMAPHORE_SIGNAL_PARAMS params = {};
    params.params.fence.value = value;
    CUexternalSemaphore s = m_external_semaphores[static_cast<usize>(semaphore)];
    cuOk(cuSignalExternalSemaphoresAsync(&s, &params, 1, stream), "cuSignalExternalSemaphoresAsync");
}

void CudaInterop::sync() {
    sync(m_stream);
}

void CudaInterop::sync(CUstream stream) {
    cuOk(cuStreamSynchronize(stream), "cuStreamSynchronize");
}

CUstream CudaInterop::makeStream(bool high_priority) {
    int lo = 0;
    int hi = 0;
    int priority = 0;
    if (high_priority && cuCtxGetStreamPriorityRange(&lo, &hi) == CUDA_SUCCESS) {
        priority = hi; // numerically lowest == highest priority
    }
    CUstream stream = nullptr;
    if (!cuOk(cuStreamCreateWithPriority(&stream, CU_STREAM_NON_BLOCKING, priority), "cuStreamCreateWithPriority")) {
        return nullptr;
    }
    m_extra_streams.push_back(stream);
    return stream;
}

} // namespace gpu

#endif // GAMEENGINE_CUDA
