# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 G4OCCT Contributors

ARG BASE_IMAGE=ghcr.io/eic/g4occt:base
FROM ${BASE_IMAGE}

LABEL org.opencontainers.image.source=https://github.com/eic/G4OCCT
LABEL org.opencontainers.image.description="G4OCCT: Geant4 interface to OpenCASCADE Technology"
LABEL org.opencontainers.image.licenses=LGPL-2.1-or-later

# Install build tools not present in the spack base layer
RUN apt-get update && apt-get install -y --no-install-recommends \
    cmake \
    g++ \
    make \
    && rm -rf /var/lib/apt/lists/*

# Use the spack view so cmake can find Geant4, OpenCASCADE, and yaml-cpp
ENV CMAKE_PREFIX_PATH=/opt/local

COPY . /usr/local/src/G4OCCT

RUN cmake -S /usr/local/src/G4OCCT -B /tmp/G4OCCT-build \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=/usr/local \
  && cmake --build /tmp/G4OCCT-build -- -j$(nproc) \
  && cmake --install /tmp/G4OCCT-build \
  && rm -rf /tmp/G4OCCT-build /usr/local/src/G4OCCT
