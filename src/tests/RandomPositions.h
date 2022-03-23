#pragma once

#include "InstanceData.h"
#include "SampleParameters.h"
#include "vulkan/shaderbuilder/VertexShaderInputSet.h"
#include <random>

class RandomPositions final : public vulkan::shaderbuilder::VertexShaderInputSet<InstanceData>
{
  std::random_device m_random_device;
  std::mt19937 m_generator;
  std::uniform_real_distribution<float> m_distribution_xy;
  std::uniform_real_distribution<float> m_distribution_z;

 public:
  RandomPositions() : m_generator(m_random_device()), m_distribution_xy(-1.0f, 1.0f), m_distribution_z(0.0f, 1.0f) { }

 private:
  int count() const override
  {
    return SampleParameters::s_max_object_count;
  }

  void create_entry(InstanceData* input_entry_ptr) override final
  {
    input_entry_ptr->m_position << m_distribution_xy(m_generator),
                                   m_distribution_xy(m_generator),
                                   m_distribution_z(m_generator),
                                   0.0f;                                // Homogeneous coordinates. This is used as an offset (a vector).
  }
};