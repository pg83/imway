#pragma once

struct Composer;

struct InputSource {
    // libinput acceleration bias, -1 (slowest) .. 1 (fastest); applied to
    // every current and future pointer device that supports accel config
    virtual void setPointerSpeed(double speed) = 0;
    virtual double pointerSpeed() const = 0;

    static InputSource* createLibinput(Composer& c);
};
