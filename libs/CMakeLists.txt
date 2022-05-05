add_subdirectory(thread_pool)

add_library(libs INTERFACE)
target_link_libraries(libs INTERFACE thread_pool)