// Cipher - numbers dragged, a vertical slider, colors, list boxes, tables and plots (cipher.hpp).
#include "cipher.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace cipher
{
    using namespace esia;
    namespace m = metrics;

    namespace
    {
        struct DragState
        {
            bool editing = false;
            std::string text;
            float shadow = 0.0f;   // DragInt: the value with its fraction (a slow drag still moves it)
            bool shadowSet = false;
        };
        struct VSliderGlass
        {
            Spring trail;
            bool placed = false;
        };
        struct ColorState
        {
            float h = 0.0f, s = 0.0f, v = 0.0f;   // kept through gray and black, where the color has no hue
            bool made = false;
            Spring sx, vy, hy, ay;                // the knobs (shares of their square and bars)
            Spring swellSV, swellHue, swellAlpha; // a knob swells while it is dragged
            bool placed = false;
            std::string hex;
            struct Ring
            {
                Vec2 at;                          // from the picker's top left
                double born = 0.0;
            };
            std::vector<Ring> rings;              // ripples where it was clicked
        };
        struct PlotState
        {
            // the values as drawn: a line's kPlotHistory older ones first (slid out of view at the left), then one per
            // value; each glides to its new one
            std::vector<float> shown;
            std::vector<float> last;   // the values as they came last frame
            float scroll = 0.0f;       // values the line still has to slide left by
            float rate = 0.0f;         // values a second it slides by: the pace they have been coming in at
            double lastShift = -1.0;
            Spring lo, hi;
            bool made = false;
            Spring hoverX, hoverShown;   // a line's: the entry of `shown` read
        };
        constexpr int kPlotHistory = 4;
        struct TableState
        {
            std::vector<float> share;   // each column's share of the width
            int sortColumn = -1;
            bool ascending = true, changed = false;
            float grab = 0.0f;
            float height = 0.0f;   // its frame's, last frame: what the rows' bands are cut to
        };

        void RgbToHsv(float r, float g, float b, float& h, float& s, float& v)
        {
            const float mx = std::max(r, std::max(g, b)), mn = std::min(r, std::min(g, b)), d = mx - mn;
            v = mx;
            s = mx > 0.0f ? d / mx : 0.0f;
            h = 0.0f;
            if (d > 1e-6f)
            {
                if (mx == r)
                    h = (g - b) / d + (g < b ? 6.0f : 0.0f);
                else if (mx == g)
                    h = (b - r) / d + 2.0f;
                else
                    h = (r - g) / d + 4.0f;
                h /= 6.0f;
            }
        }
        void HsvToRgb(float h, float s, float v, float& r, float& g, float& b)
        {
            const float hh = (h - std::floor(h)) * 6.0f;
            const int i = (int)hh;
            const float f = hh - (float)i, p = v * (1.0f - s), q = v * (1.0f - s * f), t = v * (1.0f - s * (1.0f - f));
            switch (i % 6)
            {
            case 0: r = v, g = t, b = p; break;
            case 1: r = q, g = v, b = p; break;
            case 2: r = p, g = v, b = t; break;
            case 3: r = p, g = q, b = v; break;
            case 4: r = t, g = p, b = v; break;
            default: r = v, g = p, b = q; break;
            }
        }
        Color HueColor(float h)
        {
            float r, g, b;
            HsvToRgb(h, 1.0f, 1.0f, r, g, b);
            return Color(r, g, b, 1.0f);
        }
        void Hex(char* out, std::size_t n, const float* c, bool alpha)
        {
            const auto b = [](float v) { return (int)std::lround(Clamp01(v) * 255.0f); };
            if (alpha)
                std::snprintf(out, n, "#%02X%02X%02X%02X", b(c[0]), b(c[1]), b(c[2]), b(c[3]));
            else
                std::snprintf(out, n, "#%02X%02X%02X", b(c[0]), b(c[1]), b(c[2]));
        }
        bool ParseHex(const std::string& s, float* c, bool alpha)
        {
            std::string h = s;
            if (!h.empty() && h[0] == '#')
                h.erase(h.begin());
            if (h.size() != 6 && h.size() != 8)
                return false;
            for (char ch : h)
                if (!std::isxdigit((unsigned char)ch))
                    return false;
            const unsigned long v = std::strtoul(h.c_str(), nullptr, 16);
            if (h.size() == 8)
            {
                c[0] = (float)((v >> 24) & 0xFF) / 255.0f;
                c[1] = (float)((v >> 16) & 0xFF) / 255.0f;
                c[2] = (float)((v >> 8) & 0xFF) / 255.0f;
                if (alpha)
                    c[3] = (float)(v & 0xFF) / 255.0f;
            }
            else
            {
                c[0] = (float)((v >> 16) & 0xFF) / 255.0f;
                c[1] = (float)((v >> 8) & 0xFF) / 255.0f;
                c[2] = (float)(v & 0xFF) / 255.0f;
            }
            return true;
        }
        // a checkerboard under `r` (corners `radius`): what shows through a transparent color
        void Checker(esia::Painter& p, const Rect& r, float radius, float cell, Color light, Color dark)
        {
            p.PushMask(r, radius);
            p.Rect(r, Style().Fill(light));
            int iy = 0;
            for (float y = r.min.y; y < r.max.y; y += cell, ++iy)
            {
                int ix = 0;
                for (float x = r.min.x; x < r.max.x; x += cell, ++ix)
                    if ((ix + iy) & 1)
                        p.Rect(Rect(x, y, std::min(x + cell, r.max.x), std::min(y + cell, r.max.y)), Style().Fill(dark));
            }
            p.PopMask();
        }
        // the glass a dragged knob turns into (the sliders' look): a drop swelling with `drag`, a trailing drop merged in
        void GlassDrop(esia::Painter& p, Vec2 knob, Vec2 trail, float drag, float glass)
        {
            const float R = 8.0f + 7.0f * drag;
            const float tr = R * 0.72f;
            GlassMaterial g;
            g.blur = 0.0f;
            g.refraction = 10.0f;
            g.bezel = R;
            g.dispersion = 0.6f;
            g.specular = 1.0f;
            g.saturation = 1.25f;
            g.brightness = 0.04f;
            g.magnify = 0.32f;
            g.noise = 0.0f;
            g.tint = Color(1.0f, 1.0f, 1.0f, 0.05f);
            g.rim = Color::White(0.6f);
            const float a0 = p.Alpha();
            p.SetAlpha(a0 * glass);
            p.Merge(Rect::FromCenter(knob, Vec2(R * 2.0f, R * 2.0f)), Rect::FromCenter(trail, Vec2(tr * 2.0f, tr * 2.0f)), R, R * 1.2f,
                    Style().Glass(g).Shadow(Color::Black(0.20f), 10.0f, Vec2(0.0f, 3.0f)));
            p.SetAlpha(a0);
        }
    }

    // ------------------------------------------------------------------ drags
    bool Ui::DragFloat(const char* label, float* v, float speed, float min, float max, const char* format)
    {
        const std::string_view text = Visible(label);
        if (!row_.on && !text.empty())
            Row(label);
        const Id id = IdOf(label);
        Rect field;
        float radius = m::kRadiusInner;
        if (!TakeRow(160.0f, field))
        {
            field = ItemRect(Vec2(nextWidth_ > 0.0f ? nextWidth_ : ContentWidth(), m::kRow));
            radius = m::kRadiusControl;
        }
        nextWidth_ = 0.0f;
        DragState& ds = ctx_.State<DragState>(id);
        const bool ranged = min < max;
        char buf[64];
        std::snprintf(buf, sizeof(buf), format, (double)*v);
        const InputState& in = ctx_.Input();
        bool changed = false;
        // typed: the editor in its place; Enter or a click elsewhere takes the value, Escape keeps the old one
        if (ds.editing && !inert_)
        {
            bool entered = false;
            EditText(id, field, ds.text, InputTextFlags_CharsDecimal, nullptr, false, entered, radius);
            const bool focused = ctx_.KeyboardFocusId() == id;
            if (entered || !focused)
            {
                char* end = nullptr;
                const double d = std::strtod(ds.text.c_str(), &end);
                if (!ds.text.empty() && end && *end == 0 && !in.KeyPressed(Key::Escape, false))
                {
                    const float nv = ranged ? std::clamp((float)d, min, max) : (float)d;
                    changed = nv != *v;
                    *v = nv;
                }
                ds.editing = false;
                if (focused)
                    ctx_.SetKeyboardFocusId(0);
            }
            return changed;
        }
        lastId_ = id;
        const bool disabled = disabled_.back();
        if (!AddItem(id, field, ItemFlags_Focusable | (disabled ? ItemFlags_Disabled : 0u)))
        {
            lastHovered_ = false;
            return false;
        }
        const ButtonResult b = Behavior(id, field, ButtonFlags_PressOnClick | ButtonFlags_FocusOnClick);
        lastHovered_ = b.hovered;
        const bool focusedHere = !inert_ && ctx_.KeyboardFocusId() == id;
        if (!disabled && ((b.pressed && b.clicks >= 2) || (focusedHere && ctx_.KeyPressed(Key::Enter, id, false))))
        {
            ds.editing = true;
            ds.text = buf;
            ctx_.SetKeyboardFocusId(id);
            SelectAllText(id);
        }
        const bool active = !inert_ && ctx_.ActiveId() == id;
        if (active && in.MouseDown(MouseButton::Left) && !disabled && !ds.editing)
        {
            float k = speed;
            if (in.Mods() & Mod_Shift)
                k *= 10.0f;
            if (in.Mods() & Mod_Alt)
                k *= 0.1f;
            const float dx = in.MouseDelta().x;
            if (dx != 0.0f)
            {
                const float nv = ranged ? std::clamp(*v + dx * k, min, max) : *v + dx * k;
                changed = nv != *v;
                *v = nv;
            }
        }
        if (focusedHere && !disabled && !ds.editing)
        {
            const float step = speed * ((in.Mods() & Mod_Shift) ? 10.0f : 1.0f) * 10.0f;
            float nv = *v;
            if (ctx_.KeyPressed(Key::Left, id) || ctx_.KeyPressed(Key::Down, id))
                nv -= step;
            if (ctx_.KeyPressed(Key::Right, id) || ctx_.KeyPressed(Key::Up, id))
                nv += step;
            if (ranged)
                nv = std::clamp(nv, min, max);
            if (nv != *v)
            {
                *v = nv;
                changed = true;
            }
        }
        if (changed)
            std::snprintf(buf, sizeof(buf), format, (double)*v);
        if ((b.hovered || active) && !disabled)
            ctx_.SetMouseCursor(MouseCursor::ResizeEW);
        const float hover = Anim(id, 0, (b.hovered || active) ? 1.0f : 0.0f);
        const float drag = Anim(id, 1, active ? 1.0f : 0.0f, kSmooth);
        esia::Painter p = Painter();
        p.Rect(field, Style().Radius(radius).Fill(Ink(0.035f + 0.025f * hover + 0.02f * drag))
                          .Stroke(1.0f / pixelScale_, Mix(Ink(0.12f + 0.06f * hover), shown_.accent.Fade(0.9f), drag), 0.0f));
        // with a range: its share fills the field behind the number
        if (ranged)
        {
            const float t = Anim(id, 2, Clamp01((*v - min) / (max - min)), Motion{0.12f, 1.0f});
            if (t > 0.002f)
            {
                p.PushMask(field, radius);
                p.Rect(Rect(field.min.x, field.min.y, field.min.x + field.Width() * t, field.max.y), Style().Fill(shown_.accent.Fade(0.12f + 0.08f * drag)));
                p.PopMask();
            }
        }
        // chevrons at both ends while it is under the pointer: it drags sideways
        const float cy = field.Center().y;
        if (hover > 0.004f)
        {
            DrawIcon(p, Icon::ChevronLeft, Vec2(field.min.x + 14.0f, cy), 14.0f, Ink(0.5f * hover));
            DrawIcon(p, Icon::ChevronRight, Vec2(field.max.x - 14.0f, cy), 14.0f, Ink(0.5f * hover));
        }
        MorphLabel(p, HashInt(0x44524756, id), field.Center().x, cy, {fonts_.mono, m::kFontMono}, Mix(Ink(0.9f), shown_.accent, drag), buf, 0.5f, field.Width() - 44.0f);
        FocusRing(p, field, radius, id);
        return changed;
    }

    bool Ui::DragInt(const char* label, int* v, float speed, int min, int max)
    {
        DragState& ds = ctx_.State<DragState>(IdOf(label));
        if (!ds.shadowSet || std::lround(ds.shadow) != *v)
        {
            ds.shadow = (float)*v;
            ds.shadowSet = true;
        }
        float f = ds.shadow;
        DragFloat(label, &f, speed, (float)min, (float)max, "%.0f");
        ds.shadow = f;
        const int next = (int)std::lround(f);
        if (next == *v)
            return false;
        *v = next;
        return true;
    }

    bool Ui::VSliderFloat(const char* label, Vec2 size, float* v, float min, float max, const char* format)
    {
        const std::string_view text = Visible(label);
        const Id id = IdOf(label);
        const Vec2 sz(size.x > 0.0f ? size.x : 40.0f, size.y > 0.0f ? size.y : 160.0f);
        const Rect r = ItemRect(Vec2(sz.x, sz.y + (text.empty() ? 24.0f : 44.0f)));
        const Rect area(r.min.x, r.min.y, r.max.x, r.min.y + sz.y);
        lastId_ = id;
        const bool disabled = disabled_.back();
        if (!AddItem(id, area, ItemFlags_Focusable | (disabled ? ItemFlags_Disabled : 0u)))
        {
            lastHovered_ = false;
            return false;
        }
        const ButtonResult b = Behavior(id, area, ButtonFlags_PressOnClick | ButtonFlags_FocusOnClick);
        lastHovered_ = b.hovered;
        const bool active = !inert_ && ctx_.ActiveId() == id;
        const InputState& in = ctx_.Input();
        const float y0 = area.max.y - 10.0f, y1 = area.min.y + 10.0f;
        const float before = *v;
        if (active && in.MouseDown(MouseButton::Left) && !disabled)
            *v = min + (max - min) * Clamp01((y0 - in.MousePos().y) / std::max(y0 - y1, 1.0f));
        if (!inert_ && ctx_.KeyboardFocusId() == id && !disabled)
        {
            const float step = (max - min) / ((in.Mods() & Mod_Shift) ? 10.0f : 100.0f);
            if (ctx_.KeyPressed(Key::Down, id))
                *v = std::max(min, *v - step);
            if (ctx_.KeyPressed(Key::Up, id))
                *v = std::min(max, *v + step);
        }
        const bool changed = *v != before;
        if ((b.hovered || active) && !disabled)
            ctx_.SetMouseCursor(MouseCursor::Hand);
        const float t = Anim(id, 0, Clamp01((*v - min) / std::max(max - min, 1e-6f)), Motion{0.12f, 1.0f});
        const float hover = Anim(id, 1, (b.hovered || active) ? 1.0f : 0.0f);
        const float drag = Anim(id, 2, active ? 1.0f : 0.0f, kBouncy);
        esia::Painter p = Painter();
        const float cx = area.Center().x, ky = y0 + (y1 - y0) * t;
        const float w = 2.0f + hover;
        p.Capsule(Rect(cx - w * 0.5f, y1, cx + w * 0.5f, y0), Style().Fill(Ink(0.14f)));
        p.Capsule(Rect(cx - w * 0.5f, std::min(ky, y0 - w), cx + w * 0.5f, y0), Style().Fill(shown_.accent));
        for (int i = 0; i <= 10; ++i)
        {
            const float y = std::round((y0 + (y1 - y0) * (float)i / 10.0f) * pixelScale_) / pixelScale_;
            const float len = (i % 5 == 0) ? 6.0f : 3.0f;
            p.Rect(Rect(cx + 7.0f, y, cx + 7.0f + len, y + 1.0f / pixelScale_), Style().Fill(Ink(0.14f)));
        }
        const float glass = Clamp01(drag * 1.5f);
        if (glass < 0.999f)
        {
            const float a0 = p.Alpha();
            p.SetAlpha(a0 * (1.0f - glass));
            p.Circle(Vec2(cx, ky), 8.0f + 1.5f * hover, Style().Fill(shown_.raised.WithAlpha(1.0f)).Stroke(2.0f, shown_.accent, 0.0f).Shadow(Color::Black(0.3f), 6.0f, Vec2(0, 2)));
            p.SetAlpha(a0);
        }
        // the trail's place along the track (from its bottom): a window dragged carries it along
        VSliderGlass& sg = ctx_.State<VSliderGlass>(id);
        if (!sg.placed || drag <= 0.003f)
        {
            sg.trail.Snap(y0 - ky);
            sg.placed = true;
        }
        sg.trail.target = y0 - ky;
        sg.trail.Step(dt_, Motion{0.24f, 0.5f});
        if (drag > 0.003f)
            GlassDrop(p, Vec2(cx, ky), Vec2(cx, y0 - sg.trail.value), drag, glass);
        char buf[64];
        std::snprintf(buf, sizeof(buf), format, (double)*v);
        MorphLabel(p, HashInt(0x56534C, id), cx, area.max.y + 12.0f, {fonts_.mono, 12.5f}, Mix(Ink(0.86f), shown_.accent, Clamp01(drag)), buf, 0.5f);
        if (!text.empty())
            Label(p, cx, area.max.y + 32.0f, {fonts_.text, 12.5f}, Ink(0.52f), text, 0.5f, sz.x + 24.0f);
        FocusRing(p, area, m::kRadiusInner, id);
        return changed;
    }

    // ------------------------------------------------------------------ colors
    void Ui::DrawSwatch(esia::Painter& p, const Rect& r, float radius, Color c, float glow)
    {
        if (glow > 0.004f)
            p.Rect(r, Style().Radius(radius).Fill(c.WithAlpha(0.0f)).Glow(c.WithAlpha(1.0f), 8.0f, 0.35f * glow * c.a));
        if (c.a < 0.999f)
            Checker(p, r, radius, 6.0f, Color::Hex(0xEDEDED), Color::Hex(0xBDBDBD));
        p.Rect(r, Style().Radius(radius).Fill(c).Stroke(1.0f / pixelScale_, Ink(0.16f), 0.0f));
    }

    bool Ui::ColorButton(const char* idLabel, Color c, Vec2 size)
    {
        const Id id = IdOf(idLabel);
        Rect r;
        const bool inRow = TakeRow(m::kControl, r);
        if (!inRow)
            r = ItemRect(Vec2(size.x > 0.0f ? size.x : m::kRow, size.y > 0.0f ? size.y : m::kRow));
        lastId_ = id;
        const bool disabled = disabled_.back();
        if (!AddItem(id, r, ItemFlags_Focusable | (disabled ? ItemFlags_Disabled : 0u)))
        {
            lastHovered_ = false;
            return false;
        }
        const ButtonResult b = Behavior(id, r);
        lastHovered_ = b.hovered;
        if (b.hovered && !disabled)
            ctx_.SetMouseCursor(MouseCursor::Hand);
        const float hover = Anim(id, 0, b.hovered ? 1.0f : 0.0f);
        const float press = Anim(id, 1, b.held ? 1.0f : 0.0f);
        // its color glides when it changes
        const Color shown = AnimColor(id, 0, c, kSmooth);
        esia::Painter p = Painter();
        const float radius = inRow ? m::kRadiusInner : std::min(m::kRadiusControl, r.Height() * 0.5f);
        p.PushScale(r.Center(), 1.0f + 0.04f * hover - 0.06f * press);
        DrawSwatch(p, r, radius, shown, hover);
        p.PopScale();
        FocusRing(p, r, radius, id);
        return b.pressed && !disabled;
    }

    // The picker: the saturation / value square glowing in the color picked (not in a popup, where there is no room
    // for the glow), the hue bar (and the alpha bar), and - where there is room - an orb of the color with its code.
    // Its knobs are lenses of liquid glass that magnify what is under them, swelling and lifting (their shadows
    // growing) while dragged and gliding where a click sends them; a click ripples out from where it lands.
    bool Ui::ColorPicker3(const char* label, float col[3]) { return ColorPickerEx(label, col, false); }
    bool Ui::ColorPicker4(const char* label, float col[4]) { return ColorPickerEx(label, col, true); }

    bool Ui::ColorPickerEx(const char* label, float* col, bool alpha)
    {
        const Id id = IdOf(label);
        ColorState& cs = ctx_.State<ColorState>(id);
        // the color as hue, saturation and value - keeping the hue and saturation it had where it has none
        float h, s, v;
        RgbToHsv(col[0], col[1], col[2], h, s, v);
        if (!cs.made)
        {
            cs.h = h, cs.s = s, cs.v = v;
            cs.made = true;
        }
        else
        {
            float r2, g2, b2;
            HsvToRgb(cs.h, cs.s, cs.v, r2, g2, b2);
            if (std::fabs(r2 - col[0]) + std::fabs(g2 - col[1]) + std::fabs(b2 - col[2]) > 1e-3f)
            {
                if (s > 1e-4f && v > 1e-4f)
                    cs.h = h;
                if (v > 1e-4f)
                    cs.s = s;
                cs.v = v;
            }
        }
        const float bar = 18.0f, gap = 14.0f, orbD = 84.0f;
        const float bars = (alpha ? 2.0f : 1.0f) * (bar + gap);
        const float avail = ContentWidth();
        const float S = std::max(std::min(220.0f, avail - bars), 80.0f);
        const bool orb = avail >= S + bars + gap + orbD;
        const Rect area = ItemRect(Vec2(S + bars + (orb ? gap + orbD : 0.0f), S));
        ctx_.ItemAdd(0, area);
        const Rect sq(area.min, area.min + Vec2(S, S));
        const Rect hueR(sq.max.x + gap, sq.min.y, sq.max.x + gap + bar, sq.max.y);
        const Rect alR(hueR.max.x + gap, sq.min.y, hueR.max.x + gap + bar, sq.max.y);   // with alpha
        const float alphaNow = alpha ? col[3] : 1.0f;
        const InputState& in = ctx_.Input();
        const double now = in.Time();
        bool changed = false;
        bool held[3] = {};
        const auto dragArea = [&](int which, const Rect& r) {
            const Id did = HashInt(which, id);
            AddItem(did, r);
            const ButtonResult b = Behavior(did, r, ButtonFlags_PressOnClick);
            if (b.hovered)
                ctx_.SetMouseCursor(MouseCursor::Hand);
            if (b.pressed && !inert_)
                cs.rings.push_back({in.MousePos() - area.min, now});
            held[which - 1] = !inert_ && ctx_.ActiveId() == did && in.MouseDown(MouseButton::Left);
            return held[which - 1];
        };
        if (dragArea(1, sq))
        {
            cs.s = Clamp01((in.MousePos().x - sq.min.x) / S);
            cs.v = 1.0f - Clamp01((in.MousePos().y - sq.min.y) / S);
            changed = true;
        }
        if (dragArea(2, hueR))
        {
            cs.h = std::min(Clamp01((in.MousePos().y - hueR.min.y) / S), 0.9999f);
            changed = true;
        }
        if (alpha && dragArea(3, alR))
        {
            col[3] = 1.0f - Clamp01((in.MousePos().y - alR.min.y) / S);
            changed = true;
        }
        if (changed)
            HsvToRgb(cs.h, cs.s, cs.v, col[0], col[1], col[2]);
        // the knobs glide to where they are sent; each swells while it is held (a little bounce as it lets go)
        Spring* knobs[4] = {&cs.sx, &cs.vy, &cs.hy, &cs.ay};
        const float targets[4] = {cs.s, 1.0f - cs.v, cs.h, 1.0f - alphaNow};
        for (int i = 0; i < 4; ++i)
        {
            if (!cs.placed)
                knobs[i]->Snap(targets[i]);
            knobs[i]->target = targets[i];
            knobs[i]->Step(dt_, Motion{0.16f, 1.0f});
        }
        cs.placed = true;
        Spring* swells[3] = {&cs.swellSV, &cs.swellHue, &cs.swellAlpha};
        for (int i = 0; i < 3; ++i)
        {
            swells[i]->target = held[i] ? 1.0f : 0.0f;
            swells[i]->Step(dt_, kBouncy);
        }
        esia::Painter p = Painter();
        const Color picked(col[0], col[1], col[2], 1.0f);
        const Color glow = AnimColor(id, 0, picked, kSmooth);
        // the square: its hue the knob's (it glides with it), a glow of the color picked around it
        const Color hue = HueColor(cs.hy.value);
        Style square = Style().Radius(m::kRadiusInner).Fill(Paint::Linear(Color::White(), hue, 0.0f));
        if (windows_.empty() || !windows_.back().popup)
            square.Glow(glow, 14.0f, 0.3f);
        p.Rect(sq, square);
        p.Rect(sq, Style().Radius(m::kRadiusInner).Fill(Paint::Linear(Color(0.0f, 0.0f, 0.0f, 0.0f), Color::Black(), 90.0f)).Stroke(1.0f / pixelScale_, Ink(0.12f), 0.0f));
        p.PushMask(hueR, bar * 0.5f);
        for (int i = 0; i < 6; ++i)
        {
            const float a = (float)i / 6.0f, z = (float)(i + 1) / 6.0f;
            p.Rect(Rect(hueR.min.x, hueR.min.y + S * a, hueR.max.x, hueR.min.y + S * z + 0.5f), Style().Fill(Paint::Linear(HueColor(a), HueColor(z), 90.0f)));
        }
        p.PopMask();
        if (alpha)
        {
            Checker(p, alR, bar * 0.5f, 5.0f, Color::Hex(0xEDEDED), Color::Hex(0xBDBDBD));
            p.Rect(alR, Style().Radius(bar * 0.5f).Fill(Paint::Linear(glow, glow.WithAlpha(0.0f), 90.0f)).Stroke(1.0f / pixelScale_, Ink(0.12f), 0.0f));
        }
        // ripples where it was clicked: a ring spreading out and fading, inside what was clicked
        cs.rings.erase(std::remove_if(cs.rings.begin(), cs.rings.end(), [&](const ColorState::Ring& r) { return now - r.born >= 0.7; }), cs.rings.end());
        for (const ColorState::Ring& r : cs.rings)
        {
            const float age = (float)((now - r.born) / 0.7);
            const Vec2 at = area.min + r.at;
            const Rect& clicked = sq.Contains(at) ? sq : (hueR.Contains(at) ? hueR : alR);
            p.PushMask(clicked, clicked == sq ? m::kRadiusInner : bar * 0.5f);
            p.Ring(at, 6.0f + 64.0f * EaseOutCubic(age), 2.0f * (1.0f - age) + 0.5f, Style().Fill(Color::White(0.85f * (1.0f - age) * (1.0f - age))));
            p.PopMask();
        }
        // the knobs: lenses of liquid glass magnifying the color under them, a white rim to find them by
        const auto lens = [](float radius, float magnify) {
            GlassMaterial g;
            g.blur = 0.0f;
            g.refraction = 9.0f;
            g.bezel = radius;
            g.dispersion = 0.5f;
            g.specular = 1.0f;
            g.saturation = 1.15f;
            g.brightness = 0.02f;
            g.magnify = magnify;
            g.noise = 0.0f;
            g.tint = Color(1.0f, 1.0f, 1.0f, 0.04f);
            g.rim = Color::White(0.75f);
            return g;
        };
        const float sw = cs.swellSV.value;
        const float R = 11.0f + 7.0f * sw;
        const Vec2 k(sq.min.x + S * cs.sx.value, sq.min.y + S * cs.vy.value);
        const auto lift = [](float swell) { return Style().Shadow(Color::Black(0.32f + 0.12f * swell), 8.0f + 10.0f * swell, Vec2(0.0f, 2.0f + 5.0f * swell)); };
        p.Circle(k, R, lift(sw).Glass(lens(R, 0.55f + 0.3f * Clamp01(sw))).Stroke(1.5f, Color::White(0.92f), 1.0f));
        const auto barKnob = [&](const Rect& r, float at, float swell) {
            const float y = r.min.y + S * at, half = 6.0f + 4.0f * swell;
            const Rect knob(r.min.x - 4.0f - 2.0f * swell, y - half, r.max.x + 4.0f + 2.0f * swell, y + half);
            p.Rect(knob, lift(swell).Radius(half).Glass(lens(half, 0.4f + 0.25f * Clamp01(swell))).Stroke(1.5f, Color::White(0.92f), 1.0f));
        };
        barKnob(hueR, cs.hy.value, cs.swellHue.value);
        if (alpha)
            barKnob(alR, cs.ay.value, cs.swellAlpha.value);
        // the orb: the color under clear glass, glowing in it - as it is on its left, over a checkerboard with its alpha
        // on its right; its code and channels under it, rolling as they change. It leans in while a knob is dragged.
        if (orb)
        {
            const float orr = orbD * 0.5f;
            const Vec2 oc((alpha ? alR : hueR).max.x + gap + orr, sq.min.y + orr);
            const Rect ob = Rect::FromCenter(oc, Vec2(orbD, orbD));
            const float lean = std::max(cs.swellSV.value, std::max(cs.swellHue.value, alpha ? cs.swellAlpha.value : 0.0f));
            p.PushScale(oc, 1.0f + 0.04f * lean);
            const Color shown = AnimColor(id, 1, Color(col[0], col[1], col[2], alphaNow), kSmooth);
            p.Circle(oc, orr, Style().Fill(shown.WithAlpha(1.0f)).Glow(glow, 12.0f, 0.35f).Shadow(Color::Black(0.3f), 14.0f, Vec2(0.0f, 5.0f)));
            if (shown.a < 0.999f)
            {
                p.PushClip(Rect(oc.x, ob.min.y, ob.max.x, ob.max.y));
                Checker(p, ob, orr, 7.0f, Color::Hex(0xEDEDED), Color::Hex(0xBDBDBD));
                p.Circle(oc, orr, Style().Fill(shown));
                p.PopClip();
            }
            GlassMaterial g = lens(orr, 0.15f);
            g.refraction = 16.0f;
            g.dispersion = 0.25f;   // no rainbow over the checkerboard
            g.saturation = 1.0f;
            g.brightness = 0.0f;
            g.tint = Color::White(0.0f);
            p.Circle(oc, orr, Style().Glass(g));
            p.PopScale();
            char hex[16];
            const float c4[4] = {col[0], col[1], col[2], alphaNow};
            Hex(hex, sizeof(hex), c4, alphaNow < 0.999f);
            const float tx = oc.x;
            float ty = oc.y + orr + 22.0f;
            MorphLabel(p, HashInt(0x4F524248, id), tx, ty, {fonts_.monoSemibold, 13.0f}, Ink(0.9f), hex, 0.5f);
            const char* names[3] = {"R", "G", "B"};
            for (int i = 0; i < 3; ++i)
            {
                ty += 20.0f;
                char line[16];
                std::snprintf(line, sizeof(line), "%s %3d", names[i], (int)std::lround(Clamp01(col[i]) * 255.0f));
                MorphLabel(p, HashInt(0x4F524230 + i, id), tx, ty, {fonts_.mono, 12.5f}, Ink(0.55f), line, 0.5f);
            }
        }
        return changed;
    }

    bool Ui::ColorEditEx(const char* label, float* col, bool alpha)
    {
        const std::string_view text = Visible(label);
        if (!row_.on && !text.empty())
            Row(label);
        const Id id = IdOf(label);
        const Id pid = HashInt(0x434F4C50, id);
        float c4[4] = {col[0], col[1], col[2], alpha ? col[3] : 1.0f};
        Rect ctrl;
        if (!TakeRow(m::kControl + 6.0f + 120.0f, ctrl))
            ctrl = ItemRect(Vec2(m::kControl + 6.0f + 120.0f, m::kControl));
        const Rect sw(ctrl.min.x, ctrl.min.y, ctrl.min.x + m::kControl, ctrl.max.y);
        const Rect field(sw.max.x + 6.0f, ctrl.min.y, ctrl.max.x, ctrl.max.y);
        bool changed = false;
        // the swatch: its color gliding; a click opens the picker under it (another closes it)
        const Id sid = HashInt(1, id);
        lastId_ = sid;
        if (AddItem(sid, sw, ItemFlags_Focusable))
        {
            const ButtonResult b = Behavior(sid, sw, ButtonFlags_PressOnClick);
            lastHovered_ = b.hovered;
            if (b.hovered)
                ctx_.SetMouseCursor(MouseCursor::Hand);
            if (b.pressed && !inert_)
            {
                if (ctx_.IsPopupOpen(pid))
                    ctx_.ClosePopup(pid);
                else
                    ctx_.OpenPopup(pid);
            }
            const float hover = Anim(sid, 0, b.hovered ? 1.0f : 0.0f);
            esia::Painter p = Painter();
            p.PushScale(sw.Center(), 1.0f + 0.04f * hover);
            DrawSwatch(p, sw, m::kRadiusInner, AnimColor(sid, 0, Color(c4[0], c4[1], c4[2], c4[3]), kSmooth), hover);
            p.PopScale();
            FocusRing(p, sw, m::kRadiusInner, sid);
        }
        // the hex code: typed, it takes the color when it parses; at rest it shows the color's (rolling as it changes)
        ColorState& cs = ctx_.State<ColorState>(id);
        char hex[16];
        Hex(hex, sizeof(hex), c4, alpha);
        const Id hid = HashInt(2, id);
        if (inert_ || ctx_.KeyboardFocusId() != hid)
            cs.hex = hex;
        bool entered = false;
        if (EditText(hid, field, cs.hex, InputTextFlags_None, nullptr, false, entered, m::kRadiusInner) && ParseHex(cs.hex, c4, alpha))
            changed = true;
        if (entered)
            ctx_.SetKeyboardFocusId(0);
        // the picker
        // the picker, 12 in from the popup's edges all round: its padding and as much again (the bars and the gaps
        // before them as ColorPickerEx lays them out)
        const Vec2 d = ctx_.DisplaySize();
        const bool above = sw.max.y + 260.0f > d.y && sw.min.y > 260.0f;
        const float bars = alpha ? 64.0f : 32.0f;
        if (BeginPopupEx(pid, above ? Vec2(sw.min.x, sw.min.y - 6.0f) : Vec2(sw.min.x, sw.max.y + 6.0f), above ? Vec2(0.0f, 1.0f) : Vec2(0.0f, 0.0f),
                         Vec2(244.0f + bars, 0.0f), Vec2(sw.Center().x, above ? sw.min.y : sw.max.y), false, true))
        {
            ctx_.SetCursorPos(ctx_.CursorPos() + Vec2(6.0f, 6.0f));
            if (ColorPickerEx("##picker", c4, alpha))
                changed = true;
            ctx_.ItemSize(Vec2(0.0f, 0.0f));
            EndPopupEx();
        }
        if (changed)
        {
            col[0] = c4[0], col[1] = c4[1], col[2] = c4[2];
            if (alpha)
                col[3] = c4[3];
        }
        return changed;
    }

    bool Ui::ColorEdit4(const char* label, float col[4]) { return ColorEditEx(label, col, true); }
    bool Ui::ColorEdit3(const char* label, float col[3]) { return ColorEditEx(label, col, false); }

    // ------------------------------------------------------------------ list boxes
    bool Ui::BeginListBox(const char* label, Vec2 size)
    {
        const std::string_view text = Visible(label);
        if (!text.empty())
            TextUnformatted(text, Tone::Secondary, m::kFontSmall);
        // a framed panel (radius 12) spanning the content like a row, its selectables 6 in (radius 6 ... their 12)
        const float w = size.x > 0.0f ? size.x : ContentWidth();
        const float h = size.y > 0.0f ? size.y : 6.0f + 5.0f * (34.0f + 6.0f);
        const Vec2 at = ctx_.CursorPos();
        const float bleed = size.x > 0.0f ? 0.0f : bleed_;
        const Rect panel(at.x - bleed, at.y, at.x + w, at.y + h);
        esia::Painter p = Painter();
        p.Rect(panel, Style().Radius(m::kRadiusPanel).Fill(Ink(0.03f)).Stroke(1.0f / pixelScale_, Ink(0.08f), 0.0f));
        ctx_.SetCursorPos(panel.min);
        ChildOptions co;
        co.size = panel.Size();
        co.padding = Vec2(6.0f, 6.0f);
        co.clipInset = Rect(-6.0f, -6.0f, -6.0f, -6.0f);
        co.flags = ChildFlags_ScrollY | ChildFlags_SmoothScroll;
        const bool visible = ctx_.BeginChild(label, co);
        WinFrame wf;
        wf.id = IdOf(label);
        wf.body = true;
        wf.roundTop = wf.roundBottom = m::kRadiusPanel;
        BeginFade(wf, ctx_.ViewRect());
        windows_.push_back(wf);
        bleedStack_.push_back(bleed_);
        bleed_ = 0.0f;
        return visible;
    }

    void Ui::EndListBox()
    {
        EndChild();
        if (!bleedStack_.empty())
        {
            bleed_ = bleedStack_.back();
            bleedStack_.pop_back();
        }
    }

    bool Ui::ListBox(const char* label, int* current, const char* const* items, int count, int heightInItems)
    {
        bool changed = false;
        if (BeginListBox(label, Vec2(0.0f, 6.0f + (float)heightInItems * (34.0f + 6.0f))))
        {
            for (int i = 0; i < count; ++i)
            {
                ctx_.PushId(i);
                if (Selectable(items[i], *current == i))
                {
                    changed = *current != i;
                    *current = i;
                }
                ctx_.PopId();
            }
        }
        EndListBox();
        return changed;
    }

    // ------------------------------------------------------------------ tables
    bool Ui::BeginTable(const char* idLabel, int columns, std::uint32_t flags)
    {
        row_ = PendingRow();
        TableFrame t;
        t.id = IdOf(idLabel);
        t.flags = flags;
        t.columns = std::clamp(columns, 1, 64);
        const Vec2 at = ctx_.CursorPos();
        t.lineX = at.x;
        // a surface spanning the content, as a row's: its text on the column
        t.area = Rect(at.x - bleed_, at.y, at.x + ContentWidth(), at.y);
        ctx_.State<TableState>(t.id).changed = false;
        tables_.push_back(std::move(t));
        return true;
    }

    void Ui::TableSetupColumn(const char* label, float weight)
    {
        if (tables_.empty())
            return;
        TableFrame& t = tables_.back();
        t.labels.emplace_back(Visible(label));
        t.weights.push_back(std::max(weight, 0.05f));
    }

    void Ui::TableColumns(TableFrame& t)
    {
        if (!t.edges.empty())
            return;
        TableState& st = ctx_.State<TableState>(t.id);
        if ((int)st.share.size() != t.columns)
        {
            st.share.assign((std::size_t)t.columns, 1.0f);
            float sum = 0.0f;
            for (int c = 0; c < t.columns; ++c)
                sum += (st.share[(std::size_t)c] = c < (int)t.weights.size() ? t.weights[(std::size_t)c] : 1.0f);
            for (float& s : st.share)
                s /= sum;
        }
        const float w = t.area.Width();
        t.edges.assign((std::size_t)t.columns + 1, 0.0f);
        for (int c = 0; c < t.columns; ++c)
            t.edges[(std::size_t)c + 1] = t.edges[(std::size_t)c] + st.share[(std::size_t)c] * w;
    }

    void Ui::TableHeadersRow()
    {
        if (tables_.empty())
            return;
        TableFrame& t = tables_.back();
        TableColumns(t);
        TableState& st = ctx_.State<TableState>(t.id);
        const float h = 36.0f;
        const Rect hr(t.area.min.x, t.area.min.y + t.y, t.area.max.x, t.area.min.y + t.y + h);
        esia::Painter p = Painter();
        const InputState& in = ctx_.Input();
        const text::FontRef f{fonts_.textSemibold, 13.0f};
        for (int c = 0; c < t.columns; ++c)
        {
            const Rect cr(t.area.min.x + t.edges[(std::size_t)c], hr.min.y, t.area.min.x + t.edges[(std::size_t)c + 1], hr.max.y);
            const Id cid = HashInt(c, t.id);
            bool hovered = false, held = false;
            if ((t.flags & TableFlags_Sortable) && AddItem(cid, cr))
            {
                const ButtonResult b = Behavior(cid, cr);
                hovered = b.hovered;
                held = b.held;
                if (b.hovered)
                    ctx_.SetMouseCursor(MouseCursor::Hand);
                if (b.pressed)
                {
                    if (st.sortColumn == c)
                        st.ascending = !st.ascending;
                    else
                    {
                        st.sortColumn = c;
                        st.ascending = true;
                    }
                    st.changed = true;
                }
            }
            const float hover = Anim(cid, 0, hovered ? 1.0f : 0.0f);
            const float press = Anim(cid, 3, held ? 1.0f : 0.0f);
            const float on = Anim(cid, 1, st.sortColumn == c ? 1.0f : 0.0f, kSmooth);
            const float flip = Anim(cid, 2, st.ascending ? 0.0f : 1.0f, kSmooth);
            if (hover + press > 0.004f)
                p.Rect(cr.Expanded(-3.0f, -4.0f), Style().Radius(m::kRadiusInner).Fill(Ink(0.04f * hover + 0.03f * press)));
            const std::string& label = c < (int)t.labels.size() ? t.labels[(std::size_t)c] : std::string();
            const float lw = std::min(Measure(f, label).x, cr.Width() - 40.0f);
            Label(p, cr.min.x + 12.0f, cr.Center().y, f, Mix(Ink(0.55f + 0.2f * hover), Ink(0.92f), on), label, 0.0f, cr.Width() - 40.0f);
            // the column it is sorted by: an arrow that turns over as the order reverses
            if (on > 0.004f)
            {
                IconMotion mo;
                mo.angle = kPi * flip;
                DrawIconVector(p, Icon::ChevronUp, Vec2(cr.min.x + 12.0f + lw + 12.0f, cr.Center().y), 14.0f, shown_.accent.Fade(on), mo);
            }
        }
        // the borders between the headers drag the columns wider and narrower
        if (t.flags & TableFlags_Resizable)
            for (int c = 1; c < t.columns; ++c)
            {
                const float x = t.area.min.x + t.edges[(std::size_t)c];
                const Rect hit(x - 5.0f, hr.min.y + 6.0f, x + 5.0f, hr.max.y - 6.0f);
                const Id rid = HashInt(0x52534A00 + c, t.id);
                AddItem(rid, hit);
                const ButtonResult b = Behavior(rid, hit, ButtonFlags_PressOnClick);
                const bool active = !inert_ && ctx_.ActiveId() == rid;
                if (b.hovered || active)
                    ctx_.SetMouseCursor(MouseCursor::ResizeEW);
                if (active && in.MouseDown(MouseButton::Left))
                {
                    const float w = t.area.Width();
                    const float lo = t.edges[(std::size_t)c - 1], hi = t.edges[(std::size_t)c + 1];
                    const float nx = std::clamp(in.MousePos().x - t.area.min.x, lo + 48.0f, hi - 48.0f);
                    st.share[(std::size_t)c - 1] = (nx - lo) / w;
                    st.share[(std::size_t)c] = (hi - nx) / w;
                    t.edges[(std::size_t)c] = nx;
                }
                const float hov = Anim(rid, 0, (b.hovered || active) ? 1.0f : 0.0f);
                const float bx = std::round((t.area.min.x + t.edges[(std::size_t)c]) * pixelScale_) / pixelScale_;
                p.Rect(Rect(bx, hr.min.y + 10.0f - 3.0f * hov, bx + (1.0f + hov) / pixelScale_, hr.max.y - 10.0f + 3.0f * hov),
                       Style().Fill(Mix(Ink(0.10f), shown_.accent, hov)));
            }
        p.HLine(hr.min.x + 12.0f, hr.max.x - 12.0f, hr.max.y, Ink(0.10f), 1.0f / pixelScale_);
        t.y += h + 2.0f;
    }

    void Ui::TableEndCell()
    {
        if (!tables_.empty() && tables_.back().cellClip)
        {
            ctx_.PopClipRect();
            tables_.back().cellClip = false;
        }
    }

    void Ui::TableNextRow(int key)
    {
        if (tables_.empty())
            return;
        TableEndCell();
        TableFrame& t = tables_.back();
        TableColumns(t);
        ++t.row;
        t.column = -1;
        // its place: a row with a key glides there (sorted, the rows travel to their new places)
        float top = t.y;
        if (key >= 0)
            top = Anim(HashInt(key, HashInt(0x524F574B, t.id)), 0, t.y, kSmooth);
        t.y += t.rowH;
        t.rowRect = Rect(t.area.min.x, t.area.min.y + top, t.area.max.x, t.area.min.y + top + t.rowH);
        // its band across the table, cut to the frame (the last row's corners are the frame's): every other row's a
        // shade lighter, the one under the pointer lighter still
        const InputState& in = ctx_.Input();
        const bool over = !inert_ && in.MouseValid() && t.rowRect.Contains(in.MousePos()) && ctx_.HoveredWindow() == ctx_.CurrentWindow() && ctx_.ActiveId() == 0;
        const Id hid = key >= 0 ? HashInt(key, HashInt(0x524F5748, t.id)) : HashInt(t.row, HashInt(0x524F5749, t.id));
        const float hover = Anim(hid, 0, over ? 1.0f : 0.0f);
        const float band = ((t.flags & TableFlags_RowBg) && (t.row & 1) ? 0.02f : 0.0f) + 0.035f * hover;
        if (band > 0.002f)
        {
            const float height = ctx_.State<TableState>(t.id).height;
            esia::Painter p = Painter();
            p.PushMask(Rect(t.area.min.x, t.area.min.y, t.area.max.x, t.area.min.y + (height > 0.0f ? height : 1e5f)), m::kRadiusControl);
            p.Rect(t.rowRect, Style().Fill(Ink(band)));
            p.PopMask();
        }
    }

    bool Ui::TableNextColumn()
    {
        if (tables_.empty())
            return false;
        if (tables_.back().row < 0)
            TableNextRow();
        TableEndCell();
        TableFrame& t = tables_.back();
        if (++t.column >= t.columns)
            return false;
        const Rect cell(t.area.min.x + t.edges[(std::size_t)t.column] + 12.0f, t.rowRect.min.y, t.area.min.x + t.edges[(std::size_t)t.column + 1] - 6.0f,
                        t.rowRect.max.y);
        ctx_.PushClipRect(cell.Expanded(2.0f, 0.0f), true);
        t.cellClip = true;
        ctx_.SetCursorPos(Vec2(cell.min.x, cell.min.y + std::round((t.rowH - m::kFont * 1.36f) * 0.5f)));
        return true;
    }

    void Ui::EndTable()
    {
        if (tables_.empty())
            return;
        TableEndCell();
        const TableFrame t = std::move(tables_.back());
        tables_.pop_back();
        // its frame: one control's radius round all of it, its bottom the last row's (whose band fills its corners)
        const Rect frame(t.area.min.x, t.area.min.y, t.area.max.x, t.area.min.y + t.y);
        ctx_.State<TableState>(t.id).height = t.y;
        esia::Painter p = Painter();
        p.Rect(frame, Style().Radius(m::kRadiusControl).Stroke(1.0f / pixelScale_, Ink(0.08f), 0.0f));
        ctx_.SetCursorPos(Vec2(t.lineX, frame.max.y));
        ctx_.ItemSize(Vec2(0.0f, 0.0f));
        rowIndex_ = 0;
        row_ = PendingRow();
    }

    int Ui::TableSortColumn() { return tables_.empty() ? -1 : ctx_.State<TableState>(tables_.back().id).sortColumn; }
    bool Ui::TableSortAscending() { return tables_.empty() || ctx_.State<TableState>(tables_.back().id).ascending; }
    bool Ui::TableSortChanged() { return !tables_.empty() && ctx_.State<TableState>(tables_.back().id).changed; }

    // ------------------------------------------------------------------ plots
    namespace
    {
        enum class PlotKind { Lines, Bars };
    }

    static void PlotCommon(Ui& ui, esia::Context& ctx, Id id, PlotKind kind, const float* values, int count, float min, float max, const Rect& r,
                           float dt, float pixelScale, const Fonts& fonts)
    {
        PlotState& ps = ctx.State<PlotState>(id);
        const double now = ctx.Input().Time();
        const int history = kind == PlotKind::Lines ? kPlotHistory : 0;
        if ((int)ps.last.size() != count)
        {
            ps.last.assign(values, values + count);
            ps.shown.assign((std::size_t)history, count > 0 ? values[0] : 0.0f);
            ps.shown.insert(ps.shown.end(), values, values + count);
            ps.scroll = 0.0f;
        }
        else if (!std::equal(values, values + count, ps.last.begin()))
        {
            // a series that scrolled - its values moved left by a few, new ones came in at the right: the line slides
            // over by as many (gliding every point to its neighbour's height would flatten the peaks as they pass)
            int by = 0;
            if (kind == PlotKind::Lines)
                for (int s = 1; s <= std::min(count / 2, 8) && by == 0; ++s)
                    if (std::equal(values, values + (count - s), ps.last.begin() + s))
                        by = s;
            if (by > 0)
            {
                ps.shown.erase(ps.shown.begin(), ps.shown.begin() + by);
                ps.shown.insert(ps.shown.end(), values + (count - by), values + count);
                ps.scroll = std::min(ps.scroll + (float)by, 3.0f);
                if (ps.lastShift >= 0.0)
                {
                    const float rate = (float)by / std::clamp((float)(now - ps.lastShift), 0.03f, 2.0f);
                    ps.rate = ps.rate > 0.0f ? Mix(ps.rate, rate, 0.5f) : rate;
                }
                ps.lastShift = now;
            }
            ps.last.assign(values, values + count);
        }
        // the slide: one value in the time one has been taking to come (the line moves on evenly), faster when behind
        if (ps.scroll > 0.0f)
            ps.scroll = std::max(ps.scroll - (ps.rate > 0.0f ? ps.rate : 4.0f) * std::max(ps.scroll, 1.0f) * dt, 0.0f);
        // the values as drawn glide to the new ones (a quarter of the way in about 30 ms)
        const float k = 1.0f - std::exp(-dt / 0.09f);
        float lo = FLT_MAX, hi = -FLT_MAX;
        for (int i = 0; i < count; ++i)
        {
            float& v = ps.shown[(std::size_t)(history + i)];
            v += (values[i] - v) * k;
            lo = std::min(lo, values[i]);
            hi = std::max(hi, values[i]);
        }
        if (count == 0)
            lo = 0.0f, hi = 1.0f;
        if (min != FLT_MAX)
            lo = min;
        if (max != FLT_MAX)
            hi = max;
        if (min == FLT_MAX || max == FLT_MAX)
        {
            const float pad = std::max((hi - lo) * 0.1f, 1e-3f);
            if (min == FLT_MAX)
                lo -= kind == PlotKind::Bars ? 0.0f : pad;
            if (max == FLT_MAX)
                hi += pad;
        }
        if (kind == PlotKind::Bars && min == FLT_MAX)
            lo = std::min(lo, 0.0f);
        if (!ps.made)
        {
            ps.lo.Snap(lo);
            ps.hi.Snap(hi);
            ps.made = true;
        }
        ps.lo.target = lo;
        ps.hi.target = hi;
        ps.lo.Step(dt, kSmooth);
        ps.hi.Step(dt, kSmooth);
        const float L = ps.lo.value, H = std::max(ps.hi.value, ps.lo.value + 1e-6f);
        esia::Painter p = ui.Painter();
        const Theme& th = ui.T();
        p.Rect(r, Style().Radius(metrics::kRadiusPanel).Fill(ui.Ink(0.03f)).Stroke(1.0f / pixelScale, ui.Ink(0.08f), 0.0f));
        const Rect in(r.min.x + 14.0f, r.min.y + 16.0f, r.max.x - 14.0f, r.max.y - 14.0f);
        // three faint lines across, the range at the left
        for (int i = 0; i <= 2; ++i)
        {
            const float y = std::round((in.min.y + in.Height() * (float)i / 2.0f) * pixelScale) / pixelScale;
            p.HLine(in.min.x, in.max.x, y, ui.Ink(0.05f), 1.0f / pixelScale);
        }
        char top[32], bottom[32];
        std::snprintf(top, sizeof(top), "%.2f", (double)H);
        std::snprintf(bottom, sizeof(bottom), "%.2f", (double)L);
        ui.Label(p, in.min.x + 2.0f, in.min.y - 7.0f, {fonts.mono, 11.0f}, ui.Ink(0.36f), top);
        ui.Label(p, in.min.x + 2.0f, in.max.y - 7.0f, {fonts.mono, 11.0f}, ui.Ink(0.36f), bottom);
        if (count <= 0)
            return;
        const auto yOf = [&](float v) { return in.max.y - (v - L) / (H - L) * in.Height(); };
        // the pointer reads the value under it: a guide, a dot, the number (gliding from sample to sample)
        const InputState& input = ctx.Input();
        const bool over = input.MouseValid() && r.Contains(input.MousePos()) && ctx.HoveredWindow() == ctx.CurrentWindow();
        int hoverIndex = -1;
        if (over)
        {
            const float u = Clamp01((input.MousePos().x - in.min.x) / std::max(in.Width(), 1.0f));
            hoverIndex = kind == PlotKind::Lines ? (int)std::lround(u * (float)(count - 1)) : std::min((int)(u * (float)count), count - 1);
        }
        ps.hoverShown.target = over ? 1.0f : 0.0f;
        ps.hoverShown.Step(dt, kSnappy);
        if (kind == PlotKind::Lines)
        {
            // the points `scroll` values right of their places (the older ones off at the left), the line cut to the plot
            const float step = in.Width() / (float)std::max(count - 1, 1);
            const auto xOf = [&](float j) { return count > 1 ? in.min.x + step * (j - (float)history + ps.scroll) : in.Center().x; };
            static thread_local std::vector<Vec2> pts;
            pts.clear();
            for (int j = 0; j < (int)ps.shown.size(); ++j)
            {
                const float x = xOf((float)j);
                if (x >= in.min.x - step && x <= in.max.x + step)
                    pts.push_back(Vec2(x, yOf(ps.shown[(std::size_t)j])));
            }
            p.PushClip(Rect(in.min.x, r.min.y, in.max.x, r.max.y));
            p.Area(pts.data(), (int)pts.size(), in.max.y, Paint::Linear(th.accent.Fade(0.30f), th.accent.Fade(0.0f), 90.0f), PolylineFlags_Smooth);
            p.Polyline(pts.data(), (int)pts.size(), 2.0f, Style().Fill(th.accent).Glow(th.accent, 6.0f, 0.35f), PolylineFlags_Smooth);
            p.PopClip();
            // the value under the pointer: the entry nearest it as the line is now
            if (hoverIndex >= 0)
            {
                const float under = (input.MousePos().x - in.min.x) / step + (float)history - ps.scroll;
                const float entry = std::clamp(std::round(under), (float)history, (float)(history + count - 1));
                if (ps.hoverShown.value < 0.05f)
                    ps.hoverX.Snap(entry);
                ps.hoverX.target = entry;
            }
            ps.hoverX.Step(dt, Motion{0.12f, 1.0f});
            const float hs = Clamp01(ps.hoverShown.value);
            if (hs > 0.004f)
            {
                const float fj = std::clamp(ps.hoverX.value, 0.0f, (float)ps.shown.size() - 1.0f);
                const int j0 = (int)std::floor(fj), j1 = std::min(j0 + 1, (int)ps.shown.size() - 1);
                const float v = Mix(ps.shown[(std::size_t)j0], ps.shown[(std::size_t)j1], fj - (float)j0);
                const Vec2 at(xOf(fj), yOf(v));
                const float a0 = p.Alpha();
                p.SetAlpha(a0 * hs);
                p.Rect(Rect(at.x, in.min.y, at.x + 1.0f / pixelScale, in.max.y), Style().Fill(ui.Ink(0.18f)));
                p.Circle(at, 5.0f, Style().Fill(th.accent).Stroke(2.0f, th.raised.WithAlpha(1.0f), 1.0f).Glow(th.accent, 8.0f, 0.5f));
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%.2f", (double)v);
                const text::FontRef bf{fonts.monoSemibold, 12.0f};
                const float bw = ui.Measure(bf, buf).x + 14.0f;
                const Rect pill = Rect::FromCenter(Vec2(std::clamp(at.x, in.min.x + bw * 0.5f, in.max.x - bw * 0.5f), std::max(at.y - 22.0f, r.min.y + 12.0f)), Vec2(bw, 20.0f));
                p.Rect(pill, Style().Radius(metrics::kRadiusInner).Fill(th.accent).Shadow(Color::Black(0.25f), 8.0f, Vec2(0, 2)));
                ui.Label(p, pill.Center().x, pill.Center().y, bf, th.accentInk, buf, 0.5f);
                p.SetAlpha(a0);
            }
        }
        else
        {
            const float slot = in.Width() / (float)count, gap = std::min(6.0f, slot * 0.25f);
            for (int i = 0; i < count; ++i)
            {
                const float x0 = in.min.x + slot * (float)i + gap * 0.5f, x1 = x0 + slot - gap;
                const float y = yOf(ps.shown[(std::size_t)i]), base = yOf(std::max(L, 0.0f));
                const float lit = ui.Anim(HashInt(i, id), 0, i == hoverIndex ? 1.0f : 0.0f);
                const Rect bar(x0, std::min(y, base - 1.0f), x1, base);
                p.Rect(bar, Style().Radius(std::min(4.0f, (x1 - x0) * 0.5f), std::min(4.0f, (x1 - x0) * 0.5f), 0.0f, 0.0f)
                                .Fill(Paint::Linear(Mix(th.accent, th.accent2, 0.35f * lit), th.accent.Fade(0.55f + 0.25f * lit), 90.0f)));
            }
            const float hs = Clamp01(ps.hoverShown.value);
            if (hoverIndex >= 0 && hs > 0.004f)
            {
                const float x = in.min.x + slot * ((float)hoverIndex + 0.5f);
                const float v = ps.shown[(std::size_t)hoverIndex];
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%.2f", (double)v);
                const text::FontRef bf{fonts.monoSemibold, 12.0f};
                const float bw = ui.Measure(bf, buf).x + 14.0f;
                const Rect pill = Rect::FromCenter(Vec2(std::clamp(x, in.min.x + bw * 0.5f, in.max.x - bw * 0.5f), std::max(yOf(v) - 16.0f, r.min.y + 12.0f)), Vec2(bw, 20.0f));
                const float a0 = p.Alpha();
                p.SetAlpha(a0 * hs);
                p.Rect(pill, Style().Radius(metrics::kRadiusInner).Fill(th.accent).Shadow(Color::Black(0.25f), 8.0f, Vec2(0, 2)));
                ui.Label(p, pill.Center().x, pill.Center().y, bf, th.accentInk, buf, 0.5f);
                p.SetAlpha(a0);
            }
        }
    }

    void Ui::PlotLines(const char* label, const float* values, int count, float min, float max, Vec2 size)
    {
        const std::string_view text = Visible(label);
        if (!text.empty())
            TextUnformatted(text, Tone::Secondary, m::kFontSmall);
        Rect r = ItemRect(Vec2(size.x > 0.0f ? size.x : ContentWidth(), size.y > 0.0f ? size.y : 150.0f));
        if (size.x <= 0.0f)
            r.min.x -= bleed_;
        ctx_.ItemAdd(0, r);
        PlotCommon(*this, ctx_, IdOf(label), PlotKind::Lines, values, count, min, max, r, dt_, pixelScale_, fonts_);
    }

    void Ui::PlotHistogram(const char* label, const float* values, int count, float min, float max, Vec2 size)
    {
        const std::string_view text = Visible(label);
        if (!text.empty())
            TextUnformatted(text, Tone::Secondary, m::kFontSmall);
        Rect r = ItemRect(Vec2(size.x > 0.0f ? size.x : ContentWidth(), size.y > 0.0f ? size.y : 150.0f));
        if (size.x <= 0.0f)
            r.min.x -= bleed_;
        ctx_.ItemAdd(0, r);
        PlotCommon(*this, ctx_, IdOf(label), PlotKind::Bars, values, count, min, max, r, dt_, pixelScale_, fonts_);
    }
}
