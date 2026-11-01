// Cipher - handwriting: text written out in a monoline cursive as a pen lays it down - a tube of liquid glass along the
// pen's path with its shadow, a glint at the pen's tip, the dots and crosses of a word after it - and unwritten (the pen
// running back). Any text (SetText: a cursive alphabet, tools/script_bspline.py) or strokes of your own (SetScript). It
// needs no Ui: a Painter and a clock.
//
//   cipher::Handwriting hand;
//   hand.SetText("Hello World");
//   // every frame:
//   hand.Draw(painter, area, input.Time());   // centered in `area`, written in HandwritingSpec::writeSeconds
//   // when it should go:
//   hand.Unwrite(input.Time());                // the pen runs back; Gone() once it has
//
// For machines that cannot afford glass, HandwritingSpec::quality:
//   Glass  the tube bends what is behind it (a capture of the backdrop and the FX shader's distance to the path)
//   Solid  a tube of one color with its shadow: the FX shader, no capture
//   Lines  the polyline as quads, a highlight along it: no FX shader at all
// Auto starts at `start` (SuggestQuality: what the device affords) and steps down while frames take too long.
#pragma once
#include "anim.hpp"
#include "esia/render/painter.hpp"
#include <string_view>
#include <vector>

namespace cipher
{
    using esia::Rect;

    // A handwriting's strokes in writing order: points in x-height units (y up, baseline 0); stroke k is points
    // strokeStart[k] .. strokeStart[k + 1] - 1. A stroke of two points or fewer that goes almost nowhere (under 0.1) is a dot.
    struct Script
    {
        std::vector<Vec2> points;
        std::vector<int> strokeStart;   // one more than the strokes: the last is the end
    };
    // `text` (UTF-8) in the cursive alphabet (tools/script_bspline.py: a-z, A-Z, 0-9 and . , ! ? ' - : &): the letters of a
    // word joined as one stroke, its dots and crosses after it; a character without a glyph leaves a gap
    Script WriteScript(std::string_view text);
    Script ScriptFromPoints(const Vec2* points, const int* strokeStart, int strokes);

    enum class HandwritingQuality { Auto, Glass, Solid, Lines };
    // What a device affords: Lines where the FX shader takes every point of a path in every pixel (Direct3D 9's shader
    // model 3) or there is no GPU (a software adapter: WARP, Microsoft Basic Render Driver, llvmpipe), else Glass
    HandwritingQuality SuggestQuality(std::string_view api, std::string_view adapter);

    struct HandwritingSpec
    {
        float delay = 0.15f;              // seconds before the pen starts
        float writeSeconds = 4.6f;        // the whole text, near an even pace, easing in and out a little
        float unwriteSeconds = 0.6f;      // the pen running back
        float fadeIn = 0.35f;             // the shadow and the shade behind come in over this long
        float fadeOut = 1.8f;             // after Unwrite the whole fades at this rate (1 / seconds)
        float maxXHeight = 84.0f;         // UI units
        float widthFraction = 0.62f;      // of the area's width at most
        float middle = 0.46f;             // where the text's middle sits, of the area's height from its top
        float tube = 0.19f;               // the tube's width, x-heights
        float shade = 0.42f;              // a soft shade behind the text, for the glass's light (0: none)
        bool glint = true;                // a light at the pen's tip while it writes
        HandwritingQuality quality = HandwritingQuality::Auto;
        HandwritingQuality start = HandwritingQuality::Glass;   // Auto's first (SuggestQuality)
        float slowFrame = 1.0f / 24.0f;   // Auto steps down while frames take longer than this (seconds)
        Color ink = Color(0.88f, 0.92f, 0.98f);   // Solid and Lines: the tube's color
        Color shadow = Color::Black(0.35f);
        float shadowBlur = 16.0f;
        Vec2 shadowOffset = Vec2(0.0f, 8.0f);
    };

    class Handwriting
    {
    public:
        explicit Handwriting(const HandwritingSpec& spec = {}) : spec_(spec) {}
        HandwritingSpec& Spec() { return spec_; }
        void SetText(std::string_view text);
        void SetScript(Script script);
        void Restart();                        // written again from the start at the next Draw
        void Unwrite(double now);              // the pen runs back from where it is
        bool Writing() const { return writing_; }
        bool Gone() const { return gone_; }
        HandwritingQuality Quality() const { return quality_; }   // the one it draws with now
        // Draws it centered in `area` at the clock's time `now` (seconds of any steady clock: the first call starts
        // it), at the painter's alpha
        void Draw(esia::Painter& p, const Rect& area, double now);

    private:
        struct Metrics
        {
            std::vector<float> start, length;   // each stroke's place along the pen's way, and its length
            float total = 0.0f;
            Rect bounds;
        };
        void Measure();
        void Simplify(float unit);
        void Watch(double now);
        HandwritingSpec spec_;
        Script script_;
        Metrics m_;
        // the strokes that touch (their bounds, grown by half the tube, overlap): drawn as one shape - glass that melts
        // together where they cross (a t's bar, an H's stems) - each group named by its first stroke
        std::vector<int> group_;
        // Solid and Lines: the strokes with the points closer than a fraction of a pixel to their neighbours' line left
        // out (the drawn size they were made for)
        Script thin_;
        Metrics thinM_;
        float thinUnit_ = 0.0f;
        double start_ = -1.0, unwriteStart_ = -1.0, last_ = -1.0;
        float slow_ = 0.0f;
        HandwritingQuality quality_ = HandwritingQuality::Glass;
        bool writing_ = false, gone_ = false;
    };
}
