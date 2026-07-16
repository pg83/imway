#include "wifi.h"
#include "wifi_iwd.h"
#include "wifi_nm.h"

Wifi* Wifi::create(Composer& c) {
    if (Wifi* w = WifiIwd::create(c)) {
        return w;
    }

    if (Wifi* w = WifiNm::create(c)) {
        return w;
    }

    return nullptr;
}
