// Cipher - popups, combos, menus, context menus and modal dialogs (cipher.hpp). A popup is Context's while it is
// open; when it closes it keeps being drawn - without input, at its last place - until it has faded out, so closing
// is as animated as opening.
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
        struct PopState
        {
            Spring shown;
            Rect rect;        // where it was last drawn: its ghost's place while it fades out
            Vec2 anchor;      // what it grows out of
            Id child = 0;     // the submenu open from it
            // a combo's selection: a pill gliding to the chosen row (its top from the popup's, the row's height)
            Spring selY;
            float selTarget = -1.0f, selH = 0.0f;
            bool selPlaced = false;
            int drop = 0;     // a drop-down: 1 rolls down out of its control, -1 up out of it; 0 grows out of its anchor
        };
        struct ModalState
        {
            bool open = false;
            Spring shown, shake, h;  // h: its height, gliding to its content's
            float height = 180.0f;   // its content as measured last frame
        };
        Id PopupIdOf(Id id) { return HashInt(0x504F50, id); }
        Id ModalIdOf(const char* name) { return HashLabel(name, 0x4D4F44u); }
    }

    // ------------------------------------------------------------------ the popup surface
    bool Ui::BeginPopupEx(Id pid, Vec2 pos, Vec2 pivot, Vec2 size, Vec2 anchor, bool combo, bool drop)
    {
        PopState& ps = ctx_.State<PopState>(pid);
        const bool open = ctx_.IsPopupOpen(pid);
        const bool appearing = ps.shown.value <= 0.003f;
        ps.shown.target = open ? 1.0f : 0.0f;
        ps.shown.Step(dt_, open ? Motion{0.28f, 0.86f} : Motion{0.16f, 1.0f});
        const float a = Clamp01(ps.shown.value);
        WinFrame wf;
        wf.id = pid;
        wf.popup = true;
        wf.combo = combo;
        if (open)
        {
            PopupOptions po;
            po.pos = pos;
            po.pivot = pivot;
            po.padding = Vec2(6.0f, 6.0f);
            if (size.x > 0.0f)
                po.minSize.x = po.maxSize.x = size.x;
            if (size.y > 0.0f)
                po.minSize.y = po.maxSize.y = size.y;
            if (!ctx_.BeginPopup(pid, po))
                return false;
            ps.rect = ctx_.CurrentWindow()->GetRect();
            ps.anchor = anchor;
            ps.drop = drop ? (pivot.y > 0.5f ? -1 : 1) : 0;
        }
        else if (a > 0.003f && !ps.rect.Empty())
        {
            char name[40];
            std::snprintf(name, sizeof(name), "##cipher-ghost-%08x", (unsigned)pid);
            ctx_.SetNextWindowPos(ps.rect.min, Cond::Always);
            ctx_.SetNextWindowSize(ps.rect.Size(), Cond::Always);
            WindowOptions o;
            o.flags = esia::WindowFlags_NoInputs | esia::WindowFlags_NoMove | esia::WindowFlags_NoResize | esia::WindowFlags_NoFocus |
                      esia::WindowFlags_NoScroll | esia::WindowFlags_NoBringToFront;
            o.layer = WindowLayer::Overlay;
            o.padding = Vec2(6.0f, 6.0f);
            o.minSize = Vec2(1.0f, 1.0f);
            ctx_.Begin(name, o);
            wf.ghost = true;
        }
        else
        {
            ps.rect = Rect();
            return false;
        }
        wf.begun = true;
        const Rect r = ctx_.CurrentWindow()->GetRect();
        alpha_.push_back(ps.drop ? Smoothstep(0.15f, 0.75f, a) : a);
        esia::Painter p = Painter();
        DrawList& dl = ctx_.WindowDrawList();
        dl.PushClipRect(Rect(Vec2(-64.0f, -64.0f), ctx_.DisplaySize() + Vec2(64.0f, 64.0f)), false);
        if (ps.drop)
        {
            // a drop-down rolls out of its control and back into it: its surface grows from the edge at the control,
            // what is in it shown (and taking the pointer) as far as the surface has come
            const float h = std::max(r.Height() * a, 1.0f);
            const Rect rolled = ps.drop > 0 ? Rect(r.min.x, r.min.y, r.max.x, r.min.y + h) : Rect(r.min.x, r.max.y - h, r.max.x, r.max.y);
            p.SetAlpha(Smoothstep(0.0f, 0.3f, a));
            Surface(p, rolled, std::min(m::kRadiusPanel, h * 0.5f), SurfaceKind::Popup, shown_.raised, 0.8f * a);
            p.SetAlpha(alpha_.back());
            dl.PopClipRect();
            ctx_.PushClipRect(rolled, true);
            wf.drop = true;
        }
        else
        {
            const Vec2 origin(std::clamp(ps.anchor.x, r.min.x, r.max.x), std::clamp(ps.anchor.y, r.min.y, r.max.y));
            p.PushScale(origin, 0.94f + 0.06f * a);
            Surface(p, r, m::kRadiusPanel, SurfaceKind::Popup, shown_.raised, 0.8f);
            p.PopScale();
            dl.PopClipRect();
        }
        // a combo's selection pill, under its rows: it glides to the row picked (it opens on the chosen one)
        if (combo && ps.selTarget >= 0.0f)
        {
            if (!ps.selPlaced || appearing)
            {
                ps.selY.Snap(ps.selTarget);
                ps.selPlaced = true;
            }
            ps.selY.target = ps.selTarget;
            ps.selY.Step(dt_, kBouncy);
            const float stretch = std::min(std::fabs(ps.selY.velocity) * 0.01f, 8.0f);
            const float y = r.min.y + ps.selY.value;
            p.Rect(Rect(r.min.x + 6.0f, y - stretch * 0.5f, r.max.x - 6.0f, y + ps.selH + stretch * 0.5f),
                   Style().Radius(m::kRadiusControl).Fill(shown_.accent.Fade(0.16f)).Stroke(1.0f / pixelScale_, shown_.accent.Fade(0.35f), 0.0f));
        }
        ps.selTarget = -1.0f;
        windows_.push_back(wf);
        return true;
    }

    void Ui::EndPopupEx()
    {
        if (windows_.empty())
            return;
        EndFoldsOf(windows_.size());
        const WinFrame wf = windows_.back();
        windows_.pop_back();
        alpha_.pop_back();
        if (wf.drop)
            ctx_.PopClipRect();
        if (wf.ghost)
            ctx_.End();
        else
            ctx_.EndPopup();
    }

    void Ui::OpenPopup(const char* id) { ctx_.OpenPopup(PopupIdOf(ctx_.GetId(id))); }

    bool Ui::BeginPopup(const char* id)
    {
        const Vec2 mouse = ctx_.Input().MousePos();
        return BeginPopupEx(PopupIdOf(ctx_.GetId(id)), Vec2(kNoMousePos, kNoMousePos), Vec2(0.0f, 0.0f), Vec2(0.0f, 0.0f), mouse);
    }

    void Ui::EndPopup() { EndPopupEx(); }

    bool Ui::BeginHubProfileMenu()
    {
        const Id menu = HashLabel("##cipher-profile-menu", 0x50524F46u);
        const Rect c = hubAvatar_;
        if (c.Empty())
            return BeginPopupEx(menu, Vec2(0.0f, 0.0f), Vec2(0.0f, 1.0f), Vec2(0.0f, 0.0f), Vec2(0.0f, 0.0f));
        // under the avatar, its right edge on the avatar's, rolling down out of it
        return BeginPopupEx(menu, Vec2(c.max.x, c.max.y + 6.0f), Vec2(1.0f, 0.0f), Vec2(220.0f, 0.0f), Vec2(c.Center().x, c.max.y), false, true);
    }

    void Ui::CloseCurrentPopup()
    {
        if (!windows_.empty() && windows_.back().popup && !windows_.back().ghost)
            ctx_.ClosePopup(windows_.back().id);
    }

    bool Ui::BeginPopupContextItem(const char* id)
    {
        const Id pid = PopupIdOf(ctx_.GetId(id));
        if (lastHovered_ && ctx_.Input().MouseReleased(MouseButton::Right))
        {
            ctx_.OpenPopup(pid);
            ctx_.State<PopState>(pid).anchor = ctx_.Input().MousePos();
        }
        return BeginPopupEx(pid, Vec2(kNoMousePos, kNoMousePos), Vec2(0.0f, 0.0f), Vec2(210.0f, 0.0f), ctx_.State<PopState>(pid).anchor);
    }

    // A row of a popup: a column for a check mark or an icon, the label, a shortcut or a submenu's chevron at the right.
    bool Ui::PopupRow(Id id, std::string_view label, std::string_view shortcut, Icon icon, bool check, bool enabled, bool submenu, bool& hovered)
    {
        const Rect r = ItemRect(Vec2(ContentWidth(), 34.0f));
        lastId_ = id;
        hovered = false;
        if (!AddItem(id, r, enabled ? 0u : ItemFlags_Disabled))
        {
            lastHovered_ = false;
            return false;
        }
        const ButtonResult b = Behavior(id, r);
        hovered = b.hovered && enabled;
        lastHovered_ = hovered;
        // another row of this popup under the pointer closes the submenu open from it
        if (hovered && !windows_.empty() && windows_.back().popup)
        {
            PopState& parent = ctx_.State<PopState>(windows_.back().id);
            if (parent.child && parent.child != PopupIdOf(id) && ctx_.IsPopupOpen(parent.child))
                ctx_.ClosePopup(parent.child);
        }
        const float hover = Anim(id, 0, hovered ? 1.0f : 0.0f);
        const float press = Anim(id, 1, b.held && enabled ? 1.0f : 0.0f);
        const float on = Anim(id, 2, check ? 1.0f : 0.0f, kSmooth);
        // a combo's chosen row: where its selection pill goes
        if (check && !windows_.empty() && windows_.back().combo)
        {
            PopState& ps = ctx_.State<PopState>(windows_.back().id);
            ps.selTarget = r.min.y - ctx_.CurrentWindow()->GetRect().min.y;
            ps.selH = r.Height();
        }
        esia::Painter p = Painter();
        const Color fill = Ink(0.065f * hover + 0.04f * press);
        if (fill.a > 0.002f)
            p.Rect(r, Style().Radius(m::kRadiusControl).Fill(fill));
        const float cy = r.Center().y;
        if (icon != Icon::None)
            DrawIcon(p, icon, Vec2(r.min.x + 19.0f, cy), m::kIcon, Ink(enabled ? 0.62f + 0.3f * hover : 0.3f));
        else if (on > 0.004f)
        {
            IconMotion mo;
            mo.drawn = Clamp01(on);
            DrawIconVector(p, Icon::Check, Vec2(r.min.x + 19.0f, cy), 16.0f, shown_.accent, mo, 2.4f);
        }
        const float x = r.min.x + 38.0f;
        const float right = r.max.x - 12.0f - (submenu ? 16.0f : 0.0f);
        float shortW = 0.0f;
        if (!shortcut.empty())
        {
            const text::FontRef sf{fonts_.text, 13.0f};
            shortW = Measure(sf, shortcut).x + 12.0f;
            Label(p, right, cy, sf, Ink(enabled ? 0.42f : 0.24f), shortcut, 1.0f);
        }
        Label(p, x, cy, {fonts_.text, m::kFont}, Ink(enabled ? 0.92f : 0.34f), label, 0.0f, std::max(right - shortW - x, 10.0f));
        if (submenu)
            DrawIcon(p, Icon::ChevronRight, Vec2(r.max.x - 18.0f, cy), 16.0f, Ink(0.5f + 0.4f * hover));
        if (hovered)
            ctx_.SetMouseCursor(MouseCursor::Hand);
        return b.pressed && enabled;
    }

    // ------------------------------------------------------------------ combos
    bool Ui::BeginCombo(const char* label, const char* preview)
    {
        const std::string_view text = Visible(label);
        const Id id = IdOf(label);
        const Id pid = PopupIdOf(id);
        Rect field;
        if (!row_.on && !text.empty())
            Row(label);
        const bool inRow = TakeRow(220.0f, field);
        if (!inRow)
        {
            const Rect row = ItemRect(Vec2(ContentWidth(), m::kRow));
            const float col = text.empty() ? 0.0f : LabelColumn();
            field = Rect(row.min.x + col, row.Center().y - m::kControl * 0.5f, row.max.x, row.Center().y + m::kControl * 0.5f);
            if (!text.empty())
            {
                esia::Painter p = Painter();
                Label(p, row.min.x, row.Center().y, {fonts_.text, m::kFont}, Ink(0.80f), text, 0.0f, col - 12.0f);
            }
        }
        lastId_ = id;
        const bool disabled = disabled_.back();
        bool open = ctx_.IsPopupOpen(pid);
        if (AddItem(id, field, ItemFlags_Focusable | (disabled ? ItemFlags_Disabled : 0u)))
        {
            const ButtonResult b = Behavior(id, field, ButtonFlags_PressOnClick);
            lastHovered_ = b.hovered;
            const bool keyOpen = !inert_ && ctx_.KeyboardFocusId() == id && (ctx_.KeyPressed(Key::Enter, id, false) || ctx_.KeyPressed(Key::Space, id, false));
            if ((b.pressed || keyOpen) && !disabled)
            {
                if (open)
                    ctx_.ClosePopup(pid);
                else
                    ctx_.OpenPopup(pid);
                open = !open;
            }
            if (b.hovered && !disabled)
                ctx_.SetMouseCursor(MouseCursor::Hand);
            const float hover = Anim(id, 0, b.hovered ? 1.0f : 0.0f);
            const float on = Anim(id, 1, open ? 1.0f : 0.0f, kSmooth);
            esia::Painter p = Painter();
            p.Rect(field, Style().Radius(m::kRadiusInner).Fill(Ink(0.035f + 0.03f * hover)).Stroke(1.0f / pixelScale_, Mix(Ink(0.12f), shown_.accent.Fade(0.8f), Clamp01(on)), 0.0f));
            // the choice shown: a new one rises into place as the old one leaves
            MorphLabel(p, HashInt(0x434F4D, id), field.min.x + 11.0f, field.Center().y, {fonts_.text, m::kFont}, Ink(0.92f), preview ? preview : "", 0.0f,
                       field.Width() - 42.0f);
            IconMotion mo;
            mo.angle = 3.14159265f * on;
            if (on <= 0.0f)
                DrawIcon(p, Icon::ChevronDown, Vec2(field.max.x - 16.0f, field.Center().y), 16.0f, Ink(0.5f + 0.3f * hover));
            else
                DrawIconVector(p, Icon::ChevronDown, Vec2(field.max.x - 16.0f, field.Center().y), 16.0f, Ink(0.5f + 0.3f * hover), mo);
            FocusRing(p, field, m::kRadiusInner, id);
        }
        else
            lastHovered_ = false;
        const Vec2 d = ctx_.DisplaySize();
        const bool above = field.max.y + 260.0f > d.y && field.min.y > 260.0f;
        return BeginPopupEx(pid, above ? Vec2(field.min.x, field.min.y - 6.0f) : Vec2(field.min.x, field.max.y + 6.0f), above ? Vec2(0.0f, 1.0f) : Vec2(0.0f, 0.0f),
                            Vec2(field.Width(), 0.0f), Vec2(field.Center().x, above ? field.min.y : field.max.y), true, true);
    }

    void Ui::EndCombo() { EndPopupEx(); }

    bool Ui::Combo(const char* label, int* current, const char* const* items, int count)
    {
        const char* preview = (*current >= 0 && *current < count) ? items[*current] : "";
        if (!BeginCombo(label, preview))
            return false;
        bool changed = false;
        for (int i = 0; i < count; ++i)
        {
            ctx_.PushId(i);
            if (MenuItem(items[i], nullptr, i == *current))
            {
                *current = i;
                changed = true;
            }
            ctx_.PopId();
        }
        EndCombo();
        return changed;
    }

    // ------------------------------------------------------------------ menus
    bool Ui::BeginMenuBar()
    {
        const Rect r = ItemRect(Vec2(ContentWidth(), 30.0f));
        ctx_.ItemAdd(0, r);
        menuBar_ = r;
        menuX_ = r.min.x - 8.0f;
        inMenuBar_ = true;
        return true;
    }

    void Ui::EndMenuBar()
    {
        inMenuBar_ = false;
        esia::Painter p = Painter();
        p.HLine(menuBar_.min.x - 4.0f, menuBar_.max.x + 4.0f, menuBar_.max.y + 3.0f, Ink(0.07f), 1.0f / pixelScale_);
        ctx_.ItemSize(Vec2(0.0f, 2.0f));
    }

    bool Ui::BeginMenu(const char* label, bool enabled)
    {
        const std::string_view text = Visible(label);
        const Id id = IdOf(label);
        const Id pid = PopupIdOf(id);
        const float width = 236.0f;
        if (inMenuBar_)
        {
            const text::FontRef f{fonts_.text, m::kFont};
            const float w = Measure(f, text).x + 20.0f;
            const Rect r(menuX_, menuBar_.min.y, menuX_ + w, menuBar_.max.y);
            menuX_ += w + 2.0f;
            bool open = ctx_.IsPopupOpen(pid);
            if (AddItem(id, r, enabled ? 0u : ItemFlags_Disabled))
            {
                const ButtonResult b = Behavior(id, r, ButtonFlags_PressOnClick);
                if (b.pressed && enabled)
                {
                    if (open)
                        ctx_.ClosePopup(pid);
                    else
                    {
                        ctx_.OpenPopup(pid);
                        menuBarOpen_ = pid;
                    }
                    open = !open;
                }
                // while a menu of the bar is open, the pointer moves the opening along the bar
                if (b.hovered && enabled && !open && menuBarOpen_ && menuBarOpen_ != pid && ctx_.IsPopupOpen(menuBarOpen_))
                {
                    ctx_.ClosePopup(menuBarOpen_);
                    ctx_.OpenPopup(pid);
                    menuBarOpen_ = pid;
                    open = true;
                }
                const float hover = Anim(id, 0, (b.hovered || open) ? 1.0f : 0.0f);
                esia::Painter p = Painter();
                if (hover > 0.004f)
                    p.Rect(r.Expanded(0.0f, -2.0f), Style().Radius(m::kRadiusInner).Fill(Ink(0.065f * hover)));
                Label(p, r.min.x + 10.0f, r.Center().y, f, Ink(enabled ? 0.74f + 0.2f * hover : 0.34f), text);
            }
            const bool shown = BeginPopupEx(pid, Vec2(r.min.x, r.max.y + 6.0f), Vec2(0.0f, 0.0f), Vec2(width, 0.0f), Vec2(r.Center().x, r.max.y), false, true);
            if (shown)
            {
                inMenuBar_ = false;
                openMenus_.push_back(true);
            }
            return shown;
        }
        // a submenu: a row of the menu it is in, opening to its right while hovered
        bool hovered = false;
        const bool pressed = PopupRow(id, text, {}, Icon::None, false, enabled, true, hovered);
        const Rect r = ctx_.LastItemRect();
        if ((hovered || pressed) && enabled && !ctx_.IsPopupOpen(pid) && !windows_.empty() && windows_.back().popup && !windows_.back().ghost)
        {
            ctx_.OpenPopup(pid);
            ctx_.State<PopState>(windows_.back().id).child = pid;
        }
        // to the right of its menu, or to the left when the display has no room there: never over the menu it opens from
        // (its other rows stay where the pointer can reach them)
        const Vec2 d = ctx_.DisplaySize();
        const Rect menu = windows_.empty() ? r : ctx_.CurrentWindow()->GetRect();
        const bool left = menu.max.x + 4.0f + width > d.x - 8.0f && menu.min.x - 4.0f - width >= 8.0f;
        const bool shown = left ? BeginPopupEx(pid, Vec2(menu.min.x - 4.0f, r.min.y - 6.0f), Vec2(1.0f, 0.0f), Vec2(width, 0.0f), Vec2(r.min.x, r.Center().y))
                                : BeginPopupEx(pid, Vec2(menu.max.x + 4.0f, r.min.y - 6.0f), Vec2(0.0f, 0.0f), Vec2(width, 0.0f), Vec2(r.max.x, r.Center().y));
        if (shown)
            openMenus_.push_back(false);
        return shown;
    }

    void Ui::EndMenu()
    {
        EndPopupEx();
        if (openMenus_.empty())
            return;
        if (openMenus_.back())
            inMenuBar_ = true;   // back on the bar: the next menu is the bar's
        openMenus_.pop_back();
    }

    bool Ui::MenuItem(const char* label, const char* shortcut, bool selected, bool enabled, Icon icon)
    {
        bool hovered = false;
        const Id id = IdOf(label);
        const bool pressed = PopupRow(id, Visible(label), shortcut ? shortcut : "", icon, selected, enabled, false, hovered);
        // a pick in a menu closes the whole chain of popups (from the outermost one this item is in); in a combo's list
        // it stays open: the selection glides there, the click or Escape outside closes it
        const bool inCombo = !windows_.empty() && windows_.back().combo;
        if (pressed && !inCombo)
        {
            for (const WinFrame& wf : windows_)
                if (wf.popup && !wf.ghost)
                {
                    ctx_.ClosePopup(wf.id);
                    break;
                }
        }
        return pressed;
    }

    bool Ui::MenuItem(const char* label, const char* shortcut, bool* selected, bool enabled)
    {
        if (!MenuItem(label, shortcut, selected && *selected, enabled))
            return false;
        if (selected)
            *selected = !*selected;
        return true;
    }

    void Ui::MenuSeparator()
    {
        const Rect r = ItemRect(Vec2(ContentWidth(), 7.0f));
        ctx_.ItemAdd(0, r);
        esia::Painter p = Painter();
        p.HLine(r.min.x + 8.0f, r.max.x - 8.0f, r.Center().y, Ink(0.08f), 1.0f / pixelScale_);
    }

    // ------------------------------------------------------------------ modal dialogs
    void Ui::OpenModal(const char* name) { ctx_.State<ModalState>(ModalIdOf(name)).open = true; }
    void Ui::CloseModal(const char* name) { ctx_.State<ModalState>(ModalIdOf(name)).open = false; }

    bool Ui::BeginPopupModal(const char* name, bool* open, float width)
    {
        const Id mid = ModalIdOf(name);
        ModalState& ms = ctx_.State<ModalState>(mid);
        if (open && *open && !ms.open && ms.shown.value <= 0.003f)
            ms.open = true;   // the caller opened it through its flag
        if (open && !*open)
            ms.open = false;
        const bool appearing = ms.shown.value <= 0.003f;
        ms.shown.target = ms.open ? 1.0f : 0.0f;
        ms.shown.Step(dt_, ms.open ? Motion{0.34f, 0.9f} : Motion{0.2f, 1.0f});
        ms.shake.target = 0.0f;
        ms.shake.Step(dt_, Motion{0.16f, 0.22f});
        const float a = Clamp01(ms.shown.value);
        if (a <= 0.003f && !ms.open)
            return false;
        const Vec2 d = ctx_.DisplaySize();
        char wname[40];
        std::snprintf(wname, sizeof(wname), "##cipher-modal-%08x", (unsigned)mid);
        ctx_.SetNextWindowPos(Vec2(0.0f, 0.0f), Cond::Always);
        ctx_.SetNextWindowSize(d, Cond::Always);
        WindowOptions o;
        o.flags = esia::WindowFlags_NoMove | esia::WindowFlags_NoResize | esia::WindowFlags_NoScroll | (ms.open ? 0u : esia::WindowFlags_NoInputs);
        o.layer = WindowLayer::Overlay;
        o.padding = Vec2(0.0f, 0.0f);
        ctx_.Begin(wname, o);
        ctx_.FocusWindow(ctx_.CurrentWindow());
        WinFrame wf;
        wf.id = mid;
        wf.begun = true;
        wf.modal = true;
        // the scrim: everything behind frosted and dimmed; a click on it shakes the dialog (it wants an answer)
        esia::Painter p = Painter();
        GlassMaterial scrim;
        scrim.blur = 10.0f * a;
        scrim.refraction = 0.0f;
        scrim.bezel = 1.0f;
        scrim.dispersion = 0.0f;
        scrim.specular = 0.0f;
        scrim.rim = Color::Clear();
        scrim.tint = Color(0.0f, 0.0f, 0.0f, 0.38f * a);
        p.Rect(Rect(Vec2(0.0f, 0.0f), d), Style().Glass(scrim));
        const Id sid = ctx_.GetId("##scrim");
        ctx_.ItemAdd(sid, Rect(Vec2(0.0f, 0.0f), d), ItemFlags_Background);
        const ButtonResult sb = ctx_.ButtonBehavior(sid, Rect(Vec2(0.0f, 0.0f), d));
        if (sb.pressed && ms.open)
            ms.shake.velocity += 700.0f;
        if (open && ms.open && ctx_.KeyPressed(Key::Escape, 0, false))
            ms.open = false;
        // the dialog: grows in from 94 %, rises a little; its height glides to its content's (it appears at it)
        if (appearing)
            ms.h.Snap(ms.height);
        ms.h.target = ms.height;
        ms.h.Step(dt_, kSmooth);
        // on the pixel grid: everything in it moves as one (its glyphs, snapped to pixels, would shiver against its
        // surfaces as its springs settle)
        const auto snap = [&](float v) { return std::round(v * pixelScale_) / pixelScale_; };
        const float h = snap(std::clamp(ms.h.value, 120.0f, d.y - 80.0f));
        const Vec2 dc(snap(d.x * 0.5f + ms.shake.value), snap(d.y * 0.5f + 14.0f * (1.0f - a)));
        const Rect dr(snap(dc.x - width * 0.5f), snap(dc.y - h * 0.5f), snap(dc.x - width * 0.5f) + width, snap(dc.y - h * 0.5f) + h);
        const float a0 = p.Alpha();
        p.SetAlpha(a0 * a);
        p.PushScale(dr.Center(), 0.94f + 0.06f * a);
        Surface(p, dr, m::kRadiusWindow, SurfaceKind::Popup, shown_.raised, 1.0f);
        p.PopScale();
        const std::string_view title = Visible(name);
        Label(p, dr.min.x + 24.0f, dr.min.y + 30.0f, {fonts_.textSemibold, m::kFontTitle}, Ink(0.95f), title, 0.0f, width - 90.0f);
        if (open)
        {
            const Rect cb(dr.max.x - 42.0f, dr.min.y + 6.0f, dr.max.x - 6.0f, dr.min.y + 42.0f);
            const Id cid = ctx_.GetId("##close");
            ctx_.ItemAdd(cid, cb);
            const ButtonResult b = ctx_.ButtonBehavior(cid, cb);
            const float hover = Anim(cid, 0, b.hovered ? 1.0f : 0.0f);
            if (hover > 0.004f)
                p.Circle(cb.Center(), 18.0f, Style().Fill(Ink(0.07f * hover)));
            DrawIcon(p, Icon::X, cb.Center(), m::kIcon, Ink(0.55f + 0.4f * hover));
            if (b.pressed)
                ms.open = false;
        }
        p.SetAlpha(a0);
        // the body
        ctx_.SetCursorPos(Vec2(dr.min.x, dr.min.y + 54.0f));
        ChildOptions co;
        co.size = Vec2(width, std::max(h - 54.0f, 1.0f));
        co.padding = Vec2(24.0f, 0.0f);
        co.clipInset = Rect(-24.0f, 0.0f, -24.0f, -24.0f);
        ctx_.BeginChild("##modal-body", co);
        alpha_.push_back(a);
        windows_.push_back(wf);
        if (open)
            *open = ms.open;
        return true;
    }

    void Ui::EndPopupModal()
    {
        if (windows_.empty())
            return;
        EndFoldsOf(windows_.size());
        const WinFrame wf = windows_.back();
        windows_.pop_back();
        // its height for the next frame: what the body took, and the bottom margin
        const float used = ctx_.CursorPos().y - ctx_.ViewRect().min.y;
        ModalState& ms = ctx_.State<ModalState>(wf.id);
        ms.height = std::ceil(54.0f + used + 18.0f);
        ctx_.EndChild();
        alpha_.pop_back();
        ctx_.End();
    }
}
