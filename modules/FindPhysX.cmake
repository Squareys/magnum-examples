#.rst:
# Find PhysX
# -------------
#
# Finds the PhysX library. This module defines:
#
#  PhysX_FOUND          - True if PhysX library is found
#  PhysX::PhysX         - PhysX imported target
#
# Additionally these variables are defined for internal usage:
#
#  PHYSX_LIBRARY         - PhysX library
#  PHYSX_LIBRARIES       - Same as PHYSX_LIBRARY
#  PHYSX_LIBRARY_DIR     - PhysX library dir
#  PHYSX_BINARY_DIR      - PhysX binary dir
#  PHYSX_INCLUDE_DIR     - Include dir
#
# For every component of PhysX the following are defined:
#
#  PHYSX_*_LIBRARY         - Library
#  PHYSX_*_LIBRARY_DEBUG   - Debug library
#  PHYSX_*_LIBRARY_RELASE  - Release library
#  PHYSX_*_LIBRARY_PROFILE - Pofile library
#  PHYSX_*_LIBRARY_CHECKED - Checked library
#

#
#   This file is part of Magnum.
#
#   Copyright © 2010, 2011, 2012, 2013, 2014, 2015, 2016, 2017, 2018, 2019, 2020
#             Vladimír Vondruš <mosra@centrum.cz>
#   Copyright © 2017, 2020 Jonathan Hale <squareys@googlemail.com>
#
#   Permission is hereby granted, free of charge, to any person obtaining a
#   copy of this software and associated documentation files (the "Software"),
#   to deal in the Software without restriction, including without limitation
#   the rights to use, copy, modify, merge, publish, distribute, sublicense,
#   and/or sell copies of the Software, and to permit persons to whom the
#   Software is furnished to do so, subject to the following conditions:
#
#   The above copyright notice and this permission notice shall be included
#   in all copies or substantial portions of the Software.
#
#   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
#   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
#   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
#   THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
#   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
#   FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
#   DEALINGS IN THE SOFTWARE.
#

# vcpkg puts the includes into <vcpkg-installed-root>/include/physx,
# the PhysX SDK has them directly in <root>/include
find_path(PHYSX_INCLUDE_DIR NAMES PxPhysicsAPI.h
    HINTS ${PHYSX_ROOT} PATH_SUFFIXES include include/physx)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(_TARGET "x86_64")
    set(PHYSX_SUFFIX "_64")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(_TARGET "x86")
    set(PHYSX_SUFFIX "_32")
endif()

# TODO: Linux, MSVC15/17, Mac
if(EMSCRIPTEN)
    set(_PHYSX_CONFIG_NAME "emscripten")
elseif(UNIX)
    set(_PHYSX_CONFIG_NAME "linux")
elseif(CORRADE_TARGET_WINDOWS)
    # Windows folder is named according to pattern:
    #   bin/<platform>.<target>.<compiler>.<runtime>/
    # Example: win.x86_64.vc142.mt
    set(_PLATFORM "win")
    set(_COMPILER "vc142")
    # Magnum builds with dynamic runtime library by default
    set(_RUNTIME "md")
    set(_PHYSX_CONFIG_NAME "${_PLATFORM}.${_TARGET}.${_COMPILER}.${_RUNTIME}")
endif()

set(PHYSX_BINARY_DIR "${PHYSX_ROOT}/bin/${_PHYSX_CONFIG_NAME}/")
# Libraries are in the bin dir aswell
set(PHYSX_LIBRARY_DIR "${PHYSX_BINARY_DIR}")

# Find various configurations of library for a certain component
macro(find_physx_library _component _libname)
    # Default is to use component name as library name with PhysX prefix
    if("${_component}" STREQUAL "")
        set(_libname PhysX${_component})
    endif()
    string(TOUPPER "${_component}" _COMPONENT)

    # Find library for each of the PhysX configurations, see also:
    # https://gameworksdocs.nvidia.com/PhysX/4.0/documentation/PhysXGuide/Manual/BuildingWithPhysX.html#build-configurations
    foreach(_config debug release profile checked)
        string(TOUPPER ${_config} _CONFIG)
        find_library(PHYSX_${_COMPONENT}_LIBRARY_${_CONFIG}
            NAMES ${_libname}${PHYSX_SUFFIX}.lib
            PATHS ${PHYSX_LIBRARY_DIR}${_config} lib)
    endforeach()

    if(PHYSX_${_COMPONENT}_LIBRARY_DEBUG OR PHYSX_${_COMPONENT}_LIBRARY_RELEASE)
        set(PhysX_${_component}_FOUND TRUE)
    endif()

    set(PHYSX_${_COMPONENT}_LIBRARY
        optimized ${PHYSX_${_COMPONENT}_LIBRARY_RELEASE}
        debug ${PHYSX_${_COMPONENT}_LIBRARY_DEBUG})

    list(APPEND PHYSX_LIBRARY_VARS PHYSX_${_COMPONENT}_LIBRARY)
    list(APPEND PHYSX_LIBRARIES ${PHYSX_${_COMPONENT}_LIBRARY})

    if(NOT TARGET PhysX::${_component})
        add_library(PhysX::${_component} UNKNOWN IMPORTED)
        target_include_directories(PhysX::${_component} INTERFACE ${PHYSX_INCLUDE_DIR})
        set_target_properties(PhysX::${_component} PROPERTIES
            IMPORTED_LOCATION_DEBUG ${PHYSX_${_COMPONENT}_LIBRARY_DEBUG}
            IMPORTED_LOCATION_RELEASE ${PHYSX_${_COMPONENT}_LIBRARY_RELEASE})
    endif()
endmacro()

find_physx_library("PhysX" "PhysX")
find_physx_library("Common" "PhysXCommon")
find_physx_library("Cooking" "PhysXCooking")
find_physx_library("Foundation" "PhysXFoundation")

if("Pvd" IN_LIST PhysX_FIND_COMPONENTS)
    # FIXME Aparently PhysX Visual Debugger SDK is only
    # built static, but that doesn't work, since the runtime
    # is also linked static there??
    find_physx_library("Pvd" "PhysXPvdSDK_static")
endif()

if("Extensions" IN_LIST PhysX_FIND_COMPONENTS)
    # FIXME Aparently PhysX Extensions library is only
    # built static, but that doesn't work, since the runtime
    # is also linked static there??
    find_physx_library("Extensions" "PhysXExtensions_static")
    target_link_libraries(PhysX::Extensions INTERFACE PhysX::Foundation)
endif()

if("Gpu" IN_LIST PhysX_FIND_COMPONENTS)
    find_physx_library("Device" "PhysXDevice")
    find_physx_library("Gpu" "PhysXGPU")
endif()

include(SelectLibraryConfigurations)
select_library_configurations(PhysX)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(PhysX
    REQUIRED_VARS PHYSX_INCLUDE_DIR PHYSX_LIBRARY_DIR
    HANDLE_COMPONENTS)

