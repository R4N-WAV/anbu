// Offline check: run the plugin DSP core (no oversampling) over a raw float32 stereo file.
// usage: core_check in.f32 out.f32 pr limit color
#include "../Source/DSP/AnbuCore.h"
#include <cstdio>
#include <vector>
#include <cstdlib>

int main(int argc, char** argv)
{
    if (argc < 6) { std::fprintf(stderr, "usage: core_check in.f32 out.f32 pr limit color\n"); return 1; }
    FILE* f = std::fopen(argv[1], "rb");
    if (!f) return 2;
    std::vector<float> in; float buf[4096]; size_t r;
    while ((r = std::fread(buf, 4, 4096, f)) > 0) in.insert(in.end(), buf, buf + r);
    std::fclose(f);
    const int n = (int)in.size() / 2;
    std::vector<float> L(n), R(n);
    for (int i = 0; i < n; ++i) { L[i] = in[2 * i]; R[i] = in[2 * i + 1]; }
    anbu::Settings s; s.peakReduction = std::atof(argv[3]); s.limit = std::atoi(argv[4]) != 0; s.color = std::atof(argv[5]);
    const double fs = 48000.0;
    anbu::OptoControl ctl; anbu::AudioPath ap;
    ctl.prepare(fs); ap.prepare(fs, 1);
    const double tone = anbu::colorTone(s.color), kap = anbu::colorKappa(s.color);
    ctl.setSettings(s, tone); ap.setSettings(s, tone, kap);
    const int B = 256; std::vector<double> G(B), Gs(B);
    for (int i = 0; i < n; i += B) {
        const int m = std::min(B, n - i);
        ctl.process(&L[i], &R[i], m, G.data(), Gs.data());
        ap.process(&L[i], &R[i], m, G.data(), Gs.data());
    }
    FILE* o = std::fopen(argv[2], "wb");
    for (int i = 0; i < n; ++i) { float v[2] = { L[i], R[i] }; std::fwrite(v, 4, 2, o); }
    std::fclose(o);
    return 0;
}
