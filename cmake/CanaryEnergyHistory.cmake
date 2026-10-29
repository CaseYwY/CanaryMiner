# Optional local observed-power/reset-window module. No production collector/persistence activation.
add_library(canary_energy_history STATIC
  "${CMAKE_SOURCE_DIR}/src/product/energy_history_model.cpp"
  "${CMAKE_SOURCE_DIR}/src/product/energy_compaction.cpp"
  "${CMAKE_SOURCE_DIR}/src/product/energy_ledger.cpp"
  "${CMAKE_SOURCE_DIR}/src/product/energy_history_process.cpp"
  "${CMAKE_SOURCE_DIR}/src/product/energy_recovery.cpp"
  "${CMAKE_SOURCE_DIR}/src/product/energy_history_owner.cpp")
target_compile_features(canary_energy_history PUBLIC cxx_std_17)
target_include_directories(canary_energy_history PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(canary_energy_history PUBLIC canary_economics PRIVATE canary_local_tasks ${CMAKE_DL_LIBS} Threads::Threads)
add_executable(canary_energy_history_worker "${CMAKE_SOURCE_DIR}/src/product/energy_history_worker.cpp")
target_link_libraries(canary_energy_history_worker PRIVATE canary_energy_history)
if(MSVC)
  target_compile_definitions(canary_energy_history PRIVATE NOMINMAX)
  target_compile_options(canary_energy_history PRIVATE /W3 /utf-8)
else()
  target_compile_options(canary_energy_history PRIVATE -Wall -Wextra -Werror=format)
endif()
