// Cipher - handwriting (handwriting.hpp)
#include "handwriting.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cipher
{
    using namespace esia;

    namespace
    {
        struct HandRun
        {
            std::uint16_t first, count;   // control points of kHandPoints
        };
        // A glyph as written after a way of leaving the letter before (or starting a word): the lead into it and the
        // letter up to its own exit
        struct HandVariant
        {
            std::uint16_t body, pre, fin; // runs (0xFFFF none); pre: after a low exit, in place of its last kHandTail points;
                                          // fin: ending a word, in place of its own exit's (none: kHandEnd)
            float adv;                    // to the next glyph's origin
            std::uint16_t firstRuns, firstCount;  // strokes of its own before it
            std::uint16_t extraRuns, extraCount;  // ... right after its stroke
            std::uint16_t markRuns, markCount;    // ... after its word (dots, crosses)
        };
        struct HandGlyph
        {
            char32_t code;
            std::uint8_t exit;            // 0 none (the next glyph starts a stroke of its own), 1 low, 2 high, 3 mid
            bool joins;                   // the letter before runs into it
            std::uint16_t variant[4];     // starting a word, after a low, a high, a mid exit
        };
#include "handwriting_glyphs.inc"

        constexpr std::uint16_t kNone = 0xFFFF;

        // A dot: a stroke of two points or fewer that goes almost nowhere (an i's dot); a cross-bar of two points is a
        // line
        bool IsDot(const Script& s, int k)
        {
            const int a = s.strokeStart[(std::size_t)k], z = s.strokeStart[(std::size_t)k + 1];
            return z - a <= 2 && Length(s.points[(std::size_t)z - 1] - s.points[(std::size_t)a]) < 0.1f;
        }
        constexpr float kLineGap = 3.2f;   // between lines of text, x-heights

        const HandGlyph* FindGlyph(char32_t c)
        {
            const HandGlyph* end = kHandGlyphs + sizeof(kHandGlyphs) / sizeof(kHandGlyphs[0]);
            const HandGlyph* g = std::lower_bound(kHandGlyphs, end, c, [](const HandGlyph& a, char32_t b) { return a.code < b; });
            return g != end && g->code == c ? g : nullptr;
        }

        // A control polygon: for each point the one it copies (-1 none), and the baseline of its line
        struct Polygon
        {
            std::vector<Vec2> p;
            std::vector<int> copy;
            float line = 0.0f;
        };

        void AppendRun(Polygon& to, std::uint16_t run, float dx, float dy)
        {
            if (run == kNone)
                return;
            const HandRun r = kHandRuns[run];
            const int base = (int)to.p.size();
            for (int i = 0; i < r.count; ++i)
            {
                to.p.push_back(kHandPoints[r.first + i] + Vec2(dx, dy));
                const int c = kHandCopy[r.first + i];
                to.copy.push_back(c >= 0 ? base + c : -1);
            }
        }

        // the next code point of UTF-8 text (a byte that starts none: itself)
        char32_t NextCode(std::string_view s, std::size_t& i)
        {
            const auto b = [&](std::size_t k) { return (unsigned char)s[k]; };
            const unsigned char c = b(i++);
            int more = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
            char32_t code = more == 3 ? c & 0x07u : more == 2 ? c & 0x0Fu : more == 1 ? c & 0x1Fu : c;
            for (; more > 0 && i < s.size() && (b(i) & 0xC0) == 0x80; --more)
                code = (code << 6) | (b(i++) & 0x3Fu);
            return code;
        }

        // A little smoothing of the control points (tools/script_bspline.py's): three passes of a fourth difference - it
        // leaves arcs alone and takes out wobbles; a tip (a point written more than once), its neighbours and the ends stay,
        // a copied point follows the one it copies (so a pass back over a stroke stays exact)
        void SmoothPolygon(Polygon& s)
        {
            const int n = (int)s.p.size();
            const auto same = [&](int a, int b) { return s.p[(std::size_t)a].x == s.p[(std::size_t)b].x && s.p[(std::size_t)a].y == s.p[(std::size_t)b].y; };
            std::vector<char> fixed((std::size_t)n, 0);
            for (int i = 0; i < n; ++i)
                if ((i > 0 && same(i, i - 1)) || (i + 1 < n && same(i, i + 1)))
                    for (int k = std::max(i - 1, 0); k <= std::min(i + 1, n - 1); ++k)
                        fixed[(std::size_t)k] = 1;
            std::vector<Vec2> q;
            for (int pass = 0; pass < 3; ++pass)
            {
                q = s.p;
                for (int i = 2; i + 2 < n; ++i)
                    if (!fixed[(std::size_t)i])
                    {
                        const Vec2* p = s.p.data() + i;
                        q[(std::size_t)i] = p[0] - (p[-2] - p[-1] * 4.0f + p[0] * 6.0f - p[1] * 4.0f + p[2]) * 0.06f;
                    }
                for (int i = 0; i < n; ++i)
                    if (s.copy[(std::size_t)i] >= 0)
                        q[(std::size_t)i] = q[(std::size_t)s.copy[(std::size_t)i]];
                s.p.swap(q);
            }
        }

        // The uniform cubic B-spline of a control polygon into `out`: a sample about every 0.02 x-heights along each span
        // (4 pixels at 220 a unit); its ends where the polygon writes its first and last point three times
        void SampleBSpline(const std::vector<Vec2>& p, std::vector<Vec2>& out)
        {
            for (std::size_t i = 0; i + 3 < p.size(); ++i)
            {
                const Vec2 p0 = p[i], p1 = p[i + 1], p2 = p[i + 2], p3 = p[i + 3];
                const int n = std::max((int)std::ceil(Length(p2 - p1) / 0.02f), 1);
                for (int k = 0; k < n; ++k)
                {
                    const float t = (float)k / (float)n, t2 = t * t, t3 = t2 * t, u = 1.0f - t;
                    out.push_back((p0 * (u * u * u) + p1 * (3.0f * t3 - 6.0f * t2 + 4.0f) + p2 * (-3.0f * t3 + 3.0f * t2 + 3.0f * t + 1.0f) + p3 * t3) * (1.0f / 6.0f));
                }
            }
            const std::size_t n = p.size();
            out.push_back((p[n - 3] + p[n - 2] * 4.0f + p[n - 1]) * (1.0f / 6.0f));
        }

        // Ramer-Douglas-Peucker: the points of [a, b] farther than `tolerance` from the line of what is kept around them
        void Thin(const Vec2* pts, int a, int b, float tolerance, std::vector<char>& keep)
        {
            if (b - a < 2)
                return;
            const Vec2 e = pts[b] - pts[a];
            const float len = Length(e);
            int far = -1;
            float farD = tolerance;
            for (int i = a + 1; i < b; ++i)
            {
                const Vec2 w = pts[i] - pts[a];
                const float d = len > 1e-6f ? std::fabs(e.x * w.y - e.y * w.x) / len : Length(w);
                if (d > farD)
                {
                    farD = d;
                    far = i;
                }
            }
            if (far < 0)
                return;
            keep[(std::size_t)far] = 1;
            Thin(pts, a, far, tolerance, keep);
            Thin(pts, far, b, tolerance, keep);
        }
    }

    Script WriteScript(std::string_view text)
    {
        Script s;
        std::vector<Polygon> strokes, wordMarks, after;
        Polygon cur;
        bool open = false;   // `cur` is a stroke the next letter may run into
        int exit = 0;
        const HandVariant* last = nullptr;   // the letter `cur` ends in
        float x = 0.0f, y = 0.0f;
        const auto own = [&](std::uint16_t run) {
            Polygon st;
            st.line = y;
            AppendRun(st, run, x, y);
            return st;
        };
        const auto endStroke = [&] {
            if (open)
            {
                if (exit != 0)
                {
                    // the word's last letter: its exit's last points become its end (the last point written three times)
                    const float hx = cur.p.back().x;
                    cur.p.resize(cur.p.size() - (std::size_t)kHandTail[exit]);
                    cur.copy.resize(cur.copy.size() - (std::size_t)kHandTail[exit]);
                    if (last && last->fin != kNone)
                        AppendRun(cur, last->fin, hx, y);
                    else
                        for (int k = 0; k < kHandEndCount[exit]; ++k)
                        {
                            cur.p.push_back(Vec2(hx + kHandEnd[exit][k].x, y + kHandEnd[exit][k].y));
                            cur.copy.push_back(-1);
                        }
                    for (int k = 0; k < 2; ++k)
                    {
                        cur.p.push_back(cur.p.back());
                        cur.copy.push_back(-1);
                    }
                }
                cur.line = y;
                strokes.push_back(std::move(cur));
                cur = Polygon();
                open = false;
            }
            for (Polygon& e : after)
                strokes.push_back(std::move(e));
            after.clear();
        };
        const auto endWord = [&] {
            endStroke();
            for (Polygon& m : wordMarks)
                strokes.push_back(std::move(m));
            wordMarks.clear();
            exit = 0;
        };
        for (std::size_t i = 0; i < text.size();)
        {
            const char32_t c = NextCode(text, i);
            if (c == U' ' || c == U'\t')
            {
                endWord();
                x += kHandSpace;
                continue;
            }
            if (c == U'\n')
            {
                endWord();
                x = 0.0f;
                y -= kLineGap;
                continue;
            }
            const HandGlyph* g = FindGlyph(c);
            if (!g)
            {
                endStroke();
                exit = 0;
                x += kHandUnknown;
                continue;
            }
            const HandVariant& v = kHandVariants[g->variant[open && exit != 0 && g->joins ? exit : 0]];
            for (int k = 0; k < v.firstCount; ++k)
            {
                endStroke();
                strokes.push_back(own((std::uint16_t)(v.firstRuns + k)));
            }
            if (open && exit != 0 && g->joins)
            {
                // into the letter: after a low exit a letter that reaches back over the join has the exit run on longer, one
                // that rises at once has it turn up at once
                if (exit == 1 && v.pre != kNone)
                {
                    cur.p.resize(cur.p.size() - (std::size_t)kHandTail[1]);
                    cur.copy.resize(cur.copy.size() - (std::size_t)kHandTail[1]);
                    AppendRun(cur, v.pre, x, y);
                }
                AppendRun(cur, v.body, x, y);
            }
            else
            {
                endStroke();
                AppendRun(cur, v.body, x, y);
                open = true;
            }
            last = &v;
            for (int k = 0; k < v.extraCount; ++k)
                after.push_back(own((std::uint16_t)(v.extraRuns + k)));
            for (int k = 0; k < v.markCount; ++k)
                wordMarks.push_back(own((std::uint16_t)(v.markRuns + k)));
            exit = g->exit;
            x += v.adv;
            if (exit == 0)
                endStroke();
        }
        endWord();
        // each smoothed a little and sampled along its B-spline, slanted (the slant leans each line about its own
        // baseline); a dot as it is
        std::vector<Vec2> sampled;
        for (Polygon& st : strokes)
        {
            if (st.p.empty())
                continue;
            s.strokeStart.push_back((int)s.points.size());
            sampled.clear();
            if (st.p.size() <= 2)
                sampled = st.p;
            else
            {
                SmoothPolygon(st);
                SampleBSpline(st.p, sampled);
            }
            for (const Vec2 q : sampled)
                s.points.push_back(Vec2(q.x + kHandSlant * (q.y - st.line), q.y));
        }
        s.strokeStart.push_back((int)s.points.size());
        return s;
    }

    Script ScriptFromPoints(const Vec2* points, const int* strokeStart, int strokes)
    {
        Script s;
        if (strokes <= 0)
            return s;
        s.points.assign(points + strokeStart[0], points + strokeStart[strokes]);
        for (int k = 0; k <= strokes; ++k)
            s.strokeStart.push_back(strokeStart[k] - strokeStart[0]);
        return s;
    }

    HandwritingQuality SuggestQuality(std::string_view api, std::string_view adapter)
    {
        const auto has = [](std::string_view s, std::string_view part) { return s.find(part) != std::string_view::npos; };
        if (has(api, "Direct3D 9"))
            return HandwritingQuality::Lines;
        for (const char* soft : {"Basic Render", "WARP", "llvmpipe", "lavapipe", "SwiftShader", "Software"})
            if (has(adapter, soft))
                return HandwritingQuality::Lines;
        return HandwritingQuality::Glass;
    }

    void Handwriting::SetText(std::string_view text) { SetScript(WriteScript(text)); }

    void Handwriting::SetScript(Script script)
    {
        script_ = std::move(script);
        Measure();
        thinUnit_ = 0.0f;
        Restart();
    }

    void Handwriting::Restart()
    {
        start_ = unwriteStart_ = last_ = -1.0;
        slow_ = 0.0f;
        writing_ = gone_ = false;
    }

    void Handwriting::Unwrite(double now)
    {
        if (unwriteStart_ < 0.0)
            unwriteStart_ = now;
    }

    namespace
    {
        // each stroke's length along the pen's way (a dot: a third of a second of writing - the pen stops to dot it),
        // where it starts on it, and the bounds of all
        template <class M>
        void MeasureScript(const Script& s, M& m)
        {
            const int strokes = (int)s.strokeStart.size() - 1;
            m.start.assign((std::size_t)std::max(strokes, 0), 0.0f);
            m.length.assign((std::size_t)std::max(strokes, 0), 0.0f);
            m.total = 0.0f;
            m.bounds = Rect(Vec2(1e9f, 1e9f), Vec2(-1e9f, -1e9f));
            for (int k = 0; k < strokes; ++k)
            {
                m.start[(std::size_t)k] = m.total;
                for (int i = s.strokeStart[(std::size_t)k]; i < s.strokeStart[(std::size_t)k + 1]; ++i)
                {
                    const Vec2 q = s.points[(std::size_t)i];
                    m.bounds.min = Vec2(std::min(m.bounds.min.x, q.x), std::min(m.bounds.min.y, q.y));
                    m.bounds.max = Vec2(std::max(m.bounds.max.x, q.x), std::max(m.bounds.max.y, q.y));
                    if (i > s.strokeStart[(std::size_t)k])
                        m.length[(std::size_t)k] += Length(q - s.points[(std::size_t)i - 1]);
                }
                if (IsDot(s, k))
                    m.length[(std::size_t)k] = 3.0f;
                m.total += m.length[(std::size_t)k];
            }
        }
    }

    void Handwriting::Measure()
    {
        MeasureScript(script_, m_);
        // the strokes whose bounds (grown by half the tube) overlap, joined: union-find, each group named by its first
        const int strokes = (int)script_.strokeStart.size() - 1;
        std::vector<Rect> box((std::size_t)std::max(strokes, 0));
        for (int k = 0; k < strokes; ++k)
        {
            Rect b(Vec2(1e9f, 1e9f), Vec2(-1e9f, -1e9f));
            for (int i = script_.strokeStart[(std::size_t)k]; i < script_.strokeStart[(std::size_t)k + 1]; ++i)
            {
                const Vec2 q = script_.points[(std::size_t)i];
                b.min = Vec2(std::min(b.min.x, q.x), std::min(b.min.y, q.y));
                b.max = Vec2(std::max(b.max.x, q.x), std::max(b.max.y, q.y));
            }
            box[(std::size_t)k] = b.Expanded(spec_.tube * 0.5f);
        }
        group_.resize(box.size());
        for (int k = 0; k < strokes; ++k)
            group_[(std::size_t)k] = k;
        const auto root = [&](int k) {
            while (group_[(std::size_t)k] != k)
                k = group_[(std::size_t)k] = group_[(std::size_t)group_[(std::size_t)k]];
            return k;
        };
        for (int a = 0; a < strokes; ++a)
            for (int b = a + 1; b < strokes; ++b)
                if (!IsDot(script_, a) && !IsDot(script_, b) && box[(std::size_t)a].Overlaps(box[(std::size_t)b]))
                {
                    const int ra = root(a), rb = root(b);
                    group_[(std::size_t)std::max(ra, rb)] = std::min(ra, rb);
                }
        for (int k = 0; k < strokes; ++k)
            group_[(std::size_t)k] = root(k);
    }

    void Handwriting::Simplify(float unit)
    {
        if (unit == thinUnit_)
            return;
        thinUnit_ = unit;
        thin_.points.clear();
        thin_.strokeStart.clear();
        const float tolerance = 0.2f / std::max(unit, 1e-3f);   // a fifth of a pixel at the drawn size
        std::vector<char> keep;
        const int strokes = (int)script_.strokeStart.size() - 1;
        for (int k = 0; k < strokes; ++k)
        {
            const int a = script_.strokeStart[(std::size_t)k], b = script_.strokeStart[(std::size_t)k + 1];
            thin_.strokeStart.push_back((int)thin_.points.size());
            if (b - a <= 2)
            {
                thin_.points.insert(thin_.points.end(), script_.points.begin() + a, script_.points.begin() + b);
                continue;
            }
            keep.assign((std::size_t)(b - a), 0);
            keep.front() = keep.back() = 1;
            Thin(script_.points.data() + a, 0, b - a - 1, tolerance, keep);
            for (int i = a; i < b; ++i)
                if (keep[(std::size_t)(i - a)])
                    thin_.points.push_back(script_.points[(std::size_t)i]);
        }
        thin_.strokeStart.push_back((int)thin_.points.size());
        MeasureScript(thin_, thinM_);
    }

    // Auto: a step down (Glass, Solid, Lines) once frames have run slow for half a second of the writing
    void Handwriting::Watch(double now)
    {
        if (spec_.quality != HandwritingQuality::Auto)
            quality_ = spec_.quality;
        else if (last_ < 0.0)
            quality_ = spec_.start == HandwritingQuality::Auto ? HandwritingQuality::Glass : spec_.start;
        else if (writing_ && now - start_ > 0.5)
        {
            const float dt = (float)(now - last_);
            slow_ = dt > spec_.slowFrame ? slow_ + dt : std::max(0.0f, slow_ - dt * 0.5f);
            if (slow_ > 0.5f && quality_ != HandwritingQuality::Lines)
            {
                quality_ = quality_ == HandwritingQuality::Glass ? HandwritingQuality::Solid : HandwritingQuality::Lines;
                slow_ = 0.0f;
            }
        }
        last_ = now;
    }

    void Handwriting::Draw(Painter& p, const Rect& area, double now)
    {
        if (script_.strokeStart.size() < 2 || gone_)
            return;
        if (start_ < 0.0)
            start_ = now;
        Watch(now);
        const float t = (float)(now - start_);
        const float since = unwriteStart_ < 0.0 ? 0.0f : (float)(now - unwriteStart_);
        const bool unwriting = unwriteStart_ >= 0.0;
        // the whole fades as the pen runs back
        const float over = Clamp01(1.0f - since * spec_.fadeOut);
        if (over <= 0.002f)
        {
            gone_ = true;
            writing_ = false;
            return;
        }
        const float alpha = p.Alpha();
        p.SetAlpha(alpha * over);
        const float intensity = EaseOutCubic(Clamp01(t / spec_.fadeIn));
        const Vec2 d = area.Size();
        const float cy = area.min.y + d.y * spec_.middle;
        // a soft shade behind it: the glass still has light to bend, the strokes read over the brightest backdrop
        if (spec_.shade > 0.0f)
            p.Rect(area, Style().Fill(Paint::Radial(Color(0.0f, 0.0f, 0.0f, spec_.shade * intensity), Color(0.0f, 0.0f, 0.0f, 0.0f),
                                                    Vec2(0.5f, (cy - area.min.y) / d.y), 0.32f)));
        const bool glass = quality_ == HandwritingQuality::Glass;
        const Metrics& m = m_;
        const float unit = std::min(spec_.maxXHeight, d.x * spec_.widthFraction / m.bounds.Width());   // the x-height
        const float thick = unit * spec_.tube;
        if (!glass)
            Simplify(unit);
        const Script& s = glass ? script_ : thin_;
        const Metrics& sm = glass ? m_ : thinM_;
        const Vec2 size = m.bounds.Size() * unit;
        const float left = std::round(area.min.x + d.x * 0.5f - size.x * 0.5f), top = std::round(cy - size.y * 0.5f);
        const auto toScreen = [&](Vec2 q) { return Vec2(left + (q.x - m.bounds.min.x) * unit, top + (m.bounds.max.y - q.y) * unit); };
        // how far the pen has come: near an even pace, easing in and out a little; back along it when unwritten
        const float u = Clamp01((t - spec_.delay) / spec_.writeSeconds);
        const float writeIn = Mix(u, EaseInOutCubic(u), 0.35f);
        const float unwrite = unwriting ? EaseInOutCubic(Clamp01(since / spec_.unwriteSeconds)) : 0.0f;
        const float pen = sm.total * writeIn * (1.0f - unwrite);
        Color shadow = spec_.shadow;
        shadow.a *= intensity;
        Style tube;
        if (glass)
        {
            GlassMaterial g;
            g.blur = 0.5f;
            g.refraction = 16.0f;
            g.bezel = thick * 0.5f;   // a round tube: the whole width one dome
            g.dispersion = 0.9f;
            g.specular = 1.0f;
            g.saturation = 1.4f;
            g.brightness = 0.10f;
            g.noise = 0.0f;
            g.tint = Color(1.0f, 1.0f, 1.0f, 0.16f);
            g.rim = Color::White(0.85f);
            g.lightAngle = -2.2f + 0.6f * std::sin(t * 0.6f);   // its light turns slowly: the highlight runs along the tube
            tube = Style().Glass(g).Shadow(shadow, spec_.shadowBlur, spec_.shadowOffset);
        }
        else
            tube = Style().Fill(spec_.ink).Shadow(shadow, spec_.shadowBlur, spec_.shadowOffset);
        // Lines: quads only - a shadow line under the tube, the tube, a highlight along its lit side
        const bool lines = quality_ == HandwritingQuality::Lines;
        const Vec2 lit = Vec2(-0.12f, -0.16f) * thick;
        const Color highlight = Color::White(0.55f);
        // A plain polyline in pieces, split where the pen turns back by more than 120 degrees (the tip of a stroke that
        // goes back over itself, a cusp): drawn as one, its miter there is clipped to nothing and the line closes to a
        // needle; each piece's round end makes the tip round
        const auto plain = [&](const Vec2* q, int count, float width, Color c) {
            int from = 0;
            for (int j = 1; j + 1 < count; ++j)
            {
                const Vec2 a = q[j] - q[j - 1], b = q[j + 1] - q[j];
                if (Dot(a, b) < -0.5f * Length(a) * Length(b))
                {
                    p.Polyline(q + from, j - from + 1, width, c);
                    from = j;
                }
            }
            p.Polyline(q + from, count - from, width, c);
        };
        // Glass and Solid: the strokes that touch as one shape (Painter::Polylines, group_) - where they cross, one surface
        // and one shadow, glass that melts together - each group drawn where its first stroke comes in the writing, the
        // dots where theirs do; the highlights over all of it after
        struct Item
        {
            int stroke, first, count;   // a stroke's points in `ink` (count 0: a dot)
            Vec2 at;                    // a dot's place and size
            float r;
        };
        static thread_local std::vector<Vec2> pts, lifted, ink, one;
        static thread_local std::vector<int> oneStart;
        static thread_local std::vector<Item> items;
        static thread_local std::vector<char> drawn;
        ink.clear();
        items.clear();
        Vec2 tip;
        bool writing = false;
        const int strokes = (int)s.strokeStart.size() - 1;
        for (int k = 0; k < strokes; ++k)
        {
            const float done = pen - sm.start[(std::size_t)k];
            if (done <= 0.0f)
                break;
            const int a = s.strokeStart[(std::size_t)k], z = s.strokeStart[(std::size_t)k + 1];
            if (IsDot(s, k))
            {
                // a dot: the pen taps it on - it drops onto its place from a little above, growing, settling with a
                // small bounce; the pen's glint on it while it does
                const float k01 = Clamp01(done / sm.length[(std::size_t)k]);
                const Vec2 at = toScreen(s.points[(std::size_t)a]) - Vec2(0.0f, thick * 0.9f * (1.0f - EaseOutCubic(k01)));
                const float r = thick * 0.55f * std::max(EaseOutBack(k01), 0.0f);
                items.push_back(Item{k, 0, 0, at, r});
                if (k01 < 1.0f)
                {
                    tip = at;
                    writing = true;
                }
                continue;
            }
            pts.clear();
            pts.push_back(toScreen(s.points[(std::size_t)a]));
            float run = 0.0f;
            for (int i = a + 1; i < z; ++i)
            {
                const float seg = Length(s.points[(std::size_t)i] - s.points[(std::size_t)i - 1]);
                if (run + seg >= done)
                {
                    pts.push_back(toScreen(Mix(s.points[(std::size_t)i - 1], s.points[(std::size_t)i], seg > 0.0f ? (done - run) / seg : 1.0f)));
                    tip = pts.back();
                    writing = true;
                    break;
                }
                run += seg;
                pts.push_back(toScreen(s.points[(std::size_t)i]));
            }
            if (pts.size() < 2)
                continue;
            items.push_back(Item{k, (int)ink.size(), (int)pts.size(), Vec2(), 0.0f});
            ink.insert(ink.end(), pts.begin(), pts.end());
        }
        drawn.assign(items.size(), 0);
        for (std::size_t i = 0; i < items.size(); ++i)
        {
            const Item& it = items[i];
            if (it.count == 0)
            {
                if (lines)
                {
                    p.Circle(it.at + Vec2(0.0f, thick * 0.35f), it.r * 1.05f, Style().Fill(Color::Black(0.25f * intensity)));
                    p.Circle(it.at, it.r, Style().Fill(spec_.ink));
                    p.Circle(it.at + lit * 0.5f, it.r * 0.4f, Style().Fill(highlight));
                }
                else
                    p.Circle(it.at, it.r, tube);
                continue;
            }
            const Vec2* q = ink.data() + it.first;
            if (lines)
            {
                // quads only: a shadow line under the tube, the tube, a highlight along its lit side
                lifted.resize((std::size_t)it.count);
                for (int j = 0; j < it.count; ++j)
                    lifted[(std::size_t)j] = q[j] + Vec2(0.0f, thick * 0.35f);
                plain(lifted.data(), it.count, thick * 1.1f, Color::Black(0.25f * intensity));
                plain(q, it.count, thick, spec_.ink);
                for (int j = 0; j < it.count; ++j)
                    lifted[(std::size_t)j] = q[j] + lit;
                plain(lifted.data(), it.count, thick * 0.34f, highlight);
                continue;
            }
            if (drawn[i])
                continue;
            // the stroke's group, all of it laid down so far, as one shape
            one.clear();
            oneStart.clear();
            const int g = group_[(std::size_t)it.stroke];
            for (std::size_t j = i; j < items.size(); ++j)
                if (items[j].count > 0 && group_[(std::size_t)items[j].stroke] == g)
                {
                    drawn[j] = 1;
                    oneStart.push_back((int)one.size());
                    one.insert(one.end(), ink.begin() + items[j].first, ink.begin() + items[j].first + items[j].count);
                }
            oneStart.push_back((int)one.size());
            p.Polylines(one.data(), oneStart.data(), (int)oneStart.size() - 1, thick, tube);
        }
        // Solid: the highlights along the strokes' lit side, over the tubes
        if (!glass && !lines)
            for (const Item& it : items)
                if (it.count > 0)
                {
                    lifted.resize((std::size_t)it.count);
                    for (int j = 0; j < it.count; ++j)
                        lifted[(std::size_t)j] = ink[(std::size_t)(it.first + j)] + lit;
                    plain(lifted.data(), it.count, thick * 0.34f, highlight);
                }
        writing_ = writing && writeIn < 0.999f && !unwriting;
        // the pen's tip: a glint where the ink is being laid down
        if (spec_.glint && writing_)
        {
            if (lines)
                p.Circle(tip, thick * 0.32f, Style().Fill(Color::White(0.9f)));
            else
                p.Circle(tip, thick * 0.32f, Style().Fill(Color::White(0.9f)).Glow(Color::White(), 14.0f, 0.8f));
        }
        p.SetAlpha(alpha);
    }
}
