#pragma once

#include <std/str/view.h>
#include <std/sys/types.h>

#include <stddef.h>

#include "color.h"
#include "visitor.h"

namespace stl {
    class ObjPool;
}

struct ev_loop;

struct Scene;
struct Output;
struct Renderer;
struct DmabufFormat;
struct Session;

// wp-drm-lease: a connector the compositor does not drive (the "non-desktop"
// KMS property, set by VR headsets and dedicated displays), offered to a
// client that scans out on it directly through a DRM lease
struct LeaseConnector {
    u32 connectorId = 0;
    stl::StringView name;
    stl::StringView description;
};

struct Device {
    virtual unsigned long long renderDevice() const = 0;
    virtual int drmFd() const = 0;
    virtual bool explicitSyncSupported() const = 0;

    // non-desktop connectors available to lease; empty on headless and when
    // the drm node has none
    virtual void leaseConnectorsImpl(stl::VisitorFace&& vis) = 0;

    template <typename F>
    void leaseConnectors(F f) {
        leaseConnectorsImpl(visitEach<LeaseConnector>(f));
    }

    // drmModeCreateLease over the given connectors (+ a free crtc and its
    // planes each); returns the lease fd (>=0) and fills lesseeId, or a
    // negative errno. revokeLease revokes by lessee id
    virtual int createLease(const u32* connectorIds, int count, u32& lesseeId) = 0;
    virtual void revokeLease(u32 lesseeId) = 0;

    // enumerate the dmabuf formats the render device can sample
    virtual void dmabufFormatsImpl(stl::VisitorFace&& vis) = 0;

    template <typename F>
    void dmabufFormats(F f) {
        dmabufFormatsImpl(visitEach<DmabufFormat>(f));
    }

    virtual Output* createOutput(stl::StringView connector, stl::StringView mode,
                                 const OutputConfiguration& config) = 0;
    virtual Renderer* createRenderer(struct Composer& c, stl::StringView fontPath, float uiScale, int framesLimit) = 0;
};
