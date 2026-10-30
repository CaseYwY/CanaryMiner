# Isolated readonly raw safety observations; ordinary startup does not construct this owner/helper.
add_library(canary_safety_telemetry STATIC
  "${CMAKE_SOURCE_DIR}/src/product/safety_telemetry.cpp"
  "${CMAKE_SOURCE_DIR}/src/product/safety_telemetry_owner.cpp")
target_compile_features(canary_safety_telemetry PUBLIC cxx_std_17)
target_include_directories(canary_safety_telemetry PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(canary_safety_telemetry PRIVATE canary_local_tasks Threads::Threads)
add_library(canary_safety_telemetry_native STATIC "${CMAKE_SOURCE_DIR}/src/product/safety_telemetry_native.cpp")
target_link_libraries(canary_safety_telemetry_native PUBLIC canary_safety_telemetry PRIVATE $<$<PLATFORM_ID:Linux>:dl>)
foreach(target canary_safety_telemetry canary_safety_telemetry_native)
  if(MSVC)
    target_compile_definitions(${target} PRIVATE NOMINMAX)
    target_compile_options(${target} PRIVATE /W3 /utf-8)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
  endif()
endforeach()
add_executable(canary_safety_telemetry_worker "${CMAKE_SOURCE_DIR}/src/product/safety_telemetry_worker.cpp")
target_link_libraries(canary_safety_telemetry_worker PRIVATE canary_safety_telemetry_native)
