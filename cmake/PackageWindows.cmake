if(NOT MAKENSIS)
  message(FATAL_ERROR "NSIS is required. Install makensis or set PVLEDGER_MAKENSIS.")
endif()

set(stage "${BUILD_DIR}/packaging/windows-stage/${CONFIG}")
set(output "${BUILD_DIR}/packages/pvledger-${VERSION}-windows-x64-setup.exe")
file(REMOVE_RECURSE "${stage}")
file(MAKE_DIRECTORY "${BUILD_DIR}/packages")
execute_process(COMMAND "${CMAKE_COMMAND}" --install "${BUILD_DIR}"
  --prefix "${stage}" --config "${CONFIG}" --component PVLedger
  COMMAND_ERROR_IS_FATAL ANY)
if(CMAKE_HOST_WIN32)
  set(define /D)
else()
  set(define -D)
endif()
execute_process(COMMAND "${MAKENSIS}" "${define}STAGE=${stage}" "${define}OUTPUT=${output}"
  "${define}VERSION=${VERSION}" "${define}SOURCE=${SOURCE_DIR}" "${SOURCE_DIR}/packaging/pvledger.nsi"
  COMMAND_ERROR_IS_FATAL ANY)
