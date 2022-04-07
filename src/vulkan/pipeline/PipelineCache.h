#pragma once

#include "vk_utils/TaskToTaskDeque.h"
#include "statefultask/AIStatefulTask.h"
#include "threadsafe/aithreadsafe.h"
#include "utils/nearest_multiple_of_power_of_two.h"
#include "utils/ulong_to_base.h"
#include "debug.h"
#include "debug/DebugSetName.h"
#include <boost/serialization/serialization.hpp>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <vulkan/vulkan.hpp>
#include <filesystem>
#include <cstdlib>

namespace task {

class PipelineCache : public vk_utils::TaskToTaskDeque<AIStatefulTask, vk::UniquePipelineCache> // Other PipelineCache tasks can pass their pipeline cache for merging.
{
 public:
  static constexpr condition_type condition_flush_to_disk = 2;
  static constexpr condition_type factory_finished = 4;

 private:
  // Constructor.
  task::PipelineFactory* m_owning_factory;              // We have one pipeline cache per factory - or each factory would still be
                                                        // slowed down as a result of concurrent accesses to the cache.
  // State PipelineCache_load_from_disk.
  vk::UniquePipelineCache m_pipeline_cache;

  bool m_is_merger = false;

#ifdef CWDEBUG
  vulkan::Ambifix m_create_ambifix;
#endif

 protected:
  // The different states of the stateful task.
  enum PipelineCache_state_type {
    PipelineCache_initialize = direct_base_type::state_end,
    PipelineCache_load_from_disk,
    PipelineCache_ready,
    PipelineCache_factory_finished,
    PipelineCache_factory_merge,
    PipelineCache_save_to_disk,
    PipelineCache_done,
  };

 public:
  // One beyond the largest state of this task.
  static constexpr state_type state_end = PipelineCache_done + 1;

  // Called by Application::pipeline_factory_done.
  void set_is_merger() { m_is_merger = true; }

 protected:
  // boost::serialization::access has a destroy function that want to call operator delete on us. Of course, we will never call destroy.
  friend class boost::serialization::access;
  ~PipelineCache() override                     // Call finish() (or abort()), not delete.
  {
    DoutEntering(dc::statefultask(mSMDebug), "~PipelineCache() [" << (void*)this << "]");
  }

  // Implemenation of state_str for run states.
  char const* state_str_impl(state_type run_state) const override;

  // Handle mRunState.
  void multiplex_impl(state_type run_state) override;

 public:
  PipelineCache(PipelineFactory* factory COMMA_CWDEBUG_ONLY(bool debug = false)) : direct_base_type(CWDEBUG_ONLY(debug)), m_owning_factory(factory)
  {
    Debug(m_create_ambifix = vulkan::Ambifix("PipelineCache", "[" + utils::ulong_to_base(reinterpret_cast<uint64_t>(this), "0123456789abcdef") + "]"));
  }

  std::filesystem::path get_filename() const;

  void clear_cache();

  void load(boost::archive::binary_iarchive& archive, unsigned int const version);
  void save(boost::archive::binary_oarchive& archive, unsigned int const version) const;

  // Accessor for the create pipeline cache.
  vk::PipelineCache vh_pipeline_cache() const { return *m_pipeline_cache; }

  // Rescue pipeline cache just before deleting this task. Called by Application::pipeline_factory_done.
  vk::UniquePipelineCache detach_pipeline_cache() { return std::move(m_pipeline_cache); }

  BOOST_SERIALIZATION_SPLIT_MEMBER()
};

} // namespace task
