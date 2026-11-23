execute_process(COMMAND "${READELF}" -l -d "${BINARY}" RESULT_VARIABLE status OUTPUT_VARIABLE elf)
if(NOT status EQUAL 0 OR elf MATCHES "INTERP|\\(NEEDED\\)")
  message(FATAL_ERROR "pvledger release binary contains a runtime interpreter or shared library dependency")
endif()
