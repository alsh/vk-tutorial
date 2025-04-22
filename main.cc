#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <print>
#include <set>
#include <stdexcept>

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

  bool isComplete() const { return graphicsFamily.has_value() && presentFamily.has_value(); }
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
    : instance( nullptr ), debug_messenger( nullptr ), surface( nullptr ), device( nullptr ), logical_device( nullptr )
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
    QueueFamilyIndices          indices;
    auto                        queueFamilyProperties = physical_device.getQueueFamilyProperties();
    uint32_t                    i                   = 0;
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
    glfwInit();

    glfwWindowHint( GLFW_CLIENT_API, GLFW_NO_API );
    glfwWindowHint( GLFW_RESIZABLE, GLFW_FALSE );

    window = glfwCreateWindow( WIDTH, HEIGHT, "Vulkan", nullptr, nullptr );
  }

  void initVulkan()
  {
    createInstance();
    createSurface(); // Ensure surface is created before picking device
    pickPhysicalDevice();
    createLogicalDevice();
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
    if ( messageSeverity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo )  // Check if the info bit is set
    {
      return VK_FALSE;  // Don't print info messages
    }

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
      if ( isDeviceSuitable( device_candidate ) )
      {
        this->device = device_candidate;  // Assign the suitable device
        break;
      }

    if ( !*device )
    {  // Check if a suitable device was actually found and assigned
      throw std::runtime_error( "failed to find a suitable GPU!" );
    }
    // Store the queue family indices for the chosen device
    queueFamilyIndices = findQueueFamilies( device );

    std::println( "Selected Physical Device: {}", device.getProperties().deviceName.data() );
  }

  bool isDeviceSuitable( const vk::raii::PhysicalDevice & physical_device )
  {
    // Check if the device supports the necessary queue families
    QueueFamilyIndices indices = findQueueFamilies( physical_device );

    // TODO: Add check for required device extensions (e.g., swapchain) later

    // TODO: Add check for specific physical device features if needed
    // auto features = physical_device.getFeatures();
    // if (!features.geometryShader) return false;

    return indices.isComplete();
  }

  void createLogicalDevice()
  {
    std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueQueueFamilies = { queueFamilyIndices.graphicsFamily.value(), queueFamilyIndices.presentFamily.value() };

    float queuePriority = 1.0f;
    for ( uint32_t queueFamilyIndex : uniqueQueueFamilies )
    {
      queueCreateInfos.push_back( vk::DeviceQueueCreateInfo{ .queueFamilyIndex = queueFamilyIndex, .queueCount = 1, .pQueuePriorities = &queuePriority } );
    }

    vk::PhysicalDeviceFeatures device_features{};  // Enable features here if needed
    vk::DeviceCreateInfo       device_create_info{ .pEnabledFeatures = &device_features };
    device_create_info.setQueueCreateInfos( queueCreateInfos );

    // Device extensions (add required ones like swapchain later)
    std::vector<const char *> deviceExtensions = {};  // Placeholder for now
    // TODO: Add VK_KHR_SWAPCHAIN_EXTENSION_NAME here later
    device_create_info.setEnabledExtensionCount( static_cast<uint32_t>( deviceExtensions.size() ) );
    device_create_info.setPpEnabledExtensionNames( deviceExtensions.data() );

    // Device layers are deprecated, so explicitly set count to 0.
    device_create_info.enabledLayerCount = 0;

    logical_device = vk::raii::Device( device, device_create_info );
    std::println( "Logical device created." );

    // Get the queue handles
    queues.graphicsQueue = vk::raii::Queue( logical_device, queueFamilyIndices.graphicsFamily.value(), 0 );
    queues.presentQueue  = vk::raii::Queue( logical_device, queueFamilyIndices.presentFamily.value(), 0 );
    std::println( "Graphics and Present queues obtained." );
  }

  GLFWwindow *                     window;
  vk::raii::Context                context;
  vk::raii::Instance               instance;
  vk::raii::DebugUtilsMessengerEXT debug_messenger;
  vk::raii::SurfaceKHR             surface; // Move surface declaration earlier
  vk::raii::PhysicalDevice         device;
  QueueFamilyIndices               queueFamilyIndices; // Add this
  vk::raii::Device                 logical_device;
  QueueHandles                     queues{ nullptr, nullptr }; // Add this, initializing queues
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
