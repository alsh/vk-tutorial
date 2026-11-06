#include "device_buffer.hpp"
#include "vulkan_config.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

namespace
{

  constexpr std::uint32_t WIDTH                = 800;
  constexpr std::uint32_t HEIGHT               = 600;
  constexpr std::uint32_t MAX_FRAMES_IN_FLIGHT = 2;
  constexpr char          MODEL_PATH[]         = "viking_room.obj";
  constexpr char          TEXTURE_PATH[]       = "viking_room.png";
  constexpr vk::Format    TEXTURE_FORMAT       = vk::Format::eR8G8B8A8Srgb;

#if defined( NDEBUG )
  constexpr bool ENABLE_VALIDATION_LAYERS = false;
#else
  constexpr bool ENABLE_VALIDATION_LAYERS = true;
#endif

  constexpr std::array<char const *, 1> VALIDATION_LAYERS = { "VK_LAYER_KHRONOS_validation" };
  constexpr std::array<char const *, 1> DEVICE_EXTENSIONS = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

  struct QueueFamilyIndices
  {
    std::optional<std::uint32_t> graphicsFamily;
    std::optional<std::uint32_t> presentFamily;

    [[nodiscard]] bool isComplete() const noexcept
    {
      return graphicsFamily.has_value() && presentFamily.has_value();
    }
  };

  struct SwapChainSupportDetails
  {
    vk::SurfaceCapabilitiesKHR        capabilities;
    std::vector<vk::SurfaceFormatKHR> formats;
    std::vector<vk::PresentModeKHR>   presentModes;
  };

  struct DeviceImage
  {
    vk::raii::DeviceMemory memory{ nullptr };
    vk::raii::Image        image{ nullptr };
    vk::raii::ImageView    view{ nullptr };
  };

  struct SwapChainAttachments
  {
    DeviceImage multisampleColor;
    DeviceImage depth;
  };

  struct FrameSyncObjects
  {
    vk::raii::Semaphore imageAvailableSemaphore{ nullptr };
    vk::raii::Fence     inFlightFence{ nullptr };
  };

  struct Vertex
  {
    glm::vec3 position;
    glm::vec2 texCoord;

    [[nodiscard]] static vk::VertexInputBindingDescription bindingDescription()
    {
      return {
        .binding   = 0,
        .stride    = sizeof( Vertex ),
        .inputRate = vk::VertexInputRate::eVertex,
      };
    }

    [[nodiscard]] static std::array<vk::VertexInputAttributeDescription, 2> attributeDescriptions()
    {
      return {
        vk::VertexInputAttributeDescription{
          .location = 0,
          .binding  = 0,
          .format   = vk::Format::eR32G32B32Sfloat,
          .offset   = offsetof( Vertex, position ),
        },
        vk::VertexInputAttributeDescription{
          .location = 1,
          .binding  = 0,
          .format   = vk::Format::eR32G32Sfloat,
          .offset   = offsetof( Vertex, texCoord ),
        },
      };
    }
  };

  struct UniformBufferObject
  {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
  };

  static_assert( std::is_standard_layout_v<UniformBufferObject> );
  static_assert( sizeof( glm::mat4 ) == 64 );
  static_assert( offsetof( UniformBufferObject, view ) == sizeof( glm::mat4 ) );
  static_assert( offsetof( UniformBufferObject, proj ) == 2 * sizeof( glm::mat4 ) );
  static_assert( sizeof( UniformBufferObject ) == 3 * sizeof( glm::mat4 ) );

  struct GlfwContext
  {
    GlfwContext()
    {
      if ( glfwInit() != GLFW_TRUE )
      {
        throw std::runtime_error( "failed to initialize GLFW" );
      }
    }

    GlfwContext( GlfwContext const & )             = delete;
    GlfwContext & operator=( GlfwContext const & ) = delete;

    ~GlfwContext() noexcept
    {
      glfwTerminate();
    }
  };

  struct WindowDeleter
  {
    void operator()( GLFWwindow * window ) const noexcept
    {
      if ( window != nullptr )
      {
        glfwDestroyWindow( window );
      }
    }
  };

  class HelloTriangleApplication
  {
  public:
    HelloTriangleApplication()
      : instance( nullptr )
      , debugMessenger( nullptr )
      , surface( nullptr )
      , physicalDevice( nullptr )
      , device( nullptr )
      , swapChain( nullptr )
      , textureSampler( nullptr )
      , descriptorSetLayout( nullptr )
      , pipelineLayout( nullptr )
      , descriptorPool( nullptr )
      , renderPass( nullptr )
      , graphicsPipeline( nullptr )
      , commandPool( nullptr )
    {
    }

    void run()
    {
      initWindow();
      try
      {
        initVulkan();
        mainLoop();
        device.waitIdle();
      }
      catch ( ... )
      {
        waitForDeviceIdleAfterFailure();
        throw;
      }
    }

  private:
    struct ParsedObjIndex
    {
      std::size_t position = 0;
      std::size_t texCoord = 0;
    };

    void initWindow()
    {
      glfwWindowHint( GLFW_CLIENT_API, GLFW_NO_API );
      window.reset( glfwCreateWindow( static_cast<int>( WIDTH ), static_cast<int>( HEIGHT ), "Vulkan Tutorial", nullptr, nullptr ) );
      if ( !window )
      {
        throw std::runtime_error( "failed to create the GLFW window" );
      }

      glfwSetWindowUserPointer( window.get(), this );
      glfwSetFramebufferSizeCallback( window.get(), framebufferResizeCallback );
    }

    void initVulkan()
    {
      createInstance();
      createSurface();
      pickPhysicalDevice();
      createLogicalDevice();
      createDescriptorSetLayout();
      createPipelineLayout();
      createCommandPool();
      loadModel();
      createVertexBuffer();
      createIndexBuffer();
      createTextureImage();
      createTextureImageView();
      createTextureSampler();
      createUniformBuffers();
      createDescriptorPool();
      createDescriptorSets();
      if ( !waitForUsableFramebuffer() )
      {
        return;
      }
      createSwapChainResources();
      framebufferResized = false;
      createCommandBuffers();
      createFrameSyncObjects();
    }

    void mainLoop()
    {
      while ( glfwWindowShouldClose( window.get() ) == GLFW_FALSE )
      {
        glfwPollEvents();
        if ( glfwWindowShouldClose( window.get() ) == GLFW_FALSE )
        {
          drawFrame();
        }
      }
    }

    void waitForDeviceIdleAfterFailure() noexcept
    {
      if ( !deviceCreated )
      {
        return;
      }

      try
      {
        device.waitIdle();
      }
      catch ( vk::DeviceLostError const & error )
      {
        std::fprintf( stderr, "Vulkan device was lost during the shutdown wait: %s\n", error.what() );
      }
      catch ( std::exception const & error )
      {
        std::fprintf( stderr, "Vulkan shutdown wait failed; terminating to avoid destroying resources that may still be in use: %s\n", error.what() );
        std::abort();
      }
      catch ( ... )
      {
        std::fprintf( stderr, "Vulkan shutdown wait failed; terminating because GPU completion is unknown\n" );
        std::abort();
      }
    }

    [[nodiscard]] static VKAPI_ATTR vk::Bool32 VKAPI_CALL debugCallback( vk::DebugUtilsMessageSeverityFlagBitsEXT       severity,
                                                                         vk::DebugUtilsMessageTypeFlagsEXT              type,
                                                                         vk::DebugUtilsMessengerCallbackDataEXT const * callbackData,
                                                                         void * ) noexcept
    {
      if ( callbackData != nullptr && callbackData->pMessage != nullptr )
      {
        std::fprintf( stderr,
                      "Vulkan validation (severity 0x%x, type 0x%x): %s\n",
                      static_cast<unsigned int>( severity ),
                      static_cast<unsigned int>( type ),
                      callbackData->pMessage );
      }
      return VK_FALSE;
    }

    [[nodiscard]] std::vector<char const *> getRequiredExtensions() const
    {
      std::uint32_t extensionCount = 0;
      char const ** glfwExtensions = glfwGetRequiredInstanceExtensions( &extensionCount );
      if ( glfwExtensions == nullptr || extensionCount == 0 )
      {
        throw std::runtime_error( "GLFW did not provide the Vulkan surface extensions" );
      }

      std::vector<char const *> extensions( glfwExtensions, glfwExtensions + extensionCount );
      if ( ENABLE_VALIDATION_LAYERS )
      {
        extensions.push_back( VK_EXT_DEBUG_UTILS_EXTENSION_NAME );
      }
      return extensions;
    }

    void verifyInstanceRequirements( std::vector<char const *> const & requiredExtensions ) const
    {
      if ( context.enumerateInstanceVersion() < VK_API_VERSION_1_3 )
      {
        throw std::runtime_error( "the Vulkan loader does not support Vulkan 1.3" );
      }

      if ( ENABLE_VALIDATION_LAYERS )
      {
        auto const availableLayers = context.enumerateInstanceLayerProperties();
        for ( char const * requiredLayer : VALIDATION_LAYERS )
        {
          bool found = false;
          for ( auto const & layer : availableLayers )
          {
            if ( std::string_view( layer.layerName.data() ) == requiredLayer )
            {
              found = true;
              break;
            }
          }
          if ( !found )
          {
            throw std::runtime_error( std::string( "required Vulkan validation layer is unavailable: " ) + requiredLayer );
          }
        }
      }

      auto const availableExtensions = context.enumerateInstanceExtensionProperties();
      for ( char const * requiredExtension : requiredExtensions )
      {
        bool found = false;
        for ( auto const & extension : availableExtensions )
        {
          if ( std::string_view( extension.extensionName.data() ) == requiredExtension )
          {
            found = true;
            break;
          }
        }
        if ( !found )
        {
          throw std::runtime_error( std::string( "required Vulkan instance extension is unavailable: " ) + requiredExtension );
        }
      }
    }

    void createInstance()
    {
      auto const extensions = getRequiredExtensions();
      verifyInstanceRequirements( extensions );

      vk::ApplicationInfo applicationInfo{
        .pApplicationName   = "Vulkan Tutorial",
        .applicationVersion = VK_MAKE_VERSION( 1, 0, 0 ),
        .pEngineName        = "No Engine",
        .engineVersion      = VK_MAKE_VERSION( 1, 0, 0 ),
        .apiVersion         = VK_API_VERSION_1_3,
      };

      vk::DebugUtilsMessengerCreateInfoEXT debugCreateInfo{
        .messageSeverity = vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
        .messageType     = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
                           vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance,
        .pfnUserCallback = debugCallback,
      };

      vk::InstanceCreateInfo createInfo{
        .pNext                   = ENABLE_VALIDATION_LAYERS ? &debugCreateInfo : nullptr,
        .pApplicationInfo        = &applicationInfo,
        .enabledLayerCount       = ENABLE_VALIDATION_LAYERS ? static_cast<std::uint32_t>( VALIDATION_LAYERS.size() ) : 0,
        .ppEnabledLayerNames     = ENABLE_VALIDATION_LAYERS ? VALIDATION_LAYERS.data() : nullptr,
        .enabledExtensionCount   = static_cast<std::uint32_t>( extensions.size() ),
        .ppEnabledExtensionNames = extensions.data(),
      };

      instance = context.createInstance( createInfo );
      if ( ENABLE_VALIDATION_LAYERS )
      {
        debugMessenger = instance.createDebugUtilsMessengerEXT( debugCreateInfo );
      }
    }

    void createSurface()
    {
      VkSurfaceKHR   rawSurface = VK_NULL_HANDLE;
      VkResult const result     = glfwCreateWindowSurface( *instance, window.get(), nullptr, &rawSurface );
      if ( result != VK_SUCCESS )
      {
        throw std::runtime_error( "failed to create the GLFW Vulkan surface" );
      }
      surface = vk::raii::SurfaceKHR( instance, rawSurface );
    }

    [[nodiscard]] QueueFamilyIndices findQueueFamilies( vk::raii::PhysicalDevice const & candidate ) const
    {
      QueueFamilyIndices indices;
      auto const         queueFamilies = candidate.getQueueFamilyProperties();
      for ( std::size_t i = 0; i < queueFamilies.size(); ++i )
      {
        if ( queueFamilies[i].queueCount > 0 && ( queueFamilies[i].queueFlags & vk::QueueFlagBits::eGraphics ) )
        {
          indices.graphicsFamily = static_cast<std::uint32_t>( i );
        }

        if ( queueFamilies[i].queueCount > 0 && candidate.getSurfaceSupportKHR( static_cast<std::uint32_t>( i ), *surface ) )
        {
          indices.presentFamily = static_cast<std::uint32_t>( i );
        }

        if ( indices.isComplete() )
        {
          break;
        }
      }
      return indices;
    }

    [[nodiscard]] SwapChainSupportDetails querySwapChainSupport( vk::raii::PhysicalDevice const & candidate ) const
    {
      return {
        .capabilities = candidate.getSurfaceCapabilitiesKHR( *surface ),
        .formats      = candidate.getSurfaceFormatsKHR( *surface ),
        .presentModes = candidate.getSurfacePresentModesKHR( *surface ),
      };
    }

    [[nodiscard]] bool isDeviceSuitable( vk::raii::PhysicalDevice const & candidate ) const
    {
      if ( candidate.getProperties().apiVersion < VK_API_VERSION_1_3 || !findQueueFamilies( candidate ).isComplete() )
      {
        return false;
      }

      std::set<std::string> missingExtensions( DEVICE_EXTENSIONS.begin(), DEVICE_EXTENSIONS.end() );
      for ( auto const & extension : candidate.enumerateDeviceExtensionProperties() )
      {
        missingExtensions.erase( extension.extensionName.data() );
      }
      if ( !missingExtensions.empty() )
      {
        return false;
      }

      auto const support = querySwapChainSupport( candidate );
      if ( support.formats.empty() || support.presentModes.empty() )
      {
        return false;
      }

      vk::FormatFeatureFlags const requiredTextureFeatures = vk::FormatFeatureFlagBits::eSampledImage | vk::FormatFeatureFlagBits::eSampledImageFilterLinear |
                                                             vk::FormatFeatureFlagBits::eBlitSrc | vk::FormatFeatureFlagBits::eBlitDst;
      auto const                   textureFormatProperties = candidate.getFormatProperties( TEXTURE_FORMAT );
      if ( ( textureFormatProperties.optimalTilingFeatures & requiredTextureFeatures ) != requiredTextureFeatures )
      {
        return false;
      }

      constexpr std::array<vk::Format, 3> depthFormats = {
        vk::Format::eD32Sfloat,
        vk::Format::eD32SfloatS8Uint,
        vk::Format::eD24UnormS8Uint,
      };
      for ( vk::Format format : depthFormats )
      {
        if ( candidate.getFormatProperties( format ).optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment )
        {
          return true;
        }
      }
      return false;
    }

    void pickPhysicalDevice()
    {
      auto const candidates = instance.enumeratePhysicalDevices();
      if ( candidates.empty() )
      {
        throw std::runtime_error( "no Vulkan physical devices are available" );
      }

      for ( auto const & candidate : candidates )
      {
        if ( isDeviceSuitable( candidate ) )
        {
          physicalDevice     = candidate;
          queueFamilyIndices = findQueueFamilies( physicalDevice );
          depthFormat        = chooseDepthFormat();
          msaaSamples        = chooseSampleCount();
          return;
        }
      }
      throw std::runtime_error( "no Vulkan physical device supports the required queues, formats, and swapchain" );
    }

    [[nodiscard]] vk::Format chooseDepthFormat() const
    {
      constexpr std::array<vk::Format, 3> formats = {
        vk::Format::eD32Sfloat,
        vk::Format::eD32SfloatS8Uint,
        vk::Format::eD24UnormS8Uint,
      };
      for ( vk::Format format : formats )
      {
        if ( physicalDevice.getFormatProperties( format ).optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment )
        {
          return format;
        }
      }
      throw std::runtime_error( "no supported depth attachment format is available" );
    }

    [[nodiscard]] vk::SampleCountFlagBits chooseSampleCount() const
    {
      auto const                 properties      = physicalDevice.getProperties();
      vk::SampleCountFlags const supportedCounts = properties.limits.framebufferColorSampleCounts & properties.limits.framebufferDepthSampleCounts;
      constexpr std::array<vk::SampleCountFlagBits, 7> sampleCounts = {
        vk::SampleCountFlagBits::e64, vk::SampleCountFlagBits::e32, vk::SampleCountFlagBits::e16, vk::SampleCountFlagBits::e8,
        vk::SampleCountFlagBits::e4,  vk::SampleCountFlagBits::e2,  vk::SampleCountFlagBits::e1,
      };
      for ( vk::SampleCountFlagBits sampleCount : sampleCounts )
      {
        if ( supportedCounts & sampleCount )
        {
          return sampleCount;
        }
      }
      throw std::runtime_error( "the physical device does not support a usable framebuffer sample count" );
    }

    void createLogicalDevice()
    {
      std::set<std::uint32_t>                uniqueQueueFamilies = { queueFamilyIndices.graphicsFamily.value(), queueFamilyIndices.presentFamily.value() };
      constexpr float                        queuePriority       = 1.0f;
      std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos;
      queueCreateInfos.reserve( uniqueQueueFamilies.size() );
      for ( std::uint32_t queueFamily : uniqueQueueFamilies )
      {
        queueCreateInfos.push_back( vk::DeviceQueueCreateInfo{
          .queueFamilyIndex = queueFamily,
          .queueCount       = 1,
          .pQueuePriorities = &queuePriority,
        } );
      }

      vk::PhysicalDeviceFeatures deviceFeatures{};
      deviceFeatures.samplerAnisotropy = physicalDevice.getFeatures().samplerAnisotropy;
      vk::DeviceCreateInfo createInfo{
        .queueCreateInfoCount    = static_cast<std::uint32_t>( queueCreateInfos.size() ),
        .pQueueCreateInfos       = queueCreateInfos.data(),
        .enabledExtensionCount   = static_cast<std::uint32_t>( DEVICE_EXTENSIONS.size() ),
        .ppEnabledExtensionNames = DEVICE_EXTENSIONS.data(),
        .pEnabledFeatures        = &deviceFeatures,
      };

      device        = vk::raii::Device( physicalDevice, createInfo );
      deviceCreated = true;
      graphicsQueue = device.getQueue( queueFamilyIndices.graphicsFamily.value(), 0 );
      presentQueue  = device.getQueue( queueFamilyIndices.presentFamily.value(), 0 );
    }

    [[nodiscard]] vk::SurfaceFormatKHR chooseSwapSurfaceFormat( std::vector<vk::SurfaceFormatKHR> const & availableFormats ) const
    {
      if ( availableFormats.empty() )
      {
        throw std::runtime_error( "the surface has no supported formats" );
      }

      for ( auto const & format : availableFormats )
      {
        if ( format.format == vk::Format::eB8G8R8A8Srgb && format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear )
        {
          return format;
        }
      }
      return availableFormats.front();
    }

    [[nodiscard]] vk::PresentModeKHR chooseSwapPresentMode( std::vector<vk::PresentModeKHR> const & availableModes ) const
    {
      if ( std::find( availableModes.begin(), availableModes.end(), vk::PresentModeKHR::eMailbox ) != availableModes.end() )
      {
        return vk::PresentModeKHR::eMailbox;
      }
      return vk::PresentModeKHR::eFifo;
    }

    [[nodiscard]] vk::CompositeAlphaFlagBitsKHR chooseCompositeAlpha( vk::CompositeAlphaFlagsKHR supportedAlpha ) const
    {
      constexpr std::array<vk::CompositeAlphaFlagBitsKHR, 4> alphaModes = {
        vk::CompositeAlphaFlagBitsKHR::eOpaque,
        vk::CompositeAlphaFlagBitsKHR::ePreMultiplied,
        vk::CompositeAlphaFlagBitsKHR::ePostMultiplied,
        vk::CompositeAlphaFlagBitsKHR::eInherit,
      };
      for ( vk::CompositeAlphaFlagBitsKHR alphaMode : alphaModes )
      {
        if ( supportedAlpha & alphaMode )
        {
          return alphaMode;
        }
      }
      throw std::runtime_error( "the surface does not support a composite alpha mode" );
    }

    [[nodiscard]] vk::Extent2D chooseSwapExtent( vk::SurfaceCapabilitiesKHR const & capabilities ) const
    {
      if ( capabilities.currentExtent.width != std::numeric_limits<std::uint32_t>::max() )
      {
        return capabilities.currentExtent;
      }

      int width  = 0;
      int height = 0;
      glfwGetFramebufferSize( window.get(), &width, &height );
      vk::Extent2D extent{
        .width  = static_cast<std::uint32_t>( std::max( width, 0 ) ),
        .height = static_cast<std::uint32_t>( std::max( height, 0 ) ),
      };
      extent.width  = std::clamp( extent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width );
      extent.height = std::clamp( extent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height );
      return extent;
    }

    void createSwapChain()
    {
      swapChainSupport                         = querySwapChainSupport( physicalDevice );
      vk::SurfaceFormatKHR const surfaceFormat = chooseSwapSurfaceFormat( swapChainSupport.formats );
      vk::PresentModeKHR const   presentMode   = chooseSwapPresentMode( swapChainSupport.presentModes );
      vk::Extent2D const         extent        = chooseSwapExtent( swapChainSupport.capabilities );

      std::uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
      if ( swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount )
      {
        imageCount = swapChainSupport.capabilities.maxImageCount;
      }

      std::array<std::uint32_t, 2> queueFamilies = {
        queueFamilyIndices.graphicsFamily.value(),
        queueFamilyIndices.presentFamily.value(),
      };
      bool const            separateQueueFamilies = queueFamilies[0] != queueFamilies[1];
      vk::SharingMode const sharingMode           = separateQueueFamilies ? vk::SharingMode::eConcurrent : vk::SharingMode::eExclusive;

      vk::SwapchainCreateInfoKHR createInfo{
        .surface               = *surface,
        .minImageCount         = imageCount,
        .imageFormat           = surfaceFormat.format,
        .imageColorSpace       = surfaceFormat.colorSpace,
        .imageExtent           = extent,
        .imageArrayLayers      = 1,
        .imageUsage            = vk::ImageUsageFlagBits::eColorAttachment,
        .imageSharingMode      = sharingMode,
        .queueFamilyIndexCount = separateQueueFamilies ? static_cast<std::uint32_t>( queueFamilies.size() ) : 0,
        .pQueueFamilyIndices   = separateQueueFamilies ? queueFamilies.data() : nullptr,
        .preTransform          = swapChainSupport.capabilities.currentTransform,
        .compositeAlpha        = chooseCompositeAlpha( swapChainSupport.capabilities.supportedCompositeAlpha ),
        .presentMode           = presentMode,
        .clipped               = vk::True,
      };

      swapChain       = device.createSwapchainKHR( createInfo );
      swapChainImages = swapChain.getImages();
      if ( swapChainImages.empty() )
      {
        throw std::runtime_error( "the created swapchain has no images" );
      }
      swapChainImageFormat = surfaceFormat.format;
      swapChainExtent      = extent;
    }

    [[nodiscard]] vk::raii::ImageView createImageView( vk::Image image, vk::Format format, vk::ImageAspectFlags aspectMask, std::uint32_t mipLevels ) const
    {
      vk::ImageViewCreateInfo createInfo{
      .image = image,
      .viewType = vk::ImageViewType::e2D,
      .format = format,
      .subresourceRange = {
        .aspectMask = aspectMask,
        .baseMipLevel = 0,
        .levelCount = mipLevels,
        .baseArrayLayer = 0,
        .layerCount = 1,
      },
    };
      return device.createImageView( createInfo );
    }

    void createSwapChainImageViews()
    {
      swapChainImageViews.reserve( swapChainImages.size() );
      for ( vk::Image image : swapChainImages )
      {
        swapChainImageViews.emplace_back( createImageView( image, swapChainImageFormat, vk::ImageAspectFlagBits::eColor, 1 ) );
      }
    }

    [[nodiscard]] DeviceImage createImage( std::uint32_t           width,
                                           std::uint32_t           height,
                                           std::uint32_t           mipLevels,
                                           vk::SampleCountFlagBits samples,
                                           vk::Format              format,
                                           vk::ImageUsageFlags     usage,
                                           vk::MemoryPropertyFlags requiredProperties ) const
    {
      if ( width == 0 || height == 0 || mipLevels == 0 )
      {
        throw std::runtime_error( "cannot create a Vulkan image with an empty extent or mip range" );
      }

      vk::raii::Image              image        = device.createImage( vk::ImageCreateInfo{
        .imageType     = vk::ImageType::e2D,
        .format        = format,
        .extent        = { .width = width, .height = height, .depth = 1 },
        .mipLevels     = mipLevels,
        .arrayLayers   = 1,
        .samples       = samples,
        .tiling        = vk::ImageTiling::eOptimal,
        .usage         = usage,
        .sharingMode   = vk::SharingMode::eExclusive,
        .initialLayout = vk::ImageLayout::eUndefined,
      } );
      vk::MemoryRequirements const requirements = image.getMemoryRequirements();
      auto const                   memoryType   = rendering::selectMemoryType(
        physicalDevice.getMemoryProperties(), requirements.memoryTypeBits, requiredProperties, vk::MemoryPropertyFlagBits::eDeviceLocal );
      if ( !memoryType )
      {
        throw std::runtime_error( "no compatible Vulkan memory type is available for the image" );
      }
      vk::raii::DeviceMemory memory = device.allocateMemory( vk::MemoryAllocateInfo{
        .allocationSize  = requirements.size,
        .memoryTypeIndex = memoryType->index,
      } );
      image.bindMemory( *memory, 0 );

      return {
        .memory = std::move( memory ),
        .image  = std::move( image ),
        .view   = nullptr,
      };
    }

    template <typename Recorder>
    void withOneTimeCommandBuffer( Recorder && record )
    {
      auto                    allocatedCommandBuffers = device.allocateCommandBuffers( vk::CommandBufferAllocateInfo{
        .commandPool        = *commandPool,
        .level              = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = 1,
      } );
      vk::raii::CommandBuffer commandBuffer           = std::move( allocatedCommandBuffers.front() );
      commandBuffer.begin( vk::CommandBufferBeginInfo{ .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit } );
      std::forward<Recorder>( record )( *commandBuffer );
      commandBuffer.end();

      vk::CommandBuffer const rawCommandBuffer = *commandBuffer;
      vk::SubmitInfo const    submitInfo{
        .commandBufferCount = 1,
        .pCommandBuffers    = &rawCommandBuffer,
      };
      graphicsQueue.submit( submitInfo );
      try
      {
        graphicsQueue.waitIdle();
      }
      catch ( ... )
      {
        std::exception_ptr const waitError = std::current_exception();
        waitForDeviceIdleAfterFailure();
        std::rethrow_exception( waitError );
      }
    }

    void copyBufferAndWait( rendering::DeviceBuffer const & source, rendering::DeviceBuffer const & destination )
    {
      withOneTimeCommandBuffer(
        [&]( vk::CommandBuffer commandBuffer )
        { source.recordCopyTo( commandBuffer, destination, rendering::BufferByteRange{ .offsetBytes = 0, .sizeBytes = source.sizeBytes() } ); } );
    }

    void createCommandPool()
    {
      commandPool = device.createCommandPool( vk::CommandPoolCreateInfo{
        .flags            = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
        .queueFamilyIndex = queueFamilyIndices.graphicsFamily.value(),
      } );
    }

    void loadModel()
    {
      std::ifstream modelFile( MODEL_PATH );
      if ( !modelFile )
      {
        throw std::runtime_error( std::string( "failed to open model file: " ) + MODEL_PATH );
      }

      std::vector<glm::vec3>                                       positions;
      std::vector<glm::vec2>                                       textureCoordinates;
      std::map<std::pair<std::size_t, std::size_t>, std::uint32_t> uniqueVertices;
      std::string                                                  line;
      std::size_t                                                  lineNumber = 0;

      while ( std::getline( modelFile, line ) )
      {
        ++lineNumber;
        std::istringstream lineStream( line );
        std::string        record;
        if ( !( lineStream >> record ) || record.starts_with( '#' ) )
        {
          continue;
        }

        if ( record == "v" )
        {
          glm::vec3 position{};
          if ( !( lineStream >> position.x >> position.y >> position.z ) )
          {
            throw std::runtime_error( "invalid position in model file at line " + std::to_string( lineNumber ) );
          }
          positions.push_back( position );
        }
        else if ( record == "vt" )
        {
          glm::vec2 textureCoordinate{};
          if ( !( lineStream >> textureCoordinate.x >> textureCoordinate.y ) )
          {
            throw std::runtime_error( "invalid texture coordinate in model file at line " + std::to_string( lineNumber ) );
          }
          textureCoordinates.push_back( textureCoordinate );
        }
        else if ( record == "f" )
        {
          std::vector<ParsedObjIndex> face;
          std::string                 token;
          while ( lineStream >> token )
          {
            if ( token.starts_with( '#' ) )
            {
              break;
            }
            face.push_back( parseObjIndex( token, positions.size(), textureCoordinates.size(), lineNumber ) );
          }
          if ( face.size() < 3 )
          {
            throw std::runtime_error( "model face has fewer than three vertices at line " + std::to_string( lineNumber ) );
          }

          for ( std::size_t corner = 1; corner + 1 < face.size(); ++corner )
          {
            indices.push_back( addModelVertex( face[0], positions, textureCoordinates, uniqueVertices ) );
            indices.push_back( addModelVertex( face[corner], positions, textureCoordinates, uniqueVertices ) );
            indices.push_back( addModelVertex( face[corner + 1], positions, textureCoordinates, uniqueVertices ) );
          }
        }
      }

      if ( modelFile.bad() )
      {
        throw std::runtime_error( std::string( "failed to read model file: " ) + MODEL_PATH );
      }
      if ( vertices.empty() || indices.empty() )
      {
        throw std::runtime_error( std::string( "model file contains no textured triangles: " ) + MODEL_PATH );
      }
      if ( indices.size() > std::numeric_limits<std::uint32_t>::max() )
      {
        throw std::runtime_error( "model has more indices than Vulkan can draw in one indexed call" );
      }
    }

    [[nodiscard]] static std::size_t resolveObjIndex( int index, std::size_t count, std::size_t lineNumber )
    {
      if ( index == 0 )
      {
        throw std::runtime_error( "OBJ indices must not be zero at line " + std::to_string( lineNumber ) );
      }

      std::int64_t resolved = index > 0 ? static_cast<std::int64_t>( index ) - 1 : static_cast<std::int64_t>( count ) + index;
      if ( resolved < 0 || static_cast<std::uint64_t>( resolved ) >= count )
      {
        throw std::runtime_error( "OBJ index is out of range at line " + std::to_string( lineNumber ) );
      }
      return static_cast<std::size_t>( resolved );
    }

    [[nodiscard]] static int parseObjInteger( std::string_view value, std::size_t lineNumber )
    {
      int index               = 0;
      auto const [end, error] = std::from_chars( value.data(), value.data() + value.size(), index );
      if ( error != std::errc{} || end != value.data() + value.size() )
      {
        throw std::runtime_error( "invalid OBJ index at line " + std::to_string( lineNumber ) );
      }
      return index;
    }

    [[nodiscard]] static ParsedObjIndex
      parseObjIndex( std::string_view token, std::size_t positionCount, std::size_t textureCoordinateCount, std::size_t lineNumber )
    {
      std::size_t const firstSlash = token.find( '/' );
      if ( firstSlash == std::string_view::npos )
      {
        throw std::runtime_error( "model face is missing a texture coordinate at line " + std::to_string( lineNumber ) );
      }

      std::size_t const      secondSlash   = token.find( '/', firstSlash + 1 );
      std::string_view const positionToken = token.substr( 0, firstSlash );
      std::string_view const textureToken =
        secondSlash == std::string_view::npos ? token.substr( firstSlash + 1 ) : token.substr( firstSlash + 1, secondSlash - firstSlash - 1 );
      if ( positionToken.empty() || textureToken.empty() )
      {
        throw std::runtime_error( "model face has an incomplete vertex at line " + std::to_string( lineNumber ) );
      }

      return {
        .position = resolveObjIndex( parseObjInteger( positionToken, lineNumber ), positionCount, lineNumber ),
        .texCoord = resolveObjIndex( parseObjInteger( textureToken, lineNumber ), textureCoordinateCount, lineNumber ),
      };
    }

    [[nodiscard]] std::uint32_t addModelVertex( ParsedObjIndex const &                                         index,
                                                std::vector<glm::vec3> const &                                 positions,
                                                std::vector<glm::vec2> const &                                 textureCoordinates,
                                                std::map<std::pair<std::size_t, std::size_t>, std::uint32_t> & uniqueVertices )
    {
      std::pair<std::size_t, std::size_t> const key{ index.position, index.texCoord };
      auto const                                found = uniqueVertices.find( key );
      if ( found != uniqueVertices.end() )
      {
        return found->second;
      }
      if ( vertices.size() >= std::numeric_limits<std::uint32_t>::max() )
      {
        throw std::runtime_error( "model has more unique vertices than Vulkan can index" );
      }

      std::uint32_t const vertexIndex = static_cast<std::uint32_t>( vertices.size() );
      vertices.push_back( Vertex{
        .position = positions[index.position],
        .texCoord = { textureCoordinates[index.texCoord].x, 1.0f - textureCoordinates[index.texCoord].y },
      } );
      uniqueVertices.emplace( key, vertexIndex );
      return vertexIndex;
    }

    void createVertexBuffer()
    {
      if ( vertices.empty() || vertices.size() > std::numeric_limits<vk::DeviceSize>::max() / sizeof( Vertex ) )
      {
        throw std::runtime_error( "model vertex data is empty or too large" );
      }
      vk::DeviceSize const    bufferSize    = static_cast<vk::DeviceSize>( vertices.size() ) * sizeof( Vertex );
      rendering::DeviceBuffer stagingBuffer = rendering::DeviceBuffer::create( physicalDevice,
                                                                               device,
                                                                               bufferSize,
                                                                               vk::BufferUsageFlagBits::eTransferSrc,
                                                                               vk::MemoryPropertyFlagBits::eHostVisible,
                                                                               vk::MemoryPropertyFlagBits::eHostCoherent );
      std::span<Vertex const> vertexData{ vertices };
      stagingBuffer.writeBytes( std::as_bytes( vertexData ) );

      vertexBuffer.emplace( rendering::DeviceBuffer::create( physicalDevice,
                                                             device,
                                                             bufferSize,
                                                             vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer,
                                                             vk::MemoryPropertyFlagBits::eDeviceLocal ) );
      copyBufferAndWait( stagingBuffer, *vertexBuffer );
    }

    void createIndexBuffer()
    {
      if ( indices.empty() || indices.size() > std::numeric_limits<vk::DeviceSize>::max() / sizeof( std::uint32_t ) )
      {
        throw std::runtime_error( "model index data is empty or too large" );
      }
      vk::DeviceSize const           bufferSize    = static_cast<vk::DeviceSize>( indices.size() ) * sizeof( std::uint32_t );
      rendering::DeviceBuffer        stagingBuffer = rendering::DeviceBuffer::create( physicalDevice,
                                                                                      device,
                                                                                      bufferSize,
                                                                                      vk::BufferUsageFlagBits::eTransferSrc,
                                                                                      vk::MemoryPropertyFlagBits::eHostVisible,
                                                                                      vk::MemoryPropertyFlagBits::eHostCoherent );
      std::span<std::uint32_t const> indexData{ indices };
      stagingBuffer.writeBytes( std::as_bytes( indexData ) );

      indexBuffer.emplace( rendering::DeviceBuffer::create( physicalDevice,
                                                            device,
                                                            bufferSize,
                                                            vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer,
                                                            vk::MemoryPropertyFlagBits::eDeviceLocal ) );
      copyBufferAndWait( stagingBuffer, *indexBuffer );
    }

    void transitionImageLayout( vk::CommandBuffer      commandBuffer,
                                vk::Image              image,
                                vk::ImageLayout        oldLayout,
                                vk::ImageLayout        newLayout,
                                vk::AccessFlags        sourceAccess,
                                vk::AccessFlags        destinationAccess,
                                vk::PipelineStageFlags sourceStage,
                                vk::PipelineStageFlags destinationStage,
                                std::uint32_t          baseMipLevel,
                                std::uint32_t          levelCount ) const
    {
      vk::ImageMemoryBarrier const barrier{
      .srcAccessMask = sourceAccess,
      .dstAccessMask = destinationAccess,
      .oldLayout = oldLayout,
      .newLayout = newLayout,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = image,
      .subresourceRange = {
        .aspectMask = vk::ImageAspectFlagBits::eColor,
        .baseMipLevel = baseMipLevel,
        .levelCount = levelCount,
        .baseArrayLayer = 0,
        .layerCount = 1,
      },
    };
      commandBuffer.pipelineBarrier( sourceStage, destinationStage, {}, {}, {}, barrier );
    }

    void copyBufferToImage( vk::CommandBuffer commandBuffer, vk::Buffer buffer, vk::Image image, std::uint32_t width, std::uint32_t height ) const
    {
      vk::BufferImageCopy const region{
      .bufferOffset = 0,
      .bufferRowLength = 0,
      .bufferImageHeight = 0,
      .imageSubresource = {
        .aspectMask = vk::ImageAspectFlagBits::eColor,
        .mipLevel = 0,
        .baseArrayLayer = 0,
        .layerCount = 1,
      },
      .imageOffset = { .x = 0, .y = 0, .z = 0 },
      .imageExtent = { .width = width, .height = height, .depth = 1 },
    };
      commandBuffer.copyBufferToImage( buffer, image, vk::ImageLayout::eTransferDstOptimal, region );
    }

    void generateMipmaps( vk::CommandBuffer commandBuffer, vk::Image image, std::uint32_t width, std::uint32_t height ) const
    {
      std::int32_t mipWidth  = static_cast<std::int32_t>( width );
      std::int32_t mipHeight = static_cast<std::int32_t>( height );

      for ( std::uint32_t level = 1; level < textureMipLevels; ++level )
      {
        transitionImageLayout( commandBuffer,
                               image,
                               vk::ImageLayout::eTransferDstOptimal,
                               vk::ImageLayout::eTransferSrcOptimal,
                               vk::AccessFlagBits::eTransferWrite,
                               vk::AccessFlagBits::eTransferRead,
                               vk::PipelineStageFlagBits::eTransfer,
                               vk::PipelineStageFlagBits::eTransfer,
                               level - 1,
                               1 );

        std::int32_t const nextMipWidth  = std::max( mipWidth / 2, 1 );
        std::int32_t const nextMipHeight = std::max( mipHeight / 2, 1 );
        vk::ImageBlit      blit{};
        blit.srcSubresource = {
          .aspectMask     = vk::ImageAspectFlagBits::eColor,
          .mipLevel       = level - 1,
          .baseArrayLayer = 0,
          .layerCount     = 1,
        };
        blit.srcOffsets[0]  = vk::Offset3D{ .x = 0, .y = 0, .z = 0 };
        blit.srcOffsets[1]  = vk::Offset3D{ .x = mipWidth, .y = mipHeight, .z = 1 };
        blit.dstSubresource = {
          .aspectMask     = vk::ImageAspectFlagBits::eColor,
          .mipLevel       = level,
          .baseArrayLayer = 0,
          .layerCount     = 1,
        };
        blit.dstOffsets[0] = vk::Offset3D{ .x = 0, .y = 0, .z = 0 };
        blit.dstOffsets[1] = vk::Offset3D{ .x = nextMipWidth, .y = nextMipHeight, .z = 1 };
        std::array<vk::ImageBlit, 1> const blits{ blit };
        commandBuffer.blitImage( image, vk::ImageLayout::eTransferSrcOptimal, image, vk::ImageLayout::eTransferDstOptimal, blits, vk::Filter::eLinear );

        transitionImageLayout( commandBuffer,
                               image,
                               vk::ImageLayout::eTransferSrcOptimal,
                               vk::ImageLayout::eShaderReadOnlyOptimal,
                               vk::AccessFlagBits::eTransferRead,
                               vk::AccessFlagBits::eShaderRead,
                               vk::PipelineStageFlagBits::eTransfer,
                               vk::PipelineStageFlagBits::eFragmentShader,
                               level - 1,
                               1 );

        mipWidth  = nextMipWidth;
        mipHeight = nextMipHeight;
      }

      transitionImageLayout( commandBuffer,
                             image,
                             vk::ImageLayout::eTransferDstOptimal,
                             vk::ImageLayout::eShaderReadOnlyOptimal,
                             vk::AccessFlagBits::eTransferWrite,
                             vk::AccessFlagBits::eShaderRead,
                             vk::PipelineStageFlagBits::eTransfer,
                             vk::PipelineStageFlagBits::eFragmentShader,
                             textureMipLevels - 1,
                             1 );
    }

    void createTextureImage()
    {
      int                                                    textureWidth   = 0;
      int                                                    textureHeight  = 0;
      int                                                    sourceChannels = 0;
      std::unique_ptr<stbi_uc, decltype( &stbi_image_free )> pixels( stbi_load( TEXTURE_PATH, &textureWidth, &textureHeight, &sourceChannels, STBI_rgb_alpha ),
                                                                     &stbi_image_free );
      if ( !pixels )
      {
        char const * failureReason = stbi_failure_reason();
        throw std::runtime_error( std::string( "failed to load texture '" ) + TEXTURE_PATH +
                                  "': " + ( failureReason != nullptr ? failureReason : "unknown image error" ) );
      }
      if ( textureWidth <= 0 || textureHeight <= 0 )
      {
        throw std::runtime_error( "texture has an invalid image extent" );
      }

      vk::DeviceSize const     width    = static_cast<vk::DeviceSize>( textureWidth );
      vk::DeviceSize const     height   = static_cast<vk::DeviceSize>( textureHeight );
      constexpr vk::DeviceSize CHANNELS = 4;
      if ( width > std::numeric_limits<vk::DeviceSize>::max() / height / CHANNELS )
      {
        throw std::runtime_error( "texture size exceeds the Vulkan device-size range" );
      }
      vk::DeviceSize const imageSize = width * height * CHANNELS;
      if ( imageSize > std::numeric_limits<std::size_t>::max() )
      {
        throw std::runtime_error( "texture size exceeds the host address range" );
      }

      std::uint32_t const textureWidthUnsigned  = static_cast<std::uint32_t>( textureWidth );
      std::uint32_t const textureHeightUnsigned = static_cast<std::uint32_t>( textureHeight );
      textureMipLevels                          = static_cast<std::uint32_t>( std::bit_width( std::max( textureWidthUnsigned, textureHeightUnsigned ) ) );

      rendering::DeviceBuffer  stagingBuffer = rendering::DeviceBuffer::create( physicalDevice,
                                                                                device,
                                                                                imageSize,
                                                                                vk::BufferUsageFlagBits::eTransferSrc,
                                                                                vk::MemoryPropertyFlagBits::eHostVisible,
                                                                                vk::MemoryPropertyFlagBits::eHostCoherent );
      std::span<stbi_uc const> texturePixels{ pixels.get(), static_cast<std::size_t>( imageSize ) };
      stagingBuffer.writeBytes( std::as_bytes( texturePixels ) );

      textureImage = createImage( textureWidthUnsigned,
                                  textureHeightUnsigned,
                                  textureMipLevels,
                                  vk::SampleCountFlagBits::e1,
                                  TEXTURE_FORMAT,
                                  vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
                                  vk::MemoryPropertyFlagBits::eDeviceLocal );

      vk::Image const image = *textureImage.image;
      withOneTimeCommandBuffer(
        [&]( vk::CommandBuffer commandBuffer )
        {
          transitionImageLayout( commandBuffer,
                                 image,
                                 vk::ImageLayout::eUndefined,
                                 vk::ImageLayout::eTransferDstOptimal,
                                 {},
                                 vk::AccessFlagBits::eTransferWrite,
                                 vk::PipelineStageFlagBits::eTopOfPipe,
                                 vk::PipelineStageFlagBits::eTransfer,
                                 0,
                                 textureMipLevels );
          copyBufferToImage( commandBuffer, stagingBuffer.handle(), image, textureWidthUnsigned, textureHeightUnsigned );
          generateMipmaps( commandBuffer, image, textureWidthUnsigned, textureHeightUnsigned );
        } );
    }

    void createTextureImageView()
    {
      textureImage.view = createImageView( *textureImage.image, TEXTURE_FORMAT, vk::ImageAspectFlagBits::eColor, textureMipLevels );
    }

    void createTextureSampler()
    {
      vk::PhysicalDeviceProperties const properties          = physicalDevice.getProperties();
      bool const                         anisotropySupported = physicalDevice.getFeatures().samplerAnisotropy == vk::True;
      textureSampler                                         = device.createSampler( vk::SamplerCreateInfo{
        .magFilter               = vk::Filter::eLinear,
        .minFilter               = vk::Filter::eLinear,
        .mipmapMode              = vk::SamplerMipmapMode::eLinear,
        .addressModeU            = vk::SamplerAddressMode::eRepeat,
        .addressModeV            = vk::SamplerAddressMode::eRepeat,
        .addressModeW            = vk::SamplerAddressMode::eRepeat,
        .mipLodBias              = 0.0f,
        .anisotropyEnable        = anisotropySupported ? vk::True : vk::False,
        .maxAnisotropy           = anisotropySupported ? properties.limits.maxSamplerAnisotropy : 1.0f,
        .compareEnable           = vk::False,
        .compareOp               = vk::CompareOp::eAlways,
        .minLod                  = 0.0f,
        .maxLod                  = static_cast<float>( textureMipLevels ),
        .borderColor             = vk::BorderColor::eIntOpaqueBlack,
        .unnormalizedCoordinates = vk::False,
      } );
    }

    void createUniformBuffers()
    {
      constexpr vk::DeviceSize bufferSize = sizeof( UniformBufferObject );
      uniformBuffers.reserve( MAX_FRAMES_IN_FLIGHT );
      for ( std::uint32_t frame = 0; frame < MAX_FRAMES_IN_FLIGHT; ++frame )
      {
        uniformBuffers.push_back( rendering::DeviceBuffer::create( physicalDevice,
                                                                   device,
                                                                   bufferSize,
                                                                   vk::BufferUsageFlagBits::eUniformBuffer,
                                                                   vk::MemoryPropertyFlagBits::eHostVisible,
                                                                   vk::MemoryPropertyFlagBits::eHostCoherent ) );
      }
    }

    void createDescriptorSetLayout()
    {
      std::array<vk::DescriptorSetLayoutBinding, 2> bindings = {
        vk::DescriptorSetLayoutBinding{
          .binding         = 0,
          .descriptorType  = vk::DescriptorType::eUniformBuffer,
          .descriptorCount = 1,
          .stageFlags      = vk::ShaderStageFlagBits::eVertex,
        },
        vk::DescriptorSetLayoutBinding{
          .binding         = 1,
          .descriptorType  = vk::DescriptorType::eCombinedImageSampler,
          .descriptorCount = 1,
          .stageFlags      = vk::ShaderStageFlagBits::eFragment,
        },
      };
      descriptorSetLayout = device.createDescriptorSetLayout( vk::DescriptorSetLayoutCreateInfo{
        .bindingCount = static_cast<std::uint32_t>( bindings.size() ),
        .pBindings    = bindings.data(),
      } );
    }

    void createPipelineLayout()
    {
      vk::DescriptorSetLayout const rawLayout = *descriptorSetLayout;
      pipelineLayout                          = device.createPipelineLayout( vk::PipelineLayoutCreateInfo{
        .setLayoutCount = 1,
        .pSetLayouts    = &rawLayout,
      } );
    }

    void createDescriptorPool()
    {
      std::array<vk::DescriptorPoolSize, 2> poolSizes = {
        vk::DescriptorPoolSize{ .type = vk::DescriptorType::eUniformBuffer, .descriptorCount = MAX_FRAMES_IN_FLIGHT },
        vk::DescriptorPoolSize{ .type = vk::DescriptorType::eCombinedImageSampler, .descriptorCount = MAX_FRAMES_IN_FLIGHT },
      };
      descriptorPool = device.createDescriptorPool( vk::DescriptorPoolCreateInfo{
        .flags         = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
        .maxSets       = MAX_FRAMES_IN_FLIGHT,
        .poolSizeCount = static_cast<std::uint32_t>( poolSizes.size() ),
        .pPoolSizes    = poolSizes.data(),
      } );
    }

    void createDescriptorSets()
    {
      std::array<vk::DescriptorSetLayout, MAX_FRAMES_IN_FLIGHT> layouts{};
      layouts.fill( *descriptorSetLayout );
      descriptorSets = device.allocateDescriptorSets( vk::DescriptorSetAllocateInfo{
        .descriptorPool     = *descriptorPool,
        .descriptorSetCount = static_cast<std::uint32_t>( layouts.size() ),
        .pSetLayouts        = layouts.data(),
      } );

      for ( std::size_t frame = 0; frame < descriptorSets.size(); ++frame )
      {
        vk::DescriptorBufferInfo const bufferInfo{
          .buffer = uniformBuffers[frame].handle(),
          .offset = 0,
          .range  = sizeof( UniformBufferObject ),
        };
        vk::DescriptorImageInfo const imageInfo{
          .sampler     = *textureSampler,
          .imageView   = *textureImage.view,
          .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
        };
        std::array<vk::WriteDescriptorSet, 2> writes = {
          vk::WriteDescriptorSet{
            .dstSet          = *descriptorSets[frame],
            .dstBinding      = 0,
            .descriptorCount = 1,
            .descriptorType  = vk::DescriptorType::eUniformBuffer,
            .pBufferInfo     = &bufferInfo,
          },
          vk::WriteDescriptorSet{
            .dstSet          = *descriptorSets[frame],
            .dstBinding      = 1,
            .descriptorCount = 1,
            .descriptorType  = vk::DescriptorType::eCombinedImageSampler,
            .pImageInfo      = &imageInfo,
          },
        };
        device.updateDescriptorSets( writes, {} );
      }
    }

    [[nodiscard]] vk::raii::ShaderModule createShaderModule( std::vector<std::uint32_t> const & code ) const
    {
      vk::ShaderModuleCreateInfo createInfo{
        .codeSize = code.size() * sizeof( std::uint32_t ),
        .pCode    = code.data(),
      };
      return device.createShaderModule( createInfo );
    }

    [[nodiscard]] std::vector<std::uint32_t> readShader( char const * fileName ) const
    {
      std::ifstream file( fileName, std::ios::ate | std::ios::binary );
      if ( !file )
      {
        throw std::runtime_error( std::string( "failed to open shader file: " ) + fileName );
      }

      std::streamoff const fileSize = file.tellg();
      if ( fileSize <= 0 || fileSize % static_cast<std::streamoff>( sizeof( std::uint32_t ) ) != 0 )
      {
        throw std::runtime_error( std::string( "shader file has an invalid SPIR-V size: " ) + fileName );
      }
      if ( static_cast<std::uintmax_t>( fileSize ) > std::numeric_limits<std::size_t>::max() ||
           static_cast<std::uintmax_t>( fileSize ) > static_cast<std::uintmax_t>( std::numeric_limits<std::streamsize>::max() ) )
      {
        throw std::runtime_error( std::string( "shader file is too large: " ) + fileName );
      }

      std::vector<std::uint32_t> code( static_cast<std::size_t>( fileSize ) / sizeof( std::uint32_t ) );
      file.seekg( 0 );
      if ( !file.read( reinterpret_cast<char *>( code.data() ), static_cast<std::streamsize>( fileSize ) ) )
      {
        throw std::runtime_error( std::string( "failed to read shader file: " ) + fileName );
      }
      if ( code.front() != 0x07230203 )
      {
        throw std::runtime_error( std::string( "shader file does not contain SPIR-V data: " ) + fileName );
      }
      return code;
    }

    void createRenderPass()
    {
      std::vector<vk::AttachmentDescription2> attachments;
      attachments.reserve( msaaSamples == vk::SampleCountFlagBits::e1 ? 2 : 3 );

      vk::AttachmentDescription2 colorAttachment{
        .format         = swapChainImageFormat,
        .samples        = msaaSamples,
        .loadOp         = vk::AttachmentLoadOp::eClear,
        .storeOp        = msaaSamples == vk::SampleCountFlagBits::e1 ? vk::AttachmentStoreOp::eStore : vk::AttachmentStoreOp::eDontCare,
        .stencilLoadOp  = vk::AttachmentLoadOp::eDontCare,
        .stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
        .initialLayout  = vk::ImageLayout::eUndefined,
        .finalLayout    = msaaSamples == vk::SampleCountFlagBits::e1 ? vk::ImageLayout::ePresentSrcKHR : vk::ImageLayout::eColorAttachmentOptimal,
      };
      attachments.push_back( colorAttachment );

      vk::AttachmentDescription2 depthAttachment{
        .format         = depthFormat,
        .samples        = msaaSamples,
        .loadOp         = vk::AttachmentLoadOp::eClear,
        .storeOp        = vk::AttachmentStoreOp::eDontCare,
        .stencilLoadOp  = vk::AttachmentLoadOp::eDontCare,
        .stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
        .initialLayout  = vk::ImageLayout::eUndefined,
        .finalLayout    = vk::ImageLayout::eDepthStencilAttachmentOptimal,
      };
      attachments.push_back( depthAttachment );

      vk::AttachmentReference2 colorAttachmentReference{
        .attachment = 0,
        .layout     = vk::ImageLayout::eColorAttachmentOptimal,
        .aspectMask = vk::ImageAspectFlagBits::eColor,
      };
      vk::AttachmentReference2 depthAttachmentReference{
        .attachment = 1,
        .layout     = vk::ImageLayout::eDepthStencilAttachmentOptimal,
        .aspectMask = vk::ImageAspectFlagBits::eDepth,
      };

      vk::AttachmentReference2 resolveAttachmentReference{};
      if ( msaaSamples != vk::SampleCountFlagBits::e1 )
      {
        attachments.push_back( vk::AttachmentDescription2{
          .format         = swapChainImageFormat,
          .samples        = vk::SampleCountFlagBits::e1,
          .loadOp         = vk::AttachmentLoadOp::eDontCare,
          .storeOp        = vk::AttachmentStoreOp::eStore,
          .stencilLoadOp  = vk::AttachmentLoadOp::eDontCare,
          .stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
          .initialLayout  = vk::ImageLayout::eUndefined,
          .finalLayout    = vk::ImageLayout::ePresentSrcKHR,
        } );
        resolveAttachmentReference = vk::AttachmentReference2{
          .attachment = 2,
          .layout     = vk::ImageLayout::eColorAttachmentOptimal,
          .aspectMask = vk::ImageAspectFlagBits::eColor,
        };
      }

      vk::SubpassDescription2 subpass{
        .pipelineBindPoint       = vk::PipelineBindPoint::eGraphics,
        .colorAttachmentCount    = 1,
        .pColorAttachments       = &colorAttachmentReference,
        .pResolveAttachments     = msaaSamples == vk::SampleCountFlagBits::e1 ? nullptr : &resolveAttachmentReference,
        .pDepthStencilAttachment = &depthAttachmentReference,
      };
      vk::SubpassDependency2 dependency{
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        .srcStageMask =
          vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests,
        .dstStageMask =
          vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests,
        .srcAccessMask   = vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite,
        .dstAccessMask   = vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite,
        .dependencyFlags = vk::DependencyFlagBits::eByRegion,
      };
      renderPass = device.createRenderPass2( vk::RenderPassCreateInfo2{
        .attachmentCount = static_cast<std::uint32_t>( attachments.size() ),
        .pAttachments    = attachments.data(),
        .subpassCount    = 1,
        .pSubpasses      = &subpass,
        .dependencyCount = 1,
        .pDependencies   = &dependency,
      } );
    }

    void createSwapChainAttachments()
    {
      swapChainAttachments.reserve( swapChainImages.size() );
      for ( std::size_t i = 0; i < swapChainImages.size(); ++i )
      {
        SwapChainAttachments imageAttachments;
        if ( msaaSamples != vk::SampleCountFlagBits::e1 )
        {
          imageAttachments.multisampleColor = createImage( swapChainExtent.width,
                                                           swapChainExtent.height,
                                                           1,
                                                           msaaSamples,
                                                           swapChainImageFormat,
                                                           vk::ImageUsageFlagBits::eTransientAttachment | vk::ImageUsageFlagBits::eColorAttachment,
                                                           vk::MemoryPropertyFlagBits::eDeviceLocal );
          imageAttachments.multisampleColor.view =
            createImageView( *imageAttachments.multisampleColor.image, swapChainImageFormat, vk::ImageAspectFlagBits::eColor, 1 );
        }

        imageAttachments.depth      = createImage( swapChainExtent.width,
                                                   swapChainExtent.height,
                                                   1,
                                                   msaaSamples,
                                                   depthFormat,
                                                   vk::ImageUsageFlagBits::eDepthStencilAttachment,
                                                   vk::MemoryPropertyFlagBits::eDeviceLocal );
        imageAttachments.depth.view = createImageView( *imageAttachments.depth.image, depthFormat, vk::ImageAspectFlagBits::eDepth, 1 );
        swapChainAttachments.push_back( std::move( imageAttachments ) );
      }
    }

    void createGraphicsPipeline()
    {
      std::vector<std::uint32_t> const vertexShaderCode     = readShader( "triangle.spv" );
      std::vector<std::uint32_t> const fragmentShaderCode   = readShader( "red_triangle.spv" );
      vk::raii::ShaderModule           vertexShaderModule   = createShaderModule( vertexShaderCode );
      vk::raii::ShaderModule           fragmentShaderModule = createShaderModule( fragmentShaderCode );

      std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages = {
        vk::PipelineShaderStageCreateInfo{
          .stage  = vk::ShaderStageFlagBits::eVertex,
          .module = *vertexShaderModule,
          .pName  = "main",
        },
        vk::PipelineShaderStageCreateInfo{
          .stage  = vk::ShaderStageFlagBits::eFragment,
          .module = *fragmentShaderModule,
          .pName  = "main",
        },
      };

      vk::VertexInputBindingDescription const bindingDescription    = Vertex::bindingDescription();
      auto const                              attributeDescriptions = Vertex::attributeDescriptions();
      vk::PipelineVertexInputStateCreateInfo  vertexInputInfo{
        .vertexBindingDescriptionCount   = 1,
        .pVertexBindingDescriptions      = &bindingDescription,
        .vertexAttributeDescriptionCount = static_cast<std::uint32_t>( attributeDescriptions.size() ),
        .pVertexAttributeDescriptions    = attributeDescriptions.data(),
      };
      vk::PipelineInputAssemblyStateCreateInfo inputAssembly{
        .topology               = vk::PrimitiveTopology::eTriangleList,
        .primitiveRestartEnable = vk::False,
      };
      vk::PipelineViewportStateCreateInfo viewportState{
        .viewportCount = 1,
        .scissorCount  = 1,
      };
      vk::PipelineRasterizationStateCreateInfo rasterizer{
        .depthClampEnable        = vk::False,
        .rasterizerDiscardEnable = vk::False,
        .polygonMode             = vk::PolygonMode::eFill,
        .cullMode                = vk::CullModeFlagBits::eBack,
        .frontFace               = vk::FrontFace::eCounterClockwise,
        .depthBiasEnable         = vk::False,
        .lineWidth               = 1.0f,
      };
      vk::PipelineMultisampleStateCreateInfo multisampling{
        .rasterizationSamples  = msaaSamples,
        .sampleShadingEnable   = vk::False,
        .minSampleShading      = 1.0f,
        .alphaToCoverageEnable = vk::False,
        .alphaToOneEnable      = vk::False,
      };
      vk::PipelineDepthStencilStateCreateInfo depthStencil{
        .depthTestEnable       = vk::True,
        .depthWriteEnable      = vk::True,
        .depthCompareOp        = vk::CompareOp::eLess,
        .depthBoundsTestEnable = vk::False,
        .stencilTestEnable     = vk::False,
      };
      vk::PipelineColorBlendAttachmentState colorBlendAttachment{
        .blendEnable         = vk::False,
        .srcColorBlendFactor = vk::BlendFactor::eOne,
        .dstColorBlendFactor = vk::BlendFactor::eZero,
        .colorBlendOp        = vk::BlendOp::eAdd,
        .srcAlphaBlendFactor = vk::BlendFactor::eOne,
        .dstAlphaBlendFactor = vk::BlendFactor::eZero,
        .alphaBlendOp        = vk::BlendOp::eAdd,
        .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
      };
      vk::PipelineColorBlendStateCreateInfo colorBlending{
        .logicOpEnable   = vk::False,
        .logicOp         = vk::LogicOp::eCopy,
        .attachmentCount = 1,
        .pAttachments    = &colorBlendAttachment,
      };
      constexpr std::array<vk::DynamicState, 2> dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
      vk::PipelineDynamicStateCreateInfo        dynamicState{
        .dynamicStateCount = static_cast<std::uint32_t>( dynamicStates.size() ),
        .pDynamicStates    = dynamicStates.data(),
      };

      graphicsPipeline = device.createGraphicsPipeline( nullptr,
                                                        vk::GraphicsPipelineCreateInfo{
                                                          .stageCount          = static_cast<std::uint32_t>( shaderStages.size() ),
                                                          .pStages             = shaderStages.data(),
                                                          .pVertexInputState   = &vertexInputInfo,
                                                          .pInputAssemblyState = &inputAssembly,
                                                          .pViewportState      = &viewportState,
                                                          .pRasterizationState = &rasterizer,
                                                          .pMultisampleState   = &multisampling,
                                                          .pDepthStencilState  = &depthStencil,
                                                          .pColorBlendState    = &colorBlending,
                                                          .pDynamicState       = &dynamicState,
                                                          .layout              = *pipelineLayout,
                                                          .renderPass          = *renderPass,
                                                          .subpass             = 0,
                                                        } );
    }

    void createFramebuffers()
    {
      swapChainFramebuffers.reserve( swapChainImageViews.size() );
      for ( std::size_t i = 0; i < swapChainImageViews.size(); ++i )
      {
        std::array<vk::ImageView, 3> attachments{};
        std::uint32_t                attachmentCount = 0;
        if ( msaaSamples != vk::SampleCountFlagBits::e1 )
        {
          attachments[0]  = *swapChainAttachments[i].multisampleColor.view;
          attachments[1]  = *swapChainAttachments[i].depth.view;
          attachments[2]  = *swapChainImageViews[i];
          attachmentCount = 3;
        }
        else
        {
          attachments[0]  = *swapChainImageViews[i];
          attachments[1]  = *swapChainAttachments[i].depth.view;
          attachmentCount = 2;
        }

        swapChainFramebuffers.emplace_back( device.createFramebuffer( vk::FramebufferCreateInfo{
          .renderPass      = *renderPass,
          .attachmentCount = attachmentCount,
          .pAttachments    = attachments.data(),
          .width           = swapChainExtent.width,
          .height          = swapChainExtent.height,
          .layers          = 1,
        } ) );
      }
    }

    void createSwapChainResources()
    {
      createSwapChain();
      createSwapChainImageViews();
      createRenderPass();
      createSwapChainAttachments();
      createGraphicsPipeline();
      createFramebuffers();
      createRenderFinishedSemaphores();
    }

    void createCommandBuffers()
    {
      commandBuffers = device.allocateCommandBuffers( vk::CommandBufferAllocateInfo{
        .commandPool        = *commandPool,
        .level              = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = MAX_FRAMES_IN_FLIGHT,
      } );
    }

    void recordCommandBuffer( vk::CommandBuffer commandBuffer, std::uint32_t imageIndex, std::uint32_t frameIndex )
    {
      commandBuffer.begin( vk::CommandBufferBeginInfo{} );

      std::array<vk::ClearValue, 2> clearValues{};
      clearValues[0].color        = vk::ClearColorValue{ std::array<float, 4>{ 0.0f, 0.0f, 0.0f, 1.0f } };
      clearValues[1].depthStencil = vk::ClearDepthStencilValue{ .depth = 1.0f, .stencil = 0 };
      vk::RenderPassBeginInfo renderPassInfo{
      .renderPass = *renderPass,
      .framebuffer = *swapChainFramebuffers[imageIndex],
      .renderArea = {
        .offset = { .x = 0, .y = 0 },
        .extent = swapChainExtent,
      },
      .clearValueCount = static_cast<std::uint32_t>( clearValues.size() ),
      .pClearValues = clearValues.data(),
    };
      commandBuffer.beginRenderPass( renderPassInfo, vk::SubpassContents::eInline );
      commandBuffer.bindPipeline( vk::PipelineBindPoint::eGraphics, *graphicsPipeline );

      std::array<vk::Buffer, 1>               vertexBuffers = { vertexBuffer->handle() };
      constexpr std::array<vk::DeviceSize, 1> offsets       = { 0 };
      commandBuffer.bindVertexBuffers( 0, vertexBuffers, offsets );
      commandBuffer.bindIndexBuffer( indexBuffer->handle(), 0, vk::IndexType::eUint32 );

      vk::DescriptorSet const descriptorSet = *descriptorSets[frameIndex];
      commandBuffer.bindDescriptorSets( vk::PipelineBindPoint::eGraphics, *pipelineLayout, 0, 1, &descriptorSet, 0, nullptr );

      vk::Viewport const viewport{
        .x        = 0.0f,
        .y        = 0.0f,
        .width    = static_cast<float>( swapChainExtent.width ),
        .height   = static_cast<float>( swapChainExtent.height ),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
      };
      commandBuffer.setViewport( 0, viewport );
      vk::Rect2D const scissor{
        .offset = { .x = 0, .y = 0 },
        .extent = swapChainExtent,
      };
      commandBuffer.setScissor( 0, scissor );
      commandBuffer.drawIndexed( static_cast<std::uint32_t>( indices.size() ), 1, 0, 0, 0 );
      commandBuffer.endRenderPass();
      commandBuffer.end();
    }

    void createFrameSyncObjects()
    {
      frameSyncObjects.reserve( MAX_FRAMES_IN_FLIGHT );
      for ( std::uint32_t frame = 0; frame < MAX_FRAMES_IN_FLIGHT; ++frame )
      {
        frameSyncObjects.push_back( FrameSyncObjects{
          .imageAvailableSemaphore = device.createSemaphore( vk::SemaphoreCreateInfo{} ),
          .inFlightFence           = device.createFence( vk::FenceCreateInfo{ .flags = vk::FenceCreateFlagBits::eSignaled } ),
        } );
      }
    }

    void createRenderFinishedSemaphores()
    {
      renderFinishedSemaphores.clear();
      renderFinishedSemaphores.reserve( swapChainImages.size() );
      for ( std::size_t image = 0; image < swapChainImages.size(); ++image )
      {
        renderFinishedSemaphores.push_back( device.createSemaphore( vk::SemaphoreCreateInfo{} ) );
      }
    }

    void updateUniformBuffer( std::uint32_t frameIndex )
    {
      auto const          now            = std::chrono::steady_clock::now();
      float const         elapsedSeconds = std::chrono::duration<float>( now - startTime ).count();
      UniformBufferObject uniformBufferObject{
        .model = glm::rotate( glm::mat4( 1.0f ), glm::radians( 90.0f ) * elapsedSeconds, glm::vec3( 0.0f, 0.0f, 1.0f ) ),
        .view  = glm::lookAt( glm::vec3( 2.0f, 2.0f, 2.0f ), glm::vec3( 0.0f, 0.0f, 0.0f ), glm::vec3( 0.0f, 0.0f, 1.0f ) ),
        .proj =
          glm::perspective( glm::radians( 45.0f ), static_cast<float>( swapChainExtent.width ) / static_cast<float>( swapChainExtent.height ), 0.1f, 10.0f ),
      };
      uniformBufferObject.proj[1][1] *= -1.0f;
      uniformBuffers[frameIndex].writeBytes( std::as_bytes( std::span<UniformBufferObject const>{ &uniformBufferObject, 1 } ) );
    }

    [[nodiscard]] bool waitForUsableFramebuffer()
    {
      int width  = 0;
      int height = 0;
      glfwGetFramebufferSize( window.get(), &width, &height );
      while ( ( width == 0 || height == 0 ) && glfwWindowShouldClose( window.get() ) == GLFW_FALSE )
      {
        glfwWaitEvents();
        glfwGetFramebufferSize( window.get(), &width, &height );
      }
      return glfwWindowShouldClose( window.get() ) == GLFW_FALSE;
    }

    void recreateSwapChain()
    {
      if ( !waitForUsableFramebuffer() )
      {
        return;
      }

      device.waitIdle();
      for ( auto & commandBuffer : commandBuffers )
      {
        commandBuffer.reset();
      }

      swapChainFramebuffers.clear();
      graphicsPipeline.clear();
      renderPass.clear();
      swapChainAttachments.clear();
      swapChainImageViews.clear();
      swapChainImages.clear();
      renderFinishedSemaphores.clear();
      swapChain.clear();

      createSwapChainResources();
      framebufferResized = false;
    }

    void drawFrame()
    {
      if ( framebufferResized )
      {
        recreateSwapChain();
        return;
      }

      FrameSyncObjects &             frameSync  = frameSyncObjects[currentFrame];
      std::array<vk::Fence, 1> const frameFence = { *frameSync.inFlightFence };
      vk::Result const               waitResult = device.waitForFences( frameFence, vk::True, std::numeric_limits<std::uint64_t>::max() );
      if ( waitResult != vk::Result::eSuccess )
      {
        throw std::runtime_error( "failed to wait for the current frame fence" );
      }

      vk::ResultValue<std::uint32_t> acquireResult = [&]()
      {
        try
        {
          return swapChain.acquireNextImage( std::numeric_limits<std::uint64_t>::max(), *frameSync.imageAvailableSemaphore, vk::Fence{} );
        }
        catch ( vk::OutOfDateKHRError const & )
        {
          return vk::ResultValue<std::uint32_t>{ vk::Result::eErrorOutOfDateKHR, 0 };
        }
      }();
      if ( acquireResult.result == vk::Result::eErrorOutOfDateKHR )
      {
        recreateSwapChain();
        return;
      }
      if ( acquireResult.result != vk::Result::eSuccess && acquireResult.result != vk::Result::eSuboptimalKHR )
      {
        throw std::runtime_error( "failed to acquire a swapchain image" );
      }

      std::uint32_t const imageIndex = acquireResult.value;
      updateUniformBuffer( currentFrame );
      commandBuffers[currentFrame].reset();
      recordCommandBuffer( *commandBuffers[currentFrame], imageIndex, currentFrame );

      vk::Semaphore const          imageAvailable = *frameSync.imageAvailableSemaphore;
      vk::Semaphore const          renderFinished = *renderFinishedSemaphores[imageIndex];
      vk::CommandBuffer const      commandBuffer  = *commandBuffers[currentFrame];
      vk::PipelineStageFlags const waitStage      = vk::PipelineStageFlagBits::eColorAttachmentOutput;
      vk::SubmitInfo const         submitInfo{
        .waitSemaphoreCount   = 1,
        .pWaitSemaphores      = &imageAvailable,
        .pWaitDstStageMask    = &waitStage,
        .commandBufferCount   = 1,
        .pCommandBuffers      = &commandBuffer,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores    = &renderFinished,
      };

      device.resetFences( frameFence );
      graphicsQueue.submit( submitInfo, *frameSync.inFlightFence );

      vk::PresentInfoKHR const presentInfo{
        .waitSemaphoreCount = 1,
        .pWaitSemaphores    = &renderFinished,
        .swapchainCount     = 1,
        .pSwapchains        = &*swapChain,
        .pImageIndices      = &imageIndex,
      };

      bool shouldRecreate = acquireResult.result == vk::Result::eSuboptimalKHR;
      try
      {
        vk::Result const presentResult = presentQueue.presentKHR( presentInfo );
        if ( presentResult == vk::Result::eSuboptimalKHR || presentResult == vk::Result::eErrorOutOfDateKHR )
        {
          shouldRecreate = true;
        }
        else if ( presentResult != vk::Result::eSuccess )
        {
          throw std::runtime_error( "failed to present the swapchain image" );
        }
      }
      catch ( vk::OutOfDateKHRError const & )
      {
        shouldRecreate = true;
      }

      currentFrame = ( currentFrame + 1 ) % MAX_FRAMES_IN_FLIGHT;
      if ( shouldRecreate || framebufferResized )
      {
        recreateSwapChain();
      }
    }

    static void framebufferResizeCallback( GLFWwindow * window, int, int ) noexcept
    {
      auto * application = static_cast<HelloTriangleApplication *>( glfwGetWindowUserPointer( window ) );
      if ( application != nullptr )
      {
        application->framebufferResized = true;
      }
    }

    GlfwContext                                glfwContext;
    std::unique_ptr<GLFWwindow, WindowDeleter> window;
    vk::raii::Context                          context;
    vk::raii::Instance                         instance;
    vk::raii::DebugUtilsMessengerEXT           debugMessenger;
    vk::raii::SurfaceKHR                       surface;
    vk::raii::PhysicalDevice                   physicalDevice;
    QueueFamilyIndices                         queueFamilyIndices;
    vk::raii::Device                           device;
    vk::raii::Queue                            graphicsQueue{ nullptr };
    vk::raii::Queue                            presentQueue{ nullptr };
    bool                                       deviceCreated = false;

    vk::raii::SwapchainKHR            swapChain;
    std::vector<vk::Image>            swapChainImages;
    std::vector<vk::raii::ImageView>  swapChainImageViews;
    std::vector<SwapChainAttachments> swapChainAttachments;
    vk::Format                        swapChainImageFormat = vk::Format::eUndefined;
    vk::Extent2D                      swapChainExtent{};
    SwapChainSupportDetails           swapChainSupport;
    vk::Format                        depthFormat = vk::Format::eUndefined;
    vk::SampleCountFlagBits           msaaSamples = vk::SampleCountFlagBits::e1;

    std::optional<rendering::DeviceBuffer> vertexBuffer;
    std::optional<rendering::DeviceBuffer> indexBuffer;
    std::vector<rendering::DeviceBuffer>   uniformBuffers;
    DeviceImage                            textureImage;
    vk::raii::Sampler                      textureSampler;
    std::uint32_t                          textureMipLevels = 1;
    std::vector<Vertex>                    vertices;
    std::vector<std::uint32_t>             indices;

    vk::raii::DescriptorSetLayout         descriptorSetLayout;
    vk::raii::PipelineLayout              pipelineLayout;
    vk::raii::DescriptorPool              descriptorPool;
    std::vector<vk::raii::DescriptorSet>  descriptorSets;
    vk::raii::RenderPass                  renderPass;
    vk::raii::Pipeline                    graphicsPipeline;
    std::vector<vk::raii::Framebuffer>    swapChainFramebuffers;
    vk::raii::CommandPool                 commandPool;
    std::vector<vk::raii::CommandBuffer>  commandBuffers;
    std::vector<FrameSyncObjects>         frameSyncObjects;
    std::vector<vk::raii::Semaphore>      renderFinishedSemaphores;
    bool                                  framebufferResized = false;
    std::uint32_t                         currentFrame       = 0;
    std::chrono::steady_clock::time_point startTime          = std::chrono::steady_clock::now();
  };

}  // namespace

int main()
{
  try
  {
    HelloTriangleApplication application;
    application.run();
  }
  catch ( std::exception const & error )
  {
    std::cerr << "Application error: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
  catch ( ... )
  {
    std::cerr << "Application error: unknown failure\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
