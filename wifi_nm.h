#pragma once

struct Composer;
struct Wifi;

// NetworkManager provider (org.freedesktop.NetworkManager). the most common
// desktop wifi manager; also the right choice when NM drives iwd as its
// backend, so the factory tries it after raw iwd
struct WifiNm {
    // nullptr when NetworkManager does not own its bus name
    static Wifi* create(Composer& c);
};
