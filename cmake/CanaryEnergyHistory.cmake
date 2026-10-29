# Optional observed-power/reset-window module. Native runtime wiring requires a separate development opt-in.
option(CANARY_ENABLE_ENERGY_RUNTIME "Enable development-only native persistence controls and fenced energy collection" OFF)
add_library(canary_energy_history STATIC
  "${CMAKE_SOURCE_DIR}/src/product/private_entropy.cpp"
  "${CMAKE_SOURCE_DIR}/src/product/energy_history_model.cpp"
  "${CMAKE_SOURCE_DIR}/src/product/energy_compaction.cpp"
  "${CMAKE_SOURCE_DIR}/src/product/energy_ledger.cpp"
  "${CMAKE_SOURCE_DIR}/src/product/energy_history_process.cpp"
  "${CMAKE_SOURCE_DIR}/src/product/energy_recovery.cpp"
  "${CMAKE_SOURCE_DIR}/src/product/energy_capture.cpp"
  "${CMAKE_SOURCE_DIR}/src/product/energy_authorization.cpp"
  "${CMAKE_SOURCE_DIR}/src/product/energy_authorization_owner.cpp"
  "${CMAKE_SOURCE_DIR}/src/product/energy_collector_process.cpp"
  "${CMAKE_SOURCE_DIR}/src/product/energy_leased_capture.cpp"
  "${CMAKE_SOURCE_DIR}/src/product/energy_runtime.cpp"
  "${CMAKE_SOURCE_DIR}/src/product/energy_runtime_paths.cpp"
  "${CMAKE_SOURCE_DIR}/src/product/energy_history_owner.cpp")
target_compile_features(canary_energy_history PUBLIC cxx_std_17)
target_include_directories(canary_energy_history PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(canary_energy_history PUBLIC canary_economics PRIVATE canary_local_tasks ${CMAKE_DL_LIBS} Threads::Threads)
if(WIN32)
  target_link_libraries(canary_energy_history PRIVATE bcrypt)
endif()

function(canary_enable_energy_runtime target)
  target_compile_definitions(${target} PRIVATE CANARY_ENABLE_ENERGY_RUNTIME=1)
  target_link_libraries(${target} PRIVATE canary_energy_history)
  add_dependencies(${target} canary_energy_authorization_worker canary_energy_collector_worker)
  # Same directory for single- and multi-config generators. Runtime lookup never searches build subdirectories.
  set_target_properties(${target} canary_energy_authorization_worker canary_energy_collector_worker PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
endfunction()
add_executable(canary_energy_authorization_worker "${CMAKE_SOURCE_DIR}/src/product/energy_authorization_worker.cpp")
target_link_libraries(canary_energy_authorization_worker PRIVATE canary_energy_history)
add_executable(canary_energy_collector_worker "${CMAKE_SOURCE_DIR}/src/product/energy_collector_worker.cpp")
target_link_libraries(canary_energy_collector_worker PRIVATE canary_energy_history)
add_executable(canary_energy_history_worker "${CMAKE_SOURCE_DIR}/src/product/energy_history_worker.cpp")
target_link_libraries(canary_energy_history_worker PRIVATE canary_energy_history)
if(MSVC)
  target_compile_definitions(canary_energy_history PRIVATE NOMINMAX)
  target_compile_options(canary_energy_history PRIVATE /W3 /utf-8)
else()
  target_compile_options(canary_energy_history PRIVATE -Wall -Wextra -Werror=format)
endif()
