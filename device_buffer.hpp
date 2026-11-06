#pragma once

#include "vulkan_config.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

namespace rendering
{

  struct BufferByteRange
  {
    vk::DeviceSize offsetBytes = 0;
    vk::DeviceSize sizeBytes   = 0;

    [[nodiscard]] constexpr bool fitsWithin( vk::DeviceSize bufferSizeBytes ) const noexcept
    {
      return offsetBytes <= bufferSizeBytes && sizeBytes <= bufferSizeBytes - offsetBytes;
    }
  };

  struct MemoryTypeSelection
  {
    std::uint32_t           index = 0;
    vk::MemoryPropertyFlags properties;
  };

  [[nodiscard]] std::optional<MemoryTypeSelection> selectMemoryType( vk::PhysicalDeviceMemoryProperties const & memoryProperties,
                                                                     std::uint32_t                              typeFilter,
                                                                     vk::MemoryPropertyFlags                    requiredProperties,
                                                                     vk::MemoryPropertyFlags                    preferredProperties = {} );

  class DeviceBuffer final
  {
  public:
    [[nodiscard]] static DeviceBuffer create( vk::raii::PhysicalDevice const & physicalDevice,
                                              vk::raii::Device const &         device,
                                              vk::DeviceSize                   sizeBytes,
                                              vk::BufferUsageFlags             usage,
                                              vk::MemoryPropertyFlags          requiredProperties,
                                              vk::MemoryPropertyFlags          preferredProperties = {} );

    DeviceBuffer()                                   = delete;
    DeviceBuffer( DeviceBuffer const & )             = delete;
    DeviceBuffer & operator=( DeviceBuffer const & ) = delete;
    DeviceBuffer( DeviceBuffer && other ) noexcept;
    DeviceBuffer & operator=( DeviceBuffer && ) = delete;
    ~DeviceBuffer()                             = default;

    [[nodiscard]] vk::Buffer              handle() const noexcept;
    [[nodiscard]] vk::DeviceSize          sizeBytes() const noexcept;
    [[nodiscard]] vk::DeviceSize          allocationSizeBytes() const noexcept;
    [[nodiscard]] vk::BufferUsageFlags    usage() const noexcept;
    [[nodiscard]] vk::MemoryPropertyFlags memoryProperties() const noexcept;
    [[nodiscard]] bool                    isValid() const noexcept;

    // Validates the range, maps internally, and flushes non-coherent memory.
    // The caller must prevent GPU use of the written range until the write completes.
    // An empty write is a no-op when its offset is within the logical size.
    void writeBytes( std::span<std::byte const> bytes, vk::DeviceSize destinationOffsetBytes = 0 );

    // Records a copy only. The command buffer must belong to this device.
    // The caller owns command-buffer submission, completion, and any consumer barrier.
    void recordCopyTo( vk::CommandBuffer    commandBuffer,
                       DeviceBuffer const & destination,
                       BufferByteRange      sourceRange,
                       vk::DeviceSize       destinationOffsetBytes = 0 ) const;

  private:
    DeviceBuffer( vk::raii::Device const &  device,
                  vk::DeviceSize            sizeBytes,
                  vk::DeviceSize            allocationSizeBytes,
                  vk::BufferUsageFlags      usage,
                  vk::MemoryPropertyFlags   memoryProperties,
                  vk::raii::DeviceMemory && memory,
                  vk::raii::Buffer &&       buffer ) noexcept;

    // Non-owning. The device wrapper must stay at a stable address and outlive this buffer.
    vk::raii::Device const * device_;
    vk::DeviceSize           sizeBytes_;
    vk::DeviceSize           allocationSizeBytes_;
    vk::BufferUsageFlags     usage_;
    vk::MemoryPropertyFlags  memoryProperties_;
    vk::raii::DeviceMemory   memory_;
    vk::raii::Buffer         buffer_;
  };

}  // namespace rendering
