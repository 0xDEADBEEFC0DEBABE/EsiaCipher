// Cipher - a 3D model (model.hpp).
#include "model.hpp"
#include <algorithm>
#include <atomic>
#include <charconv>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <unordered_map>

#if CIPHER_PNG
namespace esia::text::detail
{
    // Esia's PNG decoder (its FreeType text system's: color bitmap fonts) - straight-alpha RGBA8, rows top first
    bool DecodePng(const std::uint8_t* data, std::size_t size, int& width, int& height, std::vector<std::uint8_t>& rgba);
}
#endif

namespace cipher
{
    using esia::Rect;
    using esia::Vec2;

    namespace
    {
        // ------------------------------------------------------------------ JSON: what a glTF's first chunk holds
        struct Json
        {
            enum class Type : std::uint8_t { Null, Bool, Number, String, Array, Object };
            Type type = Type::Null;
            double number = 0.0;
            std::string text;          // a string's characters
            std::string key;           // an object member's name
            std::vector<Json> items;   // an array's items, an object's members

            const Json* Get(std::string_view k) const
            {
                if (type == Type::Object)
                    for (const Json& m : items)
                        if (m.key == k)
                            return &m;
                return nullptr;
            }
            const Json& At(std::size_t i) const
            {
                static const Json none;
                return type == Type::Array && i < items.size() ? items[i] : none;
            }
            std::size_t Size() const { return type == Type::Array ? items.size() : 0; }
            double Num(std::string_view k, double def) const
            {
                const Json* v = Get(k);
                return v && v->type == Type::Number ? v->number : def;
            }
            int Int(std::string_view k, int def = -1) const { return (int)Num(k, (double)def); }
            std::string_view Str(std::string_view k) const
            {
                const Json* v = Get(k);
                return v && v->type == Type::String ? std::string_view(v->text) : std::string_view();
            }
            bool Bool(std::string_view k) const
            {
                const Json* v = Get(k);
                return v && v->type == Type::Bool && v->number != 0.0;
            }
        };

        class JsonReader
        {
        public:
            JsonReader(const char* p, const char* end) : p_(p), end_(end) {}
            bool Read(Json& out)
            {
                Value(out, 0);
                Space();
                return ok_;
            }

        private:
            const char* p_;
            const char* end_;
            bool ok_ = true;

            void Fail()
            {
                ok_ = false;
                p_ = end_;
            }
            void Space()
            {
                while (p_ < end_ && (*p_ == ' ' || *p_ == '\n' || *p_ == '\r' || *p_ == '\t'))
                    ++p_;
            }
            bool Next(char c)
            {
                Space();
                if (p_ < end_ && *p_ == c)
                {
                    ++p_;
                    return true;
                }
                return false;
            }
            static void Utf8(std::string& out, std::uint32_t c)
            {
                if (c < 0x80)
                    out += (char)c;
                else if (c < 0x800)
                {
                    out += (char)(0xC0 | (c >> 6));
                    out += (char)(0x80 | (c & 0x3F));
                }
                else if (c < 0x10000)
                {
                    out += (char)(0xE0 | (c >> 12));
                    out += (char)(0x80 | ((c >> 6) & 0x3F));
                    out += (char)(0x80 | (c & 0x3F));
                }
                else
                {
                    out += (char)(0xF0 | (c >> 18));
                    out += (char)(0x80 | ((c >> 12) & 0x3F));
                    out += (char)(0x80 | ((c >> 6) & 0x3F));
                    out += (char)(0x80 | (c & 0x3F));
                }
            }
            std::uint32_t Hex4()
            {
                if (end_ - p_ < 4)
                {
                    Fail();
                    return 0;
                }
                std::uint32_t v = 0;
                for (int i = 0; i < 4; ++i)
                {
                    const char c = *p_++;
                    v <<= 4;
                    if (c >= '0' && c <= '9')
                        v |= (std::uint32_t)(c - '0');
                    else if (c >= 'a' && c <= 'f')
                        v |= (std::uint32_t)(c - 'a' + 10);
                    else if (c >= 'A' && c <= 'F')
                        v |= (std::uint32_t)(c - 'A' + 10);
                    else
                        Fail();
                }
                return v;
            }
            void String(std::string& out)
            {
                ++p_;   // the opening quote
                while (p_ < end_ && *p_ != '"')
                {
                    if (*p_ != '\\')
                    {
                        out += *p_++;
                        continue;
                    }
                    if (++p_ >= end_)
                        break;
                    const char e = *p_++;
                    switch (e)
                    {
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    case 'u':
                    {
                        std::uint32_t c = Hex4();
                        if (c >= 0xD800 && c < 0xDC00 && end_ - p_ >= 6 && p_[0] == '\\' && p_[1] == 'u')
                        {
                            p_ += 2;
                            const std::uint32_t lo = Hex4();
                            c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
                        }
                        Utf8(out, c);
                        break;
                    }
                    default: out += e; break;   // " \ /
                    }
                }
                if (p_ >= end_)
                    return Fail();
                ++p_;   // the closing quote
            }
            void Value(Json& v, int depth)
            {
                Space();
                if (p_ >= end_ || depth > 200)
                    return Fail();
                const char c = *p_;
                if (c == '{')
                {
                    ++p_;
                    v.type = Json::Type::Object;
                    if (Next('}'))
                        return;
                    do
                    {
                        Space();
                        if (p_ >= end_ || *p_ != '"')
                            return Fail();
                        Json m;
                        String(m.key);
                        if (!Next(':'))
                            return Fail();
                        Value(m, depth + 1);
                        v.items.push_back(std::move(m));
                    } while (ok_ && Next(','));
                    if (!Next('}'))
                        Fail();
                }
                else if (c == '[')
                {
                    ++p_;
                    v.type = Json::Type::Array;
                    if (Next(']'))
                        return;
                    do
                    {
                        v.items.emplace_back();
                        Value(v.items.back(), depth + 1);
                    } while (ok_ && Next(','));
                    if (!Next(']'))
                        Fail();
                }
                else if (c == '"')
                {
                    v.type = Json::Type::String;
                    String(v.text);
                }
                else if (c == 't' || c == 'f' || c == 'n')
                {
                    const std::string_view word = c == 't' ? "true" : (c == 'f' ? "false" : "null");
                    if ((std::size_t)(end_ - p_) < word.size() || std::string_view(p_, word.size()) != word)
                        return Fail();
                    p_ += word.size();
                    v.type = c == 'n' ? Json::Type::Null : Json::Type::Bool;
                    v.number = c == 't' ? 1.0 : 0.0;
                }
                else
                {
                    v.type = Json::Type::Number;
                    const std::from_chars_result r = std::from_chars(p_, end_, v.number);
                    if (r.ec != std::errc())
                        return Fail();
                    p_ = r.ptr;
                }
            }
        };

        // ------------------------------------------------------------------ transforms (4 x 4, columns first)
        struct M4
        {
            float m[16];
        };
        M4 Identity()
        {
            M4 r{};
            r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
            return r;
        }
        M4 Mul(const M4& a, const M4& b)
        {
            M4 r;
            for (int c = 0; c < 4; ++c)
                for (int row = 0; row < 4; ++row)
                    r.m[c * 4 + row] = a.m[row] * b.m[c * 4] + a.m[4 + row] * b.m[c * 4 + 1] + a.m[8 + row] * b.m[c * 4 + 2] + a.m[12 + row] * b.m[c * 4 + 3];
            return r;
        }
        // translation t, rotation q (x y z w), scale s
        M4 FromTrs(const float* t, const float* q, const float* s)
        {
            const float x = q[0], y = q[1], z = q[2], w = q[3];
            M4 r;
            r.m[0] = (1.0f - 2.0f * (y * y + z * z)) * s[0];
            r.m[1] = (2.0f * (x * y + z * w)) * s[0];
            r.m[2] = (2.0f * (x * z - y * w)) * s[0];
            r.m[3] = 0.0f;
            r.m[4] = (2.0f * (x * y - z * w)) * s[1];
            r.m[5] = (1.0f - 2.0f * (x * x + z * z)) * s[1];
            r.m[6] = (2.0f * (y * z + x * w)) * s[1];
            r.m[7] = 0.0f;
            r.m[8] = (2.0f * (x * z + y * w)) * s[2];
            r.m[9] = (2.0f * (y * z - x * w)) * s[2];
            r.m[10] = (1.0f - 2.0f * (x * x + y * y)) * s[2];
            r.m[11] = 0.0f;
            r.m[12] = t[0];
            r.m[13] = t[1];
            r.m[14] = t[2];
            r.m[15] = 1.0f;
            return r;
        }
        // a and b mixed, k of the way to b; quaternions the short way round, normalized
        void MixTrs(float* out, const float* a, const float* b, float k)
        {
            for (int i = 0; i < 3; ++i)
            {
                out[i] = a[i] + (b[i] - a[i]) * k;
                out[7 + i] = a[7 + i] + (b[7 + i] - a[7 + i]) * k;
            }
            const float sign = a[3] * b[3] + a[4] * b[4] + a[5] * b[5] + a[6] * b[6] < 0.0f ? -1.0f : 1.0f;
            float l = 0.0f;
            for (int i = 3; i < 7; ++i)
            {
                out[i] = a[i] + (b[i] * sign - a[i]) * k;
                l += out[i] * out[i];
            }
            l = l > 1e-12f ? 1.0f / std::sqrt(l) : 1.0f;
            for (int i = 3; i < 7; ++i)
                out[i] *= l;
        }
        std::uint32_t NextPow2(std::uint32_t v)
        {
            std::uint32_t p = 1;
            while (p < v)
                p <<= 1;
            return p;
        }
    }

    // ------------------------------------------------------------------ what was read
    struct Model::Data
    {
        std::atomic<State> state{State::Loading};
        std::string error;
        struct Node
        {
            int parent = -1;
            float trs[10] = {0, 0, 0, 0, 0, 0, 1, 1, 1, 1};   // translation, rotation (x y z w), scale
            bool matrix = false;                              // a fixed matrix instead (not animated)
            M4 m = Identity();
            std::string name;
        };
        std::vector<Node> nodes;
        std::vector<int> order;   // parents before their children
        struct Skin
        {
            std::vector<int> joints;
            std::vector<M4> inverseBind;
            int first = 0;   // its first joint matrix in the posed joints
        };
        std::vector<Skin> skins;
        int jointCount = 0;
        // a set of vertices (the positions the primitives of a mesh share) on a node; skinned or moved by the node
        struct Verts
        {
            int node = -1, skin = -1, accessor = -1;
            std::string name;   // its node's
            std::vector<float> pos;                  // 3 a vertex
            std::vector<float> nrm;                  // 3 a vertex: its normal (a lit set's; else empty)
            bool lit = false;                        // some primitive of it is lit
            std::vector<std::uint16_t> joints;       // 4 a vertex (skinned)
            std::vector<float> weights;              // 4 a vertex
            std::uint32_t first = 0;                 // its first vertex in the posed positions
            // its triangles, those of all its primitives in order (their indices into it), and across each edge (from
            // corner i to i + 1) the triangle on the other side (-1: none - its outline)
            std::vector<std::uint32_t> tris;
            std::vector<std::int32_t> across;
            std::uint32_t firstTri = 0;              // its first triangle in all of them
        };
        std::vector<Verts> verts;
        std::uint32_t vertexCount = 0;
        struct Prim
        {
            int verts = -1;
            std::uint32_t firstTri = 0;            // its first triangle in its vertex set's
            bool twoSided = false;
            bool unlit = false;                    // KHR_materials_unlit: its colors as they are, no light
            std::uint32_t color = 0xFFFFFFFFu;     // its material's base color factor (sRGB)
            std::vector<std::uint32_t> idx;        // into its vertex set
            std::vector<Vec2> uv;                  // a vertex of its set: in the atlas
        };
        std::vector<Prim> prims;
        int triangles = 0;
        std::uint32_t triangleCount = 0;   // all the vertex sets' triangles
        struct Channel
        {
            int node = -1, path = 0;               // 0 translation, 1 rotation, 2 scale
            std::vector<float> times, values;
        };
        struct Anim
        {
            std::string name;
            float length = 0.0f;
            std::vector<Channel> channels;
        };
        std::vector<Anim> anims;
        esia::TextureId atlas = 0;
        std::vector<std::uint8_t> atlasPixels;   // its pixels, kept (Sample reads them)
        int atlasW = 0, atlasH = 0;
        float lo[3] = {0, 0, 0}, hi[3] = {1, 1, 1};   // the rest pose's bounds
    };

    namespace
    {
        using Data = Model::Data;

        // ------------------------------------------------------------------ posing (the reading thread poses the rest
        // to measure it; the UI thread every frame)
        // the nodes' local transforms: the rest, animation `a` at `t` sampled over it
        void SampleAnim(const Data& d, int a, float t, std::vector<float>& trs)
        {
            trs.resize(d.nodes.size() * 10);
            for (std::size_t i = 0; i < d.nodes.size(); ++i)
                std::memcpy(&trs[i * 10], d.nodes[i].trs, sizeof(float) * 10);
            if (a < 0 || a >= (int)d.anims.size())
                return;
            const Data::Anim& an = d.anims[(std::size_t)a];
            if (an.length > 0.0f)
            {
                t = std::fmod(t, an.length);
                if (t < 0.0f)
                    t += an.length;
            }
            for (const Data::Channel& ch : an.channels)
            {
                const std::size_t n = ch.times.size();
                if (n == 0 || ch.node < 0)
                    continue;
                const int comps = ch.path == 1 ? 4 : 3;
                std::size_t i = (std::size_t)(std::upper_bound(ch.times.begin(), ch.times.end(), t) - ch.times.begin());
                float k = 0.0f;
                std::size_t i0, i1;
                if (i == 0)
                    i0 = i1 = 0;
                else if (i >= n)
                    i0 = i1 = n - 1;
                else
                {
                    i0 = i - 1;
                    i1 = i;
                    const float span = ch.times[i1] - ch.times[i0];
                    k = span > 1e-6f ? (t - ch.times[i0]) / span : 0.0f;
                }
                float* out = &trs[(std::size_t)ch.node * 10 + (ch.path == 0 ? 0 : (ch.path == 1 ? 3 : 7))];
                const float* v0 = &ch.values[i0 * (std::size_t)comps];
                const float* v1 = &ch.values[i1 * (std::size_t)comps];
                if (comps == 3)
                    for (int c = 0; c < 3; ++c)
                        out[c] = v0[c] + (v1[c] - v0[c]) * k;
                else
                {
                    const float sign = v0[0] * v1[0] + v0[1] * v1[1] + v0[2] * v1[2] + v0[3] * v1[3] < 0.0f ? -1.0f : 1.0f;
                    float l = 0.0f;
                    for (int c = 0; c < 4; ++c)
                    {
                        out[c] = v0[c] + (v1[c] * sign - v0[c]) * k;
                        l += out[c] * out[c];
                    }
                    l = l > 1e-12f ? 1.0f / std::sqrt(l) : 1.0f;
                    for (int c = 0; c < 4; ++c)
                        out[c] *= l;
                }
            }
        }

        // the nodes' world matrices from their local transforms
        void World(const Data& d, const std::vector<float>& trs, std::vector<float>& world)
        {
            world.resize(d.nodes.size() * 16);
            for (const int i : d.order)
            {
                const Data::Node& n = d.nodes[(std::size_t)i];
                const M4 local = n.matrix ? n.m : FromTrs(&trs[(std::size_t)i * 10], &trs[(std::size_t)i * 10 + 3], &trs[(std::size_t)i * 10 + 7]);
                M4 w = local;
                if (n.parent >= 0)
                {
                    M4 p;
                    std::memcpy(p.m, &world[(std::size_t)n.parent * 16], sizeof(p.m));
                    w = Mul(p, local);
                }
                std::memcpy(&world[(std::size_t)i * 16], w.m, sizeof(w.m));
            }
        }

        // every skin's joint matrices, then the positions of the vertex sets `shown` (null: all)
        void Skin(const Data& d, const std::vector<float>& world, std::vector<float>& joints, std::vector<float>& posed, const std::vector<bool>* shown,
                  std::vector<float>* normals = nullptr)
        {
            if (normals)
                normals->resize((std::size_t)d.vertexCount * 3);
            joints.resize((std::size_t)d.jointCount * 16);
            for (const Data::Skin& s : d.skins)
                for (std::size_t j = 0; j < s.joints.size(); ++j)
                {
                    M4 w;
                    std::memcpy(w.m, &world[(std::size_t)s.joints[j] * 16], sizeof(w.m));
                    const M4 jm = Mul(w, s.inverseBind[j]);
                    std::memcpy(&joints[((std::size_t)s.first + j) * 16], jm.m, sizeof(jm.m));
                }
            posed.resize((std::size_t)d.vertexCount * 3);
            for (std::size_t vi = 0; vi < d.verts.size(); ++vi)
            {
                if (shown && !(*shown)[vi])
                    continue;
                const Data::Verts& v = d.verts[vi];
                const std::size_t n = v.pos.size() / 3;
                float* out = &posed[(std::size_t)v.first * 3];
                if (v.skin >= 0 && !v.joints.empty())
                {
                    const float* jm = &joints[(std::size_t)d.skins[(std::size_t)v.skin].first * 16];
                    const std::size_t jn = d.skins[(std::size_t)v.skin].joints.size();
                    for (std::size_t i = 0; i < n; ++i)
                    {
                        // the joints' matrices blended by their weights (the 3 rows that matter), then the position
                        float m[12] = {};
                        for (int k = 0; k < 4; ++k)
                        {
                            const float wgt = v.weights[i * 4 + (std::size_t)k];
                            const std::size_t j = v.joints[i * 4 + (std::size_t)k];
                            if (wgt <= 0.0f || j >= jn)
                                continue;
                            const float* a = jm + j * 16;
                            for (int c = 0; c < 4; ++c)
                                for (int r = 0; r < 3; ++r)
                                    m[c * 3 + r] += a[c * 4 + r] * wgt;
                        }
                        const float x = v.pos[i * 3], y = v.pos[i * 3 + 1], z = v.pos[i * 3 + 2];
                        out[i * 3] = m[0] * x + m[3] * y + m[6] * z + m[9];
                        out[i * 3 + 1] = m[1] * x + m[4] * y + m[7] * z + m[10];
                        out[i * 3 + 2] = m[2] * x + m[5] * y + m[8] * z + m[11];
                        if (normals && v.lit && !v.nrm.empty())
                        {
                            const float nx = v.nrm[i * 3], ny = v.nrm[i * 3 + 1], nz = v.nrm[i * 3 + 2];
                            float* on = &(*normals)[((std::size_t)v.first + i) * 3];
                            on[0] = m[0] * nx + m[3] * ny + m[6] * nz;
                            on[1] = m[1] * nx + m[4] * ny + m[7] * nz;
                            on[2] = m[2] * nx + m[5] * ny + m[8] * nz;
                        }
                    }
                }
                else
                {
                    const float* a = &world[(std::size_t)std::max(v.node, 0) * 16];
                    for (std::size_t i = 0; i < n; ++i)
                    {
                        const float x = v.pos[i * 3], y = v.pos[i * 3 + 1], z = v.pos[i * 3 + 2];
                        out[i * 3] = a[0] * x + a[4] * y + a[8] * z + a[12];
                        out[i * 3 + 1] = a[1] * x + a[5] * y + a[9] * z + a[13];
                        out[i * 3 + 2] = a[2] * x + a[6] * y + a[10] * z + a[14];
                        if (normals && v.lit && !v.nrm.empty())
                        {
                            const float nx = v.nrm[i * 3], ny = v.nrm[i * 3 + 1], nz = v.nrm[i * 3 + 2];
                            float* on = &(*normals)[((std::size_t)v.first + i) * 3];
                            on[0] = a[0] * nx + a[4] * ny + a[8] * nz;
                            on[1] = a[1] * nx + a[5] * ny + a[9] * nz;
                            on[2] = a[2] * nx + a[6] * ny + a[10] * nz;
                        }
                    }
                }
            }
        }

        // ------------------------------------------------------------------ reading a .glb
        struct Accessor
        {
            const std::uint8_t* first = nullptr;
            std::size_t stride = 0;
            int count = 0, comps = 0, type = 0;   // type: the component type (5120 ... 5126)
            bool normalized = false;
            float Get(int i, int c) const
            {
                const std::uint8_t* p = first + (std::size_t)i * stride;
                switch (type)
                {
                case 5126: { float f; std::memcpy(&f, p + c * 4, 4); return f; }
                case 5125: { std::uint32_t u; std::memcpy(&u, p + c * 4, 4); return (float)u; }
                case 5123: { std::uint16_t u; std::memcpy(&u, p + c * 2, 2); return normalized ? (float)u / 65535.0f : (float)u; }
                case 5122: { std::int16_t s; std::memcpy(&s, p + c * 2, 2); return normalized ? std::max((float)s / 32767.0f, -1.0f) : (float)s; }
                case 5121: return normalized ? (float)p[c] / 255.0f : (float)p[c];
                case 5120: return normalized ? std::max((float)(std::int8_t)p[c] / 127.0f, -1.0f) : (float)(std::int8_t)p[c];
                default: return 0.0f;
                }
            }
            std::uint32_t Index(int i) const
            {
                const std::uint8_t* p = first + (std::size_t)i * stride;
                switch (type)
                {
                case 5125: { std::uint32_t u; std::memcpy(&u, p, 4); return u; }
                case 5123: { std::uint16_t u; std::memcpy(&u, p, 2); return u; }
                case 5121: return p[0];
                default: return 0;
                }
            }
        };

        class Reader
        {
        public:
            Reader(const Json& j, const std::uint8_t* bin, std::size_t binSize) : j_(j), bin_(bin), binSize_(binSize) {}

            // accessor `a` (false: none, or outside the binary chunk)
            bool Acc(int a, Accessor& out) const
            {
                const Json* accs = j_.Get("accessors");
                const Json* views = j_.Get("bufferViews");
                if (!accs || !views || a < 0 || (std::size_t)a >= accs->Size())
                    return false;
                const Json& ac = accs->At((std::size_t)a);
                const int view = ac.Int("bufferView");
                if (view < 0 || (std::size_t)view >= views->Size())
                    return false;
                const Json& bv = views->At((std::size_t)view);
                if (bv.Int("buffer", 0) != 0)
                    return false;
                const std::string_view type = ac.Str("type");
                out.comps = type == "SCALAR" ? 1 : type == "VEC2" ? 2 : type == "VEC3" ? 3 : type == "VEC4" ? 4 : type == "MAT4" ? 16 : 0;
                out.type = ac.Int("componentType", 0);
                out.count = ac.Int("count", 0);
                out.normalized = ac.Bool("normalized");
                const int size = out.type == 5126 || out.type == 5125 ? 4 : (out.type == 5123 || out.type == 5122 ? 2 : 1);
                const std::size_t elem = (std::size_t)size * (std::size_t)out.comps;
                const std::size_t offset = (std::size_t)bv.Num("byteOffset", 0.0) + (std::size_t)ac.Num("byteOffset", 0.0);
                out.stride = (std::size_t)bv.Num("byteStride", 0.0);
                if (out.stride == 0)
                    out.stride = elem;
                if (out.comps == 0 || out.count <= 0 || offset + (std::size_t)(out.count - 1) * out.stride + elem > binSize_)
                    return false;
                out.first = bin_ + offset;
                return true;
            }
            // the bytes of buffer view `v`
            bool View(int v, const std::uint8_t*& data, std::size_t& size) const
            {
                const Json* views = j_.Get("bufferViews");
                if (!views || v < 0 || (std::size_t)v >= views->Size())
                    return false;
                const Json& bv = views->At((std::size_t)v);
                const std::size_t offset = (std::size_t)bv.Num("byteOffset", 0.0), length = (std::size_t)bv.Num("byteLength", 0.0);
                if (offset + length > binSize_)
                    return false;
                data = bin_ + offset;
                size = length;
                return true;
            }

        private:
            const Json& j_;
            const std::uint8_t* bin_;
            std::size_t binSize_;
        };

        bool Fail(Data& d, std::string why)
        {
            d.error = std::move(why);
            d.state.store(Model::State::Failed, std::memory_order_release);
            return false;
        }

        bool ReadGlb(Data& d, const std::string& path, esia::TextureRegistry& textures)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file)
                return Fail(d, "cannot open " + path);
            const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            const auto u32 = [&](std::size_t at) {
                std::uint32_t v = 0;
                if (at + 4 <= bytes.size())
                    std::memcpy(&v, &bytes[at], 4);
                return v;
            };
            if (bytes.size() < 20 || u32(0) != 0x46546C67u || u32(4) != 2)
                return Fail(d, "not a glTF 2.0 binary (.glb)");
            const std::size_t jsonSize = u32(12);
            if (u32(16) != 0x4E4F534Au || 20 + jsonSize > bytes.size())
                return Fail(d, "no JSON chunk");
            const std::uint8_t* bin = nullptr;
            std::size_t binSize = 0;
            const std::size_t binAt = 20 + ((jsonSize + 3) & ~std::size_t(3));
            if (binAt + 8 <= bytes.size() && u32(binAt + 4) == 0x004E4942u)
            {
                binSize = std::min<std::size_t>(u32(binAt), bytes.size() - binAt - 8);
                bin = &bytes[binAt + 8];
            }
            const std::string text(reinterpret_cast<const char*>(&bytes[20]), jsonSize);
            Json j;
            if (!JsonReader(text.data(), text.data() + text.size()).Read(j) || j.type != Json::Type::Object)
                return Fail(d, "its JSON does not read");
            const Reader r(j, bin, binSize);

            // ---- the textures, packed into one atlas (2 pixels of each one's edge repeated round it: linear
            // filtering does not reach its neighbours); a white block for materials without one
            struct Image
            {
                int w = 0, h = 0, x = 0, y = 0;
                std::vector<std::uint8_t> rgba;
            };
            std::vector<Image> images;
            if (const Json* ims = j.Get("images"))
                for (std::size_t i = 0; i < ims->Size(); ++i)
                {
                    Image im;
                    const std::uint8_t* data = nullptr;
                    std::size_t size = 0;
#if CIPHER_PNG
                    if (r.View(ims->At(i).Int("bufferView"), data, size) && ims->At(i).Str("mimeType") == "image/png")
                        esia::text::detail::DecodePng(data, size, im.w, im.h, im.rgba);
#endif
                    if (im.rgba.empty())
                    {
                        im.w = im.h = 4;
                        im.rgba.assign(4 * 4 * 4, 0xFF);   // unread (a JPEG, no decoder): white
                    }
                    images.push_back(std::move(im));
                }
            Image white;
            white.w = white.h = 4;
            white.rgba.assign(4 * 4 * 4, 0xFF);
            images.push_back(std::move(white));
            constexpr int kPad = 2;
            int atlasW = 512;
            for (const Image& im : images)
                atlasW = std::max(atlasW, (int)NextPow2((std::uint32_t)(im.w + 2 * kPad)));
            std::vector<std::size_t> byHeight(images.size());
            for (std::size_t i = 0; i < images.size(); ++i)
                byHeight[i] = i;
            std::sort(byHeight.begin(), byHeight.end(), [&](std::size_t a, std::size_t b) { return images[a].h > images[b].h; });
            int x = 0, y = 0, shelf = 0;
            for (const std::size_t i : byHeight)
            {
                Image& im = images[i];
                if (x + im.w + 2 * kPad > atlasW)
                {
                    y += shelf;
                    x = 0;
                    shelf = 0;
                }
                im.x = x + kPad;
                im.y = y + kPad;
                x += im.w + 2 * kPad;
                shelf = std::max(shelf, im.h + 2 * kPad);
            }
            const int atlasH = (int)NextPow2((std::uint32_t)(y + shelf));
            std::vector<std::uint8_t> atlas((std::size_t)atlasW * (std::size_t)atlasH * 4, 0);
            for (const Image& im : images)
                for (int py = -kPad; py < im.h + kPad; ++py)
                    for (int px = -kPad; px < im.w + kPad; ++px)
                    {
                        const int sx = std::clamp(px, 0, im.w - 1), sy = std::clamp(py, 0, im.h - 1);
                        std::memcpy(&atlas[((std::size_t)(im.y + py) * (std::size_t)atlasW + (std::size_t)(im.x + px)) * 4], &im.rgba[((std::size_t)sy * (std::size_t)im.w + (std::size_t)sx) * 4], 4);
                    }
            esia::TextureInfo info;
            info.format = esia::TextureFormat::RGBA8;
            info.width = atlasW;
            info.height = atlasH;
            d.atlas = textures.Create(info, atlas.data());
            d.atlasPixels = std::move(atlas);
            d.atlasW = atlasW;
            d.atlasH = atlasH;

            // ---- materials: where in the atlas, both sides or one, the base color
            struct Mat
            {
                int image = -1;
                bool twoSided = false, unlit = false;
                std::uint32_t color = 0xFFFFFFFFu;
            };
            std::vector<Mat> mats;
            const Json* texs = j.Get("textures");
            if (const Json* ms = j.Get("materials"))
                for (std::size_t i = 0; i < ms->Size(); ++i)
                {
                    const Json& m = ms->At(i);
                    Mat mt;
                    mt.twoSided = m.Bool("doubleSided");
                    if (const Json* ext = m.Get("extensions"))
                        mt.unlit = ext->Get("KHR_materials_unlit") != nullptr;
                    if (const Json* pbr = m.Get("pbrMetallicRoughness"))
                    {
                        if (const Json* bt = pbr->Get("baseColorTexture"))
                        {
                            const int t = bt->Int("index");
                            if (texs && t >= 0 && (std::size_t)t < texs->Size())
                                mt.image = texs->At((std::size_t)t).Int("source");
                        }
                        if (const Json* f = pbr->Get("baseColorFactor"); f && f->Size() == 4)
                        {
                            // linear in glTF; the UI's colors are sRGB (alpha stays as it is)
                            std::uint32_t c = 0;
                            for (int k = 0; k < 4; ++k)
                            {
                                double v = std::clamp(f->At((std::size_t)k).number, 0.0, 1.0);
                                if (k < 3)
                                    v = v <= 0.0031308 ? 12.92 * v : 1.055 * std::pow(v, 1.0 / 2.4) - 0.055;
                                c |= (std::uint32_t)std::lround(std::clamp(v, 0.0, 1.0) * 255.0) << (8 * k);
                            }
                            mt.color = c;
                        }
                    }
                    mats.push_back(mt);
                }

            // ---- nodes: their transforms, their parents, an order with the parents first
            const Json* nodes = j.Get("nodes");
            if (!nodes || nodes->Size() == 0)
                return Fail(d, "no nodes");
            d.nodes.resize(nodes->Size());
            for (std::size_t i = 0; i < nodes->Size(); ++i)
            {
                const Json& n = nodes->At(i);
                Data::Node& node = d.nodes[i];
                node.name = std::string(n.Str("name"));
                if (const Json* t = n.Get("translation"); t && t->Size() == 3)
                    for (int k = 0; k < 3; ++k)
                        node.trs[k] = (float)t->At((std::size_t)k).number;
                if (const Json* q = n.Get("rotation"); q && q->Size() == 4)
                    for (int k = 0; k < 4; ++k)
                        node.trs[3 + k] = (float)q->At((std::size_t)k).number;
                if (const Json* s = n.Get("scale"); s && s->Size() == 3)
                    for (int k = 0; k < 3; ++k)
                        node.trs[7 + k] = (float)s->At((std::size_t)k).number;
                if (const Json* m = n.Get("matrix"); m && m->Size() == 16)
                {
                    node.matrix = true;
                    for (int k = 0; k < 16; ++k)
                        node.m.m[k] = (float)m->At((std::size_t)k).number;
                }
            }
            for (std::size_t i = 0; i < nodes->Size(); ++i)
                if (const Json* ch = nodes->At(i).Get("children"))
                    for (std::size_t k = 0; k < ch->Size(); ++k)
                    {
                        const int c = (int)ch->At(k).number;
                        if (c >= 0 && (std::size_t)c < d.nodes.size() && c != (int)i)
                            d.nodes[(std::size_t)c].parent = (int)i;
                    }
            {
                std::vector<int> depth(d.nodes.size(), 0);
                for (std::size_t i = 0; i < d.nodes.size(); ++i)
                {
                    int p = d.nodes[i].parent, n = 0;
                    while (p >= 0 && n < (int)d.nodes.size())
                    {
                        p = d.nodes[(std::size_t)p].parent;
                        ++n;
                    }
                    if (n >= (int)d.nodes.size())
                        return Fail(d, "its nodes loop");
                    depth[i] = n;
                }
                d.order.resize(d.nodes.size());
                for (std::size_t i = 0; i < d.nodes.size(); ++i)
                    d.order[i] = (int)i;
                std::stable_sort(d.order.begin(), d.order.end(), [&](int a, int b) { return depth[(std::size_t)a] < depth[(std::size_t)b]; });
            }

            // ---- skins
            if (const Json* ss = j.Get("skins"))
                for (std::size_t i = 0; i < ss->Size(); ++i)
                {
                    const Json& s = ss->At(i);
                    Data::Skin sk;
                    sk.first = d.jointCount;
                    if (const Json* js = s.Get("joints"))
                        for (std::size_t k = 0; k < js->Size(); ++k)
                        {
                            const int n = (int)js->At(k).number;
                            sk.joints.push_back(n >= 0 && (std::size_t)n < d.nodes.size() ? n : 0);
                        }
                    sk.inverseBind.assign(sk.joints.size(), Identity());
                    Accessor ib;
                    if (r.Acc(s.Int("inverseBindMatrices"), ib) && ib.comps == 16)
                        for (int k = 0; k < std::min(ib.count, (int)sk.joints.size()); ++k)
                            for (int c = 0; c < 16; ++c)
                                sk.inverseBind[(std::size_t)k].m[c] = ib.Get(k, c);
                    d.jointCount += (int)sk.joints.size();
                    d.skins.push_back(std::move(sk));
                }

            // ---- meshes: the vertex sets of the nodes holding them, their triangles by material
            const Json* meshes = j.Get("meshes");
            for (std::size_t ni = 0; ni < d.nodes.size() && meshes; ++ni)
            {
                const Json& n = nodes->At(ni);
                const int mesh = n.Int("mesh");
                if (mesh < 0 || (std::size_t)mesh >= meshes->Size())
                    continue;
                const int skin = n.Int("skin");
                const Json* prims = meshes->At((std::size_t)mesh).Get("primitives");
                for (std::size_t pi = 0; prims && pi < prims->Size(); ++pi)
                {
                    const Json& p = prims->At(pi);
                    if (p.Int("mode", 4) != 4)
                        continue;   // triangles only
                    const Json* attrs = p.Get("attributes");
                    if (!attrs)
                        continue;
                    const int posAcc = attrs->Int("POSITION");
                    Accessor pos;
                    if (!r.Acc(posAcc, pos) || pos.comps != 3)
                        continue;
                    // the vertex set: shared with an earlier primitive of this node with the same positions
                    int vs = -1;
                    for (std::size_t k = 0; k < d.verts.size(); ++k)
                        if (d.verts[k].node == (int)ni && d.verts[k].accessor == posAcc)
                            vs = (int)k;
                    if (vs < 0)
                    {
                        Data::Verts v;
                        v.node = (int)ni;
                        v.accessor = posAcc;
                        v.name = d.nodes[ni].name;
                        v.pos.resize((std::size_t)pos.count * 3);
                        for (int i = 0; i < pos.count; ++i)
                            for (int c = 0; c < 3; ++c)
                                v.pos[(std::size_t)i * 3 + (std::size_t)c] = pos.Get(i, c);
                        Accessor nm;
                        if (r.Acc(attrs->Int("NORMAL"), nm) && nm.comps == 3 && nm.count == pos.count)
                        {
                            v.nrm.resize((std::size_t)pos.count * 3);
                            for (int i = 0; i < pos.count; ++i)
                                for (int c = 0; c < 3; ++c)
                                    v.nrm[(std::size_t)i * 3 + (std::size_t)c] = nm.Get(i, c);
                        }
                        Accessor jn, wt;
                        if (skin >= 0 && (std::size_t)skin < d.skins.size() && r.Acc(attrs->Int("JOINTS_0"), jn) && r.Acc(attrs->Int("WEIGHTS_0"), wt) &&
                            jn.count == pos.count && wt.count == pos.count && jn.comps == 4 && wt.comps == 4)
                        {
                            v.skin = skin;
                            v.joints.resize((std::size_t)pos.count * 4);
                            v.weights.resize((std::size_t)pos.count * 4);
                            for (int i = 0; i < pos.count; ++i)
                            {
                                float sum = 0.0f;
                                for (int c = 0; c < 4; ++c)
                                {
                                    v.joints[(std::size_t)i * 4 + (std::size_t)c] = (std::uint16_t)jn.Get(i, c);
                                    sum += (v.weights[(std::size_t)i * 4 + (std::size_t)c] = wt.Get(i, c));
                                }
                                if (sum > 1e-6f)
                                    for (int c = 0; c < 4; ++c)
                                        v.weights[(std::size_t)i * 4 + (std::size_t)c] /= sum;
                            }
                        }
                        v.first = d.vertexCount;
                        d.vertexCount += (std::uint32_t)pos.count;
                        vs = (int)d.verts.size();
                        d.verts.push_back(std::move(v));
                    }
                    Data::Prim prim;
                    prim.verts = vs;
                    const int material = p.Int("material");
                    const Mat mt = material >= 0 && (std::size_t)material < mats.size() ? mats[(std::size_t)material] : Mat{};
                    prim.twoSided = mt.twoSided;
                    prim.unlit = mt.unlit;
                    prim.color = mt.color;
                    if (!mt.unlit)
                        d.verts[(std::size_t)vs].lit = true;
                    const Image& im = mt.image >= 0 && (std::size_t)mt.image < images.size() - 1 ? images[(std::size_t)mt.image] : images.back();
                    const int count = (int)(d.verts[(std::size_t)vs].pos.size() / 3);
                    prim.uv.assign((std::size_t)count, Vec2((im.x + im.w * 0.5f) / (float)atlasW, (im.y + im.h * 0.5f) / (float)atlasH));
                    Accessor uv;
                    if (r.Acc(attrs->Int("TEXCOORD_0"), uv) && uv.comps == 2 && uv.count == count)
                        for (int i = 0; i < count; ++i)
                        {
                            const float u = std::clamp(uv.Get(i, 0), 0.0f, 1.0f), v = std::clamp(uv.Get(i, 1), 0.0f, 1.0f);
                            prim.uv[(std::size_t)i] = Vec2((im.x + u * im.w) / (float)atlasW, (im.y + v * im.h) / (float)atlasH);
                        }
                    Accessor ix;
                    if (r.Acc(p.Int("indices"), ix) && ix.comps == 1)
                    {
                        prim.idx.reserve((std::size_t)ix.count);
                        for (int i = 0; i + 2 < ix.count; i += 3)
                        {
                            const std::uint32_t a = ix.Index(i), b = ix.Index(i + 1), c = ix.Index(i + 2);
                            if (a < (std::uint32_t)count && b < (std::uint32_t)count && c < (std::uint32_t)count)
                            {
                                prim.idx.push_back(a);
                                prim.idx.push_back(b);
                                prim.idx.push_back(c);
                            }
                        }
                    }
                    else
                        for (int i = 0; i + 2 < count; i += 3)
                            for (int k = 0; k < 3; ++k)
                                prim.idx.push_back((std::uint32_t)(i + k));
                    d.triangles += (int)(prim.idx.size() / 3);
                    d.prims.push_back(std::move(prim));
                }
            }
            if (d.prims.empty())
                return Fail(d, "no triangles");
            // each vertex set's triangles in one list, and across each of their edges the triangle sharing it - the
            // corners welded by where they are (a seam of the texture splits a vertex, not the surface): where there
            // is none, or more than one, the edge is on the outline
            for (Data::Prim& p : d.prims)
            {
                Data::Verts& v = d.verts[(std::size_t)p.verts];
                p.firstTri = (std::uint32_t)(v.tris.size() / 3);
                v.tris.insert(v.tris.end(), p.idx.begin(), p.idx.end());
            }
            for (Data::Verts& v : d.verts)
            {
                const std::size_t nv = v.pos.size() / 3, nt = v.tris.size() / 3;
                std::vector<std::uint32_t> weld(nv);
                std::unordered_map<std::uint64_t, std::uint32_t> at;
                at.reserve(nv);
                for (std::size_t i = 0; i < nv; ++i)
                {
                    std::uint64_t key = 0;
                    for (int c = 0; c < 3; ++c)
                        key = key * 2097143u + (std::uint64_t)(std::int64_t)std::llround(v.pos[i * 3 + (std::size_t)c] * 100000.0);
                    weld[i] = at.emplace(key, (std::uint32_t)i).first->second;
                }
                std::unordered_map<std::uint64_t, std::int64_t> edges;   // the welded edge -> its first side, or -2: shared by more
                edges.reserve(nt * 3);
                v.across.assign(nt * 3, -1);
                for (std::size_t t = 0; t < nt; ++t)
                    for (int e = 0; e < 3; ++e)
                    {
                        const std::uint32_t a = weld[v.tris[t * 3 + (std::size_t)e]], b = weld[v.tris[t * 3 + (std::size_t)((e + 1) % 3)]];
                        if (a == b)
                            continue;
                        const std::uint64_t key = ((std::uint64_t)std::min(a, b) << 32) | std::max(a, b);
                        const std::int64_t side = (std::int64_t)(t * 3 + (std::size_t)e);
                        auto [it, made] = edges.emplace(key, side);
                        if (made)
                            continue;
                        if (it->second >= 0 && v.across[(std::size_t)it->second] == -1)
                        {
                            v.across[(std::size_t)it->second] = (std::int32_t)t;
                            v.across[(std::size_t)side] = (std::int32_t)(it->second / 3);
                            it->second = side;   // two so far
                        }
                        else
                        {
                            // a third: the edge is shared by more than two, none of them across it
                            if (it->second >= 0)
                            {
                                const std::size_t s0 = (std::size_t)it->second;
                                const std::int32_t other = v.across[s0];
                                v.across[s0] = -1;
                                if (other >= 0)
                                    for (int q = 0; q < 3; ++q)
                                        if (v.across[(std::size_t)other * 3 + (std::size_t)q] == (std::int32_t)(s0 / 3))
                                            v.across[(std::size_t)other * 3 + (std::size_t)q] = -1;
                                it->second = -2;
                            }
                        }
                    }
                v.firstTri = d.triangleCount;
                d.triangleCount += (std::uint32_t)nt;
            }

            // ---- animations (translation, rotation, scale; a cubic spline's in- and out-tangents left out)
            if (const Json* as = j.Get("animations"))
                for (std::size_t i = 0; i < as->Size(); ++i)
                {
                    const Json& a = as->At(i);
                    Data::Anim an;
                    an.name = std::string(a.Str("name"));
                    if (an.name.empty())
                        an.name = "Animation " + std::to_string(i + 1);
                    const Json* samplers = a.Get("samplers");
                    const Json* channels = a.Get("channels");
                    for (std::size_t c = 0; channels && samplers && c < channels->Size(); ++c)
                    {
                        const Json& ch = channels->At(c);
                        const Json* target = ch.Get("target");
                        if (!target)
                            continue;
                        const std::string_view property = target->Str("path");
                        const int kind = property == "translation" ? 0 : (property == "rotation" ? 1 : (property == "scale" ? 2 : -1));
                        const int node = target->Int("node");
                        const int sampler = ch.Int("sampler");
                        if (kind < 0 || node < 0 || (std::size_t)node >= d.nodes.size() || sampler < 0 || (std::size_t)sampler >= samplers->Size())
                            continue;
                        const Json& s = samplers->At((std::size_t)sampler);
                        Accessor in, out;
                        if (!r.Acc(s.Int("input"), in) || !r.Acc(s.Int("output"), out) || in.comps != 1)
                            continue;
                        const int comps = kind == 1 ? 4 : 3;
                        const bool cubic = s.Str("interpolation") == "CUBICSPLINE";
                        if (out.comps != comps || out.count != in.count * (cubic ? 3 : 1))
                            continue;
                        Data::Channel chan;
                        chan.node = node;
                        chan.path = kind;
                        chan.times.resize((std::size_t)in.count);
                        chan.values.resize((std::size_t)in.count * (std::size_t)comps);
                        for (int k = 0; k < in.count; ++k)
                        {
                            chan.times[(std::size_t)k] = in.Get(k, 0);
                            for (int q = 0; q < comps; ++q)
                                chan.values[(std::size_t)k * (std::size_t)comps + (std::size_t)q] = out.Get(cubic ? k * 3 + 1 : k, q);
                        }
                        an.length = std::max(an.length, chan.times.back());
                        an.channels.push_back(std::move(chan));
                    }
                    d.anims.push_back(std::move(an));
                }

            // ---- the rest pose's bounds: what Draw fits into its box
            std::vector<float> trs, world, joints, posed;
            SampleAnim(d, -1, 0.0f, trs);
            World(d, trs, world);
            Skin(d, world, joints, posed, nullptr);
            for (int c = 0; c < 3; ++c)
            {
                d.lo[c] = 1e30f;
                d.hi[c] = -1e30f;
            }
            for (std::size_t i = 0; i + 2 < posed.size(); i += 3)
                for (int c = 0; c < 3; ++c)
                {
                    d.lo[c] = std::min(d.lo[c], posed[i + (std::size_t)c]);
                    d.hi[c] = std::max(d.hi[c], posed[i + (std::size_t)c]);
                }
            d.state.store(Model::State::Ready, std::memory_order_release);
            return true;
        }
    }

    // ------------------------------------------------------------------ the model
    Model::~Model()
    {
        Join();
        if (data_ && data_->atlas && textures_)
            textures_->Destroy(data_->atlas);
    }

    void Model::Join()
    {
        if (loader_.joinable())
            loader_.join();
    }

    void Model::Load(esia::TextureRegistry& textures, std::string path)
    {
        Join();
        if (data_ && data_->atlas && textures_)
            textures_->Destroy(data_->atlas);
        textures_ = &textures;
        path_ = path;
        local_.clear();
        world_.clear();
        joints_.clear();
        posed_.clear();
        posedNormals_.clear();
        shade_.clear();
        bounds_ = Rect();
        auto data = std::make_shared<Data>();
        data_ = data;
        loader_ = std::thread([data, path = std::move(path), reg = &textures] { ReadGlb(*data, path, *reg); });
    }

    Model::State Model::GetState() const { return data_ ? data_->state.load(std::memory_order_acquire) : State::Empty; }

    const std::string& Model::Error() const
    {
        static const std::string none;
        return GetState() == State::Failed ? data_->error : none;
    }

    int Model::Triangles() const { return GetState() == State::Ready ? data_->triangles : 0; }
    int Model::Vertices() const { return GetState() == State::Ready ? (int)data_->vertexCount : 0; }
    int Model::Joints() const { return GetState() == State::Ready ? data_->jointCount : 0; }
    int Model::Meshes() const { return GetState() == State::Ready ? (int)data_->verts.size() : 0; }
    int Model::Animations() const { return GetState() == State::Ready ? (int)data_->anims.size() : 0; }

    const std::string& Model::AnimationName(int i) const
    {
        static const std::string none;
        return i >= 0 && i < Animations() ? data_->anims[(std::size_t)i].name : none;
    }

    float Model::AnimationLength(int i) const { return i >= 0 && i < Animations() ? data_->anims[(std::size_t)i].length : 0.0f; }

    float Model::Height() const { return GetState() == State::Ready ? data_->hi[1] - data_->lo[1] : 0.0f; }

    float Model::StanceSpeed(int anim)
    {
        if (anim < 0 || anim >= Animations())
            return 0.0f;
        const Data& d = *data_;
        std::vector<int> feet;
        for (std::size_t i = 0; i < d.nodes.size(); ++i)
            if (d.nodes[i].name.find("Foot") != std::string::npos || d.nodes[i].name.find("foot") != std::string::npos)
                feet.push_back((int)i);
        if (feet.empty())
            return 0.0f;
        // the feet over the cycle (world positions: y up, z forward), then the backward speed of the lowest one while
        // it is within a step's lift of the ground
        constexpr int kSamples = 96;
        const float length = std::max(d.anims[(std::size_t)anim].length, 1e-3f);
        std::vector<float> trs, world;
        std::vector<float> ys((std::size_t)kSamples * feet.size()), zs(ys.size());
        float ground = 1e30f;
        for (int s = 0; s < kSamples; ++s)
        {
            SampleAnim(d, anim, length * (float)s / (float)kSamples, trs);
            World(d, trs, world);
            for (std::size_t f = 0; f < feet.size(); ++f)
            {
                ys[(std::size_t)s * feet.size() + f] = world[(std::size_t)feet[f] * 16 + 13];
                zs[(std::size_t)s * feet.size() + f] = world[(std::size_t)feet[f] * 16 + 14];
                ground = std::min(ground, ys[(std::size_t)s * feet.size() + f]);
            }
        }
        const float lift = 0.01f * std::max(Height(), 1e-3f);
        std::vector<float> speeds;
        for (int s = 0; s < kSamples; ++s)
            for (std::size_t f = 0; f < feet.size(); ++f)
            {
                const std::size_t a = (std::size_t)s * feet.size() + f, b = (std::size_t)((s + 1) % kSamples) * feet.size() + f;
                if (ys[a] - ground < lift && ys[b] - ground < lift)
                    speeds.push_back((zs[a] - zs[b]) * (float)kSamples / length);
            }
        if (speeds.empty())
            return 0.0f;
        std::nth_element(speeds.begin(), speeds.begin() + (std::ptrdiff_t)(speeds.size() / 2), speeds.end());
        return std::max(speeds[speeds.size() / 2], 0.0f);
    }

    int Model::FindAnimation(std::string_view part) const
    {
        for (int i = 0; i < Animations(); ++i)
            if (data_->anims[(std::size_t)i].name.find(part) != std::string::npos)
                return i;
        return -1;
    }

    void Model::ShowMesh(std::string_view part, bool shown)
    {
        const auto it = std::find(hidden_.begin(), hidden_.end(), part);
        if (!shown && it == hidden_.end())
            hidden_.emplace_back(part);
        else if (shown && it != hidden_.end())
            hidden_.erase(it);
    }

    void Model::Pose(int anim, float time, int from, float fromTime, float blend)
    {
        if (GetState() != State::Ready)
            return;
        const Data& d = *data_;
        SampleAnim(d, anim, time, local_);
        if (blend < 1.0f && from != anim)
        {
            std::vector<float>& other = depth_;   // a scratch array as long as the nodes' transforms
            SampleAnim(d, from, fromTime, other);
            const float k = std::clamp(blend, 0.0f, 1.0f);
            for (std::size_t i = 0; i < d.nodes.size(); ++i)
            {
                float mixed[10];
                MixTrs(mixed, &other[i * 10], &local_[i * 10], k);
                std::memcpy(&local_[i * 10], mixed, sizeof(mixed));
            }
        }
        World(d, local_, world_);
        // only the vertex sets that are drawn
        std::vector<bool> shown(d.verts.size(), true);
        for (std::size_t i = 0; i < d.verts.size(); ++i)
            for (const std::string& h : hidden_)
                if (d.verts[i].name.find(h) != std::string::npos)
                    shown[i] = false;
        Skin(d, world_, joints_, posed_, &shown, &posedNormals_);
    }

    bool Model::Where(std::string_view part, Vec2& at) const
    {
        if (GetState() != State::Ready || world_.empty())
            return false;
        const Data& d = *data_;
        for (std::size_t i = 0; i < d.nodes.size(); ++i)
            if (d.nodes[i].name.find(part) != std::string::npos)
            {
                const float x = world_[i * 16 + 12] - view_[6], y = world_[i * 16 + 13] - viewLo_, z = world_[i * 16 + 14] - viewZ_;
                const float x1 = view_[3] * x + view_[4] * z, z1 = -view_[4] * x + view_[3] * z;
                const float y2 = view_[5] * y - view_[7] * z1;
                at = Vec2(view_[0] + x1 * view_[2], view_[1] - y2 * view_[2]);
                return true;
            }
        return false;
    }

    void Model::Draw(esia::DrawList& dl, const Rect& box, float yaw, float pitch, std::uint32_t color, float pixel, const Mask* mask)
    {
        bounds_ = Rect();
        if (GetState() != State::Ready)
            return;
        if (posed_.empty())
            Pose(-1, 0.0f);
        const Data& d = *data_;
        const float height = std::max(d.hi[1] - d.lo[1], 1e-4f);
        const float scale = box.Height() / height;
        const float mx = (d.lo[0] + d.hi[0]) * 0.5f, mz = (d.lo[2] + d.hi[2]) * 0.5f;
        const float cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
        const float ox = box.Center().x, oy = box.max.y;
        const float view[8] = {ox, oy, scale, cy, sy, cp, mx, sp};
        std::memcpy(view_, view, sizeof(view));
        viewZ_ = mz;
        viewLo_ = d.lo[1];
        // the vertices of the sets that are drawn onto the screen: turned about the upright through its middle, tilted
        // towards the viewer
        screen_.resize(d.vertexCount);
        depth_.resize(d.vertexCount);
        const bool lightable = posedNormals_.size() >= (std::size_t)d.vertexCount * 3;
        shade_.resize(d.vertexCount);
        for (const Data::Verts& v : d.verts)
        {
            bool hidden = false;
            for (const std::string& h : hidden_)
                hidden |= v.name.find(h) != std::string::npos;
            if (hidden)
                continue;
            const std::uint32_t end = v.first + (std::uint32_t)(v.pos.size() / 3);
            for (std::uint32_t i = v.first; i < end; ++i)
            {
                const float x = posed_[(std::size_t)i * 3] - mx, y = posed_[(std::size_t)i * 3 + 1] - d.lo[1], z = posed_[(std::size_t)i * 3 + 2] - mz;
                const float x1 = cy * x + sy * z, z1 = -sy * x + cy * z;
                const float y2 = cp * y - sp * z1, z2 = sp * y + cp * z1;
                screen_[i] = Vec2(ox + x1 * scale, oy - y2 * scale);
                depth_[i] = z2;
            }
            // its light (lit sets): a studio's - a key from the top left in front, the sky above, a soft fill - on the
            // normal turned as the vertex is
            if (!v.lit || v.nrm.empty() || !lightable)
                continue;
            constexpr float kLx = -0.42f, kLy = 0.62f, kLz = 0.66f;   // towards the key light (unit)
            for (std::uint32_t i = v.first; i < end; ++i)
            {
                const float nx = posedNormals_[(std::size_t)i * 3], ny = posedNormals_[(std::size_t)i * 3 + 1], nz = posedNormals_[(std::size_t)i * 3 + 2];
                float x1 = cy * nx + sy * nz, z1 = -sy * nx + cy * nz;
                float y2 = cp * ny - sp * z1, z2 = sp * ny + cp * z1;
                const float l = std::sqrt(x1 * x1 + y2 * y2 + z2 * z2);
                if (l > 1e-8f)
                {
                    x1 /= l;
                    y2 /= l;
                    z2 /= l;
                }
                const float key = std::max(x1 * kLx + y2 * kLy + z2 * kLz, 0.0f);
                shade_[i] = 0.50f + 0.62f * key + 0.16f * (0.5f + 0.5f * y2);
            }
        }
        // the triangles facing the viewer (both sides of two-sided ones), far ones first. A primitive later in a vertex
        // set than another (eyes on a face, a mouth) is drawn as if a little nearer: lying on the other, it stays over it
        tris_.resize(d.triangleCount);
        std::size_t drawn = 0;
        front_.assign(d.triangleCount, 0);
        const float decal = 0.004f * height * 3.0f;   // in the sum of three depths
        int lastSet = -1, inSet = 0;
        float dmin = 1e30f, dmax = -1e30f;
        for (std::uint32_t pi = 0; pi < (std::uint32_t)d.prims.size(); ++pi)
        {
            const Data::Prim& p = d.prims[pi];
            const Data::Verts& v = d.verts[(std::size_t)p.verts];
            inSet = p.verts == lastSet ? inSet + 1 : 0;
            lastSet = p.verts;
            const float bias = decal * (float)inSet;
            bool hidden = false;
            for (const std::string& h : hidden_)
                hidden |= v.name.find(h) != std::string::npos;
            if (hidden)
                continue;
            for (std::size_t t = 0; t + 2 < p.idx.size(); t += 3)
            {
                const std::uint32_t a = v.first + p.idx[t], b = v.first + p.idx[t + 1], c = v.first + p.idx[t + 2];
                const Vec2 e1 = screen_[b] - screen_[a], e2 = screen_[c] - screen_[a];
                if (!p.twoSided && e1.x * e2.y - e1.y * e2.x >= 0.0f)
                    continue;
                front_[v.firstTri + p.firstTri + (std::uint32_t)(t / 3)] = 1;
                const float depth = depth_[a] + depth_[b] + depth_[c] + bias;
                dmin = std::min(dmin, depth);
                dmax = std::max(dmax, depth);
                tris_[drawn++] = {depth, a, b, c, (pi << 20) | (std::uint32_t)t, 0u};
            }
        }
        tris_.resize(drawn);
        if (tris_.empty())
            return;
        // the outline: an edge with nothing drawn across it
        std::uint32_t fringes = 0;
        for (Tri& t : tris_)
        {
            const Data::Prim& p = d.prims[t.uv >> 20];
            const Data::Verts& v = d.verts[(std::size_t)p.verts];
            const std::size_t local = p.firstTri + (t.uv & 0xFFFFFu) / 3;
            for (int e = 0; e < 3; ++e)
            {
                const std::int32_t o = v.across[local * 3 + (std::size_t)e];
                if (o < 0 || !front_[v.firstTri + (std::uint32_t)o])
                {
                    t.edges |= 1u << e;
                    ++fringes;
                }
            }
        }
        // far to near: their depths in 16 bits over the range there is, sorted in two passes of a byte (stable: equal
        // depths keep the primitives' order)
        keys_.resize(drawn);
        order_.resize(drawn);
        spare_.resize(drawn);
        const float toKey = dmax > dmin ? 65535.0f / (dmax - dmin) : 0.0f;
        std::uint32_t count[256];
        std::fill(std::begin(count), std::end(count), 0u);
        for (std::size_t i = 0; i < drawn; ++i)
        {
            keys_[i] = (std::uint16_t)((tris_[i].depth - dmin) * toKey);
            ++count[keys_[i] & 0xFFu];
        }
        for (std::uint32_t i = 0, sum = 0; i < 256; ++i)
        {
            const std::uint32_t c = count[i];
            count[i] = sum;
            sum += c;
        }
        for (std::size_t i = 0; i < drawn; ++i)
            spare_[count[keys_[i] & 0xFFu]++] = (std::uint32_t)i;
        std::fill(std::begin(count), std::end(count), 0u);
        for (std::size_t i = 0; i < drawn; ++i)
            ++count[keys_[i] >> 8];
        for (std::uint32_t i = 0, sum = 0; i < 256; ++i)
        {
            const std::uint32_t c = count[i];
            count[i] = sum;
            sum += c;
        }
        for (std::size_t i = 0; i < drawn; ++i)
            order_[count[keys_[spare_[i]] >> 8]++] = spare_[i];
        // each primitive's material color times `color`, channel by channel
        colors_.resize(d.prims.size());
        for (std::size_t pi = 0; pi < d.prims.size(); ++pi)
        {
            std::uint32_t col = 0;
            for (int s = 0; s < 32; s += 8)
                col |= ((((d.prims[pi].color >> s) & 0xFFu) * ((color >> s) & 0xFFu) + 127u) / 255u) << s;
            colors_[pi] = col;
        }
        // the mask at a point: bilinear between its cells' centers
        const bool masked = mask && mask->cells && mask->w > 0 && mask->h > 0 && !mask->area.Empty();
        const float mcw = masked ? mask->area.Width() / (float)mask->w : 1.0f, mch = masked ? mask->area.Height() / (float)mask->h : 1.0f;
        const auto maskAt = [&](Vec2 q) {
            const float fx = (q.x - mask->area.min.x) / mcw - 0.5f, fy = (q.y - mask->area.min.y) / mch - 0.5f;
            if (fx < -1.0f || fy < -1.0f || fx > (float)mask->w || fy > (float)mask->h)
                return mask->outside;
            const int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
            const float tx = fx - (float)x0, ty = fy - (float)y0;
            const auto cell = [&](int x, int y) {
                return x < 0 || y < 0 || x >= mask->w || y >= mask->h ? mask->outside : mask->cells[(std::size_t)y * (std::size_t)mask->w + (std::size_t)x];
            };
            const float top = cell(x0, y0) + (cell(x0 + 1, y0) - cell(x0, y0)) * tx;
            const float bottom = cell(x0, y0 + 1) + (cell(x0 + 1, y0 + 1) - cell(x0, y0 + 1)) * tx;
            float m = std::clamp(top + (bottom - top) * ty, 0.0f, 1.0f);
            if (mask->hard)
            {
                m = std::clamp((m - 0.3f) / 0.4f, 0.0f, 1.0f);
                m = m * m * (3.0f - 2.0f * m);
            }
            return m;
        };
        // Each primitive's vertices go out once - those its triangles use, in their order - and its triangles index
        // them: the GPU gets a vertex for each vertex, not one for each triangle's corner. The triangles keep their
        // order and their corners' order. Masked, a triangle whose three corners are at alpha 0 is left out with its
        // fringes (drawn, it would change no pixel): its room ends as degenerate triangles, as unused room did. The
        // bounds stay those of every triangle facing the viewer.
        constexpr std::uint32_t kNone = 0xFFFFFFFFu, kUsed = 0xFFFFFFFEu;
        if (usedFirst_.size() != d.prims.size() + 1)
        {
            usedFirst_.assign(1, 0u);
            remapFirst_.assign(1, 0u);
            usedVtx_.clear();
            remap_.clear();
            for (const Data::Prim& p : d.prims)
            {
                const std::size_t at = remap_.size(), count = d.verts[(std::size_t)p.verts].pos.size() / 3;
                remap_.resize(at + count, kNone);
                for (const std::uint32_t i : p.idx)
                    remap_[at + i] = kUsed;
                for (std::uint32_t i = 0; i < (std::uint32_t)count; ++i)
                    if (remap_[at + i] == kUsed)
                    {
                        remap_[at + i] = (std::uint32_t)usedVtx_.size() - usedFirst_.back();
                        usedVtx_.push_back(i);
                    }
                usedFirst_.push_back((std::uint32_t)usedVtx_.size());
                remapFirst_.push_back((std::uint32_t)remap_.size());
            }
        }
        // the primitives with a triangle drawn, where their vertices go
        primOut_.assign(d.prims.size(), kNone);
        for (const Tri& t : tris_)
            primOut_[t.uv >> 20] = 0u;
        std::uint32_t nv = 0;
        for (std::size_t pi = 0; pi < d.prims.size(); ++pi)
            if (primOut_[pi] != kNone)
            {
                primOut_[pi] = nv;
                nv += usedFirst_[pi + 1] - usedFirst_[pi];
            }
        const std::uint32_t roomV = nv + fringes * 2, roomI = (std::uint32_t)tris_.size() * 3 + fringes * 6;
        esia::DrawList::PrimWriter w = dl.PrimReserve(d.atlas, roomI, roomV);
        // the vertices, primitive by primitive
        for (std::size_t pi = 0; pi < d.prims.size(); ++pi)
        {
            if (primOut_[pi] == kNone)
                continue;
            const Data::Prim& p = d.prims[pi];
            const Data::Verts& v = d.verts[(std::size_t)p.verts];
            const std::uint32_t col = colors_[pi];
            const bool lit = !p.unlit && v.lit && !v.nrm.empty() && lightable;
            esia::Vertex* o = w.vtx + primOut_[pi];
            for (std::uint32_t j = usedFirst_[pi]; j < usedFirst_[pi + 1]; ++j, ++o)
            {
                const std::uint32_t local = usedVtx_[j], vi = v.first + local;
                const Vec2 pos = screen_[vi];
                std::uint32_t vc = col;
                if (lit)
                {
                    const float sh = shade_[vi];
                    vc = col & 0xFF000000u;
                    for (int ch = 0; ch < 24; ch += 8)
                        vc |= (std::uint32_t)std::min((int)((float)((col >> ch) & 0xFFu) * sh + 0.5f), 255) << ch;
                }
                if (masked)
                    vc = (vc & 0x00FFFFFFu) | ((std::uint32_t)std::lround((float)(col >> 24) * maskAt(pos)) << 24);
                *o = esia::Vertex{pos, p.uv[local], vc};
            }
        }
        // the triangles, far to near, each with its fringes
        float bx0 = 1e30f, by0 = 1e30f, bx1 = -1e30f, by1 = -1e30f;
        std::uint32_t k = nv, ki = 0;
        for (const std::uint32_t ti : order_)
        {
            const Tri& t = tris_[ti];
            const std::uint32_t pi = t.uv >> 20;
            const std::uint32_t first = d.verts[(std::size_t)d.prims[pi].verts].first, out = primOut_[pi];
            const std::uint32_t* const remap = remap_.data() + remapFirst_[pi];
            const std::uint32_t vi[3] = {t.a, t.b, t.c};
            std::uint32_t corner[3];
            for (int q = 0; q < 3; ++q)
            {
                const Vec2 pos = screen_[vi[q]];
                bx0 = std::min(bx0, pos.x);
                by0 = std::min(by0, pos.y);
                bx1 = std::max(bx1, pos.x);
                by1 = std::max(by1, pos.y);
                corner[q] = out + remap[vi[q] - first];
            }
            if (masked && ((w.vtx[corner[0]].color | w.vtx[corner[1]].color | w.vtx[corner[2]].color) >> 24) == 0)
                continue;
            for (int q = 0; q < 3; ++q)
                w.idx[ki++] = w.base + corner[q];
            // a fringe out of each outline edge: its colors at the edge, fading out over a pixel
            for (int e = 0; e < 3; ++e)
            {
                if (!(t.edges & (1u << e)))
                    continue;
                const std::uint32_t ia = corner[e], ib = corner[(e + 1) % 3], ic = corner[(e + 2) % 3];
                const Vec2 pa = w.vtx[ia].pos, pb = w.vtx[ib].pos, pc = w.vtx[ic].pos;
                Vec2 nrm(pb.y - pa.y, pa.x - pb.x);
                const float len = std::sqrt(nrm.x * nrm.x + nrm.y * nrm.y);
                if (len < 1e-6f)
                    continue;
                nrm = nrm * (1.0f / len);
                if ((pc.x - pa.x) * nrm.x + (pc.y - pa.y) * nrm.y > 0.0f)
                    nrm = nrm * -1.0f;   // away from the triangle
                const Vec2 off = nrm * pixel;
                w.vtx[k] = esia::Vertex{pa + off, w.vtx[ia].uv, w.vtx[ia].color & 0x00FFFFFFu};
                w.vtx[k + 1] = esia::Vertex{pb + off, w.vtx[ib].uv, w.vtx[ib].color & 0x00FFFFFFu};
                const std::uint32_t qa = w.base + ia, qb = w.base + ib, qa2 = w.base + k, qb2 = w.base + k + 1;
                w.idx[ki++] = qa;
                w.idx[ki++] = qb;
                w.idx[ki++] = qb2;
                w.idx[ki++] = qa;
                w.idx[ki++] = qb2;
                w.idx[ki++] = qa2;
                k += 2;
            }
        }
        const Rect bounds = Rect(bx0, by0, bx1, by1).Expanded(pixel);   // the fringes: a pixel out
        // the unused room (fringes of edges too short to have a direction, triangles left out)
        for (; k < roomV; ++k)
            w.vtx[k] = esia::Vertex{w.vtx[0].pos, w.vtx[0].uv, 0u};
        for (; ki < roomI; ++ki)
            w.idx[ki] = w.base;
        dl.PrimCommit(w, roomI, bounds);
        bounds_ = bounds;
    }

    Rect Model::Sample(float cell, std::vector<Point>& out, int& gw, int& gh) const
    {
        out.clear();
        gw = gh = 0;
        if (GetState() != State::Ready || tris_.empty() || order_.empty() || bounds_.Empty() || cell <= 0.0f || data_->atlasPixels.empty())
            return Rect();
        const Data& d = *data_;
        gw = std::max((int)std::ceil(bounds_.Width() / cell), 1);
        gh = std::max((int)std::ceil(bounds_.Height() / cell), 1);
        const Rect b(bounds_.min, bounds_.min + Vec2((float)gw * cell, (float)gh * cell));
        std::vector<std::uint32_t> grid((std::size_t)gw * (std::size_t)gh, 0u);
        // the triangles far to near onto the cells' centers: the nearest one there last
        for (const std::uint32_t ti : order_)
        {
            const Tri& t = tris_[ti];
            const Data::Prim& p = d.prims[t.uv >> 20];
            const Data::Verts& v = d.verts[(std::size_t)p.verts];
            const Vec2 A = screen_[t.a], B = screen_[t.b], C = screen_[t.c];
            const float area = (B.x - A.x) * (C.y - A.y) - (B.y - A.y) * (C.x - A.x);
            if (std::fabs(area) < 1e-6f)
                continue;
            const float minX = std::min(A.x, std::min(B.x, C.x)), maxX = std::max(A.x, std::max(B.x, C.x));
            const float minY = std::min(A.y, std::min(B.y, C.y)), maxY = std::max(A.y, std::max(B.y, C.y));
            const int i0 = std::max((int)std::ceil((minX - b.min.x) / cell - 0.5f), 0), i1 = std::min((int)std::floor((maxX - b.min.x) / cell - 0.5f), gw - 1);
            const int j0 = std::max((int)std::ceil((minY - b.min.y) / cell - 0.5f), 0), j1 = std::min((int)std::floor((maxY - b.min.y) / cell - 0.5f), gh - 1);
            for (int j = j0; j <= j1; ++j)
                for (int i = i0; i <= i1; ++i)
                {
                    const Vec2 q(b.min.x + ((float)i + 0.5f) * cell, b.min.y + ((float)j + 0.5f) * cell);
                    const float w0 = ((B.x - q.x) * (C.y - q.y) - (B.y - q.y) * (C.x - q.x)) / area;
                    const float w1 = ((C.x - q.x) * (A.y - q.y) - (C.y - q.y) * (A.x - q.x)) / area;
                    const float w2 = 1.0f - w0 - w1;
                    if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f)
                        continue;
                    const Vec2 uv = p.uv[t.a - v.first] * w0 + p.uv[t.b - v.first] * w1 + p.uv[t.c - v.first] * w2;
                    const int tx = std::clamp((int)(uv.x * (float)d.atlasW), 0, d.atlasW - 1), ty = std::clamp((int)(uv.y * (float)d.atlasH), 0, d.atlasH - 1);
                    const std::uint8_t* px = &d.atlasPixels[((std::size_t)ty * (std::size_t)d.atlasW + (std::size_t)tx) * 4];
                    if (px[3] < 128)
                        continue;   // see-through there: what is behind it shows
                    const bool lit = !p.unlit && v.lit && !v.nrm.empty() && shade_.size() >= d.vertexCount &&
                                     posedNormals_.size() >= (std::size_t)d.vertexCount * 3;
                    const float sh = lit ? shade_[t.a] * w0 + shade_[t.b] * w1 + shade_[t.c] * w2 : 1.0f;
                    std::uint32_t c = 0xFF000000u;
                    for (int ch = 0; ch < 3; ++ch)
                        c |= (std::uint32_t)std::min((int)((float)((std::uint32_t)px[ch] * ((p.color >> (8 * ch)) & 0xFFu) / 255u) * sh + 0.5f), 255)
                             << (8 * ch);
                    grid[(std::size_t)j * (std::size_t)gw + (std::size_t)i] = c;
                }
        }
        for (int j = 0; j < gh; ++j)
            for (int i = 0; i < gw; ++i)
                if (const std::uint32_t c = grid[(std::size_t)j * (std::size_t)gw + (std::size_t)i])
                    out.push_back({Vec2(b.min.x + ((float)i + 0.5f) * cell, b.min.y + ((float)j + 0.5f) * cell), c, j * gw + i});
        return b;
    }
}
