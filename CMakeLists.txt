cmake_minimum_required(VERSION 3.16)
project(task1)
add_compile_options(-ggdb3 -masm=intel) 
add_executable(task1 task1.cpp)
target_link_libraries(task1 QBDI)
set_target_properties(task1 PROPERTIES CXX_STANDARD 11
                                               CXX_STANDARD_REQUIRED ON)

if(QBDI_TOOLS_QBDIPRELOAD)
  add_library(tracer_preload_cpp SHARED tracer_preload.cpp)
  target_link_libraries(tracer_preload_cpp QBDIPreload QBDI_static)
  set_target_properties(tracer_preload_cpp PROPERTIES CXX_STANDARD 11
                                                      CXX_STANDARD_REQUIRED ON)
endif()
