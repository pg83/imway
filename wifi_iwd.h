#pragma once

struct Composer;
struct Wifi;

// iwd provider: net.connman.iwd on the system bus, with a passphrase agent
struct WifiIwd {
    // nullptr when the system bus / iwd is unreachable
    static Wifi* create(Composer& c);
};
