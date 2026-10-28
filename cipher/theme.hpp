// Cipher - themes: a handful of colors per theme, everything else derived from them, so every theme keeps the same
// depth and contrast steps.
//
// Surfaces from back to front: canvas (the main area) < sidebar (beside it) < surface (cards, fields) < raised (menus,
// popups, floating windows). Text and lines are `ink` at fixed alphas (Ui::Ink): 0.92 primary, 0.64 secondary, 0.44
// tertiary and placeholders, 0.28 disabled, 0.10 hairlines - so they keep their contrast on whatever surface they lie.
// `accent` fills what is on (a switch, a check box, a slider's track, the primary button), `accentInk` draws on it.
// `hack` is the loading screen's palette: background, dim, mid, bright.
#pragma once
#include "esia/base/math.hpp"
#include <span>

namespace cipher
{
    using esia::Color;

    struct Theme
    {
        const char* name = "Graphite";
        const char* note = "";     // a few words for the theme picker
        bool dark = true;
        Color canvas, sidebar, surface, raised;
        Color ink;
        Color accent, accentInk;
        Color accent2;             // a second hue (charts, gradients, the logo)
        Color success, warning, danger, info;
        Color hack[4];
    };

    // Graphite, Porcelain, Matrix, Neon, Amber, Glacier, Crimson, Aurora.
    std::span<const Theme> Themes();

    // The colors of a theme in one array, for animating one theme into another color by color.
    inline constexpr int kThemeColors = 16;
    Color* ThemeColors(Theme& t);   // kThemeColors entries
    const Color* ThemeColors(const Theme& t);

    // `a` over `b` (straight alpha): an opaque surface tinted by a translucent color.
    inline Color Over(Color a, Color b)
    {
        const float o = a.a + b.a * (1.0f - a.a);
        if (o <= 0.0f)
            return Color::Clear();
        return Color((a.r * a.a + b.r * b.a * (1.0f - a.a)) / o, (a.g * a.a + b.g * b.a * (1.0f - a.a)) / o,
                     (a.b * a.a + b.b * b.a * (1.0f - a.a)) / o, o);
    }
    // Black or white, whichever reads better on `c`.
    inline Color ReadableOn(Color c) { return c.Luminance() > 0.55f ? Color::Hex(0x0D0D0D) : Color::White(); }
}
