project(linux_vulkan_engine
  LANGUAGES CXX
  DESCRIPTION "Implementation of Uniform Buffers Test."
)

include(AICxxProject)

# Required for reflection_test2.
find_package(Eigen3 3.3 REQUIRED NO_MODULE)

#==============================================================================
# RESOURCES
#

set(RESOURCES_SUBDIRS textures)

set(RESOURCES_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/resources)
set(RESOURCES_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/data)

set(resources_src_files "")
set(resources_dst_files "")
foreach(subdir IN LISTS RESOURCES_SUBDIRS)
  file(GLOB_RECURSE resource_src_files ${RESOURCES_SOURCE_DIR}/${subdir}/*)
  list(APPEND resources_src_files ${resource_src_files})
  foreach(file IN LISTS resource_src_files)
    file(RELATIVE_PATH resource_dst_file ${RESOURCES_SOURCE_DIR} ${file})
    list(APPEND resources_dst_files ${RESOURCES_BINARY_DIR}/${resource_dst_file})
  endforeach()
endforeach()

foreach(subdir IN LISTS RESOURCES_SUBDIRS)
  add_custom_command(
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${RESOURCES_SOURCE_DIR}/${subdir} ${RESOURCES_BINARY_DIR}/${subdir}
    OUTPUT  ${resources_dst_files}
    DEPENDS ${resources_src_files}
    COMMENT "Copying ${subdir}"
  )
  list(APPEND RESOURCES_TARGETS ${resources_dst_files})
endforeach()

add_custom_target(uniform_buffers_test_resources ALL DEPENDS ${RESOURCES_TARGETS})

#==============================================================================

add_executable(uniform_buffers_test
  UniformBuffersTest.cxx
  UniformBuffersTest.h
  Window.h
  LogicalDevice.h
)

target_include_directories(uniform_buffers_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(uniform_buffers_test
  PRIVATE
    LinuxViewer::vulkan
    LinuxViewer::shader_builder
    AICxx::xcb-task
    AICxx::xcb-task::OrgFreedesktopXcbError
    AICxx::resolver-task
    ImGui::imgui
    ${AICXX_OBJECTS_LIST}
    dns::dns
)

add_executable(reflection_test2 EXCLUDE_FROM_ALL
  reflection_test2.cpp
)

target_include_directories(reflection_test2
  PRIVATE
    ${CMAKE_SOURCE_DIR}/src/vulkan
)

target_link_libraries(reflection_test2
  PRIVATE
    AICxx::cwds
    Eigen3::Eigen
)

add_executable(horse_race EXCLUDE_FROM_ALL
  horse_race.cpp
)

target_link_libraries(horse_race
  PRIVATE
    AICxx::cwds
)
