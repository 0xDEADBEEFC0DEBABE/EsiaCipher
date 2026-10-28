// Cipher - the themes (theme.hpp).
#include "theme.hpp"
#include <cstddef>

namespace cipher
{
    namespace
    {
        constexpr Color H(std::uint32_t rgb) { return Color::Hex(rgb); }

        Theme Make(const char* name, const char* note, bool dark, std::uint32_t canvas, std::uint32_t sidebar, std::uint32_t surface,
                   std::uint32_t raised, std::uint32_t ink, std::uint32_t accent, std::uint32_t accentInk, std::uint32_t accent2,
                   std::uint32_t h0, std::uint32_t h1, std::uint32_t h2, std::uint32_t h3)
        {
            Theme t;
            t.name = name;
            t.note = note;
            t.dark = dark;
            t.canvas = H(canvas);
            t.sidebar = H(sidebar);
            t.surface = H(surface);
            t.raised = H(raised);
            t.ink = H(ink);
            t.accent = H(accent);
            t.accentInk = H(accentInk);
            t.accent2 = H(accent2);
            // status colors tuned per light / dark so they read on either
            t.success = dark ? H(0x3DD68C) : H(0x0E9F5E);
            t.warning = dark ? H(0xF5B83D) : H(0xC27C00);
            t.danger = dark ? H(0xFF5D6C) : H(0xD62839);
            t.info = dark ? H(0x5BA8FF) : H(0x1F6FEB);
            t.hack[0] = H(h0);
            t.hack[1] = H(h1);
            t.hack[2] = H(h2);
            t.hack[3] = H(h3);
            return t;
        }

        const Theme kThemes[] = {
            //    name         note                         dark   canvas    sidebar   surface   raised    ink       accent    accentInk accent2   hack: bg      dim       mid       bright
            Make("Graphite", "Monochrome, after hours", true, 0x212121, 0x171717, 0x2A2A2A, 0x2F2F2F, 0xECECEC, 0xECECEC, 0x0D0D0D, 0x8AB4FF, 0x04060A, 0x16304A, 0x2F81F7, 0xCDEBFF),
            Make("Porcelain", "Monochrome, daylight", false, 0xFFFFFF, 0xF9F9F9, 0xF3F3F3, 0xFFFFFF, 0x0D0D0D, 0x0D0D0D, 0xFFFFFF, 0x3E63DD, 0xF2F2EE, 0xC8C8C0, 0x6E6E66, 0x111111),
            Make("Matrix", "Green phosphor", true, 0x07110B, 0x050C08, 0x0C1A12, 0x102219, 0xD8FFE5, 0x00F06A, 0x00160A, 0x7DFFB3, 0x010A04, 0x003B16, 0x00B347, 0xA8FFC6),
            Make("Neon", "Magenta and cyan", true, 0x0E0A16, 0x0A0711, 0x171126, 0x1D152E, 0xF3EBFF, 0xFF3FD8, 0x1C0018, 0x2EE6FF, 0x07020D, 0x3A0F5C, 0xFF3FD8, 0x2EE6FF),
            Make("Amber", "Retro terminal", true, 0x120D05, 0x0D0903, 0x1C150A, 0x231A0C, 0xFFEBC2, 0xFFB000, 0x1A1000, 0xFF6A3D, 0x0A0600, 0x4A2C00, 0xFF9F00, 0xFFE08A),
            Make("Glacier", "Cold blue", true, 0x0B1220, 0x080E19, 0x111B2D, 0x162236, 0xE6F0FF, 0x5AA9FF, 0x001326, 0x7CF5FF, 0x02060D, 0x0D2B4F, 0x2F8BFF, 0xBFE6FF),
            Make("Crimson", "Red alert", true, 0x140A0B, 0x0E0607, 0x1E1012, 0x251416, 0xFFE8EA, 0xFF3B55, 0x1A0005, 0xFF9F43, 0x0A0203, 0x4A0A12, 0xFF2D4B, 0xFFC2CB),
            Make("Aurora", "Violet and teal", true, 0x0F0D1A, 0x0B0914, 0x171428, 0x1D1932, 0xEEEAFF, 0x9D7BFF, 0x0D0620, 0x3EE6C1, 0x04030C, 0x241552, 0x8B6CFF, 0x4BF2D0),
        };
    }

    std::span<const Theme> Themes() { return kThemes; }

    Color* ThemeColors(Theme& t) { return &t.canvas; }
    const Color* ThemeColors(const Theme& t) { return &t.canvas; }

    static_assert(offsetof(Theme, hack) + sizeof(Color) * 4 - offsetof(Theme, canvas) == sizeof(Color) * kThemeColors,
                  "the colors of a theme are one array from canvas to hack[3]");
}
