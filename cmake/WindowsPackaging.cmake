find_program(PVLEDGER_MAKENSIS NAMES makensis NO_CMAKE_FIND_ROOT_PATH
  HINTS "$ENV{ProgramFiles}/NSIS" "$ENV{ProgramW6432}/NSIS" "C:/Program Files (x86)/NSIS")

foreach(dependency oatpp yaml-cpp)
  if(DEFINED ${dependency}_SOURCE_DIR)
    install(FILES "${${dependency}_SOURCE_DIR}/LICENSE"
      DESTINATION share/pvledger/licenses RENAME "${dependency}.txt" COMPONENT PVLedger)
  endif()
endforeach()
foreach(dependency openssl jsoncpp sqlite3)
  if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    set(copyright "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/${dependency}/copyright")
    if(EXISTS "${copyright}")
      install(FILES "${copyright}" DESTINATION share/pvledger/licenses
        RENAME "${dependency}.txt" COMPONENT PVLedger)
    endif()
  endif()
endforeach()

add_custom_target(package-windows
  COMMAND ${CMAKE_COMMAND}
    "-DBUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}"
    "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
    "-DCONFIG=$<CONFIG>"
    "-DVERSION=${PROJECT_VERSION}"
    "-DMAKENSIS=${PVLEDGER_MAKENSIS}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/PackageWindows.cmake"
  DEPENDS pvledger pvledger-win-helper USES_TERMINAL VERBATIM)
add_custom_target(packages DEPENDS package-windows)
