// Cipher - icons: the 1924 Lucide icons (lucide.dev, ISC license: lucide_icons.hpp), drawn two ways.
//
//   DrawIcon        the icon rasterized once per pixel size into the text system's tile pages (TextSystem::AddTile)
//                   and drawn as one quad on the pixel grid: it batches with the text around it, and a screen full of
//                   icons costs what a screen of glyphs costs.
//   DrawIconVector  its strokes through Painter::Polyline, every frame: turned, scaled, or drawn in part (a check mark
//                   that draws itself, a chevron that turns) - for icons in motion.
//
// Both cover pixels the same way (by the distance to the stroke's center line, round caps and joins as Lucide's SVGs
// have them), so an icon that comes to rest swaps from one to the other without a visible step.
#pragma once
#include "lucide_icons.hpp"
#include "esia/render/painter.hpp"
#include <cstdint>
#include <string_view>

namespace cipher
{
    namespace lucide
    {
        // One icon of the generated table (lucide_data.cpp): its commands kOps[firstOp, firstOp + ops) with their
        // coordinates from kCoords[firstCoord] on, in 1/256 of Lucide's 24-unit grid.
        struct IconData
        {
            const char* name;
            std::uint32_t firstOp, ops, firstCoord;
        };
        extern const std::uint8_t kOps[];
        extern const std::int16_t kCoords[];
        extern const IconData kIcons[];
        extern const int kIconTotal;
    }

    // "chevron-down" -> Icon::ChevronDown; Icon::None when Lucide has no such icon.
    Icon FindIcon(std::string_view name);
    const char* IconName(Icon icon);

    // `icon` centered on `center`, `size` UI units square (Lucide's grid scaled), its strokes `stroke` grid units wide
    // (Lucide's 2). The box lands on whole physical pixels (Painter::Env().pixelScale): crisp at any scale.
    void DrawIcon(esia::Painter& p, Icon icon, esia::Vec2 center, float size, esia::Color color, float stroke = 2.0f);

    struct IconMotion
    {
        float angle = 0.0f;   // radians, clockwise around the center
        float scale = 1.0f;
        float drawn = 1.0f;   // the share of its strokes' length drawn, in the order the SVG draws them (0..1)
    };
    void DrawIconVector(esia::Painter& p, Icon icon, esia::Vec2 center, float size, esia::Color color, const IconMotion& motion,
                        float stroke = 2.0f);

    // Rasterizes `icon` at `size` UI units now (the loading screen warms the icons the first frames would draw).
    void WarmIcon(esia::text::TextSystem& text, Icon icon, float size, float pixelScale, float stroke = 2.0f);
}
