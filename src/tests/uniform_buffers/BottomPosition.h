#pragma once

#include "shader_builder/ShaderVariableLayouts.h"

struct BottomPosition;

LAYOUT_DECLARATION(BottomPosition, uniform_std140)
{
  static constexpr auto struct_layout = make_struct_layout(
    LAYOUT(vec2, unused),
    LAYOUT(Float, x)
  );
};

// Struct describing data type and format of uniform block.
STRUCT_DECLARATION(BottomPosition)
{
  MEMBER(0, vec2, unused);
  MEMBER(1, Float, x);
};
