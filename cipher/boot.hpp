// Cipher - the intro: a dark pool with ribbons of liquid chrome swirling through it - a field of stripes bent by
// itself, its bevelled edges mirroring a studio's lights - rising out of the flat as it starts, the pointer's path
// pressing a trail of waves into it (a click a deep dent); the name written in glass as a pen writes it - a monoline cursive laid down along
// its strokes, one stroke a word, a tube of liquid glass bending the chrome behind it (Apple's "hello"). It waits: a
// click (or Enter, Space) unwrites the name, the ribbons sink back into the flat as the pool fades, and the app shows
// through.
//
// The chrome is a user effect (Renderer::SetEffectSource with EffectSource(), id kEffect): Direct3D 10, 11 and 12
// compile it at run time - once: the D3D shader cache keeps it (the first run after the source changes waits for the
// compile, about a second). Elsewhere (SetGpu(false): Direct3D 9, whose 4096 instruction slots are too few for it, and
// the APIs that compile no effects) the CPU works the same chrome out into a small texture, stretched smooth.
#pragma once
#include "cipher.hpp"
#include <string>
#include <vector>

namespace cipher
{
    struct BootInfo
    {
        const char* api = "";
        std::string adapter;
        int width = 0, height = 0;   // pixels
        float scale = 1.0f;
    };

    class BootScreen
    {
    public:
        static constexpr esia::EffectId kEffect = 0xC1F0;
        static const char* EffectSource();

        void SetGpu(bool gpu) { gpu_ = gpu; }
        // Draws it over everything (the foreground list); false once it is over. While Showing() the app draws
        // nothing of its own; while it dissolves, the app draws under it.
        bool Frame(Ui& ui, const BootInfo& info);
        bool Showing() const { return !done_ && reveal_ <= 0.0f; }
        float Reveal() const { return reveal_; }
        void Skip() { done_ = true; }        // gone at once (captures)
        void Begin() { begin_ = true; }      // as a click does

    private:
        void DrawCpuField(Ui& ui, esia::DrawList& dl, Vec2 size, float t, float presence, float opacity);

        bool gpu_ = true, done_ = false, begin_ = false;
        double start_ = -1.0, revealStart_ = -1.0;
        float reveal_ = 0.0f;
        int warmed_ = 0;
        // the pointer's waves (boot.cpp: WaveFrame): a height field and the step before, its slope, its texture
        void WaveFrame(Ui& ui, Vec2 display, float dt, bool clicked);
        void WaveStroke(Vec2 a, Vec2 b, float strength, float radius);
        bool WaveStep();
        Vec2 WaveSlope(Vec2 at) const;
        int waveW_ = 0, waveH_ = 0;
        Vec2 waveCell_;
        std::vector<float> waveNow_, wavePrev_, waveNext_, waveSlope_;
        std::vector<std::uint8_t> wavePixels_;
        esia::TextureId waveTex_ = 0;
        float waveClock_ = 0.0f;
        bool waveCalm_ = true, strokePlaced_ = false;
        Vec2 strokeAt_;
        // the CPU's chrome: its texture and pixels
        esia::TextureId cpuTex_ = 0;
        int cpuW_ = 0, cpuH_ = 0;
        std::vector<std::uint8_t> cpuPixels_;
    };
}
