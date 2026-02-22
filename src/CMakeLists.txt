add_library(libvdb libvdb.cpp)
add_library(vdb::libvdb ALIAS libvdb)

set_target_properties(
	libvdb
	PROPERTIES OUTPUT_NAME vdb
)

target_compile_features(libvdb PUBLIC cxx_std_17)

target_include_directories(
	libvdb
	PRIVATE include
	PUBLIC ${CMAKE_SOURCE_DIR}/include
)
