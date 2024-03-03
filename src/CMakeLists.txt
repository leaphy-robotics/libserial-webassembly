set(LIBSERIAL_SOURCES
    SerialPort.cpp
    SerialStream.cpp
    SerialStreamBuf.cpp)

add_library(libserial_static STATIC ${LIBSERIAL_SOURCES})

#
# We already have "lib" prefix in the target name. Prevent CMake from adding
# another "lib" prefix.
#
set_target_properties(libserial_static PROPERTIES PREFIX "")
target_include_directories(libserial_static PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(libserial_static Threads::Threads)
# set link flags for static library
set_target_properties(libserial_static PROPERTIES LINK_FLAGS "-s INCOMING_MODULE_JS_API=1")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -s STANDALONE_WASM=1 -s WASM=1")

#
# Install all our headers under the libserial subfolder of the include
# directory so that our headers do not conflict with similarly named headers
# from other libraries. Clients can either include our headers with libserial
# folder prefixed (for example, "#include <libserial/SerialPort.h>") or put the
# libserial folder in their include path (for example,
# "-I/usr/include/libserial").
#
install(DIRECTORY libserial
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

#
# The static library is always built as it is needed for unit tests but it is
# installed only if INSTALL_STATIC is set. 
#
if (INSTALL_STATIC)
    install(TARGETS libserial_static
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
#
# Use the same name (libserial) for both the static and shared libraries
#
set_target_properties(libserial_static PROPERTIES OUTPUT_NAME libserial)
