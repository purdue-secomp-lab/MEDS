set(MEDS_LIT_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})

set(MEDS_TESTSUITES)
set(MEDS_TEST_DEPS ${SANITIZER_COMMON_LIT_TEST_DEPS})

macro(get_bits_for_arch arch bits)
  if (${arch} MATCHES "i386|i686|arm|mips|mipsel")
    set(${bits} 32)
  elseif (${arch} MATCHES "x86_64|powerpc64|powerpc64le|aarch64|mips64|mips64el|s390x")
    set(${bits} 64)
  else()
    message(FATAL_ERROR "Unknown target architecture: ${arch}")
  endif()
endmacro()

macro(add_meds_testsuite test_mode sanitizer arch)
  set(MEDS_LIT_TEST_MODE "${test_mode}")
  set(CONFIG_NAME ${MEDS_LIT_TEST_MODE}-${arch})
  configure_lit_site_cfg(
    ${CMAKE_CURRENT_SOURCE_DIR}/lit.site.cfg.in
    ${CMAKE_CURRENT_BINARY_DIR}/${CONFIG_NAME}/lit.site.cfg)
  list(APPEND MEDS_TESTSUITES ${CMAKE_CURRENT_BINARY_DIR}/${CONFIG_NAME})
  if(NOT COMPILER_RT_STANDALONE_BUILD)
    list(APPEND MEDS_TEST_DEPS ${sanitizer})
  endif()
endmacro()

set(MEDS_TEST_ARCH ${MEDS_SUPPORTED_ARCH})

foreach(arch ${MEDS_TEST_ARCH})
  set(MEDS_TEST_TARGET_ARCH ${arch})
  get_bits_for_arch(${arch} MEDS_TEST_BITS)

  get_target_flags_for_arch(${arch} MEDS_TEST_TARGET_CFLAGS)
  string(REPLACE ";" " " MEDS_TEST_TARGET_CFLAGS "${MEDS_TEST_TARGET_CFLAGS}")

  add_meds_testsuite("Standalone" meds ${arch})
endforeach()

add_lit_testsuite(check-meds "Running the Meds tests"
  ${MEDS_TESTSUITES}
  DEPENDS ${MEDS_TEST_DEPS})
set_target_properties(check-meds PROPERTIES FOLDER "Meds tests")

