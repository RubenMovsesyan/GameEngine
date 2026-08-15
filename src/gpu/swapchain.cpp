#include <gpu/swapchain.hpp>

#include <rlog.h>

#include <algorithm>

namespace gpu {

namespace {
constexpr VkFormat SWAPCHAIN_FORMAT = VK_FORMAT_B8G8R8A8_UNORM;
constexpr VkColorSpaceKHR SWAPCHAIN_COLOR_SPACE = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
} // namespace

Swapchain::Swapchain(const Device& device, VkSurfaceKHR surface, VkExtent2D want) : m_device(&device), m_surface(surface) {
    build(want);
}

Swapchain::~Swapchain() {
    destroySwapchain();
    destroySemaphores();
}

void Swapchain::destroySwapchain() {
    m_textures.clear();
    if (m_swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(m_device->handle(), m_swapchain, nullptr);
        m_swapchain = VK_NULL_HANDLE;
    }
}

void Swapchain::destroySemaphores() {
    for (VkSemaphore s : m_image_available) {
        vkDestroySemaphore(m_device->handle(), s, nullptr);
    }
    for (VkSemaphore s : m_render_finished) {
        vkDestroySemaphore(m_device->handle(), s, nullptr);
    }
    m_image_available.clear();
    m_render_finished.clear();
}

void Swapchain::build(VkExtent2D want) {
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_device->physical(), m_surface, &caps);

    m_extent = caps.currentExtent.width != 0xFFFFFFFF
        ? caps.currentExtent
        : VkExtent2D{
              std::clamp(want.width, caps.minImageExtent.width, caps.maxImageExtent.width),
              std::clamp(want.height, caps.minImageExtent.height, caps.maxImageExtent.height),
          };
    m_format = SWAPCHAIN_FORMAT;

    u32 image_count = caps.minImageCount + 1;
    if (caps.maxImageCount > 0) {
        image_count = std::min(image_count, caps.maxImageCount);
    }

    VkSwapchainCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    info.surface = m_surface;
    info.minImageCount = image_count;
    info.imageFormat = m_format;
    info.imageColorSpace = SWAPCHAIN_COLOR_SPACE;
    info.imageExtent = m_extent;
    info.imageArrayLayers = 1;
    // TRANSFER_DST needed: clearTextureFloat outside a render pass uses vkCmdClearColorImage.
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = caps.currentTransform;
    info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    info.clipped = VK_TRUE;
    info.oldSwapchain = m_swapchain;

    VkSwapchainKHR new_swapchain = VK_NULL_HANDLE;
    if (vkCreateSwapchainKHR(m_device->handle(), &info, nullptr, &new_swapchain) != VK_SUCCESS) {
        RLOG(LL_FATAL, "vkCreateSwapchainKHR failed");
    }
    destroySwapchain();
    m_swapchain = new_swapchain;

    u32 count = 0;
    vkGetSwapchainImagesKHR(m_device->handle(), m_swapchain, &count, nullptr);
    std::vector<VkImage> images(count);
    vkGetSwapchainImagesKHR(m_device->handle(), m_swapchain, &count, images.data());

    nvrhi::TextureDesc desc{};
    desc.width = m_extent.width;
    desc.height = m_extent.height;
    desc.format = nvrhi::Format::BGRA8_UNORM;
    desc.isRenderTarget = true;
    desc.initialState = nvrhi::ResourceStates::Present;
    desc.keepInitialState = true;

    m_textures.reserve(count);
    for (VkImage image : images) {
        desc.debugName = "swapchain image";
        m_textures.push_back(m_device->nvrhi()->createHandleForNativeTexture(nvrhi::ObjectTypes::VK_Image, nvrhi::Object(image), desc));
    }

    // Image count can change across a recreate, so semaphore pools get rebuilt too.
    destroySemaphores();
    m_frame = 0;
    VkSemaphoreCreateInfo semaphore_info{};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    m_image_available.resize(count);
    m_render_finished.resize(count);
    for (u32 i = 0; i < count; ++i) {
        vkCreateSemaphore(m_device->handle(), &semaphore_info, nullptr, &m_image_available[i]);
        vkCreateSemaphore(m_device->handle(), &semaphore_info, nullptr, &m_render_finished[i]);
    }
}

Swapchain::Acquired Swapchain::acquire() {
    m_frame = (m_frame + 1) % static_cast<u32>(m_image_available.size());

    u32 image_index = 0;
    VkResult result = vkAcquireNextImageKHR(m_device->handle(), m_swapchain, UINT64_MAX, m_image_available[m_frame], VK_NULL_HANDLE, &image_index);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        return {0, false};
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        RLOG(LL_FATAL, "vkAcquireNextImageKHR failed");
    }
    return {image_index, true};
}

bool Swapchain::present(u32 image_index) {
    VkSemaphore wait = m_render_finished[image_index];

    VkPresentInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    info.waitSemaphoreCount = 1;
    info.pWaitSemaphores = &wait;
    info.swapchainCount = 1;
    info.pSwapchains = &m_swapchain;
    info.pImageIndices = &image_index;

    VkResult result = vkQueuePresentKHR(m_device->graphicsQueue(), &info);
    return result == VK_SUCCESS;
}

} // namespace gpu
