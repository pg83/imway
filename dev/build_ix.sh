#!/bin/sh
# Copyright (C) 2026 imway team
# MIT licensed
# See LICENSE for the full license.
#
# Build in the IX environment, the same set of libraries the bin/imway
# recipe uses. Arguments go to ./build, so this takes targets and flags:
#
#   dev/build_ix.sh imway
#   dev/build_ix.sh -B .build-asan -Dsanitizers=address imway_test
#
# Needs a machine where IX can realize a realm (a running sud); it does not
# work from inside a container without one.

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
    lib/vulkan/headers \
    lib/wayland/protocols \
    bld/wayland \
    bin/glslang \
    bin/python/14 \
    -- ./build "$@"
