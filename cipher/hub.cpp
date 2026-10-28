// Cipher - the hub window: title bar, a sidebar of tabs in sections over the profile card, the selected tab's content,
// element rows (cipher.hpp).
#include "cipher.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <vector>

namespace cipher
{
    using namespace esia;
    namespace m = metrics;

    namespace
    {
        constexpr float kPillHeight = 40.0f;
        constexpr float kFooter = 40.0f;   // the footer's height

        // a tab's content: in while it is selected; after, it fades out (drawn, taking no input) as the next one comes
        // in - up when the selection moved down the list, down when it moved up
        struct TabFade
        {
            Spring shown;
            float dir = 1.0f;
            bool made = false, selected = false;
        };

        Vec2 Unit(Vec2 v)
        {
            const float l = Length(v);
            return l > 1e-6f ? v * (1.0f / l) : Vec2(0.0f, 0.0f);
        }

        // The outline of a rounded rectangle as one closed line: from the top of its left side round its corners'
        // arcs clockwise, back to where it started
        void RoundedOutline(const Rect& r, float radius, std::vector<Vec2>& out)
        {
            const float rr = std::min(radius, std::min(r.Width(), r.Height()) * 0.5f);
            const Vec2 centers[4] = {Vec2(r.min.x + rr, r.min.y + rr), Vec2(r.max.x - rr, r.min.y + rr), Vec2(r.max.x - rr, r.max.y - rr),
                                     Vec2(r.min.x + rr, r.max.y - rr)};
            constexpr int kArc = 8;
            out.clear();
            for (int c = 0; c < 4; ++c)
                for (int i = 0; i <= kArc; ++i)
                {
                    const float a = kPi * (1.0f + 0.5f * (float)c + 0.5f * (float)i / (float)kArc);   // y down: clockwise
                    out.push_back(centers[c] + Vec2(std::cos(a), std::sin(a)) * rr);
                }
            out.push_back(out.front());
        }

        // `poly`'s corners rounded, each by its own radius (0: sharp), as points along the outline
        int RoundCorners(const Vec2* poly, const float* radii, int n, Vec2* out, int maxOut)
        {
            int k = 0;
            for (int i = 0; i < n; ++i)
            {
                const Vec2 v = poly[i], prev = poly[(i + n - 1) % n], next = poly[(i + 1) % n];
                const float rad = radii[i];
                if (rad <= 0.0f)
                {
                    if (k < maxOut)
                        out[k++] = v;
                    continue;
                }
                const Vec2 d0 = Unit(v - prev), d1 = Unit(next - v);
                const float phi = std::acos(std::clamp(-Dot(d0, d1), -1.0f, 1.0f));   // the corner's inner angle
                const float t = rad / std::tan(phi * 0.5f);                            // to where the arc meets the edges
                const Vec2 t0 = v - d0 * t, t1 = v + d1 * t;
                const Vec2 c = v + Unit(d1 - d0) * (rad / std::sin(phi * 0.5f));
                const float a0 = std::atan2(t0.y - c.y, t0.x - c.x);
                float sweep = std::atan2(t1.y - c.y, t1.x - c.x) - a0;
                while (sweep > kPi)
                    sweep -= 2.0f * kPi;
                while (sweep < -kPi)
                    sweep += 2.0f * kPi;
                const int segs = std::max(2, (int)std::ceil(std::fabs(sweep) * rad / 1.5f));
                for (int s = 0; s <= segs && k < maxOut; ++s)
                {
                    const float a = a0 + sweep * (float)s / (float)segs;
                    out[k++] = c + Vec2(std::cos(a), std::sin(a)) * rad;
                }
            }
            return k;
        }
    }

    // ------------------------------------------------------------------ the window
    bool Ui::BeginHub(const HubSpec& spec)
    {
        hub_ = spec;
        hubBegun_ = true;
        hubTabCount_ = 0;
        hubSelectedSeen_ = false;
        hubContentOpen_ = false;
        row_ = PendingRow();
        const Vec2 d = ctx_.DisplaySize();
        const InputState& in = ctx_.Input();
        if (hubRestore_.Empty())
        {
            hubRestore_ = spec.pos.x >= 0.0f ? Rect::FromSize(spec.pos, spec.size) : Rect::FromCenter(d * 0.5f, spec.size);
            hubShown_.Snap(0.0f);
        }
        // it assembles when it first appears: fading in, growing from 97 %
        hubShown_.target = 1.0f;
        hubShown_.Step(dt_, kGentle);
        const float appear = Clamp01(hubShown_.value);
        if (spec.toggleKey != Key::None && ctx_.KeyboardFocusId() == 0 && ctx_.KeyPressed(spec.toggleKey, 0, false))
            hubMinimized_ = !hubMinimized_;
        // where it is: its own rect, the display (maximized) or the pill (minimized), springs between them
        hubMorph_.target = hubMinimized_ ? 1.0f : 0.0f;
        hubMorph_.Step(dt_, hubMinimized_ ? kSmooth : kBouncy);
        hubMax_.target = hubMaximized_ ? 1.0f : 0.0f;
        hubMax_.Step(dt_, kGentle);
        const float morph = Clamp01(hubMorph_.value), max = Clamp01(hubMax_.value);
        const text::FontRef pillFont{fonts_.textBold, 14.0f}, keyFont{fonts_.textSemibold, 11.5f};
        const float pillW = 22.0f + 16.0f + 10.0f + Measure(pillFont, spec.title).x + 14.0f + Measure(keyFont, "RShift").x + 20.0f + 18.0f;
        const Rect pill = Rect::FromCenter(Vec2(d.x * 0.5f, 16.0f + kPillHeight * 0.5f), Vec2(pillW, kPillHeight));
        const Rect full(Vec2(12.0f, 12.0f), d - Vec2(12.0f, 12.0f));
        const bool resting = morph < 0.0005f && max < 0.0005f && !hubMinimized_ && !hubMaximized_;
        Rect target = hubRestore_;
        if (!resting)
        {
            const Rect normal(Mix(hubRestore_.min, full.min, max), Mix(hubRestore_.max, full.max, max));
            // the corners travel on a curve: the window shrinks into the pill faster across than down
            const float mx = morph, my = morph * morph * (3.0f - 2.0f * morph);
            target = Rect(Vec2(Mix(normal.min.x, pill.min.x, mx), Mix(normal.min.y, pill.min.y, my)),
                          Vec2(Mix(normal.max.x, pill.max.x, mx), Mix(normal.max.y, pill.max.y, my)));
            ctx_.SetNextWindowPos(target.min, Cond::Always);
            ctx_.SetNextWindowSize(target.Size(), Cond::Always);
        }
        else
        {
            ctx_.SetNextWindowPos(hubRestore_.min, Cond::FirstUse);
            ctx_.SetNextWindowSize(hubRestore_.Size(), Cond::FirstUse);
        }
        WindowOptions o;
        o.flags = esia::WindowFlags_NoScroll;
        if (!resting)
            o.flags |= esia::WindowFlags_NoMove | esia::WindowFlags_NoResize;
        o.padding = Vec2(0.0f, 0.0f);
        o.minSize = resting ? spec.minSize : Vec2(1.0f, 1.0f);
        o.resizeGrip = 18.0f;
        ctx_.Begin("##cipher-hub", o);
        Window* w = ctx_.CurrentWindow();
        hubRect_ = w->GetRect();
        if (resting)
            hubRestore_ = hubRect_;
        const Rect r = hubRect_;
        hubResizable_ = resting;
        const float content = Clamp01(1.0f - morph * 1.6f);   // the content is gone before the window is a pill
        alpha_.push_back(alpha_.back() * appear);
        esia::Painter p = Painter();
        DrawList& dl = ctx_.WindowDrawList();
        const float radius = Mix(m::kRadiusWindow, kPillHeight * 0.5f, morph);
        const float grow = 0.97f + 0.03f * appear;
        dl.PushClipRect(Rect(Vec2(-64.0f, -64.0f), d + Vec2(64.0f, 64.0f)), false);
        p.PushScale(r.Center(), grow);
        Surface(p, r, std::min(radius, r.Height() * 0.5f), SurfaceKind::Window, shown_.raised, 0.9f);
        p.PopScale();
        dl.PopClipRect();
        // the background in motion, where the title bar and the footer let it show (not as a pill)
        if (morph < 0.002f)
            DrawHubWaves(dl, r, m::kTitleBar, spec.profile.name || spec.version ? kFooter : 0.0f, appear * Clamp01(1.0f - morph * 1.6f));

        // the pill: a click (or the key) opens the hub again
        if (morph > 0.4f)
        {
            const float a = Clamp01((morph - 0.4f) / 0.6f);
            const Id pid = ctx_.GetId("##pill");
            ctx_.ItemAdd(pid, r);
            const ButtonResult b = ctx_.ButtonBehavior(pid, r);
            if (b.hovered)
                ctx_.SetMouseCursor(MouseCursor::Hand);
            if (b.pressed && hubMinimized_)
                hubMinimized_ = false;
            const float hov = Anim(pid, 0, b.hovered ? 1.0f : 0.0f);
            const float a0 = p.Alpha();
            p.SetAlpha(a0 * a);
            if (hov > 0.004f)
                p.Rect(r, Style().Radius(r.Height() * 0.5f).Fill(Ink(0.05f * hov)));
            const float cy = r.Center().y;
            float x = r.min.x + 22.0f;
            const float spin = (float)time_ * 40.0f;
            p.RegularPolygon(Vec2(x + 8.0f, cy), 8.0f, 4, spin, Style().Radius(1.5f).Fill(Paint::Linear(shown_.accent2, shown_.accent, 45)));
            x += 26.0f;
            Label(p, x, cy, pillFont, Ink(0.92f), spec.title);
            x += Measure(pillFont, spec.title).x + 14.0f;
            const float kw = Measure(keyFont, "RShift").x + 14.0f;
            p.Rect(Rect(x, cy - 11.0f, x + kw, cy + 11.0f), Style().Radius(m::kRadiusInner).Fill(Ink(0.06f)).Stroke(1.0f / pixelScale_, Ink(0.12f)));
            Label(p, x + kw * 0.5f, cy, keyFont, Ink(0.62f), "RShift", 0.5f);
            p.SetAlpha(a0);
        }
        if (content <= 0.002f)
        {
            hubContent_ = Rect();
            return false;
        }
        alpha_.back() *= content;
        p.SetAlpha(alpha_.back());

        // ---- the title bar: the mark in the window's corner, the title, a badge; the window's buttons at the right
        const float titleH = m::kTitleBar;
        const float cy = r.min.y + titleH * 0.5f;
        float x = r.min.x;
        {
            // the mark's tile is the corner of the window: its rounded corner the window's own, a right angle where the
            // title bar meets the sidebar under it, its right edge slanted
            const float tileW = 56.0f, slant = 14.0f;
            const Vec2 poly[4] = {Vec2(r.min.x, r.min.y + titleH), Vec2(r.min.x, r.min.y), Vec2(r.min.x + tileW + slant, r.min.y),
                                  Vec2(r.min.x + tileW, r.min.y + titleH)};
            const float radii[4] = {0.0f, m::kRadiusWindow, m::kRadiusInner, m::kRadiusInner};
            Vec2 pts[128];
            const int n = RoundCorners(poly, radii, 4, pts, 128);
            p.PushScale(r.Center(), grow);
            p.Polygon(pts, n, Style().Fill(Paint::Linear(shown_.accent2, shown_.accent, 120.0f)));
            // the mark: two diamonds turning against each other
            const Vec2 c(r.min.x + (tileW + slant * 0.5f) * 0.5f + 1.0f, cy);
            const float spin = (float)time_ * 36.0f;
            p.RegularPolygon(c, 10.5f, 4, spin, Style().Radius(2.0f).Fill(shown_.accentInk.WithAlpha(0.9f)));
            p.RegularPolygon(c, 4.2f, 4, -spin, Style().Fill(Paint::Linear(shown_.accent2, shown_.accent, 120.0f)));
            p.PopScale();
            x = r.min.x + tileW + slant + 16.0f;
        }
        const Id hid = ctx_.GetId("##hub-title");
        const text::FontRef titleFont{fonts_.displaySemibold, 18.0f}, subFont{fonts_.text, m::kFontSmall}, badgeFont{fonts_.textSemibold, 12.0f};
        const float titleCy = spec.subtitle ? cy - 8.0f : cy;
        Label(p, x, titleCy, titleFont, Ink(0.95f), spec.title);
        const float titleW = Measure(titleFont, spec.title).x;
        if (spec.subtitle)
            MorphLabel(p, HashInt(1, hid), x, cy + 10.0f, subFont, Ink(0.52f), spec.subtitle);
        if (spec.badge)
        {
            // its width follows its text
            const float bx = x + titleW + 10.0f;
            const float bw = Anim(hid, 0, Measure(badgeFont, spec.badge).x + 16.0f, kSmooth);
            const Rect badge(bx, titleCy - 11.0f, bx + bw, titleCy + 11.0f);
            p.Rect(badge, Style().Radius(m::kRadiusInner).Fill(shown_.accent.Fade(0.14f)));
            MorphLabel(p, HashInt(2, hid), badge.Center().x, badge.Center().y, badgeFont, shown_.accent, spec.badge, 0.5f);
        }
        // buttons: 36 round, 6 in from the top and the right (concentric with the corner)
        float right = r.max.x - 6.0f;
        const auto barButton = [&](const char* id, Icon icon, const char* tip, Icon alt = Icon::None, bool altOn = false) {
            const Rect b(right - 36.0f, r.min.y + 6.0f, right, r.min.y + 42.0f);
            right -= 36.0f;
            const Id bid = ctx_.GetId(id);
            ctx_.ItemAdd(bid, b);
            const ButtonResult br = ctx_.ButtonBehavior(bid, b);
            lastHovered_ = br.hovered;
            lastId_ = bid;
            if (br.hovered)
                ctx_.SetMouseCursor(MouseCursor::Hand);
            const float hover = Anim(bid, 0, br.hovered ? 1.0f : 0.0f);
            const float press = Anim(bid, 1, br.held ? 1.0f : 0.0f);
            if (hover > 0.004f)
                p.Circle(b.Center(), 18.0f * (1.0f - 0.06f * press), Style().Fill(Ink(0.07f * hover + 0.04f * press)));
            const Color ink = Ink(0.55f + 0.4f * hover);
            if (alt == Icon::None)
                DrawIcon(p, icon, b.Center(), m::kIcon, ink);
            else
            {
                // two states: one icon turns and shrinks away as the other turns in
                const float k = Clamp01(Anim(bid, 2, altOn ? 1.0f : 0.0f, kSmooth));
                if (k <= 0.0f)
                    DrawIcon(p, icon, b.Center(), m::kIcon, ink);
                else if (k >= 1.0f)
                    DrawIcon(p, alt, b.Center(), m::kIcon, ink);
                else
                {
                    IconMotion out, in2;
                    out.angle = 1.5707963f * k;
                    out.scale = 1.0f - 0.35f * k;
                    in2.angle = -1.5707963f * (1.0f - k);
                    in2.scale = 0.65f + 0.35f * k;
                    DrawIconVector(p, icon, b.Center(), m::kIcon, ink.Fade(1.0f - k), out);
                    DrawIconVector(p, alt, b.Center(), m::kIcon, ink.Fade(k), in2);
                }
            }
            if (tip)
                SetItemTooltip("%s", tip);
            return br.pressed;
        };
        if (barButton("##hub-close", Icon::X, "Hide (RShift)"))
            hubMinimized_ = true;
        if (barButton("##hub-max", Icon::Maximize2, hubMaximized_ ? "Restore" : "Maximize", Icon::Minimize2, hubMaximized_))
            hubMaximized_ = !hubMaximized_;
        if (barButton("##hub-min", Icon::Minus, "Minimize (RShift)"))
            hubMinimized_ = true;
        p.Rect(Rect(right - 7.0f, cy - 9.0f, right - 7.0f + 1.0f / pixelScale_, cy + 9.0f), Style().Fill(Ink(0.10f)));
        right -= 14.0f;
        if (barButton("##hub-theme", Icon::Palette, "Next theme"))
            SetTheme((themeIndex_ + 1) % (int)Themes().size());
        hubAvatar_ = Rect();
        if (spec.profile.name)
        {
            hubAvatar_ = DrawHubAvatar(p, right, cy);   // in step with the buttons: 36 apart
            right = hubAvatar_.min.x;
        }
        // a double click on the bar maximizes
        const Rect bar(r.min.x, r.min.y, right, r.min.y + titleH);
        if (ctx_.HoveredWindow() == w && in.MouseDoubleClicked(MouseButton::Left) && bar.Contains(in.MousePos()) && ctx_.HoveredId() == 0)
            hubMaximized_ = !hubMaximized_;
        p.HLine(r.min.x, r.max.x, r.min.y + titleH, Ink(0.07f), 1.0f / pixelScale_);

        // ---- the body: the sidebar at the left, the content beside it, over the footer (when there is one)
        hubFooter_ = spec.profile.name || spec.version ? kFooter : 0.0f;
        const float bottom = r.max.y - hubFooter_;
        hubSidebar_ = Rect(r.min.x, r.min.y + titleH, r.min.x + spec.sidebar, bottom);
        hubContent_ = Rect(hubSidebar_.max.x + 1.0f, r.min.y + titleH + 1.0f, r.max.x, bottom);
        p.Rect(Rect(hubSidebar_.min.x, hubSidebar_.min.y, hubSidebar_.max.x, hubSidebar_.max.y),
               Style().Radius(0, 0, 0, hubFooter_ > 0.0f ? 0.0f : m::kRadiusWindow).Fill(Ink(0.022f)));
        p.Rect(Rect(hubSidebar_.max.x, hubSidebar_.min.y, hubSidebar_.max.x + 1.0f / pixelScale_, hubSidebar_.max.y), Style().Fill(Ink(0.07f)));
        return true;
    }

    void Ui::SetHubSelection(Id id)
    {
        if (id == hubSelected_)
            return;
        // which way the content moves: by the order of the tabs in the sidebar
        int from = -1, to = -1, i = 0;
        for (const HubTabEntry& e : hubTabs_)
        {
            if (e.id && e.id == hubSelected_)
                from = i;
            if (e.id && e.id == id)
                to = i;
            ++i;
        }
        hubDir_ = (from >= 0 && to >= 0 && to < from) ? -1.0f : 1.0f;
        hubSelected_ = id;
    }

    void Ui::SelectHubTab(const char* label) { SetHubSelection(HashLabel(label, 0x48554254u)); }

    void Ui::HubSection(const char* label)
    {
        CloseHubContent();
        if (hubTabCount_ == hubTabs_.size())
            hubTabs_.emplace_back();
        HubTabEntry& e = hubTabs_[hubTabCount_++];
        e.label = label;
        e.icon = Icon::None;
        e.note.clear();
        e.id = 0;
    }

    bool Ui::HubTab(const char* label, Icon icon, const char* note)
    {
        CloseHubContent();
        const Id id = HashLabel(label, 0x48554254u);
        if (hubTabCount_ == hubTabs_.size())
            hubTabs_.emplace_back();
        HubTabEntry& e = hubTabs_[hubTabCount_++];
        e.label = label;
        e.icon = icon;
        e.note = note ? note : "";
        e.id = id;
        if (!hubSelected_)
            hubSelected_ = id;
        const bool sel = id == hubSelected_;
        TabFade& tf = ctx_.State<TabFade>(HashInt(0x54414246, id));
        if (!tf.made)
        {
            tf.made = true;
            tf.selected = sel;
            tf.shown.Snap(sel ? 1.0f : 0.0f);
        }
        if (sel != tf.selected)
        {
            tf.selected = sel;
            tf.dir = hubDir_;
        }
        // in at the pace of a state change, out quicker: the new content leads
        tf.shown.target = sel ? 1.0f : 0.0f;
        tf.shown.Step(dt_, sel ? kSmooth : Motion{0.20f, 1.0f});
        if (sel)
            hubSelectedSeen_ = true;
        if (hubContent_.Empty() || (!sel && tf.shown.value <= 0.003f))
            return false;
        const float shown = Clamp01(tf.shown.value);
        const float offset = (sel ? 1.0f : -1.0f) * tf.dir * 12.0f * (1.0f - shown);
        OpenHubContent(id, Visible(label), note, shown, offset, !sel);
        return true;
    }

    void Ui::OpenHubContent(Id id, std::string_view title, const char* note, float shown, float offset, bool ghost)
    {
        ctx_.SetCursorPos(hubContent_.min);
        ChildOptions co;
        co.size = hubContent_.Size();
        co.padding = Vec2(20.0f, 0.0f);
        co.clipInset = Rect(-20.0f, 0.0f, -20.0f, 0.0f);
        co.flags = ChildFlags_ScrollY | ChildFlags_SmoothScroll | (ghost ? ChildFlags_NoWheel : 0u);
        ctx_.PushId((int)id);
        ctx_.BeginChild("##hub-content", co);
        WinFrame wf;
        wf.id = id;
        wf.body = true;
        wf.roundBottom = hubFooter_ > 0.0f ? 0.0f : m::kRadiusWindow;   // without a footer: the window's rounded corner
        BeginFade(wf, ctx_.ViewRect());
        windows_.push_back(wf);
        hubContentOpen_ = true;
        inert_ = ghost;
        alpha_.push_back(alpha_.back() * Smoothstep(0.3f, 1.0f, shown));
        // one column: everything starts 12 in, the surfaces that span the content reach back to its edge
        ctx_.Indent(12.0f);
        bleed_ = 12.0f;
        ctx_.ItemSize(Vec2(0.0f, std::max(14.0f + offset, 0.0f)));
        // its title and what it is about
        const Rect head = ItemRect(Vec2(ContentWidth(), note ? 60.0f : 34.0f));
        ctx_.ItemAdd(0, head);
        esia::Painter p = Painter();
        Label(p, head.min.x, head.min.y + 15.0f, {fonts_.displaySemibold, m::kFontHeading}, Ink(0.95f), title);
        if (note)
            Label(p, head.min.x, head.min.y + 45.0f, {fonts_.text, m::kFontSmall}, Ink(0.56f), note);
        rowIndex_ = 0;
    }

    void Ui::CloseHubContent()
    {
        if (!hubContentOpen_)
            return;
        EndFoldsOf(windows_.size());
        row_ = PendingRow();
        ctx_.ItemSize(Vec2(0.0f, 18.0f));
        const WinFrame wf = windows_.back();
        windows_.pop_back();
        esia::Painter p = Painter();
        p.EndEdgeFade();
        ScrollBar(ctx_.GetId("##scroll"), wf.roundTop, wf.roundBottom);
        alpha_.pop_back();
        ctx_.EndChild();
        ctx_.PopId();
        hubContentOpen_ = false;
        inert_ = false;
        bleed_ = 0.0f;
    }

    void Ui::EndHub()
    {
        if (!hubBegun_)
            return;
        hubBegun_ = false;
        CloseHubContent();
        hubTabs_.resize(std::max(hubTabs_.size(), hubTabCount_));
        // the selected tab went away: the first one
        if (!hubSelectedSeen_ && !hubContent_.Empty())
            for (std::size_t i = 0; i < hubTabCount_; ++i)
                if (hubTabs_[i].id)
                {
                    hubSelected_ = hubTabs_[i].id;
                    break;
                }
        if (!hubContent_.Empty())
            DrawHubSidebar();
        else
        {
            // minimized: the avatar is gone, its menu closes (and fades out where it was)
            hubAvatar_ = Rect();
            const Id menu = HashLabel("##cipher-profile-menu", 0x50524F46u);
            if (ctx_.IsPopupOpen(menu))
                ctx_.ClosePopup(menu);
        }
        // the footer, once the tabs and their content have said where you are
        if (hubFooter_ > 0.0f && !hubContent_.Empty())
        {
            esia::Painter p = Painter();
            DrawHubFooter(p, hubRect_, 1.0f);
        }
        if (hubResizable_)
            ResizeCorner(ctx_.GetId("##corner"), hubRect_, m::kRadiusWindow);
        alpha_.pop_back();
        ctx_.End();
    }

    void Ui::DrawHubSidebar()
    {
        const Rect list = hubSidebar_;
        ctx_.SetCursorPos(list.min);
        ChildOptions co;
        co.size = list.Size();
        co.padding = Vec2(8.0f, 8.0f);
        co.clipInset = Rect(-8.0f, -8.0f, -8.0f, -8.0f);
        co.flags = ChildFlags_ScrollY | ChildFlags_SmoothScroll;
        ctx_.BeginChild("##hub-sidebar", co);
        // its ends fade where its tabs scroll on past them
        WinFrame fade;
        BeginFade(fade, ctx_.ViewRect());
        esia::Painter p = Painter();
        const text::FontRef labelFont{fonts_.text, m::kFont}, captionFont{fonts_.textSemibold, 12.0f};
        const float w = ContentWidth();
        for (std::size_t i = 0; i < hubTabCount_; ++i)
        {
            const HubTabEntry& e = hubTabs_[i];
            if (!e.id)
            {
                // a section: its name, small, upper case and a little spaced out
                const Rect cr = ItemRect(Vec2(w, i == 0 ? 26.0f : 38.0f));
                ctx_.ItemAdd(0, cr);
                SpacedLabel(p, cr.min.x + 12.0f, cr.max.y - 11.0f, captionFont, Ink(0.42f), e.label, 0.9f);
                continue;
            }
            const Rect tr = ItemRect(Vec2(w, m::kRow));
            ctx_.ItemAdd(e.id, tr);
            const ButtonResult b = ctx_.ButtonBehavior(e.id, tr);
            if (b.hovered)
                ctx_.SetMouseCursor(MouseCursor::Hand);
            if (b.pressed)
                SetHubSelection(e.id);
            const bool sel = hubSelected_ == e.id;
            const float hover = Anim(e.id, 0, b.hovered ? 1.0f : 0.0f);
            const float on = Anim(e.id, 1, sel ? 1.0f : 0.0f, kSmooth);
            const float press = Anim(e.id, 2, b.held ? 1.0f : 0.0f);
            const Color fill = Ink(0.045f * hover + 0.07f * on + 0.03f * press);
            if (fill.a > 0.002f)
                p.Rect(tr, Style().Radius(m::kRadiusControl).Fill(fill));
            // the selected tab: a faint stroke round its edge, the same all the way round, and light flowing round it -
            // a comet going clockwise at an even, unhurried pace, its tail fading behind it - blooming a little (drawn
            // into a glow layer: the glow goes where the light is). It opens from the left as the tab is selected and
            // closes back into it as another is.
            if (on > 0.004f)
            {
                const float k = Clamp01(on);
                const Rect o = tr.Expanded(-0.75f);
                p.PushClip(Rect(tr.min.x - 16.0f, tr.min.y - 16.0f, tr.min.x - 16.0f + (tr.Width() + 32.0f) * EaseOutCubic(k), tr.max.y + 16.0f));
                p.BeginGlowLayer(Color(0.0f, 0.0f, 0.0f, 0.0f), 10.0f, 0.6f * k, 1.0f);
                p.Rect(o, Style().Radius(m::kRadiusControl - 0.75f).Stroke(1.25f, shown_.accent.Fade(0.3f * k), 0.0f));
                // the comet: the line through the stroke's middle lit round points along it - the head, then the tail
                // ever fainter behind it - going round once every 5 s at an even pace (along its length, not by angle:
                // on a long tab an angle races along the sides and crawls round the ends)
                static thread_local std::vector<Vec2> outline;
                static thread_local std::vector<float> along;   // the outline's length up to each of its points
                RoundedOutline(tr.Expanded(-1.375f), m::kRadiusControl - 1.375f, outline);
                along.assign(outline.size(), 0.0f);
                for (std::size_t q = 1; q < outline.size(); ++q)
                    along[q] = along[q - 1] + Length(outline[q] - outline[q - 1]);
                const float perimeter = std::max(along.back(), 1.0f);
                const auto pointAt = [&](float s) {
                    s = std::fmod(s, perimeter);
                    if (s < 0.0f)
                        s += perimeter;
                    const std::size_t q = std::clamp((std::size_t)(std::upper_bound(along.begin(), along.end(), s) - along.begin()), (std::size_t)1,
                                                     along.size() - 1);
                    const float f = (s - along[q - 1]) / std::max(along[q] - along[q - 1], 1e-4f);
                    return outline[q - 1] + (outline[q] - outline[q - 1]) * f;
                };
                const float head = (float)std::fmod(time_ / 5.0, 1.0) * perimeter, longest = std::max(o.Width(), o.Height());
                constexpr float kTail[6] = {0.70f, 0.50f, 0.34f, 0.21f, 0.11f, 0.04f};
                for (int spot = 0; spot < 6; ++spot)
                {
                    // the line's bounds are `o` (the outline grown by half the line): the light's center in them
                    const Vec2 c = pointAt(head - 12.0f * (float)spot);
                    const Vec2 uv((c.x - o.min.x) / o.Width(), (c.y - o.min.y) / o.Height());
                    p.Polyline(outline.data(), (int)outline.size(), 1.25f,
                               Style().Fill(Paint::Radial(shown_.accent.Fade(kTail[spot] * k), shown_.accent.Fade(0.0f), uv, 18.0f / longest)));
                }
                p.EndGlowLayer();
                p.PopClip();
            }
            const float cy = tr.Center().y;
            DrawIcon(p, e.icon, Vec2(tr.min.x + 12.0f + m::kIcon * 0.5f, cy), m::kIcon, Mix(Ink(0.58f + 0.2f * hover), shown_.accent, Clamp01(on)));
            Label(p, tr.min.x + 42.0f, cy, labelFont, Ink(0.70f + 0.25f * std::max(on, hover)), Visible(e.label), 0.0f, tr.Width() - 42.0f - 12.0f);
        }
        p.EndEdgeFade();
        ScrollBar(ctx_.GetId("##scroll"));
        ctx_.EndChild();
    }

    // The profile's avatar in the title bar, its right edge at `right`: as big as the icons beside it, its initials on
    // the accent, its status a dot on its edge; the round behind it lit under the pointer, as the other buttons' are, a
    // ring of the accent round it while its menu is open; a click opens or closes the menu (BeginHubProfileMenu), which
    // hangs under it. Returns where it is.
    Rect Ui::DrawHubAvatar(esia::Painter& p, float right, float cy)
    {
        const HubProfile& pr = hub_.profile;
        const Rect ab(right - 36.0f, cy - 18.0f, right, cy + 18.0f);
        const Id id = ctx_.GetId("##hub-avatar");
        const Id menu = HashLabel("##cipher-profile-menu", 0x50524F46u);
        const bool open = ctx_.IsPopupOpen(menu);
        ctx_.ItemAdd(id, ab);
        const ButtonResult b = ctx_.ButtonBehavior(id, ab);
        lastHovered_ = b.hovered;
        lastId_ = id;
        if (b.pressed)
        {
            if (open)
                ctx_.ClosePopup(menu);
            else
                ctx_.OpenPopup(menu);
        }
        if (b.hovered)
            ctx_.SetMouseCursor(MouseCursor::Hand);
        const float hover = Anim(id, 0, b.hovered ? 1.0f : 0.0f);
        const float press = Anim(id, 1, b.held ? 1.0f : 0.0f);
        const float on = Clamp01(Anim(id, 2, open ? 1.0f : 0.0f, kSmooth));
        const Vec2 ac = ab.Center();
        if (hover + press > 0.004f)
            p.Circle(ac, 18.0f * (1.0f - 0.06f * press), Style().Fill(Ink(0.07f * hover + 0.04f * press)));
        if (on > 0.004f)
            p.Ring(ac, 12.75f, 1.25f, Style().Fill(shown_.accent.Fade(on)));
        p.Circle(ac, 10.0f, Style().Fill(Paint::Linear(shown_.accent2, shown_.accent, 135.0f)));
        char initials[8] = {};
        if (pr.initials)
            std::snprintf(initials, sizeof(initials), "%s", pr.initials);
        else
            initials[0] = (char)std::toupper((unsigned char)pr.name[0]);
        Label(p, ac.x, ac.y, {fonts_.textBold, 8.5f}, shown_.accentInk, initials, 0.5f);
        // the status: a dot on its edge (its color changes with the status, the dot itself keeps still)
        const Vec2 dot = ac + Vec2(7.2f, 7.2f);
        const Color sc = AnimColor(id, 0, pr.online ? shown_.success : Ink(0.45f), kSmooth);
        p.Circle(dot, 3.8f, Style().Fill(Mix(shown_.raised, shown_.ink, 0.03f).WithAlpha(1.0f)));
        p.Circle(dot, 2.6f, Style().Fill(sc));
        // who it is, while its menu is closed
        if (!open && pr.detail)
            SetItemTooltip("%s \xC2\xB7 %s", pr.name, pr.detail);
        else if (!open)
            SetItemTooltip("%s", pr.name);
        return ab;
    }

    namespace
    {
        // A line through `pts` in plain triangles (no layer, no FX), on the draw list's clip, its color `c` faded along it
        // by `fade`. Its cross-section by `soft`: false - a solid core `width` across with a pixel (`aa`) of anti-aliasing
        // each side (a crisp line: the same alpha all across its core, so it does not flicker as it crosses the pixel
        // rows); true - a soft bell `width` across (a glow).
        void SoftLine(DrawList& dl, const Vec2* pts, const float* fade, int n, float width, float aa, Color c, bool soft)
        {
            if (n < 2 || c.a <= 0.0005f)
                return;
            // the rows across: their offsets from the middle and their share of the alpha
            float off[5], share[5];
            int rows;
            if (soft)
            {
                const float hw = width * 0.5f;
                const float o[5] = {-hw, -hw * 0.45f, 0.0f, hw * 0.45f, hw}, k[5] = {0.0f, 0.42f, 1.0f, 0.42f, 0.0f};
                std::copy(o, o + 5, off);
                std::copy(k, k + 5, share);
                rows = 5;
            }
            else
            {
                // thinner than a pixel: a pixel wide, fainter
                const float core = std::max(width, aa), hw = core * 0.5f;
                if (width < aa)
                    c.a *= width / aa;
                const float o[4] = {-hw - aa * 0.5f, -hw + aa * 0.5f, hw - aa * 0.5f, hw + aa * 0.5f}, k[4] = {0.0f, 1.0f, 1.0f, 0.0f};
                std::copy(o, o + 4, off);
                std::copy(k, k + 4, share);
                rows = 4;
            }
            const std::uint32_t nv = (std::uint32_t)(n * rows), ni = (std::uint32_t)((n - 1) * (rows - 1) * 6);
            DrawList::PrimWriter w = dl.PrimReserve(TextureId(0), ni, nv);   // on white: the overload that keeps its command
            Rect bounds(1e30f, 1e30f, -1e30f, -1e30f);
            const float reach = std::max(std::fabs(off[0]), std::fabs(off[rows - 1]));
            for (int i = 0; i < n; ++i)
            {
                const Vec2 d = pts[std::min(i + 1, n - 1)] - pts[std::max(i - 1, 0)];
                const float l = std::sqrt(d.x * d.x + d.y * d.y);
                const Vec2 nrm = l > 1e-6f ? Vec2(-d.y / l, d.x / l) : Vec2(0.0f, 1.0f);
                for (int r = 0; r < rows; ++r)
                    w.vtx[i * rows + r] = Vertex{pts[i] + nrm * off[r], Vec2(0.0f, 0.0f), c.Fade(share[r] * fade[i]).ToRgba8()};
                bounds = bounds.Union(Rect(pts[i] - Vec2(reach, reach), pts[i] + Vec2(reach, reach)));
            }
            std::uint32_t k = 0;
            for (int i = 0; i + 1 < n; ++i)
                for (int r = 0; r + 1 < rows; ++r)
                {
                    const std::uint32_t a0 = w.base + (std::uint32_t)(i * rows + r), a1 = a0 + 1, b0 = a0 + (std::uint32_t)rows, b1 = b0 + 1;
                    w.idx[k++] = a0;
                    w.idx[k++] = a1;
                    w.idx[k++] = b1;
                    w.idx[k++] = a0;
                    w.idx[k++] = b1;
                    w.idx[k++] = b0;
                }
            dl.PrimCommit(w, ni, bounds);
        }
    }

    // The hub's background in motion where it shows through, faint, a different motion in each band. In the title bar
    // a thread of light: one slow wave the size of the window, its crests drifting through the bar - a fine line with a
    // soft glow, gone towards the window's ends. In the footer two soft lights in the accents gliding to and fro at
    // their own paces, like light moving under the glass.
    void Ui::DrawHubWaves(DrawList& dl, const Rect& r, float top, float bottom, float alpha)
    {
        if (alpha <= 0.003f)
            return;
        const float t = (float)time_, W = r.Width(), Hh = r.Height(), cy = r.Center().y;
        const float aa = 1.0f / pixelScale_;
        const auto ends = [&](float x) {
            const float u = (x - r.min.x) / std::max(W, 1.0f);
            return Smoothstep(0.0f, 0.10f, u) * (1.0f - Smoothstep(0.90f, 1.0f, u));   // nothing at the window's ends
        };
        // ---- the title bar: the thread
        if (top > 0.5f)
        {
            constexpr int kPoints = 240;
            Vec2 pts[kPoints + 1];
            float fade[kPoints + 1];
            for (int i = 0; i <= kPoints; ++i)
            {
                const float u = (float)i / (float)kPoints, x = r.min.x + W * u;
                const float y = cy + Hh * (0.47f * std::sin(6.2831853f * u / 1.35f + 0.14f * t) + 0.05f * std::sin(6.2831853f * u / 0.41f - 0.21f * t));
                pts[i] = Vec2(x, y);
                fade[i] = ends(x);
            }
            dl.PushClipRect(Rect(r.min.x, r.min.y, r.max.x, r.min.y + top));
            SoftLine(dl, pts, fade, kPoints + 1, 14.0f, aa, shown_.accent.Fade(0.06f * alpha), true);
            SoftLine(dl, pts, fade, kPoints + 1, 1.1f, aa, Mix(shown_.accent, shown_.ink, 0.25f).Fade(0.26f * alpha), false);
            dl.PopClipRect();
        }
        // ---- the footer: two soft lights gliding
        if (bottom > 0.5f)
        {
            const Rect band(r.min.x, r.max.y - bottom, r.max.x, r.max.y);
            dl.PushClipRect(band);
            struct Light
            {
                float period, phase, width;
                Color color;
            };
            const Light lights[2] = {{23.0f, 0.0f, 300.0f, shown_.accent}, {31.0f, 2.4f, 240.0f, shown_.accent2}};
            for (const Light& l : lights)
            {
                // to and fro, easing at the turns
                const float s = 0.5f - 0.5f * std::cos(6.2831853f * t / l.period + l.phase);
                const float x = Mix(r.min.x + W * 0.12f, r.max.x - W * 0.12f, s);
                SoftSpot(dl, Rect::FromCenter(Vec2(x, band.Center().y + 4.0f), Vec2(l.width, bottom * 1.6f)), l.color.Fade(0.13f * alpha * ends(x)));
            }
            dl.PopClipRect();
        }
    }

    // The hub's footer: a hairline over it; at the left where you are - the selected tab's icon and name, the selected
    // tab of its own tab bar after it, both rolling over as they change; in its corner a tile like the mark's turned
    // round - its gradient, the window's corner its own, its edge slanted at the mark's angle - holding the mark, who
    // you are and the version. The mark there does not spin as the title bar's does: every few seconds it turns over
    // like a coin, then rests. The version, tapped seven times quickly, gives away a secret (the pet cheers, sparks fly
    // out of it); once the pet has been patted, a heart and how many times before the tile.
    void Ui::DrawHubFooter(esia::Painter& p, const Rect& r, float grow)
    {
        (void)grow;
        const float h = hubFooter_;
        const Rect foot(r.min.x, r.max.y - h, r.max.x, r.max.y);
        p.HLine(r.min.x, r.max.x, foot.min.y, Ink(0.07f), 1.0f / pixelScale_);
        const float cy = foot.Center().y;
        const Id fid = HashLabel("##hub-footer", 0x464F4F54u);
        const text::FontRef font{fonts_.text, 13.0f}, mono{fonts_.monoSemibold, 12.0f};
        // ---- where you are
        float x = r.min.x + 18.0f;
        const HubTabEntry* sel = nullptr;
        for (std::size_t i = 0; i < hubTabCount_; ++i)
            if (hubTabs_[i].id == hubSelected_)
                sel = &hubTabs_[i];
        if (sel)
        {
            // the icon comes in (fading, growing) each time the tab changes
            IconMotion mo;
            const float in = Clamp01(Anim(HashInt(0x46494331, sel->id), 0, 1.0f, kSmooth));
            mo.scale = 0.7f + 0.3f * in;
            DrawIconVector(p, sel->icon, Vec2(x + 8.0f, cy), 15.0f, shown_.accent.Fade(in), mo);
            x += 24.0f;
            MorphLabel(p, HashInt(1, fid), x, cy, font, Ink(0.78f), sel->label);
            x += Anim(HashInt(2, fid), 0, Measure(font, sel->label).x, kSmooth) + 8.0f;
            const float crumb = Clamp01(Anim(HashInt(3, fid), 0, hubCrumb_.empty() ? 0.0f : 1.0f, kSmooth));
            if (crumb > 0.004f)
            {
                DrawIcon(p, Icon::ChevronRight, Vec2(x + 6.0f, cy), 13.0f, Ink(0.30f * crumb));
                MorphLabel(p, HashInt(4, fid), x + 18.0f, cy, font, Ink(0.52f * crumb), hubCrumb_);
            }
        }
        hubCrumb_.clear();
        // ---- the corner tile: the mark, who you are, the version
        float right = r.max.x - 14.0f;
        const char* name = hub_.profile.name;
        const char* version = hub_.version;
        if (name || version)
        {
            const text::FontRef nameFont{fonts_.textSemibold, 13.5f}, versionFont{fonts_.text, 12.5f};
            const float nameW = name ? Measure(nameFont, name).x : 0.0f, versionW = version ? Measure(versionFont, version).x : 0.0f;
            // from the corner in: 18 to the version, the name 8 before it, the mark (16 across) 10 before the name, 14
            // more to the slanted edge at mid-height
            const float slant = 14.0f * h / m::kTitleBar;
            const float versionX = r.max.x - 18.0f - versionW, nameX = versionX - (name && version ? 8.0f : 0.0f) - nameW;
            const float markX = nameX - 10.0f - 8.0f;
            const float tileW = r.max.x - (markX - 8.0f - 14.0f) - slant * 0.5f;   // its top edge
            const Vec2 poly[4] = {Vec2(r.max.x, foot.min.y), Vec2(r.max.x, r.max.y), Vec2(r.max.x - tileW - slant, r.max.y), Vec2(r.max.x - tileW, foot.min.y)};
            const float radii[4] = {0.0f, m::kRadiusWindow, m::kRadiusInner, m::kRadiusInner};
            Vec2 pts[128];
            const int n = RoundCorners(poly, radii, 4, pts, 128);
            p.Polygon(pts, n, Style().Fill(Paint::Linear(shown_.accent2, shown_.accent, 300.0f)));
            // the mark: the two diamonds turning over together like a coin every 8 s (1.2 s round, eased), the inner
            // one a beat behind; edge-on they are lines, past it their backs show a shade darker
            const float period = 8.0f, turn = 1.2f;
            const float local = (float)std::fmod(time_, (double)period);
            const float k = EaseInOutCubic(Clamp01(local / turn)), k2 = EaseInOutCubic(Clamp01((local - 0.12f) / turn));
            const auto coin = [&](float radius, float flip, Color c) {
                const float sx = std::cos(flip * 2.0f * kPi);   // all the way round: it ends as it began
                const float w = std::max(std::fabs(sx), 0.06f) * radius;
                const Vec2 q[4] = {Vec2(markX, cy - radius), Vec2(markX + w, cy), Vec2(markX, cy + radius), Vec2(markX - w, cy)};
                p.Polygon(q, 4, Style().Radius(radius * 0.18f).Fill(sx >= 0.0f ? c : Mix(c, Color::Black(c.a), 0.25f)));
            };
            coin(8.0f, k, shown_.accentInk.WithAlpha(0.9f));
            coin(3.2f, k2, Mix(shown_.accent2, shown_.accent, 0.5f));
            if (name)
                Label(p, nameX, cy, nameFont, shown_.accentInk.WithAlpha(0.95f), name);
            if (version)
            {
                // the secret's taps: on the version
                const Rect vr(versionX - 4.0f, cy - 11.0f, versionX + versionW + 4.0f, cy + 11.0f);
                const Id cid = HashInt(5, fid);
                ctx_.ItemAdd(cid, vr);
                const ButtonResult b = ctx_.ButtonBehavior(cid, vr);
                const float press = Anim(cid, 1, b.held ? 1.0f : 0.0f);
                if (b.pressed)
                {
                    hubEggClicks_ = time_ - hubEggLast_ < 1.2 ? hubEggClicks_ + 1 : 1;
                    hubEggLast_ = time_;
                    if (hubEggClicks_ >= 7)
                    {
                        hubEggClicks_ = 0;
                        hubEgg_ = time_;
                        Notify({NoteKind::Success, "You found it", "Seven taps on the version: the pet cheers.", Icon::PartyPopper});
                    }
                }
                // each tap a little pulse, brighter the closer to the secret
                const float near = Clamp01(Anim(HashInt(8, fid), 0, time_ - hubEggLast_ < 1.2 ? (float)hubEggClicks_ / 7.0f : 0.0f, kSnappy));
                p.PushScale(vr.Center(), 1.0f - 0.08f * press);
                Label(p, versionX, cy, versionFont, shown_.accentInk.WithAlpha(0.68f + 0.32f * near), version);
                p.PopScale();
                // the secret: sparks out of the version - the mark's diamonds in the theme's colors, flying up and falling
                const float age = (float)(time_ - hubEgg_);
                if (hubEgg_ >= 0.0 && age < 1.6f)
                {
                    DrawList& dl = ctx_.WindowDrawList();
                    dl.PushClipRect(Rect(Vec2(-64.0f, -64.0f), ctx_.DisplaySize() + Vec2(64.0f, 64.0f)), false);
                    const Color colors[4] = {shown_.accent, shown_.accent2, shown_.hack[3], shown_.success};
                    for (int i = 0; i < 18; ++i)
                    {
                        const float a0 = -1.5708f + ((float)(i * 37 % 18) / 17.0f - 0.5f) * 2.4f;
                        const float v = 300.0f + 140.0f * (float)((i * 53) % 7) / 6.0f;
                        const Vec2 at = vr.Center() + Vec2(std::cos(a0), std::sin(a0)) * v * age + Vec2(0.0f, 330.0f * age * age);
                        const float fade = 1.0f - Smoothstep(0.9f, 1.6f, age);
                        p.RegularPolygon(at, 3.0f + (float)(i % 3), 4, age * 540.0f + (float)i * 40.0f, Style().Radius(0.8f).Fill(colors[i % 4].Fade(fade)));
                    }
                    dl.PopClipRect();
                }
            }
            right = r.max.x - tileW - slant - 12.0f;
        }
        // the pats, once there are any: a heart and the count, rolling as it grows
        const float pats = Clamp01(Anim(HashInt(6, fid), 0, hubPats_ > 0 ? 1.0f : 0.0f, kSmooth));
        if (pats > 0.004f)
        {
            char count[16];
            std::snprintf(count, sizeof(count), "%d", hubPats_);
            const float w = Measure(mono, count).x;
            MorphLabel(p, HashInt(7, fid), right - w, cy, mono, Ink(0.50f * pats), count);
            IconMotion mo;
            mo.scale = 0.6f + 0.4f * EaseOutBack(pats);
            DrawIconVector(p, Icon::Heart, Vec2(right - w - 12.0f, cy), 13.0f, Color::Hex(0xFF6FA5).Fade(pats), mo);
        }
    }

    // ------------------------------------------------------------------ element rows
    void Ui::Group(const char* caption)
    {
        EndImplicitFolds();
        row_ = PendingRow();
        const float w = ContentWidth();
        const Rect r = ItemRect(Vec2(w, 38.0f));
        ctx_.ItemAdd(0, r);
        esia::Painter p = Painter();
        const text::FontRef f{fonts_.textSemibold, 12.5f};
        const float cy = r.max.y - 11.0f;
        // on the content's column (the rows' text, the controls, the selectables' icons)
        const float cx = r.min.x - bleed_ + 12.0f;
        const float lx = cx + SpacedLabel(p, cx, cy, f, Ink(0.50f), caption, 0.9f) + 12.0f;
        if (lx < r.max.x)
            p.HLine(lx, r.max.x, cy, Ink(0.07f), 1.0f / pixelScale_);
        rowIndex_ = 0;
    }

    void Ui::Row(const char* title, const char* desc)
    {
        const float w = ContentWidth();
        const text::FontRef titleFont{fonts_.text, m::kFont}, descFont{fonts_.text, m::kFontSmall};
        // the text keeps clear of the control: it ends three steps before the widest control begins (a field: 42 % of the
        // row, 6 in from its right edge), its description wrapping
        const float textMax = std::max((w + bleed_) * 0.58f - 36.0f, 80.0f);
        const float textW = desc ? BalancedWidth(descFont, desc, textMax) : textMax;
        const float descH = desc ? Measure(descFont, desc, textW).y : 0.0f;
        // the text (the title, the description under it) in the middle of the row, two steps above and below it
        const float titleH = 20.0f;
        const float h = desc ? std::max(m::kElement, 12.0f + titleH + 3.0f + descH + 12.0f) : m::kElement;
        const Rect r = ItemRect(Vec2(w, h));
        ctx_.ItemAdd(0, r);
        esia::Painter p = Painter();
        // its surface reaches back to the content's edge: the text stands on the column, 12 into it
        const Rect sr(r.min.x - bleed_, r.min.y, r.max.x, r.max.y);
        // the row under the pointer lights up a little; rows of a group share hairlines
        const InputState& in = ctx_.Input();
        const bool over = !inert_ && in.MouseValid() && sr.Contains(in.MousePos()) && ctx_.HoveredWindow() == ctx_.CurrentWindow() && ctx_.ActiveId() == 0;
        const Id rid = HashLabel(title, 0x524F57u ^ (Id)(rowIndex_ * 2654435761u));
        const float hover = Anim(ctx_.GetId((int)rid), 0, over ? 1.0f : 0.0f);
        if (hover > 0.004f)
            p.Rect(sr, Style().Radius(m::kRadiusControl).Fill(Ink(0.03f * hover)));
        if (rowIndex_ > 0)
            p.HLine(sr.min.x + 12.0f, sr.max.x - 12.0f, sr.min.y - 3.0f, Ink(0.06f), 1.0f / pixelScale_);
        const float tx = sr.min.x + 12.0f;
        const float titleCy = desc ? r.min.y + 12.0f + titleH * 0.5f : r.Center().y;
        Label(p, tx, titleCy, titleFont, Ink(0.92f), Visible(title), 0.0f, textW);
        if (desc)
            p.Text(Vec2(tx, titleCy + titleH * 0.5f + 3.0f), descFont, Ink(0.54f), desc, textW);
        ++rowIndex_;
        row_.on = true;
        row_.row = sr;
        // the control: 30 tall, 6 from the row's right edge, on the row's middle line
        row_.control = Rect(r.max.x - 6.0f, r.Center().y - m::kControl * 0.5f, r.max.x - 6.0f, r.Center().y + m::kControl * 0.5f);
    }

    bool Ui::TakeRow(float width, Rect& control)
    {
        if (!row_.on)
            return false;
        row_.on = false;
        // at most 42 % of the row: where the row's text ends (Row), three steps before it; a narrow row narrows a field
        const float maxW = row_.row.Width() * 0.42f;
        const float cw = width > 0.0f ? std::min(width, maxW) : maxW;
        control = Rect(row_.control.max.x - cw, row_.control.min.y, row_.control.max.x, row_.control.max.y);
        return true;
    }
}
