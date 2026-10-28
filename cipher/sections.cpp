// Cipher - sections: headers and trees whose content opens and closes with its height, and tab bars whose content
// cross-fades (cipher.hpp).
//
// A fold lays its content out whole, in a container, every frame it is open or moving: clipped to the spring's share of
// its height (last frame's, measured), faded with it, and what follows moved up as much - so the content is never
// rebuilt or cached, and any frame may turn the spring around. A tab bar lays the content of every tab it shows - the
// selected one and those still fading out - from the same place, each in its own container, and leaves room under them
// for the selected one's height (a spring too).
#include "cipher.hpp"
#include <algorithm>
#include <cmath>

namespace cipher
{
    using namespace esia;
    namespace m = metrics;

    namespace
    {
        struct FoldState
        {
            bool made = false, open = false;
            float full = 0.0f;     // the content's height, last frame
        };
        struct TabBarState
        {
            Id selected = 0;
            int selectedIndex = 0;
            float dir = 1.0f;      // +1: the selection moved right (content comes in from the right), -1 left
            Spring pillX, pillW, height;   // pillX from the bar's left edge: a window dragged carries it along
            bool placed = false, measured = false;
        };
        struct TabItemFade
        {
            Spring shown;
            float dir = 1.0f;
            bool made = false, selected = false;
        };
    }

    // ------------------------------------------------------------------ folds
    bool Ui::FoldToggle(Id id, bool defaultOpen, bool pressed, float& open)
    {
        FoldState& st = ctx_.State<FoldState>(id);
        if (!st.made)
        {
            st.made = true;
            st.open = defaultOpen;
        }
        if (pressed)
            st.open = !st.open;
        open = Clamp01(Anim(id, 1, st.open ? 1.0f : 0.0f, kSmooth));
        return st.open || open > 0.002f;
    }

    void Ui::BeginFold(Id id, float open, bool implicit, bool tree)
    {
        const FoldState& st = ctx_.State<FoldState>(id);
        Fold fd;
        fd.id = id;
        fd.open = open;
        fd.implicit = implicit;
        fd.tree = tree;
        fd.depth = windows_.size();
        fd.indent = tree ? 22.0f : 0.0f;
        fd.start = ctx_.CursorPos();
        fd.clipped = open < 0.999f;
        // open and at rest: a container fitted to its content; moving: as tall as it shows, its content clipped there
        ContainerOptions co;
        co.fillWidth = true;
        co.size = Vec2(0.0f, fd.clipped ? std::max(open * st.full, 0.5f) : 0.0f);
        ctx_.BeginContainer(HashInt(0x464F4C44, id), co);
        if (fd.clipped)
            ctx_.PushClipRect(Rect(-1e5f, fd.start.y - 4.0f, 1e5f, fd.start.y + open * st.full), true);
        alpha_.push_back(alpha_.back() * Smoothstep(0.05f, 0.9f, open));
        if (fd.indent > 0.0f)
            ctx_.Indent(fd.indent);
        folds_.push_back(fd);
        rowIndex_ = 0;
        row_ = PendingRow();
    }

    void Ui::EndFold()
    {
        if (folds_.empty())
            return;
        const Fold fd = folds_.back();
        folds_.pop_back();
        // its content's height, for the next frame
        const float spacing = ctx_.Metrics().itemSpacing.y;
        const float full = std::max(ctx_.CursorPos().y - fd.start.y - spacing, 0.0f);
        ctx_.State<FoldState>(fd.id).full = full;
        if (fd.indent > 0.0f)
            ctx_.Unindent(fd.indent);
        if (fd.tree && full > 0.0f)
        {
            // a tree's guide: a hairline down its children, under the chevron of their node
            esia::Painter p = Painter();
            const float gx = std::round((fd.start.x + 7.5f) * pixelScale_) / pixelScale_;
            p.Rect(Rect(gx, fd.start.y, gx + 1.0f / pixelScale_, fd.start.y + full - 2.0f), Style().Fill(Ink(0.10f)));
        }
        alpha_.pop_back();
        if (fd.clipped)
            ctx_.PopClipRect();
        ctx_.EndContainer();
        if (fd.clipped)
            ctx_.SetCursorPos(Vec2(ctx_.CursorPos().x, fd.start.y + fd.open * (full + spacing)));
        rowIndex_ = 0;
        row_ = PendingRow();
    }

    void Ui::EndImplicitFolds()
    {
        while (!folds_.empty() && folds_.back().implicit && folds_.back().depth == windows_.size())
            EndFold();
    }

    void Ui::EndFoldsOf(std::size_t depth)
    {
        while (!folds_.empty() && folds_.back().depth >= depth)
            EndFold();
    }

    // A header (a box with a chevron) or a tree node (a row with a chevron): its toggle; true while its content shows.
    bool Ui::HeaderItem(const char* label, bool defaultOpen, bool tree, Icon icon, Id& id, float& open)
    {
        const std::string_view text = Visible(label);
        id = IdOf(label);
        const Rect r = ItemRect(Vec2(ContentWidth(), tree ? 32.0f : m::kElement));
        lastId_ = id;
        bool pressed = false, hovered = false, held = false;
        if (AddItem(id, r, ItemFlags_Focusable))
        {
            const ButtonResult b = Behavior(id, r);
            pressed = b.pressed;
            hovered = b.hovered;
            held = b.held;
            if (!inert_ && ctx_.KeyboardFocusId() == id && (ctx_.KeyPressed(Key::Enter, id, false) || ctx_.KeyPressed(Key::Space, id, false)))
                pressed = true;
        }
        lastHovered_ = hovered;
        if (hovered)
            ctx_.SetMouseCursor(MouseCursor::Hand);
        const bool shown = FoldToggle(id, defaultOpen, pressed, open);
        const float hover = Anim(id, 0, hovered ? 1.0f : 0.0f);
        const float press = Anim(id, 2, held ? 1.0f : 0.0f);
        esia::Painter p = Painter();
        const float cy = r.Center().y;
        // the chevron turns down as it opens (at rest: the icon as drawn, on the pixel grid)
        const auto chevron = [&](Vec2 c, float size, Color color) {
            if (open <= 0.0f)
                DrawIcon(p, Icon::ChevronRight, c, size, color);
            else if (open >= 1.0f)
                DrawIcon(p, Icon::ChevronDown, c, size, color);
            else
            {
                IconMotion mo;
                mo.angle = -1.5707963f * (1.0f - open);
                DrawIconVector(p, Icon::ChevronDown, c, size, color, mo);
            }
        };
        if (tree)
        {
            // the chevron's left edge on the column, the row's surface reaching a little behind it
            const Color fill = Ink(0.045f * hover + 0.03f * press);
            if (fill.a > 0.002f)
                p.Rect(Rect(r.min.x - std::min(bleed_, 6.0f), r.min.y, r.max.x, r.max.y), Style().Radius(m::kRadiusInner).Fill(fill));
            chevron(Vec2(r.min.x + 7.5f, cy), 15.0f, Mix(Ink(0.48f + 0.3f * hover), shown_.accent, open));
            float x = r.min.x + 22.0f;
            if (icon != Icon::None)
            {
                DrawIcon(p, icon, Vec2(x + 8.0f, cy), 16.0f, Mix(Ink(0.62f + 0.2f * hover), shown_.accent, 0.6f * open));
                x += 24.0f;
            }
            Label(p, x, cy, {fonts_.text, m::kFont}, Ink(0.84f + 0.1f * std::max(hover, open)), text, 0.0f, r.max.x - x - 8.0f);
            FocusRing(p, r, m::kRadiusInner, id);
        }
        else
        {
            // the box reaches back to the content's edge: its chevron's left edge stands on the column
            const Rect box(r.min.x - bleed_, r.min.y, r.max.x, r.max.y);
            p.PushScale(box.Center(), 1.0f - 0.01f * press);
            p.Rect(box, Style().Radius(m::kRadiusControl).Fill(Ink(0.03f + 0.035f * hover + 0.015f * open)).Stroke(1.0f / pixelScale_, Ink(0.06f + 0.04f * open), 0.0f));
            chevron(Vec2(box.min.x + 12.0f + m::kIcon * 0.5f, cy), m::kIcon, Mix(Ink(0.55f + 0.3f * hover), shown_.accent, open));
            float x = box.min.x + 12.0f + m::kIcon + 10.0f;
            if (icon != Icon::None)
            {
                DrawIcon(p, icon, Vec2(x + m::kIcon * 0.5f, cy), m::kIcon, Ink(0.7f));
                x += m::kIcon + 10.0f;
            }
            Label(p, x, cy, {fonts_.textSemibold, m::kFont}, Ink(0.92f), text, 0.0f, r.max.x - x - 12.0f);
            p.PopScale();
            FocusRing(p, r, m::kRadiusControl, id);
        }
        return shown;
    }

    bool Ui::CollapsingHeader(const char* label, bool defaultOpen)
    {
        EndImplicitFolds();   // the header before it ends here
        Id id = 0;
        float open = 0.0f;
        if (!HeaderItem(label, defaultOpen, false, Icon::None, id, open))
            return false;
        BeginFold(id, open, true, false);
        return true;
    }

    bool Ui::BeginCollapse(const char* label, bool defaultOpen)
    {
        Id id = 0;
        float open = 0.0f;
        if (!HeaderItem(label, defaultOpen, false, Icon::None, id, open))
            return false;
        BeginFold(id, open, false, false);
        return true;
    }

    void Ui::EndCollapse()
    {
        EndImplicitFolds();
        EndFold();
    }

    bool Ui::TreeNode(const char* label, bool defaultOpen, Icon icon)
    {
        Id id = 0;
        float open = 0.0f;
        if (!HeaderItem(label, defaultOpen, true, icon, id, open))
            return false;
        BeginFold(id, open, false, true);
        return true;
    }

    void Ui::TreePop()
    {
        EndImplicitFolds();
        EndFold();
    }

    // ------------------------------------------------------------------ tab bars
    bool Ui::BeginTabBar(const char* idLabel)
    {
        row_ = PendingRow();
        TabBarFrame tb;
        tb.id = IdOf(idLabel);
        tb.bar = ItemRect(Vec2(ContentWidth(), m::kElement));
        ctx_.ItemAdd(0, tb.bar);
        tb.x = tb.bar.min.x + 6.0f;
        tb.start = Vec2(ctx_.CursorPos().x, ctx_.CursorPos().y + m::kStep);
        tb.width = ContentWidth();
        tb.folds = folds_.size();
        tb.prevInert = inert_;
        tabBars_.push_back(std::move(tb));
        return true;
    }

    bool Ui::BeginTabItem(const char* label, Icon icon)
    {
        if (tabBars_.empty())
            return false;
        if (tabBars_.back().itemOpen)
            EndTabItem();
        TabBarFrame& tb = tabBars_.back();
        TabBarState& st = ctx_.State<TabBarState>(tb.id);
        const std::string_view text = Visible(label);
        const Id id = HashLabel(label, tb.id);
        const text::FontRef f{fonts_.text, m::kFont};
        const float w = std::ceil(Measure(f, text).x + (icon != Icon::None ? m::kIcon + 8.0f : 0.0f) + 32.0f);
        const Rect r(tb.x, tb.bar.min.y + m::kStep, tb.x + w, tb.bar.max.y - m::kStep);
        tb.x += w + m::kStep;
        const int index = tb.count++;
        if (!st.selected)
            st.selected = id;
        bool hovered = false, held = false;
        lastId_ = id;
        if (AddItem(id, r, ItemFlags_Focusable))
        {
            const ButtonResult b = Behavior(id, r, ButtonFlags_PressOnClick);
            hovered = b.hovered;
            held = b.held;
            bool pick = b.pressed;
            if (!inert_ && ctx_.KeyboardFocusId() == id && (ctx_.KeyPressed(Key::Enter, id, false) || ctx_.KeyPressed(Key::Space, id, false)))
                pick = true;
            if (pick && st.selected != id)
            {
                st.dir = index > st.selectedIndex ? 1.0f : -1.0f;
                st.selected = id;
            }
        }
        lastHovered_ = hovered;
        if (hovered)
            ctx_.SetMouseCursor(MouseCursor::Hand);
        const bool sel = st.selected == id;
        if (sel)
            st.selectedIndex = index;
        TabBarEntry e;
        e.id = id;
        e.label.assign(text);
        e.icon = icon;
        e.rect = r;
        e.hover = Anim(id, 0, hovered ? 1.0f : 0.0f);
        e.press = Anim(id, 2, held ? 1.0f : 0.0f);
        tb.tabs.push_back(std::move(e));

        // the content: the selected tab's; those left fade out (drawn inert) to the other side
        TabItemFade& tf = ctx_.State<TabItemFade>(HashInt(0x54494644, id));
        if (!tf.made)
        {
            tf.made = true;
            tf.selected = sel;
            tf.shown.Snap(sel ? 1.0f : 0.0f);
        }
        if (sel != tf.selected)
        {
            tf.selected = sel;
            tf.dir = st.dir;
        }
        tf.shown.target = sel ? 1.0f : 0.0f;
        tf.shown.Step(dt_, sel ? kSmooth : Motion{0.18f, 1.0f});
        if (!sel && tf.shown.value <= 0.003f)
            return false;
        const float shown = Clamp01(tf.shown.value);
        const float offset = (sel ? 1.0f : -1.0f) * tf.dir * 18.0f * (1.0f - shown);
        ctx_.SetCursorPos(Vec2(tb.start.x + offset, tb.start.y));
        ContainerOptions co;
        co.size = Vec2(tb.width, 0.0f);
        ctx_.BeginContainer(HashInt(0x54424954, id), co);
        tb.itemOpen = true;
        tb.itemInert = !sel;
        inert_ = tb.prevInert || !sel;
        alpha_.push_back(alpha_.back() * Smoothstep(0.3f, 1.0f, shown));
        tb.itemFolds = folds_.size();
        rowIndex_ = 0;
        row_ = PendingRow();
        return true;
    }

    void Ui::EndTabItem()
    {
        if (tabBars_.empty() || !tabBars_.back().itemOpen)
            return;
        while (folds_.size() > tabBars_.back().itemFolds)
            EndFold();
        TabBarFrame& tb = tabBars_.back();
        const float spacing = ctx_.Metrics().itemSpacing.y;
        const float h = std::max(ctx_.CursorPos().y - tb.start.y - spacing, 0.0f);
        if (!tb.itemInert)
            tb.selectedHeight = h;
        alpha_.pop_back();
        ctx_.EndContainer();
        tb.itemOpen = false;
        inert_ = tb.prevInert;
        row_ = PendingRow();
    }

    void Ui::EndTabBar()
    {
        if (tabBars_.empty())
            return;
        if (tabBars_.back().itemOpen)
            EndTabItem();
        const TabBarFrame tb = std::move(tabBars_.back());
        tabBars_.pop_back();
        TabBarState& st = ctx_.State<TabBarState>(tb.id);
        // the selected tab went away: the first one
        Rect sel;
        for (const TabBarEntry& t : tb.tabs)
            if (t.id == st.selected)
                sel = t.rect;
        if (sel.Empty() && !tb.tabs.empty())
        {
            st.selected = tb.tabs[0].id;
            sel = tb.tabs[0].rect;
        }
        // the hub's footer shows where you are: the selected tab of the selected hub tab's own tab bar
        if (hubContentOpen_ && tabBars_.empty() && !inert_)
            for (const TabBarEntry& t : tb.tabs)
                if (t.id == st.selected)
                    hubCrumb_ = t.label;
        esia::Painter p = Painter();
        // the track around its tabs: a capsule holding them 6 in, capsules too (concentric: the circles of their ends
        // share their centers, so the edges run 6 apart all the way round)
        const float right = tb.tabs.empty() ? tb.bar.min.x + 60.0f : tb.tabs.back().rect.max.x + m::kStep;
        const Rect track(tb.bar.min.x, tb.bar.min.y, right, tb.bar.max.y);
        p.Rect(track, Style().Radius(track.Height() * 0.5f).Fill(Ink(0.035f)).Stroke(1.0f / pixelScale_, Ink(0.07f), 0.0f));
        // the pill: glides to the selected tab, stretching with its speed
        if (!sel.Empty())
        {
            const float left = tb.bar.min.x;
            if (!st.placed)
            {
                st.pillX.Snap(sel.min.x - left);
                st.pillW.Snap(sel.Width());
                st.placed = true;
            }
            st.pillX.target = sel.min.x - left;
            st.pillW.target = sel.Width();
            st.pillX.Step(dt_, kBouncy);
            st.pillW.Step(dt_, kBouncy);
            const float stretch = std::min(std::fabs(st.pillX.velocity) * 0.012f, 10.0f);
            const float px = left + st.pillX.value;
            const Rect pill(px - stretch * 0.5f, sel.min.y, px + st.pillW.value + stretch * 0.5f, sel.max.y);
            // a piece of liquid glass: what is under it bent and lit at its rim, a breath of the theme's surface in it
            GlassMaterial g;
            g.blur = 6.0f;
            g.refraction = 7.0f;
            g.bezel = pill.Height() * 0.5f;
            g.dispersion = 0.4f;
            g.specular = 1.0f;
            g.saturation = 1.35f;
            g.brightness = 0.05f;
            g.legibility = 0.6f;
            g.noise = 0.0f;
            g.tint = Mix(shown_.raised, shown_.ink, Mix(0.08f, 0.24f, Dark())).WithAlpha(0.5f);
            g.rim = Mix(Color::Black(0.10f), Color::White(0.30f), Dark());
            p.Rect(pill, Style().Radius(pill.Height() * 0.5f).Glass(g).Shadow(Color::Black(Mix(0.10f, 0.26f, Dark())), 10.0f, Vec2(0.0f, 3.0f)));
        }
        // the labels: the selected one brightest, its icon in the accent
        for (const TabBarEntry& t : tb.tabs)
        {
            const bool on = t.id == st.selected;
            const float k = Clamp01(Anim(t.id, 1, on ? 1.0f : 0.0f, kSmooth));
            const Color fill = Ink((0.04f * t.hover + 0.03f * t.press) * (1.0f - k));
            if (fill.a > 0.002f)
                p.Rect(t.rect, Style().Radius(t.rect.Height() * 0.5f).Fill(fill));
            const Color ink = Mix(Ink(0.60f + 0.2f * t.hover), Ink(0.96f), k);
            // the icon and the label centered in the tab
            const float contentW = (t.icon != Icon::None ? m::kIcon + 8.0f : 0.0f) + Measure({fonts_.text, m::kFont}, t.label).x;
            float x = t.rect.Center().x - contentW * 0.5f;
            const float cy = t.rect.Center().y;
            if (t.icon != Icon::None)
            {
                DrawIcon(p, t.icon, Vec2(x + m::kIcon * 0.5f, cy), m::kIcon, Mix(ink, shown_.accent, k));
                x += m::kIcon + 8.0f;
            }
            Label(p, x, cy, {fonts_.text, m::kFont}, ink, t.label);
            FocusRing(p, t.rect, t.rect.Height() * 0.5f, t.id);
        }
        // what follows goes under the selected tab's content: its height glides from one tab's to the next
        if (tb.selectedHeight >= 0.0f)
        {
            if (!st.measured)
            {
                st.height.Snap(tb.selectedHeight);
                st.measured = true;
            }
            st.height.target = tb.selectedHeight;
        }
        st.height.Step(dt_, kSmooth);
        const float spacing = ctx_.Metrics().itemSpacing.y;
        ctx_.SetCursorPos(Vec2(tb.start.x, tb.start.y + std::max(st.height.value, 0.0f) + spacing));
        rowIndex_ = 0;
        row_ = PendingRow();
    }
}
