// Cipher - the boot screen (boot.hpp).
#include "boot.hpp"
#include "icons.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

namespace cipher
{
    using namespace esia;

    namespace
    {
        // The chrome, a user effect (WgtEffect, premultiplied out). params: x its opacity, y how far the ribbons have
        // risen out of the flat (0..1), z / w the size of the pointer's wave grid (cells). The shape's image is the waves'
        // slope (R across, G down, 128 flat, a cell's rise over a cell in 255ths); its fill is a clear white (it draws
        // nothing).
        //
        // The look of React Bits' Liquid Chrome: a field of stripes bent by itself (a few octaves of cosines, the rows
        // of the bending's Jacobian carried along), each stripe a ridge of a squared sine - flat tops, bevelled sides -
        // its slope tilting the normal; the normal mirrors a studio (soft boxes, a rim, a wall of streaks) with Schlick's
        // Fresnel, ACES-tonemapped. Where it is flat it is clear: the pool shows through.
        const char* const kSource = R"(
float ChromeLamp(float2 p, float2 c, float2 size, float soft)
{
    const float2 d = abs(p - c) - size;
    const float dist = length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
    return 1.0 - smoothstep(-soft, soft, dist);
}
float3 ChromeStudio(float3 r, float blur)
{
    const float2 p = r.xy / max(r.z + 1.0, 0.05);
    const float radius = max(length(p), 1e-5);
    const float2 dir = p / radius;
    const float soft = 0.008 + blur;
    const float key = ChromeLamp(p, float2(-0.25, 0.42), float2(0.30, 0.12), soft);
    const float side = ChromeLamp(p, float2(0.48, 0.12), float2(0.06, 0.30), soft);
    const float ground = ChromeLamp(p, float2(-0.10, -0.55), float2(0.45, 0.06), soft);
    const float rise = smoothstep(-1.0, 1.0, dir.y);
    const float w = 0.035 + blur;
    const float rim = exp(-abs(radius - 1.0) / w) * 0.031 / w * (0.55 + 0.9 * rise);
    const float band = 1.0 - 0.95 * smoothstep(1.04, 1.12, radius);
    const float streak = 0.5 + 0.5 * cos(3.0 * atan2(dir.y, dir.x) + 0.7);
    const float wall = smoothstep(1.25, 2.4, radius) * (0.25 + 0.75 * rise) * 0.55;
    const float3 base = 0.0045 + 0.0035 * rise;
    return (base + key * 4.0 * float3(1.0, 0.93, 0.84) + side * 3.0 * float3(0.84, 0.92, 1.0) + ground * 1.2 + rim) * band + lerp(0.45, 1.8, streak) * wall;
}
float3 ChromeAces(float3 x)
{
    return saturate((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14));
}
// one sample at pixel `frag`: premultiplied by how sloped it is (the ribbons), clear where it is flat
// the waves' slope at `uv` (0..1 of the display) through a cubic B-spline of the grid's (four bilinear taps): smooth
float2 ChromeWaves(float2 uv, float2 grid)
{
    const float2 tc = uv * grid - 0.5, i = floor(tc), f = tc - i;
    const float2 f2 = f * f, f3 = f2 * f;
    const float2 w0 = (1.0 - 3.0 * f + 3.0 * f2 - f3) / 6.0, w1 = (4.0 - 6.0 * f2 + 3.0 * f3) / 6.0;
    const float2 w2 = (1.0 + 3.0 * f + 3.0 * f2 - 3.0 * f3) / 6.0, w3 = f3 / 6.0;
    const float2 g0 = w0 + w1, g1 = w2 + w3;
    const float2 h0 = (i - 0.5 + w1 / g0) / grid, h1 = (i + 1.5 + w3 / g1) / grid;
    const float2 s = g0.y * (g0.x * WgtTexture(float2(h0.x, h0.y)).rg + g1.x * WgtTexture(float2(h1.x, h0.y)).rg) +
                     g1.y * (g0.x * WgtTexture(float2(h0.x, h1.y)).rg + g1.x * WgtTexture(float2(h1.x, h1.y)).rg);
    const float2 slope = (s * 255.0 - 128.0) / 255.0 * 2.2;
    return float2(slope.x, -slope.y);   // y up, as the stripes' coordinates have it
}
// one sample at pixel `frag`: premultiplied by how sloped it is (the ribbons, the waves), clear where it is flat
float4 ChromeSample(float2 frag, float presence, float t, float2 ripple)
{
    const float2 res = gTarget.xy;
    const float unit = min(res.x, res.y);
    float2 uv = (2.0 * frag - res) / unit;
    uv.y = -uv.y;
    // the stripes bent by themselves, the rows of the bending's Jacobian carried along
    float2 p = uv, rowX = float2(1.0, 0.0), rowY = float2(0.0, 1.0);
    const float amp = 0.3 * presence;
    [unroll] for (int k = 1; k <= 4; ++k)
    {
        const float i = (float)k, a = amp / i, soften = exp(0.3 - 0.3 * i);
        const float ax = i * 3.0 * p.y + t;
        p.x += a * cos(ax);
        rowX -= a * sin(ax) * i * 3.0 * soften * rowY;
        const float ay = i * 3.0 * p.x + t;
        p.y += a * cos(ay);
        rowY -= a * sin(ay) * i * 3.0 * soften * rowX;
    }
    const float phase = t - p.x - p.y;
    const float2 field = -(rowX + rowY);
    const float density = max(length(field), 1e-4);
    // a ridge of a squared sine across the stripes; finer than a few pixels, it lies down flat
    const float sharp = 5.0 / 3.0;
    const float th = tanh(sharp * sin(phase));
    const float bevel = (1.0 - th * th) / tanh(sharp);
    const float fade = smoothstep(3.0, 9.0, 3.14159265 * unit / density);
    // the pointer's waves on it
    const float2 slope = sharp * cos(phase) * bevel * presence * fade * (field / density) + ripple;
    // how fast the slope turns across a pixel (all four samples of the quad agree on it: before any branch)
    const float2 sx = ddx(slope), sy = ddy(slope);
    const float bend = sqrt(max(dot(sx, sx), dot(sy, sy)));
    const float blur = 0.03 + 0.6 * min(bend, 0.6);
    const float m = length(slope);
    const float alpha = smoothstep(0.0, 0.1, m);
    if (alpha <= 0.0)
        return float4(0.0, 0.0, 0.0, 0.0);
    const float2 s = slope * (3.0 * tanh(m / 3.0) / m);   // a slope softly held under 3
    const float3 n = normalize(float3(-s.x, -s.y, 1.0));
    const float3 v = normalize(float3(uv * 0.08, -1.0));
    const float facing = saturate(dot(n, -v));
    const float fresnel = 0.92 + 0.08 * pow(1.0 - facing, 5.0);
    const float3 col = pow(ChromeAces(ChromeStudio(reflect(v, n), blur) * fresnel), 1.0 / 2.2);
    return float4(col * alpha, alpha);
}
float4 WgtEffect(WgtFx fx)
{
    const float2 res = gTarget.xy;
    const float t = fx.time * 0.2 + 11.0;
    const float2 frag = fx.screenUV * res;
    const float2 ripple = ChromeWaves(fx.screenUV, fx.params.zw);
    // one sample a pixel: the blur (how fast the slope turns across it) keeps the fine highlights smooth
    float4 acc = ChromeSample(frag, fx.params.y, t, ripple) * (fx.coverage * fx.params.x);
    // grain against banding
    const float g = frac(52.9829189 * frac(dot(frag, float2(0.06711056, 0.00583715))));
    acc.rgb = max(acc.rgb + (g - 0.5) / 255.0 * acc.a, 0.0);
    return acc;
}
)";

        // ------------------------------------------------------------------ the same chrome on the CPU
        float Smooth(float a, float b, float x)
        {
            const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        }
        float Lamp(float px, float py, float cx, float cy, float sx, float sy, float soft)
        {
            const float dx = std::fabs(px - cx) - sx, dy = std::fabs(py - cy) - sy;
            const float ox = std::max(dx, 0.0f), oy = std::max(dy, 0.0f);
            const float dist = std::sqrt(ox * ox + oy * oy) + std::min(std::max(dx, dy), 0.0f);
            return 1.0f - Smooth(-soft, soft, dist);
        }
        float Aces(float x) { return std::clamp((x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f), 0.0f, 1.0f); }

        // one texel of the chrome (straight alpha): `ux`, `uy` its place as the effect's uv; `unit` its texture's size;
        // `wx`, `wy` the pointer's waves' slope there (y up)
        void ChromeTexel(float ux, float uy, float unit, float presence, float t, float wx, float wy, std::uint8_t* out)
        {
            float px = ux, py = uy, r0x = 1.0f, r0y = 0.0f, r1x = 0.0f, r1y = 1.0f;
            const float amp = 0.3f * presence;
            for (int k = 1; k <= 4; ++k)
            {
                const float i = (float)k, a = amp / i, soften = std::exp(0.3f - 0.3f * i);
                const float ax = i * 3.0f * py + t;
                px += a * std::cos(ax);
                const float sx = a * std::sin(ax) * i * 3.0f * soften;
                r0x -= sx * r1x;
                r0y -= sx * r1y;
                const float ay = i * 3.0f * px + t;
                py += a * std::cos(ay);
                const float sy = a * std::sin(ay) * i * 3.0f * soften;
                r1x -= sy * r0x;
                r1y -= sy * r0y;
            }
            const float phase = t - px - py;
            const float fx = -(r0x + r1x), fy = -(r0y + r1y);
            const float density = std::max(std::sqrt(fx * fx + fy * fy), 1e-4f);
            const float sharp = 5.0f / 3.0f;
            const float th = std::tanh(sharp * std::sin(phase));
            const float bevel = (1.0f - th * th) / std::tanh(sharp);
            const float fade = Smooth(3.0f, 9.0f, 3.14159265f * unit / density);
            const float g = sharp * std::cos(phase) * bevel * presence * fade / density;
            float sx = g * fx + wx, sy = g * fy + wy;
            const float m = std::sqrt(sx * sx + sy * sy);
            const float alpha = Smooth(0.0f, 0.1f, m);
            if (alpha <= 0.0f)
            {
                out[0] = out[1] = out[2] = out[3] = 0;
                return;
            }
            const float hold = 3.0f * std::tanh(m / 3.0f) / m;
            sx *= hold;
            sy *= hold;
            // the normal, the view, the reflection, the studio
            float nx = -sx, ny = -sy, nz = 1.0f;
            const float nl = 1.0f / std::sqrt(nx * nx + ny * ny + nz * nz);
            nx *= nl, ny *= nl, nz *= nl;
            float vx = ux * 0.08f, vy = uy * 0.08f, vz = -1.0f;
            const float vl = 1.0f / std::sqrt(vx * vx + vy * vy + vz * vz);
            vx *= vl, vy *= vl, vz *= vl;
            const float dn = vx * nx + vy * ny + vz * nz;
            const float rx = vx - 2.0f * dn * nx, ry = vy - 2.0f * dn * ny, rz = vz - 2.0f * dn * nz;
            const float facing = std::clamp(-dn, 0.0f, 1.0f);
            const float fresnel = 0.92f + 0.08f * std::pow(1.0f - facing, 5.0f);
            const float qx = rx / std::max(rz + 1.0f, 0.05f), qy = ry / std::max(rz + 1.0f, 0.05f);
            const float radius = std::max(std::sqrt(qx * qx + qy * qy), 1e-5f);
            const float dirx = qx / radius, diry = qy / radius;
            const float soft = 0.008f + 0.06f;
            const float key = Lamp(qx, qy, -0.25f, 0.42f, 0.30f, 0.12f, soft);
            const float side = Lamp(qx, qy, 0.48f, 0.12f, 0.06f, 0.30f, soft);
            const float ground = Lamp(qx, qy, -0.10f, -0.55f, 0.45f, 0.06f, soft);
            const float rise = Smooth(-1.0f, 1.0f, diry);
            const float rim = std::exp(-std::fabs(radius - 1.0f) / 0.095f) * 0.031f / 0.095f * (0.55f + 0.9f * rise);
            const float band = 1.0f - 0.95f * Smooth(1.04f, 1.12f, radius);
            const float streak = 0.5f + 0.5f * std::cos(3.0f * std::atan2(diry, dirx) + 0.7f);
            const float wall = Smooth(1.25f, 2.4f, radius) * (0.25f + 0.75f * rise) * 0.55f;
            const float base = 0.0045f + 0.0035f * rise;
            const float wallLight = (0.45f + 1.35f * streak) * wall;
            const float light[3] = {(base + key * 4.0f + side * 3.0f * 0.84f + ground * 1.2f + rim) * band + wallLight,
                                    (base + key * 4.0f * 0.93f + side * 3.0f * 0.92f + ground * 1.2f + rim) * band + wallLight,
                                    (base + key * 4.0f * 0.84f + side * 3.0f + ground * 1.2f + rim) * band + wallLight};
            for (int c = 0; c < 3; ++c)
                out[c] = (std::uint8_t)std::lround(255.0f * std::pow(Aces(light[c] * fresnel), 1.0f / 2.2f));
            out[3] = (std::uint8_t)std::lround(255.0f * alpha);
        }
    }

    namespace
    {
        // the name's cursive (generated: its strokes in writing order, x-height units, y up)
#include "boot_script.inc"
        constexpr int kScriptStrokes = (int)(sizeof(kScriptStrokeStart) / sizeof(kScriptStrokeStart[0])) - 1;

        // the strokes' lengths and where each starts along the whole, the bounds: measured once
        struct ScriptMetrics
        {
            float length[kScriptStrokes] = {}, start[kScriptStrokes] = {};
            float total = 0.0f;
            Rect bounds;
        };
        const ScriptMetrics& Script()
        {
            static const ScriptMetrics m = [] {
                ScriptMetrics s;
                s.bounds = Rect(Vec2(1e9f, 1e9f), Vec2(-1e9f, -1e9f));
                for (int k = 0; k < kScriptStrokes; ++k)
                {
                    s.start[k] = s.total;
                    for (int i = kScriptStrokeStart[k]; i < kScriptStrokeStart[k + 1]; ++i)
                    {
                        const Vec2 q = kScriptPoints[i];
                        s.bounds.min = Vec2(std::min(s.bounds.min.x, q.x), std::min(s.bounds.min.y, q.y));
                        s.bounds.max = Vec2(std::max(s.bounds.max.x, q.x), std::max(s.bounds.max.y, q.y));
                        if (i > kScriptStrokeStart[k])
                            s.length[k] += Length(q - kScriptPoints[i - 1]);
                    }
                    // a dot: as much of the pen's way as a third of a second of writing - the pen stops to dot it
                    if (kScriptStrokeStart[k + 1] - kScriptStrokeStart[k] <= 2)
                        s.length[k] = 3.0f;
                    s.total += s.length[k];
                }
                return s;
            }();
            return m;
        }
    }

    const char* BootScreen::EffectSource() { return kSource; }

    // Without run-time effects: the same chrome worked out on the CPU into a texture of about 200 x 125 texels, the
    // pointer's waves sampled from their grid, stretched over the display - bilinear filtering keeps it smooth.
    void BootScreen::DrawCpuField(Ui& ui, DrawList& dl, Vec2 size, float t, float presence, float opacity)
    {
        const int w = 200, h = std::max((int)std::lround(200.0f * size.y / std::max(size.x, 1.0f)), 1);
        TextureRegistry& reg = ui.Ctx().Textures();
        if (!cpuTex_ || w != cpuW_ || h != cpuH_)
        {
            if (cpuTex_)
                reg.Destroy(cpuTex_);
            TextureInfo info;
            info.format = TextureFormat::RGBA8;
            info.width = w;
            info.height = h;
            cpuTex_ = reg.Create(info);
            cpuW_ = w;
            cpuH_ = h;
            cpuPixels_.assign((std::size_t)w * (std::size_t)h * 4, 0);
        }
        const float unit = (float)std::min(w, h);
        const float tt = t * 0.2f + 11.0f;
        // its rows shared out among the cores (this thread takes the first band)
        const auto rows = [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y)
                for (int x = 0; x < w; ++x)
                {
                    const float ux = (2.0f * ((float)x + 0.5f) - (float)w) / unit, uy = -(2.0f * ((float)y + 0.5f) - (float)h) / unit;
                    const Vec2 ripple = WaveSlope(Vec2(((float)x + 0.5f) / (float)w * size.x, ((float)y + 0.5f) / (float)h * size.y)) * 2.2f;
                    ChromeTexel(ux, uy, unit, presence, tt, ripple.x, -ripple.y, &cpuPixels_[((std::size_t)y * (std::size_t)w + (std::size_t)x) * 4]);
                }
        };
        const int bands = (int)std::clamp(std::thread::hardware_concurrency(), 1u, 8u);
        std::vector<std::thread> workers;
        for (int b = 1; b < bands; ++b)
            workers.emplace_back(rows, h * b / bands, h * (b + 1) / bands);
        rows(0, h / bands);
        for (std::thread& wk : workers)
            wk.join();
        reg.Update(cpuTex_, 0, 0, w, h, cpuPixels_.data());
        dl.AddImage(cpuTex_, Rect(Vec2(0, 0), size), Vec2(0.0f, 0.0f), Vec2(1.0f, 1.0f), Color::White(opacity).ToRgba8());
    }

    // ------------------------------------------------------------------ the pointer's waves
    // React Bits' Liquid Chrome's own: a height field on a grid of cells about 5 units across, stepped 120 times a
    // second - a damped wave (each cell from its neighbours, the last step's and this one's), a little viscous. The
    // pointer's path presses a trail into it as it moves (deeper the further it went in a step), a click a deep dent;
    // the waves run out from them, cross, bounce off the display's edges and settle in a second or two. Their slope
    // goes to the chrome as a texture.
    void BootScreen::WaveFrame(Ui& ui, Vec2 display, float dt, bool clicked)
    {
        const float target = std::max(5.0f, std::sqrt(display.x * display.y / 80000.0f));   // at most about 80 000 cells
        const int gw = std::max((int)std::lround(display.x / target), 2), gh = std::max((int)std::lround(display.y / target), 2);
        TextureRegistry& reg = ui.Ctx().Textures();
        if (gw != waveW_ || gh != waveH_ || !waveTex_)
        {
            waveW_ = gw;
            waveH_ = gh;
            const std::size_t n = (std::size_t)gw * (std::size_t)gh;
            waveNow_.assign(n, 0.0f);
            wavePrev_.assign(n, 0.0f);
            waveNext_.assign(n, 0.0f);
            waveSlope_.assign(n * 2, 0.0f);
            wavePixels_.assign(n * 4, 0);
            for (std::size_t i = 0; i < n; ++i)
            {
                wavePixels_[i * 4] = wavePixels_[i * 4 + 1] = 128;
                wavePixels_[i * 4 + 3] = 255;
            }
            if (waveTex_)
                reg.Destroy(waveTex_);
            TextureInfo info;
            info.format = TextureFormat::RGBA8;
            info.width = gw;
            info.height = gh;
            waveTex_ = reg.Create(info, wavePixels_.data());
            waveCalm_ = true;
            strokePlaced_ = false;
        }
        waveCell_ = Vec2(display.x / (float)gw, display.y / (float)gh);
        const float cell = 0.5f * (waveCell_.x + waveCell_.y);
        // the pointer's path, a click
        const InputState& in = ui.Ctx().Input();
        bool touched = false;
        if (in.MouseValid())
        {
            const Vec2 m = in.MousePos();
            if (!strokePlaced_)
            {
                strokeAt_ = m;
                strokePlaced_ = true;
            }
            const float travel = Length(m - strokeAt_) / cell;
            if (travel > 0.25f)
            {
                WaveStroke(strokeAt_, m, -std::min(0.45f, 0.04f + travel * 0.045f), 4.0f);
                strokeAt_ = m;
                touched = true;
            }
            if (clicked)
            {
                WaveStroke(m, m, -1.6f, 6.0f);
                touched = true;
            }
        }
        else
            strokePlaced_ = false;
        if (touched)
            waveCalm_ = false;
        // 120 steps a second whatever the frame rate (at most 4 a frame)
        waveClock_ = std::min(waveClock_ + dt, 4.0f / 120.0f);
        bool stepped = false;
        while (waveClock_ >= 1.0f / 120.0f)
        {
            waveClock_ -= 1.0f / 120.0f;
            if (waveCalm_)
                continue;
            waveCalm_ = !WaveStep();
            stepped = true;
        }
        if (!stepped && !touched)
            return;
        // the slope (central differences, a cell's rise over a cell) into the texture
        for (int y = 0; y < gh; ++y)
        {
            const float* row = &waveNow_[(std::size_t)y * (std::size_t)gw];
            const float* up = &waveNow_[(std::size_t)(y > 0 ? y - 1 : 0) * (std::size_t)gw];
            const float* down = &waveNow_[(std::size_t)(y < gh - 1 ? y + 1 : gh - 1) * (std::size_t)gw];
            for (int x = 0; x < gw; ++x)
            {
                const std::size_t i = (std::size_t)y * (std::size_t)gw + (std::size_t)x;
                const float sx = (row[x < gw - 1 ? x + 1 : x] - row[x > 0 ? x - 1 : 0]) * 0.5f, sy = (down[x] - up[x]) * 0.5f;
                waveSlope_[i * 2] = sx;
                waveSlope_[i * 2 + 1] = sy;
                wavePixels_[i * 4] = (std::uint8_t)std::clamp((int)std::lround(128.0f + sx * 255.0f), 0, 255);
                wavePixels_[i * 4 + 1] = (std::uint8_t)std::clamp((int)std::lround(128.0f + sy * 255.0f), 0, 255);
            }
        }
        reg.Update(waveTex_, 0, 0, gw, gh, wavePixels_.data());
    }

    // `strength` pressed in round the segment a-b (units), a Gaussian `radius` cells across
    void BootScreen::WaveStroke(Vec2 a, Vec2 b, float strength, float radius)
    {
        const Vec2 ca(a.x / waveCell_.x - 0.5f, a.y / waveCell_.y - 0.5f), cb(b.x / waveCell_.x - 0.5f, b.y / waveCell_.y - 0.5f);
        const float reach = radius * 3.0f;
        const int x0 = std::max((int)std::floor(std::min(ca.x, cb.x) - reach), 0), x1 = std::min((int)std::ceil(std::max(ca.x, cb.x) + reach), waveW_ - 1);
        const int y0 = std::max((int)std::floor(std::min(ca.y, cb.y) - reach), 0), y1 = std::min((int)std::ceil(std::max(ca.y, cb.y) + reach), waveH_ - 1);
        const Vec2 ab = cb - ca;
        const float len2 = std::max(ab.x * ab.x + ab.y * ab.y, 1e-6f), inv = 1.0f / (radius * radius);
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
            {
                const Vec2 q((float)x, (float)y);
                const float k = std::clamp(((q.x - ca.x) * ab.x + (q.y - ca.y) * ab.y) / len2, 0.0f, 1.0f);
                const Vec2 o = q - (ca + ab * k);
                const float dd = (o.x * o.x + o.y * o.y) * inv;
                if (dd < 9.0f)
                    waveNow_[(std::size_t)y * (std::size_t)waveW_ + (std::size_t)x] += strength * std::exp(-dd);
            }
    }

    // One step: false once it has settled (nothing left above a ten-thousandth)
    bool BootScreen::WaveStep()
    {
        const int w = waveW_, h = waveH_;
        float peak = 0.0f;
        for (int y = 0; y < h; ++y)
        {
            const std::size_t r0 = (std::size_t)y * (std::size_t)w, ru = (std::size_t)(y > 0 ? y - 1 : 0) * (std::size_t)w,
                              rd = (std::size_t)(y < h - 1 ? y + 1 : h - 1) * (std::size_t)w;
            const float* c = &waveNow_[r0];
            const float* cu = &waveNow_[ru];
            const float* cd = &waveNow_[rd];
            const float* g = &wavePrev_[r0];
            const float* gu = &wavePrev_[ru];
            const float* gd = &wavePrev_[rd];
            float* out = &waveNext_[r0];
            for (int x = 0; x < w; ++x)
            {
                const int xm = x > 0 ? x - 1 : 0, xp = x < w - 1 ? x + 1 : w - 1;
                const float lap = c[xm] + c[xp] + cu[x] + cd[x] - 4.0f * c[x];
                const float lapPrev = g[xm] + g[xp] + gu[x] + gd[x] - 4.0f * g[x];
                const float v = (c[x] + (c[x] - g[x]) * 0.982f + 0.3f * lap + 0.08f * (lap - lapPrev)) * 0.998f;
                out[x] = v;
                peak = std::max(peak, std::fabs(v));
            }
        }
        wavePrev_.swap(waveNow_);   // the step before: this one
        waveNow_.swap(waveNext_);   // this one: the new
        return peak > 1e-4f;
    }

    // the slope at a point of the display (units): bilinear between the cells' centers; per cell, x right, y down
    Vec2 BootScreen::WaveSlope(Vec2 at) const
    {
        if (waveW_ < 2 || waveH_ < 2 || waveSlope_.empty())
            return Vec2(0.0f, 0.0f);
        const float fx = std::clamp(at.x / waveCell_.x - 0.5f, 0.0f, (float)(waveW_ - 1)), fy = std::clamp(at.y / waveCell_.y - 0.5f, 0.0f, (float)(waveH_ - 1));
        const int x0 = std::min((int)fx, waveW_ - 2), y0 = std::min((int)fy, waveH_ - 2);
        const float tx = fx - (float)x0, ty = fy - (float)y0;
        const auto s = [&](int x, int y, int c) { return waveSlope_[((std::size_t)y * (std::size_t)waveW_ + (std::size_t)x) * 2 + (std::size_t)c]; };
        Vec2 r;
        for (int c = 0; c < 2; ++c)
        {
            const float top = s(x0, y0, c) + (s(x0 + 1, y0, c) - s(x0, y0, c)) * tx;
            const float bottom = s(x0, y0 + 1, c) + (s(x0 + 1, y0 + 1, c) - s(x0, y0 + 1, c)) * tx;
            (c == 0 ? r.x : r.y) = top + (bottom - top) * ty;
        }
        return r;
    }

    bool BootScreen::Frame(Ui& ui, const BootInfo& info)
    {
        if (done_)
        {
            TextureRegistry& reg = ui.Ctx().Textures();
            if (cpuTex_)
            {
                reg.Destroy(cpuTex_);
                cpuTex_ = 0;
            }
            if (waveTex_)
            {
                reg.Destroy(waveTex_);
                waveTex_ = 0;
            }
            return false;
        }
        Context& ctx = ui.Ctx();
        const InputState& in = ctx.Input();
        if (start_ < 0.0)
            start_ = in.Time();
        const float t = (float)(in.Time() - start_);
        const Vec2 d = ctx.DisplaySize();
        const double now = in.Time();
        const float dt = std::clamp(in.DeltaTime(), 0.0f, 0.1f);

        // the work while it waits: the icons the hub draws first, rasterized at this density (a few a frame)
        static const Icon kWarm[] = {Icon::LayoutGrid, Icon::MousePointerClick, Icon::ToggleRight, Icon::SlidersHorizontal, Icon::TextCursorInput,
                                     Icon::Menu, Icon::ListTree, Icon::Bell, Icon::Palette, Icon::Layers, Icon::Activity, Icon::Minus, Icon::Maximize2,
                                     Icon::X, Icon::ChevronDown, Icon::ChevronRight, Icon::ChevronsUpDown, Icon::Check, Icon::Droplets, Icon::Orbit,
                                     Icon::Copy, Icon::Trash, Icon::Rocket, Icon::Terminal, Icon::RefreshCw, Icon::Settings, Icon::Info,
                                     Icon::CircleCheck, Icon::TriangleAlert, Icon::CircleX, Icon::Plus, Icon::SquarePlus, Icon::FolderOpen};
        constexpr int kWarmCount = (int)(sizeof(kWarm) / sizeof(kWarm[0]));
        if (ui.TextSystem())
            for (int i = 0; i < 4 && warmed_ < kWarmCount; ++i)
                WarmIcon(*ui.TextSystem(), kWarm[warmed_++], metrics::kIcon, info.scale);

        // it waits for a click (or Enter, Space); then the ribbons sink back into the flat and the pool fades
        const bool clicked = in.MouseClicked(MouseButton::Left);
        if (!begin_ && t > 0.6f && (clicked || in.KeyPressed(Key::Enter, false) || in.KeyPressed(Key::Space, false)))
            begin_ = true;
        if (begin_ && revealStart_ < 0.0)
            revealStart_ = now;
        const float since = revealStart_ < 0.0 ? 0.0f : (float)(now - revealStart_);
        reveal_ = revealStart_ < 0.0 ? 0.0f : EaseInOutCubic(Clamp01(since / 1.3f));
        if (reveal_ >= 1.0f)
        {
            done_ = true;
            return false;
        }

        // the pointer's waves: its path, its clicks
        WaveFrame(ui, d, dt, clicked);

        DrawList& dl = ctx.ForegroundDrawList();
        PainterEnv env = ui.Env();
        env.alpha = 1.0f;
        Painter p(dl, env);
        // there at once: the chrome fades in over its first third of a second already well formed, its ribbons rising
        // the rest of the way over a second and a half; as it goes they sink back into the flat
        const float intensity = EaseOutCubic(Clamp01(t / 0.35f));
        const float presence = (0.6f + 0.4f * EaseOutCubic(Clamp01(t / 1.6f))) * (1.0f - reveal_);
        // ---- the pool, the chrome in it (the screen is clean while the effect compiles; the pool fades as it goes)
        dl.AddRectFilled(Rect(Vec2(0, 0), d), Color(0.018f, 0.018f, 0.024f, 1.0f - reveal_).ToRgba8());
        if (gpu_)
        {
            // the waves' slope its image (the effect samples it); a clear white fill: it draws nothing of its own
            Paint clear;
            clear.kind = fx::PaintKind::Solid;
            clear.a = Color(1.0f, 1.0f, 1.0f, 0.0f);
            clear.b = Color(1.0f, 1.0f, 1.0f, 1.0f);
            p.Rect(Rect(Vec2(0, 0), d), Style().Fill(clear).Image(waveTex_).Effect(kEffect, intensity * (1.0f - reveal_), presence, (float)waveW_, (float)waveH_));
        }
        else
            DrawCpuField(ui, dl, d, t, presence, intensity * (1.0f - reveal_));

        // ---- the words: the name written in glass; after the click it is unwritten (the pen running back along it) as
        // the chrome sinks
        const float over = Clamp01(1.0f - since * 1.8f);
        if (over <= 0.002f)
            return true;
        p.SetAlpha(over);
        const float cy = d.y * 0.46f;
        // a soft shade behind them: the glass still has light to bend, the strokes read over the brightest chrome
        p.Rect(Rect(Vec2(0, 0), d), Style().Fill(Paint::Radial(Color(0.0f, 0.0f, 0.0f, 0.42f * intensity), Color(0.0f, 0.0f, 0.0f, 0.0f), Vec2(0.5f, cy / d.y), 0.32f)));
        const ScriptMetrics& sm = Script();
        const float unit = std::min(84.0f, d.x * 0.62f / sm.bounds.Width());   // the x-height
        const float thick = unit * 0.19f;                                        // the tube
        const Vec2 size = sm.bounds.Size() * unit;
        const float left = std::round(d.x * 0.5f - size.x * 0.5f), top = std::round(cy - size.y * 0.5f);
        const auto toScreen = [&](Vec2 q) { return Vec2(left + (q.x - sm.bounds.min.x) * unit, top + (sm.bounds.max.y - q.y) * unit); };
        // how far the pen has come: from the start, the whole name in 4.6 s at a hand's pace - near even, easing in and
        // out a little; unwritten in 0.6 s
        const float u = Clamp01((t - 0.15f) / 4.6f);
        const float writeIn = Mix(u, EaseInOutCubic(u), 0.35f);
        const float unwrite = begin_ ? EaseInOutCubic(Clamp01(since / 0.6f)) : 0.0f;
        const float pen = sm.total * writeIn * (1.0f - unwrite);
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
        const Style tube = Style().Glass(g).Shadow(Color::Black(0.35f * intensity), 16.0f, Vec2(0.0f, 8.0f));
        static thread_local std::vector<Vec2> pts;
        Vec2 tip;
        bool writing = false;
        for (int k = 0; k < kScriptStrokes; ++k)
        {
            const float done = pen - sm.start[k];
            if (done <= 0.0f)
                break;
            const int a = kScriptStrokeStart[k], z = kScriptStrokeStart[k + 1];
            if (z - a <= 2)
            {
                // a dot: the pen taps it on - it drops onto its place from a little above, growing, settling with a
                // small bounce; the pen's glint on it while it does
                const float k01 = Clamp01(done / sm.length[k]);
                const Vec2 at = toScreen(kScriptPoints[a]) - Vec2(0.0f, thick * 0.9f * (1.0f - EaseOutCubic(k01)));
                p.Circle(at, thick * 0.55f * std::max(EaseOutBack(k01), 0.0f), tube);
                if (k01 < 1.0f)
                {
                    tip = at;
                    writing = true;
                }
                continue;
            }
            pts.clear();
            pts.push_back(toScreen(kScriptPoints[a]));
            float run = 0.0f;
            for (int i = a + 1; i < z; ++i)
            {
                const float seg = Length(kScriptPoints[i] - kScriptPoints[i - 1]);
                if (run + seg >= done)
                {
                    pts.push_back(toScreen(Mix(kScriptPoints[i - 1], kScriptPoints[i], seg > 0.0f ? (done - run) / seg : 1.0f)));
                    tip = pts.back();
                    writing = true;
                    break;
                }
                run += seg;
                pts.push_back(toScreen(kScriptPoints[i]));
            }
            if (pts.size() >= 2)
                p.Polyline(pts.data(), (int)pts.size(), thick, tube);
        }
        // the pen's tip: a glint where the glass is being laid down
        if (writing && writeIn < 0.999f && !begin_)
            p.Circle(tip, thick * 0.32f, Style().Fill(Color::White(0.9f)).Glow(Color::White(), 14.0f, 0.8f));
        return true;
    }
}
