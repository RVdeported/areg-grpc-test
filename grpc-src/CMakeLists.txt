#=============================================================================#
# grpc-src — gRPC task-distribution 
#=============================================================================#

# -- locate gRPC & protobuf ------------------------------------------------
find_package(gRPC CONFIG  REQUIRED)
find_package(Protobuf REQUIRED)

# -- proto generation -------------------------------------------------------
set(PROTO_DIR  "${CMAKE_CURRENT_SOURCE_DIR}/proto")
set(PROTO_FILE "${PROTO_DIR}/task.proto")

get_target_property(GRPC_CPP_PLUGIN  gRPC::grpc_cpp_plugin LOCATION)
get_target_property(PROTOBUF_PROTOC  protobuf::protoc      LOCATION)

set(PROTO_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${PROTO_GEN_DIR}")

# Generated sources
set(GEN_SOURCES
  "${PROTO_GEN_DIR}/task.pb.cc"
  "${PROTO_GEN_DIR}/task.grpc.pb.cc")
set(GEN_HEADERS
  "${PROTO_GEN_DIR}/task.pb.h"
  "${PROTO_GEN_DIR}/task.grpc.pb.h")

add_custom_command(
  OUTPUT  ${GEN_SOURCES} ${GEN_HEADERS}
  COMMAND ${PROTOBUF_PROTOC}
    --proto_path="${PROTO_DIR}"
    --cpp_out="${PROTO_GEN_DIR}"
    --grpc_out="${PROTO_GEN_DIR}"
    --plugin=protoc-gen-grpc="${GRPC_CPP_PLUGIN}"
    "${PROTO_FILE}"
  DEPENDS "${PROTO_FILE}"
  COMMENT "Generating gRPC/protobuf sources from task.proto"
)

# -- convenience library for generated code ---------------------------------
add_library(task-proto STATIC ${GEN_SOURCES} ${GEN_HEADERS})
target_link_libraries(task-proto
  PUBLIC
    protobuf::libprotobuf
    gRPC::grpc++
    gRPC::grpc)
target_include_directories(task-proto
  PUBLIC
    "${PROTO_GEN_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}")   # so includes like "proto/task.pb.h" work

# -- server -----------------------------------------------------------------
add_executable(grpc-server server.cpp)
target_link_libraries(grpc-server PRIVATE task-proto)

# -- client -----------------------------------------------------------------
add_executable(grpc-client client.cpp)
target_link_libraries(grpc-client PRIVATE task-proto)
