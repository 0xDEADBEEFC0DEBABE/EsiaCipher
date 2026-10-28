// Cipher's demo: the hub - a tab for each part of the library, sub-tabs in each - over a moving backdrop, a floating
// window, notifications and the watermark. glass_window's options apply (--api, --size, --scale, --frames,
// --screenshot ...); --script queues input, --tab picks the hub's tab, --no-boot skips the loading screen.
#include "app.hpp"
#include "boot.hpp"
#include "cipher.hpp"
#include "model.hpp"
#include "esia/render/renderer.hpp"
#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    using cipher::ButtonKind;
    using cipher::Icon;
    using cipher::SurfaceKind;
    using esia::Color;
    using esia::Rect;
    using esia::Style;
    using esia::Vec2;

    std::unique_ptr<cipher::Ui> gUi;
    cipher::BootScreen gBoot;
    bool gBootDone = false, gNoBoot = false;

    // --script "frame:what[:x,y]; ...": input queued at those frames, as a user would give it (captures of open menus,
    // dialogs, hovered rows, a dragged slider, typed text). what: move, click, rclick, down, up, type=<text>,
    // key=<Escape|RightShift|Enter|Tab>, wheel=<notches> (+ up, - down; at the pointer).
    struct ScriptStep
    {
        int frame = 0;
        std::string what;
        Vec2 at;
    };
    std::vector<ScriptStep> gScript;
    std::string gTab;
    // the scripted pointer: queued again every frame, so the real cursor over the window does not move it
    Vec2 gScriptMouse;
    bool gScriptMouseSet = false, gScriptWarned = false;

    void ParseScript(const std::string& text)
    {
        std::stringstream all(text);
        std::string step;
        while (std::getline(all, step, ';'))
        {
            ScriptStep st;
            const std::size_t a = step.find(':');
            if (a == std::string::npos)
                continue;
            st.frame = std::atoi(step.substr(0, a).c_str());
            const std::size_t b = step.find(':', a + 1);
            st.what = step.substr(a + 1, b == std::string::npos ? std::string::npos : b - a - 1);
            while (!st.what.empty() && st.what.front() == ' ')
                st.what.erase(st.what.begin());
            if (b != std::string::npos)
                std::sscanf(step.c_str() + b + 1, "%f,%f", &st.at.x, &st.at.y);
            gScript.push_back(st);
        }
    }

    void RunScript(esia::Context& ctx)
    {
        using esia::InputEvent;
        using esia::MouseButton;
        const int frame = (int)ctx.FrameCount();
        // The window thread queues the real cursor's moves while a frame runs, after the scripted pointer queued here:
        // a capture taken while the real mouse moved over the window may not show what the script did.
        if (gScriptMouseSet && !gScriptWarned && !(ctx.Input().MousePos() == gScriptMouse))
        {
            std::fprintf(stderr, "script: the real cursor moved the pointer at frame %d\n", frame);
            gScriptWarned = true;
        }
        if (gScriptMouseSet)
            ctx.QueueInput(InputEvent::MouseMove(gScriptMouse));
        for (const ScriptStep& st : gScript)
        {
            if (st.frame != frame)
                continue;
            if (st.what == "move" || st.what == "click" || st.what == "rclick" || st.what == "down")
            {
                gScriptMouse = st.at;
                gScriptMouseSet = true;
            }
            if (st.what == "move")
                ctx.QueueInput(InputEvent::MouseMove(st.at));
            else if (st.what == "click" || st.what == "rclick")
            {
                const MouseButton b = st.what == "click" ? MouseButton::Left : MouseButton::Right;
                ctx.QueueInput(InputEvent::MouseMove(st.at));
                ctx.QueueInput(InputEvent::Button(b, true));
                ctx.QueueInput(InputEvent::Button(b, false));
            }
            else if (st.what == "down")
            {
                ctx.QueueInput(InputEvent::MouseMove(st.at));
                ctx.QueueInput(InputEvent::Button(MouseButton::Left, true));
            }
            else if (st.what == "up")
                ctx.QueueInput(InputEvent::Button(MouseButton::Left, false));
            else if (st.what.rfind("wheel=", 0) == 0)
                ctx.QueueInput(InputEvent::Wheel(0.0f, (float)std::atof(st.what.c_str() + 6)));
            else if (st.what.rfind("type=", 0) == 0)
                ctx.QueueInput(InputEvent::TextEvent(st.what.substr(5)));
            else if (st.what.rfind("key=", 0) == 0)
            {
                const std::string k = st.what.substr(4);
                const esia::Key key = k == "Escape" ? esia::Key::Escape : k == "RightShift" ? esia::Key::RightShift : k == "Enter" ? esia::Key::Enter : esia::Key::Tab;
                ctx.QueueInput(InputEvent::KeyEvent(key, true));
                ctx.QueueInput(InputEvent::KeyEvent(key, false));
            }
        }
    }

    struct DemoState
    {
        // overview
        bool glass = true, paused = false, across = false;
        double pauseAt = -1.0;   // the clock stops once the Pause switch has finished moving (real time)
        float opacity = 0.80f, timeScale = 1.0f;
        float progress = 0.62f;
        int sent = 0;
        // buttons
        bool enabled = true, following = false;
        // toggles
        bool sound = false, telemetry = true, autoUpdate = true, haptics = true;
        bool check[4] = {true, false, true, false};
        unsigned flags = 0x5u;
        int radio = 1, selected = 1;
        // sliders
        float scale = 1.0f, speed = 0.45f, gamma = 2.2f, volume = 0.7f;
        int quality = 3, threads = 8;
        // structure
        bool vsync = true, hdr = false, spatial = true, ipv6 = false;
        float fov = 90.0f, master = 0.8f;
        int file = 0;
        // notifications
        float duration = 4.5f;
        // themes
        int materialKinds[4] = {3, 3, 2, 2};
        int backdrop = 0;   // the theme's, daylight, dusk
        bool inspector = true;
    } gState;

    // Paused, the clock stops a moment later - once the switch that paused it has slid all the way - so nothing is
    // caught half-way that the user just asked for; unpaused, it runs again at once.
    // the Data tab's numbers, colors, list, table and plots
    struct DataState
    {
        float speed = 1.25f, offset = 12.0f;
        int samples = 64;
        float accent[4] = {0.36f, 0.66f, 1.0f, 1.0f}, tint[3] = {0.95f, 0.42f, 0.30f}, picked[4] = {0.24f, 0.84f, 0.55f, 0.85f};
        int listSel = 2;
        int order[8] = {0, 1, 2, 3, 4, 5, 6, 7};
        float frames[48] = {};
        float builds[12] = {};
        double nextFrame = 0.0, nextBuilds = 0.0;
        unsigned seed = 0x9E3779B9u;
        float Random()
        {
            seed = seed * 1664525u + 1013904223u;
            return (float)(seed >> 8) / 16777216.0f;
        }
    } gData;

    void ApplyClock(cipher::Ui& ui)
    {
        if (!gState.paused)
        {
            ui.SetTimeScale(gState.timeScale);
            gState.pauseAt = -1.0;
        }
        else if (gState.pauseAt < 0.0 && ui.TimeScale() > 0.0f)
            gState.pauseAt = ui.Ctx().Input().Time() + 0.45;
    }

    // What lies behind the windows: the theme's own backdrop, a pale daylight wallpaper or a dusk one - the glass keeps
    // the theme's text legible over each. A new one fades in over the old.
    void DrawBackdrop(cipher::Ui& ui, esia::DrawList& dl, const Rect& r)
    {
        const esia::Id id = esia::HashLabel("##demo-backdrop", 0x42444B50u);
        float w[3];
        for (int i = 0; i < 3; ++i)
            w[i] = ui.Anim(id, i, gState.backdrop == i ? 1.0f : 0.0f, cipher::kGentle);
        const float t = (float)ui.Time();
        const auto wallpaper = [&](int which, float alpha) {
            if (which == 0)
            {
                ui.Backdrop(dl, r, 1.0f, alpha);
                return;
            }
            esia::Painter p = ui.Painter(dl);
            p.SetAlpha(alpha);
            const bool day = which == 1;
            p.Rect(r, Style().Fill(day ? esia::Paint::Linear(Color::Hex(0xF7F8FB), Color::Hex(0xD9DFE8), 90.0f)
                                       : esia::Paint::Linear(Color::Hex(0xFF9A5A), Color::Hex(0x3B2A6B), 90.0f)));
            const Color blobs[3] = {day ? Color::Hex(0xFFE2BF) : Color::Hex(0xFFD36E), day ? Color::Hex(0xC9E4FF) : Color::Hex(0xFF5E8A),
                                    day ? Color::Hex(0xFFFFFF) : Color::Hex(0x6E8BFF)};
            for (int i = 0; i < 3; ++i)
            {
                const float fi = (float)i;
                const Vec2 c(r.min.x + r.Width() * (0.5f + 0.32f * std::sin(0.11f * t + fi * 2.1f)), r.min.y + r.Height() * (0.5f + 0.28f * std::cos(0.09f * t + fi * 1.7f)));
                p.Circle(c, r.Height() * 0.55f, Style().Fill(esia::Paint::Radial(blobs[i].WithAlpha(0.85f), blobs[i].WithAlpha(0.0f))));
            }
        };
        // the ones fading out under the one coming in
        for (int i = 0; i < 3; ++i)
            if (i != gState.backdrop && w[i] > 0.001f)
                wallpaper(i, 1.0f);
        wallpaper(gState.backdrop, std::min(w[gState.backdrop], 1.0f));
    }

    // A drop of liquid glass on a track: sent across by a spring, so a click before it lands turns it around from where
    // it is, at the speed it has. It stretches with its speed.
    void DropTrack(cipher::Ui& ui)
    {
        const float w = ui.ContentWidth();
        ui.Dummy(Vec2(w, 64.0f));
        const Rect r = ui.Ctx().LastItemRect();
        const esia::Id id = ui.Ctx().GetId("##drop-track");
        const float k = ui.Anim(id, 0, gState.across ? 1.0f : 0.0f, cipher::Motion{0.55f, 0.62f});
        const float speed = ui.AnimVelocity(id, 0);
        esia::Painter p = ui.Painter();
        const float cy = r.Center().y, x0 = r.min.x + 34.0f, x1 = r.max.x - 34.0f;
        p.Rect(r, Style().Radius(cipher::metrics::kRadiusControl).Fill(ui.Ink(0.03f)).Stroke(1.0f / ui.PixelScale(), ui.Ink(0.07f), 0.0f));
        for (int i = 0; i <= 20; ++i)
        {
            const float x = std::round((x0 + (x1 - x0) * (float)i / 20.0f) * ui.PixelScale()) / ui.PixelScale();
            const float h = (i % 5 == 0) ? 12.0f : 6.0f;
            p.Rect(Rect(x, cy - h * 0.5f, x + 1.0f / ui.PixelScale(), cy + h * 0.5f), Style().Fill(ui.Ink(i % 5 == 0 ? 0.22f : 0.12f)));
        }
        p.Capsule(Rect(x0, cy - 1.5f, x1, cy + 1.5f), Style().Fill(ui.T().accent.Fade(0.55f)));
        const float R = 17.0f;
        // where the spring throws it past an end, it bounces off the end: its place mirrored back off it (60 % of the way
        // the spring goes past), flattened against the end at the hit - as much as it was going fast, its leading edge
        // on the end - and back to rest on it as the spring settles. Every frame its place and shape follow the spring:
        // it never stops at the end. Its stretch with speed turns into the squash at the hit.
        const float over = k > 1.0f ? (k - 1.0f) * (x1 - x0) : (k < 0.0f ? k * (x1 - x0) : 0.0f);   // signed
        const float back = std::fabs(over) * 0.6f;   // how far it has bounced back off the end
        const float hit = over != 0.0f ? std::exp(-back / 5.0f) * std::min(std::fabs(speed) / 2.5f, 1.0f) : 0.0f;
        const float squash = 0.25f * hit;
        const float stretch = std::min(std::fabs(speed) * 0.09f, 0.55f) * (over != 0.0f ? 1.0f - std::exp(-back / 5.0f) : 1.0f);
        esia::GlassMaterial g;
        g.blur = 0.0f;
        g.refraction = 12.0f;
        g.bezel = R;
        g.dispersion = 0.7f;
        g.specular = 1.0f;
        g.saturation = 1.3f;
        g.magnify = 0.35f;
        g.tint = Color(1.0f, 1.0f, 1.0f, 0.05f);
        g.rim = Color::White(0.6f);
        g.noise = 0.0f;
        const Vec2 size(R * 2.0f * (1.0f + stretch - squash), R * 2.0f * (1.0f - stretch * 0.35f + squash * 0.6f));
        const float x = over > 0.0f ? x1 - back + R * squash : (over < 0.0f ? x0 + back - R * squash : x0 + (x1 - x0) * k);
        p.Rect(Rect::FromCenter(Vec2(x, cy), size), Style().Radius(size.y * 0.5f).Glass(g).Shadow(Color::Black(0.22f), 12.0f, Vec2(0.0f, 4.0f)));
    }

    void Overview(cipher::Ui& ui)
    {
        if (!ui.BeginTabBar("##overview"))
            return;
        if (ui.BeginTabItem("Glass", Icon::Droplets))
        {
            ui.Group("Panels");
            cipher::MaterialSpec& window = ui.MaterialOf(SurfaceKind::Window);
            ui.Row("Glass panels", "Frosted glass over the backdrop: blur, refraction, a lit rim.");
            if (ui.Switch("##glass", &gState.glass))
                window.kind = gState.glass ? cipher::Material::Frosted : cipher::Material::Solid;
            ui.Row("Window opacity", "How much of the theme's color the glass holds.");
            ui.SliderFloat("##opacity", &window.opacity, 0.2f, 1.0f, "%.2f");
            ui.Row("Frost", "How much the glass blurs what lies behind it, in units.");
            ui.SliderFloat("##blur", &window.blur, 0.0f, 48.0f, "%.0f");
            ui.EndTabItem();
        }
        if (ui.BeginTabItem("Motion", Icon::Orbit))
        {
            ui.Group("The clock");
            ui.Row("Time scale", "Every animation runs on this clock: slow it down to watch one.");
            if (ui.SliderFloat("##timescale", &gState.timeScale, 0.05f, 1.5f, "%.2f"))
                ApplyClock(ui);
            ui.Row("Pause", "Every spring stops where it is; step them a frame at a time.");
            if (ui.Switch("##pause", &gState.paused))
                ApplyClock(ui);
            ui.Row("Step one frame", "A sixtieth of a second, while paused.");
            ui.BeginDisabled(!gState.paused);
            if (ui.Button("Step", ButtonKind::Secondary, Vec2(0, 0), Icon::StepForward))
                ui.StepFrame();
            ui.EndDisabled();
            ui.Group("Interrupt it");
            ui.Row("Send the drop across", "Click again before it lands: it turns around from where it is, at its speed.");
            if (ui.Button(gState.across ? "Back###drop" : "Across###drop", ButtonKind::Primary, Vec2(0, 0), gState.across ? Icon::Rewind : Icon::Play))
                gState.across = !gState.across;
            DropTrack(ui);
            ui.EndTabItem();
        }
        if (ui.BeginTabItem("Session", Icon::Activity))
        {
            ui.Group("Progress");
            ui.Row("Indexing", "Rebuilding the icon atlas at the display's density.");
            ui.ProgressBar(gState.progress);
            ui.Row("Syncing", "No end in sight: an indeterminate bar.");
            ui.ProgressBar(-1.0f);
            ui.Group("Notifications");
            ui.Row("Send a notification", "Slides in at the top right; the ring round its close button is the time left.");
            if (ui.Button("Notify", ButtonKind::Primary, Vec2(0, 0), Icon::Bell))
            {
                static const char* titles[4] = {"Profile saved", "New device", "Low disk space", "Build failed"};
                static const char* bodies[4] = {"Your changes are stored and synced.", "A sign-in from Sydney was approved.",
                                                "Less than 2 GB left on drive D:.", "esia_render: 2 errors in painter.cpp."};
                const int k = gState.sent++ % 4;
                ui.Notify({(cipher::NoteKind)((k + 1) % 4), titles[k], bodies[k]});
            }
            ui.EndTabItem();
        }
        ui.EndTabBar();
    }

    void Buttons(cipher::Ui& ui)
    {
        if (!ui.BeginTabBar("##buttons"))
            return;
        if (ui.BeginTabItem("Kinds", Icon::Shapes))
        {
            ui.Group("Kinds");
            ui.Button("Primary", ButtonKind::Primary);
            ui.SameLine();
            ui.Button("Secondary");
            ui.SameLine();
            ui.Button("Outline", ButtonKind::Outline);
            ui.SameLine();
            ui.Button("Ghost", ButtonKind::Ghost);
            ui.SameLine();
            ui.Button("Delete", ButtonKind::Danger, Vec2(0, 0), Icon::Trash);
            ui.Spacing();
            ui.Group("With icons");
            ui.Button("Deploy", ButtonKind::Primary, Vec2(0, 0), Icon::Rocket);
            ui.SameLine();
            ui.Button("Run", ButtonKind::Secondary, Vec2(0, 0), Icon::Terminal);
            ui.SameLine();
            ui.IconButton("##copy", Icon::Copy, "Copy");
            ui.SameLine();
            ui.IconButton("##refresh", Icon::RefreshCw, "Refresh");
            ui.SameLine();
            ui.IconButton("##settings", Icon::Settings, "Settings");
            ui.EndTabItem();
        }
        if (ui.BeginTabItem("In rows", Icon::Rows3))
        {
            ui.Group("Storage");
            ui.Row("Clear the cache", "Removes 1.2 GB of rasterized glyphs and icons.");
            ui.Button("Clear", ButtonKind::Secondary);
            ui.Row("Export the settings", "A file you can take to another machine.");
            ui.Button("Export", ButtonKind::Outline, Vec2(0, 0), Icon::Package);
            ui.Row("Reset everything", "Back to the defaults - this cannot be undone.");
            ui.Button("Reset", ButtonKind::Danger);
            ui.EndTabItem();
        }
        if (ui.BeginTabItem("States", Icon::ToggleRight))
        {
            ui.Group("A label and a kind that change");
            ui.Row("Follow the channel", "The button blends into its new kind as its label rolls over.");
            if (ui.Button(gState.following ? "Following###follow" : "Follow###follow", gState.following ? ButtonKind::Secondary : ButtonKind::Primary,
                          Vec2(0, 0), gState.following ? Icon::Check : Icon::Plus))
                gState.following = !gState.following;
            ui.Group("Disabled");
            ui.Row("Buttons enabled", "The buttons under it dim as they are disabled.");
            ui.Switch("##enabled", &gState.enabled);
            ui.BeginDisabled(!gState.enabled);
            ui.Button("Save", ButtonKind::Primary);
            ui.SameLine();
            ui.Button("Discard");
            ui.SameLine();
            ui.SmallButton("Small");
            ui.SameLine();
            ui.SmallButton("Small primary", ButtonKind::Primary);
            ui.EndDisabled();
            ui.EndTabItem();
        }
        ui.EndTabBar();
    }

    void Toggles(cipher::Ui& ui)
    {
        if (!ui.BeginTabBar("##toggles"))
            return;
        if (ui.BeginTabItem("Switches", Icon::ToggleRight))
        {
            ui.Group("Device");
            ui.Row("Sound effects");
            ui.Switch("##sound", &gState.sound);
            ui.Row("Haptics", "A tick under the finger as a switch turns.");
            ui.Switch("##haptics", &gState.haptics);
            ui.Row("Telemetry", "Anonymous frame times, to tune the renderer.");
            ui.Switch("##telemetry", &gState.telemetry);
            ui.Row("Update automatically");
            ui.Switch("##update", &gState.autoUpdate);
            ui.EndTabItem();
        }
        if (ui.BeginTabItem("Check boxes", Icon::SquareCheck))
        {
            ui.Group("Overlays");
            ui.Checkbox("Show hit boxes", &gState.check[0]);
            ui.Checkbox("Show glyph pages", &gState.check[1]);
            ui.Checkbox("Show backdrop captures", &gState.check[2]);
            ui.Row("Lock the layout", "A check box in a row.");
            ui.Checkbox("##lock", &gState.check[3]);
            ui.Group("Flags");
            ui.CheckboxFlags("Bold", &gState.flags, 0x1u);
            ui.SameLine(18.0f);
            ui.CheckboxFlags("Italic", &gState.flags, 0x2u);
            ui.SameLine(18.0f);
            ui.CheckboxFlags("Underline", &gState.flags, 0x4u);
            ui.EndTabItem();
        }
        if (ui.BeginTabItem("Choices", Icon::ListChecks))
        {
            ui.Group("Radio buttons");
            ui.RadioButton("Direct3D 12", &gState.radio, 0);
            ui.SameLine(18.0f);
            ui.RadioButton("Vulkan", &gState.radio, 1);
            ui.SameLine(18.0f);
            ui.RadioButton("OpenGL", &gState.radio, 2);
            ui.Group("Selectables");
            const char* items[4] = {"Graphite", "Matrix", "Neon", "Amber"};
            const Icon icons[4] = {Icon::Hexagon, Icon::Terminal, Icon::Zap, Icon::Sun};
            for (int i = 0; i < 4; ++i)
                if (ui.Selectable(items[i], gState.selected == i, Vec2(0, 0), icons[i]))
                    gState.selected = i;
            ui.EndTabItem();
        }
        ui.EndTabBar();
    }

    void Sliders(cipher::Ui& ui)
    {
        if (!ui.BeginTabBar("##sliders"))
            return;
        if (ui.BeginTabItem("In rows", Icon::SlidersHorizontal))
        {
            ui.Group("Display");
            ui.Row("Render scale", "Pixels per UI unit. Drag it: the knob turns into a drop of glass.");
            ui.SliderFloat("##scale", &gState.scale, 0.5f, 2.0f, "%.2f");
            ui.Row("Animation speed");
            ui.SliderFloat("##speed", &gState.speed, 0.0f, 1.0f, "%.2f");
            ui.Row("Quality", "0 is the fastest, 5 the finest.");
            ui.SliderInt("##quality", &gState.quality, 0, 5);
            ui.EndTabItem();
        }
        if (ui.BeginTabItem("Vertical", Icon::SlidersVertical))
        {
            ui.Group("Equalizer");
            static float bands[6] = {0.62f, 0.48f, 0.70f, 0.52f, 0.36f, 0.58f};
            static const char* names[6] = {"60 Hz", "230 Hz", "910 Hz", "3.6 kHz", "14 kHz", "Gain"};
            for (int i = 0; i < 6; ++i)
            {
                if (i)
                    ui.SameLine(26.0f);
                ui.PushId(i);
                ui.VSliderFloat(names[i], Vec2(56.0f, 180.0f), &bands[i], 0.0f, 1.0f, "%.2f");
                ui.PopId();
            }
            ui.EndTabItem();
        }
        if (ui.BeginTabItem("Label column", Icon::Columns3))
        {
            ui.Group("Renderer");
            ui.SliderInt("Worker threads", &gState.threads, 1, 32);
            ui.SliderFloat("Opacity##2", &gState.opacity, 0.0f, 1.0f);
            ui.SliderFloat("Gamma", &gState.gamma, 1.0f, 3.0f, "%.2f");
            ui.SliderFloat("Volume", &gState.volume, 0.0f, 1.0f, "%.2f");
            ui.EndTabItem();
        }
        ui.EndTabBar();
    }

    void Inputs(cipher::Ui& ui)
    {
        static std::string name = "Cipher", search, secret = "hunter2", host = "127.0.0.1",
                           notes = "Cipher draws every pixel with Esia's Painter.\nType here: Enter breaks the line,\nCtrl+Z takes it back.";
        static float gain = 1.25f, ratio = 0.5f;
        static int count = 12;
        if (!ui.BeginTabBar("##inputs"))
            return;
        if (ui.BeginTabItem("Text", Icon::Type))
        {
            ui.Group("Fields");
            ui.Row("Display name", "Shown in the title bar and on notifications.");
            ui.InputText("##name", &name);
            ui.Row("Search");
            ui.InputTextWithHint("##search", "Type to filter the icons...", &search);
            ui.Row("Password", "Bullets for the characters; nothing reaches the clipboard.");
            ui.InputText("##secret", &secret, cipher::InputTextFlags_Password);
            ui.Row("Host");
            ui.InputText("##host", &host);
            ui.EndTabItem();
        }
        if (ui.BeginTabItem("Numbers", Icon::Hash))
        {
            ui.Group("Numbers");
            ui.Row("Gain", "Type a value or step it: the digits roll.");
            ui.InputFloat("##gain", &gain, 0.05f, "%.2f");
            ui.Row("Samples");
            ui.InputInt("##count", &count, 1);
            ui.InputFloat("Ratio", &ratio, 0.0f, "%.3f");
            ui.EndTabItem();
        }
        if (ui.BeginTabItem("Notes", Icon::FileText))
        {
            ui.Group("Several lines");
            ui.InputTextMultiline("##notes", &notes, Vec2(0.0f, 160.0f));
            ui.EndTabItem();
        }
        ui.EndTabBar();
    }

    void Menus(cipher::Ui& ui)
    {
        static int api = 1, quality = 2, font = 0;
        static bool confirm = false;
        if (!ui.BeginTabBar("##menus"))
            return;
        if (ui.BeginTabItem("Combos", Icon::ChevronsUpDown))
        {
            ui.Group("Combos");
            ui.Row("Graphics API", "The backend the renderer draws with.");
            const char* apis[6] = {"Direct3D 12", "Direct3D 11", "Vulkan", "OpenGL", "Direct3D 10", "Direct3D 9"};
            ui.Combo("##api", &api, apis, 6);
            ui.Row("Text rendering");
            const char* modes[4] = {"Design outlines", "Light hinting", "Crisp (GDI)", "ClearType"};
            ui.Combo("##text", &quality, modes, 4);
            const char* fonts[3] = {"Segoe UI Variable", "Cascadia Mono", "Microsoft YaHei UI"};
            ui.Combo("Interface font", &font, fonts, 3);
            ui.EndTabItem();
        }
        if (ui.BeginTabItem("Context menu", Icon::MousePointerClick))
        {
            ui.Group("Context menu");
            ui.Row("Right-click the button", "A context menu with icons, shortcuts and a submenu.");
            ui.Button("Right-click me");
            if (ui.BeginPopupContextItem("##ctx"))
            {
                ui.MenuItem("Copy", "Ctrl+C", false, true, Icon::Copy);
                ui.MenuItem("Duplicate", "Ctrl+D", false, true, Icon::Layers);
                if (ui.BeginMenu("Move to"))
                {
                    ui.MenuItem("Overview");
                    ui.MenuItem("Archive");
                    ui.MenuItem("Trash", nullptr, false, false);
                    ui.EndMenu();
                }
                ui.MenuSeparator();
                ui.MenuItem("Delete", "Del", false, true, Icon::Trash);
                ui.EndPopup();
            }
            ui.EndTabItem();
        }
        if (ui.BeginTabItem("Dialog", Icon::MessageSquare))
        {
            ui.Group("Dialog");
            ui.Row("A modal dialog", "Over a frosted scrim; a click beside it shakes it.");
            if (ui.Button("Open dialog", ButtonKind::Primary))
                confirm = true;
            ui.EndTabItem();
        }
        ui.EndTabBar();
        if (ui.BeginPopupModal("Delete 3 snapshots?", &confirm))
        {
            ui.TextUnformatted("The snapshots are removed from this device and from the cloud. This cannot be undone.", cipher::Tone::Secondary,
                               cipher::metrics::kFont, true);
            ui.Spacing(12.0f);
            const float half = std::floor((ui.ContentWidth() - 6.0f) * 0.5f);
            if (ui.Button("Delete", ButtonKind::Danger, Vec2(half, 0.0f)))
            {
                confirm = false;
                ui.Notify({cipher::NoteKind::Success, "Snapshots deleted", "3 snapshots, 412 MB."});
            }
            ui.SameLine();
            if (ui.Button("Cancel", ButtonKind::Secondary, Vec2(half, 0.0f)))
                confirm = false;
            ui.EndPopupModal();
        }
    }

    void Data(cipher::Ui& ui)
    {
        if (!ui.BeginTabBar("##data"))
            return;
        if (ui.BeginTabItem("Drags", Icon::MoveHorizontal))
        {
            ui.Group("Drag a number");
            ui.Row("Speed", "Drag sideways: Shift is faster, Alt finer; a double click types.");
            ui.DragFloat("##speed", &gData.speed, 0.005f, 0.0f, 4.0f, "%.3f");
            ui.Row("Samples", "Whole numbers from 1 to 256.");
            ui.DragInt("##samples", &gData.samples, 0.25f, 1, 256);
            ui.Row("Offset", "No range: as far as it is dragged.");
            ui.DragFloat("##offset", &gData.offset, 0.1f, 0.0f, 0.0f, "%.1f");
            ui.EndTabItem();
        }
        if (ui.BeginTabItem("Colors", Icon::Pipette))
        {
            ui.Group("Edit");
            ui.Row("Accent", "The swatch opens the picker; or type a hex code.");
            ui.ColorEdit4("##accent", gData.accent);
            ui.Row("Tint", "Three channels, no alpha.");
            ui.ColorEdit3("##tint", gData.tint);
            ui.Group("Picker");
            ui.ColorPicker4("##picker", gData.picked);
            ui.Group("Swatches");
            static const Color swatches[7] = {Color::Hex(0xFF5D6C), Color::Hex(0xF5B83D), Color::Hex(0x3DD68C), Color::Hex(0x5BA8FF), Color::Hex(0x9D7BFF),
                                              Color::Hex(0xFF3FD8), Color(1.0f, 1.0f, 1.0f, 0.5f)};
            for (int i = 0; i < 7; ++i)
            {
                if (i)
                    ui.SameLine(10.0f);
                ui.PushId(i);
                if (ui.ColorButton("##swatch", swatches[i], Vec2(36.0f, 36.0f)))
                {
                    gData.picked[0] = swatches[i].r, gData.picked[1] = swatches[i].g, gData.picked[2] = swatches[i].b, gData.picked[3] = swatches[i].a;
                }
                ui.PopId();
            }
            ui.EndTabItem();
        }
        if (ui.BeginTabItem("Lists", Icon::List))
        {
            ui.Group("List box");
            static const char* names[10] = {"Direct3D 12", "Direct3D 11", "Vulkan", "OpenGL", "Metal", "Direct3D 10", "Direct3D 9", "OpenGL ES", "WebGPU", "Software"};
            ui.ListBox("##apis", &gData.listSel, names, 10, 5);
            ui.EndTabItem();
        }
        if (ui.BeginTabItem("Table", Icon::Table))
        {
            struct File
            {
                const char* name;
                const char* kind;
                int size;   // KB
                int days;   // since it was changed
            };
            static const File files[8] = {{"painter.cpp", "C++", 84, 1},   {"renderer.cpp", "C++", 132, 3}, {"esia_fx.hlsl", "HLSL", 61, 0},
                                          {"README.md", "Markdown", 18, 7}, {"theme.hpp", "C++", 3, 12},     {"boot_script.inc", "Data", 22, 0},
                                          {"glass.png", "Image", 940, 30}, {"CMakeLists.txt", "CMake", 5, 2}};
            ui.Group("Files");
            if (ui.BeginTable("##files", 4, cipher::TableFlags_Sortable | cipher::TableFlags_Resizable | cipher::TableFlags_RowBg))
            {
                ui.TableSetupColumn("Name", 2.2f);
                ui.TableSetupColumn("Kind", 1.2f);
                ui.TableSetupColumn("Size", 1.0f);
                ui.TableSetupColumn("Changed", 1.2f);
                ui.TableHeadersRow();
                if (ui.TableSortChanged())
                {
                    const int column = ui.TableSortColumn();
                    const bool ascending = ui.TableSortAscending();
                    std::sort(std::begin(gData.order), std::end(gData.order), [&](int a, int b) {
                        const File& x = files[a];
                        const File& y = files[b];
                        int c = 0;
                        if (column == 0)
                            c = std::strcmp(x.name, y.name);
                        else if (column == 1)
                            c = std::strcmp(x.kind, y.kind);
                        else if (column == 2)
                            c = x.size - y.size;
                        else
                            c = x.days - y.days;
                        if (c == 0)
                            c = a - b;
                        return ascending ? c < 0 : c > 0;
                    });
                }
                for (int i : gData.order)
                {
                    const File& f = files[i];
                    ui.TableNextRow(i);
                    ui.TableNextColumn();
                    ui.Text("%s", f.name);
                    ui.TableNextColumn();
                    ui.TextDisabled("%s", f.kind);
                    ui.TableNextColumn();
                    ui.Text("%d KB", f.size);
                    ui.TableNextColumn();
                    if (f.days == 0)
                        ui.TextColored(ui.T().success, "today");
                    else
                        ui.Text("%d %s ago", f.days, f.days == 1 ? "day" : "days");
                }
                ui.EndTable();
            }
            ui.Caption("Click a header to sort by it (the rows travel to their places), again to reverse; drag the borders between headers.");
            ui.EndTabItem();
        }
        if (ui.BeginTabItem("Plots", Icon::ChartLine))
        {
            ui.Group("Frame times");
            ui.PlotLines("##frames", gData.frames, 48, 0.0f, FLT_MAX);
            ui.Group("Builds per hour");
            ui.PlotHistogram("##builds", gData.builds, 12);
            ui.Caption("The values glide as they change; the pointer reads them.");
            ui.EndTabItem();
        }
        ui.EndTabBar();
    }

    void Structure(cipher::Ui& ui)
    {
        if (!ui.BeginTabBar("##structure"))
            return;
        if (ui.BeginTabItem("Headers", Icon::PanelTop))
        {
            if (ui.CollapsingHeader("Display", true))
            {
                ui.Row("Vertical sync", "Frames wait for the display's refresh.");
                ui.Switch("##vsync", &gState.vsync);
                ui.Row("HDR output", "scRGB on displays that take it.");
                ui.Switch("##hdr", &gState.hdr);
                ui.Row("Field of view");
                ui.SliderFloat("##fov", &gState.fov, 60.0f, 120.0f, "%.0f");
            }
            if (ui.CollapsingHeader("Audio"))
            {
                ui.Row("Master volume");
                ui.SliderFloat("##master", &gState.master, 0.0f, 1.0f, "%.2f");
                ui.Row("Spatial audio", "Sound placed around the listener.");
                ui.Switch("##spatial", &gState.spatial);
            }
            if (ui.CollapsingHeader("Network"))
            {
                ui.Row("Prefer IPv6");
                ui.Switch("##ipv6", &gState.ipv6);
                ui.Row("Test the connection", "Pings the update server.");
                if (ui.Button("Test", ButtonKind::Secondary, Vec2(0, 0), Icon::Radar))
                    ui.Notify({cipher::NoteKind::Success, "Connected", "18 ms to the update server."});
            }
            ui.EndTabItem();
        }
        if (ui.BeginTabItem("Tree", Icon::ListTree))
        {
            ui.Group("Project");
            const auto file = [&](const char* name, Icon icon, int index) {
                if (ui.Selectable(name, gState.file == index, Vec2(0, 32.0f), icon))
                    gState.file = index;
            };
            if (ui.TreeNode("cipher", true, Icon::Folder))
            {
                if (ui.TreeNode("render", false, Icon::Folder))
                {
                    file("painter.cpp", Icon::FileCode, 0);
                    file("frame_plan.cpp", Icon::FileCode, 1);
                    file("renderer.hpp", Icon::FileCode, 2);
                    ui.TreePop();
                }
                if (ui.TreeNode("text", true, Icon::Folder))
                {
                    file("shaper.cpp", Icon::FileCode, 3);
                    file("atlas.cpp", Icon::FileCode, 4);
                    ui.TreePop();
                }
                file("cipher.hpp", Icon::FileCode, 5);
                ui.TreePop();
            }
            if (ui.TreeNode("docs", false, Icon::Folder))
            {
                file("README.md", Icon::FileText, 6);
                file("CHANGES.md", Icon::FileText, 7);
                ui.TreePop();
            }
            ui.EndTabItem();
        }
        ui.EndTabBar();
    }

    void Notifications(cipher::Ui& ui)
    {
        if (!ui.BeginTabBar("##notes"))
            return;
        if (ui.BeginTabItem("Kinds", Icon::Bell))
        {
            ui.Group("Kinds");
            if (ui.Button("Info", ButtonKind::Secondary, Vec2(0, 0), Icon::Info))
                ui.Notify({cipher::NoteKind::Info, "Heads up", "A new version of the icon set is available.", Icon::None, gState.duration});
            ui.SameLine();
            if (ui.Button("Success", ButtonKind::Secondary, Vec2(0, 0), Icon::CircleCheck))
                ui.Notify({cipher::NoteKind::Success, "Saved", "The theme is stored with your profile.", Icon::None, gState.duration});
            ui.SameLine();
            if (ui.Button("Warning", ButtonKind::Secondary, Vec2(0, 0), Icon::TriangleAlert))
                ui.Notify({cipher::NoteKind::Warning, "Slow frame", "A frame took 21 ms to draw.", Icon::None, gState.duration});
            ui.SameLine();
            if (ui.Button("Error", ButtonKind::Secondary, Vec2(0, 0), Icon::CircleX))
                ui.Notify({cipher::NoteKind::Error, "Device lost", "The renderer restarted the swap chain.", Icon::None, gState.duration});
            ui.Caption("The ring round a notification's close button is the time it has left; the pointer on it holds the count. "
                       "Swipe one to the right to send it away.");
            ui.EndTabItem();
        }
        if (ui.BeginTabItem("Stack", Icon::Layers))
        {
            ui.Group("Stack");
            ui.Row("How long they stay", "Seconds, for the notifications sent from here.");
            ui.SliderFloat("##duration", &gState.duration, 1.5f, 12.0f, "%.1f");
            ui.Row("Send three at once", "They stack; each gives its room back as it leaves.");
            if (ui.Button("Send", ButtonKind::Primary, Vec2(0, 0), Icon::BellRing))
            {
                ui.Notify({cipher::NoteKind::Info, "Download started", "cipher-1.0.zip, 4.2 MB.", Icon::None, gState.duration});
                ui.Notify({cipher::NoteKind::Success, "Download finished", "Saved to the Downloads folder.", Icon::None, gState.duration + 1.0f});
                ui.Notify({cipher::NoteKind::Warning, "Unsigned file", "Check its source before you open it.", Icon::None, gState.duration + 2.0f});
            }
            ui.EndTabItem();
        }
        ui.EndTabBar();
    }

    // The themes as cards: each drawn in its own colors - its canvas, a window of its surface with a switch and a slider
    // in its accent; the current one ringed. A click animates every color of the interface into the card's.
    void ThemeCards(cipher::Ui& ui)
    {
        const auto themes = cipher::Themes();
        const float gap = 12.0f, w = std::floor((ui.ContentWidth() - gap * 2.0f) / 3.0f), h = 124.0f;
        const cipher::Fonts& f = ui.GetFonts();
        const esia::Id base = ui.Ctx().GetId("##theme-cards");
        for (int i = 0; i < (int)themes.size(); ++i)
        {
            if (i % 3)
                ui.SameLine(gap);
            else if (i > 0)
                ui.Spacing(gap);
            ui.PushId(i);
            const bool pressed = ui.InvisibleButton("##card", Vec2(w, h));
            const bool hovered = ui.ItemHovered();
            const Rect r = ui.Ctx().LastItemRect();
            ui.PopId();
            if (pressed)
                ui.SetTheme(i);
            if (hovered)
                ui.Ctx().SetMouseCursor(esia::MouseCursor::Hand);
            const cipher::Theme& t = themes[(std::size_t)i];
            const esia::Id id = esia::HashInt(i, base);
            const float hov = ui.Anim(id, 0, hovered ? 1.0f : 0.0f);
            const float cur = ui.Anim(id, 1, ui.ThemeIndex() == i ? 1.0f : 0.0f, cipher::kBouncy);
            esia::Painter p = ui.Painter();
            p.PushScale(r.Center(), 1.0f + 0.015f * hov);
            p.Rect(r, Style().Radius(cipher::metrics::kRadiusPanel).Fill(t.canvas).Stroke(1.0f / ui.PixelScale(), t.ink.Fade(0.12f), 0.0f)
                          .Shadow(Color::Black(0.28f * hov), 18.0f, Vec2(0.0f, 6.0f)));
            const Rect win(r.min.x + 12.0f, r.min.y + 12.0f, r.max.x - 12.0f, r.min.y + 64.0f);
            p.Rect(win, Style().Radius(cipher::metrics::kRadiusControl).Fill(t.raised).Stroke(1.0f / ui.PixelScale(), t.ink.Fade(0.08f), 0.0f));
            p.Capsule(Rect(win.min.x + 12.0f, win.min.y + 13.0f, win.min.x + 66.0f, win.min.y + 19.0f), Style().Fill(t.ink.Fade(0.55f)));
            p.Capsule(Rect(win.min.x + 12.0f, win.min.y + 27.0f, win.min.x + 96.0f, win.min.y + 31.0f), Style().Fill(t.ink.Fade(0.22f)));
            const Rect sw(win.max.x - 44.0f, win.min.y + 10.0f, win.max.x - 12.0f, win.min.y + 28.0f);
            p.Capsule(sw, Style().Fill(t.accent));
            p.Circle(Vec2(sw.max.x - 9.0f, sw.Center().y), 6.0f, Style().Fill(t.accentInk.Luminance() > 0.5f ? t.accentInk : Color::White()));
            const float sy = win.max.y - 12.0f;
            p.Capsule(Rect(win.min.x + 12.0f, sy - 1.5f, win.max.x - 12.0f, sy + 1.5f), Style().Fill(t.ink.Fade(0.14f)));
            p.Capsule(Rect(win.min.x + 12.0f, sy - 1.5f, win.min.x + 12.0f + (win.Width() - 24.0f) * 0.62f, sy + 1.5f), Style().Fill(t.accent));
            p.Circle(Vec2(win.min.x + 12.0f + (win.Width() - 24.0f) * 0.62f, sy), 5.0f, Style().Fill(t.raised).Stroke(2.0f, t.accent, 0.0f));
            ui.Label(p, r.min.x + 14.0f, r.max.y - 38.0f, {f.textSemibold, 14.5f}, t.ink, t.name);
            ui.Label(p, r.min.x + 14.0f, r.max.y - 18.0f, {f.text, 12.5f}, t.ink.Fade(0.55f), t.note, 0.0f, w - 60.0f);
            p.Circle(Vec2(r.max.x - 32.0f, r.max.y - 24.0f), 7.0f, Style().Fill(t.accent));
            p.Circle(Vec2(r.max.x - 18.0f, r.max.y - 24.0f), 7.0f, Style().Fill(t.accent2));
            if (cur > 0.004f)
                p.Rect(r.Expanded(4.0f * cur), Style().Radius(cipher::metrics::kRadiusPanel + 4.0f * cur).Stroke(2.0f, ui.T().accent.Fade(std::min(cur, 1.0f)), 0.0f));
            p.PopScale();
        }
    }

    void Themes(cipher::Ui& ui)
    {
        if (!ui.BeginTabBar("##themes"))
            return;
        if (ui.BeginTabItem("Themes", Icon::Palette))
        {
            ui.Group("Pick one");
            ThemeCards(ui);
            ui.EndTabItem();
        }
        if (ui.BeginTabItem("Materials", Icon::Layers))
        {
            static const char* backdrops[3] = {"The theme's", "Daylight", "Dusk"};
            ui.Group("Behind the windows");
            ui.Row("Backdrop", "Glass keeps the theme's text legible over any of them.");
            ui.Combo("##backdrop", &gState.backdrop, backdrops, 3);
            static const char* kinds[4] = {"Solid", "Clear glass", "Liquid glass", "Frosted glass"};
            static const char* names[4] = {"Windows", "Popups", "Notifications", "Watermark"};
            static const SurfaceKind surfaces[4] = {SurfaceKind::Window, SurfaceKind::Popup, SurfaceKind::Toast, SurfaceKind::Watermark};
            for (int i = 0; i < 4; ++i)
            {
                ui.PushId(i);
                if (ui.CollapsingHeader(names[i], i == 0))
                {
                    cipher::MaterialSpec& ms = ui.MaterialOf(surfaces[i]);
                    gState.materialKinds[i] = (int)ms.kind;
                    ui.Row("Material", "Each one turns into the next: frost, lensing and light glide.");
                    if (ui.Combo("##kind", &gState.materialKinds[i], kinds, 4))
                        ms.kind = (cipher::Material)gState.materialKinds[i];
                    ui.Row("Opacity");
                    ui.SliderFloat("##opacity", &ms.opacity, 0.0f, 1.0f, "%.2f");
                    ui.Row("Frost", "Units of blur (liquid glass takes a quarter of it, clear glass none).");
                    ui.SliderFloat("##blur", &ms.blur, 0.0f, 48.0f, "%.0f");
                }
                ui.PopId();
            }
            ui.EndTabItem();
        }
        ui.EndTabBar();
    }

    // --bench: from frame 60 on, every frame's UI build (CPU), GPU time (and its categories) and the time since the last
    // frame; their medians and 90th percentiles are printed at the end (with --vsync off the interval is what a frame
    // costs)
    struct Bench
    {
        bool on = false;
        std::vector<float> cpu, gpu, interval;
        std::vector<float> category[(int)esia::rhi::ProfileCategory::Count];
        std::vector<float> startup;   // the first 60 frames' intervals
        std::chrono::steady_clock::time_point launched = std::chrono::steady_clock::now(), last;
        float firstFrame = 0.0f;      // ms from the start to the first frame
    } gBench;

    void BenchFrame(esia::Context& ctx, const glass::SceneInfo& info)
    {
        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        if (ctx.FrameCount() == 1)
            gBench.firstFrame = std::chrono::duration<float, std::milli>(now - gBench.launched).count();
        else if (ctx.FrameCount() <= 60)
            gBench.startup.push_back(std::chrono::duration<float, std::milli>(now - gBench.last).count());
        if (ctx.FrameCount() > 60)
        {
            gBench.interval.push_back(std::chrono::duration<float, std::milli>(now - gBench.last).count());
            gBench.cpu.push_back(info.cpuMs);
            if (info.stats && info.stats->gpu.valid)
            {
                gBench.gpu.push_back(info.stats->gpu.totalMs);
                for (int c = 0; c < (int)esia::rhi::ProfileCategory::Count; ++c)
                    gBench.category[c].push_back(info.stats->gpu.categoryMs[c]);
            }
        }
        gBench.last = now;
    }

    void BenchReport()
    {
        const auto at = [](std::vector<float> v, float q) {
            if (v.empty())
                return 0.0f;
            std::sort(v.begin(), v.end());
            return v[std::min((std::size_t)(q * (float)v.size()), v.size() - 1)];
        };
        std::printf("bench: %zu frames  frame %.3f ms (p90 %.3f)  ui %.3f ms (p90 %.3f)  gpu %.3f ms (p90 %.3f)\n", gBench.interval.size(),
                    at(gBench.interval, 0.5f), at(gBench.interval, 0.9f), at(gBench.cpu, 0.5f), at(gBench.cpu, 0.9f), at(gBench.gpu, 0.5f),
                    at(gBench.gpu, 0.9f));
        float worst = 0.0f, sum = 0.0f;
        for (float v : gBench.startup)
        {
            worst = std::max(worst, v);
            sum += v;
        }
        std::printf("bench startup: first frame %.1f ms, frames 2-60 take %.1f ms (worst %.1f ms)\n", gBench.firstFrame, sum, worst);
        std::printf("bench gpu: capture %.3f  layer %.3f  fx %.3f  glass %.3f  geometry %.3f ms\n", at(gBench.category[0], 0.5f), at(gBench.category[1], 0.5f),
                    at(gBench.category[2], 0.5f), at(gBench.category[3], 0.5f), at(gBench.category[4], 0.5f));
    }

    // ---- the pet: a model read from a .glb (--model, else pet.glb by the program or in the project's assets), shown on
    // the Pet page's turntable and walking along the top of the hub
    struct PetDemo
    {
        std::unique_ptr<cipher::Model> model = std::make_unique<cipher::Model>();
        std::string path;
        bool loaded = false;       // its animations looked up
        int preview = -1;          // the turntable's animation
        std::vector<std::string> names;   // the animations', without the model's prefix
        std::vector<const char*> items;
        cipher::PetSpec spec;
        bool reported = false;     // a failure told once
    } gPet;

    // the model's animations by what they are for: the first whose name holds one of these words
    void PetReady(cipher::Ui& ui)
    {
        cipher::Model& m = *gPet.model;
        gPet.loaded = true;
        gPet.names.clear();
        gPet.items.clear();
        for (int i = 0; i < m.Animations(); ++i)
        {
            std::string n = m.AnimationName(i);
            if (const std::size_t u = n.find('_'); u != std::string::npos && u + 1 < n.size())
                n = n.substr(u + 1);   // "Hero_Idle_Loop" -> "Idle_Loop": the model's own prefix dropped
            gPet.names.push_back(n);
        }
        for (const std::string& n : gPet.names)
            gPet.items.push_back(n.c_str());
        // each word: the animation named exactly that ("Jump", not "WalkJump"), else the first holding it; "=No": exactly
        // that only
        const auto find = [&](std::initializer_list<const char*> parts) {
            for (const char* part : parts)
            {
                const std::string_view word = part[0] == '=' ? part + 1 : part;
                for (std::size_t i = 0; i < gPet.names.size(); ++i)
                    if (gPet.names[i] == word)
                        return (int)i;
                if (part[0] != '=')
                    if (const int i = m.FindAnimation(word); i >= 0)
                        return i;
            }
            return -1;
        };
        gPet.spec.idle = find({"Idle", "idle", "Stand"});
        gPet.spec.walk = find({"Walking", "Walk", "walk"});
        gPet.spec.pat = find({"Reaction", "Wave", "Greet", "Happy"});
        gPet.spec.carried = find({"Pickup", "Carry", "Hang", "=No"});
        gPet.spec.fall = find({"Panic", "Fall", "fall", "=Jump"});
        gPet.spec.jump = find({"Jump", "jump", "Run", "Move"});
        gPet.spec.land = find({"Land", "land", "Move_End"});
        gPet.spec.cheer = find({"Victory", "Cheer", "Dance"});
        gPet.spec.cheerEnd = find({"Victory_End", "Cheer_End"});
        gPet.preview = gPet.spec.idle;
        // a face for every expression in this model: the first one shows
        m.ShowMesh("Face01", false);
        m.ShowMesh("Face02", false);
        (void)ui;
    }

    void Pet(cipher::Ui& ui)
    {
        cipher::Model& m = *gPet.model;
        ui.Group("Model");
        ui.ModelView("##pet-view", m, gPet.preview, 320.0f);
        ui.Row("File", "A glTF binary (.glb): its meshes, textures, skins and animations.");
        ui.InputText("##pet-path", &gPet.path, cipher::InputTextFlags_None, "C:\\path\\to\\model.glb");
        ui.Row("Read it", "On a thread of its own: the hub keeps moving while it loads.");
        if (ui.Button("Load", ButtonKind::Primary, Vec2(0, 0), Icon::Upload) && !gPet.path.empty())
        {
            gPet.loaded = gPet.reported = false;
            m.Load(ui.Ctx().Textures(), gPet.path);
        }
        if (m.GetState() == cipher::Model::State::Ready && !gPet.items.empty())
        {
            ui.Group("Animation");
            ui.Row("On the turntable", "Every animation the file holds: they crossfade on the hub, play here as they are.");
            ui.Combo("##pet-anim", &gPet.preview, gPet.items.data(), (int)gPet.items.size());
        }
        ui.Group("Pet");
        ui.Row("Out and about", "At home on the hub, it walks the windows' tops and the bottom of the display; click it to pat it, drag to throw it.");
        ui.Switch("##pet-on", &gPet.spec.shown);
        ui.Row("Size", "Its height, in units.");
        ui.SliderFloat("##pet-size", &gPet.spec.height, 60.0f, 260.0f, "%.0f");
        ui.Row("Datamosh", "It bursts into little balls that fall and bounce (sweep the pointer through them: wind) - and back together.");
        if (ui.Button(gPet.spec.scattered ? "Restore###mosh" : "Scatter###mosh", gPet.spec.scattered ? ButtonKind::Secondary : ButtonKind::Primary, Vec2(0, 0),
                      gPet.spec.scattered ? Icon::RotateCcw : Icon::Sparkles))
            gPet.spec.scattered = !gPet.spec.scattered;
        ui.Row("Scatter speed", "The grains' clock: below 1 slow motion, above it faster - their flight, bounces and way home.");
        ui.SliderFloat("##pet-mosh-speed", &gPet.spec.scatterSpeed, 0.1f, 3.0f, "%.2fx");
    }

    // the inspector's numbers, held half a second: a number that changes every frame cannot be read
    struct Held
    {
        double at = -1.0;
        float fps = 0.0f, cpu = 0.0f, gpu = 0.0f;
        int draws = 0, shapes = 0, captures = 0;
    } gHeld;

    void Frame(esia::Context& ctx, const glass::SceneInfo& info)
    {
        cipher::Ui& ui = *gUi;
        if (gBench.on)
            BenchFrame(ctx, info);
        RunScript(ctx);
        ui.NewFrame(info.scale);
        if (!gTab.empty())
        {
            ui.SelectHubTab(gTab.c_str());
            gTab.clear();
        }
        // the boot screen first: the app draws nothing of its own until it starts to dissolve
        cipher::BootInfo boot;
        boot.api = info.api;
        boot.adapter = info.adapter;
        boot.width = info.width;
        boot.height = info.height;
        boot.scale = info.scale;
        // the chrome as a user effect on Direct3D 10, 11 and 12; Direct3D 9's 4096 instruction slots are too few for it
        // (~4400) and the other APIs compile none: the CPU's chrome there
        gBoot.SetGpu(std::string(info.api).rfind("Direct3D 1", 0) == 0);
        if (gNoBoot)
            gBoot.Skip();
        const bool booting = gBoot.Frame(ui, boot);
        if (!booting && !gBootDone)
        {
            gBootDone = true;
            ui.Notify({cipher::NoteKind::Success, "Cipher is ready", "Drag the title bar; RShift hides the hub."});
        }
        if (gBoot.Showing())
        {
            ui.EndFrame();
            return;
        }
        if (gState.paused && gState.pauseAt >= 0.0 && ctx.Input().Time() >= gState.pauseAt)
        {
            ui.SetTimeScale(0.0f);
            gState.pauseAt = -1.0;
        }
        DrawBackdrop(ui, ctx.BackgroundDrawList(), Rect(Vec2(0, 0), ctx.DisplaySize()));
        // the Data tab's plots: a new frame time four times a second, new builds every second and a half
        {
            const double now = ctx.Input().Time();
            if (gData.nextFrame == 0.0)   // the first frame: a history to show at once
                for (int i = 0; i < 48; ++i)
                    gData.frames[i] = 4.0f + 6.0f * (0.5f + 0.5f * std::sin((float)i * 0.23f)) + 3.0f * gData.Random();
            if (now >= gData.nextFrame)
            {
                gData.nextFrame = now + 0.25;
                for (int i = 0; i < 47; ++i)
                    gData.frames[i] = gData.frames[i + 1];
                const float wave = 0.5f + 0.5f * std::sin((float)now * 0.9f);
                gData.frames[47] = 4.0f + 6.0f * wave + 3.0f * gData.Random() + (gData.Random() > 0.93f ? 7.0f : 0.0f);
            }
            if (now >= gData.nextBuilds)
            {
                gData.nextBuilds = now + 1.5;
                for (float& b : gData.builds)
                    b = 2.0f + 18.0f * gData.Random();
            }
        }
        gState.progress = std::fmod(gState.progress + ui.DeltaTime() * 0.05f, 1.0f);

        cipher::HubSpec hub;
        // in the room right of the inspector, centered there
        const float left = 18.0f + 244.0f + 18.0f;
        hub.pos = Vec2(left + std::max((ctx.DisplaySize().x - left - hub.size.x) * 0.5f, 0.0f), std::max((ctx.DisplaySize().y - hub.size.y) * 0.5f, 12.0f));
        hub.title = "Cipher";
        hub.version = "v1.0";
        hub.profile.name = "Operator";
        hub.profile.detail = "Administrator";
        hub.profile.initials = "OP";
        if (ui.BeginHub(hub))
        {
            ui.HubSection("Start");
            if (ui.HubTab("Overview", Icon::LayoutGrid, "Glass, motion and a few of everything"))
                Overview(ui);
            ui.HubSection("Controls");
            if (ui.HubTab("Buttons", Icon::MousePointerClick, "Kinds, icons, rows and states"))
                Buttons(ui);
            if (ui.HubTab("Toggles", Icon::ToggleRight, "Switches, check boxes, radio buttons, selectables"))
                Toggles(ui);
            if (ui.HubTab("Sliders", Icon::SlidersHorizontal, "Ruled tracks; dragged, the knob turns into liquid glass"))
                Sliders(ui);
            if (ui.HubTab("Inputs", Icon::TextCursorInput, "Text, passwords, numbers and notes"))
                Inputs(ui);
            if (ui.HubTab("Menus", Icon::Menu, "Combos, context menus, submenus and dialogs"))
                Menus(ui);
            if (ui.HubTab("Structure", Icon::ListTree, "Headers and trees that open with their height"))
                Structure(ui);
            if (ui.HubTab("Data", Icon::Database, "Drags, colors, list boxes, tables and plots"))
                Data(ui);
            ui.HubSection("Overlays");
            if (ui.HubTab("Notifications", Icon::Bell, "Toasts that slide in, count down and swipe away"))
                Notifications(ui);
            ui.HubSection("Style");
            if (ui.HubTab("Themes", Icon::Palette, "Eight themes and the materials of every surface"))
                Themes(ui);
            if (ui.HubTab("Pet", Icon::PawPrint, "A model of your own, on a turntable and walking on the hub"))
                Pet(ui);
        }
        ui.EndHub();
        // the pet: its animations looked up once it is read; a failure told once
        {
            cipher::Model& m = *gPet.model;
            if (m.GetState() == cipher::Model::State::Ready && !gPet.loaded)
                PetReady(ui);
            if (m.GetState() == cipher::Model::State::Failed && !gPet.reported)
            {
                gPet.reported = true;
                ui.Notify({cipher::NoteKind::Error, "The model did not load", m.Error()});
            }
            ui.HubPet(m, gPet.spec);
        }
        if (ui.BeginHubProfileMenu())
        {
            ui.MenuItem("Profile", nullptr, false, true, Icon::User);
            ui.MenuItem("Preferences", "Ctrl+,", false, true, Icon::Settings);
            ui.MenuItem("Shortcuts", "?", false, true, Icon::Keyboard);
            ui.MenuSeparator();
            ui.MenuItem("Lock", "Win+L", false, true, Icon::Lock);
            if (ui.MenuItem("Sign out", nullptr, false, true, Icon::LogOut))
                ui.Notify({cipher::NoteKind::Info, "Signed out", "See you soon, Operator."});
            ui.EndPopup();
        }

        cipher::WindowSpec ws;
        ws.icon = Icon::Activity;
        ws.pos = Vec2(18.0f, ctx.DisplaySize().y - 18.0f - 340.0f);
        ws.size = Vec2(244.0f, 340.0f);
        ws.minSize = Vec2(210.0f, 160.0f);
        ws.open = &gState.inspector;
        if (ui.Begin("Inspector", ws))
        {
            if (ui.BeginMenuBar())
            {
                if (ui.BeginMenu("File"))
                {
                    ui.MenuItem("New window", "Ctrl+N", false, true, Icon::SquarePlus);
                    ui.MenuItem("Open...", "Ctrl+O", false, true, Icon::FolderOpen);
                    if (ui.BeginMenu("Recent"))
                    {
                        ui.MenuItem("scene.cipher");
                        ui.MenuItem("themes.cipher");
                        ui.EndMenu();
                    }
                    ui.MenuSeparator();
                    ui.MenuItem("Close", "Ctrl+W", false, true, Icon::X);
                    ui.EndMenu();
                }
                if (ui.BeginMenu("View"))
                {
                    static bool grid = true, stats = true;
                    ui.MenuItem("Grid", "G", &grid);
                    ui.MenuItem("Statistics", "S", &stats);
                    ui.EndMenu();
                }
                if (ui.BeginMenu("Help"))
                {
                    ui.MenuItem("About Cipher", nullptr, false, true, Icon::Info);
                    ui.EndMenu();
                }
                ui.EndMenuBar();
            }
            const esia::render::RenderStats* rs = info.stats;
            const double now = ctx.Input().Time();
            if (gHeld.at < 0.0 || now - gHeld.at >= 0.5)
            {
                gHeld.at = now;
                gHeld.fps = info.fps;
                gHeld.cpu = info.cpuMs;
                gHeld.gpu = rs && rs->gpu.valid ? rs->gpu.totalMs : 0.0f;
                gHeld.draws = rs ? rs->drawCalls : 0;
                gHeld.shapes = rs ? rs->fxInstances : 0;
                gHeld.captures = rs ? rs->backdropCaptures : 0;
            }
            ui.LabelText("API", "%s", info.api);
            ui.LabelText("Frame", "%.0f fps", (double)gHeld.fps);
            ui.LabelText("CPU", "%.2f ms", (double)gHeld.cpu);
            ui.LabelText("GPU", "%.2f ms", (double)gHeld.gpu);
            ui.LabelText("Draws", "%d", gHeld.draws);
            ui.LabelText("Shapes", "%d", gHeld.shapes);
            ui.LabelText("Captures", "%d", gHeld.captures);
            ui.Spacing();
            ui.ProgressBar(-1.0f);
        }
        ui.End();

        cipher::WatermarkInfo wm;
        wm.api = info.api;
        wm.fps = info.fps;
        wm.cpuMs = info.cpuMs;
        ui.Watermark(wm);
        ui.EndFrame();
    }
}

int main(int argc, char** argv)
{
    glass::App app;
    app.name = "cipher";
    app.loadFonts = false;
    app.option = [](const std::string& option, const char* value, bool& usedValue) {
        if (option == "--no-boot")
        {
            gNoBoot = true;
            return true;
        }
        if (option == "--bench")
        {
            gBench.on = true;
            return true;
        }
        if (option == "--model" && value)
        {
            gPet.path = value;
            usedValue = true;
            return true;
        }
        if (option == "--backdrop" && value)
        {
            gState.backdrop = std::atoi(value);
            usedValue = true;
            return true;
        }
        if ((option == "--script" || option == "--tab") && value)
        {
            if (option == "--script")
                ParseScript(value);
            else
                gTab = value;
            usedValue = true;
            return true;
        }
        return false;
    };
    // the pet's model: --model, else pet.glb next to the program, else the project's assets/pet.glb (a model of your own:
    // not in the repository), else the robot that comes with it (RobotExpressive, CC0)
    {
        const std::filesystem::path exe = argc > 0 ? std::filesystem::absolute(argv[0]).parent_path() : std::filesystem::path();
        const std::filesystem::path assets = exe / ".." / ".." / "assets";
        for (const std::filesystem::path& p : {exe / "pet.glb", assets / "pet.glb", assets / "RobotExpressive" / "RobotExpressive.glb"})
        {
            std::error_code ec;
            if (gPet.path.empty() && std::filesystem::exists(p, ec))
                gPet.path = std::filesystem::weakly_canonical(p, ec).string();
        }
    }
    app.init = [](esia::Context& ctx, esia::text::TextSystem* text, esia::text::FontId, const std::vector<std::string>&) {
        if (!text)
            return;
        gUi = std::make_unique<cipher::Ui>(ctx, text, cipher::LoadFonts(*text));
        if (!gPet.path.empty())
            gPet.model->Load(ctx.Textures(), gPet.path);
    };
    app.renderer = [](esia::render::Renderer& r) { r.SetEffectSource(cipher::BootScreen::kEffect, "cipher-chrome", cipher::BootScreen::EffectSource()); };
    app.frame = [](esia::Context& ctx, const glass::SceneInfo& info) {
        if (gUi)
            Frame(ctx, info);
    };
    app.shutdown = [] {
        if (gBench.on)
            BenchReport();
        gPet.model.reset();   // its texture goes back to the Context's registry while there is one
    };
    return glass::RunApp(argc, argv, app);
}
