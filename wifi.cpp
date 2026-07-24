#include "wifi.h"

#include "composer.h"
#include "notifier.h"
#include "util.h"
#include "wifi_iwd.h"
#include "wifi_nm.h"

using namespace stl;

Wifi* Wifi::create(Composer& c) {
    if (Wifi* w = WifiIwd::create(c)) {
        return w;
    }

    if (Wifi* w = WifiNm::create(c)) {
        return w;
    }

    return nullptr;
}

void wifiNotifyTransition(Composer& c, WifiState& last, WifiState now, StringView ssid) {
    if (!c.notifier) {
        return;
    }

    // only the stable edges are worth a toast — scanning/connecting churn
    // is not
    if (now != WifiState::connected && now != WifiState::disconnected && now != WifiState::unavailable) {
        return;
    }

    if (now == last) {
        return;
    }

    bool wasOnline = last == WifiState::connected;

    last = now;

    Post p;

    p.app = "wi-fi"_sv;
    p.icon = "network-wireless"_sv;

    if (now == WifiState::connected) {
        p.summary = "wi-fi connected"_sv;
        p.body = ssid;
        c.notifier->post(p);
    } else if (wasOnline) {
        p.summary = "wi-fi disconnected"_sv;
        c.notifier->post(p);
    }
}
