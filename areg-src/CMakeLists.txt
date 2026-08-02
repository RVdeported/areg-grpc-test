#=============================================================================#
# areg-src — AREG SDK task-distribution skeleton
#=============================================================================#

# -- AREG SDK setup -------------------------------------------------------
# Try to find Areg SDK as installed package
find_package(areg CONFIG)

if (NOT areg_FOUND)
    # Areg SDK not found, fetch from GitHub
    
    # Configuration for faster builds
    set(AREG_BUILD_DIR "${CMAKE_BINARY_DIR}")
    set(AREG_DEPS_DIR   "${CMAKE_BINARY_DIR}/packages")
    set(AREG_LIB_TYPE    shared)
    
    # Disable examples and tests
    option(AREG_TESTS    "Build areg-sdk tests"    OFF)
    option(AREG_EXAMPLES "Build areg-sdk examples" OFF)
    option(AREG_SYSTEM_GTEST  "Build GTest"             OFF)
    option(AREG_OUTPUT_LAYOUT "Areg build structure"    OFF)
    
    # Fetch Areg SDK from GitHub
    include(FetchContent)
    set(FETCHCONTENT_BASE_DIR "${AREG_DEPS_DIR}")
    
    FetchContent_Declare(
        areg
        GIT_REPOSITORY https://github.com/aregtech/areg-sdk.git
        GIT_TAG "master"
    )
    FetchContent_MakeAvailable(areg)
    
    set(AREG_SDK_ROOT        "${areg_SOURCE_DIR}")
    set(AREG_CMAKE_CONFIG_DIR "${AREG_SDK_ROOT}/conf/cmake")
    set(AREG_CMAKE          "${AREG_SDK_ROOT}/areg.cmake")
else()
    # Areg SDK package found (via vcpkg or system install)
    message(STATUS ">>> Found Areg package at '${areg_DIR}'")
endif()

# Include Areg CMake utilities
include(${AREG_CMAKE})

# -- service interface code generation -------------------------------------
addServiceInterface(areg_generated areg-src/Interface.siml)

# -- master (server) -------------------------------------------------------
macro_declare_executable(areg_server 
  areg_generated
  MasterComponent.cpp
  server.cpp
)

# -- worker (client) -------------------------------------------------------
macro_declare_executable(areg_client 
  areg_generated
  WorkerComponent.cpp
  client.cpp
)
