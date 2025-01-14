#pragma once

#include "ShaderVariable.h"
#include "BasicType.h"
#ifdef CWDEBUG
#include "debug/vulkan_print_on.h"
#endif
#include "debug.h"

namespace vulkan::shader_builder {

class PushConstant final : public ShaderVariable
{
  BasicType m_type;                             // For example vec3.
  uint32_t const m_offset;                      // The offset of the variable inside its C++ ENTRY struct.
  uint32_t const m_array_size;                  // Set to zero when this is not an array.

 public:
  PushConstant(BasicType type, char const* glsl_id_full, uint32_t offset, uint32_t array_size = 0) :
    ShaderVariable(glsl_id_full), m_type(type), m_offset(offset), m_array_size(array_size) { }

  uint32_t offset() const
  {
    return m_offset;
  }

  uint32_t size() const
  {
    uint32_t sz = m_type.size();
    if (m_array_size > 0)
      sz *= m_array_size;
    return sz;
  }

  BasicType basic_type() const
  {
    return m_type;
  }

  uint32_t elements() const
  {
    return m_array_size;
  }

 private:
  // Implement base class interface.
  DeclarationContext* is_used_in(vk::ShaderStageFlagBits shader_stage, pipeline::ShaderInputData* shader_input_data) const override;
  std::string name() const override;

#ifdef CWDEBUG
 public:
  void print_on(std::ostream& os) const override;
#endif
};

} // namespace vulkan::shader_builder
