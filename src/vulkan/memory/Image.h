#ifndef VULKAN_MEMORY_IMAGE_H
#define VULKAN_MEMORY_IMAGE_H

#include "Allocator.h"

namespace vulkan {
class LogicalDevice;
class ImageViewKind;

namespace memory {

struct ImageMemoryCreateInfoDefaults
{
  vk::MemoryPropertyFlags     properties{};
  VmaAllocationCreateFlags    vma_allocation_create_flags{};
  VmaMemoryUsage              vma_memory_usage{VMA_MEMORY_USAGE_AUTO};
  VmaAllocationInfo*          allocation_info_out{};
};

// Vulkan Image's parameters container class.
struct Image
{
  LogicalDevice const* m_logical_device{};              // The associated logical device; only valid when m_vh_image is non-null.
  vk::Image m_vh_image;                                 // Vulkan handle to the underlying image, or VK_NULL_HANDLE when no image is represented.
  VmaAllocation m_vh_allocation{};                      // The memory allocation used for the image; only valid when m_vh_image is non-null.

  using MemoryCreateInfo = ImageMemoryCreateInfoDefaults;

  Image() = default;

  Image(
    LogicalDevice const* logical_device,
    vk::Extent2D extent,
    ImageViewKind const& image_view_kind,
    MemoryCreateInfo memory_create_info
    COMMA_CWDEBUG_ONLY(Ambifix const& ambifix));

  Image(Image&& rhs) : m_logical_device(rhs.m_logical_device), m_vh_image(rhs.m_vh_image), m_vh_allocation(rhs.m_vh_allocation)
  {
    rhs.m_vh_image = VK_NULL_HANDLE;
  }

  ~Image()
  {
    destroy();
  }

  Image& operator=(Image&& rhs)
  {
    destroy();
    m_logical_device = rhs.m_logical_device;
    m_vh_image = rhs.m_vh_image;
    m_vh_allocation = rhs.m_vh_allocation;
    rhs.m_vh_image = VK_NULL_HANDLE;
    return *this;
  }

#ifdef CWDEBUG
  void print_on(std::ostream& os) const;
#endif

  [[gnu::always_inline]] inline void* map_memory();
  [[gnu::always_inline]] inline void unmap_memory();

 protected:
  // Free GPU resources.
  inline void destroy();
};

} // namespace memory
} // namespace vulkan

#endif // VULKAN_MEMORY_IMAGE_H

#ifndef VULKAN_LOGICAL_DEVICE_H
#include "LogicalDevice.h"
#endif

#ifndef VULKAN_MEMORY_IMAGE_H_definitions
#define VULKAN_MEMORY_IMAGE_H_definitions

namespace vulkan::memory {

//inline
void* Image::map_memory()
{
  // Don't call map_memory() when m_vh_image is VK_NULL_HANDLE.
  ASSERT(m_vh_image);
  return m_logical_device->map_memory(m_vh_allocation);
}

//inline
void Image::unmap_memory()
{
  // Don't call unmap_memory() when m_vh_image is VK_NULL_HANDLE.
  ASSERT(m_vh_image);
  m_logical_device->unmap_memory(m_vh_allocation);
}

//inline
void Image::destroy()
{
  if (m_vh_image)
    m_logical_device->destroy_image({}, m_vh_image, m_vh_allocation);
  m_vh_image = VK_NULL_HANDLE;
}

} // namespace vulkan::memory

#endif // VULKAN_MEMORY_IMAGE_H_definitions
