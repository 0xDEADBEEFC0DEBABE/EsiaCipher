// Cipher - a 3D model: a glTF 2.0 binary (.glb) read on a thread of its own - its meshes, their textures packed into
// one atlas, its skins and its animations - posed on the CPU (an animation sampled at a time, crossfading from the one
// before; the skins blended per vertex) and drawn as textured triangles sorted far to near into a draw list. The 2D
// renderer draws it on every backend, its see-through parts blended in order (no depth buffer). Made for small
// characters of some thousands of triangles: a pet, a preview. Lit by a soft studio light from the top left (its
// normals); a material marked unlit (KHR_materials_unlit) keeps its textures' colors as they are.
#pragma once
#include "esia/base/math.hpp"
#include "esia/core/draw_list.hpp"
#include "esia/core/texture.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace cipher
{
    class Model
    {
    public:
        enum class State : std::uint8_t { Empty, Loading, Ready, Failed };

        Model() = default;
        ~Model();
        Model(const Model&) = delete;
        Model& operator=(const Model&) = delete;

        // Starts reading `path` on a thread of its own (the atlas goes into `textures` from there: the registry takes
        // textures from any thread); the model there before goes. Loading, then Ready - or Failed, Error() saying why.
        void Load(esia::TextureRegistry& textures, std::string path);
        State GetState() const;
        const std::string& Error() const;
        const std::string& Path() const { return path_; }

        // what it is made of (Ready)
        int Triangles() const;
        int Vertices() const;
        int Joints() const;
        int Meshes() const;
        int Animations() const;
        const std::string& AnimationName(int i) const;
        float AnimationLength(int i) const;
        int FindAnimation(std::string_view part) const;   // the first whose name holds `part` (-1: none)
        float Height() const;                              // its rest pose's height, in its own units
        // How fast animation `anim` walks (its own units a second): how fast its lowest foot (a node named "...Foot...")
        // goes back while it is on the ground. 0: no feet, or they do not go back.
        float StanceSpeed(int anim);
        // Meshes whose node's name holds `part` are drawn or not (a face's variants, say). Before or after it is Ready.
        void ShowMesh(std::string_view part, bool shown);

        // Its pose: animation `anim` at `time` (seconds, wrapped round its length; -1: the rest pose), mixed while
        // `blend` < 1 with `from` at `fromTime` (0: all `from`, 1: all `anim`).
        void Pose(int anim, float time, int from = -1, float fromTime = 0.0f, float blend = 1.0f);

        // Draws the pose into `dl` on the current clip: its feet on the middle of `box`'s bottom, as tall as the box
        // (its rest pose), turned `yaw` radians (+ turns its face to its left: the viewer's right), seen from `pitch`
        // radians above; `color` (RGBA8) multiplies its texels. Its outline anti-aliased over `pixel` UI units (one
        // pixel: 1 / the pixel scale): a fringe fading out round every edge where it turns away. Bounds(): what it
        // covered. Where(): a node of the pose (the first whose name holds `part`) where Draw put it - its head, a hand.
        // `mask`: how much of it shows where - a grid of `w` x `h` cells over `area` (screen units), each 0..1; a vertex
        // takes the value where it falls (bilinear; outside the grid, `outside`). `hard`: sharpened (smoothstep 0.3 -
        // 0.7), so that little of it is half there - half there, what is behind its surface shows through it.
        struct Mask
        {
            const float* cells = nullptr;
            int w = 0, h = 0;
            esia::Rect area;
            float outside = 1.0f;
            bool hard = false;
        };
        void Draw(esia::DrawList& dl, const esia::Rect& box, float yaw, float pitch, std::uint32_t color, float pixel = 1.0f, const Mask* mask = nullptr);
        // The pose as the last Draw drew it, as points: cells of `cell` units over Bounds(), each the color of the
        // nearest of it there (its texel times its material's color) - what can be seen of it, evenly spread. Returns
        // the grid (its area; `gw` x `gh` cells): each point's `index` is its cell in it (row by row).
        struct Point
        {
            esia::Vec2 at;
            std::uint32_t color;   // RGBA8, opaque
            int index = 0;
        };
        esia::Rect Sample(float cell, std::vector<Point>& out, int& gw, int& gh) const;
        esia::Rect Bounds() const { return bounds_; }
        bool Where(std::string_view part, esia::Vec2& at) const;

        struct Data;   // what was read (model.cpp)

    private:
        std::shared_ptr<Data> data_;   // shared with the thread reading it
        std::thread loader_;
        std::string path_;
        esia::TextureRegistry* textures_ = nullptr;
        std::vector<std::string> hidden_;   // ShowMesh(part, false)
        esia::Rect bounds_;
        float view_[8] = {};   // the last Draw's projection: origin x, y, scale, cos / sin yaw, cos / sin pitch, middle x / z
        float viewZ_ = 0.0f, viewLo_ = 0.0f;

        // the pose (Ready): the nodes' local transforms and world matrices, the skinned positions of every vertex set
        std::vector<float> local_, world_, joints_;
        std::vector<esia::Vec2> screen_;
        std::vector<float> depth_, posed_;
        std::vector<float> posedNormals_, shade_;   // the posed normals of the lit vertex sets; each vertex's light
        struct Tri
        {
            float depth;
            std::uint32_t a, b, c;
            std::uint32_t uv;     // its primitive (high 12 bits) and its first index in it (low 20)
            std::uint32_t edges;  // the edges on its outline (bit i: from corner i to corner i + 1)
        };
        std::vector<Tri> tris_;
        std::vector<std::uint8_t> front_;   // every triangle of every vertex set: drawn (facing the viewer, shown)
        std::vector<std::uint16_t> keys_;   // the triangles' depths, 16 bits over the range there is
        std::vector<std::uint32_t> order_, spare_;   // the triangles far to near (a radix sort's two passes)
        std::vector<std::uint32_t> colors_;          // each primitive's color this frame
        // Draw's vertices: each primitive's, those its triangles use (usedVtx_ from usedFirst_[i], each vertex's place
        // among them in remap_ from remapFirst_[i]: found once); where each primitive's go this Draw (primOut_)
        std::vector<std::uint32_t> usedFirst_, usedVtx_, remapFirst_, remap_, primOut_;
        void Join();
    };
}
