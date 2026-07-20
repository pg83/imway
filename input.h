#pragma once

struct Composer;

struct InputSource {
    static InputSource* createLibinput(Composer& c);
};
