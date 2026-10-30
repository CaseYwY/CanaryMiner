# Explicit measurement capture/audit; linking starts no source, command owner or hardware access.
add_library(canary_work_profiles STATIC "${CMAKE_SOURCE_DIR}/src/product/work_profile.cpp" "${CMAKE_SOURCE_DIR}/src/product/work_measurement.cpp"
  "${CMAKE_SOURCE_DIR}/src/product/work_experiment_owner.cpp" "${CMAKE_SOURCE_DIR}/src/product/work_experiment_budget.cpp")
target_compile_features(canary_work_profiles PUBLIC cxx_std_17)
target_include_directories(canary_work_profiles PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(canary_work_profiles PUBLIC canary_economics canary_worker_control)
if(MSVC)
  target_compile_definitions(canary_work_profiles PRIVATE NOMINMAX)
  target_compile_options(canary_work_profiles PRIVATE /W3 /utf-8)
else()
  target_compile_options(canary_work_profiles PRIVATE -Wall -Wextra -Werror)
endif()
