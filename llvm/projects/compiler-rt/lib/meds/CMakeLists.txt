# Build for the AddressSanitizer runtime support library.

set(MEDS_SOURCES
  asan_allocator.cc
  asan_activation.cc
  asan_debugging.cc
  asan_descriptions.cc
  asan_errors.cc
  asan_fake_stack.cc
  asan_flags.cc
  asan_globals.cc
  asan_interceptors.cc
  asan_linux.cc
  asan_malloc_linux.cc
  asan_memory_profile.cc
  asan_poisoning.cc
  asan_posix.cc
  asan_report.cc
  asan_rtl.cc
  asan_stack.cc
  asan_stats.cc
  asan_suppressions.cc
  asan_thread.cc
  meds_allocator.cc
  meds_reloc.cc)

set(MEDS_CXX_SOURCES
  asan_new_delete.cc)

set(MEDS_PREINIT_SOURCES
  asan_preinit.cc)

include_directories(..)

set(MEDS_CFLAGS ${SANITIZER_COMMON_CFLAGS})
append_rtti_flag(OFF MEDS_CFLAGS)

if(NOT APPLE)
  add_compiler_rt_object_libraries(RTMeds
    ARCHS ${MEDS_SUPPORTED_ARCH}
    SOURCES ${MEDS_SOURCES} CFLAGS ${MEDS_CFLAGS}
    DEFS ${MEDS_COMMON_DEFINITIONS})
  add_compiler_rt_object_libraries(RTMeds_cxx
    ARCHS ${MEDS_SUPPORTED_ARCH}
    SOURCES ${MEDS_CXX_SOURCES} CFLAGS ${MEDS_CFLAGS}
    DEFS ${MEDS_COMMON_DEFINITIONS})
  add_compiler_rt_object_libraries(RTMeds_preinit
    ARCHS ${MEDS_SUPPORTED_ARCH}
    SOURCES ${MEDS_PREINIT_SOURCES} CFLAGS ${MEDS_CFLAGS}
    DEFS ${MEDS_COMMON_DEFINITIONS})
endif()

# Build ASan runtimes shipped with Clang.
add_compiler_rt_component(meds)

set(MEDS_COMMON_RUNTIME_OBJECT_LIBS
    RTInterception
    RTSanitizerCommon
    RTSanitizerCommonLibc
    RTLSanCommon
    RTUbsan)

add_compiler_rt_runtime(clang_rt.meds
    STATIC
    ARCHS ${MEDS_SUPPORTED_ARCH}
    OBJECT_LIBS RTMeds_preinit
                RTMeds
                ${MEDS_COMMON_RUNTIME_OBJECT_LIBS}
    CFLAGS ${MEDS_CFLAGS}
    DEFS ${MEDS_COMMON_DEFINITIONS}
    PARENT_TARGET meds)

add_compiler_rt_runtime(clang_rt.meds_cxx
    STATIC
    ARCHS ${MEDS_SUPPORTED_ARCH}
    OBJECT_LIBS RTMeds_cxx
                RTUbsan_cxx
    CFLAGS ${MEDS_CFLAGS}
    DEFS ${MEDS_COMMON_DEFINITIONS}
    PARENT_TARGET meds)

add_compiler_rt_runtime(clang_rt.meds-preinit
    STATIC
    ARCHS ${MEDS_SUPPORTED_ARCH}
    OBJECT_LIBS RTMeds_preinit
    CFLAGS ${MEDS_CFLAGS}
    DEFS ${MEDS_COMMON_DEFINITIONS}
    PARENT_TARGET meds)

add_compiler_rt_resource_file(meds_blacklist asan_blacklist.txt meds)


