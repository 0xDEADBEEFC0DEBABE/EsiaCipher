// Cipher - buttons, toggles, selectables, progress and sliders (cipher.hpp). Every one of them fills the control place
// of a pending element row (Ui::Row) when there is one.
#include "cipher.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace cipher
{
    using namespace esia;
    namespace m = metrics;

    namespace
    {
        // a slider's glass drop while it is dragged: the trailing part of the lens
        struct SliderGlass
        {
            Spring trail;
            bool placed = false;
        };

        // colors summed by weight with their alpha (a fill fading out does not tint what fades in)
        struct Premul
        {
            float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
            void Add(Color c, float w)
            {
                r += c.r * c.a * w;
                g += c.g * c.a * w;
                b += c.b * c.a * w;
                a += c.a * w;
            }
            Color Get() const { return a > 1e-5f ? Color(r / a, g / a, b / a, a) : Color(r, g, b, 0.0f); }
        };

        // A check mark drawn along its stroke as `drawn` goes 0 -> 1: the short stroke down, then the long one up.
        void CheckMark(esia::Painter& p, Vec2 c, float size, Color color, float drawn, float thickness)
        {
            if (drawn <= 0.04f || !color.IsVisible())
                return;   // too little of it to be a stroke: no dot is left behind
            const float k = size / 16.0f;
            const Vec2 a = c + Vec2(-4.6f, 0.2f) * k, b = c + Vec2(-1.4f, 3.4f) * k, e = c + Vec2(4.8f, -3.4f) * k;
            const float l1 = Length(b - a), l2 = Length(e - b), at = Clamp01(drawn) * (l1 + l2);
            Vec2 pts[3] = {a, b, e};
            int n = 3;
            if (at <= l1)
            {
                pts[1] = a + (b - a) * (at / l1);
                n = 2;
            }
            else
                pts[2] = b + (e - b) * ((at - l1) / l2);
            p.Polyline(pts, n, thickness, Style().Fill(color));
        }
    }

    // ------------------------------------------------------------------ colors of the buttons
    Color Ui::ButtonFill(ButtonKind kind, float hover, float press) const
    {
        switch (kind)
        {
        case ButtonKind::Primary: return Mix(shown_.accent, shown_.canvas, 0.12f * hover + 0.10f * press).WithAlpha(1.0f);
        case ButtonKind::Secondary: return Ink(0.045f + 0.045f * hover + 0.05f * press);
        case ButtonKind::Ghost: return Ink(0.065f * hover + 0.05f * press);
        case ButtonKind::Danger: return shown_.danger.Fade(0.13f + 0.08f * hover + 0.06f * press);
        case ButtonKind::Outline: return Ink(0.045f * hover + 0.04f * press);
        }
        return Ink(0.06f);
    }

    Color Ui::ButtonInk(ButtonKind kind) const
    {
        switch (kind)
        {
        case ButtonKind::Primary: return shown_.accentInk;
        case ButtonKind::Danger: return shown_.danger;
        default: return Ink(0.92f);
        }
    }

    void Ui::FocusRing(esia::Painter& p, const Rect& r, float radius, Id id)
    {
        const float f = Anim(id, 7, focusVisible_ && !inert_ && ctx_.KeyboardFocusId() == id ? 1.0f : 0.0f);
        if (f > 0.004f)
            p.Rect(r.Expanded(3.0f), Style().Radius(radius + 3.0f).Stroke(1.5f, shown_.accent.Fade(0.85f * f), 0.0f));
    }

    // ------------------------------------------------------------------ buttons
    bool Ui::Button(const char* label, ButtonKind kind, Vec2 size, Icon icon)
    {
        const std::string_view text = Visible(label);
        const text::FontRef f{fonts_.textSemibold, m::kFont};
        const Id id = IdOf(label);
        // its width follows its label: a new label slides in as the button grows or shrinks to it
        const float tw = Anim(HashInt(0x4254574B, id), 0, text.empty() ? 0.0f : Measure(f, text).x, kSmooth);
        const float iw = icon != Icon::None ? m::kIcon + (text.empty() ? 0.0f : 8.0f) : 0.0f;
        const float natural = std::ceil(tw + iw + (text.empty() ? 20.0f : 34.0f));
        Rect r;
        const bool inRow = TakeRow(size.x > 0.0f ? size.x : natural, r);
        if (!inRow)
        {
            float w = size.x > 0.0f ? size.x : (size.x < 0.0f ? ContentWidth() + size.x : natural);
            if (nextWidth_ > 0.0f)
                w = nextWidth_;
            r = ItemRect(Vec2(w, size.y > 0.0f ? size.y : m::kRow));
        }
        nextWidth_ = 0.0f;
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
        // its kind: a weight per kind, so a button that changes kind blends into the new one
        Premul fill, ink;
        float outline = 0.0f;
        for (int k = 0; k < 5; ++k)
        {
            const float w = Clamp01(Anim(id, 2 + k, (int)kind == k ? 1.0f : 0.0f, kSmooth));
            if (w <= 0.0f)
                continue;
            fill.Add(ButtonFill((ButtonKind)k, hover, press), w);
            ink.Add(ButtonInk((ButtonKind)k), w);
            if ((ButtonKind)k == ButtonKind::Secondary || (ButtonKind)k == ButtonKind::Outline)
                outline += w;
        }
        esia::Painter p = Painter();
        const float radius = inRow ? m::kRadiusInner : std::min(m::kRadiusControl, r.Height() * 0.5f);
        p.PushScale(r.Center(), 1.0f - 0.03f * press);
        Style s = Style().Radius(radius).Fill(fill.Get());
        if (outline > 0.004f)
            s.Stroke(1.0f / pixelScale_, Ink((0.13f + 0.10f * hover) * outline), 0.0f);
        p.Rect(r, s);
        const Color inkNow = ink.Get();
        float x = r.Center().x - (tw + iw) * 0.5f;
        if (icon != Icon::None)
        {
            DrawIcon(p, icon, Vec2(x + m::kIcon * 0.5f, r.Center().y), m::kIcon, inkNow);
            x += iw;
        }
        if (!text.empty())
            MorphLabel(p, HashInt(0x42544E4C, id), x + tw * 0.5f, r.Center().y, f, inkNow, text, 0.5f);
        p.PopScale();
        FocusRing(p, r, radius, id);
        return b.pressed && !disabled;
    }

    bool Ui::SmallButton(const char* label, ButtonKind kind) { return Button(label, kind, Vec2(0.0f, m::kRowSmall)); }

    bool Ui::IconButton(const char* idLabel, Icon icon, const char* tooltip, float size, ButtonKind kind)
    {
        const Id id = IdOf(idLabel);
        Rect r;
        const bool inRow = TakeRow(m::kControl, r);
        if (!inRow)
            r = ItemRect(Vec2(size, size));
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
        esia::Painter p = Painter();
        const float radius = inRow ? m::kRadiusInner : std::min(m::kRadiusControl, r.Height() * 0.5f);
        p.PushScale(r.Center(), 1.0f - 0.05f * press);
        const Color fill = ButtonFill(kind, hover, press);
        if (fill.a > 0.002f)
            p.Rect(r, Style().Radius(radius).Fill(fill));
        const Color ink = kind == ButtonKind::Primary ? shown_.accentInk : (kind == ButtonKind::Danger ? shown_.danger : Ink(0.62f + 0.32f * hover));
        DrawIcon(p, icon, r.Center(), m::kIcon, ink);
        p.PopScale();
        FocusRing(p, r, radius, id);
        if (tooltip)
            SetItemTooltip("%s", tooltip);
        return b.pressed && !disabled;
    }

    bool Ui::InvisibleButton(const char* idLabel, Vec2 size)
    {
        const Id id = IdOf(idLabel);
        const Rect r = ItemRect(size);
        lastId_ = id;
        if (!AddItem(id, r))
        {
            lastHovered_ = false;
            return false;
        }
        const ButtonResult b = Behavior(id, r);
        lastHovered_ = b.hovered;
        return b.pressed && !disabled_.back();
    }

    // ------------------------------------------------------------------ toggles
    bool Ui::Checkbox(const char* label, bool* v)
    {
        const std::string_view text = Visible(label);
        const text::FontRef f{fonts_.text, m::kFont};
        const float box = 18.0f;
        const Id id = IdOf(label);
        Rect r, hit;
        Vec2 c;
        const bool inRow = TakeRow(box, r);
        if (inRow)
        {
            // as far in from the row's right edge as from its top
            hit = row_.row;
            c = Vec2(r.max.x - box * 0.5f - (m::kControl - box) * 0.5f, r.Center().y);
        }
        else
        {
            const float w = box + (text.empty() ? 0.0f : 10.0f + Measure(f, text).x);
            hit = r = ItemRect(Vec2(w, m::kCheckRow + 2.0f));
            c = Vec2(r.min.x + box * 0.5f, r.Center().y);
        }
        lastId_ = id;
        const bool disabled = disabled_.back();
        if (!AddItem(id, hit, ItemFlags_Focusable | (disabled ? ItemFlags_Disabled : 0u)))
        {
            lastHovered_ = false;
            return false;
        }
        const ButtonResult b = Behavior(id, hit);
        lastHovered_ = b.hovered;
        bool changed = false;
        if (b.pressed && !disabled)
        {
            *v = !*v;
            changed = true;
        }
        if (b.hovered && !disabled)
            ctx_.SetMouseCursor(MouseCursor::Hand);
        const float on = Anim(id, 0, *v ? 1.0f : 0.0f, kSmooth);
        const float hover = Anim(id, 1, b.hovered ? 1.0f : 0.0f);
        const float press = Anim(id, 2, b.held ? 1.0f : 0.0f);
        const float drawn = Anim(id, 3, *v ? 1.0f : 0.0f, Motion{0.36f, 1.0f});
        const Rect bx = Rect::FromCenter(c, Vec2(box, box));
        esia::Painter p = Painter();
        p.PushScale(c, 1.0f - 0.10f * press);
        p.Rect(bx, Style().Radius(5.0f).Fill(Ink(0.04f * hover)).Stroke(1.25f, Ink((0.34f + 0.22f * hover) * (1.0f - Clamp01(on))), 0.0f));
        if (on > 0.002f)
        {
            // the accent fills it from its middle
            const float k = 0.45f + 0.55f * Clamp01(on);
            p.Rect(Rect::FromCenter(c, Vec2(box, box) * k), Style().Radius(5.0f * k).Fill(shown_.accent.Fade(Clamp01(on))));
        }
        CheckMark(p, c, box, shown_.accentInk.Fade(Clamp01(on)), drawn, 2.0f);   // it leaves with the box's fill
        p.PopScale();
        if (!text.empty() && !inRow)
            Label(p, bx.max.x + 10.0f, c.y, f, Ink(0.90f), text);
        FocusRing(p, bx, 5.0f, id);
        return changed;
    }

    bool Ui::CheckboxFlags(const char* label, unsigned* flags, unsigned value)
    {
        bool on = (*flags & value) == value;
        if (!Checkbox(label, &on))
            return false;
        *flags = on ? (*flags | value) : (*flags & ~value);
        return true;
    }

    bool Ui::RadioButton(const char* label, bool active)
    {
        const std::string_view text = Visible(label);
        const text::FontRef f{fonts_.text, m::kFont};
        const float d = 18.0f;
        const float w = d + (text.empty() ? 0.0f : 10.0f + Measure(f, text).x);
        const Id id = IdOf(label);
        const Rect r = ItemRect(Vec2(w, m::kCheckRow + 2.0f));
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
        const float on = Anim(id, 0, active ? 1.0f : 0.0f, kSmooth);
        const float dot = Anim(id, 3, active ? 1.0f : 0.0f, kBouncy);
        const float hover = Anim(id, 1, b.hovered ? 1.0f : 0.0f);
        const float press = Anim(id, 2, b.held ? 1.0f : 0.0f);
        const Vec2 c(r.min.x + d * 0.5f, r.Center().y);
        esia::Painter p = Painter();
        p.PushScale(c, 1.0f - 0.10f * press);
        p.Circle(c, d * 0.5f, Style().Fill(Ink(0.04f * hover)).Stroke(1.25f, Ink((0.34f + 0.22f * hover) * (1.0f - Clamp01(on))), 0.0f));
        if (on > 0.002f)
            p.Ring(c, d * 0.5f - 1.0f, 2.0f * Clamp01(on), Style().Fill(shown_.accent));
        if (dot > 0.002f)
            p.Circle(c, 4.0f * std::max(dot, 0.0f), Style().Fill(shown_.accent));
        p.PopScale();
        if (!text.empty())
            Label(p, c.x + d * 0.5f + 10.0f, r.Center().y, f, Ink(0.90f), text);
        FocusRing(p, Rect::FromCenter(c, Vec2(d, d)), d * 0.5f, id);
        return b.pressed && !disabled;
    }

    bool Ui::RadioButton(const char* label, int* v, int value)
    {
        if (!RadioButton(label, *v == value))
            return false;
        *v = value;
        return true;
    }

    bool Ui::Switch(const char* label, bool* v)
    {
        const std::string_view text = Visible(label);
        const text::FontRef f{fonts_.text, m::kFont};
        const Id id = IdOf(label);
        const Vec2 trackSize(38.0f, 22.0f);
        Rect hit, track;
        Rect ctrl;
        if (!row_.on && !text.empty())
            Row(label);   // a labeled switch is an element row: its label and edges line up with the others
        const bool inRow = TakeRow(trackSize.x, ctrl);
        if (inRow)
        {
            // as far in from the row's right edge as from its top
            hit = row_.row;
            track = Rect::FromSize(Vec2(ctrl.max.x - trackSize.x - (m::kControl - trackSize.y) * 0.5f, ctrl.Center().y - trackSize.y * 0.5f), trackSize);
        }
        else
        {
            hit = ItemRect(Vec2(ContentWidth(), m::kRow));
            track = Rect::FromSize(Vec2(hit.max.x - trackSize.x, hit.Center().y - trackSize.y * 0.5f), trackSize);
        }
        lastId_ = id;
        const bool disabled = disabled_.back();
        if (!AddItem(id, hit, ItemFlags_Focusable | (disabled ? ItemFlags_Disabled : 0u)))
        {
            lastHovered_ = false;
            return false;
        }
        const ButtonResult b = Behavior(id, hit);
        lastHovered_ = b.hovered;
        bool changed = false;
        if (b.pressed && !disabled)
        {
            *v = !*v;
            changed = true;
        }
        if (b.hovered && !disabled)
            ctx_.SetMouseCursor(MouseCursor::Hand);
        const float on = Anim(id, 0, *v ? 1.0f : 0.0f, kSmooth);
        const float hover = Anim(id, 1, b.hovered ? 1.0f : 0.0f);
        const float press = Anim(id, 2, b.held ? 1.0f : 0.0f);
        esia::Painter p = Painter();
        const float k = Clamp01(on);
        p.Capsule(track, Style().Fill(Mix(Ink(0.10f + 0.05f * hover), shown_.accent.WithAlpha(1.0f), k)).Stroke(1.0f / pixelScale_, Ink(0.14f * (1.0f - k)), 0.0f));
        // the knob: 16 across, stretching while pressed, sliding with the spring
        const float kw = 16.0f + 4.0f * press;
        const float x0 = track.min.x + 3.0f, x1 = track.max.x - 3.0f - kw;
        const float kx = Mix(x0, x1, on);
        const Color knob = shown_.accent.Luminance() > 0.6f ? Mix(Color::White(), shown_.accentInk, k) : Color::White();
        p.Capsule(Rect(kx, track.min.y + 3.0f, kx + kw, track.max.y - 3.0f), Style().Fill(Mix(Ink(0.7f).WithAlpha(1.0f), knob, k)).Shadow(Color::Black(0.25f), 3.0f, Vec2(0.0f, 1.0f)));
        if (!text.empty() && !inRow)
            Label(p, hit.min.x, hit.Center().y, f, Ink(0.90f), text, 0.0f, track.min.x - hit.min.x - 12.0f);
        FocusRing(p, track, trackSize.y * 0.5f, id);
        return changed;
    }

    bool Ui::Selectable(const char* label, bool selected, Vec2 size, Icon icon)
    {
        const std::string_view text = Visible(label);
        const text::FontRef f{fonts_.text, m::kFont};
        const float w = size.x > 0.0f ? size.x : ContentWidth();
        const float h = size.y > 0.0f ? size.y : 34.0f;
        const Id id = IdOf(label);
        const Rect r = ItemRect(Vec2(w, h));
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
        const float sel = Anim(id, 1, selected ? 1.0f : 0.0f, kSmooth);
        const float press = Anim(id, 2, b.held ? 1.0f : 0.0f);
        esia::Painter p = Painter();
        // its surface reaches back to the content's edge; its icon and label stand on the column
        const Rect sr(r.min.x - bleed_, r.min.y, r.max.x, r.max.y);
        const Color fill = Ink(0.045f * hover + 0.07f * sel + 0.03f * press);
        if (fill.a > 0.002f)
            p.Rect(sr, Style().Radius(std::min(m::kRadiusControl, h * 0.5f)).Fill(fill));
        if (sel > 0.004f)
            p.Capsule(Rect(sr.min.x + 4.0f, sr.Center().y - 8.0f * sel, sr.min.x + 6.5f, sr.Center().y + 8.0f * sel), Style().Fill(shown_.accent));
        float x = sr.min.x + 12.0f;
        if (icon != Icon::None)
        {
            DrawIcon(p, icon, Vec2(x + m::kIcon * 0.5f, r.Center().y), m::kIcon, Mix(Ink(0.6f + 0.3f * hover), shown_.accent, Clamp01(sel)));
            x += m::kIcon + 10.0f;
        }
        Label(p, x, r.Center().y, f, Ink(0.80f + 0.15f * std::max(sel, hover)), text, 0.0f, r.max.x - x - 10.0f);
        FocusRing(p, r, m::kRadiusControl, id);
        return b.pressed && !disabled;
    }

    // ------------------------------------------------------------------ progress
    void Ui::ProgressBar(float fraction, Vec2 size, const char* overlay)
    {
        Rect row;
        const bool inRow = TakeRow(size.x > 0.0f ? size.x : 0.0f, row);
        if (!inRow)
        {
            const float w = size.x > 0.0f ? size.x : ContentWidth();
            row = ItemRect(Vec2(w, 24.0f));
            ctx_.ItemAdd(0, row);
        }
        const float h = size.y > 0.0f ? size.y : 4.0f;
        const Id id = SeqId("##progress");
        esia::Painter p = Painter();
        char buf[32];
        const char* note = overlay;
        if (!note && fraction >= 0.0f)
        {
            std::snprintf(buf, sizeof(buf), "%3d%%", (int)std::lround(Clamp01(fraction) * 100.0f));
            note = buf;
        }
        const text::FontRef f{fonts_.mono, 13.0f};
        const float noteW = note ? 48.0f : 0.0f;
        const Rect track(row.min.x, row.Center().y - h * 0.5f, row.max.x - noteW - (inRow ? 7.0f : 0.0f), row.Center().y + h * 0.5f);
        p.Capsule(track, Style().Fill(Ink(0.09f)));
        if (fraction >= 0.0f)
        {
            const float shown = Anim(id, 0, Clamp01(fraction), kSmooth);
            const float fw = std::max(track.Width() * Clamp01(shown), h);
            if (shown > 0.001f)
                p.Capsule(Rect(track.min.x, track.min.y, track.min.x + fw, track.max.y),
                          Style().Fill(Paint::Linear(shown_.accent, Mix(shown_.accent, shown_.accent2, 0.7f), 0)).Glow(shown_.accent, 6.0f, 0.35f));
        }
        else
        {
            // indeterminate: a third of the track sweeping across, easing at both ends
            const float t = std::fmod((float)time_ * 0.75f, 1.0f);
            const float a = EaseInOutCubic(t) * 1.4f - 0.4f, b = a + 0.38f;
            const float x0 = track.min.x + track.Width() * Clamp01(a), x1 = track.min.x + track.Width() * Clamp01(b);
            if (x1 - x0 > 1.0f)
            {
                p.PushClip(track.Expanded(0.0f, 4.0f));
                p.Capsule(Rect(x0, track.min.y, std::max(x1, x0 + h), track.max.y), Style().Fill(shown_.accent));
                p.PopClip();
            }
        }
        if (note)
            MorphLabel(p, HashInt(1, id), row.max.x - (inRow ? 7.0f : 0.0f), row.Center().y, f, Ink(0.62f), note, 1.0f);
    }

    void Ui::Spinner(float size, Color color)
    {
        const Rect r = ItemRect(Vec2(size, size));
        ctx_.ItemAdd(0, r);
        esia::Painter p = Painter();
        const Color c = color.a > 0.0f ? color : shown_.accent;
        const float t = (float)time_;
        const float rad = size * 0.5f - 1.5f;
        p.Ring(r.Center(), rad, 2.0f, Style().Fill(c.Fade(0.14f)));
        const float sweep = 1.9f + 1.4f * std::sin(t * 2.6f);
        p.Arc(r.Center(), rad, 2.0f, t * 5.2f, sweep, Style().Fill(c));
    }

    // ------------------------------------------------------------------ sliders
    bool Ui::SliderFloat(const char* label, float* v, float min, float max, const char* format)
    {
        const std::string_view text = Visible(label);
        const Id id = IdOf(label);
        const float valueW = 64.0f;
        Rect area, value;
        Rect ctrl;
        if (!row_.on && !text.empty())
            Row(label);
        const bool inRow = TakeRow(0.0f, ctrl);
        Rect row;
        if (inRow)
        {
            row = row_.row;
            value = Rect(ctrl.max.x - valueW, ctrl.min.y, ctrl.max.x, ctrl.max.y);
            area = Rect(ctrl.min.x, ctrl.min.y, value.min.x - 12.0f, ctrl.max.y);
        }
        else
        {
            row = ItemRect(Vec2(ContentWidth(), m::kRow));
            const float col = text.empty() ? 0.0f : LabelColumn();
            value = Rect(row.max.x - valueW, row.Center().y - m::kControl * 0.5f, row.max.x, row.Center().y + m::kControl * 0.5f);
            area = Rect(row.min.x + col, row.min.y, value.min.x - 12.0f, row.max.y);
        }
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
        const float x0 = area.min.x + 8.0f, x1 = area.max.x - 8.0f;
        const float before = *v;
        if (active && in.MouseDown(MouseButton::Left) && !disabled)
        {
            const float t = Clamp01((in.MousePos().x - x0) / std::max(x1 - x0, 1.0f));
            *v = min + (max - min) * t;
        }
        if (!inert_ && ctx_.KeyboardFocusId() == id && !disabled)
        {
            const float step = (max - min) / ((in.Mods() & Mod_Shift) ? 10.0f : 100.0f);
            if (ctx_.KeyPressed(Key::Left, id) || ctx_.KeyPressed(Key::Down, id))
                *v = std::max(min, *v - step);
            if (ctx_.KeyPressed(Key::Right, id) || ctx_.KeyPressed(Key::Up, id))
                *v = std::min(max, *v + step);
        }
        const bool changed = *v != before;
        if ((b.hovered || active) && !disabled)
            ctx_.SetMouseCursor(MouseCursor::Hand);
        const float t = Clamp01((*v - min) / std::max(max - min, 1e-6f));
        const float shownT = Anim(id, 0, t, Motion{0.12f, 1.0f});
        const float hover = Anim(id, 1, (b.hovered || active) ? 1.0f : 0.0f);
        const float drag = Anim(id, 2, active ? 1.0f : 0.0f, kBouncy);
        esia::Painter p = Painter();
        if (!inRow && !text.empty())
            Label(p, row.min.x, row.Center().y, {fonts_.text, m::kFont}, Ink(0.80f), text, 0.0f, LabelColumn() - 12.0f);
        const float cy = area.Center().y;
        // a ruler: ticks every tenth under the track, the track, the part up to the knob in the accent
        for (int i = 0; i <= 10; ++i)
        {
            const float x = std::round((x0 + (x1 - x0) * (float)i / 10.0f) * pixelScale_) / pixelScale_;
            const float th = (i % 5 == 0) ? 5.0f : 3.0f;
            p.Rect(Rect(x, cy + 6.0f, x + 1.0f / pixelScale_, cy + 6.0f + th), Style().Fill(Ink(0.14f)));
        }
        const float track = 2.0f + 1.0f * hover;
        p.Capsule(Rect(x0, cy - track * 0.5f, x1, cy + track * 0.5f), Style().Fill(Ink(0.14f)));
        const float kx = x0 + (x1 - x0) * shownT;
        p.Capsule(Rect(x0, cy - track * 0.5f, std::max(kx, x0 + track), cy + track * 0.5f), Style().Fill(shown_.accent));
        // at rest the knob is a ring of the accent; dragged, it turns into a drop of liquid glass (below)
        const float glass = Clamp01(drag * 1.5f);
        if (glass < 0.999f)
        {
            const float a0 = p.Alpha();
            p.SetAlpha(a0 * (1.0f - glass));
            const float kr = 8.0f + 1.5f * hover;
            p.Circle(Vec2(kx, cy), kr, Style().Fill(shown_.raised.WithAlpha(1.0f)).Stroke(2.0f, shown_.accent, 0.0f).Shadow(Color::Black(0.3f), 6.0f, Vec2(0, 2)));
            p.SetAlpha(a0);
        }
        // the trail's place along the track (from its start): a window dragged carries it along
        SliderGlass& sg = ctx_.State<SliderGlass>(id);
        if (!sg.placed || drag <= 0.003f)
        {
            sg.trail.Snap(kx - x0);
            sg.placed = true;
        }
        sg.trail.target = kx - x0;
        sg.trail.Step(dt_, Motion{0.24f, 0.5f});
        if (drag > 0.003f)
        {
            // the drop: it swells past its size and settles (the bouncy spring), magnifies and bends the track under it,
            // and a second drop trailing on a softer spring merges into it - it stretches along its way like liquid and
            // gathers round again when it stops
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
            p.Merge(Rect::FromCenter(Vec2(kx, cy), Vec2(R * 2.0f, R * 2.0f)), Rect::FromCenter(Vec2(x0 + sg.trail.value, cy), Vec2(tr * 2.0f, tr * 2.0f)), R, R * 1.2f,
                    Style().Glass(g).Shadow(Color::Black(0.20f), 10.0f, Vec2(0.0f, 3.0f)));
            p.SetAlpha(a0);
        }
        // the value, monospaced, in its box: its digits roll as it changes
        char buf[64];
        std::snprintf(buf, sizeof(buf), format, (double)*v);
        p.Rect(value, Style().Radius(m::kRadiusInner).Fill(Ink(0.035f + 0.03f * hover)).Stroke(1.0f / pixelScale_, Ink(0.11f + 0.08f * Clamp01(drag)), 0.0f));
        MorphLabel(p, HashInt(0x534C56, id), value.Center().x, value.Center().y, {fonts_.mono, m::kFontMono}, Mix(Ink(0.86f), shown_.accent, Clamp01(drag)), buf, 0.5f,
                   valueW - 8.0f);
        // while dragged, the value in a bubble over the drop
        if (drag > 0.01f)
        {
            const text::FontRef bf{fonts_.monoSemibold, 12.5f};
            const float bw = Measure(bf, buf).x + 16.0f;
            const float lift = 8.0f + 7.0f * drag;
            const Rect bubble = Rect::FromCenter(Vec2(kx, cy - lift - 18.0f), Vec2(bw, 22.0f));
            p.PushScale(Vec2(kx, cy - lift), 0.6f + 0.4f * Clamp01(drag));
            const float a0 = p.Alpha();
            p.SetAlpha(a0 * Clamp01(drag));
            p.Rect(bubble, Style().Radius(m::kRadiusInner).Fill(shown_.accent).Shadow(Color::Black(0.25f), 10.0f, Vec2(0, 3)));
            MorphLabel(p, HashInt(0x534C42, id), bubble.Center().x, bubble.Center().y, bf, shown_.accentInk, buf, 0.5f);
            p.SetAlpha(a0);
            p.PopScale();
        }
        FocusRing(p, area, m::kRadiusInner, id);
        return changed;
    }

    bool Ui::SliderInt(const char* label, int* v, int min, int max, const char* format)
    {
        float f = (float)*v;
        (void)format;
        SliderFloat(label, &f, (float)min, (float)max, "%.0f");
        const int next = (int)std::lround(f);
        if (next == *v)
            return false;
        *v = next;
        return true;
    }
}
