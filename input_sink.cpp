#include "input_sink.h"

#include <std/mem/obj_pool.h>

using namespace stl;

namespace {
    struct TeeSink: public InputSink {
        InputSink* a = nullptr;
        InputSink* b = nullptr;

        TeeSink(InputSink& x, InputSink& y)
            : a(&x)
            , b(&y)
        {
        }

        void motion(double x, double y) override {
            a->motion(x, y);
            b->motion(x, y);
        }

        void button(u32 btn, bool pressed) override {
            a->button(btn, pressed);
            b->button(btn, pressed);
        }

        void key(u32 code, bool pressed) override {
            a->key(code, pressed);
            b->key(code, pressed);
        }

        void scroll(double dx, double dy) override {
            a->scroll(dx, dy);
            b->scroll(dx, dy);
        }
    };
}

InputSink* InputSink::tee(ObjPool* pool, InputSink& a, InputSink& b) {
    return pool->make<TeeSink>(a, b);
}
