cmake_minimum_required(VERSION 3.12...3.19)

include(AICxxProject)

find_package(Vulkan REQUIRED COMPONENTS glslc)
find_program(glslc_executable NAMES glslc HINTS Vulkan::glslc)

find_package(Eigen3 3.3 REQUIRED NO_MODULE)
find_package(Boost REQUIRED COMPONENTS serialization)

find_package(PkgConfig REQUIRED)
pkg_check_modules(Shaderc REQUIRED IMPORTED_TARGET shaderc)

find_package(magic_enum REQUIRED)

#==============================================================================
# BUILD PROJECT
#

# This project is an OBJECT-library, used by other git submodules and the main project.
add_library(vulkan_ObjLib OBJECT)

# Set SOURCE_DIR to the current directory.
set_property(
  TARGET vulkan_ObjLib
  PROPERTY SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}"
)

file(GLOB SOURCE_FILES
  ${CMAKE_CURRENT_SOURCE_DIR}/*.cxx
  ${CMAKE_CURRENT_SOURCE_DIR}/rendergraph/*.cxx
  ${CMAKE_CURRENT_SOURCE_DIR}/pipeline/*.cxx
  ${CMAKE_CURRENT_SOURCE_DIR}/pipeline/partitions/*.cxx
  ${CMAKE_CURRENT_SOURCE_DIR}/queues/*.cxx
  ${CMAKE_CURRENT_SOURCE_DIR}/memory/*.cxx
  ${CMAKE_CURRENT_SOURCE_DIR}/vk_utils/*.cxx
  ${CMAKE_CURRENT_SOURCE_DIR}/infos/*.cxx
  ${CMAKE_CURRENT_SOURCE_DIR}/math/*.cxx
  ${CMAKE_CURRENT_SOURCE_DIR}/descriptor/*.cxx
  ${CMAKE_CURRENT_SOURCE_DIR}/tracy/*.cxx
)

file(GLOB HEADER_FILES
  ${CMAKE_CURRENT_SOURCE_DIR}/*.h
  ${CMAKE_CURRENT_SOURCE_DIR}/rendergraph/*.h
  ${CMAKE_CURRENT_SOURCE_DIR}/pipeline/*.h
  ${CMAKE_CURRENT_SOURCE_DIR}/pipeline/partitions/*.h
  ${CMAKE_CURRENT_SOURCE_DIR}/queues/*.h
  ${CMAKE_CURRENT_SOURCE_DIR}/memory/*.h
  ${CMAKE_CURRENT_SOURCE_DIR}/vk_utils/*.h
  ${CMAKE_CURRENT_SOURCE_DIR}/infos/*.h
  ${CMAKE_CURRENT_SOURCE_DIR}/math/*.h
  ${CMAKE_CURRENT_SOURCE_DIR}/descriptor/*.h
  ${CMAKE_CURRENT_SOURCE_DIR}/tracy/*.h
)

# The list of source files.
target_sources(vulkan_ObjLib
  PRIVATE
    ${SOURCE_FILES}
    ${HEADER_FILES}
)

# Add debug source files.
if (OptionEnableLibcwd)
  file(GLOB DEBUG_SOURCE_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/debug/*.cxx
  )

  file(GLOB DEBUG_HEADER_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/debug/*.h
  )

  # Add to the list of source files.
  target_sources(vulkan_ObjLib
    PRIVATE
      ${DEBUG_SOURCE_FILES}
      ${DEBUG_HEADER_FILES}
  )
endif ()

# Required include search-paths.
get_target_property(CWDS_INTERFACE_INCLUDE_DIRECTORIES AICxx::cwds INTERFACE_INCLUDE_DIRECTORIES)
target_include_directories(vulkan_ObjLib
  PRIVATE
    "${CMAKE_SOURCE_DIR}/external/peelo-xdg/include"
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CWDS_INTERFACE_INCLUDE_DIRECTORIES}" # For sys.h and debug.h.
)

target_compile_definitions(vulkan_ObjLib
  PUBLIC
    VK_USE_PLATFORM_XCB_KHR
    VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
    VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
    VULKAN_HPP_NO_UNION_CONSTRUCTORS
)

# Require support for C++20.
target_compile_features(vulkan_ObjLib
  PUBLIC cxx_std_20
)

# Set link dependencies.
target_link_libraries(vulkan_ObjLib
  PUBLIC
    LinuxViewer::shader_builder
    VulkanMemoryAllocator
    Vulkan::Vulkan
    AICxx::xcb-task
    AICxx::xcb-task::OrgFreedesktopXcbError
    AICxx::utils
    farmhash::farmhash
    ${CMAKE_DL_LIBS}
    PkgConfig::Shaderc
    ImGui::imgui
    Eigen3::Eigen
    Boost::serialization
    Tracy::TracyClient
)

# Create an ALIAS target.
add_library(LinuxViewer::vulkan ALIAS vulkan_ObjLib)

# Test executable.
add_executable(allocator_test tests/allocator_test.cxx)
target_link_libraries(allocator_test ${AICXX_OBJECTS_LIST})

# Math library.
add_subdirectory(math)
add_subdirectory(shader_builder)
