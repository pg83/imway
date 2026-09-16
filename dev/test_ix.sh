#!/bin/sh
# Copyright (C) 2026 imway team
# MIT licensed
# See LICENSE for the full license.
#
# Run the integration suite in the IX environment: the build set of
# dev/build_ix.sh plus what the scenarios reach for at runtime — a vulkan
# driver for the headless compositor, a session bus, sndiod, and the shell
# tools lib.sh calls. Arguments go to ./build test:
#
#   dev/test_ix.sh -Druns=1
#   dev/test_ix.sh -Druns=1 -Dfilter='headless_reg_positioner_*'
#
# Needs a machine where IX can realize a realm (a running sud).

IX=${IX:-$HOME/monorepo/ix/ix}

exec "$IX" run \
    lib/c \
    lib/c++ \
    lib/ev \
    lib/drm \
    lib/png \
    lib/jxl \
    lib/std \
    lib/glfw \
    lib/dbus \
    lib/seat \
    lib/pam \
    lib/udev \
    lib/input \
    lib/display/info \
    lib/sndio \
    lib/wayland \
    lib/lunasvg \
    lib/xkb/common \
    lib/vulkan/loader \
    lib/vulkan/drivers \
    lib/vulkan/headers \
    lib/wayland/protocols \
    bld/wayland \
    bin/glslang \
    bin/python/14 \
    bin/dbus \
    bin/sndio \
    bin/util/linux \
    bin/coreutils \
    bin/gawk/lite \
    bin/grep/patched \
    bin/procps \
    bin/psmisc \
    bin/gdb \
    -- ./build test "$@"
