# Episode/profit core and opt-in isolated readonly detection. No ordinary mining activation.
add_library(canary_games STATIC "${CMAKE_SOURCE_DIR}/src/product/game_profit.cpp" "${CMAKE_SOURCE_DIR}/src/product/game_guard.cpp")
target_compile_features(canary_games PUBLIC cxx_std_17)
target_include_directories(canary_games PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(canary_games PUBLIC canary_forecast)
if(MSVC)
  target_compile_definitions(canary_games PRIVATE NOMINMAX)
  target_compile_options(canary_games PRIVATE /W3 /utf-8)
else()
  target_compile_options(canary_games PRIVATE -Wall -Wextra -Werror)
endif()
add_library(canary_game_detection STATIC
  "${CMAKE_SOURCE_DIR}/src/product/game_detection.cpp"
  "${CMAKE_SOURCE_DIR}/src/product/game_detection_native.cpp"
  "${CMAKE_SOURCE_DIR}/src/product/game_detection_owner.cpp"
  "${CMAKE_SOURCE_DIR}/src/product/game_runtime.cpp"
  "${CMAKE_SOURCE_DIR}/src/product/private_entropy.cpp")
target_compile_features(canary_game_detection PUBLIC cxx_std_17)
target_include_directories(canary_game_detection PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(canary_game_detection PUBLIC canary_games PRIVATE canary_local_tasks Threads::Threads)
if(WIN32)
  target_link_libraries(canary_game_detection PRIVATE bcrypt)
endif()
if(MSVC)
  target_compile_definitions(canary_game_detection PRIVATE NOMINMAX)
  target_compile_options(canary_game_detection PRIVATE /W3 /utf-8)
else()
  target_compile_options(canary_game_detection PRIVATE -Wall -Wextra -Werror)
endif()
add_executable(canary_game_detection_worker "${CMAKE_SOURCE_DIR}/src/product/game_detection_worker.cpp")
target_link_libraries(canary_game_detection_worker PRIVATE canary_game_detection)
