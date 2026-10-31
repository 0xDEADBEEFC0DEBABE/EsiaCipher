// Cipher - a flat, animated immediate-mode UI library drawn with Esia's render class (esia::Painter) on Esia's core
// (esia::Context): Dear ImGui's widget set, a hub window (title bar, a sidebar of tabs, rows of elements) and floating
// windows, in a flat look of its own - hairlines, soft glows, one accent per theme. Every
// change on screen is a spring (anim.hpp), so any frame may interrupt, reverse or replay it; themes animate into each
// other color by color; surfaces take solid, clear, liquid or frosted glass; notifications slide in and out; a
// watermark shows the frame's numbers.
//
//   cipher::Ui ui(ctx, text, cipher::LoadFonts(*text));
//   // every frame, between Context::NewFrame and Context::EndFrame:
//   ui.NewFrame(pixelScale);   // the display's pixels per UI unit
//   static bool open = true, wifi = true;
//   if (ui.Begin("Settings", {.icon = cipher::Icon::Settings, .open = &open}))
//   {
//       ui.Switch("Wi-Fi", &wifi);
//       if (ui.Button("Save", cipher::ButtonKind::Primary))
//           ui.Notify({.kind = cipher::NoteKind::Success, .title = "Saved"});
//   }
//   ui.End();
//   ui.EndFrame();
//
// Labels follow Dear ImGui's conventions: "Save##toolbar" shows "Save", "Save###id" keeps its id whatever it shows.
//
// Geometry: one 6-unit step between a layer and what it holds, corner radii concentric from layer to
// layer - windows 24, panels and popups 18, controls 12, what lies in a control 6, the smallest 3; a title bar of 48
// (36-unit buttons), tabs of 36, element rows of 42 holding 30-unit controls; buttons 36 tall; text 15.5 (secondary
// 13.5); icons 18; the labels of a window share one column; text keeps two steps from the edge of its surface.
#pragma once
#include "anim.hpp"
#include "icons.hpp"
#include "theme.hpp"
#include "esia/core/context.hpp"
#include "esia/render/painter.hpp"
#include "esia/text/text.hpp"
#include <cfloat>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace esia::render
{
    struct RenderStats;
}

namespace cipher
{
    using esia::Id;
    using esia::Rect;
    namespace text = esia::text;

    namespace metrics
    {
        inline constexpr float kStep = 6.0f;
        inline constexpr float kRow = 36.0f;          // a button, a tab
        inline constexpr float kRowSmall = 28.0f;     // a small button, a menu row, a chip
        inline constexpr float kElement = 42.0f;      // an element row of the hub
        inline constexpr float kControl = 30.0f;      // the control in an element row
        inline constexpr float kCheckRow = 30.0f;     // a check box or radio button and its label
        inline constexpr float kPad = 12.0f;          // a window's content from its edge
        inline constexpr float kRadiusWindow = 24.0f, kRadiusPanel = 18.0f, kRadiusControl = 12.0f, kRadiusInner = 6.0f, kRadiusTiny = 3.0f;
        inline constexpr float kFont = 15.5f, kFontSmall = 13.5f, kFontCaption = 12.5f, kFontTitle = 16.5f, kFontHeading = 23.0f,
                               kFontHero = 32.0f, kFontMono = 14.0f;
        inline constexpr float kIcon = 18.0f;
        inline constexpr float kTitleBar = 48.0f;
    }

    // Segoe UI Variable's named instances (its Text optical size for UI text, Display for headings, Small below 12 px)
    // and Cascadia Mono; the platform's UI family elsewhere. Chinese, Japanese, Korean, symbols and emoji from the
    // platform's fallback chain.
    struct Fonts
    {
        text::FontId text = 0, textSemibold = 0, textBold = 0;
        text::FontId display = 0, displaySemibold = 0, displayBold = 0;
        text::FontId small = 0, smallSemibold = 0;
        text::FontId mono = 0, monoSemibold = 0;
    };
    Fonts LoadFonts(text::TextSystem& ts);

    // What a surface is made of. Glass needs something behind it to bend: the demo's backdrop (Ui::Backdrop).
    enum class Material : std::uint8_t
    {
        Solid,     // the theme's color, `opacity` of it
        Clear,     // transparent glass: no frost, strong lensing at the rim, dispersion
        Liquid,    // liquid glass: a light frost, lensing, a lit rim
        Frosted,   // frosted glass: a heavy frost, a soft rim
    };
    struct MaterialSpec
    {
        Material kind = Material::Solid;
        float opacity = 1.0f;   // of the surface's color over what is behind (glass: its tint)
        float blur = 18.0f;     // UI units of frost (Liquid takes a quarter of it, Clear none)
    };
    enum class SurfaceKind : std::uint8_t { Window, Popup, Sidebar, Composer, Toast, Watermark, Card, Count };

    enum class ButtonKind : std::uint8_t
    {
        Primary,     // filled with the accent
        Secondary,   // a light fill of the text color
        Ghost,       // nothing until hovered
        Danger,      // the danger color
        Outline,     // a hairline
    };

    // Text colors: the theme's ink at its levels, or a status color.
    enum class Tone : std::uint8_t { Primary, Secondary, Tertiary, Disabled, Accent, Success, Warning, Danger, Info };

    enum WindowFlags_ : std::uint32_t
    {
        WindowFlags_None = 0,
        WindowFlags_NoResize = 1u << 0,
        WindowFlags_NoMove = 1u << 1,
        WindowFlags_NoCollapse = 1u << 2,
        WindowFlags_NoScroll = 1u << 3,
    };

    struct WindowSpec
    {
        Icon icon = Icon::None;
        Vec2 pos{-1.0f, -1.0f};        // where it first appears (< 0: cascaded from the top left)
        Vec2 size{420.0f, 540.0f};     // its first size
        Vec2 minSize{300.0f, 200.0f};
        bool* open = nullptr;          // a close button: false closes it (it fades out first)
        std::uint32_t flags = WindowFlags_None;
    };

    // Who is signed in: an avatar of initials (a status dot on its edge) in the hub's title bar, left of the theme
    // button - a click on it opens its menu (BeginHubProfileMenu) - and the name in the corner tile of the hub's footer.
    struct HubProfile
    {
        const char* name = nullptr;      // null: no card
        const char* detail = nullptr;    // under the name: a role, an address
        const char* initials = nullptr;  // on the avatar (null: the name's first letter)
        bool online = true;
    };

    // The hub: one window of tabs - a title bar (the mark in its corner, title, subtitle, badge, the profile's avatar,
    // window buttons), a sidebar of tabs in sections, the selected tab's content (rows of elements, or any widget) and,
    // with a profile or a version, a footer whose corner tile mirrors the mark's. It moves, resizes, minimizes into a
    // pill (`toggleKey` brings it back) and maximizes to the display.
    struct HubSpec
    {
        const char* title = "Cipher";
        const char* subtitle = nullptr;
        const char* badge = nullptr;
        Vec2 pos{-1.0f, -1.0f};        // where it first appears (< 0: centered on the display)
        Vec2 size{980.0f, 700.0f};
        Vec2 minSize{680.0f, 440.0f};
        float sidebar = 228.0f;
        esia::Key toggleKey = esia::Key::RightShift;
        HubProfile profile;
        const char* version = nullptr;   // in the footer's corner tile, after the profile's name
    };

    enum InputTextFlags_ : std::uint32_t
    {
        InputTextFlags_None = 0,
        InputTextFlags_Password = 1u << 0,          // bullets for the characters
        InputTextFlags_ReadOnly = 1u << 1,
        InputTextFlags_EnterReturnsTrue = 1u << 2,  // true when Enter is pressed (else on every change)
        InputTextFlags_CharsDecimal = 1u << 3,      // digits, . - + e only
    };

    enum TableFlags_ : std::uint32_t
    {
        TableFlags_None = 0,
        TableFlags_Sortable = 1u << 0,    // a click on a header sorts by its column (TableSortColumn), another reverses it
        TableFlags_Resizable = 1u << 1,   // the borders between the headers drag
        TableFlags_RowBg = 1u << 2,       // every other row a shade darker
    };

    enum class NoteKind : std::uint8_t { Info, Success, Warning, Error };
    struct Notification
    {
        NoteKind kind = NoteKind::Info;
        std::string title;
        std::string body;
        Icon icon = Icon::None;        // None: the kind's
        float duration = 4.5f;         // seconds; the pointer over it holds the countdown
    };

    // The watermark: a pill in a corner of the display with the frame's numbers. Drag it: it snaps to the nearest
    // corner.
    struct WatermarkInfo
    {
        const char* api = "";
        float fps = 0.0f;
        float cpuMs = 0.0f;
        float gpuMs = 0.0f;
    };

    // What an overlay's drawing costs - a UI drawn over another program's frame, a game's - and only that: on the CPU,
    // its UI and the rest of what it draws through Esia (BeginUi .. EndUi, around Context::NewFrame .. EndFrame) and
    // Esia's Renderer::Render (BeginRender .. EndRender); on the GPU, Esia's own commands (EndRender's RenderStats: the
    // times RenderParams::profile asks for, from timestamps around Esia's work - not the game's frame, not its
    // present). A frame loop that times these itself (glass_app's SceneInfo) gives them to Add instead. Held(): the
    // averages over half a second and its slowest frame, kept for the next half second (a number that changes every
    // frame cannot be read).
    class FrameCost
    {
    public:
        void BeginUi();
        void EndUi();
        void BeginRender();
        void EndRender(const esia::render::RenderStats& stats);
        void Add(float uiMs, float renderMs, const esia::render::RenderStats* stats);

        struct Numbers
        {
            float uiMs = 0.0f;       // its UI and the rest of its drawing, CPU
            float renderMs = 0.0f;   // Renderer::Render, CPU
            float cpuMs = 0.0f;      // both
            float gpuMs = 0.0f;      // Esia's commands, GPU
            float worstCpuMs = 0.0f, worstGpuMs = 0.0f;   // the slowest frame's
            int draws = 0;           // the last frame's draw calls
            int frames = 0;          // the frames measured (0: none yet)
            bool gpu = false;        // the device gave GPU times
        };
        const Numbers& Held() const { return held_; }

    private:
        using Clock = std::chrono::steady_clock;
        Clock::time_point uiStart_, renderStart_, since_;
        float uiMs_ = 0.0f;
        double ui_ = 0.0, render_ = 0.0, gpu_ = 0.0;
        float worstCpu_ = 0.0f, worstGpu_ = 0.0f;
        int frames_ = 0, gpuFrames_ = 0, draws_ = 0;
        Numbers held_;
    };

    class Model;   // model.hpp: a glTF model, read on a thread of its own, posed and drawn on the CPU

    // The pet: a model at home on the hub. It walks along the tops of the hub and the windows and along the bottom of the
    // display, steps off their ends and falls onto what is below, jumps up onto the ones above; it can be patted (a
    // click), picked up and thrown (a drag). Its animations by their index in the model (-1: none): standing about,
    // walking (in place: the pet moves it, as fast as its feet go), patted, carried, falling, jumping, landing,
    // cheering (the footer's secret: `cheer`, then `cheerEnd`).
    struct PetSpec
    {
        bool shown = true;
        float height = 88.0f;   // its rest pose, UI units
        int idle = -1, walk = -1, pat = -1, carried = -1, fall = -1, jump = -1, land = -1, cheer = -1, cheerEnd = -1;
        // It bursts into little balls of its colors that fall, bounce and settle on the windows and the display's
        // bottom (the pointer's sweep blows them about); false again: they fly home and it is whole.
        bool scattered = false;
        float scatterSpeed = 1.0f;   // the grains' clock: 0.5 slow motion at half speed, 2 twice as fast
    };

    class Ui
    {
    public:
        Ui(esia::Context& ctx, text::TextSystem* text, const Fonts& fonts);

        // ---- frame
        // NewFrame advances the text system (glass_app's App::loadFonts = false) and the animation clock.
        void NewFrame(float pixelScale);
        // Draws what lies over everything: notifications, the watermark, the tooltip.
        void EndFrame();
        double Time() const { return time_; }       // the animation clock (seconds)
        float DeltaTime() const { return dt_; }     // this frame's step of it
        // 1 real time, 0.25 slow motion, 0 paused (StepFrame then advances one 60th of a second)
        void SetTimeScale(float s) { timeScale_ = s < 0.0f ? 0.0f : s; }
        float TimeScale() const { return timeScale_; }
        void StepFrame() { step_ = true; }

        // ---- theme and materials
        void SetTheme(int index, bool animate = true);
        int ThemeIndex() const { return themeIndex_; }
        const Theme& T() const { return shown_; }   // the theme as shown (on its way to the chosen one)
        MaterialSpec& MaterialOf(SurfaceKind s) { return materials_[(int)s]; }
        Color Ink(float level) const { return shown_.ink.Fade(level); }
        Color ToneColor(Tone t) const;

        // ---- windows
        // A floating window: drag its title bar, resize it from any edge or its corner, collapse it with a double click
        // on the title bar or its chevron. False while it is collapsed or closed; End either way.
        bool Begin(const char* title, const WindowSpec& spec = {});
        void End();
        // A scroll region (thin scroll bar, faded edges), `size` as Context's (0: what is left).
        bool BeginChild(const char* id, Vec2 size = Vec2(0, 0), bool border = false, Vec2 padding = Vec2(0, 0));
        void EndChild();

        // ---- the hub
        // False while it is minimized; EndHub either way. Tabs are true while selected: their content follows.
        bool BeginHub(const HubSpec& spec);
        void HubSection(const char* label);
        bool HubTab(const char* label, Icon icon, const char* note = nullptr);
        void EndHub();
        bool HubMinimized() const { return hubMinimized_; }
        void SelectHubTab(const char* label);
        // the profile's menu, opened by a click on the avatar: its items, then EndPopup (after EndHub)
        bool BeginHubProfileMenu();
        // A model on a turntable: a card spanning the content, the model on a disc in it playing `anim` (-1: its rest
        // pose); a drag turns it. It shows the model loading, failing, or not there yet.
        void ModelView(const char* id, Model& model, int anim, float height = 300.0f);
        // The pet (after EndHub: the hub, the windows of the frame before and the display are where it goes). HubPats:
        // how many times it was patted.
        void HubPet(Model& model, const PetSpec& spec);
        int HubPats() const { return hubPats_; }
        // An element row: `title` (and `desc` under it) on the left, the next widget's control on the right - a
        // switch, a slider, a field, a button. Rows of one group share hairlines.
        void Row(const char* title, const char* desc = nullptr);
        // A group's caption: monospaced, upper case, a hairline after it.
        void Group(const char* caption);

        // ---- layout
        void SameLine(float spacing = -1.0f);
        void NewLine();
        void Spacing(float height = metrics::kStep);
        void Dummy(Vec2 size);
        void Indent(float width = 0.0f);
        void Unindent(float width = 0.0f);
        void BeginGroup();
        void EndGroup();
        void Separator();
        void SeparatorText(const char* label);
        void SetNextItemWidth(float width) { nextWidth_ = width; }
        void PushId(const char* id);
        void PushId(int id);
        void PopId();
        void BeginDisabled(bool disabled = true);
        void EndDisabled();
        float ContentWidth() const;

        // ---- text
        void Text(const char* fmt, ...);
        void TextColored(Color c, const char* fmt, ...);
        void TextDisabled(const char* fmt, ...);
        void TextWrapped(const char* fmt, ...);
        void Caption(const char* fmt, ...);
        void Heading(const char* text);
        void LabelText(const char* label, const char* fmt, ...);
        // A FrameCost's numbers as rows (LabelText's: they roll to new values twice a second) - Build (its UI and the
        // rest of its drawing, CPU), Render (Renderer::Render, CPU), GPU (Esia's commands), the slowest frame's CPU and
        // GPU, draw calls.
        void FrameCostRows(const FrameCost& cost);
        void BulletText(const char* fmt, ...);
        void Bullet();
        // `s` as it is (no formatting), wrapped at the content width when `wrap`
        void TextUnformatted(std::string_view s, Tone tone = Tone::Primary, float size = metrics::kFont, bool wrap = false, text::FontId font = 0);

        // ---- buttons and toggles
        bool Button(const char* label, ButtonKind kind = ButtonKind::Secondary, Vec2 size = Vec2(0, 0), Icon icon = Icon::None);
        bool SmallButton(const char* label, ButtonKind kind = ButtonKind::Secondary);
        bool IconButton(const char* id, Icon icon, const char* tooltip = nullptr, float size = metrics::kRow, ButtonKind kind = ButtonKind::Ghost);
        bool InvisibleButton(const char* id, Vec2 size);
        bool Checkbox(const char* label, bool* v);
        bool CheckboxFlags(const char* label, unsigned* flags, unsigned value);
        bool RadioButton(const char* label, bool active);
        bool RadioButton(const char* label, int* v, int value);
        bool Switch(const char* label, bool* v);
        bool Selectable(const char* label, bool selected = false, Vec2 size = Vec2(0, 0), Icon icon = Icon::None);
        // fraction < 0: indeterminate
        void ProgressBar(float fraction, Vec2 size = Vec2(0, 0), const char* overlay = nullptr);
        void Spinner(float size = 18.0f, Color color = Color::Clear());

        // ---- sliders
        bool SliderFloat(const char* label, float* v, float min, float max, const char* format = "%.2f");
        bool SliderInt(const char* label, int* v, int min, int max, const char* format = "%d");

        // ---- text fields
        bool InputText(const char* label, std::string* text, std::uint32_t flags = InputTextFlags_None, const char* hint = nullptr);
        bool InputTextWithHint(const char* label, const char* hint, std::string* text, std::uint32_t flags = InputTextFlags_None)
        {
            return InputText(label, text, flags, hint);
        }
        // several lines (Enter breaks the line), `size` as Context's (0: the width left, 6 lines)
        bool InputTextMultiline(const char* label, std::string* text, Vec2 size = Vec2(0, 0), std::uint32_t flags = InputTextFlags_None);
        // a number: typed in, or stepped with the - and + buttons beside it (`step` 0: none)
        bool InputFloat(const char* label, float* v, float step = 0.0f, const char* format = "%.3f");
        bool InputInt(const char* label, int* v, int step = 1);

        // ---- drags: a number dragged sideways (Shift ten times faster, Alt ten times slower; with a range, its share
        // fills the field behind it), typed after a double click or Enter
        bool DragFloat(const char* label, float* v, float speed = 0.01f, float min = 0.0f, float max = 0.0f, const char* format = "%.3f");
        bool DragInt(const char* label, int* v, float speed = 0.2f, int min = 0, int max = 0);
        // a vertical slider: its track `size` (0: 40 x 160), its value under it; dragged, its knob turns into a drop of glass
        bool VSliderFloat(const char* label, Vec2 size, float* v, float min, float max, const char* format = "%.2f");

        // ---- colors (straight RGBA, 0..1): a swatch; a swatch and its hex code (the swatch opens the picker); the
        // picker - the saturation / value square, the hue and alpha bars, their knobs gliding where they are sent
        bool ColorButton(const char* id, Color c, Vec2 size = Vec2(0, 0));
        bool ColorEdit3(const char* label, float col[3]);
        bool ColorEdit4(const char* label, float col[4]);
        bool ColorPicker3(const char* label, float col[3]);
        bool ColorPicker4(const char* label, float col[4]);

        // ---- list boxes: a framed scroll region of selectables
        bool BeginListBox(const char* label, Vec2 size = Vec2(0, 0));
        void EndListBox();
        bool ListBox(const char* label, int* current, const char* const* items, int count, int heightInItems = 5);

        // ---- tables: columns by weight (resizable), a header row that sorts, rows that light under the pointer. A row
        // given a key glides to its new place when the rows are sorted. Cells take text (Text, TextColored ...).
        bool BeginTable(const char* id, int columns, std::uint32_t flags = TableFlags_None);
        void TableSetupColumn(const char* label, float weight = 1.0f);
        void TableHeadersRow();
        void TableNextRow(int key = -1);
        bool TableNextColumn();
        void EndTable();
        // the column the table is sorted by (-1: none) and its direction; true the frame a click changed them
        int TableSortColumn();
        bool TableSortAscending();
        bool TableSortChanged();

        // ---- plots: values as a line over a filled area, or as bars, gliding to new values; the pointer reads them
        void PlotLines(const char* label, const float* values, int count, float min = FLT_MAX, float max = FLT_MAX, Vec2 size = Vec2(0, 0));
        void PlotHistogram(const char* label, const float* values, int count, float min = FLT_MAX, float max = FLT_MAX, Vec2 size = Vec2(0, 0));

        // ---- tab bars: a track of tabs, a pill sliding to the selected one; the content of the tab left fades out as the
        // new one's comes in from the side it moved to. BeginTabItem is true for the selected tab (and, inert, for those
        // fading out): their content, then EndTabItem.
        bool BeginTabBar(const char* id);
        bool BeginTabItem(const char* label, Icon icon = Icon::None);
        void EndTabItem();
        void EndTabBar();

        // ---- headers and trees: what they hold opens and closes with its height, never at once
        // The content after it, up to the next header, group or the end of its region.
        bool CollapsingHeader(const char* label, bool defaultOpen = false);
        // ... or up to EndCollapse.
        bool BeginCollapse(const char* label, bool defaultOpen = false);
        void EndCollapse();
        bool TreeNode(const char* label, bool defaultOpen = false, Icon icon = Icon::None);
        void TreePop();

        // ---- combos and menus (popups fade and grow out of what opened them, and fade out when they close)
        bool BeginCombo(const char* label, const char* preview);
        void EndCombo();
        bool Combo(const char* label, int* current, const char* const* items, int count);
        // the menu bar of a floating window (right under its title bar), its menus and their items
        bool BeginMenuBar();
        void EndMenuBar();
        bool BeginMenu(const char* label, bool enabled = true);
        void EndMenu();
        bool MenuItem(const char* label, const char* shortcut = nullptr, bool selected = false, bool enabled = true, Icon icon = Icon::None);
        bool MenuItem(const char* label, const char* shortcut, bool* selected, bool enabled = true);
        void MenuSeparator();

        // ---- popups
        void OpenPopup(const char* id);
        bool BeginPopup(const char* id);
        void EndPopup();
        void CloseCurrentPopup();
        // opened by a right click on the last item
        bool BeginPopupContextItem(const char* id);
        // a dialog over a frosted scrim: Escape or its close button closes it (when `open`), a click beside it shakes it
        void OpenModal(const char* name);
        bool BeginPopupModal(const char* name, bool* open = nullptr, float width = 420.0f);
        void EndPopupModal();
        void CloseModal(const char* name);

        // ---- tooltips (the last item, after a short hover)
        void SetItemTooltip(const char* fmt, ...);
        bool ItemHovered() const { return lastHovered_; }

        // ---- over everything
        void Notify(const Notification& n);
        void Watermark(const WatermarkInfo& info) { watermark_ = info; watermarkOn_ = true; }

        // ---- for custom widgets (the demo's shell)
        esia::Context& Ctx() { return ctx_; }
        text::TextSystem* TextSystem() const { return text_; }
        const Fonts& GetFonts() const { return fonts_; }
        esia::PainterEnv Env() const;
        esia::Painter Painter();                        // on the current window's draw list
        esia::Painter Painter(esia::DrawList& dl);
        float PixelScale() const { return pixelScale_; }
        Vec2 Measure(text::FontRef f, std::string_view s, float wrap = 0.0f) const;
        // `s` with its capitals centered on `cy`: from `x` (align 0), centered on it (0.5) or ending at it (1); trimmed
        // with an ellipsis past `maxWidth`; `dy` moves it after its baseline is put on a pixel row (text in motion)
        void Label(esia::Painter& p, float x, float cy, text::FontRef f, Color c, std::string_view s, float align = 0.0f, float maxWidth = 0.0f,
                   float dy = 0.0f) const;
        // ... and when the text of `id` changes, the old one rises out as the new one comes up into its place (a change
        // undone halfway turns back). Monospaced texts of one length change by columns: only the digits that change roll.
        void MorphLabel(esia::Painter& p, Id id, float x, float cy, text::FontRef f, Color c, std::string_view s, float align = 0.0f, float maxWidth = 0.0f);
        // springs per item: created at `target` (or `from`), stepped once a frame
        float Anim(Id id, int slot, float target, const Motion& m = kSnappy);
        float AnimFrom(Id id, int slot, float from, float target, const Motion& m);
        float AnimVelocity(Id id, int slot);            // the spring's speed (units per second)
        Color AnimColor(Id id, int slot, Color target, const Motion& m = kSnappy);
        // a surface of `kind`'s material and the theme's `base` color; `shadow` under it
        void Surface(esia::Painter& p, const Rect& r, float radius, SurfaceKind kind, Color base, float shadow = 0.0f);
        esia::GlassMaterial GlassOf(const MaterialSpec& m, Color tint) const;
        // The display's backdrop: the canvas and slow light blobs in the theme's colors (what glass bends)
        void Backdrop(esia::DrawList& dl, const Rect& r, float intensity = 1.0f, float alpha = 1.0f);
        std::string_view Visible(std::string_view label) const;   // up to "##"
        // `s` with `spacing` UI units between its letters (upper case when `caps`): captions, the title; returns its width
        float SpacedLabel(esia::Painter& p, float x, float cy, text::FontRef f, Color c, std::string_view s, float spacing, bool caps = true) const;
        // The width to wrap `s` at so its lines come out even: the narrowest that keeps the line count `width` gives it
        // (never one word alone on the last line). Cached per text, font and width.
        float BalancedWidth(text::FontRef f, std::string_view s, float width);

    private:
        struct WinFrame
        {
            Id id = 0;
            bool begun = false;       // Context::Begin was called (End calls Context::End)
            bool body = false;        // the body child is open
            float alpha = 1.0f;
            // its edge fade: a region's content fades out where it is scrolled under its edges (a fade nested in it
            // replaces it in the draw list until it ends: it is begun again then)
            Rect fadeView;
            float fadeTop = 0.0f, fadeBottom = 0.0f;
            bool popup = false;       // a popup (EndPopup), or its ghost while it fades out (Context::End)
            bool ghost = false;
            bool modal = false;
            bool combo = false;       // a combo's list: a pick does not close it
            bool drop = false;        // a drop-down rolling out of its control: what is in it clipped to the surface so far
            bool resizable = false;   // a window its corner resizes
            // the radius of its scroll region's corners at the top and at the bottom: the scroll bar keeps clear of them
            float roundTop = 0.0f, roundBottom = 0.0f;
        };
        // a region that opens and closes with its height (BeginCollapse, CollapsingHeader, TreeNode): its content laid
        // out whole in a container, shown down to `open` of its height (last frame's), what follows it moved up as much
        struct Fold
        {
            Id id = 0;
            Vec2 start;               // where its content starts
            float open = 1.0f;        // how far it is open (the spring)
            bool implicit = false;    // CollapsingHeader's: ends at the next header, group or region end
            bool clipped = false;     // opening or closing: clipped to what is shown
            bool tree = false;        // TreeNode's: indented, a guide along its children
            float indent = 0.0f;
            std::size_t depth = 0;    // the region it is in (windows_.size())
        };
        struct HubTabEntry
        {
            std::string label;
            Icon icon = Icon::None;
            std::string note;
            Id id = 0;                // 0: a section caption
        };
        // the tab bar being submitted: its row, its tabs so far, where the content of its tabs starts
        // the table being submitted
        struct TableFrame
        {
            Id id = 0;
            std::uint32_t flags = 0;
            int columns = 0;
            std::vector<std::string> labels;
            std::vector<float> weights;
            std::vector<float> edges;   // the columns' borders, from the table's left (columns + 1)
            Rect area;                  // its left and right edges, its top
            float y = 0.0f;             // the next row's top, from the table's top
            float rowH = 34.0f;
            int row = -1, column = -1;
            Rect rowRect;
            bool cellClip = false;
            float lineX = 0.0f;
        };
        struct TabBarEntry
        {
            Id id = 0;
            std::string label;
            Icon icon = Icon::None;
            Rect rect;
            float hover = 0.0f, press = 0.0f;
        };
        struct TabBarFrame
        {
            Id id = 0;
            Rect bar;
            float x = 0.0f;           // where the next tab goes
            int count = 0;
            Vec2 start;               // the content's top left
            float width = 0.0f;
            float tallest = 0.0f;     // the tallest content this frame
            float selectedHeight = -1.0f;
            Id selected = 0;
            int selectedIndex = 0;
            bool itemOpen = false, itemInert = false, prevInert = false;
            std::size_t folds = 0, itemFolds = 0;
            std::vector<TabBarEntry> tabs;
        };
        // The element row the next widget fills (Row): its rect, the control's place in it
        struct PendingRow
        {
            bool on = false;
            Rect row, control;
        };
        struct Toast
        {
            Notification n;
            int serial = 0;
            float age = 0.0f;
            Spring x, y, shown;       // slide (0 in place, 1 off the display), place in the stack, fade
            float height = 0.0f;
            bool closing = false, placed = false;
            bool dragging = false;    // swiped by the pointer
            float grab = 0.0f;
        };

        // items
        Rect ItemRect(Vec2 size);                          // at the cursor, laid out (ItemSize)
        // The control rect of the pending element row (Row) for a control `width` wide (<= 0: as wide as it allows),
        // else false
        bool TakeRow(float width, Rect& control);
        void CloseHubContent();
        void DrawHubSidebar();
        Rect DrawHubAvatar(esia::Painter& p, float right, float cy);
        void DrawHubFooter(esia::Painter& p, const Rect& r, float grow);
        void DrawHubWaves(esia::DrawList& dl, const Rect& r, float top, float bottom, float alpha);
        void SetHubSelection(Id id);
        void OpenHubContent(Id id, std::string_view title, const char* note, float shown, float offset, bool ghost);
        void BeginFold(Id id, float open, bool implicit, bool tree);
        void EndFold();
        void EndImplicitFolds();                 // a header's sibling ends where the next header or group starts
        void EndFoldsOf(std::size_t depth);      // a region ends: the folds in it
        // a header's or tree node's toggle: its state, the spring of it; false once it is closed and at rest
        bool FoldToggle(Id id, bool defaultOpen, bool pressed, float& open);
        bool HeaderItem(const char* label, bool defaultOpen, bool tree, Icon icon, Id& id, float& open);
        // items of a tab's content fading out (inert_): drawn as they were, taking no input
        bool AddItem(Id id, const Rect& r, std::uint32_t flags = 0);
        esia::ButtonResult Behavior(Id id, const Rect& r, std::uint32_t flags = 0);
        void SnapMorph(Id id, std::string_view s);   // MorphLabel's text, without an animation
        void SelectAllText(Id id);                   // the editor of `id`: all its text selected (a drag opening its editor)
        void DrawSwatch(esia::Painter& p, const Rect& r, float radius, Color c, float glow = 0.0f);
        bool ColorEditEx(const char* label, float* col, bool alpha);
        bool ColorPickerEx(const char* label, float* col, bool alpha);
        void TableColumns(TableFrame& t);
        void TableEndCell();
        // an id for an item without one (a progress bar, a disabled region): the n-th of `scope` in the current id scope
        // this frame - stable while the items before it in that scope are
        Id SeqId(const char* scope);
        // the material of a surface on its way to the one asked for (its kind, opacity and blur are springs)
        esia::GlassMaterial MaterialNow(SurfaceKind kind, Color tint, float& glassness, float& opacity) const;
        float Dark() const { return Clamp01(dark_.value); }   // 1 dark theme, 0 light, between while it changes
        float LabelColumn() const;
        Id IdOf(std::string_view label) const { return ctx_.GetId(label); }
        void FocusRing(esia::Painter& p, const Rect& r, float radius, Id id);
        Color ButtonFill(ButtonKind kind, float hover, float press) const;
        Color ButtonInk(ButtonKind kind) const;
        void FormatV(const char* fmt, va_list args);
        void DrawToasts();
        void DrawWatermark();
        void DrawTooltip();
        void ScrollBar(Id id, float roundTop = 0.0f, float roundBottom = 0.0f);
        void ResizeCorner(Id id, const Rect& r, float radius, Color ink = Color(0.0f, 0.0f, 0.0f, 0.0f));
        void BeginFade(WinFrame& wf, const Rect& view);
        void ResumeFade();
        // popups: Context's popup while it is open, a window without input that fades out after
        bool BeginPopupEx(Id pid, Vec2 pos, Vec2 pivot, Vec2 size, Vec2 anchor, bool combo = false, bool drop = false);
        void EndPopupEx();
        bool PopupRow(Id id, std::string_view label, std::string_view shortcut, Icon icon, bool check, bool enabled, bool submenu, bool& hovered);
        // the text editor behind every field: `field` the control's rect; returns whether the text changed (`entered`:
        // Enter was pressed in a single line)
        bool EditText(Id id, const Rect& field, std::string& text, std::uint32_t flags, const char* hint, bool multiline, bool& entered, float radius);

        esia::Context& ctx_;
        text::TextSystem* text_;
        Fonts fonts_;

        // the clock
        double time_ = 0.0;
        float dt_ = 0.0f;
        float timeScale_ = 1.0f;
        bool step_ = false;
        float pixelScale_ = 1.0f;

        // the theme
        int themeIndex_ = 0;
        Theme shown_;
        ColorSpring themeColors_[kThemeColors];
        bool themeMoving_ = false;
        Spring dark_;
        MaterialSpec materials_[(int)SurfaceKind::Count];
        // each surface's material as shown: a weight per kind, its opacity and blur
        struct MaterialMotion
        {
            Spring kind[4], opacity, blur;
            bool made = false;
        };
        MaterialMotion materialMotion_[(int)SurfaceKind::Count];

        // state of the frame
        std::vector<WinFrame> windows_;
        std::vector<float> alpha_{1.0f};
        std::vector<bool> disabled_{false};
        std::vector<std::pair<Id, int>> seq_;   // items without ids this frame, counted per id scope (SeqId)
        std::vector<Fold> folds_;
        bool inert_ = false;
        bool focusVisible_ = false;   // the keyboard moved the focus (Tab): focus rings show; a click hides them
        // The hub's content keeps one column: text, captions and controls start 12 in; a surface that spans the content
        // (a row, a selectable, a header, a field) reaches `bleed_` further left, so the text in it stands on that column.
        float bleed_ = 0.0f;
        float nextWidth_ = 0.0f;
        bool lastHovered_ = false;
        Id lastId_ = 0;
        int cascade_ = 0;
        std::string fmt_;

        // the tooltip: text asked for this frame, the item it belongs to and how long it has been hovered
        std::string tooltip_;
        Id tooltipItem_ = 0, tooltipShownFor_ = 0;
        float tooltipHover_ = 0.0f;
        Spring tooltipShown_;
        Rect tooltipAnchor_;
        double tooltipWarm_ = -10.0;
        // where it is and how large (it glides from item to item), the text it shows and the one it changes from
        Spring tooltipX_, tooltipY_, tooltipW_, tooltipH_, tooltipSwap_;
        std::string tooltipText_, tooltipPrev_;
        Id tooltipFollow_ = 0;       // the item it was shown for last frame, and where it was: it moves with it
        Vec2 tooltipFollowAt_;

        // the hub
        bool hubBegun_ = false, hubMinimized_ = false, hubMaximized_ = false, hubResizable_ = false;
        HubSpec hub_;
        Rect hubRect_, hubSidebar_, hubContent_, hubRestore_;
        std::vector<HubTabEntry> hubTabs_;
        std::size_t hubTabCount_ = 0;
        Id hubSelected_ = 0;
        int hubSelectedIndex_ = 0;   // the order of the tabs: which way the content moves when the selection changes
        bool hubSelectedSeen_ = false, hubContentOpen_ = false;
        Spring hubShown_, hubMorph_, hubMax_;
        float hubDir_ = 1.0f;          // +1: the selection moved down the list (content comes up), -1 up
        Rect hubAvatar_;          // the profile's avatar in the title bar: its menu's anchor
        std::string hubCrumb_;    // the selected tab of the hub content's first tab bar (the footer's path)
        int hubPats_ = 0;
        // the floating windows (Begin / End) of this frame and the one before: where the pet can walk
        struct Floating
        {
            Id id = 0;
            Rect rect;
        };
        std::vector<Floating> floating_, floatingPrev_;
        esia::TextureId spot_ = 0;   // a soft round spot (shadows, glows of any shape): SoftSpot
        void SoftSpot(esia::DrawList& dl, const Rect& r, Color c);
        esia::TextureId ball_ = 0;   // a little lit ball: the pet's grains
        void Ball(esia::DrawList& dl, Vec2 c, float r, std::uint32_t color);
        int hubEggClicks_ = 0;    // the footer's secret: the version clicked 7 times
        double hubEggLast_ = -10.0, hubEgg_ = -1.0;
        float hubFooter_ = 0.0f;  // the footer's height (0: none)
        PendingRow row_;
        std::vector<TabBarFrame> tabBars_;
        std::vector<TableFrame> tables_;
        std::vector<float> bleedStack_;   // list boxes: their inside spans them (no bleed)
        int rowIndex_ = 0;
        // menus: the bar being submitted, the menus open from it
        bool inMenuBar_ = false;
        Rect menuBar_;
        float menuX_ = 0.0f;
        // the menus being submitted (BeginMenu returned true), true for a menu of the bar: what is in it is not on the
        // bar (a menu in it is a submenu), and its EndMenu brings the bar back
        std::vector<bool> openMenus_;
        Id menuBarOpen_ = 0;

        // overlays
        std::vector<Toast> toasts_;
        int toastSerial_ = 0;
        WatermarkInfo watermark_;
        mutable std::vector<std::pair<std::uint64_t, float>> baselines_;   // per font and size: the baseline of "H"
        std::vector<std::pair<std::uint64_t, float>> balanced_;             // BalancedWidth's answers
        bool watermarkOn_ = false;
        Vec2 watermarkPos_{-1.0f, -1.0f};
        Spring watermarkX_, watermarkY_;
        Spring watermarkParts_[4];   // the widths of its numbers (they grow and shrink as the digits change)
        bool watermarkMade_ = false;
        // its numbers, held for half a second (a number that changes every frame cannot be read)
        float watermarkFps_ = 0.0f, watermarkMs_ = 0.0f, watermarkMsSum_ = 0.0f;
        int watermarkMsCount_ = 0;
        double watermarkHeld_ = -1.0;
        int watermarkCorner_ = 3;   // 0 top left, 1 top right, 2 bottom left, 3 bottom right
        Vec2 watermarkGrab_;
        bool watermarkDragging_ = false;
    };
}
