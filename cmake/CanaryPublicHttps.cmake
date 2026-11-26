# No automatic dependency download or configure-time network access. Supply reviewed CURL/c-ares packages explicitly.
option(CANARY_ENABLE_PUBLIC_HTTPS "Build optional verified public JSON HTTPS transport (libcurl + c-ares)" OFF)

# The miner links the process client and shared validation only. The installed
# worker and the backend tests are the only users of the socket/TLS client.
add_library(canary_public_https STATIC
  "${CMAKE_SOURCE_DIR}/src/product/public_https_contract.cpp"
  "${CMAKE_SOURCE_DIR}/src/product/public_json_process.cpp")
if(WIN32)
  target_link_libraries(canary_public_https PUBLIC ws2_32)
endif()
add_library(canary_public_https_client STATIC EXCLUDE_FROM_ALL
  "${CMAKE_SOURCE_DIR}/src/product/public_https.cpp")
target_link_libraries(canary_public_https_client PUBLIC canary_public_https)
target_compile_features(canary_public_https PUBLIC cxx_std_17)
target_include_directories(canary_public_https PUBLIC "${CMAKE_SOURCE_DIR}/src")
foreach(target canary_public_https canary_public_https_client)
  if(MSVC)
    target_compile_definitions(${target} PRIVATE NOMINMAX)
    target_compile_options(${target} PRIVATE /W3 /utf-8)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Werror=format)
  endif()
endforeach()
if(CANARY_ENABLE_PUBLIC_HTTPS)
  find_package(CURL 8.12 REQUIRED)
  target_compile_definitions(canary_public_https_client PRIVATE CANARY_PUBLIC_HTTPS=1)
  target_link_libraries(canary_public_https_client PRIVATE CURL::libcurl)
  add_executable(canary_public_json_worker "${CMAKE_SOURCE_DIR}/src/product/public_json_worker.cpp")
  target_link_libraries(canary_public_json_worker PRIVATE canary_public_https_client)
endif()
