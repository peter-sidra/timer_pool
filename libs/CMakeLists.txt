add_subdirectory(thread_pool)

add_library(timer_pool_external_libs INTERFACE)
target_link_libraries(timer_pool_external_libs INTERFACE thread_pool)