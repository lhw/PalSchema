# PalSchema Linux Build Environment
# Multi-stage build: UE4SS Linux -> PalSchema
FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

# Install build dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    cmake ninja-build pkg-config \
    gcc g++ binutils \
    git ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Setup Rust (needed for patternsleuth in UE4SS)
RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y --default-toolchain stable --profile minimal
ENV PATH="/root/.cargo/bin:${PATH}"

WORKDIR /src

# Copy entire project (including submodules)
COPY . .

# Build UE4SS Linux from submodule (headless, no GUI)
RUN cd deps/ue4ss-linux && \
    git submodule update --init --recursive && \
    cmake -B build_linux \
      -G Ninja \
      -DCMAKE_BUILD_TYPE=Game__Shipping__Linux64 \
      -DUE4SS_GUI_ENABLED=OFF \
      -DUE4SS_INPUT_ENABLED=OFF \
      -DUE4SS_PROFILERS=OFF \
      -Wno-dev && \
    cmake --build build_linux --target UE4SS --verbose

# Build PalSchema against UE4SS Linux
RUN cmake -B build_palschema \
      -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DUE4SS_DIR=$(pwd)/deps/ue4ss-linux \
      -Wno-dev && \
    cmake --build build_palschema --verbose

# Runtime stage - minimal image with just the built .so
FROM ubuntu:24.04 AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
    libstdc++6 \
    && rm -rf /var/lib/apt/lists/*

# Copy the built shared library
COPY --from=builder /src/build_palschema/libPalSchema.so /output/libPalSchema.so

# Copy UE4SS Linux build for reference
COPY --from=builder /src/deps/ue4ss-linux/build_linux/Game__Shipping__Linux64/lib/libUE4SS.so /output/libUE4SS.so

WORKDIR /output

CMD ["ls", "-la", "/output/"]
