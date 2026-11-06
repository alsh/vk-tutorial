#include "device_buffer.hpp"

#include <array>
#include <iostream>
#include <limits>

namespace
{

  struct BufferRangeCase
  {
    char const *               description;
    rendering::BufferByteRange range;
    vk::DeviceSize             bufferSizeBytes;
    bool                       expected;
  };

  [[nodiscard]] bool testBufferRanges()
  {
    constexpr vk::DeviceSize                 MAX_SIZE = std::numeric_limits<vk::DeviceSize>::max();
    constexpr std::array<BufferRangeCase, 8> cases    = {
      BufferRangeCase{ "empty range in empty buffer", { 0, 0 }, 0, true },
      BufferRangeCase{ "empty range at end", { 64, 0 }, 64, true },
      BufferRangeCase{ "empty range beyond end", { 65, 0 }, 64, false },
      BufferRangeCase{ "full range", { 0, 64 }, 64, true },
      BufferRangeCase{ "range ending at buffer end", { 60, 4 }, 64, true },
      BufferRangeCase{ "range beyond buffer end", { 60, 5 }, 64, false },
      BufferRangeCase{ "largest valid range", { MAX_SIZE - 3, 3 }, MAX_SIZE, true },
      BufferRangeCase{ "overflowing range", { MAX_SIZE - 3, 4 }, MAX_SIZE, false },
    };

    for ( BufferRangeCase const & testCase : cases )
    {
      if ( testCase.range.fitsWithin( testCase.bufferSizeBytes ) != testCase.expected )
      {
        std::cerr << "Buffer range test failed: " << testCase.description << '\n';
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] bool testMemoryTypeSelection()
  {
    vk::PhysicalDeviceMemoryProperties memoryProperties{};
    memoryProperties.memoryTypeCount              = 3;
    memoryProperties.memoryTypes[0].propertyFlags = vk::MemoryPropertyFlagBits::eHostVisible;
    memoryProperties.memoryTypes[1].propertyFlags = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eDeviceLocal;
    memoryProperties.memoryTypes[2].propertyFlags = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;

    vk::MemoryPropertyFlags const hostVisible  = vk::MemoryPropertyFlagBits::eHostVisible;
    vk::MemoryPropertyFlags const hostCoherent = vk::MemoryPropertyFlagBits::eHostCoherent;
    auto const                    preferred    = rendering::selectMemoryType( memoryProperties, 0b111, hostVisible, hostCoherent );
    if ( !preferred || preferred->index != 2 || ( preferred->properties & hostCoherent ) != hostCoherent )
    {
      std::cerr << "Memory type test failed: preferred properties were not selected\n";
      return false;
    }

    auto const fallback = rendering::selectMemoryType( memoryProperties, 0b011, hostVisible, hostCoherent );
    if ( !fallback || fallback->index != 0 )
    {
      std::cerr << "Memory type test failed: required-property fallback was not selected\n";
      return false;
    }

    auto const unavailable = rendering::selectMemoryType( memoryProperties, 0b101, hostVisible | vk::MemoryPropertyFlagBits::eDeviceLocal, {} );
    if ( unavailable )
    {
      std::cerr << "Memory type test failed: an unsupported property set was accepted\n";
      return false;
    }
    return true;
  }

}  // namespace

int main()
{
  if ( !testBufferRanges() || !testMemoryTypeSelection() )
  {
    return 1;
  }
  return 0;
}
