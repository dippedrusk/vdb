add_executable(vdb vdb.cpp)
target_link_libraries(vdb PRIVATE vdb::libvdb)

include(GNUInstallDirs)
install(
	TARGETS vdb
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
