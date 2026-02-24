add_executable(tests tests.cpp)
target_link_libraries(tests PRIVATE vdb::libvdb Catch2::Catch2WithMain)
