# Native host-side market data/math only. Creating targets does not start public requests.
add_library(canary_market STATIC
  "${CMAKE_SOURCE_DIR}/src/product/exact_decimal.cpp"
  "${CMAKE_SOURCE_DIR}/src/product/market_data.cpp"
  "${CMAKE_SOURCE_DIR}/src/product/market_owner.cpp")
target_compile_features(canary_market PUBLIC cxx_std_17)
target_include_directories(canary_market PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(canary_market PUBLIC canary_public_https)
find_package(Threads REQUIRED)
target_link_libraries(canary_market PRIVATE Threads::Threads)
if(MSVC)
  target_compile_options(canary_market PRIVATE /W3 /utf-8)
else()
  target_compile_options(canary_market PRIVATE -Wall -Wextra -Werror=format)
endif()
