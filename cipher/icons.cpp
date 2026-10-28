// Cipher - Lucide icons as tiles and as strokes (icons.hpp).
#include "icons.hpp"
#include "esia/text/glyph_atlas.hpp"
#include "esia/text/glyph_raster.hpp"
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace cipher
{
    using namespace esia;

    namespace
    {
        // An icon's paths flattened into polylines on Lucide's 24-unit grid, once per icon.
        struct Flat
        {
            struct Run
            {
                std::uint32_t first = 0, count = 0;
                bool closed = false, filled = false;
                float length = 0.0f;
            };
            std::vector<Vec2> pts;
            std::vector<Run> runs;
            float length = 0.0f;
            Rect bounds{1e9f, 1e9f, -1e9f, -1e9f};
        };

        // flat when the control points lie within 1/64 of a grid unit of the chord (1/32 px at 48 px)
        void Cubic(std::vector<Vec2>& out, Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, int depth)
        {
            const Vec2 d = p3 - p0;
            const float d1 = std::fabs((p1.x - p3.x) * d.y - (p1.y - p3.y) * d.x);
            const float d2 = std::fabs((p2.x - p3.x) * d.y - (p2.y - p3.y) * d.x);
            const float chord = LengthSq(d);
            constexpr float kTol = 1.0f / 64.0f;
            const bool flat = chord > 1e-8f ? (d1 + d2) * (d1 + d2) <= kTol * kTol * chord
                                             : LengthSq(p1 - p0) + LengthSq(p2 - p0) <= kTol * kTol;
            if (flat || depth > 12)
            {
                out.push_back(p3);
                return;
            }
            const Vec2 p01 = (p0 + p1) * 0.5f, p12 = (p1 + p2) * 0.5f, p23 = (p2 + p3) * 0.5f;
            const Vec2 p012 = (p01 + p12) * 0.5f, p123 = (p12 + p23) * 0.5f, m = (p012 + p123) * 0.5f;
            Cubic(out, p0, p01, p012, m, depth + 1);
            Cubic(out, m, p123, p23, p3, depth + 1);
        }

        const Flat& GetFlat(int index)
        {
            static std::vector<std::unique_ptr<Flat>> cache((std::size_t)lucide::kIconTotal);
            std::unique_ptr<Flat>& slot = cache[(std::size_t)index];
            if (slot)
                return *slot;
            slot = std::make_unique<Flat>();
            Flat& f = *slot;
            const lucide::IconData& d = lucide::kIcons[index];
            const std::int16_t* c = lucide::kCoords + d.firstCoord;
            const auto next = [&c]() {
                const Vec2 v((float)c[0] * (1.0f / 256.0f), (float)c[1] * (1.0f / 256.0f));
                c += 2;
                return v;
            };
            const auto endRun = [&f]() {
                if (f.runs.empty())
                    return;
                Flat::Run& r = f.runs.back();
                r.count = (std::uint32_t)f.pts.size() - r.first;
                r.length = 0.0f;
                for (std::uint32_t i = 1; i < r.count; ++i)
                    r.length += Length(f.pts[r.first + i] - f.pts[r.first + i - 1]);
                if (r.closed && r.count > 1)
                    r.length += Length(f.pts[r.first] - f.pts[r.first + r.count - 1]);
                f.length += r.length;
            };
            for (std::uint32_t i = 0; i < d.ops; ++i)
            {
                const std::uint8_t op = lucide::kOps[d.firstOp + i];
                if (op == 0 || op == 4)
                {
                    endRun();
                    Flat::Run r;
                    r.first = (std::uint32_t)f.pts.size();
                    r.filled = op == 4;
                    f.runs.push_back(r);
                    f.pts.push_back(next());
                }
                else if (op == 1)
                    f.pts.push_back(next());
                else if (op == 2)
                {
                    const Vec2 p0 = f.pts.back(), p1 = next(), p2 = next(), p3 = next();
                    Cubic(f.pts, p0, p1, p2, p3, 0);
                }
                else if (op == 3 && !f.runs.empty())
                    f.runs.back().closed = true;
            }
            endRun();
            for (const Vec2& p : f.pts)
                f.bounds = f.bounds.Union(Rect(p, p));
            return f;
        }

        float SegmentDistance(Vec2 p, Vec2 a, Vec2 b)
        {
            const Vec2 ab = b - a, ap = p - a;
            const float len = LengthSq(ab);
            const float t = len > 1e-12f ? Clamp(Dot(ap, ab) / len, 0.0f, 1.0f) : 0.0f;
            return Length(ap - ab * t);
        }

        // Tiles under keys with this tag in bits 56..62 (Painter's flat tiles hash into the same space; bit 63 is the
        // text system's)
        constexpr std::uint64_t kTag = 0x43ull << 56;

        std::uint64_t TileKey(int index, int px, float stroke)
        {
            const std::uint64_t sq = (std::uint64_t)std::clamp((int)std::lround(stroke * 16.0f), 1, 255);
            return kTag | ((std::uint64_t)(index & 0xFFF) << 40) | ((std::uint64_t)(px & 0x3FF) << 30) | (sq << 22);
        }

        // The icon at `px` pixels square: its coverage by the distance to the strokes' center lines (round caps and
        // joins, as Lucide's stroke-linecap / linejoin), its filled shapes by the nonzero rule at 4 x 4 samples.
        const text::GlyphSlot* Tile(text::TextSystem& ts, int index, int px, float stroke)
        {
            const std::uint64_t key = TileKey(index, px, stroke);
            if (const text::GlyphSlot* s = ts.FindTile(key))
                return s;
            const Flat& f = GetFlat(index);
            const float k = (float)px / 24.0f;
            const float hw = 0.5f * stroke * k;
            const int x0 = (int)std::floor(f.bounds.min.x * k - hw - 1.0f), y0 = (int)std::floor(f.bounds.min.y * k - hw - 1.0f);
            const int x1 = (int)std::ceil(f.bounds.max.x * k + hw + 1.0f), y1 = (int)std::ceil(f.bounds.max.y * k + hw + 1.0f);
            const int w = std::max(1, x1 - x0), h = std::max(1, y1 - y0);
            if (w > 1024 || h > 1024)
                return nullptr;
            static thread_local std::vector<float> dist;
            dist.assign((std::size_t)w * h, 1e9f);
            const Vec2 o((float)x0, (float)y0);
            const auto segment = [&](Vec2 ga, Vec2 gb) {
                const Vec2 a = ga * k - o, b = gb * k - o;
                const float reach = hw + 1.0f;
                const int sx0 = std::max(0, (int)std::floor(std::min(a.x, b.x) - reach)), sx1 = std::min(w, (int)std::ceil(std::max(a.x, b.x) + reach));
                const int sy0 = std::max(0, (int)std::floor(std::min(a.y, b.y) - reach)), sy1 = std::min(h, (int)std::ceil(std::max(a.y, b.y) + reach));
                for (int y = sy0; y < sy1; ++y)
                    for (int x = sx0; x < sx1; ++x)
                    {
                        float& d = dist[(std::size_t)y * w + x];
                        d = std::min(d, SegmentDistance(Vec2((float)x + 0.5f, (float)y + 0.5f), a, b));
                    }
            };
            for (const Flat::Run& r : f.runs)
            {
                if (r.count == 1)
                    segment(f.pts[r.first], f.pts[r.first]);   // a dot: a round cap
                for (std::uint32_t i = 1; i < r.count; ++i)
                    segment(f.pts[r.first + i - 1], f.pts[r.first + i]);
                if (r.closed && r.count > 2)
                    segment(f.pts[r.first + r.count - 1], f.pts[r.first]);
            }
            text::GlyphBitmap b;
            b.left = x0;
            b.top = y0;
            b.width = w;
            b.height = h;
            b.channels = 1;
            b.pixels.resize((std::size_t)w * h);
            for (std::size_t i = 0; i < b.pixels.size(); ++i)
                b.pixels[i] = (std::uint8_t)std::lround(Saturate(hw + 0.5f - dist[i]) * 255.0f);
            // filled shapes (fill="currentColor"): the winding number of 16 samples per pixel over all filled runs
            bool anyFill = false;
            for (const Flat::Run& r : f.runs)
                anyFill |= r.filled;
            if (anyFill)
                for (int y = 0; y < h; ++y)
                    for (int x = 0; x < w; ++x)
                    {
                        int inside = 0;
                        for (int sy = 0; sy < 4; ++sy)
                            for (int sx = 0; sx < 4; ++sx)
                            {
                                const Vec2 p = (Vec2((float)x + ((float)sx + 0.5f) * 0.25f, (float)y + ((float)sy + 0.5f) * 0.25f) + o) * (1.0f / k);
                                int winding = 0;
                                for (const Flat::Run& r : f.runs)
                                {
                                    if (!r.filled || r.count < 3)
                                        continue;
                                    for (std::uint32_t i = 0; i < r.count; ++i)
                                    {
                                        const Vec2 a = f.pts[r.first + i], c = f.pts[r.first + (i + 1) % r.count];
                                        if (a.y <= p.y)
                                        {
                                            if (c.y > p.y && (c.x - a.x) * (p.y - a.y) - (p.x - a.x) * (c.y - a.y) > 0.0f)
                                                ++winding;
                                        }
                                        else if (c.y <= p.y && (c.x - a.x) * (p.y - a.y) - (p.x - a.x) * (c.y - a.y) < 0.0f)
                                            --winding;
                                    }
                                }
                                inside += winding != 0;
                            }
                        std::uint8_t& px8 = b.pixels[(std::size_t)y * w + x];
                        px8 = std::max(px8, (std::uint8_t)((inside * 255 + 8) / 16));
                    }
            return ts.AddTile(key, b);
        }

        bool Valid(Icon icon) { return (int)icon >= 1 && (int)icon <= lucide::kIconTotal; }
    }

    Icon FindIcon(std::string_view name)
    {
        static const std::unordered_map<std::string_view, int> byName = [] {
            std::unordered_map<std::string_view, int> m;
            m.reserve((std::size_t)lucide::kIconTotal);
            for (int i = 0; i < lucide::kIconTotal; ++i)
                m.emplace(lucide::kIcons[i].name, i + 1);
            return m;
        }();
        const auto it = byName.find(name);
        return it == byName.end() ? Icon::None : (Icon)it->second;
    }

    const char* IconName(Icon icon) { return Valid(icon) ? lucide::kIcons[(int)icon - 1].name : ""; }

    void WarmIcon(text::TextSystem& text, Icon icon, float size, float pixelScale, float stroke)
    {
        if (Valid(icon))
            Tile(text, (int)icon - 1, std::max(1, (int)std::lround(size * pixelScale)), stroke);
    }

    void DrawIcon(Painter& p, Icon icon, Vec2 center, float size, Color color, float stroke)
    {
        if (!Valid(icon) || !color.IsVisible() || size <= 0.0f)
            return;
        const PainterEnv& env = p.Env();
        const float scale = env.pixelScale > 0.0f ? env.pixelScale : 1.0f;
        const text::GlyphSlot* s = env.text ? Tile(*env.text, (int)icon - 1, std::max(1, (int)std::lround(size * scale)), stroke) : nullptr;
        if (!s || !s->page)
        {
            DrawIconVector(p, icon, center, size, color, IconMotion(), stroke);
            return;
        }
        // the box on whole pixels: Lucide's grid lines on pixel edges at 24 px, and the strokes as crisp as they get
        const int px = std::max(1, (int)std::lround(size * scale));
        const float bx = std::round(center.x * scale - (float)px * 0.5f), by = std::round(center.y * scale - (float)px * 0.5f);
        const Rect r((bx + (float)s->left) / scale, (by + (float)s->top) / scale, (bx + (float)(s->left + s->width)) / scale,
                     (by + (float)(s->top + s->height)) / scale);
        p.GetDrawList().AddQuad(s->page, r, s->uv0, s->uv1, color.Fade(p.Alpha()).ToRgba8());
    }

    void DrawIconVector(Painter& p, Icon icon, Vec2 center, float size, Color color, const IconMotion& motion, float stroke)
    {
        if (!Valid(icon) || !color.IsVisible() || size <= 0.0f || motion.drawn <= 0.0f)
            return;
        const Flat& f = GetFlat((int)icon - 1);
        const float k = size / 24.0f * motion.scale;
        const float c = std::cos(motion.angle), s = std::sin(motion.angle);
        const auto map = [&](Vec2 g) {
            const Vec2 d = (g - Vec2(12.0f, 12.0f)) * k;
            return center + Vec2(d.x * c - d.y * s, d.x * s + d.y * c);
        };
        const Style style = Style().Fill(color);
        const float thickness = stroke * k;
        float budget = Saturate(motion.drawn) * f.length;
        static thread_local std::vector<Vec2> pts;
        for (const Flat::Run& r : f.runs)
        {
            if (budget <= 1e-4f)
                break;
            pts.clear();
            pts.push_back(map(f.pts[r.first]));
            float used = 0.0f;
            const std::uint32_t n = r.count + ((r.closed && r.count > 2) ? 1u : 0u);
            for (std::uint32_t i = 1; i < n; ++i)
            {
                const Vec2 a = f.pts[r.first + i - 1], b = f.pts[r.first + (i % r.count)];
                const float len = Length(b - a);
                if (used + len >= budget)
                {
                    const float t = len > 1e-6f ? (budget - used) / len : 1.0f;
                    pts.push_back(map(a + (b - a) * t));
                    used = budget;
                    break;
                }
                used += len;
                pts.push_back(map(b));
            }
            budget -= std::max(used, 1e-3f);
            if (pts.size() == 1)
                pts.push_back(pts[0] + Vec2(0.01f, 0.0f));   // a dot: Polyline's round caps make it one
            p.Polyline(pts.data(), (int)pts.size(), thickness, style);
        }
    }
}
