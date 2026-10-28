// Cipher - animation. Every change on screen moves with a spring: a damped oscillator integrated in closed form, so
// it is the same at 30 or 300 frames a second, and a new target taken at any frame keeps where it is and how fast it
// moves - an animation interrupted, reversed or replayed halfway never jumps.
//
//   Spring s;                  // value 0
//   s.target = 1.0f;           // any frame: a new target
//   s.Step(dt, kSnappy);       // every frame: s.value moves on, velocity kept
//
// The Ui keeps one per animated property of an item (Ui::Animate): created at the item's first frame, dropped some
// frames after the item stopped asking for it.
#pragma once
#include "esia/base/math.hpp"
#include <cstdint>

namespace cipher
{
    using esia::Color;
    using esia::Vec2;

    // How a spring moves: `response` is the period it would oscillate with undamped (seconds: how fast), `damping` the
    // ratio to critical damping (1: no overshoot, below: a little bounce).
    struct Motion
    {
        float response = 0.30f;
        float damping = 1.0f;
    };

    inline constexpr Motion kSnappy{0.22f, 1.0f};     // hover, press, focus: quick, no overshoot
    inline constexpr Motion kSmooth{0.34f, 1.0f};     // state changes: a toggle's knob, a selection moving
    inline constexpr Motion kBouncy{0.42f, 0.72f};    // things that arrive: a popup, a notification, a check mark
    inline constexpr Motion kGentle{0.60f, 1.0f};     // large movements: a sidebar, a theme, a window's size
    inline constexpr Motion kSlow{1.10f, 1.0f};       // backgrounds, the loading screen's reveal

    struct Spring
    {
        float value = 0.0f;
        float velocity = 0.0f;
        float target = 0.0f;

        // Advances `dt` seconds towards `target`; false once at rest (then exactly at the target).
        bool Step(float dt, const Motion& m);
        void Snap(float v)
        {
            value = target = v;
            velocity = 0.0f;
        }
        bool Resting() const { return value == target && velocity == 0.0f; }
    };

    // A color as four springs (straight alpha), so a color change has the same motion as everything else.
    struct ColorSpring
    {
        Spring c[4];
        Color Value() const { return Color(c[0].value, c[1].value, c[2].value, c[3].value); }
        void SetTarget(Color v)
        {
            c[0].target = v.r;
            c[1].target = v.g;
            c[2].target = v.b;
            c[3].target = v.a;
        }
        void Snap(Color v)
        {
            c[0].Snap(v.r);
            c[1].Snap(v.g);
            c[2].Snap(v.b);
            c[3].Snap(v.a);
        }
        bool Step(float dt, const Motion& m)
        {
            bool moving = false;
            for (Spring& s : c)
                moving |= s.Step(dt, m);
            return moving;
        }
    };

    // Easing curves, for what follows a clock rather than a target (the loading screen's script, text typed in).
    float EaseOutCubic(float t);
    float EaseInOutCubic(float t);
    float EaseOutBack(float t, float overshoot = 1.4f);
    float Smoothstep(float a, float b, float x);

    inline float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
    inline float Mix(float a, float b, float t) { return a + (b - a) * t; }
    inline Color Mix(Color a, Color b, float t) { return Color(Mix(a.r, b.r, t), Mix(a.g, b.g, t), Mix(a.b, b.b, t), Mix(a.a, b.a, t)); }
    inline Vec2 Mix(Vec2 a, Vec2 b, float t) { return Vec2(Mix(a.x, b.x, t), Mix(a.y, b.y, t)); }
}
