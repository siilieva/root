# Copyright (C) 1995-2019, Rene Brun and Fons Rademakers.
# All rights reserved.
#
# For the licensing terms see $ROOTSYS/LICENSE.
# For the list of contributors see $ROOTSYS/README/CREDITS.

include(ExternalProject)

find_package(OpenSSL REQUIRED)

set(XROOTD_VERSION "5.5.2")
set(XROOTD_VERSIONNUM 500050002 CACHE INTERNAL "" FORCE)
set(lcgpackages http://lcgpackages.web.cern.ch/lcgpackages/tarFiles/sources)
set(XROOTD_SRC_URI ${lcgpackages}/xrootd-${XROOTD_VERSION}.tar.gz)
set(XROOTD_PREFIX ${CMAKE_CURRENT_BINARY_DIR}/XROOTD-prefix)
message(STATUS "Downloading and building XROOTD version ${XROOTD_VERSION}")

# Guess under which directory XRootD will install its libraires
set(XROOTD_LIBDIR "lib")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND ${CMAKE_SIZEOF_VOID_P} EQUAL 8
AND NOT CMAKE_CROSSCOMPILING AND NOT EXISTS "/etc/debian_version")
  set(XROOTD_LIBDIR "lib64")
endif()

foreach(lib XrdUtils XrdCl)
  set(libname ${CMAKE_SHARED_LIBRARY_PREFIX}${lib}${CMAKE_SHARED_LIBRARY_SUFFIX})
  list(APPEND XROOTD_LIBRARIES ${XROOTD_PREFIX}/${XROOTD_LIBDIR}/${libname})
endforeach()

if(OPENSSL_VERSION VERSION_GREATER_EQUAL 3)
   set(XROOTD_WITH_OPENSSL3 "-DWITH_OPENSSL3=TRUE")
endif()

ExternalProject_Add(
    XROOTD
    URL ${XROOTD_SRC_URI}
    URL_HASH SHA256=ec4e0490b8ee6a3254a4ea4449342aa364bc95b78dc9a8669151be30353863c6
    INSTALL_DIR ${XROOTD_PREFIX}
    CMAKE_ARGS -DCMAKE_INSTALL_PREFIX:PATH=<INSTALL_DIR>
               -DCMAKE_PREFIX_PATH:STRING=${OPENSSL_PREFIX}
               -DCMAKE_BUILD_TYPE=Release
               -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
               -DCMAKE_C_FLAGS=${CMAKE_C_FLAGS}
               -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
               -DCMAKE_CXX_FLAGS=${ROOT_EXTERNAL_CXX_FLAGS}
               -DCMAKE_OSX_SYSROOT=${CMAKE_OSX_SYSROOT}
               -DCMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}
               -DENABLE_PYTHON=OFF
               -DENABLE_CEPH=OFF
               -DXRDCL_LIB_ONLY=ON
               ${XROOTD_WITH_OPENSSL3}
               -DCMAKE_INSTALL_RPATH:STRING=${XROOTD_PREFIX}/${XROOTD_LIBDIR}
               -DOPENSSL_ROOT_DIR=${OPENSSL_ROOT_DIR}
    INSTALL_COMMAND ${CMAKE_COMMAND} --build . --target install
            COMMAND ${CMAKE_COMMAND} -E copy_directory <INSTALL_DIR>/include/xrootd <INSTALL_DIR>/include
    LOG_DOWNLOAD 1 LOG_CONFIGURE 1 LOG_BUILD 1 LOG_INSTALL 1
    BUILD_BYPRODUCTS ${XROOTD_LIBRARIES}
    TIMEOUT 600
)

if(builtin_openssl)
  add_dependencies(XROOTD OPENSSL)
endif()

list(APPEND XROOTD_LIBRARIES OpenSSL::SSL)

unset(XROOTD_FOUND CACHE)
unset(XROOTD_FOUND PARENT_SCOPE)
set(XROOTD_FOUND TRUE CACHE BOOL "" FORCE)

set(XROOTD_INCLUDE_DIRS ${XROOTD_PREFIX}/include/xrootd ${XROOTD_DESTDIR}/include/xrootd/private)
set(XROOTD_NOMAIN TRUE)
set(XROOTD_CFLAGS "-DROOTXRDVERS=${XROOTD_VERSIONNUM}")

set(XROOTD_INCLUDE_DIR ${XROOTD_PREFIX}/include/xrootd ${XROOTD_PREFIX}/include/xrootd/private CACHE INTERNAL "" FORCE)
set(XROOTD_INCLUDE_DIRS ${XROOTD_PREFIX}/include/xrootd ${XROOTD_PREFIX}/include/xrootd/private CACHE INTERNAL "" FORCE)
set(XROOTD_LIBRARY ${XROOTD_PREFIX}/${XROOTD_LIBDIR}/${XROOTD_LIBNAME} CACHE INTERNAL "" FORCE)
set(XROOTD_LIBRARIES ${XROOTD_LIBRARIES} CACHE INTERNAL "" FORCE)

add_library(xrootd INTERFACE)
foreach(dir ${XROOTD_INCLUDE_DIR})
  target_include_directories(xrootd INTERFACE $<BUILD_INTERFACE:${dir}>)
endforeach()
target_link_libraries(xrootd INTERFACE $<BUILD_INTERFACE:${XROOTD_LIBRARIES}>)
set_property(TARGET xrootd PROPERTY INTERFACE_BUILD_RPATH ${XROOTD_PREFIX}/${XROOTD_LIBDIR})
add_dependencies(xrootd XROOTD)

add_library(Xrootd::Xrootd ALIAS xrootd)

# So that we can use ${XROOTD_CLIENT_LIBRARIES} in target_link_libraries, just
# like you would do when finding and external xrootd with find_package(XRootD).
set(XROOTD_CLIENT_LIBRARIES Xrootd::Xrootd CACHE INTERNAL "" FORCE)

set_property(GLOBAL APPEND PROPERTY ROOT_BUILTIN_TARGETS XROOTD)

install(DIRECTORY ${XROOTD_PREFIX}/${XROOTD_LIBDIR}/ DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT libraries FILES_MATCHING PATTERN "libXrd*")
install(DIRECTORY ${XROOTD_PREFIX}/include/xrootd/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} COMPONENT headers)
if(APPLE)
  # XRootD libraries on mac need the LC_RPATH variable set. The build process already takes care of setting
  #   * BUILD_RPATH = build/XROOTD-prefix/../src
  #   * INSTALL_RPATH = build/lib
  # Since the install directory for the builtin_xrootd target corresponds to the build directory of the main project.
  # Use a post install script to change the LC_RPATH variable of the libraries in the ROOT install folder.
  install(SCRIPT ${CMAKE_CURRENT_LIST_DIR}/XROOTDApplePostInstall.cmake
          CODE "xrootd_libs_change_rpath(${XROOTD_PREFIX}/${XROOTD_LIBDIR} ${CMAKE_INSTALL_FULL_LIBDIR})"
  )
endif()
