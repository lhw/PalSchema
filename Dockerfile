# syntax=docker/dockerfile:1
# PalSchema Linux Build Environment
# Multi-stage build: builds UE4SS + PalSchema via add_subdirectory(deps)
# Build with: container build --platform linux/amd64 --progress=plain -t palschema-linux .
FROM --platform=linux/amd64 ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

# Install build dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    cmake ninja-build pkg-config \
    gcc g++ binutils \
    git ca-certificates curl \
    && rm -rf /var/lib/apt/lists/*

# Setup Rust (needed for patternsleuth in UE4SS)
RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y --default-toolchain stable --profile minimal

WORKDIR /src

# Copy entire project (including submodules)
COPY . .

# Build PalSchema (which builds UE4SS + deps via add_subdirectory(deps))
RUN export PATH="/root/.cargo/bin:${PATH}" && \
    cmake -B build_palschema \
      -G Ninja \
      -DCMAKE_BUILD_TYPE=Game__Shipping__Linux64 && \
    cmake --build build_palschema

# Runtime stage - minimal image with just the built .so
FROM --platform=linux/amd64 ubuntu:24.04 AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
    libstdc++6 \
    && rm -rf /var/lib/apt/lists/*

# Copy the built shared library (output dir follows UE4SS convention: $<CONFIG>/lib/)
COPY --from=builder /src/build_palschema/Game__Shipping__Linux64/lib/libPalSchema.so /output/libPalSchema.so

# Copy UE4SS Linux build for reference
COPY --from=builder /src/build_palschema/Game__Shipping__Linux64/lib/libUE4SS.so /output/libUE4SS.so

WORKDIR /output

CMD ["ls", "-la", "/output/"]
