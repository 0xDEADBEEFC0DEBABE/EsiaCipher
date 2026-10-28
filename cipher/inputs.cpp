// Cipher - text fields: one line, several lines, numbers (cipher.hpp). One editor behind all of them: a caret and a
// selection in UTF-8 bytes; the pointer (click, drag, a double click selects a word, a triple click everything); the
// keys (arrows, by words with Ctrl, Home / End, Backspace / Delete, Ctrl+A / C / X / V / Z / Y); the IME's composition
// at the caret; scrolling that glides.
//
// Every change moves: the caret - a slim bar glowing in the accent, a terminal's - glides to where it goes, stays lit
// while it moves and blinks at rest; a selection sweeps out
// of its anchor and shrinks back into the caret; a change to the text - typed, pasted, cut, undone, or made by the
// caller - fades what came in, fades out what went where it was, and slides the rest of its line from where it stood.
// A field without focus rolls a new value in as a label does (Ui::MorphLabel).
#include "cipher.hpp"
#include "esia/base/utf8.hpp"
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
        struct Snapshot
        {
            std::string text;
            int caret = 0, anchor = 0;
        };
        // bytes (of the text as shown) just typed: they fade in
        struct Fresh
        {
            int at = 0, len = 0;
            double time = 0.0;
        };
        // text just removed: it fades out where it was
        struct Gone
        {
            std::string s;
            float x = 0.0f;
            int line = 0;
            double time = 0.0;
        };
        struct EditState
        {
            int caret = 0, anchor = 0;
            Spring scrollX, scrollY;
            float wantX = -1.0f;            // up / down keep the column they started from
            double moved = -10.0;           // the caret's last move: it stays lit for a while after
            std::vector<Snapshot> undo, redo;
            double edited = -10.0;
            bool dragging = false;
            std::string number;             // InputFloat / InputInt: the text being typed
            // the motion: the text as drawn last frame, the caret's and the selection's springs, the slide of the
            // text after the last change (from shiftAt, on shiftLine), what fades in and out
            std::string last;
            bool lastValid = false, wasFocused = false;
            Spring caretX, caretY, caretW, selA, selZ, selShown, shift;
            int shiftAt = -1, shiftLine = -1;
            std::vector<Fresh> fresh;
            std::vector<Gone> gone;
        };
        struct Line
        {
            int start = 0, end = 0;          // bytes of the shown text
            std::vector<text::CaretStop> stops;
        };
        constexpr double kFreshTime = 0.16, kGoneTime = 0.18;
        constexpr Motion kCaretMotion{0.09f, 1.0f}, kSlideMotion{0.12f, 1.0f};

        bool WordByte(char c) { return ((unsigned char)c) >= 0x80 || std::isalnum((unsigned char)c) || c == '_'; }
        int PrevChar(const std::string& s, int i)
        {
            if (i <= 0)
                return 0;
            --i;
            while (i > 0 && ((unsigned char)s[(std::size_t)i] & 0xC0) == 0x80)
                --i;
            return i;
        }
        int NextChar(const std::string& s, int i)
        {
            const int n = (int)s.size();
            if (i >= n)
                return n;
            ++i;
            while (i < n && ((unsigned char)s[(std::size_t)i] & 0xC0) == 0x80)
                ++i;
            return i;
        }
        int PrevWord(const std::string& s, int i)
        {
            i = PrevChar(s, i);
            while (i > 0 && !WordByte(s[(std::size_t)i]))
                i = PrevChar(s, i);
            while (i > 0 && WordByte(s[(std::size_t)PrevChar(s, i)]))
                i = PrevChar(s, i);
            return i;
        }
        int NextWord(const std::string& s, int i)
        {
            const int n = (int)s.size();
            while (i < n && WordByte(s[(std::size_t)i]))
                i = NextChar(s, i);
            while (i < n && !WordByte(s[(std::size_t)i]))
                i = NextChar(s, i);
            return i;
        }
        bool Continuation(const std::string& s, int i) { return i > 0 && i < (int)s.size() && ((unsigned char)s[(std::size_t)i] & 0xC0) == 0x80; }
        // a password's text as bullets: byte offsets of the text <-> of the bullets (3 bytes each)
        int ToShown(const std::string& s, int off, bool password)
        {
            if (!password)
                return off;
            int k = 0;
            for (int i = 0; i < off;)
            {
                i = NextChar(s, i);
                ++k;
            }
            return k * 3;
        }
        int ToText(const std::string& s, int off, bool password)
        {
            if (!password)
                return off;
            int i = 0;
            for (int k = 0; k < off / 3 && i < (int)s.size(); ++k)
                i = NextChar(s, i);
            return i;
        }
        int LineOf(const std::vector<Line>& lines, int shownOff)
        {
            for (std::size_t k = 0; k < lines.size(); ++k)
                if (shownOff <= lines[k].end)
                    return (int)k;
            return (int)lines.size() - 1;
        }
        float XOf(const std::vector<Line>& lines, int shownOff)
        {
            const Line& l = lines[(std::size_t)LineOf(lines, shownOff)];
            const std::uint32_t local = (std::uint32_t)std::max(shownOff - l.start, 0);
            float x = 0.0f;
            for (const text::CaretStop& st : l.stops)
                if (st.offset <= local)
                    x = st.x;
            return x;
        }
    }

    bool Ui::EditText(Id id, const Rect& field, std::string& text, std::uint32_t flags, const char* hint, bool multiline, bool& entered, float radius)
    {
        entered = false;
        EditState& es = ctx_.State<EditState>(id);
        const bool disabled = disabled_.back();
        const bool readOnly = (flags & InputTextFlags_ReadOnly) != 0;
        const bool password = (flags & InputTextFlags_Password) != 0 && !multiline;
        const text::FontRef f{(flags & InputTextFlags_CharsDecimal) ? fonts_.mono : fonts_.text, (flags & InputTextFlags_CharsDecimal) ? m::kFontMono : m::kFont};
        lastId_ = id;
        if (!AddItem(id, field, ItemFlags_Focusable | (multiline ? ItemFlags_Wheel : 0u) | (disabled ? ItemFlags_Disabled : 0u)))
        {
            lastHovered_ = false;
            es.last = text;
            es.lastValid = true;
            return false;
        }
        const ButtonResult b = Behavior(id, field, ButtonFlags_PressOnClick | ButtonFlags_FocusOnClick);
        lastHovered_ = b.hovered;
        if (b.hovered && !disabled)
            ctx_.SetMouseCursor(MouseCursor::TextInput);
        const bool focused = !inert_ && ctx_.KeyboardFocusId() == id && !disabled;
        const InputState& in = ctx_.Input();
        const double now = in.Time();
        es.caret = std::clamp(es.caret, 0, (int)text.size());
        es.anchor = std::clamp(es.anchor, 0, (int)text.size());
        while (Continuation(text, es.caret))
            --es.caret;
        while (Continuation(text, es.anchor))
            --es.anchor;

        const float padX = 12.0f, lineH = std::round(f.size * 1.42f);
        const Rect inner(field.min.x + padX, field.min.y + (multiline ? 8.0f : 0.0f), field.max.x - padX, field.max.y - (multiline ? 8.0f : 0.0f));
        // the lines and their caret stops (a password's as bullets)
        static thread_local std::vector<Line> lines, oldLines;
        static thread_local std::string shown, oldShown;
        const auto layoutOf = [&](const std::string& txt, std::vector<Line>& out, std::string& bullets) {
            bullets.clear();
            if (password)
                for (int i = 0; i < (int)txt.size(); i = NextChar(txt, i))
                    bullets += "\xE2\x80\xA2";
            const std::string& s = password ? bullets : txt;
            out.clear();
            int start = 0;
            for (int i = 0; i <= (int)s.size(); ++i)
                if (i == (int)s.size() || (multiline && s[(std::size_t)i] == '\n'))
                {
                    Line l;
                    l.start = start;
                    l.end = i;
                    if (text_)
                        text_->CaretStops(f, std::string_view(s).substr((std::size_t)start, (std::size_t)(i - start)), l.stops);
                    if (l.stops.empty())
                        l.stops.push_back({0u, 0.0f});
                    out.push_back(std::move(l));
                    start = i + 1;
                }
        };
        const auto layout = [&]() { layoutOf(text, lines, shown); };
        layout();
        const auto hit = [&](Vec2 at) {
            int k = multiline ? (int)std::floor((at.y - inner.min.y + es.scrollY.value) / lineH) : 0;
            k = std::clamp(k, 0, (int)lines.size() - 1);
            const Line& l = lines[(std::size_t)k];
            const float x = at.x - inner.min.x + es.scrollX.value;
            std::uint32_t best = 0;
            float bestD = 1e30f;
            for (const text::CaretStop& st : l.stops)
                if (std::fabs(st.x - x) < bestD)
                {
                    bestD = std::fabs(st.x - x);
                    best = st.offset;
                }
            return ToText(text, l.start + (int)best, password);
        };

        bool changed = false;
        const auto snapshot = [&]() {
            if (now - es.edited > 0.8 || es.undo.empty())
            {
                es.undo.push_back({text, es.caret, es.anchor});
                if (es.undo.size() > 64)
                    es.undo.erase(es.undo.begin());
            }
            es.redo.clear();
            es.edited = now;
        };
        const auto moveTo = [&](int to, bool keep) {
            es.caret = std::clamp(to, 0, (int)text.size());
            if (!keep)
                es.anchor = es.caret;
            es.moved = now;
        };
        const auto erase = [&](int a, int z) {
            if (z <= a)
                return;
            snapshot();
            text.erase((std::size_t)a, (std::size_t)(z - a));
            es.caret = es.anchor = a;
            es.moved = now;
            changed = true;
        };
        const auto insert = [&](std::string_view s) {
            if (s.empty())
                return;
            const int a = std::min(es.caret, es.anchor), z = std::max(es.caret, es.anchor);
            snapshot();
            if (z > a)
                text.erase((std::size_t)a, (std::size_t)(z - a));
            text.insert((std::size_t)a, s);
            es.caret = es.anchor = a + (int)s.size();
            es.moved = now;
            changed = true;
        };

        // ---- the pointer
        if (b.pressed && !disabled)
        {
            const int at = hit(in.MousePos());
            if (b.clicks >= 3)
            {
                es.anchor = 0;
                es.caret = (int)text.size();
            }
            else if (b.clicks == 2)
            {
                int a = at, z = at;
                while (a > 0 && WordByte(text[(std::size_t)PrevChar(text, a)]))
                    a = PrevChar(text, a);
                while (z < (int)text.size() && WordByte(text[(std::size_t)z]))
                    z = NextChar(text, z);
                es.anchor = a;
                es.caret = z;
            }
            else
            {
                moveTo(at, (in.Mods() & Mod_Shift) != 0);
                es.dragging = true;
            }
            es.moved = now;
            es.wantX = -1.0f;
        }
        if (es.dragging && !inert_ && ctx_.ActiveId() == id && in.MouseDown(MouseButton::Left))
        {
            es.caret = hit(in.MousePos());
            es.moved = now;
        }
        if (!in.MouseDown(MouseButton::Left))
            es.dragging = false;

        // ---- the keys (the IME's own while it composes)
        const std::string& comp = in.Composition();
        if (focused && comp.empty())
        {
            ctx_.ClaimKeyboard(id);
            const bool shift = (in.Mods() & Mod_Shift) != 0, ctrl = (in.Mods() & Mod_Ctrl) != 0;
            const auto shortcut = [&](Key k) { return in.KeyPressed(k, false) && (in.KeyMods(k) & Mod_Ctrl) != 0; };
            const int selA = std::min(es.caret, es.anchor), selZ = std::max(es.caret, es.anchor);
            const bool sel = selZ > selA;
            if (ctx_.KeyPressed(Key::Left, id))
                moveTo(sel && !shift ? selA : (ctrl ? PrevWord(text, es.caret) : PrevChar(text, es.caret)), shift), es.wantX = -1.0f;
            if (ctx_.KeyPressed(Key::Right, id))
                moveTo(sel && !shift ? selZ : (ctrl ? NextWord(text, es.caret) : NextChar(text, es.caret)), shift), es.wantX = -1.0f;
            if (ctx_.KeyPressed(Key::Home, id))
            {
                int to = 0;
                if (multiline && !ctrl)
                {
                    to = es.caret;
                    while (to > 0 && text[(std::size_t)to - 1] != '\n')
                        --to;
                }
                moveTo(to, shift);
            }
            if (ctx_.KeyPressed(Key::End, id))
            {
                int to = es.caret;
                if (multiline && !ctrl)
                    while (to < (int)text.size() && text[(std::size_t)to] != '\n')
                        ++to;
                else
                    to = (int)text.size();
                moveTo(to, shift);
            }
            if (multiline && (ctx_.KeyPressed(Key::Up, id) || ctx_.KeyPressed(Key::Down, id)))
            {
                const int sc = ToShown(text, es.caret, password);
                const int k = LineOf(lines, sc) + (ctx_.KeyPressed(Key::Up, id) ? -1 : 1);
                if (es.wantX < 0.0f)
                    es.wantX = XOf(lines, sc);
                if (k >= 0 && k < (int)lines.size())
                {
                    const Line& l = lines[(std::size_t)k];
                    std::uint32_t best = 0;
                    float bestD = 1e30f;
                    for (const text::CaretStop& st : l.stops)
                        if (std::fabs(st.x - es.wantX) < bestD)
                        {
                            bestD = std::fabs(st.x - es.wantX);
                            best = st.offset;
                        }
                    const float keep = es.wantX;
                    moveTo(ToText(text, l.start + (int)best, password), shift);
                    es.wantX = keep;
                }
            }
            if (shortcut(Key::A))
            {
                es.anchor = 0;
                es.caret = (int)text.size();
            }
            if ((shortcut(Key::C) || shortcut(Key::X)) && sel && !password)
                ctx_.SetClipboardText(text.substr((std::size_t)selA, (std::size_t)(selZ - selA)));
            if (!readOnly)
            {
                if (ctx_.KeyPressed(Key::Backspace, id))
                    sel ? erase(selA, selZ) : erase(ctrl ? PrevWord(text, es.caret) : PrevChar(text, es.caret), es.caret);
                else if (ctx_.KeyPressed(Key::Delete, id))
                    sel ? erase(selA, selZ) : erase(es.caret, ctrl ? NextWord(text, es.caret) : NextChar(text, es.caret));
                if (shortcut(Key::X) && sel && !password)
                    erase(selA, selZ);
                if (shortcut(Key::V))
                {
                    std::string clip = ctx_.GetClipboardText();
                    if (!multiline)
                        clip.erase(std::remove_if(clip.begin(), clip.end(), [](char c) { return c == '\n' || c == '\r'; }), clip.end());
                    insert(clip);
                }
                if (shortcut(Key::Z) && !es.undo.empty())
                {
                    es.redo.push_back({text, es.caret, es.anchor});
                    const Snapshot s = es.undo.back();
                    es.undo.pop_back();
                    text = s.text;
                    es.caret = s.caret;
                    es.anchor = s.anchor;
                    changed = true;
                }
                if (shortcut(Key::Y) && !es.redo.empty())
                {
                    es.undo.push_back({text, es.caret, es.anchor});
                    const Snapshot s = es.redo.back();
                    es.redo.pop_back();
                    text = s.text;
                    es.caret = s.caret;
                    es.anchor = s.anchor;
                    changed = true;
                }
                if (in.KeyPressed(Key::Enter, false))
                {
                    if (multiline)
                        insert("\n");
                    else
                        entered = true;
                }
                std::string typed;
                for (char32_t c : in.Text())
                {
                    if (c < 0x20 || c == 0x7F)
                        continue;
                    if ((flags & InputTextFlags_CharsDecimal) && !((c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+' || c == 'e' || c == 'E'))
                        continue;
                    if (ctrl && c < 0x80)
                        continue;
                    EncodeUtf8(typed, c);
                }
                insert(typed);
            }
            if (in.KeyPressed(Key::Escape, false))
                ctx_.SetKeyboardFocusId(0);
        }
        if (changed)
            layout();

        // ---- a change to the text while it has focus - from the keys or from the caller - moves: what came in fades
        // in, what went out fades out where it was, the rest of its line slides from where it stood
        if (focused && es.lastValid && es.last != text)
        {
            layoutOf(es.last, oldLines, oldShown);
            const std::string& o = password ? oldShown : es.last;
            const std::string& n = password ? shown : text;
            const int common = (int)std::min(o.size(), n.size());
            int pre = 0;
            while (pre < common && o[(std::size_t)pre] == n[(std::size_t)pre])
                ++pre;
            while (Continuation(n, pre))
                --pre;
            int suf = 0;
            while (suf < common - pre && o[o.size() - 1 - (std::size_t)suf] == n[n.size() - 1 - (std::size_t)suf])
                ++suf;
            while (suf > 0 && Continuation(n, (int)n.size() - suf))
                --suf;
            const int oEnd = (int)o.size() - suf, nEnd = (int)n.size() - suf;
            for (Fresh& fr : es.fresh)
            {
                if (fr.at >= oEnd)
                    fr.at += nEnd - oEnd;
                else if (fr.at + fr.len > pre)
                    fr.len = 0;
            }
            es.fresh.erase(std::remove_if(es.fresh.begin(), es.fresh.end(), [](const Fresh& x) { return x.len <= 0; }), es.fresh.end());
            if (nEnd > pre)
                es.fresh.push_back({pre, nEnd - pre, now});
            if (oEnd > pre)
            {
                const std::string_view gs = std::string_view(o).substr((std::size_t)pre, (std::size_t)(oEnd - pre));
                if (gs.find('\n') == std::string_view::npos && es.gone.size() < 16)
                    es.gone.push_back({std::string(gs), XOf(oldLines, pre), LineOf(oldLines, pre), now});
            }
            const int ol = LineOf(oldLines, oEnd), nl = LineOf(lines, nEnd);
            if (ol == nl)
            {
                es.shift.value += XOf(oldLines, oEnd) - XOf(lines, nEnd);
                es.shift.target = 0.0f;
                es.shiftAt = nEnd;
                es.shiftLine = nl;
            }
            else
            {
                es.shift.Snap(0.0f);
                es.shiftAt = -1;
            }
        }
        es.last = text;
        es.lastValid = true;
        es.shift.Step(dt_, kSlideMotion);
        es.fresh.erase(std::remove_if(es.fresh.begin(), es.fresh.end(), [&](const Fresh& x) { return now - x.time >= kFreshTime; }), es.fresh.end());
        es.gone.erase(std::remove_if(es.gone.begin(), es.gone.end(), [&](const Gone& x) { return now - x.time >= kGoneTime; }), es.gone.end());
        if (es.shift.Resting())
            es.shiftAt = -1;

        // ---- what shows: the caret's line and column kept in view, gliding there
        const int sc = ToShown(text, es.caret, password), sa = ToShown(text, es.anchor, password);
        const float caretX = XOf(lines, sc);
        const int caretLine = LineOf(lines, sc);
        const float viewW = inner.Width(), viewH = inner.Height();
        float widest = 0.0f;
        for (const Line& l : lines)
            widest = std::max(widest, l.stops.back().x);
        if (focused)
        {
            float tx = es.scrollX.target;
            if (caretX - tx > viewW - 2.0f)
                tx = caretX - viewW + 2.0f;
            if (caretX - tx < 0.0f)
                tx = caretX;
            es.scrollX.target = std::clamp(tx, 0.0f, std::max(widest - viewW + 2.0f, 0.0f));
            if (multiline)
            {
                float ty = es.scrollY.target;
                const float top = (float)caretLine * lineH;
                if (top + lineH - ty > viewH)
                    ty = top + lineH - viewH;
                if (top - ty < 0.0f)
                    ty = top;
                es.scrollY.target = ty;
            }
        }
        if (multiline && b.hovered && in.Wheel().y != 0.0f)
            es.scrollY.target -= in.Wheel().y * lineH * 3.0f;
        if (multiline)
            es.scrollY.target = std::clamp(es.scrollY.target, 0.0f, std::max((float)lines.size() * lineH - viewH, 0.0f));
        es.scrollX.Step(dt_, Motion{0.14f, 1.0f});
        es.scrollY.Step(dt_, Motion{0.16f, 1.0f});
        // the caret glides; it starts where it is when the field takes focus
        if (focused && !es.wasFocused)
        {
            es.caretX.Snap(caretX);
            es.caretY.Snap((float)caretLine * lineH);
        }
        es.caretX.target = caretX;
        es.caretY.target = (float)caretLine * lineH;
        es.caretX.Step(dt_, kCaretMotion);
        es.caretY.Step(dt_, kCaretMotion);
        // the selection (one line) sweeps out of its anchor and shrinks back into the caret
        const bool selecting = focused && sa != sc;
        if (selecting && es.selShown.value <= 0.02f)
        {
            es.selA.Snap(XOf(lines, sa));
            es.selZ.Snap(XOf(lines, sa));
        }
        es.selA.target = selecting ? XOf(lines, std::min(sa, sc)) : caretX;
        es.selZ.target = selecting ? XOf(lines, std::max(sa, sc)) : caretX;
        es.selA.Step(dt_, Motion{0.12f, 1.0f});
        es.selZ.Step(dt_, Motion{0.12f, 1.0f});
        es.selShown.target = selecting ? 1.0f : 0.0f;
        es.selShown.Step(dt_, kSnappy);
        es.wasFocused = focused;

        // ---- drawing
        const float hover = Anim(id, 0, b.hovered ? 1.0f : 0.0f);
        const float focus = Anim(id, 1, focused ? 1.0f : 0.0f, kSmooth);
        const float filled = Anim(id, 2, text.empty() && comp.empty() ? 0.0f : 1.0f, kSnappy);
        esia::Painter p = Painter();
        if (focus > 0.004f)
            p.Rect(field.Expanded(3.0f), Style().Radius(radius + 3.0f).Stroke(3.0f, shown_.accent.Fade(0.18f * focus), 0.0f));
        p.Rect(field, Style().Radius(radius).Fill(Ink(0.035f + 0.025f * hover + 0.015f * focus))
                          .Stroke(1.0f / pixelScale_, Mix(Ink(0.12f + 0.06f * hover), shown_.accent.Fade(0.9f), Clamp01(focus)), 0.0f));
        p.PushClip(Rect(inner.min.x - 1.0f, field.min.y + 1.0f, inner.max.x + 2.0f, field.max.y - 1.0f));
        const std::string& s = password ? shown : text;
        // a line's box (caret, selection) and its glyphs: one line centered as Label puts text, several from the top
        const float ps = p.Env().pixelScale > 0.0f ? p.Env().pixelScale : 1.0f;
        const float baselineH = text_ ? text_->Measure(f, "H").baseline : f.size;
        const float boxTop0 = multiline ? inner.min.y : field.Center().y - lineH * 0.5f;
        const float glyphTop0 = multiline ? inner.min.y + std::round((lineH - f.size * 1.3f) * 0.5f)
                                          : std::round((field.Center().y + 0.35f * f.size) * ps) / ps - baselineH;
        const Color ink = Ink(disabled ? 0.4f : 0.92f);
        // the selection
        const float selA = Clamp01(es.selShown.value);
        if (selA > 0.004f)
        {
            if (!multiline)
            {
                const float x0 = std::min(es.selA.value, es.selZ.value), x1 = std::max(es.selA.value, es.selZ.value);
                if (x1 - x0 > 0.25f)
                    p.Rect(Rect(inner.min.x + x0 - es.scrollX.value, boxTop0 + 2.0f, inner.min.x + x1 - es.scrollX.value, boxTop0 + lineH - 2.0f),
                           Style().Radius(3.0f).Fill(shown_.accent.Fade(0.28f * selA)));
            }
            else if (sa != sc)
            {
                const int a = std::min(sa, sc), z = std::max(sa, sc);
                for (std::size_t k = 0; k < lines.size(); ++k)
                {
                    const Line& l = lines[k];
                    if (z < l.start || a > l.end)
                        continue;
                    const float x0 = XOf(lines, std::max(a, l.start)), x1 = (z > l.end) ? l.stops.back().x + 6.0f : XOf(lines, z);
                    const float y = boxTop0 + (float)k * lineH - es.scrollY.value;
                    p.Rect(Rect(inner.min.x + x0 - es.scrollX.value, y + 1.0f, inner.min.x + x1 - es.scrollX.value, y + lineH - 1.0f),
                           Style().Radius(3.0f).Fill(shown_.accent.Fade(0.28f * selA)));
                }
            }
        }
        // the text: one line out of focus and at rest rolls a new value in like a label; otherwise line by line, in
        // pieces where something fades or slides
        const Id morph = HashInt(0x54585446, id);
        const bool still = es.fresh.empty() && es.gone.empty() && es.shiftAt < 0;
        if (!multiline && !focused && still)
            MorphLabel(p, morph, inner.min.x - es.scrollX.value, field.Center().y, f, ink, s);
        else
        {
            if (!multiline)
                SnapMorph(morph, s);
            std::vector<int> cuts;
            for (std::size_t k = 0; k < lines.size(); ++k)
            {
                const Line& l = lines[k];
                const float y = glyphTop0 + (float)k * lineH - es.scrollY.value;
                if (y + lineH < field.min.y || y > field.max.y)
                    continue;
                const float x0 = inner.min.x - es.scrollX.value;
                if (focused && !comp.empty() && (int)k == caretLine)
                {
                    // the IME's composition at the caret, underlined
                    const std::string_view ls = std::string_view(s).substr((std::size_t)l.start, (std::size_t)(l.end - l.start));
                    const int cut = sc - l.start;
                    p.Text(Vec2(x0, y), f, ink, ls.substr(0, (std::size_t)cut));
                    const float cx = x0 + caretX;
                    const float cw = Measure(f, comp).x;
                    p.Text(Vec2(cx, y), f, shown_.accent, comp);
                    p.HLine(cx, cx + cw, y + f.size * 1.25f, shown_.accent, 1.0f);
                    p.Text(Vec2(cx + cw, y), f, ink, ls.substr((std::size_t)cut));
                    continue;
                }
                cuts.assign({l.start, l.end});
                for (const Fresh& fr : es.fresh)
                    for (int c : {fr.at, fr.at + fr.len})
                        if (c > l.start && c < l.end)
                            cuts.push_back(c);
                const bool slides = es.shiftAt >= 0 && es.shiftLine == (int)k;
                if (slides && es.shiftAt > l.start && es.shiftAt < l.end)
                    cuts.push_back(es.shiftAt);
                std::sort(cuts.begin(), cuts.end());
                cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
                for (std::size_t c = 0; c + 1 < cuts.size(); ++c)
                {
                    const int a = cuts[c], z = cuts[c + 1];
                    float alpha = 1.0f, rise = 0.0f;
                    for (const Fresh& fr : es.fresh)
                        if (a >= fr.at && z <= fr.at + fr.len)
                        {
                            const float t = EaseOutCubic(Clamp01((float)((now - fr.time) / kFreshTime)));
                            alpha = t;
                            rise = 3.0f * (1.0f - t);
                        }
                    const float dx = (slides && a >= es.shiftAt) ? es.shift.value : 0.0f;
                    p.Text(Vec2(x0 + XOf(lines, a) + dx, y + rise), f, ink.Fade(alpha), std::string_view(s).substr((std::size_t)a, (std::size_t)(z - a)));
                }
            }
            // what was removed, fading out where it was
            for (const Gone& g : es.gone)
            {
                const float t = Clamp01((float)((now - g.time) / kGoneTime));
                const float y = glyphTop0 + (float)g.line * lineH - es.scrollY.value;
                p.Text(Vec2(inner.min.x - es.scrollX.value + g.x, y - 3.0f * t), f, ink.Fade(0.9f * (1.0f - t) * (1.0f - t)), g.s);
            }
        }
        // the hint fades out as the first character comes in
        if (hint && filled < 0.999f)
        {
            const float hx = inner.min.x + 4.0f * filled;
            if (multiline)
                p.Text(Vec2(hx, glyphTop0), f, Ink(0.40f * (1.0f - filled)), hint);
            else
                Label(p, hx, field.Center().y, f, Ink(0.40f * (1.0f - filled)), hint);
        }
        p.PopClip();
        // the caret: a slim bar glowing in the accent; lit while it moves, blinking at rest as a terminal's does (on and
        // off a little over half a second each, every edge a quick fade); it fades with the focus. Cut to the field, not
        // to its text: its glow reaches into the padding at the start of the text
        if (focus > 0.004f)
        {
            const double rest = now - es.moved;
            float lit = 1.0f;
            if (rest >= 0.55)
            {
                const float ph = (float)std::fmod(rest - 0.55, 1.06);
                lit = ph < 0.53f ? 1.0f - Smoothstep(0.43f, 0.53f, ph) : Smoothstep(0.96f, 1.06f, ph);
            }
            const float compW = comp.empty() ? 0.0f : Measure(f, comp.substr(0, (std::size_t)std::clamp(in.CompositionCursor(), 0, (int)comp.size()))).x;
            const float x = inner.min.x + es.caretX.value + compW - es.scrollX.value;
            const float y = boxTop0 + es.caretY.value - es.scrollY.value;
            const Rect bar(x - 0.9f, y + 3.0f, x + 0.9f, y + lineH - 3.0f);
            const float a = Clamp01(lit) * focus;
            if (a > 0.004f)
            {
                p.PushClip(field.Expanded(-1.0f));
                p.Rect(bar, Style().Radius(0.9f).Fill(shown_.accent.Fade(a)).Glow(shown_.accent, 8.0f, 0.55f * a));
                p.PopClip();
            }
            if (focused)
                ctx_.RequestTextInput(bar);
        }
        return changed;
    }

    void Ui::SelectAllText(Id id)
    {
        EditState& es = ctx_.State<EditState>(id);
        es.anchor = 0;
        es.caret = 1 << 30;   // the text's end, once it is drawn
        es.moved = ctx_.Input().Time();
    }

    bool Ui::InputText(const char* label, std::string* text, std::uint32_t flags, const char* hint)
    {
        const std::string_view vis = Visible(label);
        if (!row_.on && !vis.empty())
            Row(label);
        Rect field;
        float radius = m::kRadiusInner;
        if (!TakeRow(280.0f, field))
        {
            field = ItemRect(Vec2(nextWidth_ > 0.0f ? nextWidth_ : ContentWidth(), m::kRow));
            if (nextWidth_ <= 0.0f)
                field.min.x -= bleed_;   // spanning the content: it reaches back to its edge, its text on the column
            radius = m::kRadiusControl;
        }
        nextWidth_ = 0.0f;
        bool entered = false;
        const bool changed = EditText(IdOf(label), field, *text, flags, hint, false, entered, radius);
        return (flags & InputTextFlags_EnterReturnsTrue) ? entered : changed;
    }

    bool Ui::InputTextMultiline(const char* label, std::string* text, Vec2 size, std::uint32_t flags)
    {
        const std::string_view vis = Visible(label);
        if (!vis.empty())
            TextUnformatted(vis, Tone::Secondary, m::kFontSmall);
        const float w = size.x > 0.0f ? size.x : ContentWidth() + (size.x < 0.0f ? size.x : 0.0f);
        const float h = size.y > 0.0f ? size.y : 8.0f + 6.0f * std::round(m::kFont * 1.42f) + 8.0f;
        Rect field = ItemRect(Vec2(w, h));
        if (size.x <= 0.0f)
            field.min.x -= bleed_;   // spanning the content: it reaches back to its edge, its text on the column
        bool entered = false;
        return EditText(IdOf(label), field, *text, flags, nullptr, true, entered, m::kRadiusControl);
    }

    namespace
    {
        bool Parse(const std::string& s, double& out)
        {
            if (s.empty())
                return false;
            char* end = nullptr;
            out = std::strtod(s.c_str(), &end);
            return end && *end == 0;
        }
    }

    bool Ui::InputFloat(const char* label, float* v, float step, const char* format)
    {
        const std::string_view vis = Visible(label);
        if (!row_.on && !vis.empty())
            Row(label);
        const Id id = IdOf(label);
        const float stepW = step > 0.0f ? 2.0f * (m::kControl + 4.0f) : 0.0f;
        Rect ctrl;
        float radius = m::kRadiusInner;
        if (!TakeRow(160.0f + stepW, ctrl))
        {
            ctrl = ItemRect(Vec2(nextWidth_ > 0.0f ? nextWidth_ : ContentWidth(), m::kRow));
            radius = m::kRadiusControl;
        }
        nextWidth_ = 0.0f;
        const Rect field(ctrl.min.x, ctrl.min.y, ctrl.max.x - stepW, ctrl.max.y);
        EditState& es = ctx_.State<EditState>(id);
        char buf[64];
        std::snprintf(buf, sizeof(buf), format, (double)*v);
        if (inert_ || ctx_.KeyboardFocusId() != id)
            es.number = buf;
        bool entered = false;
        bool valueChanged = false;
        if (EditText(id, field, es.number, InputTextFlags_CharsDecimal, nullptr, false, entered, radius))
        {
            double d = 0.0;
            if (Parse(es.number, d) && (float)d != *v)
            {
                *v = (float)d;
                valueChanged = true;
            }
        }
        if (entered)
            ctx_.SetKeyboardFocusId(0);
        if (step > 0.0f)
        {
            const auto stepper = [&](int which, Icon icon, float delta) {
                const Id sid = HashInt(which, id);
                const float x1 = ctrl.max.x - (which == 0 ? m::kControl + 4.0f : 0.0f);
                const Rect r(x1 - m::kControl, field.Center().y - m::kControl * 0.5f, x1, field.Center().y + m::kControl * 0.5f);
                AddItem(sid, r);
                const ButtonResult b = Behavior(sid, r, ButtonFlags_Repeat);
                const float hover = Anim(sid, 0, b.hovered ? 1.0f : 0.0f);
                const float press = Anim(sid, 1, b.held ? 1.0f : 0.0f);
                esia::Painter p = Painter();
                p.PushScale(r.Center(), 1.0f - 0.06f * press);
                p.Rect(r, Style().Radius(m::kRadiusInner).Fill(Ink(0.04f + 0.05f * hover + 0.05f * press)).Stroke(1.0f / pixelScale_, Ink(0.12f), 0.0f));
                DrawIcon(p, icon, r.Center(), 16.0f, Ink(0.6f + 0.35f * hover));
                p.PopScale();
                if (b.hovered)
                    ctx_.SetMouseCursor(MouseCursor::Hand);
                if (b.pressed && !disabled_.back())
                {
                    *v += delta;
                    std::snprintf(buf, sizeof(buf), format, (double)*v);
                    es.number = buf;
                    valueChanged = true;
                }
            };
            stepper(0, Icon::Minus, -step);
            stepper(1, Icon::Plus, step);
        }
        return valueChanged;
    }

    bool Ui::InputInt(const char* label, int* v, int step)
    {
        float f = (float)*v;
        const bool changed = InputFloat(label, &f, (float)step, "%.0f");
        const int next = (int)std::lround(f);
        if (!changed || next == *v)
            return false;
        *v = next;
        return true;
    }
}
