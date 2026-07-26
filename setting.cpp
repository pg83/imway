#include "settings.h"

#include "listener.h"
#include "intr_list.h"

#include <std/lib/buffer.h>

using namespace stl;

void publishSettingListeners(IntrusiveList& listeners) {
    forEach<Listener>(listeners, [](Listener& listener) {
        listener.onListen();
    });
}

TextSetting::TextSetting() noexcept
    : value_(nullptr)
{
}

TextSetting::TextSetting(StringView initial)
    : value_(initial.empty() ? nullptr : new Buffer(initial))
{
}

TextSetting::~TextSetting() noexcept {
    delete value_;
}

StringView TextSetting::get() const noexcept {
    return value_ ? StringView(*value_) : StringView();
}

bool TextSetting::set(StringView value) {
    if (get() == value) {
        return false;
    }

    Buffer* next = value.empty() ? nullptr : new Buffer(value);

    delete value_;
    value_ = next;
    publishSettingListeners(changedListeners);

    return true;
}
