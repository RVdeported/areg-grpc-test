FROM debian:12

ENV DEBIAN_FRONTEND=noninteractive

# ── system dependencies ─────────────────────────────────────────────────────
RUN apt-get update 
RUN apt-get install -y \
    build-essential \
    cmake \
    git \
    curl \
    ca-certificates \
    autoconf \
    libtool \
    pkg-config \
    python3 \
    libgrpc++-dev \
    wget \
    protobuf-compiler-grpc protobuf-codegen 

WORKDIR /tmp
RUN wget https://download.oracle.com/java/17/archive/jdk-17.0.12_linux-x64_bin.deb
RUN apt-get install -y ./jdk-17.0.12_linux-x64_bin.deb

# ── gRPC + Protobuf (built from source, ~20-30 min) ────────────────────────
# ARG GRPC_VERSION=v1.65.0
# WORKDIR /tmp/grpc-src
# RUN git clone --depth 1 --branch ${GRPC_VERSION} \
#       --recurse-submodules -j$(nproc) \
#       https://github.com/grpc/grpc.git .                                        && \
#     mkdir -p cmake/build && cd cmake/build                                      && \
#     cmake ../.. \
#       -DCMAKE_BUILD_TYPE=Release \
#       -DgRPC_INSTALL=ON \
#       -DgRPC_BUILD_TESTS=OFF \
#       -DgRPC_BUILD_CSHARP_EXT=OFF \
#       -DgRPC_BUILD_GRPC_CSHARP_PLUGIN=OFF \
#       -DgRPC_BUILD_GRPC_NODE_PLUGIN=OFF \
#       -DgRPC_BUILD_GRPC_OBJECTIVE_C_PLUGIN=OFF \
#       -DgRPC_BUILD_GRPC_PHP_PLUGIN=OFF \
#       -DgRPC_BUILD_GRPC_PYTHON_PLUGIN=OFF \
#       -DgRPC_BUILD_GRPC_RUBY_PLUGIN=OFF \
#       -DCMAKE_INSTALL_PREFIX=/usr/local                                        && \
#     make -j$(nproc)                                                            && \
#     make install                                                               && \
#     rm -rf /tmp/grpc-src

# Reindex shared libraries so gRPC/protobuf are found at runtime
# RUN ldconfig

# ── project build (AREG SDK fetched via FetchContent here) ──────────────────
ARG BUILD_TYPE=Release
WORKDIR /app
COPY ./areg-src/ ./areg-src/
COPY ./grpc-src/ ./grpc-src/
COPY ./start-clients.sh .
COPY ./start-server.sh .
COPY ./run_benchmark.py .
COPY ./common/ ./common/
COPY ./CMakeLists.txt .
COPY .git/ ./.git/

RUN mkdir -p /app/build

WORKDIR /app/build
RUN cmake .. -DCMAKE_BUILD_TYPE=${BUILD_TYPE}                                  && \
    make -j$(nproc)

WORKDIR /app
RUN chmod +x start-server.sh start-clients.sh                                  && \
    mkdir -p /app/results

# No default ENTRYPOINT — docker-compose sets per-service commands.
