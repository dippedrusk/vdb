install(
	EXPORT vdb-targets
	FILE vdb-config.cmake
	NAMESPACE vdb::
	DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/vdb
)
