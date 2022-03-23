#include "sys.h"
#include "PipelineFactory.h"
#include "Handle.h"
#include "SynchronousWindow.h"
#include "pipeline/CacheBrokerKey.h"
#include "threadsafe/aithreadsafe.h"

namespace task {

namespace synchronous {

class PipelineFactoryWatcher : public SynchronousTask
{
 public:
  static constexpr condition_type need_action = 1;

 private:
  using container_type = std::deque<vulkan::pipeline::Handle>;
  using new_pipelines_type = aithreadsafe::Wrapper<container_type, aithreadsafe::policy::Primitive<std::mutex>>;
  new_pipelines_type m_new_pipelines;
  std::atomic_bool m_parent_finished = false;

 protected:
  /// The base class of this task.
  using direct_base_type = SynchronousTask;

  /// The different states of the stateful task.
  enum delayed_destroyer_task_state_type {
    PipelineFactoryWatcher_start = direct_base_type::state_end,
    PipelineFactoryWatcher_need_action,
    PipelineFactoryWatcher_done
  };

 public:
  /// One beyond the largest state of this task.
  static constexpr state_type state_end = PipelineFactoryWatcher_done + 1;

 public:
  PipelineFactoryWatcher(SynchronousWindow* owning_window COMMA_CWDEBUG_ONLY(bool debug)) :
    SynchronousTask(owning_window COMMA_CWDEBUG_ONLY(debug))
  {
    DoutEntering(dc::vulkan, "PipelineFactoryWatcher::PipelineFactoryWatcher(" << owning_window << ") [" << this << "]")
  }

  void have_new_pipeline(vulkan::pipeline::Handle pipeline_handle);
  void terminate();

 protected:
  /// Call finish() (or abort()), not delete.
  ~PipelineFactoryWatcher() override
  {
    DoutEntering(dc::vulkan, "PipelineFactoryWatcher::~PipelineFactoryWatcher() [" << this << "]");
  }

  /// Implemenation of state_str for run states.
  char const* state_str_impl(state_type run_state) const override;

  /// Handle mRunState.
  void multiplex_impl(state_type run_state) override final;
};

void PipelineFactoryWatcher::have_new_pipeline(vulkan::pipeline::Handle pipeline_handle)
{
  new_pipelines_type::wat(m_new_pipelines)->push_back(pipeline_handle);
  signal(synchronous::PipelineFactoryWatcher::need_action);
}

void PipelineFactoryWatcher::terminate()
{
  m_parent_finished = true;
  signal(synchronous::PipelineFactoryWatcher::need_action);
}

char const* PipelineFactoryWatcher::state_str_impl(state_type run_state) const
{
  switch (run_state)
  {
    AI_CASE_RETURN(PipelineFactoryWatcher_start);
    AI_CASE_RETURN(PipelineFactoryWatcher_need_action);
    AI_CASE_RETURN(PipelineFactoryWatcher_done);
  }
  AI_NEVER_REACHED
}

void PipelineFactoryWatcher::multiplex_impl(state_type run_state)
{
  switch (run_state)
  {
    case PipelineFactoryWatcher_start:
      set_state(PipelineFactoryWatcher_need_action);
      wait(need_action);
      break;
    case PipelineFactoryWatcher_need_action:
      for (;;)
      {
        vulkan::pipeline::Handle pipeline_handle;
        {
          new_pipelines_type::wat new_pipelines_w(m_new_pipelines);
          if (new_pipelines_w->empty())
            break;
          pipeline_handle = new_pipelines_w->front();
          new_pipelines_w->pop_front();
        }
        owning_window()->new_pipeline(pipeline_handle);
      }
      if (!m_parent_finished)
      {
        wait(need_action);
        break;
      }
      set_state(PipelineFactoryWatcher_done);
      [[fallthrough]];
    case PipelineFactoryWatcher_done:
      finish();
      break;
  }
}

} // namespace synchronous

PipelineFactory::PipelineFactory(SynchronousWindow* owning_window, vk::PipelineLayout vh_pipeline_layout, vk::RenderPass vh_render_pass
    COMMA_CWDEBUG_ONLY(bool debug)) : AIStatefulTask(CWDEBUG_ONLY(debug)),
    m_owning_window(owning_window), m_vh_pipeline_layout(vh_pipeline_layout), m_vh_render_pass(vh_render_pass)
{
  DoutEntering(dc::statefultask(mSMDebug), "PipelineFactory(" << owning_window << ", " << vh_pipeline_layout << ", " << vh_render_pass << ")");
}

PipelineFactory::~PipelineFactory()
{
  DoutEntering(dc::statefultask(mSMDebug), "~PipelineFactory() [" << this << "]");
}

void PipelineFactory::set_pipeline_cache_broker(boost::intrusive_ptr<pipeline_cache_broker_type> broker)
{
  m_broker = std::move(broker);
}

void PipelineFactory::add(boost::intrusive_ptr<vulkan::pipeline::CharacteristicRange> characteristic_range)
{
  m_characteristics.push_back(std::move(characteristic_range));
}

char const* PipelineFactory::state_str_impl(state_type run_state) const
{
  switch (run_state)
  {
    AI_CASE_RETURN(PipelineFactory_start);
    AI_CASE_RETURN(PipelineFactory_initialize);
    AI_CASE_RETURN(PipelineFactory_initialized);
    AI_CASE_RETURN(PipelineFactory_generate);
    AI_CASE_RETURN(PipelineFactory_done);
  }
  AI_NEVER_REACHED
}

void PipelineFactory::multiplex_impl(state_type run_state)
{
  switch (run_state)
  {
    case PipelineFactory_start:
    {
      // Get the- or create a task::PipelineCache object that is associated with m_broker_key.
      vulkan::pipeline::CacheBrokerKey broker_key;
      broker_key.set_logical_device(&m_owning_window->logical_device());
      broker_key.set_owning_factory(this);
      m_pipeline_cache_task = m_broker->run(broker_key, [this](bool success){ Dout(dc::notice, "pipeline cache set up!"); signal(pipeline_cache_set_up); });
      // Wait until the pipeline cache is ready, then continue with PipelineFactory_initialize.
      set_state(PipelineFactory_initialize);
      wait(pipeline_cache_set_up);
      break;
    }
    case PipelineFactory_initialize:
      // Wait until the user is done adding CharacteristicRange objects and called generate().
      set_state(PipelineFactory_initialized);
      wait(fully_initialized);
      break;
    case PipelineFactory_initialized:
    {
      // Start a synchronous task that will be run when this task, that runs asynchronously, is finished.
      m_finished_watcher = statefultask::create<synchronous::PipelineFactoryWatcher>(m_owning_window COMMA_CWDEBUG_ONLY(mSMDebug));
      m_finished_watcher->run();
      // Do not use an empty factory - it makes no sense.
      ASSERT(!m_characteristics.empty());
      vulkan::pipeline::Index max_pipeline_index{0};
      // Call initialize on each characteristic.
      for (int i = 0; i < m_characteristics.size(); ++i)
      {
        m_characteristics[i]->initialize(m_flat_create_info, m_owning_window);
        m_characteristics[i]->update(max_pipeline_index, m_characteristics[i]->iend() - 1);
      }
      // max_pipeline_index is now equal to the maximum value that a pipeline_index can be.
      m_graphics_pipelines.resize(max_pipeline_index.get_value() + 1);

      // Start as many for loops as there are characteristics.
      m_range_counters.initialize(m_characteristics.size(), m_characteristics[0]->ibegin());
      // Enter the multi-loop.
      set_state(PipelineFactory_generate);
      [[fallthrough]];
    }
    case PipelineFactory_generate:
      // Each time we enter from the top, m_range_counters.finished() should be false.
      // It is therefore the same as entering at the bottom of the while loop at *).
      for (; !m_range_counters.finished(); m_range_counters.next_loop())                // This is just MultiLoop magic; these two lines result in
        while (m_range_counters() < m_characteristics[*m_range_counters]->iend())       // having multiple for loops inside eachother.
        {
          // Set b to a random magic value to indicate that we are in the inner loop.
          int b = std::numeric_limits<int>::max();
          // Required MultiLoop magic.
          if (m_range_counters.inner_loop())
          {
            // MultiLoop inner loop body.

            vulkan::pipeline::Index pipeline_index{0};

            // Run over each loop variable (and hence characteristic).
            for (int i = 0; i < m_characteristics.size(); ++i)
            {
              // Call fill with its current range index.
              m_characteristics[i]->fill(m_flat_create_info, m_range_counters[i]);
              // Calculate the pipeline_index.
              m_characteristics[i]->update(pipeline_index, m_range_counters[i]);
            }

            // Merge the results of all characteristics into local vectors.
            std::vector<vk::VertexInputBindingDescription>     const vertex_input_binding_descriptions      = m_flat_create_info.get_vertex_input_binding_descriptions();
            std::vector<vk::VertexInputAttributeDescription>   const vertex_input_attribute_descriptions    = m_flat_create_info.get_vertex_input_attribute_descriptions();
            std::vector<vk::PipelineShaderStageCreateInfo>     const pipeline_shader_stage_create_infos     = m_flat_create_info.get_pipeline_shader_stage_create_infos();
            std::vector<vk::PipelineColorBlendAttachmentState> const pipeline_color_blend_attachment_states = m_flat_create_info.get_pipeline_color_blend_attachment_states();
            std::vector<vk::DynamicState>                      const dynamic_state                          = m_flat_create_info.get_dynamic_states();

            vk::PipelineVertexInputStateCreateInfo pipeline_vertex_input_state_create_info{
              .vertexBindingDescriptionCount = static_cast<uint32_t>(vertex_input_binding_descriptions.size()),
              .pVertexBindingDescriptions = vertex_input_binding_descriptions.data(),
              .vertexAttributeDescriptionCount = static_cast<uint32_t>(vertex_input_attribute_descriptions.size()),
              .pVertexAttributeDescriptions = vertex_input_attribute_descriptions.data()
            };

            vk::PipelineDynamicStateCreateInfo pipeline_dynamic_state_create_info{
              .dynamicStateCount = static_cast<uint32_t>(dynamic_state.size()),
              .pDynamicStates = dynamic_state.data()
            };

            vk::GraphicsPipelineCreateInfo pipeline_create_info{
              .stageCount = static_cast<uint32_t>(pipeline_shader_stage_create_infos.size()),
              .pStages = pipeline_shader_stage_create_infos.data(),
              .pVertexInputState = &pipeline_vertex_input_state_create_info,
              .pInputAssemblyState = &m_flat_create_info.m_pipeline_input_assembly_state_create_info,
              .pTessellationState = nullptr,
              .pViewportState = &m_flat_create_info.m_viewport_state_create_info,
              .pRasterizationState = &m_flat_create_info.m_rasterization_state_create_info,
              .pMultisampleState = &m_flat_create_info.m_multisample_state_create_info,
              .pDepthStencilState = &m_flat_create_info.m_depth_stencil_state_create_info,
              .pColorBlendState = &m_flat_create_info.m_color_blend_state_create_info,
              .pDynamicState = &pipeline_dynamic_state_create_info,
              .layout = m_vh_pipeline_layout,
              .renderPass = m_vh_render_pass,
              .subpass = 0,
              .basePipelineHandle = vk::Pipeline{},
              .basePipelineIndex = -1
            };

#ifdef CWDEBUG
            Dout(dc::vulkan|continued_cf, "PipelineFactory [" << this << "] creating graphics pipeline with range values: ");
            char const* prefix = "";
            for (int i = 0; i < m_characteristics.size(); ++i)
            {
              Dout(dc::continued, prefix << m_range_counters[i]);
              prefix = ", ";
            }
            Dout(dc::finish, " --> pipeline::Index " << pipeline_index);
#endif

            // Create and then store the graphics pipeline.
            m_graphics_pipelines[pipeline_index] = m_owning_window->logical_device().create_graphics_pipeline(m_pipeline_cache_task->vh_pipeline_cache(), pipeline_create_info
                COMMA_CWDEBUG_ONLY({ m_owning_window, "pipeline" }));

            // Inform the SynchronousWindow.
            m_finished_watcher->have_new_pipeline({m_pipeline_factory_index , pipeline_index});

            // End of MultiLoop inner loop.
          }
          else
            // This is not the inner loop; set b to the start value of the next loop.
            b = m_characteristics[*m_range_counters + 1]->ibegin();                     // Each loop, one per characteristic, runs from ibegin() till iend().
          // Required MultiLoop magic.
          m_range_counters.start_next_loop_at(b);
          // If this is true then we're at the end of the inner loop and want to yield.
          if (b == std::numeric_limits<int>::max() && !m_range_counters.finished())
          {
            yield();
            return;     // Yield and then continue here --.
          }             //                                |
          // *) <-- actual entry point of this state <----'
        }
      set_state(PipelineFactory_done);
      [[fallthrough]];
    case PipelineFactory_done:
      m_finished_watcher->terminate();
      finish();
      break;
  }
}

} // namespace task