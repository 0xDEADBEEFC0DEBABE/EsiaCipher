// Cipher - the Ui's fonts, frame, theme, painters, text, windows, scroll regions and overlays (cipher.hpp).
#include "cipher.hpp"
#include "esia/text/system_fonts.hpp"
#include <algorithm>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <ctime>

namespace cipher
{
    using namespace esia;
    namespace m = metrics;

    namespace
    {
        // springs per item (Ui::Anim): up to 8 per id, each stepped once a frame
        struct Springs
        {
            Spring s[8];
            std::uint64_t stepped[8] = {};
            std::uint8_t made = 0;
        };
        struct ColorSprings
        {
            ColorSpring c[4];
            std::uint64_t stepped[4] = {};
            std::uint8_t made = 0;
        };
        // a floating window's state
        struct WinState
        {
            Spring shown, collapse, focus;
            bool made = false, collapsed = false;
            float fullHeight = 0.0f;
        };
        // a scroll region's bar: shown while it scrolls, is hovered or dragged
        struct ScrollState
        {
            float last = -1.0f;
            float idle = 10.0f;
            float grab = 0.0f;
            Spring shown, wide;
        };
        // MorphLabel: the texts on screen, the last one the current; each at its place (0), under it (+1) or over it (-1)
        struct MorphEntry
        {
            std::string text;
            Spring pos;
        };
        struct MorphState
        {
            std::vector<MorphEntry> e;
            std::uint64_t stepped = 0;
            double changed = -10.0;   // the last change (InputState::Time)
        };
        constexpr Motion kMorph{0.20f, 1.0f};
        constexpr double kMorphBurst = 0.15;   // a change closer than this to the one before swaps the digits in place

        MorphState& UpdateMorph(Context& ctx, Id id, std::string_view s, float dt)
        {
            MorphState& m = ctx.State<MorphState>(id);
            const std::uint64_t frame = ctx.FrameCount();
            if (m.e.empty() || m.stepped + 2 < frame)
            {
                // new, or back after a while off screen: at rest with the text it has now
                m.e.clear();
                m.e.push_back({std::string(s), Spring()});
                m.stepped = frame;
                return m;
            }
            const double now = ctx.Input().Time();
            if (m.e.back().text != s)
            {
                const bool burst = now - m.changed < kMorphBurst;
                m.changed = now;
                const std::size_t last = m.e.size() - 1;
                std::size_t back = last;
                for (std::size_t i = 0; i < last; ++i)
                    if (m.e[i].text == s)
                        back = i;
                // the text shown leaves to the side it is on (one coming up turns back down)
                Spring& cur = m.e[last].pos;
                if (back != last)
                {
                    // a text still leaving comes back: the change is undone, it plays backwards
                    cur.target = cur.value > 0.0f ? 1.0f : -1.0f;
                    MorphEntry revived = std::move(m.e[back]);
                    m.e.erase(m.e.begin() + (std::ptrdiff_t)back);
                    revived.pos.target = 0.0f;
                    m.e.push_back(std::move(revived));
                }
                else if (burst || cur.value > 0.45f)
                    m.e[last].text.assign(s);   // changing on and on, or hardly up yet: the new digits take its place and land with it
                else
                {
                    cur.target = cur.value > 0.0f ? 1.0f : -1.0f;
                    MorphEntry n;
                    n.text.assign(s);
                    n.pos.Snap(1.0f);
                    n.pos.target = 0.0f;
                    m.e.push_back(std::move(n));
                }
                if (m.e.size() > 5)
                    m.e.erase(m.e.begin());
            }
            if (m.stepped != ctx.FrameCount())
            {
                m.stepped = ctx.FrameCount();
                for (MorphEntry& x : m.e)
                    x.pos.Step(dt, kMorph);
                // the ones that are out are gone
                m.e.erase(std::remove_if(m.e.begin(), m.e.end() - 1, [](const MorphEntry& x) { return x.pos.target != 0.0f && std::fabs(x.pos.value) >= 0.995f; }),
                          m.e.end() - 1);
            }
            return m;
        }

        // What a material kind is made of (Solid: nothing of glass - a glass of these looks like the solid fill). The
        // glass kinds expose what is behind them towards their tint's brightness (legibility): a dark theme's glass
        // stays dark over a white wallpaper, a light one's light over a night sky - the theme's text reads on it
        // whatever the backdrop, and the backdrop's detail and color still come through.
        struct KindParams
        {
            float blur, refraction, bezel, dispersion, specular, saturation, legibility, tint, brightness, rim;
        };
        constexpr KindParams kKinds[4] = {
            {0.0f, 0.0f, 14.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f},        // Solid
            {0.0f, 14.0f, 26.0f, 0.6f, 1.0f, 1.15f, 0.6f, 0.35f, 0.03f, 1.0f},    // Clear
            {0.25f, 12.0f, 22.0f, 0.4f, 0.9f, 1.3f, 0.8f, 1.0f, 0.03f, 1.0f},     // Liquid
            {1.0f, 4.0f, 14.0f, 0.1f, 0.45f, 1.6f, 0.9f, 1.0f, 0.03f, 1.0f},      // Frosted
        };

        Color KindColor(const Theme& t, NoteKind k)
        {
            switch (k)
            {
            case NoteKind::Success: return t.success;
            case NoteKind::Warning: return t.warning;
            case NoteKind::Error: return t.danger;
            default: return t.info;
            }
        }
        Icon KindIcon(NoteKind k)
        {
            switch (k)
            {
            case NoteKind::Success: return Icon::CircleCheck;
            case NoteKind::Warning: return Icon::TriangleAlert;
            case NoteKind::Error: return Icon::CircleX;
            default: return Icon::Info;
            }
        }
    }

    // ------------------------------------------------------------------ fonts
    Fonts LoadFonts(text::TextSystem& ts)
    {
        Fonts f;
        const char* windir = std::getenv("WINDIR");
        const std::string dir = std::string(windir ? windir : "C:\\Windows") + "\\Fonts\\";
        // Segoe UI Variable's named instances (1-based, in the high 16 bits): Small 3 / 4, Text 8 / 9 / 10, Display 13 / 14 / 15
        const std::string variable = dir + "SegUIVar.ttf";
        const auto instance = [&](int k) { return ts.AddFontFile(variable.c_str(), k << 16); };
        f.text = instance(8);
        if (f.text)
        {
            f.textSemibold = instance(9);
            f.textBold = instance(10);
            f.display = instance(13);
            f.displaySemibold = instance(14);
            f.displayBold = instance(15);
            f.small = instance(3);
            f.smallSemibold = instance(4);
        }
        else
        {
            // before Windows 11, and elsewhere: the platform's UI family at three weights
            const auto system = [&](int weight) -> text::FontId {
                if (const std::optional<text::SystemFont> font = text::FindSystemFont(text::kSystemUiFamily, weight))
                    return text::AddSystemFont(ts, *font);
                return 0;
            };
            f.text = f.small = f.display = system(400);
            f.textSemibold = f.smallSemibold = f.displaySemibold = system(600);
            f.textBold = f.displayBold = system(700);
        }
        const std::string mono = dir + "CascadiaMono.ttf";
        f.mono = ts.AddFontFile(mono.c_str(), 4 << 16);
        f.monoSemibold = f.mono ? ts.AddFontFile(mono.c_str(), 5 << 16) : 0;
        if (!f.mono)
            for (const char* family : {"Cascadia Mono", "Consolas", "SF Mono", "Menlo", "DejaVu Sans Mono"})
                if (const std::optional<text::SystemFont> font = text::FindSystemFont(family))
                    if ((f.mono = text::AddSystemFont(ts, *font)) != 0)
                        break;
        // a face that is missing takes the nearest one
        for (text::FontId* id : {&f.textSemibold, &f.small, &f.display})
            if (!*id)
                *id = f.text;
        for (text::FontId* id : {&f.textBold, &f.smallSemibold, &f.displaySemibold})
            if (!*id)
                *id = f.textSemibold;
        if (!f.displayBold)
            f.displayBold = f.textBold;
        if (!f.mono)
            f.mono = f.text;
        if (!f.monoSemibold)
            f.monoSemibold = f.mono;
        text::AddFallbackFonts(ts, text::FindDefaultFallbackFonts());
        return f;
    }

    // ------------------------------------------------------------------ frame
    Ui::Ui(Context& ctx, text::TextSystem* textSystem, const Fonts& fonts) : ctx_(ctx), text_(textSystem), fonts_(fonts)
    {
        LayoutMetrics& lm = ctx_.Metrics();
        lm.itemSpacing = Vec2(m::kStep, m::kStep);
        lm.windowPadding = Vec2(0.0f, 0.0f);
        lm.scrollStep = 72.0f;
        lm.scrollSmoothing = 0.085f;
        SetTheme(0, false);
        // glass that lets what is behind it through (its legibility keeps the text readable over any of it)
        materials_[(int)SurfaceKind::Window] = {Material::Frosted, 0.56f, 26.0f};
        materials_[(int)SurfaceKind::Popup] = {Material::Frosted, 0.70f, 20.0f};
        materials_[(int)SurfaceKind::Sidebar] = {Material::Solid, 0.92f, 18.0f};
        materials_[(int)SurfaceKind::Composer] = {Material::Liquid, 0.55f, 12.0f};
        materials_[(int)SurfaceKind::Toast] = {Material::Liquid, 0.64f, 18.0f};
        materials_[(int)SurfaceKind::Watermark] = {Material::Liquid, 0.52f, 10.0f};
        materials_[(int)SurfaceKind::Card] = {Material::Solid, 1.0f, 12.0f};
    }

    void Ui::NewFrame(float pixelScale)
    {
        pixelScale_ = pixelScale > 0.0f ? pixelScale : 1.0f;
        const InputState& in = ctx_.Input();
        if (text_)
            text_->NewFrame({pixelScale_, in.Time()});
        float dt = std::clamp(in.DeltaTime(), 0.0f, 0.25f) * timeScale_;
        if (timeScale_ <= 0.0f && step_)
            dt = 1.0f / 60.0f;
        step_ = false;
        dt_ = dt;
        time_ += (double)dt;
        if (themeMoving_)
        {
            bool moving = false;
            Color* c = ThemeColors(shown_);
            for (int i = 0; i < kThemeColors; ++i)
            {
                moving |= themeColors_[i].Step(dt_, kGentle);
                c[i] = themeColors_[i].Value();
            }
            themeMoving_ = moving;
        }
        dark_.Step(dt_, kGentle);
        // the materials: each surface's kind, opacity and blur glide to what is asked for
        for (int k = 0; k < (int)SurfaceKind::Count; ++k)
        {
            MaterialMotion& mm = materialMotion_[k];
            const MaterialSpec& ms = materials_[k];
            if (!mm.made)
            {
                for (int i = 0; i < 4; ++i)
                    mm.kind[i].Snap(i == (int)ms.kind ? 1.0f : 0.0f);
                mm.opacity.Snap(ms.opacity);
                mm.blur.Snap(ms.blur);
                mm.made = true;
            }
            for (int i = 0; i < 4; ++i)
            {
                mm.kind[i].target = i == (int)ms.kind ? 1.0f : 0.0f;
                mm.kind[i].Step(dt_, kGentle);
            }
            mm.opacity.target = ms.opacity;
            mm.opacity.Step(dt_, kSnappy);
            mm.blur.target = ms.blur;
            mm.blur.Step(dt_, kSnappy);
        }
        if (in.KeyPressed(Key::Tab, false))
            focusVisible_ = true;
        if (in.MouseClicked(MouseButton::Left) || in.MouseClicked(MouseButton::Right))
            focusVisible_ = false;
        windows_.clear();
        floatingPrev_.swap(floating_);
        floating_.clear();
        folds_.clear();
        openMenus_.clear();
        inert_ = false;
        bleed_ = 0.0f;
        seq_.clear();
        alpha_.assign(1, 1.0f);
        disabled_.assign(1, false);
        nextWidth_ = 0.0f;
        tooltip_.clear();
        tooltipItem_ = 0;
        cascade_ = 0;
        if (baselines_.size() > 64)
            baselines_.clear();
    }

    void Ui::EndFrame()
    {
        DrawToasts();
        if (watermarkOn_)
            DrawWatermark();
        DrawTooltip();
        watermarkOn_ = false;
    }

    // ------------------------------------------------------------------ theme and materials
    void Ui::SetTheme(int index, bool animate)
    {
        const std::span<const Theme> themes = Themes();
        themeIndex_ = std::clamp(index, 0, (int)themes.size() - 1);
        const Theme& t = themes[(std::size_t)themeIndex_];
        const Color* to = ThemeColors(t);
        Color* shown = ThemeColors(shown_);
        for (int i = 0; i < kThemeColors; ++i)
        {
            if (animate)
                themeColors_[i].SetTarget(to[i]);
            else
            {
                themeColors_[i].Snap(to[i]);
                shown[i] = to[i];
            }
        }
        shown_.name = t.name;
        shown_.note = t.note;
        shown_.dark = t.dark;
        dark_.target = t.dark ? 1.0f : 0.0f;
        if (!animate)
            dark_.Snap(dark_.target);
        themeMoving_ = animate;
    }

    Color Ui::ToneColor(Tone t) const
    {
        switch (t)
        {
        case Tone::Secondary: return Ink(0.64f);
        case Tone::Tertiary: return Ink(0.44f);
        case Tone::Disabled: return Ink(0.28f);
        case Tone::Accent: return shown_.accent;
        case Tone::Success: return shown_.success;
        case Tone::Warning: return shown_.warning;
        case Tone::Danger: return shown_.danger;
        case Tone::Info: return shown_.info;
        default: return Ink(0.92f);
        }
    }

    GlassMaterial Ui::GlassOf(const MaterialSpec& mat, Color tint) const
    {
        GlassMaterial g;
        g.tint = tint.WithAlpha(Clamp01(mat.opacity));
        g.rim = Mix(Color::Black(0.10f), Color::White(0.24f), Dark());
        g.noise = 0.008f;
        switch (mat.kind)
        {
        case Material::Clear:
            g.blur = 0.0f;
            g.refraction = 14.0f;
            g.bezel = 26.0f;
            g.dispersion = 0.6f;
            g.specular = 1.0f;
            g.saturation = 1.15f;
            g.legibility = 0.6f;
            g.tint.a *= 0.35f;
            break;
        case Material::Liquid:
            g.blur = mat.blur * 0.25f;
            g.refraction = 12.0f;
            g.bezel = 22.0f;
            g.dispersion = 0.4f;
            g.specular = 0.9f;
            g.saturation = 1.3f;
            g.legibility = 0.8f;
            break;
        case Material::Frosted:
            g.blur = mat.blur;
            g.refraction = 4.0f;
            g.bezel = 14.0f;
            g.dispersion = 0.1f;
            g.specular = 0.45f;
            g.saturation = 1.6f;
            g.legibility = 0.9f;
            break;
        default: break;
        }
        return g;
    }

    GlassMaterial Ui::MaterialNow(SurfaceKind kind, Color tint, float& glassness, float& opacity) const
    {
        const MaterialMotion& mm = materialMotion_[(int)kind];
        float w[4], sum = 0.0f;
        for (int i = 0; i < 4; ++i)
            sum += (w[i] = std::max(mm.kind[i].value, 0.0f));
        for (float& x : w)
            x /= std::max(sum, 1e-4f);
        KindParams k{};
        float* out = &k.blur;
        for (int i = 0; i < 4; ++i)
        {
            const float* in = &kKinds[i].blur;
            for (int j = 0; j < 10; ++j)
                out[j] += w[i] * in[j];
        }
        glassness = 1.0f - w[(int)Material::Solid];
        opacity = Clamp01(mm.opacity.value);
        // the solid surface's hairline and glass's rim, as much of each as the kinds are
        const Color hairline = Ink(Mix(0.10f, 0.08f, Dark()));
        const Color rim = Mix(Color::Black(0.10f), Color::White(0.24f), Dark());
        GlassMaterial g;
        g.tint = tint.WithAlpha(opacity * k.tint);
        g.blur = k.blur * std::max(mm.blur.value, 0.0f);
        g.refraction = k.refraction;
        g.bezel = k.bezel;
        g.dispersion = k.dispersion;
        g.specular = k.specular;
        g.saturation = k.saturation;
        g.legibility = k.legibility;
        g.brightness = k.brightness;
        g.noise = 0.008f * glassness;
        g.rim = Mix(hairline, rim, Clamp01(k.rim));
        return g;
    }

    void Ui::Surface(esia::Painter& p, const Rect& r, float radius, SurfaceKind kind, Color base, float shadow)
    {
        float glassness = 0.0f, opacity = 1.0f;
        const GlassMaterial g = MaterialNow(kind, base, glassness, opacity);
        Style s = Style().Radius(radius);
        if (shadow > 0.0f)
            s.Shadow(Color::Black(Mix(0.14f, 0.34f, Dark()) * shadow), 36.0f * shadow, Vec2(0.0f, 14.0f * shadow), -4.0f);
        // glass turning solid loses its frost, lensing and light on the way: at the end it is the solid fill
        if (glassness <= 0.001f)
        {
            s.Fill(base.Fade(opacity));
            s.Stroke(1.0f / pixelScale_, Ink(Mix(0.10f, 0.08f, Dark())));
        }
        else
            s.Glass(g);
        p.Rect(r, s);
    }

    void Ui::Backdrop(DrawList& dl, const Rect& r, float intensity, float alpha)
    {
        esia::Painter p(dl, Env());
        p.SetAlpha(alpha);
        p.Rect(r, Style().Fill(shown_.canvas));
        // slow light in the theme's colors: what glass has to bend (over a flat canvas it would show nothing)
        struct Blob
        {
            Color c;
            float ax, ay, fx, fy, phase, size, alpha;
        };
        const Blob blobs[4] = {{shown_.accent2, 0.30f, 0.20f, 0.050f, 0.037f, 0.0f, 0.62f, 0.42f},
                               {shown_.hack[2], 0.26f, 0.28f, 0.031f, 0.047f, 2.1f, 0.70f, 0.40f},
                               {shown_.accent, 0.34f, 0.24f, 0.043f, 0.029f, 4.2f, 0.46f, 0.18f},
                               {shown_.hack[3], 0.22f, 0.30f, 0.027f, 0.053f, 5.3f, 0.40f, 0.16f}};
        const float t = (float)time_;
        for (const Blob& b : blobs)
        {
            const Vec2 c(r.min.x + r.Width() * (0.5f + b.ax * std::sin(b.fx * 6.2831853f * t + b.phase)),
                         r.min.y + r.Height() * (0.5f + b.ay * std::cos(b.fy * 6.2831853f * t + b.phase * 1.3f)));
            const float a = b.alpha * intensity * Mix(0.6f, 1.0f, Dark());
            p.Circle(c, r.Height() * b.size, Style().Fill(Paint::Radial(b.c.WithAlpha(a), b.c.WithAlpha(0.0f))));
        }
        // a grid of dots every 24 units: the texture glass refracts, too faint to read as a pattern
        const float step = 24.0f, dot = 1.0f;
        const std::uint32_t col = Ink(0.07f * intensity * alpha).ToRgba8();
        for (float y = r.min.y + step * 0.5f; y < r.max.y; y += step)
            for (float x = r.min.x + step * 0.5f; x < r.max.x; x += step)
                dl.AddRectFilled(Rect(x, y, x + dot, y + dot), col);
    }

    // ------------------------------------------------------------------ painters and text
    PainterEnv Ui::Env() const
    {
        PainterEnv env;
        env.pixelScale = ctx_.CurrentWindow() ? ctx_.Scale() : pixelScale_;
        env.cornerSmoothing = 0.0f;   // circular corners: a layer and the one in it stay concentric
        env.text = text_;
        env.alpha = alpha_.back();
        return env;
    }

    esia::Painter Ui::Painter() { return esia::Painter(ctx_.WindowDrawList(), Env()); }
    esia::Painter Ui::Painter(DrawList& dl) { return esia::Painter(dl, Env()); }

    Vec2 Ui::Measure(text::FontRef f, std::string_view s, float wrap) const
    {
        if (!text_ || !f.id || s.empty())
            return Vec2(0.0f, f.size);
        return text_->Measure(f, s, wrap).size;
    }

    void Ui::Label(esia::Painter& p, float x, float cy, text::FontRef f, Color c, std::string_view s, float align, float maxWidth, float dy) const
    {
        if (!text_ || !f.id || s.empty() || !c.IsVisible())
            return;
        const std::uint64_t key = ((std::uint64_t)f.id << 32) | std::bit_cast<std::uint32_t>(f.size);
        float baseline = -1.0f;
        for (const auto& [k, b] : baselines_)
            if (k == key)
                baseline = b;
        if (baseline < 0.0f)
        {
            baseline = text_->Measure(f, "H").baseline;
            baselines_.push_back({key, baseline});
        }
        // capitals are 0.7 em tall: their middle on cy, the baseline on a pixel row
        const float ps = p.Env().pixelScale > 0.0f ? p.Env().pixelScale : 1.0f;
        const float base = std::round((cy + 0.35f * f.size) * ps) / ps;
        const std::uint32_t flags = maxWidth > 0.0f ? text::TextFlags_Ellipsis : text::TextFlags_None;
        float w = 0.0f;
        if (align != 0.0f)
            w = maxWidth > 0.0f ? std::min(Measure(f, s).x, maxWidth) : Measure(f, s).x;
        p.Text(Vec2(x - w * align, base - baseline + dy), f, c, s, maxWidth > 0.0f ? maxWidth : 0.0f, flags);
    }

    void Ui::MorphLabel(esia::Painter& p, Id id, float x, float cy, text::FontRef f, Color c, std::string_view s, float align, float maxWidth)
    {
        MorphState& m = UpdateMorph(ctx_, id, s, dt_);
        if (m.e.size() == 1 && m.e[0].pos.value == 0.0f)
        {
            Label(p, x, cy, f, c, s, align, maxWidth);
            return;
        }
        const float rise = f.size * 0.45f;
        const auto alphaOf = [](const MorphEntry& en) { return Clamp01(1.0f - std::fabs(en.pos.value) * 1.25f); };
        // monospaced texts of one length: by columns - a column all of them agree on stays still
        bool columns = (f.id == fonts_.mono || f.id == fonts_.monoSemibold) && fonts_.mono != fonts_.text;
        for (const MorphEntry& en : m.e)
        {
            columns = columns && en.text.size() == s.size();
            for (char ch : en.text)
                columns = columns && (unsigned char)ch < 0x80;
        }
        if (!columns)
        {
            for (const MorphEntry& en : m.e)
            {
                const float a = alphaOf(en);
                if (a > 0.004f)
                    Label(p, x, cy, f, c.Fade(a), en.text, align, maxWidth, en.pos.value * rise);
            }
            return;
        }
        const std::size_t n = s.size();
        const float adv = Measure(f, "0").x;
        const float x0 = x - adv * (float)n * align;
        const auto agree = [&](std::size_t k) {
            for (const MorphEntry& en : m.e)
                if (en.text[k] != s[k])
                    return false;
            return true;
        };
        for (std::size_t k = 0; k < n;)
        {
            const bool same = agree(k);
            std::size_t z = k + 1;
            while (z < n && agree(z) == same)
                ++z;
            if (same)
                Label(p, x0 + adv * (float)k, cy, f, c, s.substr(k, z - k));
            else
                for (const MorphEntry& en : m.e)
                {
                    const float a = alphaOf(en);
                    if (a > 0.004f)
                        Label(p, x0 + adv * (float)k, cy, f, c.Fade(a), std::string_view(en.text).substr(k, z - k), 0.0f, 0.0f, en.pos.value * rise);
                }
            k = z;
        }
    }

    void Ui::SnapMorph(Id id, std::string_view s)
    {
        MorphState& m = ctx_.State<MorphState>(id);
        m.e.clear();
        m.e.push_back({std::string(s), Spring()});
        m.stepped = ctx_.FrameCount();
    }

    bool Ui::AddItem(Id id, const Rect& r, std::uint32_t flags) { return ctx_.ItemAdd(inert_ ? 0 : id, r, inert_ ? 0u : flags); }

    ButtonResult Ui::Behavior(Id id, const Rect& r, std::uint32_t flags)
    {
        if (inert_)
            return ButtonResult();
        return ctx_.ButtonBehavior(id, r, flags);
    }

    float Ui::SpacedLabel(esia::Painter& p, float x, float cy, text::FontRef f, Color c, std::string_view s, float spacing, bool caps) const
    {
        float at = x;
        char ch[2] = {0, 0};
        for (std::size_t i = 0; i < s.size(); ++i)
        {
            ch[0] = caps ? (char)std::toupper((unsigned char)s[i]) : s[i];
            const std::string_view one(ch, 1);
            if (ch[0] != ' ')
                Label(p, at, cy, f, c, one);
            at += Measure(f, ch[0] == ' ' ? std::string_view("n") : one).x + spacing;
        }
        return at - spacing - x;
    }

    float Ui::BalancedWidth(text::FontRef f, std::string_view s, float width)
    {
        if (!text_ || !f.id || s.empty() || width <= 0.0f)
            return width;
        const std::uint64_t key = ((std::uint64_t)HashString(s, f.id) << 32) ^ ((std::uint64_t)std::bit_cast<std::uint32_t>(f.size) * 2654435761u) ^
                                  (std::uint64_t)std::lround(width * 4.0f);
        for (const auto& [k, w] : balanced_)
            if (k == key)
                return w;
        float best = width;
        const int lines = text_->Measure(f, s, width).lines;
        if (lines > 1)
        {
            // the narrowest width with as many lines, by halving: lines that come out even
            float lo = width * 0.5f, hi = width;
            for (int i = 0; i < 10; ++i)
            {
                const float mid = (lo + hi) * 0.5f;
                if (text_->Measure(f, s, mid).lines <= lines)
                    hi = mid;
                else
                    lo = mid;
            }
            best = std::ceil(hi) + 1.0f;
        }
        if (balanced_.size() > 512)
            balanced_.clear();
        balanced_.push_back({key, best});
        return best;
    }

    std::string_view Ui::Visible(std::string_view label) const
    {
        const std::size_t hash = label.find("##");
        return hash == std::string_view::npos ? label : label.substr(0, hash);
    }

    float Ui::AnimFrom(Id id, int slot, float from, float target, const Motion& mo)
    {
        Springs& a = ctx_.State<Springs>(id);
        Spring& s = a.s[slot & 7];
        const std::uint8_t bit = (std::uint8_t)(1u << (slot & 7));
        const std::uint64_t frame = ctx_.FrameCount();
        if (!(a.made & bit))
        {
            s.Snap(from);
            a.made |= bit;
        }
        else if (a.stepped[slot & 7] + 2 < frame)
            s.Snap(target);   // not drawn for a while (a popup closed, a tab away): it comes back at rest
        s.target = target;
        if (a.stepped[slot & 7] != frame)
        {
            s.Step(dt_, mo);
            a.stepped[slot & 7] = frame;
        }
        return s.value;
    }

    float Ui::Anim(Id id, int slot, float target, const Motion& mo) { return AnimFrom(id, slot, target, target, mo); }

    float Ui::AnimVelocity(Id id, int slot) { return ctx_.State<Springs>(id).s[slot & 7].velocity; }

    Color Ui::AnimColor(Id id, int slot, Color target, const Motion& mo)
    {
        ColorSprings& a = ctx_.State<ColorSprings>(id);
        ColorSpring& c = a.c[slot & 3];
        const std::uint8_t bit = (std::uint8_t)(1u << (slot & 3));
        const std::uint64_t frame = ctx_.FrameCount();
        if (!(a.made & bit) || a.stepped[slot & 3] + 2 < frame)
        {
            c.Snap(target);
            a.made |= bit;
        }
        c.SetTarget(target);
        if (a.stepped[slot & 3] != frame)
        {
            c.Step(dt_, mo);
            a.stepped[slot & 3] = frame;
        }
        return c.Value();
    }

    // ------------------------------------------------------------------ layout
    void Ui::SameLine(float spacing) { ctx_.SameLine(0.0f, spacing); }
    void Ui::NewLine() { ctx_.NewLine(); }
    void Ui::Spacing(float height) { ctx_.ItemSize(Vec2(0.0f, std::max(height - m::kStep, 0.0f))); }
    void Ui::Dummy(Vec2 size)
    {
        const Rect r = Rect::FromSize(ctx_.CursorPos(), size);
        ctx_.ItemSize(size);
        ctx_.ItemAdd(0, r);
    }
    void Ui::Indent(float width) { ctx_.Indent(width > 0.0f ? width : 18.0f); }
    void Ui::Unindent(float width) { ctx_.Unindent(width > 0.0f ? width : 18.0f); }
    void Ui::BeginGroup() { ctx_.BeginGroup(); }
    void Ui::EndGroup() { ctx_.EndGroup(); }
    void Ui::PushId(const char* id) { ctx_.PushId(id); }
    void Ui::PushId(int id) { ctx_.PushId(id); }
    void Ui::PopId() { ctx_.PopId(); }
    float Ui::ContentWidth() const { return std::max(ctx_.ContentRegionAvail().x, 1.0f); }

    void Ui::BeginDisabled(bool disabled)
    {
        const bool now = disabled_.back() || disabled;
        disabled_.push_back(now);
        // the region dims as it is disabled and lights up again as it is enabled: a spring per region
        float dim = disabled ? 1.0f : 0.0f;
        if (ctx_.CurrentWindow())
            dim = Clamp01(Anim(SeqId("##disabled"), 0, dim, kSmooth));
        alpha_.push_back(alpha_.back() * (1.0f - 0.58f * dim));
    }

    Id Ui::SeqId(const char* scope)
    {
        const Id key = ctx_.CurrentWindow() ? ctx_.GetId(scope) : HashLabel(scope, 0x53455155u);
        auto it = std::find_if(seq_.begin(), seq_.end(), [&](const std::pair<Id, int>& e) { return e.first == key; });
        if (it == seq_.end())
        {
            seq_.push_back({key, 1});
            return HashInt(0, key);
        }
        return HashInt(it->second++, key);
    }
    void Ui::EndDisabled()
    {
        if (disabled_.size() > 1)
        {
            disabled_.pop_back();
            alpha_.pop_back();
        }
    }

    Rect Ui::ItemRect(Vec2 size)
    {
        const Rect r = Rect::FromSize(ctx_.CursorPos(), size);
        ctx_.ItemSize(size);
        return r;
    }

    float Ui::LabelColumn() const
    {
        // a third of the width, on the 6-unit grid: every row of a window shares it
        const float avail = ContentWidth();
        return std::clamp(std::floor(avail * 0.36f / m::kStep) * m::kStep, 96.0f, 186.0f);
    }

    void Ui::Separator()
    {
        const float w = ContentWidth();
        const Rect r = ItemRect(Vec2(w, 13.0f));
        ctx_.ItemAdd(0, r);
        esia::Painter p = Painter();
        p.HLine(r.min.x, r.max.x, r.Center().y, Ink(0.09f), 1.0f / pixelScale_);
    }

    void Ui::SeparatorText(const char* label)
    {
        const float w = ContentWidth();
        const Rect r = ItemRect(Vec2(w, 30.0f));
        ctx_.ItemAdd(0, r);
        esia::Painter p = Painter();
        const text::FontRef f{fonts_.smallSemibold, m::kFontCaption};
        const std::string_view s = Visible(label);
        const float cy = r.min.y + 18.0f;
        Label(p, r.min.x, cy, f, Ink(0.46f), s);
        const float x = r.min.x + Measure(f, s).x + 10.0f;
        if (x < r.max.x)
            p.HLine(x, r.max.x, cy, Ink(0.08f), 1.0f / pixelScale_);
    }

    // ------------------------------------------------------------------ text
    void Ui::FormatV(const char* fmt, va_list args)
    {
        va_list copy;
        va_copy(copy, args);
        const int n = std::vsnprintf(nullptr, 0, fmt, copy);
        va_end(copy);
        fmt_.resize(n > 0 ? (std::size_t)n : 0u);
        if (n > 0)
            std::vsnprintf(fmt_.data(), (std::size_t)n + 1, fmt, args);
    }

    void Ui::TextUnformatted(std::string_view s, Tone tone, float size, bool wrap, text::FontId font)
    {
        const text::FontRef f{font ? font : fonts_.text, size};
        const float wrapWidth = wrap ? ContentWidth() : 0.0f;
        const text::TextMetrics tm = text_ && !s.empty() ? text_->Measure(f, s, wrapWidth) : text::TextMetrics{Vec2(0.0f, size * 1.33f), size, 1};
        if (ctx_.LineBaseline() >= 0.0f)
            ctx_.AlignToLineBaseline(tm.baseline);
        const Vec2 pos = ctx_.CursorPos();
        ctx_.ItemSize(tm.size, tm.baseline);
        ctx_.ItemAdd(0, Rect::FromSize(pos, tm.size));
        esia::Painter p = Painter();
        p.Text(pos, f, ToneColor(tone), s, wrapWidth);
    }

    void Ui::Text(const char* fmt, ...)
    {
        va_list a;
        va_start(a, fmt);
        FormatV(fmt, a);
        va_end(a);
        TextUnformatted(fmt_);
    }

    void Ui::TextColored(Color c, const char* fmt, ...)
    {
        va_list a;
        va_start(a, fmt);
        FormatV(fmt, a);
        va_end(a);
        const text::FontRef f{fonts_.text, m::kFont};
        const text::TextMetrics tm = text_ ? text_->Measure(f, fmt_) : text::TextMetrics{};
        const Vec2 pos = ctx_.CursorPos();
        ctx_.ItemSize(tm.size, tm.baseline);
        ctx_.ItemAdd(0, Rect::FromSize(pos, tm.size));
        esia::Painter p = Painter();
        p.Text(pos, f, c, fmt_);
    }

    void Ui::TextDisabled(const char* fmt, ...)
    {
        va_list a;
        va_start(a, fmt);
        FormatV(fmt, a);
        va_end(a);
        TextUnformatted(fmt_, Tone::Disabled);
    }

    void Ui::TextWrapped(const char* fmt, ...)
    {
        va_list a;
        va_start(a, fmt);
        FormatV(fmt, a);
        va_end(a);
        TextUnformatted(fmt_, Tone::Primary, m::kFont, true);
    }

    void Ui::Caption(const char* fmt, ...)
    {
        va_list a;
        va_start(a, fmt);
        FormatV(fmt, a);
        va_end(a);
        TextUnformatted(fmt_, Tone::Secondary, m::kFontSmall, true);
    }

    void Ui::Heading(const char* textValue)
    {
        Spacing(m::kStep);
        TextUnformatted(Visible(textValue), Tone::Primary, m::kFontHeading, false, fonts_.displaySemibold);
        Spacing(2.0f);
    }

    void Ui::LabelText(const char* label, const char* fmt, ...)
    {
        va_list a;
        va_start(a, fmt);
        FormatV(fmt, a);
        va_end(a);
        const float w = ContentWidth();
        const Rect r = ItemRect(Vec2(w, 28.0f));
        ctx_.ItemAdd(0, r);
        esia::Painter p = Painter();
        const float col = LabelColumn();
        Label(p, r.min.x, r.Center().y, {fonts_.text, m::kFont}, Ink(0.64f), Visible(label), 0.0f, col - 12.0f);
        MorphLabel(p, HashInt(0x4C424C, IdOf(label)), r.min.x + col, r.Center().y, {fonts_.mono, m::kFontMono}, Ink(0.92f), fmt_, 0.0f, w - col);
    }

    void Ui::Bullet()
    {
        const Rect r = ItemRect(Vec2(18.0f, 22.0f));
        ctx_.ItemAdd(0, r);
        esia::Painter p = Painter();
        p.Circle(r.Center(), 2.5f, Style().Fill(Ink(0.6f)));
    }

    void Ui::BulletText(const char* fmt, ...)
    {
        va_list a;
        va_start(a, fmt);
        FormatV(fmt, a);
        va_end(a);
        const std::string s = fmt_;
        const text::FontRef f{fonts_.text, m::kFont};
        const float indent = 20.0f;
        const float w = ContentWidth() - indent;
        const text::TextMetrics tm = text_ ? text_->Measure(f, s, w) : text::TextMetrics{};
        const Vec2 pos = ctx_.CursorPos();
        const Vec2 size(ContentWidth(), tm.size.y);
        ctx_.ItemSize(size, tm.baseline);
        ctx_.ItemAdd(0, Rect::FromSize(pos, size));
        esia::Painter p = Painter();
        p.Circle(Vec2(pos.x + 7.0f, pos.y + tm.baseline - 0.35f * m::kFont), 2.5f, Style().Fill(Ink(0.6f)));
        p.Text(Vec2(pos.x + indent, pos.y), f, Ink(0.92f), s, w);
    }

    // ------------------------------------------------------------------ windows
    bool Ui::Begin(const char* title, const WindowSpec& spec)
    {
        const std::string_view name(title);
        WinFrame wf;
        wf.id = HashLabel(name, 0x43495048u);
        WinState& ws = ctx_.State<WinState>(wf.id);
        const bool open = spec.open ? *spec.open : true;
        if (!ws.made)
        {
            ws.made = true;
            ws.fullHeight = spec.size.y;
        }
        ws.shown.target = open ? 1.0f : 0.0f;
        ws.shown.Step(dt_, open ? kSmooth : kSnappy);
        ws.collapse.target = ws.collapsed ? 1.0f : 0.0f;
        ws.collapse.Step(dt_, kSmooth);
        const int slot = cascade_++;
        if (!open && ws.shown.value <= 0.002f)
        {
            ws.shown.Snap(0.0f);
            windows_.push_back(wf);
            return false;
        }
        const float shown = Clamp01(ws.shown.value), fold = Clamp01(ws.collapse.value);
        const float titleH = m::kTitleBar;
        Vec2 pos = spec.pos;
        if (pos.x < 0.0f)
            pos = Vec2(300.0f + 36.0f * (float)(slot % 6), 96.0f + 36.0f * (float)(slot % 6));
        ctx_.SetNextWindowPos(pos, Cond::FirstUse);
        ctx_.SetNextWindowSize(spec.size, Cond::FirstUse);
        const bool folding = fold > 0.0005f;
        if (folding)
        {
            const Window* existing = ctx_.FindWindowByName(name);
            const float width = existing ? existing->GetRect().Width() : spec.size.x;
            ctx_.SetNextWindowSize(Vec2(width, Mix(ws.fullHeight, titleH, fold)), Cond::Always);
        }
        WindowOptions o;
        o.flags = esia::WindowFlags_NoScroll;
        if (!open)
            o.flags |= esia::WindowFlags_NoInputs;
        if (folding || (spec.flags & WindowFlags_NoResize))
            o.flags |= esia::WindowFlags_NoResize;
        if (spec.flags & WindowFlags_NoMove)
            o.flags |= esia::WindowFlags_NoMove;
        o.padding = Vec2(0.0f, 0.0f);
        o.minSize = folding ? Vec2(spec.minSize.x, titleH) : spec.minSize;
        o.resizeGrip = 18.0f;
        ctx_.Begin(name, o);
        wf.begun = true;
        wf.resizable = !(o.flags & esia::WindowFlags_NoResize);
        Window* w = ctx_.CurrentWindow();
        const Rect r = w->GetRect();
        if (!folding)
            ws.fullHeight = r.Height();
        const bool focused = ctx_.FocusedWindow() == w;
        ws.focus.target = focused ? 1.0f : 0.0f;
        ws.focus.Step(dt_, kSnappy);
        alpha_.push_back(alpha_.back() * shown);
        wf.alpha = shown;

        esia::Painter p = Painter();
        DrawList& dl = ctx_.WindowDrawList();
        // the panel grows into place from 96 % as it fades in; its shadow reaches past the window's clip
        dl.PushClipRect(Rect(Vec2(-64.0f, -64.0f), ctx_.DisplaySize() + Vec2(64.0f, 64.0f)), false);
        p.PushScale(r.Center(), 0.96f + 0.04f * shown);
        Surface(p, r, m::kRadiusWindow, SurfaceKind::Window, shown_.raised, 0.75f + 0.25f * ws.focus.value);
        p.PopScale();
        dl.PopClipRect();

        // the title bar: icon and title, then the buttons, concentric with the corner (6 in, 36 tall: radius 18)
        const InputState& in = ctx_.Input();
        const float cy = r.min.y + titleH * 0.5f;
        float right = r.max.x - 6.0f;
        bool overButton = false;
        const auto titleButton = [&](const char* id, Icon icon, float angle) {
            const Rect b(right - 36.0f, r.min.y + 6.0f, right, r.min.y + 42.0f);
            right -= 36.0f;
            const Id bid = ctx_.GetId(id);
            ctx_.ItemAdd(bid, b);
            const ButtonResult br = ctx_.ButtonBehavior(bid, b);
            overButton |= br.hovered;
            const float hover = Anim(bid, 0, br.hovered ? 1.0f : 0.0f);
            const float press = Anim(bid, 1, br.held ? 1.0f : 0.0f);
            if (hover > 0.004f)
                p.Circle(b.Center(), 18.0f * (1.0f - 0.06f * press), Style().Fill(Ink(0.08f * hover + 0.04f * press)));
            IconMotion mo;
            mo.angle = angle;
            if (angle != 0.0f)
                DrawIconVector(p, icon, b.Center(), m::kIcon, Ink(0.55f + 0.37f * hover), mo);
            else
                DrawIcon(p, icon, b.Center(), m::kIcon, Ink(0.55f + 0.37f * hover));
            return br.pressed;
        };
        if (spec.open && titleButton("##close", Icon::X, 0.0f))
            *spec.open = false;
        if (!(spec.flags & WindowFlags_NoCollapse) && titleButton("##fold", Icon::ChevronDown, -1.5707963f * fold))
            ws.collapsed = !ws.collapsed;
        float x = r.min.x + 18.0f;
        if (spec.icon != Icon::None)
        {
            DrawIcon(p, spec.icon, Vec2(x + 8.0f, cy), m::kIcon, Ink(0.62f + 0.3f * ws.focus.value));
            x += 26.0f;
        }
        Label(p, x, cy, {fonts_.textSemibold, m::kFont}, Ink(0.60f + 0.32f * ws.focus.value), Visible(name), 0.0f, std::max(right - x - 6.0f, 1.0f));
        const Rect bar(r.min.x, r.min.y, r.max.x, r.min.y + titleH);
        if (open && !overButton && ctx_.HoveredWindow() == w && in.MouseDoubleClicked(MouseButton::Left) && bar.Contains(in.MousePos()) &&
            !(spec.flags & WindowFlags_NoCollapse))
            ws.collapsed = !ws.collapsed;
        if (fold >= 0.999f)
        {
            windows_.push_back(wf);
            return false;
        }

        // the body: a scroll region under the title bar, its edges where the window's are
        ctx_.SetCursorPos(Vec2(r.min.x, r.min.y + titleH));
        ChildOptions co;
        co.size = Vec2(r.Width(), std::max(r.Height() - titleH, 1.0f));
        co.padding = Vec2(m::kPad, 2.0f);
        co.clipInset = Rect(-m::kPad, -2.0f, -m::kPad, -2.0f);
        co.flags = (spec.flags & WindowFlags_NoScroll) ? ChildFlags_None : (ChildFlags_ScrollY | ChildFlags_SmoothScroll);
        ctx_.BeginChild("##body", co);
        wf.body = true;
        wf.roundBottom = m::kRadiusWindow;   // the body reaches the window's rounded bottom
        // the body's empty space takes presses: only the title bar moves the window
        const Rect view = ctx_.ViewRect();
        const Id bg = ctx_.GetId("##background");
        ctx_.ItemAdd(bg, view, ItemFlags_Background);
        ctx_.ButtonBehavior(bg, view);
        // content scrolled under the title bar: a hairline appears, the content fades out into it
        const float scroll = ctx_.Scroll().y;
        const float line = Anim(bg, 0, scroll > 0.5f ? 1.0f : 0.0f);
        if (line > 0.004f)
        {
            esia::Painter top = Painter();
            top.PushClip(view, false);
            top.HLine(view.min.x, view.max.x, view.min.y, Ink(0.09f * line), 1.0f / pixelScale_);
            top.PopClip();
        }
        BeginFade(wf, view);
        ctx_.ItemSize(Vec2(0.0f, 4.0f));
        windows_.push_back(wf);
        return true;
    }

    void Ui::BeginFade(WinFrame& wf, const Rect& view)
    {
        // each edge fades over as much as is scrolled beyond it: at most a sixth of the view (48 at the bottom, a little
        // less at the top, where the content scrolls away under a heading)
        const float scroll = ctx_.Scroll().y, max = ctx_.ScrollMax().y;
        const float reach = std::min(48.0f, view.Height() / 6.0f);
        wf.fadeView = view;
        wf.fadeTop = std::min(scroll, reach * 0.6f);
        wf.fadeBottom = std::min(std::max(max - scroll, 0.0f), reach);
        esia::Painter p = Painter();
        p.BeginEdgeFade(view, wf.fadeTop, wf.fadeBottom);
    }

    void Ui::ResumeFade()
    {
        if (windows_.empty() || !windows_.back().body)
            return;
        const WinFrame& wf = windows_.back();
        esia::Painter p = Painter();
        p.BeginEdgeFade(wf.fadeView, wf.fadeTop, wf.fadeBottom);
    }

    void Ui::End()
    {
        if (windows_.empty())
            return;
        EndFoldsOf(windows_.size());
        const WinFrame wf = windows_.back();
        windows_.pop_back();
        if (wf.body)
        {
            ctx_.ItemSize(Vec2(0.0f, 8.0f));
            esia::Painter p = Painter();
            p.EndEdgeFade();
            ScrollBar(ctx_.GetId("##scroll"), wf.roundTop, wf.roundBottom);
            ctx_.EndChild();
        }
        if (!wf.begun)
            return;
        floating_.push_back({wf.id, ctx_.CurrentWindow()->GetRect()});   // a surface for the pet
        if (wf.resizable)
            ResizeCorner(HashInt(0x434F524E, wf.id), ctx_.CurrentWindow()->GetRect(), m::kRadiusWindow);
        ctx_.End();
        alpha_.pop_back();
    }

    bool Ui::BeginChild(const char* id, Vec2 size, bool border, Vec2 padding)
    {
        ChildOptions co;
        co.size = size;
        co.padding = padding;
        co.clipInset = Rect(-padding.x, -padding.y, -padding.x, -padding.y);
        co.flags = ChildFlags_ScrollY | ChildFlags_SmoothScroll;
        if (border)
        {
            // the panel under it, drawn before it begins
            const Vec2 avail = ctx_.ContentRegionAvail();
            const Vec2 sz(size.x > 0.0f ? size.x : avail.x + size.x, size.y > 0.0f ? size.y : avail.y + size.y);
            esia::Painter p = Painter();
            p.Rect(Rect::FromSize(ctx_.CursorPos(), sz), Style().Radius(m::kRadiusPanel).Fill(Ink(0.03f)).Stroke(1.0f / pixelScale_, Ink(0.08f)));
        }
        const bool visible = ctx_.BeginChild(id, co);
        WinFrame wf;
        wf.id = ctx_.GetId("##child");
        wf.body = true;
        if (border)
            wf.roundTop = wf.roundBottom = m::kRadiusPanel;
        BeginFade(wf, ctx_.ViewRect());
        windows_.push_back(wf);
        return visible;
    }

    void Ui::EndChild()
    {
        if (windows_.empty())
            return;
        EndFoldsOf(windows_.size());
        const WinFrame wf = windows_.back();
        windows_.pop_back();
        esia::Painter p = Painter();
        p.EndEdgeFade();
        ScrollBar(ctx_.GetId("##scroll"), wf.roundTop, wf.roundBottom);
        ctx_.EndChild();
        ResumeFade();   // the region it was in fades again
    }

    // A thin bar at the right of the scroll region being submitted: shown while it scrolls, is hovered or dragged,
    // wider under the pointer; dragging the thumb scrolls. Its track ends where the region's rounded corners begin
    // (roundTop / roundBottom: their radius), so the thumb runs along the straight edge only.
    void Ui::ScrollBar(Id id, float roundTop, float roundBottom)
    {
        ScrollState& ss = ctx_.State<ScrollState>(id);
        const float scroll = ctx_.Scroll().y, max = ctx_.ScrollMax().y;
        const Rect view = ctx_.ViewRect();
        if (ss.last >= 0.0f && std::fabs(scroll - ss.last) > 0.25f)
            ss.idle = 0.0f;
        else
            ss.idle += dt_;
        ss.last = scroll;
        if (max <= 0.5f)
        {
            ss.shown.Snap(0.0f);
            return;
        }
        const InputState& in = ctx_.Input();
        const float top = view.min.y + std::max(8.0f, roundTop), bottom = view.max.y - std::max(8.0f, roundBottom);
        const float track = std::max(bottom - top, 1.0f);
        const float thumbH = std::max(32.0f, track * view.Height() / (view.Height() + max));
        const float thumbY = top + (track - thumbH) * Clamp01(scroll / max);
        const Rect area(view.max.x - 14.0f, top, view.max.x - 2.0f, bottom);
        ChildOptions fo;
        fo.flags = ChildFlags_Floating;
        fo.rect = area;
        ctx_.BeginChild("##scrollbar", fo);
        const Id thumb = ctx_.GetId("thumb");
        AddItem(thumb, area);
        const ButtonResult b = Behavior(thumb, area, ButtonFlags_PressOnClick);
        const bool active = !inert_ && ctx_.ActiveId() == thumb;
        if (b.pressed)
        {
            const float y = in.MousePos().y;
            ss.grab = (y >= thumbY && y <= thumbY + thumbH) ? y - thumbY : thumbH * 0.5f;
        }
        if (active && in.MouseDown(MouseButton::Left))
        {
            const float t = Clamp01((in.MousePos().y - ss.grab - top) / std::max(track - thumbH, 1.0f));
            ctx_.SetScrollY(t * max);
            ss.idle = 0.0f;
        }
        const bool hoverView = !inert_ && in.MouseValid() && view.Contains(in.MousePos()) && ctx_.HoveredWindow() == ctx_.CurrentWindow();
        ss.shown.target = (ss.idle < 1.2f || b.hovered || active || hoverView) ? 1.0f : 0.0f;
        ss.shown.Step(dt_, kSmooth);
        ss.wide.target = (b.hovered || active) ? 1.0f : 0.0f;
        ss.wide.Step(dt_, kSnappy);
        const float a = Clamp01(ss.shown.value);
        if (a > 0.004f)
        {
            esia::Painter p = Painter();
            const float wbar = 4.0f + 3.0f * ss.wide.value;
            p.Capsule(Rect(area.max.x - 2.0f - wbar, thumbY, area.max.x - 2.0f, thumbY + thumbH),
                      Style().Fill(Ink((0.26f + 0.2f * ss.wide.value) * a)));
        }
        ctx_.EndChild();
    }

    // ------------------------------------------------------------------ tooltips
    void Ui::SetItemTooltip(const char* fmt, ...)
    {
        if (!lastHovered_)
            return;
        va_list a;
        va_start(a, fmt);
        FormatV(fmt, a);
        va_end(a);
        tooltip_ = fmt_;
        tooltipItem_ = lastId_;
        tooltipAnchor_ = ctx_.LastItemRect();
    }

    void Ui::DrawTooltip()
    {
        // shown after the pointer rests 0.45 s on an item, at once while another was just shown
        const bool asked = !tooltip_.empty();
        if (asked && tooltipItem_ != tooltipShownFor_)
        {
            tooltipHover_ = (time_ - tooltipWarm_ < 0.6) ? 1.0f : 0.0f;
            tooltipShownFor_ = tooltipItem_;
        }
        tooltipHover_ = asked ? tooltipHover_ + dt_ : 0.0f;
        tooltipShown_.target = (asked && tooltipHover_ > 0.45f) ? 1.0f : 0.0f;
        tooltipShown_.Step(dt_, kSnappy);
        if (asked && tooltipShown_.target > 0.5f)
            tooltipWarm_ = time_;
        if (!asked)
            tooltipShownFor_ = 0;
        float a = Clamp01(tooltipShown_.value);
        const bool appearing = a < 0.05f;
        if (asked && tooltipShown_.target > 0.5f && tooltip_ != tooltipText_)
        {
            // another text while it is shown: the old one fades out as the box takes the new one's size
            if (!appearing && !tooltipText_.empty())
            {
                if (tooltipSwap_.value >= 0.5f)
                {
                    tooltipPrev_ = tooltipText_;
                    tooltipSwap_.Snap(0.0f);
                }
            }
            else
                tooltipSwap_.Snap(1.0f);
            tooltipText_ = tooltip_;
        }
        tooltipSwap_.target = 1.0f;
        tooltipSwap_.Step(dt_, kSnappy);
        if (a <= 0.004f || tooltipText_.empty())
            return;
        const text::FontRef f{fonts_.text, m::kFontSmall};
        const Vec2 ts = Measure(f, tooltipText_, 280.0f);
        const Vec2 size(ts.x + 20.0f, ts.y + 12.0f);
        const Vec2 d = ctx_.DisplaySize();
        Vec2 pos(tooltipAnchor_.Center().x - size.x * 0.5f, tooltipAnchor_.max.y + 8.0f);
        if (pos.y + size.y > d.y - 8.0f)
            pos.y = tooltipAnchor_.min.y - 8.0f - size.y;
        pos.x = std::clamp(pos.x, 8.0f, d.x - size.x - 8.0f);
        // it glides from item to item, its size with it; appearing, it starts where it is; an item that moves (its
        // window dragged) carries it along
        if (asked && tooltipItem_ == tooltipFollow_)
        {
            const Vec2 moved = tooltipAnchor_.min - tooltipFollowAt_;
            tooltipX_.value += moved.x;
            tooltipY_.value += moved.y;
        }
        tooltipFollow_ = asked ? tooltipItem_ : 0;
        tooltipFollowAt_ = tooltipAnchor_.min;
        Spring* springs[4] = {&tooltipX_, &tooltipY_, &tooltipW_, &tooltipH_};
        const float targets[4] = {pos.x, pos.y, size.x, size.y};
        for (int i = 0; i < 4; ++i)
        {
            if (appearing)
                springs[i]->Snap(targets[i]);
            springs[i]->target = targets[i];
            springs[i]->Step(dt_, Motion{0.24f, 1.0f});
        }
        ctx_.SetNextWindowPos(Vec2(tooltipX_.value, tooltipY_.value - 4.0f * (1.0f - a)), Cond::Always);
        ctx_.SetNextWindowSize(Vec2(std::max(tooltipW_.value, 8.0f), std::max(tooltipH_.value, 8.0f)), Cond::Always);
        WindowOptions o;
        o.flags = esia::WindowFlags_NoInputs | esia::WindowFlags_NoMove | esia::WindowFlags_NoResize | esia::WindowFlags_NoFocus | esia::WindowFlags_NoScroll;
        o.layer = WindowLayer::Tooltip;
        o.padding = Vec2(0, 0);
        o.minSize = Vec2(1, 1);
        ctx_.Begin("##cipher-tooltip", o);
        alpha_.push_back(a);
        esia::Painter p = Painter();
        const Rect r = ctx_.CurrentWindow()->GetRect();
        // the theme's ink as the surface, its canvas as the text: inverted, and changing with the theme
        p.Rect(r, Style().Radius(std::min(10.0f, r.Height() * 0.5f)).Fill(Mix(shown_.ink, shown_.canvas, 0.06f).WithAlpha(1.0f))
                      .Shadow(Color::Black(0.25f), 12.0f, Vec2(0, 4)));
        const Color ink = shown_.canvas.WithAlpha(1.0f);
        p.PushClip(r);
        const float swap = Clamp01(tooltipSwap_.value);
        if (swap < 0.999f && !tooltipPrev_.empty())
            p.Text(r.min + Vec2(10.0f, 6.0f), f, ink.Fade(1.0f - swap), tooltipPrev_, 280.0f);
        p.Text(r.min + Vec2(10.0f, 6.0f), f, ink.Fade(swap), tooltipText_, 280.0f);
        p.PopClip();
        alpha_.pop_back();
        ctx_.End();
    }

    // ------------------------------------------------------------------ notifications
    void Ui::Notify(const Notification& n)
    {
        Toast t;
        t.n = n;
        t.serial = ++toastSerial_;
        t.x.Snap(1.0f);
        t.x.target = 0.0f;
        t.shown.Snap(0.0f);
        t.shown.target = 1.0f;
        toasts_.insert(toasts_.begin(), t);
        if (toasts_.size() > 6)
            toasts_.back().closing = true;
    }

    // Notifications: the newest at the top right, the others under it; each slides in from beyond the display's edge with
    // a little bounce and out again, and can be swiped away; the stack closes up as one leaves. Its close button wears a
    // ring: the time left, running out from the top clockwise - held while the pointer is on the toast.
    void Ui::DrawToasts()
    {
        const Vec2 d = ctx_.DisplaySize();
        const InputState& in = ctx_.Input();
        const float w = 400.0f, gap = 10.0f, margin = 16.0f;
        const float travel = w + margin + 32.0f;   // from its place to fully off the display, its shadow too
        const text::FontRef titleFont{fonts_.textSemibold, m::kFont}, bodyFont{fonts_.text, m::kFontSmall};
        // every toast alike: 400 by 64, its title on one line and its message on the next (cut short with an ellipsis
        // when longer); a hint of its kind's color at the left, the icon in it, the color's edge leaning a little and
        // fading softly into the glass, where the text is; the close ring 32 in from the right
        const float ringOuter = 12.0f, height = 64.0f;
        const float side = 2.0f * m::kStep + 20.0f;   // the close ring's center from the right
        const float textX = 84.0f, textW = w - textX - (side + ringOuter + 12.0f);
        float y = margin;
        for (Toast& t : toasts_)
        {
            t.height = height;
            if (!t.placed)
            {
                t.y.Snap(y);
                t.placed = true;
            }
            t.y.target = y;
            t.y.Step(dt_, kSmooth);
            if (!t.dragging)
                t.x.Step(dt_, t.closing ? kSnappy : kBouncy);
            t.shown.target = t.closing ? 0.0f : 1.0f;
            t.shown.Step(dt_, t.closing ? Motion{0.30f, 1.0f} : kSnappy);
            // a closing toast gives its room back as it leaves
            y += (t.height + gap) * (t.closing ? Clamp01(t.shown.value) : 1.0f);
        }
        for (Toast& t : toasts_)
        {
            const float a = Clamp01(t.shown.value);
            if (a <= 0.002f && t.closing)
                continue;
            char name[32];
            std::snprintf(name, sizeof(name), "##cipher-toast-%d", t.serial);
            // on the pixel grid: everything in it moves as one (its glyphs, snapped to pixels, would shiver against its
            // surfaces as the slide settles)
            const Vec2 pos(std::round((d.x - margin - w + t.x.value * travel) * pixelScale_) / pixelScale_, std::round(t.y.value * pixelScale_) / pixelScale_);
            ctx_.SetNextWindowPos(pos, Cond::Always);
            ctx_.SetNextWindowSize(Vec2(w, t.height), Cond::Always);
            WindowOptions o;
            o.flags = esia::WindowFlags_NoMove | esia::WindowFlags_NoResize | esia::WindowFlags_NoFocus | esia::WindowFlags_NoBringToFront |
                      esia::WindowFlags_NoScroll | (t.closing ? esia::WindowFlags_NoInputs : 0u);
            o.layer = WindowLayer::Overlay;
            o.padding = Vec2(0, 0);
            o.minSize = Vec2(1, 1);
            ctx_.Begin(name, o);
            alpha_.push_back(a);
            Window* win = ctx_.CurrentWindow();
            const Rect r = win->GetRect();
            const bool hovered = ctx_.HoveredWindow() == win;
            // a swipe: the pointer drags it sideways (to the left against a resistance); let go far or fast enough to the
            // right and it leaves, else it springs back - from where it is, as fast as it was thrown
            const Id body = ctx_.GetId("##body");
            ctx_.ItemAdd(body, r, ItemFlags_Background);
            const ButtonResult drag = ctx_.ButtonBehavior(body, r, ButtonFlags_PressOnClick);
            if (drag.pressed && !t.closing)
            {
                t.dragging = true;
                t.grab = in.MousePos().x - t.x.value * travel;
            }
            if (t.dragging && ctx_.ActiveId() == body && in.MouseDown(MouseButton::Left))
            {
                const float dx = in.MousePos().x - t.grab;
                const float nx = (dx >= 0.0f ? dx : dx * 0.3f) / travel;
                t.x.velocity = dt_ > 0.0f ? (nx - t.x.value) / dt_ : 0.0f;
                t.x.value = nx;
                ctx_.SetMouseCursor(MouseCursor::Hand);
            }
            else if (t.dragging)
            {
                t.dragging = false;
                const bool away = t.x.value * travel > w * 0.3f || t.x.velocity * travel > 900.0f;
                t.closing = t.closing || away;
                t.x.target = t.closing ? 1.0f : 0.0f;
            }
            if (!hovered && !t.dragging && !t.closing)
                t.age += dt_;
            if (t.age >= t.n.duration && !t.closing)
            {
                t.closing = true;
                t.x.target = 1.0f;
            }
            esia::Painter p = Painter();
            DrawList& dl = ctx_.WindowDrawList();
            dl.PushClipRect(Rect(Vec2(-64.0f, -64.0f), d + Vec2(64.0f, 64.0f)), false);
            Surface(p, r, m::kRadiusPanel, SurfaceKind::Toast, shown_.raised, 0.6f);
            dl.PopClipRect();
            const Color kc = KindColor(shown_, t.n.kind);
            // the color: a parallelogram drawn as its shadow alone - even inside, its edge softened 16 either way - its
            // slanted edge 56 in at mid-height, leaning 0.35 across for every unit down; its other edges lie well
            // outside the toast, which cuts it to its corners
            const float cy = r.Center().y, lean = 0.35f, past = 40.0f;
            const auto edgeX = [&](float y) { return r.min.x + 56.0f - lean * (y - cy); };
            const Vec2 quad[4] = {Vec2(r.min.x - past, r.min.y - past), Vec2(edgeX(r.min.y - past), r.min.y - past),
                                  Vec2(edgeX(r.max.y + past), r.max.y + past), Vec2(r.min.x - past, r.max.y + past)};
            p.PushMask(r, m::kRadiusPanel);
            p.Polygon(quad, 4, Style().Shadow(kc.Fade(0.28f), 16.0f));
            p.PopMask();
            // the title and the message one line each (the title alone in the middle), the icon and the close ring on
            // the middle line
            const float titleCy = t.n.body.empty() ? cy : r.min.y + 22.0f;
            DrawIcon(p, t.n.icon != Icon::None ? t.n.icon : KindIcon(t.n.kind), Vec2(r.min.x + 30.0f, cy), 22.0f, kc);
            Label(p, r.min.x + textX, titleCy, titleFont, Ink(0.94f), t.n.title, 0.0f, textW);
            if (!t.n.body.empty())
                Label(p, r.min.x + textX, r.min.y + 43.0f, bodyFont, Ink(0.64f), t.n.body, 0.0f, textW);
            // the close button and its ring: the time left, from the top clockwise; the pointer on the toast holds it
            const Vec2 cc(r.max.x - side, r.Center().y);
            const Id cid = ctx_.GetId("##close");
            const Rect cb = Rect::FromCenter(cc, Vec2(ringOuter * 2.0f + 4.0f, ringOuter * 2.0f + 4.0f));
            ctx_.ItemAdd(cid, cb);
            const ButtonResult br = ctx_.ButtonBehavior(cid, cb);
            if (br.hovered)
                ctx_.SetMouseCursor(MouseCursor::Hand);
            const float held = Anim(cid, 0, (hovered || t.dragging) ? 1.0f : 0.0f);
            const float hov = Anim(cid, 1, br.hovered ? 1.0f : 0.0f);
            const float press = Anim(cid, 2, br.held ? 1.0f : 0.0f);
            p.PushScale(cc, 1.0f - 0.08f * press);
            if (hov > 0.004f)
                p.Circle(cc, ringOuter - 0.5f, Style().Fill(Ink(0.08f * hov + 0.04f * press)));
            const float left = Clamp01(1.0f - t.age / std::max(t.n.duration, 0.1f));
            const float ringW = 2.0f, ringR = ringOuter - ringW * 0.5f;
            p.Ring(cc, ringR, ringW, Style().Fill(Ink(0.09f)));
            if (left > 0.0f)
                p.Arc(cc, ringR, ringW, -kPi * 0.5f, 2.0f * kPi * left, Style().Fill(kc.Fade(0.70f + 0.30f * held)));
            DrawIcon(p, Icon::X, cc, 13.0f, Ink(0.42f + 0.18f * held + 0.35f * hov));
            p.PopScale();
            if (br.pressed && !t.closing)
            {
                t.closing = true;
                t.x.target = 1.0f;
            }
            alpha_.pop_back();
            ctx_.End();
        }
        toasts_.erase(std::remove_if(toasts_.begin(), toasts_.end(), [](const Toast& t) { return t.closing && t.shown.value <= 0.002f; }), toasts_.end());
    }

    // A resizable window's corner while the pointer is near it or drags it: a quarter arc round the outside of its rounded
    // corner, concentric with it, 5 out from the edge (in the band that resizes: Context's resize border), glowing a
    // little (brighter while it is dragged). Outside, it never lies over what is in the corner.
    void Ui::ResizeCorner(Id id, const Rect& r, float radius, Color ink)
    {
        struct CornerState
        {
            bool held = false;
        };
        CornerState& cs = ctx_.State<CornerState>(id);
        const InputState& in = ctx_.Input();
        const bool near = !inert_ && ctx_.HoveredWindow() == ctx_.CurrentWindow() && in.MouseValid() && LengthSq(in.MousePos() - r.max) < 30.0f * 30.0f;
        if (near && in.MouseClicked(MouseButton::Left))
            cs.held = true;
        if (!in.MouseDown(MouseButton::Left))
            cs.held = false;
        const float shown = Anim(id, 0, near || cs.held ? 1.0f : 0.0f);
        const float held = Anim(id, 1, cs.held ? 1.0f : 0.0f);
        if (shown <= 0.004f)
            return;
        (void)ink;
        esia::Painter p = Painter();
        DrawList& dl = ctx_.WindowDrawList();
        dl.PushClipRect(Rect(Vec2(-64.0f, -64.0f), ctx_.DisplaySize() + Vec2(64.0f, 64.0f)), false);
        const float inset = 0.16f;   // radians off each straight edge: its round caps stay on the arc
        const Color c = Ink(0.55f + 0.35f * held);
        p.Arc(r.max - Vec2(radius, radius), radius + 5.0f, 2.5f, inset, kPi * 0.5f - 2.0f * inset,
              Style().Fill(c.Fade(shown)).Glow(c, 6.0f, (0.35f + 0.30f * held) * shown));
        dl.PopClipRect();
    }

    // ------------------------------------------------------------------ watermark
    void Ui::DrawWatermark()
    {
        const Vec2 d = ctx_.DisplaySize();
        const InputState& in = ctx_.Input();
        const text::FontRef brand{fonts_.textBold, 12.0f}, value{fonts_.monoSemibold, 12.0f}, unit{fonts_.text, 11.5f};
        // its numbers change twice a second (in real time: a paused animation clock does not stop them)
        const double real = in.Time();
        watermarkMsSum_ += watermark_.cpuMs;
        ++watermarkMsCount_;
        if (watermarkHeld_ < 0.0 || real - watermarkHeld_ >= 0.5)
        {
            watermarkFps_ = watermark_.fps;
            watermarkMs_ = watermarkMsSum_ / (float)watermarkMsCount_;
            watermarkMsSum_ = 0.0f;
            watermarkMsCount_ = 0;
            watermarkHeld_ = real;
        }
        char fps[16], ms[24], clock[16];
        std::snprintf(fps, sizeof(fps), "%.0f", (double)watermarkFps_);
        std::snprintf(ms, sizeof(ms), "%.2f", (double)watermarkMs_);
        const std::time_t now = std::time(nullptr);
        std::tm tmv{};
#if defined(_WIN32)
        localtime_s(&tmv, &now);
#else
        localtime_r(&now, &tmv);
#endif
        std::strftime(clock, sizeof(clock), "%H:%M:%S", &tmv);
        // the parts and their widths: logo, CIPHER, | fps, | ms, | api, | clock
        const char* letters = "CIPHER";
        float brandW = 0.0f;
        for (const char* c = letters; *c; ++c)
            brandW += Measure(brand, std::string_view(c, 1)).x + (c[1] ? 2.2f : 0.0f);
        // the parts' slots: a number's as wide as its usual digits ("000" fps, "0.00" ms), wider only when a digit comes -
        // gliding then; the numbers right-aligned in them, so the digits and the unit after them stay put
        const float widths[4] = {std::max(Measure(value, "000").x, Measure(value, fps).x), std::max(Measure(value, "0.00").x, Measure(value, ms).x),
                                 Measure(unit, watermark_.api).x, Measure(value, clock).x};
        for (int i = 0; i < 4; ++i)
        {
            if (!watermarkMade_)
                watermarkParts_[i].Snap(widths[i]);
            watermarkParts_[i].target = widths[i];
            watermarkParts_[i].Step(dt_, kSmooth);
        }
        watermarkMade_ = true;
        const float fpsNum = watermarkParts_[0].value, msNum = watermarkParts_[1].value, apiW = watermarkParts_[2].value, clockW = watermarkParts_[3].value;
        const float fpsW = fpsNum + 4.0f + Measure(unit, "fps").x;
        const float msW = msNum + 4.0f + Measure(unit, "ms").x;
        const float sep = 25.0f;
        const float width = 14.0f + 16.0f + 8.0f + brandW + sep + fpsW + sep + msW + sep + apiW + sep + clockW + 14.0f;
        const Vec2 size(width, 32.0f);
        const float margin = 16.0f;
        const Vec2 corners[4] = {Vec2(margin, margin), Vec2(d.x - margin - size.x, margin), Vec2(margin, d.y - margin - size.y),
                                 Vec2(d.x - margin - size.x, d.y - margin - size.y)};
        if (watermarkPos_.x < 0.0f)
        {
            watermarkX_.Snap(corners[watermarkCorner_].x);
            watermarkY_.Snap(corners[watermarkCorner_].y);
            watermarkPos_ = corners[watermarkCorner_];
        }
        if (!watermarkDragging_)
        {
            watermarkX_.target = corners[watermarkCorner_].x;
            watermarkY_.target = corners[watermarkCorner_].y;
            watermarkX_.Step(dt_, kBouncy);
            watermarkY_.Step(dt_, kBouncy);
        }
        ctx_.SetNextWindowPos(Vec2(watermarkX_.value, watermarkY_.value), Cond::Always);
        ctx_.SetNextWindowSize(size, Cond::Always);
        WindowOptions o;
        o.flags = esia::WindowFlags_NoMove | esia::WindowFlags_NoResize | esia::WindowFlags_NoFocus | esia::WindowFlags_NoScroll;
        o.layer = WindowLayer::Overlay;
        o.padding = Vec2(0, 0);
        o.minSize = Vec2(1, 1);
        ctx_.Begin("##cipher-watermark", o);
        Window* win = ctx_.CurrentWindow();
        const Rect r = win->GetRect();
        const Id id = ctx_.GetId("##drag");
        ctx_.ItemAdd(id, r);
        const ButtonResult b = ctx_.ButtonBehavior(id, r, ButtonFlags_PressOnClick);
        if (b.pressed)
        {
            watermarkDragging_ = true;
            watermarkGrab_ = in.MousePos() - Vec2(watermarkX_.value, watermarkY_.value);
        }
        if (watermarkDragging_)
        {
            if (in.MouseDown(MouseButton::Left))
            {
                const Vec2 at = in.MousePos() - watermarkGrab_;
                // follows the pointer through a quick spring: dragged, not teleported
                watermarkX_.target = at.x;
                watermarkY_.target = at.y;
                watermarkX_.Step(dt_, Motion{0.08f, 1.0f});
                watermarkY_.Step(dt_, Motion{0.08f, 1.0f});
            }
            else
            {
                // let go: to the nearest corner, keeping its speed
                watermarkDragging_ = false;
                const Vec2 c(watermarkX_.value + size.x * 0.5f, watermarkY_.value + size.y * 0.5f);
                watermarkCorner_ = (c.x > d.x * 0.5f ? 1 : 0) + (c.y > d.y * 0.5f ? 2 : 0);
            }
        }
        if (b.hovered || watermarkDragging_)
            ctx_.SetMouseCursor(watermarkDragging_ ? MouseCursor::ResizeAll : MouseCursor::Hand);
        esia::Painter p = Painter();
        DrawList& dl = ctx_.WindowDrawList();
        dl.PushClipRect(Rect(Vec2(-64.0f, -64.0f), d + Vec2(64.0f, 64.0f)), false);
        const float lift = Anim(id, 0, (b.hovered || watermarkDragging_) ? 1.0f : 0.0f);
        p.PushScale(r.Center(), 1.0f + 0.03f * lift);
        Surface(p, r, r.Height() * 0.5f, SurfaceKind::Watermark, shown_.raised, 0.35f + 0.35f * lift);
        p.PopScale();
        dl.PopClipRect();
        const float cy = r.Center().y;
        float x = r.min.x + 14.0f;
        // the mark: a diamond turning, the theme's two hues across it
        const float spin = (float)time_ * 40.0f;
        p.RegularPolygon(Vec2(x + 8.0f, cy), 7.5f, 4, spin, Style().Radius(1.5f).Fill(Paint::Linear(shown_.accent2, shown_.accent, 45)));
        p.RegularPolygon(Vec2(x + 8.0f, cy), 3.0f, 4, -spin, Style().Fill(shown_.canvas.WithAlpha(0.85f)));
        x += 24.0f;
        for (const char* c = letters; *c; ++c)
        {
            const std::string_view ch(c, 1);
            Label(p, x, cy, brand, Ink(0.92f), ch);
            x += Measure(brand, ch).x + 2.2f;
        }
        x -= 2.2f;
        const auto divider = [&]() {
            x += 12.0f;
            p.Rect(Rect(x, cy - 7.0f, x + 1.0f / pixelScale_, cy + 7.0f), Style().Fill(Ink(0.14f)));
            x += 12.0f + 1.0f / pixelScale_;
        };
        // the frame rate's color follows it through green, amber and red
        const Color fpsTarget = watermarkFps_ >= 100.0f ? shown_.success : (watermarkFps_ >= 50.0f ? shown_.warning : shown_.danger);
        const Color fpsColor = AnimColor(id, 0, fpsTarget, kSmooth);
        divider();
        MorphLabel(p, HashInt(1, id), x + fpsNum, cy, value, fpsColor, fps, 1.0f);
        x += fpsNum + 4.0f;
        Label(p, x, cy, unit, Ink(0.46f), "fps");
        x += Measure(unit, "fps").x;
        divider();
        MorphLabel(p, HashInt(2, id), x + msNum, cy, value, Ink(0.9f), ms, 1.0f);
        x += msNum + 4.0f;
        Label(p, x, cy, unit, Ink(0.46f), "ms");
        x += Measure(unit, "ms").x;
        divider();
        MorphLabel(p, HashInt(3, id), x, cy, unit, Ink(0.70f), watermark_.api);
        x += apiW;
        divider();
        MorphLabel(p, HashInt(4, id), x, cy, value, Ink(0.9f), clock);
        ctx_.End();
    }
}
