# Independent exact posted-credit store. No provider polling, credentials or income display activation.
add_library(canary_credits STATIC
  "${CMAKE_SOURCE_DIR}/src/product/credit_ledger.cpp"
  "${CMAKE_SOURCE_DIR}/src/product/local_task_process.cpp"
  "${CMAKE_SOURCE_DIR}/src/product/credit_process.cpp"
  "${CMAKE_SOURCE_DIR}/src/product/credit_recovery.cpp"
  "${CMAKE_SOURCE_DIR}/src/product/credit_owner.cpp")
target_compile_features(canary_credits PUBLIC cxx_std_17)
target_include_directories(canary_credits PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(canary_credits PUBLIC canary_market PRIVATE ${CMAKE_DL_LIBS})
find_package(Threads REQUIRED)
target_link_libraries(canary_credits PRIVATE Threads::Threads)
add_executable(canary_credit_worker "${CMAKE_SOURCE_DIR}/src/product/credit_worker.cpp")
target_link_libraries(canary_credit_worker PRIVATE canary_credits)
if(MSVC)
  target_compile_definitions(canary_credits PRIVATE NOMINMAX)
  target_compile_options(canary_credits PRIVATE /W3 /utf-8)
else()
  target_compile_options(canary_credits PRIVATE -Wall -Wextra -Werror=format)
endif()
