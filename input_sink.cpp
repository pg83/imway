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

        void relMotion(double dx, double dy, double dxRaw, double dyRaw) override {
            a->relMotion(dx, dy, dxRaw, dyRaw);
            b->relMotion(dx, dy, dxRaw, dyRaw);
        }

        void swipeBegin(u32 fingers) override {
            a->swipeBegin(fingers);
            b->swipeBegin(fingers);
        }

        void swipeUpdate(double dx, double dy) override {
            a->swipeUpdate(dx, dy);
            b->swipeUpdate(dx, dy);
        }

        void swipeEnd(bool cancelled) override {
            a->swipeEnd(cancelled);
            b->swipeEnd(cancelled);
        }

        void pinchBegin(u32 fingers) override {
            a->pinchBegin(fingers);
            b->pinchBegin(fingers);
        }

        void pinchUpdate(double dx, double dy, double scale, double rotation) override {
            a->pinchUpdate(dx, dy, scale, rotation);
            b->pinchUpdate(dx, dy, scale, rotation);
        }

        void pinchEnd(bool cancelled) override {
            a->pinchEnd(cancelled);
            b->pinchEnd(cancelled);
        }

        void holdBegin(u32 fingers) override {
            a->holdBegin(fingers);
            b->holdBegin(fingers);
        }

        void holdEnd(bool cancelled) override {
            a->holdEnd(cancelled);
            b->holdEnd(cancelled);
        }
    };
}

InputSink* InputSink::tee(ObjPool* pool, InputSink& a, InputSink& b) {
    return pool->make<TeeSink>(a, b);
}
