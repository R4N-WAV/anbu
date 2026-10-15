// Height-field panel renderer for the Anbu faceplate (offline asset tool, not part of the plugin).
// Orthographic, frontal view. Inputs are raw float32 maps written by scene.py:
//   h (height, design units), alb (3, linear), rough, metal, emis (3, linear), aniso, tx, ty
// Lighting: one soft key light from the upper left (shadowed, soft), a hemispherical fill with
// ambient occlusion, and a procedural studio environment for reflections (GGX, importance sampled,
// anisotropic for spun metal). Output: linear RGB float32.
//
// usage: render <dir> <W> <H> <k px per unit> [samples]
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>
#include <algorithm>
#include <thread>
#include <atomic>

struct V3 { float x, y, z; };
static inline V3 operator+ (V3 a, V3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
static inline V3 operator- (V3 a, V3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
static inline V3 operator* (V3 a, float s) { return { a.x * s, a.y * s, a.z * s }; }
static inline V3 mul (V3 a, V3 b) { return { a.x * b.x, a.y * b.y, a.z * b.z }; }
static inline float dot (V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline V3 norm (V3 a) { float l = std::sqrt (dot (a, a)); return l > 0 ? a * (1.0f / l) : a; }
static inline V3 cross (V3 a, V3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
static inline float clampf (float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static inline float smooth (float e0, float e1, float x) { float t = clampf ((x - e0) / (e1 - e0), 0, 1); return t * t * (3 - 2 * t); }

static int W, H; static float K;
static std::vector<float> hmap, alb, rough, metal, emis, aniso, tx, ty;

static bool load (const std::string& path, std::vector<float>& v, size_t n)
{
    v.resize (n);
    FILE* f = std::fopen (path.c_str(), "rb");
    if (! f) return false;
    size_t r = std::fread (v.data(), 4, n, f);
    std::fclose (f);
    return r == n;
}

static inline float hAt (float x, float y)
{
    x = clampf (x, 0, (float) W - 1.001f); y = clampf (y, 0, (float) H - 1.001f);
    int x0 = (int) x, y0 = (int) y; float fx = x - x0, fy = y - y0;
    const float* p = hmap.data() + (size_t) y0 * W + x0;
    return (p[0] * (1 - fx) + p[1] * fx) * (1 - fy) + (p[W] * (1 - fx) + p[W + 1] * fx) * fy;
}

// key light (towards the light): upper left, 45 degrees up
static const V3 Lkey = norm ({ -0.5f, -0.5f, 0.7071f });
static const float keyRad = 0.11f;   // angular radius (rad) -> soft shadows
static const V3 Lrim = norm ({ 0.9f, 0.05f, 0.45f });

// studio environment radiance (z = towards the viewer, y = down the screen)
static inline V3 env (V3 d, float keyVis)
{
    float base = 0.012f + 0.07f * clampf (-d.y, 0, 1) + 0.008f * clampf (d.z, 0, 1);
    float k = dot (d, Lkey);
    float key = smooth (std::cos (0.42f), std::cos (0.30f), k) * 3.2f * keyVis;
    float rim = smooth (std::cos (0.30f), std::cos (0.18f), dot (d, Lrim)) * 1.1f;
    float top = smooth (0.75f, 0.95f, -d.y) * 0.22f;   // ceiling softbox
    V3 warm { 1.0f, 0.96f, 0.9f };
    return warm * (base + key + top) + V3 { 0.95f, 0.97f, 1.0f } * rim;
}

static inline uint32_t hash (uint32_t x) { x ^= x >> 16; x *= 0x7feb352d; x ^= x >> 15; x *= 0x846ca68b; x ^= x >> 16; return x; }
static inline float rnd (uint32_t& s) { s = hash (s + 0x9e3779b9u); return (s >> 8) * (1.0f / 16777216.0f); }

int main (int argc, char** argv)
{
    if (argc < 5) { std::fprintf (stderr, "usage: render dir W H k [samples]\n"); return 1; }
    std::string dir = argv[1];
    W = std::atoi (argv[2]); H = std::atoi (argv[3]); K = (float) std::atof (argv[4]);
    const int S = argc > 5 ? std::atoi (argv[5]) : 24;
    const size_t N = (size_t) W * H;
    if (! load (dir + "/h.f32", hmap, N) || ! load (dir + "/alb.f32", alb, N * 3) || ! load (dir + "/rough.f32", rough, N)
        || ! load (dir + "/metal.f32", metal, N) || ! load (dir + "/emis.f32", emis, N * 3) || ! load (dir + "/aniso.f32", aniso, N)
        || ! load (dir + "/tx.f32", tx, N) || ! load (dir + "/ty.f32", ty, N))
    { std::fprintf (stderr, "missing maps in %s\n", dir.c_str()); return 2; }

    std::vector<float> out (N * 3, 0.0f);
    const float tanKey = Lkey.z / std::sqrt (Lkey.x * Lkey.x + Lkey.y * Lkey.y);
    const float elevKey = std::atan (tanKey);
    const V3 up = norm ({ 0.0f, -0.3f, 0.95f });
    const float maxShadow = 40.0f * K;       // px
    const float aoRange = 14.0f * K;
    const int aoDirs = 12;

    auto work = [&] (int y0, int y1)
    {
        for (int y = y0; y < y1; ++y)
            for (int x = 0; x < W; ++x)
            {
                const size_t i = (size_t) y * W + x;
                const float h0 = hmap[i];
                // normal from central differences (height in units, distance in px / K)
                const float dhx = (hAt (x + 1.0f, (float) y) - hAt (x - 1.0f, (float) y)) * 0.5f * K;
                const float dhy = (hAt ((float) x, y + 1.0f) - hAt ((float) x, y - 1.0f)) * 0.5f * K;
                const V3 n = norm ({ -dhx, -dhy, 1.0f });

                // soft key shadow: horizon elevation towards the light
                float hor = -1.0f;
                {
                    const float dx = Lkey.x, dy = Lkey.y, dl = std::sqrt (dx * dx + dy * dy);
                    const float sx = dx / dl, sy = dy / dl;
                    float t = 1.0f;
                    while (t < maxShadow)
                    {
                        const float hh = hAt (x + sx * t, y + sy * t) - h0;
                        hor = std::max (hor, hh * K / t);
                        t += t < 8 ? 0.5f : (t < 40 ? 1.0f : 2.0f);
                    }
                }
                const float vis = smooth (-keyRad, keyRad, elevKey - std::atan (hor));

                // ambient occlusion: horizon in several directions
                float ao = 0.0f;
                for (int d = 0; d < aoDirs; ++d)
                {
                    const float a = (d + 0.5f) * 6.2831853f / aoDirs;
                    const float sx = std::cos (a), sy = std::sin (a);
                    float m = 0.0f;
                    for (float t = 1.0f; t < aoRange; t += t < 6 ? 1.0f : 2.0f)
                        m = std::max (m, (hAt (x + sx * t, y + sy * t) - h0) * K / t);
                    const float s = m / std::sqrt (1 + m * m);   // sin of horizon angle
                    ao += 1.0f - s;
                }
                ao /= aoDirs;
                ao = std::pow (clampf (ao, 0, 1), 1.5f);

                const V3 A { alb[i * 3], alb[i * 3 + 1], alb[i * 3 + 2] };
                const float r = clampf (rough[i], 0.04f, 1.0f), m = metal[i];
                const V3 F0 = V3 { 0.04f, 0.04f, 0.04f } * (1 - m) + A * m;

                // diffuse: key + hemispherical fill
                const float ndl = std::max (0.0f, dot (n, Lkey));
                const float fill = 0.34f * (0.55f + 0.45f * std::max (0.0f, dot (n, up))) * ao;
                V3 col = A * ((1 - m) * (0.92f * ndl * vis + fill));

                // specular: GGX importance sampling of the environment
                const V3 v { 0, 0, 1 };
                V3 t1, t2;
                const bool an = aniso[i] > 0.5f;
                if (an) { t1 = norm ({ tx[i], ty[i], 0.0f }); t1 = norm (t1 - n * dot (t1, n)); t2 = cross (n, t1); }
                else { V3 a = std::fabs (n.x) < 0.9f ? V3 { 1, 0, 0 } : V3 { 0, 1, 0 }; t1 = norm (cross (a, n)); t2 = cross (n, t1); }
                const float ax = an ? std::max (0.06f, 0.5f * r * r) : r * r, ay = an ? std::max (0.25f, r * r) : r * r;
                uint32_t seed = hash ((uint32_t) i * 9781u + 17u);
                V3 spec { 0, 0, 0 };
                const float nv = std::max (1e-3f, dot (n, v));
                for (int s = 0; s < S; ++s)
                {
                    const float u1 = (s + rnd (seed)) / S, u2 = rnd (seed);
                    // sample the half vector (anisotropic GGX)
                    const float phi = std::atan2 (ay * std::sin (6.2831853f * u2), ax * std::cos (6.2831853f * u2));
                    const float cp = std::cos (phi), sp = std::sin (phi);
                    const float a2 = 1.0f / (cp * cp / (ax * ax) + sp * sp / (ay * ay));
                    const float tan2 = a2 * u1 / std::max (1e-6f, 1 - u1);
                    const float cth = 1.0f / std::sqrt (1 + tan2), sth = std::sqrt (std::max (0.0f, 1 - cth * cth));
                    const V3 hv = norm (t1 * (sth * cp) + t2 * (sth * sp) + n * cth);
                    const float vh = dot (v, hv);
                    if (vh <= 0) continue;
                    const V3 l = hv * (2 * vh) - v;
                    const float nl = dot (n, l);
                    if (nl <= 0) continue;
                    if (l.z <= 0)   // looks down at the faceplate: a dim red reflection
                    {
                        const float fw0 = std::pow (1 - vh, 5.0f);
                        const V3 F = F0 + (V3 { 1, 1, 1 } - F0) * fw0;
                        spec = spec + mul (F, V3 { 0.07f, 0.018f, 0.016f });
                        continue;
                    }
                    const float nh = std::max (1e-4f, dot (n, hv));
                    const float k = (ax + ay) * 0.5f;
                    const float g = (nl / (nl * (1 - k) + k)) * (nv / (nv * (1 - k) + k));
                    const float fw = std::pow (1 - vh, 5.0f);
                    const V3 F = F0 + (V3 { 1, 1, 1 } - F0) * fw;
                    const float w = g * vh / (nv * nh);
                    const float kv = dot (l, Lkey) > std::cos (0.45f) ? vis : 1.0f;
                    spec = spec + mul (F, env (l, kv)) * w;
                }
                spec = spec * (1.0f / S) * (0.5f + 0.5f * ao);
                col = col + spec;
                col = col + V3 { emis[i * 3], emis[i * 3 + 1], emis[i * 3 + 2] };
                out[i * 3] = col.x; out[i * 3 + 1] = col.y; out[i * 3 + 2] = col.z;
            }
    };
    const int T = std::max (1u, std::thread::hardware_concurrency());
    std::vector<std::thread> th;
    // interleave bands so the threads finish together
    const int band = 16;
    std::atomic_int cursor { 0 };
    for (int t = 0; t < T; ++t)
        th.emplace_back ([&] { for (;;) { int b = cursor.fetch_add (1); int y0 = b * band; if (y0 >= H) break; work (y0, std::min (H, y0 + band)); } });
    for (auto& t : th) t.join();

    FILE* f = std::fopen ((dir + "/out.f32").c_str(), "wb");
    std::fwrite (out.data(), 4, out.size(), f);
    std::fclose (f);
    return 0;
}
