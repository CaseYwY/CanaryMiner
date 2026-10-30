# Offline exact comparison only. No runtime controller or public source is activated by linking.
add_library(canary_optimizer STATIC "${CMAKE_SOURCE_DIR}/src/product/economic_study.cpp" "${CMAKE_SOURCE_DIR}/src/product/optimization_schedule.cpp"
  "${CMAKE_SOURCE_DIR}/src/product/economic_study_use.cpp")
target_compile_features(canary_optimizer PUBLIC cxx_std_17)
target_include_directories(canary_optimizer PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(canary_optimizer PUBLIC canary_work_profiles canary_forecast)
if(MSVC)
  target_compile_definitions(canary_optimizer PRIVATE NOMINMAX)
  target_compile_options(canary_optimizer PRIVATE /W3 /utf-8)
else()
  target_compile_options(canary_optimizer PRIVATE -Wall -Wextra -Werror)
endif()
