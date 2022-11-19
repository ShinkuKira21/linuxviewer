#pragma once

#include "descriptor/CombinedImageSampler.h"

namespace vulkan::shader_builder::shader_resource {

class CombinedImageSampler
{
 private:
  boost::intrusive_ptr<descriptor::CombinedImageSampler> m_descriptor_task;

 public:
  // Note: the call to set_glsl_id_postfix creates and runs m_descriptor_task,
  // but that task won't do anything until after the same thread had a chance
  // to call set_array_size (if this is an array) to finish the initialization.
  void set_glsl_id_postfix(char const* glsl_id_full_postfix);
  void set_array_size(uint32_t array_size);

  // Accessor.
  descriptor::CombinedImageSampler const* descriptor_task() const { return m_descriptor_task.get(); }

#ifdef CWDEBUG
 public:
  void print_on(std::ostream& os) const;
#endif
};

} // namespace vulkan::shader_builder::shader_resource