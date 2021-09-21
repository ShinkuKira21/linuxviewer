#pragma once

#include "VulkanWindow.h"
#include "DispatchLoader.h"
#include "ApplicationInfo.h"
#include "InstanceCreateInfo.h"
#include "PhysicalDeviceFeatures.h"
#include "statefultask/DefaultMemoryPagePool.h"
#include "statefultask/Broker.h"
#include "threadpool/AIThreadPool.h"
#include "threadsafe/aithreadsafe.h"
#include "xcb-task/ConnectionBrokerKey.h"
#include "utils/threading/Gate.h"
#include <boost/intrusive_ptr.hpp>
#include "debug.h"

namespace evio {
class EventLoop;
} // namespace evio

namespace resolver {
class Scope;
} // namespace resolver

namespace vulkan {

struct DeviceCreateInfo;

class Application
{
 public:
  // Set up the thread pool for the application.
  static constexpr int default_number_of_threads = 8;                   // Use a thread pool of 8 threads.
  static constexpr int default_reserved_threads = 1;                    // Reserve 1 thread for each priority.

  enum class QueuePriority {
    high,
    medium,
    low
  };

 protected:
  // Create a AIMemoryPagePool object (must be created before thread_pool).
  AIMemoryPagePool m_mpp;

  // Create the thread pool.
  AIThreadPool m_thread_pool;

  // And the thread pool queues.
  AIQueueHandle m_high_priority_queue;
  AIQueueHandle m_medium_priority_queue;
  AIQueueHandle m_low_priority_queue;

  // Set up the I/O event loop.
  std::unique_ptr<evio::EventLoop> m_event_loop;
  std::unique_ptr<resolver::Scope> m_resolver_scope;

  // A task that hands out connections to the X server.
  boost::intrusive_ptr<task::VulkanWindow::xcb_connection_broker_type> m_xcb_connection_broker;

  // Configuration of the main X server connection.
  xcb::ConnectionBrokerKey m_main_display_broker_key;

  // To stop the main thread from exiting until the last xcb connection is closed.
  utils::threading::Gate m_until_terminated;

  // All windows.
  using window_list_container_t = std::vector<boost::intrusive_ptr<task::VulkanWindow>>;
  using window_list_t = aithreadsafe::Wrapper<window_list_container_t, aithreadsafe::policy::Primitive<std::mutex>>;
  window_list_t m_window_list;

  // Loader for vulkan extension functions.
  DispatchLoader m_dispatch_loader;

  // Vulkan instance.
  vk::UniqueInstance m_instance;                        // Per application state. Creating a vk::Instance object initializes
                                                        // the Vulkan library and allows the application to pass information
                                                        // about itself to the implementation. Using vk::UniqueInstance also
                                                        // automatically destroys it.

 private:
  friend class task::VulkanWindow;
  void add(task::VulkanWindow* window_task);
  void remove(task::VulkanWindow* window_task);

  // Create and initialize the vulkan instance (m_instance).
  void createInstance(vulkan::InstanceCreateInfo const& instance_create_info);

 public:
  Application();
  ~Application();

 public:
  void initialize(int argc = 0, char** argv = nullptr);
  void create_main_window(std::unique_ptr<linuxviewer::OS::Window>&& window, vk::Extent2D extent, std::string&& title = {});
  void run(int argc, char* argv[]);

#ifdef CWDEBUG
  void debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    VkDebugUtilsMessengerCallbackDataEXT const* pCallbackData);

  static VkBool32 debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    VkDebugUtilsMessengerCallbackDataEXT const* pCallbackData,
    void* pUserData)
  {
    Application* self = reinterpret_cast<Application*>(pUserData);
    self->debugCallback(messageSeverity, messageType, pCallbackData);
    return VK_FALSE;
  }
#endif

 protected:
  // Get the default DISPLAY name to use (can be overridden by parse_command_line_parameters).
  virtual std::string default_display_name() const;

  virtual void parse_command_line_parameters(int argc, char* argv[]);

  // Override this function to change the number of worker threads.
  virtual int thread_pool_number_of_worker_threads() const;

  // Override this function to change the size of the thread pool queues.
  virtual int thread_pool_queue_capacity(QueuePriority UNUSED_ARG(priority)) const;

  // Override this function to change the number of reserved threads for each queue (except the last, of course).
  virtual int thread_pool_reserved_threads(QueuePriority UNUSED_ARG(priority)) const;

  // Override this function to change the default ApplicatioInfo values.
  virtual std::string application_name() const { return vk_defaults::ApplicationInfo::default_application_name; }

  // Override this function to change the default application version. The result should be a value returned by vk_utils::encode_version.
  virtual uint32_t application_version() const { return vk_defaults::ApplicationInfo::default_application_version; }

  // Override this function to add Instance layers and/or extensions.
  virtual void prepare_instance_info(vulkan::InstanceCreateInfo& instance_create_info) const { }

  // Override this function to change the default physical device features.
  virtual void prepare_physical_device_features(vulkan::PhysicalDeviceFeatures& physical_device_features) const { }
};

} // namespace vulkan