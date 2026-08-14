#include <gpu/swapchain.hpp>

#include <devtools/rlog.h>

#include <algorithm>

namespace gpu {

Swapchain::Swapchain(const Device& device, vk::SurfaceKHR surface, vk::Extent2D want)
    : m_device(&device), m_surface(surface) {
    const auto formats = device.physical().getSurfaceFormatsKHR(surface);
    m_format = formats[0].format;
    for (const auto& f : formats) {
        if (f.format == vk::Format::eB8G8R8A8Srgb && f.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
            m_format = f.format;
            break;
        }
    }
    build(want);
}

void Swapchain::build(vk::Extent2D want) {
    const auto caps = m_device->physical().getSurfaceCapabilitiesKHR(m_surface);

    // UINT32_MAX means the surface defers to us; otherwise its extent wins.
    m_extent = caps.currentExtent.width == UINT32_MAX
                   ? vk::Extent2D{
                         std::clamp(want.width, caps.minImageExtent.width, caps.maxImageExtent.width),
                         std::clamp(want.height, caps.minImageExtent.height, caps.maxImageExtent.height),
                     }
                   : caps.currentExtent;

    u32 count = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && count > caps.maxImageCount) {
        count = caps.maxImageCount;
    }

    const auto& families = m_device->families();
    const u32 shared[]{families.graphics, families.present};
    const bool concurrent = families.graphics != families.present;

    // Views and semaphores must die before the swapchain they came from.
    m_views.clear();
    m_render_finished.clear();

    m_swapchain = vk::raii::SwapchainKHR{
        **m_device,
        vk::SwapchainCreateInfoKHR{
            .surface = m_surface,
            .minImageCount = count,
            .imageFormat = m_format,
            .imageColorSpace = vk::ColorSpaceKHR::eSrgbNonlinear,
            .imageExtent = m_extent,
            .imageArrayLayers = 1,
            .imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
            .imageSharingMode = concurrent ? vk::SharingMode::eConcurrent : vk::SharingMode::eExclusive,
            .queueFamilyIndexCount = concurrent ? 2u : 0u,
            .pQueueFamilyIndices = concurrent ? shared : nullptr,
            .preTransform = caps.currentTransform,
            .compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque,
            .presentMode = vk::PresentModeKHR::eFifo, // always supported, no tearing
            .clipped = vk::True,
            .oldSwapchain = *m_swapchain,
        },
    };

    m_images = m_swapchain.getImages();
    for (vk::Image image : m_images) {
        m_views.emplace_back(
            **m_device,
            vk::ImageViewCreateInfo{
                .image = image,
                .viewType = vk::ImageViewType::e2D,
                .format = m_format,
                .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
            }
        );
        m_render_finished.emplace_back(**m_device, vk::SemaphoreCreateInfo{});
    }
}

Swapchain::Acquired Swapchain::acquire(vk::Semaphore signal) {
    try {
        const auto [result, index] = m_swapchain.acquireNextImage(UINT64_MAX, signal, nullptr);
        // eSuboptimalKHR: index is valid and the semaphore IS signalled, so render it.
        // Skipping would strand a signalled semaphore; present() asks for the rebuild.
        return {index, result == vk::Result::eSuccess || result == vk::Result::eSuboptimalKHR};
    } catch (const vk::OutOfDateKHRError&) {
        return {0, false}; // semaphore was not signalled on this path
    }
}

bool Swapchain::present(const vk::raii::Queue& queue, u32 image_index) {
    const vk::Semaphore wait = *m_render_finished[image_index];
    const vk::SwapchainKHR swapchain = *m_swapchain;
    try {
        return queue.presentKHR(vk::PresentInfoKHR{
                   .waitSemaphoreCount = 1,
                   .pWaitSemaphores = &wait,
                   .swapchainCount = 1,
                   .pSwapchains = &swapchain,
                   .pImageIndices = &image_index,
               }) == vk::Result::eSuccess;
    } catch (const vk::OutOfDateKHRError&) {
        return false;
    }
}

} // namespace gpu
