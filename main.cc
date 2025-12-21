#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <print>
#include <set>
#include <stdexcept>
#include <vector>

#define VULKAN_HPP_NO_CONSTRUCTORS
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_funcs.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vulkan/vulkan_to_string.hpp>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
// #include "utils.hpp"

const uint32_t WIDTH  = 800;
const uint32_t HEIGHT = 600;

const std::vector<const char *> validationLayers = { "VK_LAYER_KHRONOS_validation" };

// Add this global constant
const std::vector<const char *> deviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

#ifdef NDEBUG
const bool enableValidationLayers = false;
#else
const bool enableValidationLayers = true;
#endif

// Add this struct definition
struct QueueFamilyIndices
{
  std::optional<uint32_t> graphicsFamily;
  std::optional<uint32_t> presentFamily;

  bool isComplete() const
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

// Add this struct definition
struct QueueHandles
{
  vk::raii::Queue graphicsQueue = nullptr;
  vk::raii::Queue presentQueue  = nullptr;
};

// Remove the old findGraphicsQueueFamilyIndex function
// uint32_t findGraphicsQueueFamilyIndex( std::vector<vk::QueueFamilyProperties> const & queueFamilyProperties )
// {
//   // get the first index into queueFamiliyProperties which supports graphics
//   std::vector<vk::QueueFamilyProperties>::const_iterator graphicsQueueFamilyProperty =
//     std::find_if( queueFamilyProperties.begin(),
//                   queueFamilyProperties.end(),
//                   []( vk::QueueFamilyProperties const & qfp ) { return qfp.queueFlags & vk::QueueFlagBits::eGraphics; } );
//   assert( graphicsQueueFamilyProperty != queueFamilyProperties.end() );
//   return static_cast<uint32_t>( std::distance( queueFamilyProperties.begin(), graphicsQueueFamilyProperty ) );
// }

class HelloTriangleApplication
{
public:
  HelloTriangleApplication()
    : instance( nullptr ), debug_messenger( nullptr ), surface( nullptr ), physical_device( nullptr ), device( nullptr ), swapChain( nullptr )
  {
  }  // Initialize graphics_queue

  void run()
  {
    initWindow();
    initVulkan();
    mainLoop();
    cleanup();
  }

private:
  // Add this function
  QueueFamilyIndices findQueueFamilies( vk::raii::PhysicalDevice const & physical_device )
  {
    QueueFamilyIndices indices;
    auto               queueFamilyProperties = physical_device.getQueueFamilyProperties();
    uint32_t           i                     = 0;
    for ( const auto & queueFamily : queueFamilyProperties )
    {
      if ( queueFamily.queueFlags & vk::QueueFlagBits::eGraphics )
      {
        indices.graphicsFamily = i;
      }

      // Check for presentation support
      VkBool32 presentSupport = physical_device.getSurfaceSupportKHR( i, *surface );
      if ( presentSupport )
      {
        indices.presentFamily = i;
      }

      if ( indices.isComplete() )
      {
        break;
      }
      i++;
    }
    return indices;
  }

  void initWindow()
  {
    // NOTE: On Wayland, the window will remain invisible until the first frame is presented.
    // This is normal behavior. The window is created but has no content, so the compositor
    // does not display it. It will appear once the swap chain is created and the first image is presented.
    // glfwInitHint( GLFW_PLATFORM, GLFW_PLATFORM_X11 ); // Uncomment to force X11 if needed for debugging

    if ( !glfwInit() )
    {
      throw std::runtime_error( "failed to initialize GLFW!" );
    }

    glfwWindowHint( GLFW_CLIENT_API, GLFW_NO_API );
    glfwWindowHint( GLFW_RESIZABLE, GLFW_FALSE );

    window = glfwCreateWindow( WIDTH, HEIGHT, "Vulkan", nullptr, nullptr );
    if ( !window )
    {
      throw std::runtime_error( "failed to create window!" );
    }
    std::println( "Window created. Note: On Wayland, the window may remain invisible until the first frame is presented." );
  }

  void initVulkan()
  {
    createInstance();
    createSurface();
    pickPhysicalDevice();
    createLogicalDevice();
    createSwapChain();
    createImageViews();
    createRenderPass();
    createGraphicsPipeline();
  }

  void mainLoop()
  {
    while ( !glfwWindowShouldClose( window ) )
    {
      glfwPollEvents();
    }
  }

  void cleanup()
  {
    // Destroy logical device before instance
    // RAII handles this automatically for logical_device, graphics_queue, debug_messenger, instance
    glfwDestroyWindow( window );
    glfwTerminate();
  }

  std::vector<const char *> getRequiredExtensions()
  {
    uint32_t      glfwExtensionCount = 0;
    const char ** glfwExtensions;
    glfwExtensions = glfwGetRequiredInstanceExtensions( &glfwExtensionCount );

    std::vector<const char *> extensions( glfwExtensions, glfwExtensions + glfwExtensionCount );

    if ( enableValidationLayers )
    {
      extensions.push_back( VK_EXT_DEBUG_UTILS_EXTENSION_NAME );
    }

    return extensions;
  }

  // Reverted signature to C-style types as required by PFN_vkDebugUtilsMessengerCallbackEXT
  //  VkBool32 (*)(VkDebugUtilsMessageSeverityFlagBitsEXT, VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT*, void*);
  static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback( vk::DebugUtilsMessageSeverityFlagBitsEXT       messageSeverity,
                                                       vk::DebugUtilsMessageTypeFlagsEXT              messageType,
                                                       const vk::DebugUtilsMessengerCallbackDataEXT * pCallbackData,
                                                       void *                                         pUserData )
  {
    // Use vk::DebugUtilsMessageSeverityFlagBitsEXT for comparison
    // if ( messageSeverity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo )  // Check if the info bit is set
    // {
    //   return VK_FALSE;  // Don't print info messages
    // }

    // Use vk::to_string for severity level output
    std::cerr << "validation layer (" << vk::to_string( static_cast<vk::DebugUtilsMessageSeverityFlagBitsEXT>( messageSeverity ) )
              << "): " << pCallbackData->pMessage << std::endl;

    // The callback must always return VK_FALSE as per the spec unless testing layers themselves
    return VK_FALSE;
  }

  void createInstance()
  {
    vk::ApplicationInfo appInfo{ .pApplicationName   = "Hello Triangle",
                                 .applicationVersion = VK_MAKE_VERSION( 1, 0, 0 ),
                                 .pEngineName        = "No Engine",
                                 .engineVersion      = VK_MAKE_VERSION( 1, 0, 0 ),
                                 .apiVersion         = VK_API_VERSION_1_3 };  // Use a more recent API version like 1.3

    auto                   extensions = getRequiredExtensions();
    vk::InstanceCreateInfo createInfo{ .pApplicationInfo        = &appInfo,
                                       .enabledLayerCount       = 0,  // Layers handled below if enabled
                                       .enabledExtensionCount   = static_cast<uint32_t>( extensions.size() ),
                                       .ppEnabledExtensionNames = extensions.data() };

    vk::DebugUtilsMessengerCreateInfoEXT dm_create_info{};
    if ( enableValidationLayers )
    {
      createInfo.enabledLayerCount   = static_cast<uint32_t>( validationLayers.size() );
      createInfo.ppEnabledLayerNames = validationLayers.data();

      dm_create_info   = vk::DebugUtilsMessengerCreateInfoEXT{ .messageSeverity = vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose |
                                                                                vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
                                                                                vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
                                                               .messageType = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                                                                            vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
                                                                            vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance,
                                                               .pfnUserCallback = debugCallback,  // Pass the C-style function pointer
                                                               .pUserData       = nullptr };
      createInfo.pNext = &dm_create_info;  // Chain the debug messenger create info for instance creation
    }
    else
    {
      createInfo.enabledLayerCount = 0;
      createInfo.pNext             = nullptr;
    }

    instance = context.createInstance( createInfo, nullptr );

    // Create the debug messenger *after* the instance if validation layers are enabled
    if ( enableValidationLayers )
    {
      // Re-use the dm_create_info from above, but this time pass it to createDebugUtilsMessengerEXT
      debug_messenger = instance.createDebugUtilsMessengerEXT( dm_create_info );
    }

    auto extension_properties = context.enumerateInstanceExtensionProperties();
    std::println( "available extensions:" );
    for ( auto extension : extension_properties )
      std::println( "{}", extension.extensionName.data() );
  }

  void createSurface()
  {
    VkSurfaceKHR vk_surface;
    if ( glfwCreateWindowSurface( *instance, window, nullptr, &vk_surface ) != VK_SUCCESS )
    {
      throw std::runtime_error( "failed to create window surface!" );
    }
    // Assign to the RAII member variable
    surface = vk::raii::SurfaceKHR( instance, vk_surface );
  }

  void pickPhysicalDevice()
  {
    auto devices = instance.enumeratePhysicalDevices();

    if ( devices.empty() )
    {
      throw std::runtime_error( "failed to find GPUs with Vulkan support!" );
    }

    for ( const auto & device_candidate : devices )
    {
      if ( isDeviceSuitable( device_candidate ) )
      {
        this->physical_device = device_candidate;  // Assign the suitable device
        break;
      }
    }

    if ( !*physical_device )
    {  // Check if a suitable device was actually found and assigned
      throw std::runtime_error( "failed to find a suitable GPU!" );
    }
    // Store the queue family indices for the chosen device
    queueFamilyIndices = findQueueFamilies( physical_device );
  }

  vk::SurfaceFormatKHR chooseSwapSurfaceFormat( const std::vector<vk::SurfaceFormatKHR> & availableFormats )
  {
    for ( const auto & availableFormat : availableFormats )
    {
      if ( availableFormat.format == vk::Format::eB8G8R8A8Srgb && availableFormat.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear )
      {
        return availableFormat;
      }
    }

    return availableFormats[0];
  }

  vk::PresentModeKHR chooseSwapPresentMode( const std::vector<vk::PresentModeKHR> & availablePresentModes )
  {
    for ( const auto & availablePresentMode : availablePresentModes )
    {
      if ( availablePresentMode == vk::PresentModeKHR::eMailbox )
      {
        return availablePresentMode;
      }
    }

    return vk::PresentModeKHR::eFifo;
  }

  vk::Extent2D chooseSwapExtent( const vk::SurfaceCapabilitiesKHR & capabilities )
  {
    if ( capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max() )
    {
      return capabilities.currentExtent;
    }
    else
    {
      int width, height;
      glfwGetFramebufferSize( window, &width, &height );

      vk::Extent2D actualExtent = { static_cast<uint32_t>( width ), static_cast<uint32_t>( height ) };

      actualExtent.width  = std::clamp( actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width );
      actualExtent.height = std::clamp( actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height );

      return actualExtent;
    }
  }

  bool isDeviceSuitable( const vk::raii::PhysicalDevice & physical_device )
  {
    // Check if the device supports the necessary queue families
    QueueFamilyIndices indices = findQueueFamilies( physical_device );

    // Check for required device extension support
    std::vector<vk::ExtensionProperties> availableExtensions = physical_device.enumerateDeviceExtensionProperties();

    std::set<std::string> requiredExtensions( deviceExtensions.begin(), deviceExtensions.end() );

    for ( const auto & extension : availableExtensions )
    {
      requiredExtensions.erase( extension.extensionName );
    }

    bool extensionsSupported = requiredExtensions.empty();

    // Check for swap chain adequacy (formats and present modes)
    bool swapChainAdequate = false;
    if ( extensionsSupported )
    {  // Only query if the extension itself is supported
      swapChainSupport  = querySwapChainSupport( physical_device );
      swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
    }

    // TODO: Add check for required device extensions (e.g., swapchain) later

    // TODO: Add check for specific physical device features if needed
    // auto features = physical_device.getFeatures();
    // if (!features.geometryShader) return false;

    return indices.isComplete() && extensionsSupported && swapChainAdequate;
  }

  void createLogicalDevice()
  {
    std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t>                     uniqueQueueFamilies = { queueFamilyIndices.graphicsFamily.value(), queueFamilyIndices.presentFamily.value() };

    float queuePriority = 1.0f;
    for ( uint32_t queueFamilyIndex : uniqueQueueFamilies )
    {
      queueCreateInfos.push_back( vk::DeviceQueueCreateInfo{ .queueFamilyIndex = queueFamilyIndex, .queueCount = 1, .pQueuePriorities = &queuePriority } );
    }

    vk::PhysicalDeviceFeatures device_features{};  // Enable features here if needed
    vk::DeviceCreateInfo       device_create_info{ .pEnabledFeatures = &device_features };
    device_create_info.setQueueCreateInfos( queueCreateInfos );

    device_create_info.setEnabledExtensionCount( static_cast<uint32_t>( deviceExtensions.size() ) );
    device_create_info.setPpEnabledExtensionNames( deviceExtensions.data() );

    // Device layers are deprecated, so explicitly set count to 0.
    // Device layers are deprecated, so explicitly set count to 0.
    // device_create_info.enabledLayerCount = 0;

    device = vk::raii::Device( physical_device, device_create_info );
    std::println( "Logical device created." );

    // Get the queue handles
    queues.graphicsQueue = vk::raii::Queue( device, queueFamilyIndices.graphicsFamily.value(), 0 );
    queues.presentQueue  = vk::raii::Queue( device, queueFamilyIndices.presentFamily.value(), 0 );
    std::println( "Graphics and Present queues obtained." );
  }

  SwapChainSupportDetails querySwapChainSupport( vk::raii::PhysicalDevice const & physical_device )
  {
    SwapChainSupportDetails details;
    // Query capabilities
    details.capabilities = physical_device.getSurfaceCapabilitiesKHR( *surface );
    // Query supported formats
    details.formats = physical_device.getSurfaceFormatsKHR( *surface );
    // Query supported presentation modes
    details.presentModes = physical_device.getSurfacePresentModesKHR( *surface );
    return details;
  }

  void createSwapChain()
  {
    vk::SurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat( swapChainSupport.formats );
    vk::PresentModeKHR   presentMode   = chooseSwapPresentMode( swapChainSupport.presentModes );
    vk::Extent2D         extent        = chooseSwapExtent( swapChainSupport.capabilities );

    uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
    if ( swapChainSupport.capabilities.maxImageCount > 0 )
    {
      imageCount = std::clamp( imageCount, swapChainSupport.capabilities.minImageCount, swapChainSupport.capabilities.maxImageCount );
    }

    vk::SharingMode       sharingMode             = vk::SharingMode::eExclusive;
    uint32_t              queueFamilyIndicesCount = 1;
    std::vector<uint32_t> vQueueFamilyIndices;
    if ( queueFamilyIndices.graphicsFamily != queueFamilyIndices.presentFamily )
    {
      sharingMode             = vk::SharingMode::eConcurrent;
      queueFamilyIndicesCount = 2;
      vQueueFamilyIndices     = { queueFamilyIndices.graphicsFamily.value(), queueFamilyIndices.presentFamily.value() };
    }
    swapChain            = device.createSwapchainKHR( { .surface               = *surface,
                                                        .minImageCount         = imageCount,
                                                        .imageFormat           = surfaceFormat.format,
                                                        .imageColorSpace       = surfaceFormat.colorSpace,
                                                        .imageExtent           = extent,
                                                        .imageArrayLayers      = 1,
                                                        .imageUsage            = vk::ImageUsageFlagBits::eColorAttachment,
                                                        .imageSharingMode      = sharingMode,
                                                        .queueFamilyIndexCount = queueFamilyIndicesCount,
                                                        .pQueueFamilyIndices   = vQueueFamilyIndices.data(),
                                                        .preTransform          = swapChainSupport.capabilities.currentTransform,
                                                        .compositeAlpha        = vk::CompositeAlphaFlagBitsKHR::eOpaque,
                                                        .presentMode           = presentMode,
                                                        .clipped               = true } );
    swapChainImages      = swapChain.getImages();
    swapChainImageFormat = surfaceFormat.format;
    swapChainExtent      = extent;
  }

  void createImageViews()
  {
    swapChainImageViews.reserve( swapChainImages.size() );
    for ( const auto & image : swapChainImages )
    {
      vk::ImageViewCreateInfo createInfo{
        .image            = image,
        .viewType         = vk::ImageViewType::e2D,
        .format           = swapChainImageFormat,
        .components       = { .r = vk::ComponentSwizzle::eIdentity,
                              .g = vk::ComponentSwizzle::eIdentity,
                              .b = vk::ComponentSwizzle::eIdentity,
                              .a = vk::ComponentSwizzle::eIdentity },
        .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eColor, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1 }
      };
      swapChainImageViews.push_back( device.createImageView( createInfo ) );
    }
  }

  static std::vector<char> readFile( const std::string & filename )
  {
    std::ifstream file( filename, std::ios::ate | std::ios::binary );

    if ( !file.is_open() )
    {
      throw std::runtime_error( "failed to open file!" );
    }

    size_t            fileSize = (size_t)file.tellg();
    std::vector<char> buffer( fileSize );

    file.seekg( 0 );
    file.read( buffer.data(), fileSize );

    file.close();

    return buffer;
  }

  vk::ShaderModule createShaderModule( const std::vector<char> & code )
  {
    vk::ShaderModuleCreateInfo createInfo{ .codeSize = code.size(), .pCode = reinterpret_cast<const uint32_t *>( code.data() ) };
    return device.createShaderModule( createInfo );
  }

  void createRenderPass()
  {
    vk::AttachmentDescription colorAttachment {
      .format = swapChainImageFormat,
      .samples = vk::SampleCountFlagBits::e1,
      .loadOp = vk::AttachmentLoadOp::eClear,
      .storeOp = vk::AttachmentStoreOp::eStore,
      .stencilLoadOp = vk::AttachmentLoadOp::eDontCare,
      .stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
      .initialLayout = vk::ImageLayout::eUndefined,
      .finalLayout = vk::ImageLayout::ePresentSrcKHR
    };

    vk::AttachmentReference colorAttachmentRef {
      .attachment = 0,
      .layout = vk::ImageLayout::eColorAttachmentOptimal
    };

    vk::SubpassDescription subpass {
      .pipelineBindPoint = vk::PipelineBindPoint::eGraphics,
      .colorAttachmentCount = 1,
      .pColorAttachments = &colorAttachmentRef
    };
    
    vk::RenderPassCreateInfo renderPassInfo {
      .attachmentCount = 1,
      .pAttachments = &colorAttachment,
      .subpassCount = 1,
      .pSubpasses = &subpass
    };
    renderPass = device.createRenderPass(renderPassInfo);

  }

  void createGraphicsPipeline()
  {
    auto vertShaderCode = readFile( "./vert.spv" );
    auto fragShaderCode = readFile( "./frag.spv" );

    vk::ShaderModule vertShaderModule = createShaderModule( vertShaderCode );
    vk::ShaderModule fragShaderModule = createShaderModule( fragShaderCode );

    vk::PipelineShaderStageCreateInfo vertShaderStageInfo{
      .stage = vk::ShaderStageFlagBits::eVertex,
      .module = vertShaderModule,
      .pName = "main"
    };

    vk::PipelineShaderStageCreateInfo fragShaderStageInfo{
      .stage = vk::ShaderStageFlagBits::eFragment,
      .module = fragShaderModule,
      .pName = "main"
    };

    vk::PipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };
    std::vector<vk::DynamicState> dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
    vk::PipelineDynamicStateCreateInfo dynamicState {
      .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
      .pDynamicStates = dynamicStates.data()
    };

    vk::PipelineVertexInputStateCreateInfo vertexInputInfo {
      .vertexBindingDescriptionCount = 0,
      .pVertexBindingDescriptions = nullptr,
      .vertexAttributeDescriptionCount = 0,
      .pVertexAttributeDescriptions = nullptr
    };

    vk::PipelineInputAssemblyStateCreateInfo inputAssembly {
      .topology = vk::PrimitiveTopology::eTriangleList,
      .primitiveRestartEnable = vk::False
    };

    vk::Viewport viewport {
      .x = 0.0f,
      .y = 0.0f,
      .width = (float)swapChainExtent.width,
      .height = (float)swapChainExtent.height,
      .minDepth = 0.0f,
      .maxDepth = 1.0f
    };

    vk::Rect2D scissor {
      .offset = { .x = 0, .y = 0 },
      .extent = swapChainExtent
    };

    vk::PipelineViewportStateCreateInfo viewportState {
      .viewportCount = 1,
      .pViewports = &viewport,
      .scissorCount = 1,
      .pScissors = &scissor
    };

    vk::PipelineRasterizationStateCreateInfo rasterizer {
      .depthClampEnable = vk::False,
      .rasterizerDiscardEnable = vk::False,
      .polygonMode = vk::PolygonMode::eFill,
      .cullMode = vk::CullModeFlagBits::eBack,
      .frontFace = vk::FrontFace::eClockwise,
      .depthBiasEnable = vk::False,
      .depthBiasConstantFactor = 0.0f,
      .depthBiasClamp = 0.0f,
      .depthBiasSlopeFactor = 0.0f,
      .lineWidth = 1.0f
    };

    vk::PipelineMultisampleStateCreateInfo multisampling {
      .rasterizationSamples = vk::SampleCountFlagBits::e1,
      .sampleShadingEnable = vk::False,
      .minSampleShading = 1.0f,
      .pSampleMask = nullptr,
      .alphaToCoverageEnable = vk::False,
      .alphaToOneEnable = vk::False
    };

    vk::PipelineColorBlendAttachmentState colorBlendAttachment {
      .blendEnable = vk::False,
      .srcColorBlendFactor = vk::BlendFactor::eOne,
      .dstColorBlendFactor = vk::BlendFactor::eZero,
      .colorBlendOp = vk::BlendOp::eAdd,
      .srcAlphaBlendFactor = vk::BlendFactor::eOne,
      .dstAlphaBlendFactor = vk::BlendFactor::eZero,
      .alphaBlendOp = vk::BlendOp::eAdd,
      .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
    };

    vk::PipelineColorBlendStateCreateInfo colorBlending {
      .logicOpEnable = vk::False,
      .logicOp = vk::LogicOp::eCopy,
      .attachmentCount = 1,
      .pAttachments = &colorBlendAttachment,
      .blendConstants = std::array{ 0.0f, 0.0f, 0.0f, 0.0f }
    };

    vk::PipelineLayoutCreateInfo pipelineLayoutInfo {
      .setLayoutCount = 0,
      .pSetLayouts = nullptr,
      .pushConstantRangeCount = 0,
      .pPushConstantRanges = nullptr
    };
    pipelineLayout = device.createPipelineLayout( pipelineLayoutInfo );

    vk::GraphicsPipelineCreateInfo pipelineInfo {
      .stageCount = 2,
      .pStages = shaderStages,
      .pVertexInputState = &vertexInputInfo,
      .pInputAssemblyState = &inputAssembly,
      .pViewportState = &viewportState,
      .pRasterizationState = &rasterizer,
      .pMultisampleState = &multisampling,
      .pDepthStencilState = nullptr,
      .pColorBlendState = &colorBlending,
      .pDynamicState = &dynamicState,
      .layout = *pipelineLayout,
      .renderPass = *renderPass,
      .subpass = 0
    };
    graphicsPipeline = device.createGraphicsPipeline( nullptr, pipelineInfo );
  }

  GLFWwindow *                     window;
  vk::raii::Context                context;
  vk::raii::Instance               instance;
  vk::raii::DebugUtilsMessengerEXT debug_messenger;
  vk::raii::SurfaceKHR             surface;  // Move surface declaration earlier
  vk::raii::PhysicalDevice         physical_device;
  QueueFamilyIndices               queueFamilyIndices;  // Add this
  SwapChainSupportDetails          swapChainSupport;
  vk::raii::Device                 device;
  QueueHandles                     queues{ nullptr, nullptr };  // Add this, initializing queues
  vk::raii::SwapchainKHR           swapChain;
  std::vector<vk::raii::Image>     swapChainImages;
  vk::Format                       swapChainImageFormat;
  vk::Extent2D                     swapChainExtent;
  std::vector<vk::raii::ImageView> swapChainImageViews;
  vk::raii::RenderPass             renderPass;
  vk::raii::PipelineLayout         pipelineLayout;
  vk::raii::Pipeline               graphicsPipeline;
};

int main()
{
  HelloTriangleApplication app;

  try
  {
    app.run();
  }
  catch ( const vk::SystemError & err )  // Catch Vulkan specific errors
  {
    std::cerr << "vk::SystemError: " << err.what() << std::endl;
    return EXIT_FAILURE;
  }
  catch ( const std::exception & e )
  {
    std::cerr << "std::exception: " << e.what() << std::endl;
    return EXIT_FAILURE;
  }
  catch ( ... )  // Catch all other errors
  {
    std::cerr << "Unknown error occurred." << std::endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
