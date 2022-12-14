cmake_minimum_required(VERSION 2.4)
project(libogg)

# Required modules
include(GNUInstallDirs)
include(CheckIncludeFiles)
include(CMakePackageConfigHelpers)
include(CTest)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")
set(CMAKE_C_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")

# Build options
option(BUILD_SHARED_LIBS "Build shared library" OFF)
if(APPLE)
    option(BUILD_FRAMEWORK "Build Framework bundle for OSX" OFF)
endif()

# Install options
option(INSTALL_DOCS "Install documentation" OFF)
option(INSTALL_PKG_CONFIG_MODULE "Install ogg.pc file" OFF)
option(INSTALL_CMAKE_PACKAGE_MODULE "Install CMake package configiguration module" OFF)

# Extract project version from configure.ac
file(READ configure.ac CONFIGURE_AC_CONTENTS)
string(REGEX MATCH "AC_INIT\\(\\[libogg\\],\\[([0-9]*).([0-9]*).([0-9]*)" DUMMY ${CONFIGURE_AC_CONTENTS})
set(PROJECT_VERSION_MAJOR ${CMAKE_MATCH_1})
set(PROJECT_VERSION_MINOR ${CMAKE_MATCH_2})
set(PROJECT_VERSION_PATCH ${CMAKE_MATCH_3})
set(PROJECT_VERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH})

# Extract library version from configure.ac
string(REGEX MATCH "LIB_CURRENT=([0-9]*)" DUMMY ${CONFIGURE_AC_CONTENTS})
set(LIB_CURRENT ${CMAKE_MATCH_1})

string(REGEX MATCH "LIB_AGE=([0-9]*)" DUMMY ${CONFIGURE_AC_CONTENTS})
set(LIB_AGE ${CMAKE_MATCH_1})

string(REGEX MATCH "LIB_REVISION=([0-9]*)" DUMMY ${CONFIGURE_AC_CONTENTS})
set(LIB_REVISION ${CMAKE_MATCH_1})

math(EXPR LIB_SOVERSION "${LIB_CURRENT} - ${LIB_AGE}")
set(LIB_VERSION "${LIB_SOVERSION}.${LIB_AGE}.${LIB_REVISION}")


# Helper function to configure pkg-config files
function(configure_pkg_config_file pkg_config_file_in)
    set(prefix ${CMAKE_INSTALL_PREFIX})
    set(exec_prefix ${CMAKE_INSTALL_FULL_BINDIR})
    set(libdir ${CMAKE_INSTALL_FULL_LIBDIR})
    set(includedir ${CMAKE_INSTALL_FULL_INCLUDEDIR})
    set(VERSION ${PROJECT_VERSION})
    string(REPLACE ".in" "" pkg_config_file ${pkg_config_file_in})
    configure_file(${pkg_config_file_in} ${pkg_config_file} @ONLY)
endfunction()

message(STATUS "Configuring ${PROJECT_NAME} ${PROJECT_VERSION}")

# Configure config_type.h
check_include_files(inttypes.h INCLUDE_INTTYPES_H)
check_include_files(stdint.h INCLUDE_STDINT_H)
check_include_files(sys/types.h INCLUDE_SYS_TYPES_H)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
set(SIZE16 int16_t)
set(USIZE16 uint16_t)
set(SIZE32 int32_t)
set(USIZE32 uint32_t)
set(SIZE64 int64_t)
set(USIZE64 uint64_t)

#include(CheckSizes)

configure_file(include/ogg/config_types.h.in include/ogg/config_types.h @ONLY)

set(OGG_HEADERS
    ${CMAKE_CURRENT_BINARY_DIR}/include/ogg/config_types.h
    include/ogg/ogg.h
    include/ogg/os_types.h
)

set(OGG_SOURCES
    src/bitwise.c
    src/framing.c
    src/crctable.h
)

if(WIN32 AND BUILD_SHARED_LIBS)
    list(APPEND OGG_SOURCES win32/ogg.def)
endif()

if(BUILD_FRAMEWORK)
    set(BUILD_SHARED_LIBS TRUE)
endif()

add_library(ogg ${OGG_HEADERS} ${OGG_SOURCES})
target_include_directories(ogg PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

set_target_properties(
    ogg PROPERTIES
    SOVERSION ${LIB_SOVERSION}
    VERSION ${LIB_VERSION}
    PUBLIC_HEADER "${OGG_HEADERS}"
)

if(BUILD_FRAMEWORK)
    set_target_properties(ogg PROPERTIES
        FRAMEWORK TRUE
        FRAMEWORK_VERSION ${PROJECT_VERSION}
        MACOSX_FRAMEWORK_IDENTIFIER org.xiph.ogg
        MACOSX_FRAMEWORK_SHORT_VERSION_STRING ${PROJECT_VERSION}
        MACOSX_FRAMEWORK_BUNDLE_VERSION ${PROJECT_VERSION}
        XCODE_ATTRIBUTE_INSTALL_PATH "@rpath"
        OUTPUT_NAME Ogg
    )
endif()

configure_pkg_config_file(ogg.pc.in)

#install(TARGETS ogg
#    EXPORT OggTargets
#    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
#    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/ogg
#)

set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
include(CPack)
