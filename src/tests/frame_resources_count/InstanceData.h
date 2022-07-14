#pragma once

#include "shaderbuilder/ShaderVariableLayout.h"
#include "math/glsl.h"

// Struct describing data type and format of instance attributes.
struct InstanceData
{
  glsl::vec4 m_position;
};

namespace vulkan::shaderbuilder {

template<>
struct ShaderVariableLayouts<InstanceData>
{
  static constexpr vk::VertexInputRate input_rate = vk::VertexInputRate::eInstance;     // This is per instance data.
  static constexpr std::array<ShaderVariableLayout, 1> layouts = {{
    { Type::vec4, "InstanceData::m_position", offsetof(InstanceData, m_position) }
  }};
};

} // namespace vulkan::shaderbuilder