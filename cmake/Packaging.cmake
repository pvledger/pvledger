# nFPM is optional for normal builds. Resolve it when a packaging target is run.
set(PVLEDGER_PACKAGE_RELEASE "1" CACHE STRING "Native package revision")
set(PVLEDGER_NFPM "nfpm" CACHE STRING "nFPM executable (path or command name)")
set(package_dir "${CMAKE_CURRENT_BINARY_DIR}/packaging")
file(MAKE_DIRECTORY "${package_dir}")
configure_file(packaging/pvledger.init.in "${package_dir}/pvledger.init" @ONLY)
configure_file(packaging/pvledger.service.in "${package_dir}/pvledger.service" @ONLY)
configure_file(packaging/nfpm.yaml.in "${package_dir}/nfpm.yaml" @ONLY)
configure_file(cmake/Package.cmake.in "${package_dir}/Package.cmake" @ONLY)
install(PROGRAMS "${package_dir}/pvledger.init" DESTINATION ${CMAKE_INSTALL_DOCDIR} COMPONENT PVLedger)
install(FILES "${package_dir}/pvledger.service" DESTINATION ${CMAKE_INSTALL_DOCDIR} COMPONENT PVLedger)

foreach(format deb rpm archlinux)
  add_custom_target(package-${format}
    COMMAND ${CMAKE_COMMAND} -DPACKAGER=${format} -P "${package_dir}/Package.cmake"
    DEPENDS pvledger USES_TERMINAL VERBATIM)
endforeach()
add_custom_target(packages
  COMMAND ${CMAKE_COMMAND} -DPACKAGER=all -P "${package_dir}/Package.cmake"
  DEPENDS pvledger USES_TERMINAL VERBATIM)
