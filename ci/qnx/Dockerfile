# SPDX-License-Identifier: MPL-2.0
# The private QNX 6.5 SP1 build image of the release workflow (ci/qnx/README.md).
# ci/qnx/build-image.sh assembles its context from local files; none is committed.
FROM ubuntu:24.04@sha256:9c7f200c07e3d4a03e7249d2c16bf66a51120fb876aa2242cf31451412543289

# The QNX host tools are 32-bit; the host half of the build (nbt-verify-release)
# and the repository's Python checks run natively.
RUN dpkg --add-architecture i386 \
 && apt-get update \
 && apt-get install -y --no-install-recommends \
      ca-certificates git make g++ python3 openssl libssl-dev zlib1g-dev libc6:i386 zlib1g:i386 \
 && rm -rf /var/lib/apt/lists/*

# qnx/: the files of ci/qnx/files.txt. car/screen: the head unit's libscreen and
# libcrypto; car/rhmi: its libAppFramework.A.so and libCoreFramework.A.so.
COPY qnx /opt/qnx65
COPY car /opt/car

# QNX_ENV: the toolchain's env.sh, which the application Makefiles source.
ENV QNX_ENV=/opt/qnx65/env.sh \
    QNX_HOST=/opt/qnx65/sdk/host/linux/x86 \
    QNX_TARGET=/opt/qnx65/sdk/target/qnx6 \
    QNX_CONFIGURATION=/opt/qnx65/sdk/config \
    QNX_SCREEN_LIBDIR=/opt/car/screen \
    RHMI_LIBDIR=/opt/car/rhmi \
    PATH=/opt/qnx65/sdk/host/linux/x86/usr/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
LABEL org.opencontainers.image.description="NBT Manager release builds: QNX 6.5 SP1 qcc (GCC 4.8.3) subset and head-unit link libraries. Private."
