#include "sys.h"
#include "Application.h"
#include "SynchronousWindow.h"
#include "FrameResourcesData.h"
#include "debug/vulkan_print_on.h"
#include "infos/ApplicationInfo.h"
#include "infos/InstanceCreateInfo.h"
#include "evio/EventLoop.h"
#include "resolver-task/DnsResolver.h"
#include <algorithm>
#include <chrono>
#include "debug.h"
#ifdef CWDEBUG
#include "debug/DebugUtilsMessengerCreateInfoEXT.h"
#include "utils/debug_ostream_operators.h"
#endif

namespace vulkan {

// Construct the base class of the Application object.
//
// Because this is a base class, virtual functions can't be used in the constructor.
// Therefore initialization happens after construction.
Application::Application() : m_dmri(m_mpp.instance()), m_thread_pool(1)
{
  DoutEntering(dc::vulkan, "vulkan::Application::Application()");
}

// This instantiates the destructor of our std::unique_ptr's. Put here instead of the header
// so that we could use forward declarations for EventLoop and DnsResolver.
Application::~Application()
{
  DoutEntering(dc::vulkan, "vulkan::Application::~Application()");
}

//virtual
std::string Application::default_display_name() const
{
  DoutEntering(dc::vulkan, "vulkan::Application::default_display_name()");
  return ":0";
}

//virtual
void Application::parse_command_line_parameters(int argc, char* argv[])
{
  DoutEntering(dc::vulkan, "vulkan::Application::parse_command_line_parameters(" << argc << ", " << debug::print_argv(argv) << ")");
}

//virtual
std::string Application::application_name() const
{
  return vk_defaults::ApplicationInfo::default_application_name;
}

//virtual
uint32_t Application::application_version() const
{
  return vk_defaults::ApplicationInfo::default_application_version;
}

// Finish initialization of a default constructed Application.
void Application::initialize(int argc, char** argv)
{
  DoutEntering(dc::vulkan, "vulkan::Application::initialize(" << argc << ", ...)");

  // Only call initialize once. Calling it twice leads to a nasty dead-lock that was hard to debug ;).
  ASSERT(!m_event_loop);

  try
  {
    // Set a default display name.
    m_main_display_broker_key.set_display_name(default_display_name());
  }
  catch (AIAlert::Error const& error)
  {
    // It is not a problem when the default_display_name() is empty (that is the same as not
    // calling set_display_name at all, here). So just print a warning and continue.
    Dout(dc::warning, "\e[31m" << error << ", caught in vulkan/Application.cxx\e[0m");
  }

  try
  {
    // Parse command line parameters before doing any initialization, so the command line arguments can influence the initialization too.
    // Get the directory that the executable resides in.
    std::filesystem::path executable_path = argv[0];
    auto directory_element_iterator = executable_path.end();
    int n = 0;
    bool in_directory_bin = false;
    while (directory_element_iterator != executable_path.begin())
    {
      --directory_element_iterator;
      if (++n == 2)
      {
        in_directory_bin = *directory_element_iterator == "bin";
        break;
      }
    }
    if (in_directory_bin)
    {
      std::filesystem::path prefix = executable_path.parent_path().remove_filename();
      std::filesystem::path executable_name = executable_path.filename();
      m_resources_path = prefix / "share" / executable_name / "resources";
    }
    else
      m_resources_path = executable_path.remove_filename() / "data";

    // Allow the user to override stuff.
    if (argc > 0)
      parse_command_line_parameters(argc, argv);

    // Initialize the thread pool.
    m_thread_pool.change_number_of_threads_to(thread_pool_number_of_worker_threads());
    Debug(m_thread_pool.set_color_functions([](int color){ std::string code{"\e[30m"}; code[3] = '1' + color; return code; }));

    // Initialize the first thread pool queue.
    m_high_priority_queue   = m_thread_pool.new_queue(thread_pool_queue_capacity(QueuePriority::high), thread_pool_reserved_threads(QueuePriority::high));
  }
  catch (AIAlert::Error const& error)
  {
    // If an exception is thrown before we have at least one thread pool queue, then that
    // exception is never caught by main(), because leaving initialize() before that point
    // will cause the application to terminate in AIThreadPool::Worker::tmain(int) with
    // FATAL         : No thread pool queue found after 100 ms. [...]
    // while the main thread is blocked in the destructor of AIThreadPool, waiting to join
    // with the one thread that was already created.
    Dout(dc::warning, "\e[31m" << error << ", caught in vulkan/Application.cxx\e[0m");
    return;
  }

  // Initialize the remaining thread pool queues.
  m_medium_priority_queue = m_thread_pool.new_queue(thread_pool_queue_capacity(QueuePriority::medium), thread_pool_reserved_threads(QueuePriority::medium));
  m_low_priority_queue    = m_thread_pool.new_queue(thread_pool_queue_capacity(QueuePriority::low));

  // Set up the I/O event loop.
  m_event_loop = std::make_unique<evio::EventLoop>(m_low_priority_queue COMMA_CWDEBUG_ONLY("\e[36m", "\e[0m"));
  m_resolver_scope = std::make_unique<resolver::Scope>(m_low_priority_queue, false);

  // Start the connection broker.
  m_xcb_connection_broker = statefultask::create<task::SynchronousWindow::xcb_connection_broker_type>(CWDEBUG_ONLY(false));
  m_xcb_connection_broker->run(m_low_priority_queue);           // Note: the broker never finishes, until abort() is called on it.

  ApplicationInfo application_info;
  application_info.set_application_name(application_name());
  application_info.set_application_version(application_version());
  InstanceCreateInfo instance_create_info(application_info.read_access());

#ifdef CWDEBUG
  // Turn on required debug channels.
  vk_defaults::debug_init();

  // Set debug call back function to call the static DebugUtilsMessenger::debugCallback(nullptr);
  DebugUtilsMessengerCreateInfoEXT debug_create_info(&DebugUtilsMessenger::debugCallback, nullptr);
  // Add extension debug_create_info to use during Instance creation and destruction.
  VkDebugUtilsMessengerCreateInfoEXT& debug_create_info_ref = debug_create_info;
  instance_create_info.setPNext(&debug_create_info_ref);
#endif

  prepare_instance_info(instance_create_info);
  create_instance(instance_create_info);

#ifdef CWDEBUG
  m_debug_utils_messenger.prepare(*m_instance, debug_create_info);
#endif
}

boost::intrusive_ptr<task::LogicalDevice> Application::create_logical_device(
    std::unique_ptr<LogicalDevice>&& logical_device, boost::intrusive_ptr<task::SynchronousWindow const>&& root_window)
{
  DoutEntering(dc::vulkan, "vulkan::Application::create_logical_device(" << (void*)logical_device.get() << ", " << (void const*)root_window.get() << ")");

  auto logical_device_task = statefultask::create<task::LogicalDevice>(this COMMA_CWDEBUG_ONLY(true));
  logical_device_task->set_logical_device(std::move(logical_device));
  logical_device_task->set_root_window(std::move(root_window));

  logical_device_task->run(m_high_priority_queue);

  return logical_device_task;
}

int Application::create_device(std::unique_ptr<LogicalDevice>&& logical_device, task::SynchronousWindow* root_window)
{
  DoutEntering(dc::vulkan, "vulkan::Application::create_device(" << (void*)logical_device.get() << ", [" << root_window << "])");

  logical_device->prepare(*m_instance, m_dispatch_loader, root_window);
  Dout(dc::vulkan, "Created LogicalDevice " << *logical_device);

  int logical_device_index;
  {
    logical_device_list_t::wat logical_device_list_w(m_logical_device_list);
    logical_device_index = logical_device_list_w->size();
    logical_device_list_w->emplace_back(std::move(logical_device));
  }

  root_window->set_logical_device_index(logical_device_index);

  return logical_device_index;
}

void Application::add(task::SynchronousWindow* window_task)
{
  DoutEntering(dc::vulkan, "vulkan::Application::add(" << window_task << ")");
  window_list_t::wat window_list_w(m_window_list);
  if (m_window_created && window_list_w->empty())
  {
    // This is not allowed because the program is already terminating, or could be;
    // allowing this would introduce race conditions.
    THROW_ALERT("Trying to add a new window after the last window was closed.");
  }
  m_window_created = true;
  window_list_w->emplace_back(window_task);
}

void Application::remove(task::SynchronousWindow* window_task)
{
  DoutEntering(dc::vulkan, "vulkan::Application::remove(" << window_task << ")");
  window_list_t::wat window_list_w(m_window_list);
  window_list_w->erase(
      std::remove_if(window_list_w->begin(), window_list_w->end(), [window_task](auto element){ return element.get() == window_task; }),
      window_list_w->end());
}

void Application::create_instance(vulkan::InstanceCreateInfo const& instance_create_info)
{
  DoutEntering(dc::vulkan, "vulkan::Application::create_instance(" << instance_create_info.read_access() << ")");

  // Check that all required layers are available.
  instance_create_info.check_instance_layers_availability();

  // Check that all required extensions are available.
  instance_create_info.check_instance_extensions_availability();

  Dout(dc::vulkan|continued_cf|flush_cf, "Calling vk::createInstanceUnique()... ");
#ifdef CWDEBUG
  std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
#endif
  m_instance = vk::createInstanceUnique(instance_create_info.read_access());
#ifdef CWDEBUG
  std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
#endif
  Dout(dc::finish, "done (" << std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count() << " ms)");
  // Mandatory call after creating the vulkan instance.
  m_dispatch_loader.load(*m_instance);
}

// Run the application.
// This function does not return until the program terminated.
void Application::run()
{
  // The main thread goes to sleep for the entirety of the application.
  m_until_terminated.wait();

  Dout(dc::notice, "======= Program terminated ======");

  // Wait till all logical devices are idle.
  {
    logical_device_list_t::rat logical_device_list_r(m_logical_device_list);
    for (auto& device : *logical_device_list_r)
      device->wait_idle();
  }

  // Stop the broker task.
  m_xcb_connection_broker->terminate();

  // Application terminated cleanly.
  m_event_loop->join();
}

} // namespace vulkan
