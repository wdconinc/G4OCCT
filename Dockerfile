# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 G4OCCT Contributors

ARG BASE_IMAGE=ghcr.io/eic/g4occt:base
FROM ${BASE_IMAGE}

LABEL org.opencontainers.image.source=https://github.com/eic/G4OCCT
LABEL org.opencontainers.image.description="G4OCCT: Geant4 interface to OpenCASCADE Technology"
LABEL org.opencontainers.image.licenses=LGPL-2.1-or-later

# Install build tools not present in the spack base layer
# Versions pinned to Ubuntu 22.04 (Jammy) for reproducible builds
RUN apt-get update && apt-get install -y --no-install-recommends \
    cmake=3.22.1-1ubuntu1.22.04.2 \
    g++=4:11.2.0-1ubuntu1 \
    make=4.3-4.1build1 \
    && rm -rf /var/lib/apt/lists/*

# Use the spack view so cmake can find Geant4 and OpenCASCADE
ENV CMAKE_PREFIX_PATH=/opt/local

COPY . /usr/local/src/G4OCCT

# BUILD_TESTING=OFF: include(CTest) sets it ON by default; yaml-cpp (test-only
# dependency) is not in the EIC spack buildcache so must not be required here.
RUN set -e; \
    NPROC="$(nproc)"; \
    # Limit parallel build jobs to avoid excessive resource usage during Docker builds
    JOBS="$((NPROC < 4 ? NPROC : 4))"; \
    cmake -S /usr/local/src/G4OCCT -B /tmp/G4OCCT-build \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=/usr/local \
      -DBUILD_TESTING=OFF \
  && cmake --build /tmp/G4OCCT-build -- -j"${JOBS}" \
  && cmake --install /tmp/G4OCCT-build \
  && rm -rf /tmp/G4OCCT-build /usr/local/src/G4OCCT
