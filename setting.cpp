#include "settings.h"

#include "listener.h"
#include "intr_list.h"

using namespace stl;

void publishSettingListeners(IntrusiveList& listeners) {
    forEach<Listener>(listeners, [](Listener& listener) {
        listener.onListen();
    });
}
