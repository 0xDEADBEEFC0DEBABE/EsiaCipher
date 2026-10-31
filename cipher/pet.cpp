// Cipher - a 3D model on a turntable (ModelView) and the pet (HubPet): a model at home on the hub, walking along the
// tops of the windows and the bottom of the display.
#include "cipher.hpp"
#include "model.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace cipher
{
    using namespace esia;
    namespace m = metrics;

    namespace
    {
        struct ViewState
        {
            Spring yaw;              // dragged: follows the pointer; let go: coasts to a stop
            float spin = 0.0f;       // radians a second it coasts at
            float idle = 0.0f;       // seconds since it was last turned: after a while it turns on its own
            float time = 0.0f;       // its animation's clock
            int anim = -2;
            bool made = false;
        };

        // where the pet can stand: the top of a window (or the bottom of the display), from `left` to `right` at `y`
        struct PetSurface
        {
            Id id = 0;
            float left = 0.0f, right = 0.0f, y = 0.0f;
        };

        struct PetState
        {
            bool placed = false;
            Id on = 0;                    // the surface it stands on (0: in the air)
            float x = 0.0f;               // standing: from its surface's left end
            Vec2 feet;                    // where its feet are
            Vec2 v;                       // in the air: its velocity
            Spring yaw, shown;
            // scattered (PetSpec::scattered): grains of what could be seen of it, each a ball of its color moved by
            // physics - gravity, the air, bounces, friction, the pointer's wind - then a spring home. Each has its cell
            // of a grid over the pet: the model shows in a cell while its grain is home, fading as the grain leaves,
            // growing back as it lands (the mask)
            struct Grain
            {
                Vec2 p, v;
                Vec2 home;                // where on it it was, from its feet
                float r = 1.0f, bounce = 0.3f;
                std::uint32_t color = 0;
                int cell = 0;             // its cell of the grid
                Id on = 0;                // resting on a ledge (0: in the air)
                float ox = 0.0f;          // resting: from the ledge's left end
                float delay = 0.0f;       // when it leaves (crumbling) or sets off home
                float since = -1.0f;      // how long since it left or arrived (< 0: not yet)
                bool gone = false;        // it has left the pet
                bool arrived = false;     // it is home again
            };
            std::vector<Grain> grains;
            enum class Grains : std::uint8_t { None, Leaving, Apart, Homing, Settling } grainMode = Grains::None;
            float grainTime = 0.0f;
            std::vector<float> mask;      // how much of the model shows in each cell of the grid
            int maskW = 0, maskH = 0;
            Vec2 maskOrigin, maskSize;    // the grid: its top left from the feet, its size
            Vec2 pointer;                 // last frame's: the pointer's motion is the wind
            bool pointerPlaced = false;
            enum class Mode : std::uint8_t { Idle, Walk, Pat, Cheer, Land, Carried, Fall, Jump } mode = Mode::Idle;
            float modeTime = 0.0f, modeLength = 4.0f;
            float target = 0.0f;          // walking: where to, on its surface
            int exit = 0;                 // walking: then step off its left end (-1) or right end (1), or jump (2)
            Id jumpTo = 0;                // jumping: onto which surface, where on it (from its left end)
            float jumpX = 0.0f, jumpLength = 0.6f, jumpApex = 40.0f;
            Vec2 jumpFrom;
            int anim = -1, from = -1;
            float animTime = 0.0f, fromTime = 0.0f, blend = 1.0f;
            float speed = -1.0f, measuredFor = 0.0f;   // walking, units a second at its height (measured once)
            std::uint32_t seed = 0x2545F491u;
            bool pressed = false, dragging = false;
            Vec2 pressAt, hold, throwV;
            struct Heart
            {
                Vec2 at;   // from its head
                float born;
            };
            std::vector<Heart> hearts;
            double eggSeen = -1.0;
            float Random()
            {
                seed ^= seed << 13;
                seed ^= seed >> 17;
                seed ^= seed << 5;
                return (float)(seed >> 8) / 16777216.0f;
            }
        };
        using PetMode = PetState::Mode;
    }

    // A soft round spot stretched over `r`: `c` in its middle, fading smoothly to nothing at the edge of the ellipse
    // (a texture of one, made once: smooth at any size and any shape).
    void Ui::SoftSpot(DrawList& dl, const Rect& r, Color c)
    {
        if (!spot_)
        {
            constexpr int kSize = 128;
            std::vector<std::uint8_t> px((std::size_t)kSize * kSize * 4);
            for (int y = 0; y < kSize; ++y)
                for (int x = 0; x < kSize; ++x)
                {
                    const float u = ((float)x + 0.5f) / (float)kSize * 2.0f - 1.0f, w = ((float)y + 0.5f) / (float)kSize * 2.0f - 1.0f;
                    const float q = std::max(1.0f - (u * u + w * w), 0.0f);
                    std::uint8_t* o = &px[((std::size_t)y * kSize + (std::size_t)x) * 4];
                    o[0] = o[1] = o[2] = 255;
                    o[3] = (std::uint8_t)std::lround(255.0f * q * q * q);   // (1 - r^2)^3: no edge, no kink
                }
            TextureInfo info;
            info.format = TextureFormat::RGBA8;
            info.width = info.height = kSize;
            spot_ = ctx_.Textures().Create(info, px.data());
        }
        if (c.a <= 0.002f || r.Empty())
            return;
        dl.AddImage(spot_, r, Vec2(0.0f, 0.0f), Vec2(1.0f, 1.0f), c.ToRgba8());
    }

    // A little ball of `color` (RGBA8) round `c`, `r` across: a disc lit from the top left, darker at its rim (a texture
    // of one, made once).
    void Ui::Ball(DrawList& dl, Vec2 c, float r, std::uint32_t color)
    {
        if (!ball_)
        {
            constexpr int kSize = 32;
            std::vector<std::uint8_t> px((std::size_t)kSize * kSize * 4);
            for (int y = 0; y < kSize; ++y)
                for (int x = 0; x < kSize; ++x)
                {
                    const float u = ((float)x + 0.5f) / (float)kSize * 2.0f - 1.0f, w = ((float)y + 0.5f) / (float)kSize * 2.0f - 1.0f;
                    const float d = std::sqrt(u * u + w * w);
                    const float cover = Clamp01((1.0f - d) * (float)kSize * 0.5f + 0.5f);   // a texel of anti-aliasing
                    const float hx = u + 0.38f, hy = w + 0.42f;
                    const float light = 0.82f + 0.18f * Clamp01(1.0f - std::sqrt(hx * hx + hy * hy) / 1.1f);
                    const float rim = 0.88f + 0.12f * Clamp01(1.0f - d * d);
                    std::uint8_t* o = &px[((std::size_t)y * kSize + (std::size_t)x) * 4];
                    o[0] = o[1] = o[2] = (std::uint8_t)std::lround(255.0f * std::min(light * rim, 1.0f));
                    o[3] = (std::uint8_t)std::lround(255.0f * cover);
                }
            TextureInfo info;
            info.format = TextureFormat::RGBA8;
            info.width = info.height = kSize;
            ball_ = ctx_.Textures().Create(info, px.data());
        }
        if ((color >> 24) == 0 || r <= 0.0f)
            return;
        dl.AddImage(ball_, Rect(c.x - r, c.y - r, c.x + r, c.y + r), Vec2(0.0f, 0.0f), Vec2(1.0f, 1.0f), color);
    }

    namespace
    {
        // One step of the grains: in the air - gravity, the air's drag, the pointer's wind, bounces off the ledges they
        // fall onto (from above) and the display's sides; resting on a ledge - sliding to a stop, carried along with
        // it, falling off its ends (or when it goes); going home - a spring to where on the pet they were.
        void StepGrains(PetState& st, const std::vector<PetSurface>& ledges, Vec2 display, float h, Vec2 feet, bool homing, Vec2 wind0,
                        Vec2 wind1, Vec2 windV, bool windy)
        {
            const auto find = [&](Id id) -> const PetSurface* {
                for (const PetSurface& s : ledges)
                    if (s.id == id)
                        return &s;
                return nullptr;
            };
            const Vec2 seg = wind1 - wind0;
            const float segLen2 = std::max(seg.x * seg.x + seg.y * seg.y, 1e-6f);
            constexpr float kReach = 70.0f;   // how far the pointer's wind reaches
            for (PetState::Grain& g : st.grains)
            {
                if (g.arrived || !g.gone)
                {
                    g.p = feet + g.home;
                    g.v = Vec2(0.0f, 0.0f);
                    continue;
                }
                // the pointer's wind: the air along its path dragged with it (the grains it reaches with it)
                if (windy)
                {
                    const float k0 = std::clamp(((g.p.x - wind0.x) * seg.x + (g.p.y - wind0.y) * seg.y) / segLen2, 0.0f, 1.0f);
                    const Vec2 o = g.p - (wind0 + seg * k0);
                    const float dist = std::sqrt(o.x * o.x + o.y * o.y);
                    if (dist < kReach)
                    {
                        const float k = (1.0f - dist / kReach) * (1.0f - dist / kReach);
                        g.v = g.v + (windV - g.v) * std::min(9.0f * k * h, 1.0f);
                        if (g.on && k * (std::fabs(windV.x) + std::fabs(windV.y)) > 90.0f)
                        {
                            g.on = 0;   // blown off where it lay
                            g.v.y = std::min(g.v.y, -40.0f * k);
                        }
                    }
                }
                if (homing && st.grainTime >= g.delay)
                {
                    // home: a spring nearly critically damped (no ledge holds it back)
                    const Vec2 to = feet + g.home - g.p;
                    if (g.on)
                    {
                        g.on = 0;
                        g.v.y -= 60.0f;   // it hops off as it sets out
                    }
                    g.v = g.v + (to * 90.0f - g.v * 17.0f) * h;
                    g.p = g.p + g.v * h;
                    if (to.x * to.x + to.y * to.y < 0.5f && g.v.x * g.v.x + g.v.y * g.v.y < 900.0f)
                    {
                        g.arrived = true;
                        g.since = 0.0f;
                    }
                    continue;
                }
                if (g.on)
                {
                    if (const PetSurface* s = find(g.on))
                    {
                        g.v.x *= std::exp(-10.0f * h);
                        g.ox += g.v.x * h;
                        g.p = Vec2(s->left + g.ox, s->y - g.r);
                        g.v.y = 0.0f;
                        if (g.ox < -g.r || g.ox > s->right - s->left + g.r)
                            g.on = 0;   // off its end
                        else
                            continue;
                    }
                    else
                        g.on = 0;   // its ledge went (a window closed, moved away)
                }
                g.v.y += 1500.0f * h;
                g.v = g.v * std::exp(-0.35f * h);
                const Vec2 before = g.p;
                g.p = g.p + g.v * h;
                if (g.p.x < g.r)
                {
                    g.p.x = g.r;
                    g.v.x = std::fabs(g.v.x) * 0.4f;
                }
                else if (g.p.x > display.x - g.r)
                {
                    g.p.x = display.x - g.r;
                    g.v.x = -std::fabs(g.v.x) * 0.4f;
                }
                // a ledge it fell onto this step: the highest one crossed
                if (g.v.y > 0.0f)
                {
                    const PetSurface* hit = nullptr;
                    for (const PetSurface& s : ledges)
                        if (before.y + g.r <= s.y + 0.5f && g.p.y + g.r >= s.y && g.p.x >= s.left && g.p.x <= s.right && (!hit || s.y < hit->y))
                            hit = &s;
                    if (hit)
                    {
                        g.p.y = hit->y - g.r;
                        if (g.v.y > 70.0f)
                        {
                            g.v.y = -g.v.y * g.bounce;
                            g.v.x *= 0.72f;
                        }
                        else
                        {
                            g.on = hit->id;
                            g.ox = g.p.x - hit->left;
                            g.v.y = 0.0f;
                        }
                    }
                }
            }
        }
    }

    // ------------------------------------------------------------------ the turntable
    // A card spanning the content (a row's surface), the model standing in a pool of light, its shadow under it, its
    // animation playing; a drag turns it (let go, it coasts to a stop; left alone a while, it turns slowly on its own).
    // Loading: a spinner; failed: why; empty: what to do.
    void Ui::ModelView(const char* idLabel, Model& model, int anim, float height)
    {
        const Id id = IdOf(idLabel);
        ViewState& vs = ctx_.State<ViewState>(id);
        const Rect r = ItemRect(Vec2(ContentWidth(), height));
        const Rect card(r.min.x - bleed_, r.min.y, r.max.x, r.max.y);
        AddItem(id, card);
        const ButtonResult b = Behavior(id, card, ButtonFlags_PressOnClick);
        const InputState& in = ctx_.Input();
        const bool ready = model.GetState() == Model::State::Ready;
        const bool held = !inert_ && ready && ctx_.ActiveId() == id && in.MouseDown(MouseButton::Left);
        if (!vs.made)
        {
            vs.yaw.Snap(0.45f);
            vs.made = true;
        }
        if (b.hovered && ready)
            ctx_.SetMouseCursor(held ? MouseCursor::ResizeEW : MouseCursor::Hand);
        if (held)
        {
            const float d = in.MouseDelta().x * 0.012f;
            vs.yaw.value += d;
            vs.yaw.target = vs.yaw.value;
            vs.spin = dt_ > 0.0f ? Mix(vs.spin, d / dt_, 0.5f) : vs.spin;
            vs.idle = 0.0f;
        }
        else
        {
            vs.idle += dt_;
            // coasting, slowing to a stop; after 4 s alone a slow turn of its own fades in
            vs.spin *= std::exp(-dt_ * 2.5f);
            const float own = 0.35f * Smoothstep(4.0f, 6.0f, vs.idle);
            vs.yaw.value += (vs.spin + own) * dt_;
            vs.yaw.target = vs.yaw.value;
        }
        if (anim != vs.anim)
        {
            vs.anim = anim;
            vs.time = 0.0f;
        }
        vs.time += dt_;

        esia::Painter p = Painter();
        DrawList& dl = ctx_.WindowDrawList();
        p.Rect(card, Style().Radius(m::kRadiusPanel).Fill(Ink(0.03f)).Stroke(1.0f / pixelScale_, Ink(0.08f), 0.0f));
        p.PushClip(card.Expanded(-1.0f));
        dl.PushClipRect(card.Expanded(-1.0f));
        const float floorY = card.max.y - 46.0f, cx = card.Center().x;
        const float shown = Clamp01(Anim(id, 0, ready ? 1.0f : 0.0f, kSmooth));
        // the pool of light it stands in, the accent's
        SoftSpot(dl, Rect::FromCenter(Vec2(cx, floorY), Vec2(360.0f, 90.0f)), shown_.accent.Fade(0.16f * (0.4f + 0.6f * shown) * alpha_.back()));
        // scrolled out of sight (its card clipped away): nothing of it would show, so it is neither posed nor drawn
        if (ready && shown > 0.004f && !dl.ClipRect().Empty())
        {
            // its shadow, then the model on it (rising in as it appears)
            SoftSpot(dl, Rect::FromCenter(Vec2(cx, floorY + 1.0f), Vec2(132.0f, 24.0f)), Color::Black(0.42f * shown * alpha_.back()));
            model.Pose(anim, vs.time);
            const float mh = (floorY - card.min.y - 22.0f) * (0.94f + 0.06f * shown);
            const std::uint32_t c = Color(1.0f, 1.0f, 1.0f, shown * alpha_.back()).ToRgba8();
            model.Draw(dl, Rect(cx - mh * 0.5f, floorY - mh + 8.0f * (1.0f - shown), cx + mh * 0.5f, floorY + 8.0f * (1.0f - shown)), vs.yaw.value, 0.10f, c,
                       1.0f / pixelScale_);
        }
        dl.PopClipRect();
        const text::FontRef small{fonts_.text, m::kFontCaption};
        if (!ready)
        {
            const Vec2 c(cx, card.Center().y - 8.0f);
            if (model.GetState() == Model::State::Loading)
            {
                const float spin = (float)time_ * 5.0f;
                p.Ring(c, 14.0f, 2.0f, Style().Fill(Ink(0.08f)));
                p.Arc(c, 14.0f, 2.0f, spin, 1.6f, Style().Fill(shown_.accent));
                Label(p, cx, c.y + 34.0f, small, Ink(0.5f), "Reading the model...", 0.5f);
            }
            else if (model.GetState() == Model::State::Failed)
            {
                DrawIcon(p, Icon::TriangleAlert, c, 22.0f, shown_.danger);
                Label(p, cx, c.y + 34.0f, small, Ink(0.6f), model.Error(), 0.5f, card.Width() - 48.0f);
            }
            else
            {
                DrawIcon(p, Icon::Cuboid, c, 22.0f, Ink(0.35f));
                Label(p, cx, c.y + 34.0f, small, Ink(0.45f), "No model yet: load a .glb file", 0.5f);
            }
        }
        else
        {
            // what it is made of, at the foot of the card
            char info[96];
            std::snprintf(info, sizeof(info), "%d triangles  \xC2\xB7  %d joints  \xC2\xB7  %d animations", model.Triangles(), model.Joints(), model.Animations());
            Label(p, card.min.x + 14.0f, card.max.y - 16.0f, small, Ink(0.42f), info);
            DrawIcon(p, Icon::Rotate3d, Vec2(card.max.x - 22.0f, card.max.y - 16.0f), 15.0f, Ink(0.32f + 0.3f * Clamp01(Anim(id, 1, b.hovered ? 1.0f : 0.0f))));
        }
        p.PopClip();
    }

    // ------------------------------------------------------------------ the pet
    // At home on the hub's top edge. It strolls, stands about looking at the pointer, now and then walks off an end and
    // falls onto whatever is below - another window's top, or the bottom of the display - and from there jumps back up
    // onto a window above (the hub first). A click pats it (it reacts, a heart floats up); a drag picks it up (it
    // dangles under the pointer) and lets it fall where it is dropped - thrown, as fast as it was moving. The footer's
    // secret makes it cheer. Every change of animation is a crossfade; it turns on a spring. Its window is above the
    // windows it walks on and under the menus and notifications that come later.
    void Ui::HubPet(Model& model, const PetSpec& spec)
    {
        const Id pid = HashLabel("##cipher-pet", 0x50455421u);
        PetState& st = ctx_.State<PetState>(pid);
        const bool ready = model.GetState() == Model::State::Ready;
        const float H = spec.height, half = H * 0.18f;
        const Vec2 d = ctx_.DisplaySize();
        WindowOptions o;
        o.flags = esia::WindowFlags_NoMove | esia::WindowFlags_NoResize | esia::WindowFlags_NoScroll | esia::WindowFlags_NoFocus | esia::WindowFlags_NoBringToFront;
        o.layer = WindowLayer::Overlay;
        o.padding = Vec2(0.0f, 0.0f);
        o.minSize = Vec2(1.0f, 1.0f);

        // ---- where it can stand: the hub's top (not as a pill), the windows' of the frame before, the display's bottom;
        // their rounded corners left out, room above them for it
        std::vector<PetSurface> surfaces;
        const Id hubId = HashLabel("##cipher-hub-top", 0x48554254u), floorId = HashLabel("##cipher-floor", 0x464C4F52u);
        const Rect hub = hubRect_;
        if (!hubContent_.Empty() && hubResizable_)
            surfaces.push_back({hubId, hub.min.x + m::kRadiusWindow + half, hub.max.x - m::kRadiusWindow - half, hub.min.y});
        for (const Floating& f : floatingPrev_)
            surfaces.push_back({f.id, f.rect.min.x + m::kRadiusWindow + half, f.rect.max.x - m::kRadiusWindow - half, f.rect.min.y});
        surfaces.erase(std::remove_if(surfaces.begin(), surfaces.end(), [&](const PetSurface& s) { return s.right <= s.left || s.y < H * 1.05f || s.y > d.y - 8.0f; }),
                       surfaces.end());
        surfaces.push_back({floorId, half + 10.0f, d.x - half - 10.0f, d.y - 5.0f});
        const auto find = [&](Id id) -> const PetSurface* {
            for (const PetSurface& s : surfaces)
                if (s.id == id)
                    return &s;
            return nullptr;
        };
        // the first surface under `x` from `y` down
        const auto below = [&](float x, float y) -> const PetSurface* {
            const PetSurface* best = nullptr;
            for (const PetSurface& s : surfaces)
                if (x >= s.left && x <= s.right && s.y >= y - 0.5f && (!best || s.y < best->y))
                    best = &s;
            return best;
        };

        st.shown.target = ready && spec.shown && !hubContent_.Empty() ? 1.0f : 0.0f;
        st.shown.Step(dt_, kSmooth);
        const float vis = Clamp01(st.shown.value) * alpha_.back();
        if (!ready || vis <= 0.003f)
        {
            // its window keeps its place among the windows: under whatever opens later
            st.pressed = st.dragging = false;
            o.flags |= esia::WindowFlags_NoInputs;
            ctx_.SetNextWindowPos(Vec2(-64.0f, -64.0f), Cond::Always);
            ctx_.SetNextWindowSize(Vec2(1.0f, 1.0f), Cond::Always);
            ctx_.Begin("##cipher-pet", o);
            ctx_.End();
            return;
        }
        const InputState& in = ctx_.Input();
        if (!st.placed)
        {
            // home: the hub, a third of the way along (clear of the notifications at the display's top right)
            const PetSurface* home = find(hubId);
            if (!home)
                home = &surfaces.back();
            st.on = home->id;
            st.x = (home->right - home->left) * 0.3f;
            st.yaw.Snap(0.3f);
            st.placed = true;
        }
        if (st.speed < 0.0f || st.measuredFor != H)
        {
            const float mh = std::max(model.Height(), 1e-3f);
            const float s = spec.walk >= 0 ? model.StanceSpeed(spec.walk) : 0.0f;
            st.speed = s > 0.0f ? s * H / mh : H * 0.45f;
            st.measuredFor = H;
        }
        const auto setMode = [&](PetMode mode, float length) {
            st.mode = mode;
            st.modeTime = 0.0f;
            st.modeLength = length;
        };
        const auto lengthOf = [&](int a, float otherwise) { return a >= 0 ? model.AnimationLength(a) : otherwise; };
        const auto airborne = [&](Vec2 v) {
            st.on = 0;
            st.v = v;
            setMode(PetMode::Fall, 1e9f);
        };
        const auto landOn = [&](const PetSurface& s) {
            st.on = s.id;
            st.x = std::clamp(st.feet.x - s.left, 0.0f, s.right - s.left);
            st.v = Vec2(0.0f, 0.0f);
            setMode(PetMode::Land, std::min(lengthOf(spec.land, 0.6f), 1.0f));
        };
        // standing: on its surface (gone - a window closed, the hub a pill - it falls from where it was)
        if (st.on)
        {
            if (const PetSurface* s = find(st.on))
            {
                st.x = std::clamp(st.x, 0.0f, s->right - s->left);
                st.feet = Vec2(s->left + st.x, s->y);
            }
            else
                airborne(Vec2(0.0f, 0.0f));
        }
        // the footer's secret: it cheers (where it stands)
        if (hubEgg_ > st.eggSeen && hubEgg_ > 0.0)
        {
            st.eggSeen = hubEgg_;
            if (spec.cheer >= 0 && st.on)
                setMode(PetMode::Cheer, lengthOf(spec.cheer, 0.0f) + lengthOf(spec.cheerEnd, 0.0f));
        }
        st.modeTime += dt_;
        // scattered: it crumbles into grains from its head down; asked back, they fly home from its feet up and it shows
        // again where each one lands; asked again on the way, those home crumble once more, those flying burst out
        using Grains = PetState::Grains;
        const Vec2 center(st.feet.x, st.feet.y - H * 0.5f);
        const auto burst = [&](PetState::Grain& g, std::uint32_t seed) {
            const auto rnd = [&seed]() {
                seed ^= seed << 13;
                seed ^= seed >> 17;
                seed ^= seed << 5;
                return (float)(seed >> 8) / 16777216.0f;
            };
            Vec2 dir = g.p - center + Vec2((rnd() - 0.5f) * H * 0.3f, (rnd() - 0.5f) * H * 0.3f);
            const float l = std::max(std::sqrt(dir.x * dir.x + dir.y * dir.y), 1e-3f);
            dir = dir * (1.0f / l);
            g.v = dir * (H * (1.4f + 2.6f * rnd())) + Vec2(0.0f, -H * (0.8f + 1.8f * rnd()));
            g.on = 0;
            g.arrived = false;
            g.gone = true;
            g.since = 0.0f;
        };
        const auto heightOf = [&](const PetState::Grain& g) { return Clamp01(-g.home.y / std::max(H, 1.0f)); };   // 0 feet .. 1 head
        if (spec.scattered && (st.grainMode == Grains::Homing || st.grainMode == Grains::Settling))
        {
            std::uint32_t seed = 0x9E3779B9u ^ (std::uint32_t)(time_ * 1000.0);
            for (PetState::Grain& g : st.grains)
            {
                seed += 0x6D2B79F5u;
                if (g.arrived)
                {
                    // home: it leaves again in its turn
                    g.arrived = g.gone = false;
                    g.since = -1.0f;
                    g.delay = 0.38f * (1.0f - heightOf(g)) + 0.08f * (float)((seed >> 9) & 1023u) / 1023.0f;
                }
                else
                    burst(g, seed);   // on its way home: out again from where it is
            }
            st.grainMode = Grains::Leaving;
            st.grainTime = 0.0f;
        }
        if (!spec.scattered && (st.grainMode == Grains::Leaving || st.grainMode == Grains::Apart))
        {
            std::uint32_t seed = 0x85EBCA6Bu;
            for (PetState::Grain& g : st.grains)
            {
                seed = seed * 1664525u + 1013904223u;
                if (!g.gone)
                {
                    g.arrived = true;   // it never left
                    g.since = 10.0f;
                }
                else
                {
                    g.arrived = false;
                    g.since = -1.0f;
                    g.delay = 0.55f * heightOf(g) + 0.12f * (float)(seed >> 8) / 16777216.0f;   // home from the feet up
                }
            }
            st.grainMode = Grains::Homing;
            st.grainTime = 0.0f;
        }
        const bool apart = spec.scattered || st.grainMode != Grains::None;
        if (apart)
            o.flags |= esia::WindowFlags_NoInputs;   // nothing there to take the pointer: the windows under it do

        // ---- its window (what takes the pointer), the pointer on it
        const Rect box(st.feet.x - H * 0.36f, st.feet.y - H * 0.98f, st.feet.x + H * 0.36f, st.feet.y + 2.0f);
        ctx_.SetNextWindowPos(box.min, Cond::Always);
        ctx_.SetNextWindowSize(box.Size(), Cond::Always);
        ctx_.Begin("##cipher-pet", o);
        const Id bid = ctx_.GetId("##body");
        ctx_.ItemAdd(bid, box);
        const ButtonResult b = ctx_.ButtonBehavior(bid, box, ButtonFlags_PressOnClick);
        if ((b.hovered || st.dragging) && !apart)
            ctx_.SetMouseCursor(st.dragging ? MouseCursor::ResizeAll : MouseCursor::Hand);
        if (b.pressed && !apart)
        {
            st.pressed = true;
            st.pressAt = in.MousePos();
        }
        if (st.pressed && in.MouseDown(MouseButton::Left))
        {
            // a drag picks it up: it hangs from where it was taken, its feet under the pointer
            if (!st.dragging && LengthSq(in.MousePos() - st.pressAt) > 6.0f * 6.0f)
            {
                st.dragging = true;
                st.on = 0;
                st.hold = Vec2(st.feet.x - st.pressAt.x, H * 0.82f);
                st.throwV = Vec2(0.0f, 0.0f);
                setMode(PetMode::Carried, 1e9f);
            }
            if (st.dragging)
            {
                const Vec2 to = in.MousePos() + st.hold;
                if (dt_ > 0.0f)
                    st.throwV = st.throwV * 0.6f + (to - st.feet) * (0.4f / dt_);
                st.feet = to;
            }
        }
        else if (st.pressed)
        {
            st.pressed = false;
            if (st.dragging)
            {
                // let go: thrown as fast as it was moving (a little of it), falling from there
                st.dragging = false;
                airborne(Vec2(std::clamp(st.throwV.x * 0.5f, -700.0f, 700.0f), std::clamp(st.throwV.y * 0.5f, -700.0f, 400.0f)));
            }
            else if (st.on && st.mode != PetMode::Cheer)
            {
                // a pat: it reacts, a heart floats up
                ++hubPats_;
                st.hearts.push_back({Vec2((st.Random() - 0.5f) * H * 0.3f, 0.0f), (float)time_});
                if (spec.pat >= 0)
                    setMode(PetMode::Pat, lengthOf(spec.pat, 0.0f));
            }
        }

        // ---- what it does
        const PetSurface* here = st.on ? find(st.on) : nullptr;
        float yawTarget = st.yaw.target;
        switch (st.mode)
        {
        case PetMode::Idle:
        {
            // it looks at the pointer when it is near; after a while it goes somewhere
            const float dx = in.MousePos().x - st.feet.x;
            const bool near = in.MouseValid() && std::fabs(dx) < 260.0f && std::fabs(in.MousePos().y - st.feet.y) < 220.0f;
            yawTarget = near ? std::clamp(dx / 240.0f, -0.75f, 0.75f) : 0.3f;
            if (apart)
                st.modeTime = std::min(st.modeTime, st.modeLength - 1.5f);   // apart, it stays: a while yet once whole
            if (st.modeTime < st.modeLength || !here || spec.walk < 0)
                break;
            const float width = here->right - here->left, r = st.Random();
            // up: the surfaces above within a leap (the hub first), down: off an end
            const PetSurface* up = nullptr;
            for (const PetSurface& s : surfaces)
                if (s.y < here->y - 24.0f && here->y - s.y < 600.0f && (!up || s.id == hubId || (up->id != hubId && s.y > up->y)))
                    up = &s;
            const bool home = here->id == hubId, floor = here->id == floorId;
            st.exit = 0;
            if (up && (floor ? r < 0.65f : (!home && r < 0.45f)))
            {
                // to the spot under where it will land (or the nearest end), then up
                st.jumpTo = up->id;
                st.jumpX = std::clamp(st.feet.x + (st.Random() - 0.5f) * 160.0f, up->left, up->right) - up->left;
                st.target = std::clamp(up->left + st.jumpX, here->left, here->right) - here->left;
                st.exit = 2;
            }
            else if (!floor && r > (home ? 0.78f : 0.75f))
            {
                // off the nearer end
                st.exit = st.x < width * 0.5f ? -1 : 1;
                st.target = st.exit < 0 ? 0.0f : width;
            }
            else
            {
                float t = st.x;
                for (int i = 0; i < 8 && std::fabs(t - st.x) < 80.0f; ++i)
                    t = width * st.Random();
                st.target = t;
            }
            setMode(PetMode::Walk, 1e9f);
            break;
        }
        case PetMode::Walk:
        {
            if (!here)
                break;
            if (apart)
            {
                setMode(PetMode::Idle, 2.0f);
                break;
            }
            const float dir = st.target > st.x ? 1.0f : -1.0f;
            yawTarget = dir * 1.35f;   // most of a quarter turn: its face still shows
            // it sets off once it has turned that way
            const float go = Smoothstep(0.15f, 0.45f, st.modeTime);
            st.x += dir * std::min(st.speed * go * dt_, std::fabs(st.target - st.x));
            st.feet = Vec2(here->left + st.x, here->y);
            if (std::fabs(st.target - st.x) < 0.5f)
            {
                if (st.exit == 1 || st.exit == -1)
                    airborne(Vec2((float)st.exit * st.speed, -90.0f));   // a little hop off the end
                else if (st.exit == 2 && find(st.jumpTo))
                {
                    const PetSurface& to = *find(st.jumpTo);
                    const float gap = here->y - to.y, across = std::fabs(to.left + st.jumpX - st.feet.x);
                    st.jumpFrom = st.feet;
                    st.jumpLength = 0.42f + gap / 1100.0f + across / 1500.0f;
                    st.jumpApex = std::max(gap * 0.35f + 40.0f, 40.0f);
                    st.on = 0;
                    setMode(PetMode::Jump, st.jumpLength);
                }
                else
                    setMode(PetMode::Idle, 3.0f + 5.0f * st.Random());
            }
            break;
        }
        case PetMode::Pat:
        case PetMode::Cheer:
        case PetMode::Land:
            yawTarget = st.mode == PetMode::Land ? st.yaw.target * 0.5f : 0.0f;
            if (st.modeTime >= st.modeLength)
                setMode(PetMode::Idle, 3.0f + 4.0f * st.Random());
            break;
        case PetMode::Carried:
            yawTarget = std::clamp(-st.throwV.x * 0.0015f, -0.7f, 0.7f);
            break;
        case PetMode::Fall:
        {
            // gravity; the horizontal speed fades; it lands on the first surface it passes
            const float y0 = st.feet.y;
            st.v.y += 2200.0f * dt_;
            st.v.x *= std::exp(-dt_ * 1.2f);
            st.feet = st.feet + st.v * dt_;
            st.feet.x = std::clamp(st.feet.x, half + 10.0f, d.x - half - 10.0f);
            yawTarget = std::clamp(st.v.x * 0.003f, -1.0f, 1.0f);
            const PetSurface* hit = nullptr;
            if (st.v.y > 0.0f)
                for (const PetSurface& s : surfaces)
                    if (st.feet.x >= s.left - half && st.feet.x <= s.right + half && s.y >= y0 - 0.5f && s.y <= st.feet.y && (!hit || s.y < hit->y))
                        hit = &s;
            if (hit)
            {
                st.feet.y = hit->y;
                landOn(*hit);
            }
            break;
        }
        case PetMode::Jump:
        {
            // an arc onto where it is going (that surface moving, it follows it)
            const PetSurface* to = find(st.jumpTo);
            if (!to)
            {
                airborne(Vec2(0.0f, 0.0f));
                break;
            }
            const Vec2 at(to->left + std::min(st.jumpX, to->right - to->left), to->y);
            const float s = Clamp01(st.modeTime / st.jumpLength);
            st.feet = Mix(st.jumpFrom, at, s) - Vec2(0.0f, st.jumpApex * 4.0f * s * (1.0f - s));
            yawTarget = at.x > st.jumpFrom.x ? 0.9f : -0.9f;
            if (s >= 1.0f)
            {
                st.feet = at;
                landOn(*to);
            }
            break;
        }
        }
        st.yaw.target = yawTarget;
        if (!apart)
            st.yaw.Step(dt_, Motion{0.45f, 0.9f});   // apart: as it was (its grains come home to it as they left it)

        // ---- its animation, a crossfade from the one before
        int want = spec.idle;
        switch (st.mode)
        {
        case PetMode::Walk: want = spec.walk; break;
        case PetMode::Pat: want = spec.pat; break;
        case PetMode::Land: want = spec.land; break;
        case PetMode::Carried: want = spec.carried; break;
        case PetMode::Fall: want = spec.fall >= 0 ? spec.fall : spec.carried; break;
        case PetMode::Jump: want = spec.jump >= 0 ? spec.jump : spec.carried; break;
        case PetMode::Cheer: want = st.modeTime < lengthOf(spec.cheer, 0.0f) || spec.cheerEnd < 0 ? spec.cheer : spec.cheerEnd; break;
        default: break;
        }
        if (want < 0)
            want = spec.idle;
        if (!apart)
        {
            // apart, its pose stays as it was
            if (want != st.anim)
            {
                st.from = st.anim;
                st.fromTime = st.animTime;
                st.anim = want;
                st.animTime = 0.0f;
                st.blend = st.from >= 0 ? 0.0f : 1.0f;
            }
            st.animTime += dt_;
            st.fromTime += dt_;
            st.blend = std::min(st.blend + dt_ / (st.mode == PetMode::Land || st.mode == PetMode::Jump ? 0.15f : 0.3f), 1.0f);
        }
        // the one-shot animations hold their last frame instead of starting over
        float t = st.animTime;
        if (st.mode == PetMode::Pat || st.mode == PetMode::Cheer || st.mode == PetMode::Land)
            t = std::min(t, std::max(model.AnimationLength(st.anim) - 1e-3f, 0.0f));
        model.Pose(st.anim, t, st.from, st.fromTime, Smoothstep(0.0f, 1.0f, st.blend));

        // ---- drawing: its shadow on the surface under it (smaller and fainter the higher it is, and as much as there is of
        // it), the model (masked where it has crumbled into grains), the grains, the hearts
        DrawList& dl = ctx_.WindowDrawList();
        dl.PushClipRect(Rect(Vec2(-64.0f, -64.0f), d + Vec2(64.0f, 64.0f)), false);
        constexpr float kFadeOut = 0.07f, kGrowIn = 0.09f;   // a cell as its grain leaves, as it lands: quick, so that
                                                              // little of the model is ever half there
        Model::Mask mask;
        float wholeness = 1.0f;
        if (st.grainMode != Grains::None && st.maskW > 0 && !st.grains.empty())
        {
            // each grain's cell: the model there while it is home, fading as it leaves, growing as it lands; a cell
            // without one as its neighbours (twice over), else as the whole
            std::fill(st.mask.begin(), st.mask.end(), -1.0f);
            const bool homeward = st.grainMode == Grains::Homing || st.grainMode == Grains::Settling;
            float sum = 0.0f;
            for (const PetState::Grain& g : st.grains)
            {
                const float m = homeward ? (g.arrived ? Clamp01(g.since / kGrowIn) : 0.0f) : (!g.gone ? 1.0f : Clamp01(1.0f - g.since / kFadeOut));
                st.mask[(std::size_t)g.cell] = m;
                sum += m;
            }
            wholeness = sum / (float)st.grains.size();
            for (int pass = 0; pass < 2; ++pass)
                for (int y = 0; y < st.maskH; ++y)
                    for (int x = 0; x < st.maskW; ++x)
                    {
                        float& c = st.mask[(std::size_t)y * (std::size_t)st.maskW + (std::size_t)x];
                        if (c >= 0.0f)
                            continue;
                        float best = -1.0f;
                        for (int dy = -1; dy <= 1; ++dy)
                            for (int dx = -1; dx <= 1; ++dx)
                            {
                                const int nx = x + dx, ny = y + dy;
                                if (nx >= 0 && ny >= 0 && nx < st.maskW && ny < st.maskH)
                                    best = std::max(best, st.mask[(std::size_t)ny * (std::size_t)st.maskW + (std::size_t)nx]);
                            }
                        if (best >= 0.0f)
                            c = -2.0f - best;   // set after the pass (not spreading further within it)
                    }
            for (float& c : st.mask)
                c = c <= -2.0f ? -2.0f - c : (c < 0.0f ? wholeness : c);
            mask.cells = st.mask.data();
            mask.w = st.maskW;
            mask.h = st.maskH;
            mask.area = Rect(st.feet + st.maskOrigin, st.feet + st.maskOrigin + st.maskSize);
            mask.outside = wholeness;
            mask.hard = true;
        }
        if (const PetSurface* under = st.on ? find(st.on) : below(st.feet.x, st.feet.y))
        {
            const float up = std::max(under->y - st.feet.y, 0.0f);
            const float k = Clamp01(1.0f - up / (H * 2.5f)) * wholeness;
            if (k > 0.01f)
                SoftSpot(dl, Rect::FromCenter(Vec2(st.feet.x, under->y + 1.0f), Vec2(H * 0.62f * (0.5f + 0.5f * k), H * 0.15f * (0.5f + 0.5f * k))),
                         Color::Black(0.36f * k * vis));
        }
        esia::Painter p = Painter();
        p.SetAlpha(vis);
        // the model: whole, or as much of it as its grains leave (asked to scatter, it is read into grains as it is drawn
        // this frame)
        const Rect modelBox(st.feet.x - H * 0.5f, st.feet.y - H, st.feet.x + H * 0.5f, st.feet.y);
        const std::uint32_t modelColor = Color(1.0f, 1.0f, 1.0f, vis).ToRgba8();
        if (st.grainMode == Grains::None)
            model.Draw(dl, modelBox, st.yaw.value, 0.10f, modelColor, 1.0f / pixelScale_);
        else if (wholeness > 0.002f)
            model.Draw(dl, modelBox, st.yaw.value, 0.10f, modelColor, 1.0f / pixelScale_, &mask);
        if (spec.scattered && st.grainMode == Grains::None)
        {
            // little balls, one for each cell of what can be seen of it - a sixtieth of its height across - leaving it
            // from its head down
            const float cell = std::max(1.5f, H / 60.0f);
            static thread_local std::vector<Model::Point> points;
            int gw = 0, gh = 0;
            const Rect area = model.Sample(cell, points, gw, gh);
            st.grains.clear();
            st.grains.reserve(points.size());
            st.maskW = gw;
            st.maskH = gh;
            st.maskOrigin = area.min - st.feet;
            st.maskSize = area.Size();
            st.mask.assign((std::size_t)std::max(gw * gh, 0), 1.0f);
            std::uint32_t seed = 0x2545F491u;
            for (const Model::Point& pt : points)
            {
                PetState::Grain g;
                g.p = pt.at;
                g.home = pt.at - st.feet;
                g.color = pt.color;
                g.cell = pt.index;
                seed ^= seed << 13;
                seed ^= seed >> 17;
                seed ^= seed << 5;
                const float r1 = (float)(seed >> 8) / 16777216.0f;
                g.r = cell * (0.42f + 0.22f * r1);
                g.bounce = 0.2f + 0.25f * (float)((seed >> 3) & 255u) / 255.0f;
                g.delay = 0.38f * (1.0f - heightOf(g)) + 0.08f * (float)((seed >> 11) & 1023u) / 1023.0f;
                st.grains.push_back(g);
            }
            st.grainMode = st.grains.empty() ? Grains::None : Grains::Leaving;
            st.grainTime = 0.0f;
        }
        // the grains: leaving in their turn, moved (in steps of at most 1/120 s of their own clock), drawn
        if (st.grainMode != Grains::None)
        {
            const float speed = std::clamp(spec.scatterSpeed, 0.02f, 8.0f), gdt = dt_ * speed;
            st.grainTime += gdt;
            for (PetState::Grain& g : st.grains)
                if (g.since >= 0.0f)
                    g.since += gdt;
            if (st.grainMode == Grains::Leaving)
            {
                std::uint32_t seed = 0x51ED270Bu ^ (std::uint32_t)(time_ * 1000.0);
                bool all = true;
                for (PetState::Grain& g : st.grains)
                {
                    seed += 0x6D2B79F5u;
                    if (!g.gone && st.grainTime >= g.delay)
                        burst(g, seed);
                    all = all && g.gone;
                }
                if (all)
                    st.grainMode = Grains::Apart;
            }
            // the ledges they can land on: the tops of the hub and the windows, the display's bottom
            std::vector<PetSurface> ledges;
            if (!hubContent_.Empty())
                ledges.push_back({hubId, hub.min.x + 8.0f, hub.max.x - 8.0f, hub.min.y});
            for (const Floating& f : floatingPrev_)
                ledges.push_back({f.id, f.rect.min.x + 8.0f, f.rect.max.x - 8.0f, f.rect.min.y});
            ledges.push_back({floorId, 0.0f, d.x, d.y - 2.0f});
            // the pointer's wind: its motion this frame
            bool windy = false;
            Vec2 windV, from = st.pointer;
            if (in.MouseValid())
            {
                const Vec2 m = in.MousePos();
                if (st.pointerPlaced && dt_ > 0.0f)
                {
                    windV = (m - st.pointer) * (1.0f / gdt);   // on the grains' clock: they go along with it on the screen
                    const float fast = std::sqrt(windV.x * windV.x + windV.y * windV.y), most = 3000.0f / std::min(speed, 1.0f);
                    if (fast > most)
                        windV = windV * (most / fast);
                    windy = fast > 20.0f;
                }
                st.pointer = m;
                st.pointerPlaced = true;
            }
            else
                st.pointerPlaced = false;
            const int steps = std::clamp((int)std::ceil(gdt * 120.0f), 1, 24);
            for (int i = 0; i < steps; ++i)
                StepGrains(st, ledges, d, gdt / (float)steps, st.feet, st.grainMode == Grains::Homing, from, st.pointer, windV, windy);
            // home: once all have arrived (the stragglers after 3.5 s go straight there), the last cells grow in
            if (st.grainMode == Grains::Homing)
            {
                bool all = true;
                for (const PetState::Grain& g : st.grains)
                    all = all && g.arrived;
                if (!all && st.grainTime > 3.5f)
                    for (PetState::Grain& g : st.grains)
                        if (!g.arrived)
                        {
                            g.arrived = true;
                            g.since = 0.0f;
                            g.p = st.feet + g.home;
                        }
                if (all || st.grainTime > 3.5f)
                    st.grainMode = Grains::Settling;
            }
            if (st.grainMode == Grains::Settling)
            {
                bool grown = true;
                for (const PetState::Grain& g : st.grains)
                    grown = grown && g.since >= kGrowIn;
                if (grown)
                {
                    st.grainMode = Grains::None;
                    st.grains.clear();
                }
            }
            // a grain home shows as little as the model shows there
            const bool homeward = st.grainMode == Grains::Homing || st.grainMode == Grains::Settling;
            for (const PetState::Grain& g : st.grains)
            {
                float a = 1.0f;
                if (homeward ? g.arrived : !g.gone)
                    a = homeward ? 1.0f - Clamp01(g.since / kGrowIn) : 0.0f;
                else if (!homeward)
                    a = Clamp01(g.since / kFadeOut);   // leaving: it shows as the model there fades
                const std::uint32_t alpha = (std::uint32_t)std::lround(255.0f * a * vis);
                if (alpha)
                    Ball(dl, g.p, g.r, (g.color & 0x00FFFFFFu) | (alpha << 24));
            }
        }
        // the hearts: up from its head, swaying, fading
        Vec2 head(st.feet.x, st.feet.y - H);
        model.Where("Head", head);
        st.hearts.erase(std::remove_if(st.hearts.begin(), st.hearts.end(), [&](const PetState::Heart& h) { return (float)time_ - h.born > 1.4f; }), st.hearts.end());
        for (const PetState::Heart& h : st.hearts)
        {
            const float age = ((float)time_ - h.born) / 1.4f;
            const Vec2 at = head + h.at + Vec2(std::sin(age * 9.0f) * 4.0f, -18.0f - 46.0f * EaseOutCubic(age));
            const float a = Smoothstep(0.0f, 0.12f, age) * (1.0f - Smoothstep(0.55f, 1.0f, age));
            IconMotion mo;
            mo.scale = 0.7f + 0.3f * EaseOutBack(Clamp01(age * 3.0f));
            DrawIconVector(p, Icon::Heart, at, 16.0f, Color::Hex(0xFF6FA5).Fade(a), mo);
        }
        dl.PopClipRect();
        ctx_.End();
    }
}
