FROM debian:12

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
