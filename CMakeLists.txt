#
# Minimum version is 3.5 (this supports Ubuntu 16.04 and later, for example)
#
CMAKE_MINIMUM_REQUIRED(VERSION 3.5)

#
# Project version numbering using semantic versioning. See:
# https://semver.org/
#
# These are used to set VERSION and SOVERSION properties of the LibSerial
# libarary. See:
# - https://cmake.org/cmake/help/latest/prop_tgt/SOVERSION.html
# - https://cmake.org/cmake/help/latest/prop_tgt/VERSION.html
#
PROJECT(LibSerial LANGUAGES C CXX VERSION 1.0.0)

#
# Project specific options and variables
#
OPTION(INSTALL_STATIC "Install static library." ON)

#
# LibSerial requies a C++ compiler that supports at least C++14 standard
#
SET(CMAKE_CXX_STANDARD 14)
SET(CMAKE_STANDARD_REQUIRES ON)

INCLUDE(ExternalProject)

SET(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)

#
# Create compile_commands.json file so that it may be used by various
# editors/plugins/IDEs that support it.
#
SET(CMAKE_EXPORT_COMPILE_COMMANDS 1)

#
# Use GNU standard installation directories. CMake will use /usr/local
# as the default install directory. Users may override this by setting
# CMAKE_INSTALL_PREFIX. For example:
#
# cd build && cmake -DCMAKE_INSTALL_PREFIX=/usr ..
#
INCLUDE(GNUInstallDirs)

#
# Prefer -pthread compiler and linker flag when using libpthread. This must
# be set before call to FIND_PACKAGE(Threads).
#
SET(THREADS_HAVE_PTHREAD_ARG 1)
#FIND_PACKAGE(SIP REQUIRED)
FIND_PACKAGE(Threads REQUIRED)

#
# Use -DCMAKE_BUILD_TYPE=Release or -DCMAKE_BUILD_TYPE=Debug to let CMake
# decide whether to use debug or optimization flags. We should not hard-code
# them here. Similarly, let CMake handle flags needed for shared object files
# (such as -fPIC). Additionally, "-pthread" flag will also be handled by CMake
# via the use of CMAKE_THREAD_LIBS_INIT (cmake < 3.1) or Threads::Threads.
#
ADD_DEFINITIONS(
  -Wall
  -Wcast-align
  -Wchar-subscripts
  -Wdouble-promotion
  -Wextra
  -Wfatal-errors
  -Wformat
  -Wformat-security
  -Wlogical-op
  -Wno-format-extra-args
  -Wno-long-long
  -Wno-parentheses
  -Wno-psabi
  -Wno-variadic-macros
  -Woverlength-strings
  -Wpacked
  -Wpointer-arith
  -Wunused-local-typedefs
  -Wwrite-strings

  -fstrict-aliasing
  -fno-check-new
  -fno-common
  -fvisibility=default
  -pedantic
  )

SET(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
SET(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
SET(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
SET(CMAKE_INSTALL_MESSAGE ALWAYS)

ADD_SUBDIRECTORY(src)

#
# Create pkg-config file for cmake builds as well as autotool builds
#
set(prefix ${CMAKE_INSTALL_PREFIX})
set(exec_prefix ${CMAKE_INSTALL_PREFIX})
set(libdir ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR})
set(includedir ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_INCLUDEDIR})
set(VERSION ${PROJECT_VERSION})
configure_file(libserial.pc.in libserial.pc @ONLY)
install(FILES ${CMAKE_BINARY_DIR}/libserial.pc DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/pkgconfig)
