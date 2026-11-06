#include "device_buffer.hpp"

#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

namespace rendering
{

  std::optional<MemoryTypeSelection> selectMemoryType( vk::PhysicalDeviceMemoryProperties const & memoryProperties,
                                                       std::uint32_t                              typeFilter,
                                                       vk::MemoryPropertyFlags                    requiredProperties,
                                                       vk::MemoryPropertyFlags                    preferredProperties )
  {
    std::optional<MemoryTypeSelection> fallback;
    for ( std::uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i )
    {
      if ( ( typeFilter & ( std::uint32_t{ 1 } << i ) ) == 0 )
      {
        continue;
      }

      vk::MemoryPropertyFlags const properties = memoryProperties.memoryTypes[i].propertyFlags;
      if ( ( properties & requiredProperties ) != requiredProperties )
      {
        continue;
      }

      MemoryTypeSelection const selection{ .index = i, .properties = properties };
      if ( ( properties & preferredProperties ) == preferredProperties )
      {
        return selection;
      }
      if ( !fallback )
      {
        fallback = selection;
      }
    }
    return fallback;
  }

  DeviceBuffer::DeviceBuffer( vk::raii::Device const &  device,
                              vk::DeviceSize            sizeBytes,
                              vk::DeviceSize            allocationSizeBytes,
                              vk::BufferUsageFlags      usage,
                              vk::MemoryPropertyFlags   memoryProperties,
                              vk::raii::DeviceMemory && memory,
                              vk::raii::Buffer &&       buffer ) noexcept
    : device_( &device )
    , sizeBytes_( sizeBytes )
    , allocationSizeBytes_( allocationSizeBytes )
    , usage_( usage )
    , memoryProperties_( memoryProperties )
    , memory_( std::move( memory ) )
    , buffer_( std::move( buffer ) )
  {
  }

  DeviceBuffer::DeviceBuffer( DeviceBuffer && other ) noexcept
    : device_( other.device_ )
    , sizeBytes_( other.sizeBytes_ )
    , allocationSizeBytes_( other.allocationSizeBytes_ )
    , usage_( other.usage_ )
    , memoryProperties_( other.memoryProperties_ )
    , memory_( std::move( other.memory_ ) )
    , buffer_( std::move( other.buffer_ ) )
  {
    other.device_              = nullptr;
    other.sizeBytes_           = 0;
    other.allocationSizeBytes_ = 0;
    other.usage_               = {};
    other.memoryProperties_    = {};
  }

  DeviceBuffer DeviceBuffer::create( vk::raii::PhysicalDevice const & physicalDevice,
                                     vk::raii::Device const &         device,
                                     vk::DeviceSize                   sizeBytes,
                                     vk::BufferUsageFlags             usage,
                                     vk::MemoryPropertyFlags          requiredProperties,
                                     vk::MemoryPropertyFlags          preferredProperties )
  {
    if ( sizeBytes == 0 )
    {
      throw std::invalid_argument( "cannot create a zero-size Vulkan buffer" );
    }

    vk::raii::Buffer             buffer       = device.createBuffer( vk::BufferCreateInfo{
      .size        = sizeBytes,
      .usage       = usage,
      .sharingMode = vk::SharingMode::eExclusive,
    } );
    vk::MemoryRequirements const requirements = buffer.getMemoryRequirements();
    auto const memoryType = selectMemoryType( physicalDevice.getMemoryProperties(), requirements.memoryTypeBits, requiredProperties, preferredProperties );
    if ( !memoryType )
    {
      throw std::runtime_error( "no compatible Vulkan memory type is available for the buffer" );
    }

    vk::raii::DeviceMemory memory = device.allocateMemory( vk::MemoryAllocateInfo{
      .allocationSize  = requirements.size,
      .memoryTypeIndex = memoryType->index,
    } );
    buffer.bindMemory( *memory, 0 );

    return DeviceBuffer( device, sizeBytes, requirements.size, usage, memoryType->properties, std::move( memory ), std::move( buffer ) );
  }

  vk::Buffer DeviceBuffer::handle() const noexcept
  {
    return isValid() ? *buffer_ : vk::Buffer{};
  }

  vk::DeviceSize DeviceBuffer::sizeBytes() const noexcept
  {
    return sizeBytes_;
  }

  vk::DeviceSize DeviceBuffer::allocationSizeBytes() const noexcept
  {
    return allocationSizeBytes_;
  }

  vk::BufferUsageFlags DeviceBuffer::usage() const noexcept
  {
    return usage_;
  }

  vk::MemoryPropertyFlags DeviceBuffer::memoryProperties() const noexcept
  {
    return memoryProperties_;
  }

  bool DeviceBuffer::isValid() const noexcept
  {
    return device_ != nullptr && sizeBytes_ != 0;
  }

  void DeviceBuffer::writeBytes( std::span<std::byte const> bytes, vk::DeviceSize destinationOffsetBytes )
  {
    if ( !isValid() )
    {
      throw std::logic_error( "cannot write to an empty or moved-from buffer" );
    }
    if ( std::cmp_greater( bytes.size(), std::numeric_limits<vk::DeviceSize>::max() ) )
    {
      throw std::length_error( "buffer write size exceeds the Vulkan device-size range" );
    }

    BufferByteRange const range{
      .offsetBytes = destinationOffsetBytes,
      .sizeBytes   = static_cast<vk::DeviceSize>( bytes.size() ),
    };
    if ( !range.fitsWithin( sizeBytes_ ) )
    {
      throw std::out_of_range( "buffer write is outside the logical buffer range" );
    }
    if ( bytes.empty() )
    {
      return;
    }
    if ( !( memoryProperties_ & vk::MemoryPropertyFlagBits::eHostVisible ) )
    {
      throw std::logic_error( "cannot map a buffer that is not host-visible" );
    }
    if ( std::cmp_greater( destinationOffsetBytes, std::numeric_limits<std::size_t>::max() ) )
    {
      throw std::length_error( "buffer write offset exceeds the host address range" );
    }

    void * mappedData = memory_.mapMemory( 0, vk::WholeSize );
    try
    {
      auto * mappedBytes = static_cast<std::byte *>( mappedData );
      std::memcpy( mappedBytes + static_cast<std::size_t>( destinationOffsetBytes ), bytes.data(), bytes.size() );
      if ( !( memoryProperties_ & vk::MemoryPropertyFlagBits::eHostCoherent ) )
      {
        vk::MappedMemoryRange range{ .memory = *memory_, .offset = 0, .size = vk::WholeSize };
        device_->flushMappedMemoryRanges( range );
      }
    }
    catch ( ... )
    {
      memory_.unmapMemory();
      throw;
    }
    memory_.unmapMemory();
  }

  void DeviceBuffer::recordCopyTo( vk::CommandBuffer    commandBuffer,
                                   DeviceBuffer const & destination,
                                   BufferByteRange      sourceRange,
                                   vk::DeviceSize       destinationOffsetBytes ) const
  {
    if ( !isValid() || !destination.isValid() )
    {
      throw std::logic_error( "cannot copy from or to an empty or moved-from buffer" );
    }
    if ( device_ != destination.device_ )
    {
      throw std::invalid_argument( "buffer copy resources must belong to the same Vulkan device" );
    }
    if ( !( usage_ & vk::BufferUsageFlagBits::eTransferSrc ) || !( destination.usage_ & vk::BufferUsageFlagBits::eTransferDst ) )
    {
      throw std::invalid_argument( "buffer copy requires transfer-source and transfer-destination usage flags" );
    }
    if ( !sourceRange.fitsWithin( sizeBytes_ ) || !BufferByteRange{ destinationOffsetBytes, sourceRange.sizeBytes }.fitsWithin( destination.sizeBytes_ ) )
    {
      throw std::out_of_range( "buffer copy range is outside a logical buffer range" );
    }
    if ( sourceRange.sizeBytes == 0 )
    {
      return;
    }

    constexpr vk::DeviceSize COPY_ALIGNMENT = 4;
    if ( sourceRange.offsetBytes % COPY_ALIGNMENT != 0 || destinationOffsetBytes % COPY_ALIGNMENT != 0 || sourceRange.sizeBytes % COPY_ALIGNMENT != 0 )
    {
      throw std::invalid_argument( "buffer copy offsets and size must be multiples of four bytes" );
    }
    if ( *buffer_ == *destination.buffer_ )
    {
      throw std::invalid_argument( "self-copy is not supported by DeviceBuffer" );
    }

    commandBuffer.copyBuffer( *buffer_,
                              *destination.buffer_,
                              vk::BufferCopy{
                                .srcOffset = sourceRange.offsetBytes,
                                .dstOffset = destinationOffsetBytes,
                                .size      = sourceRange.sizeBytes,
                              } );
  }

}  // namespace rendering
