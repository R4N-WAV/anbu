// Runs the real AnbuProcessor over raw float32 stereo (48 kHz) and writes raw float32 stereo.
// usage: AnbuRender in.f32 out.f32 key=value ...   (keys are parameter IDs, values in plain units)
//        block=<n> sets the host block size (default 512). Output is latency-compensated.
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include <cstdio>
#include <vector>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 3) { std::fprintf (stderr, "usage: AnbuRender in.f32 out.f32 [id=value ...]\n"); return 1; }
    std::vector<float> in;
    {
        FILE* f = std::fopen (argv[1], "rb"); if (! f) return 2;
        float b[4096]; size_t r; while ((r = std::fread (b, 4, 4096, f)) > 0) in.insert (in.end(), b, b + r);
        std::fclose (f);
    }
    const int n = (int) in.size() / 2;
    AnbuProcessor p;
    int block = 512; double fs = 48000.0;
    for (int i = 3; i < argc; ++i)
    {
        juce::String a (argv[i]); auto k = a.upToFirstOccurrenceOf ("=", false, false); auto v = a.fromFirstOccurrenceOf ("=", false, false);
        if (k == "block") { block = v.getIntValue(); continue; }
        if (auto* prm = dynamic_cast<juce::RangedAudioParameter*> (p.apvts.getParameter (k)))
            prm->setValueNotifyingHost (prm->convertTo0to1 (v.getFloatValue()));
        else std::fprintf (stderr, "unknown parameter %s\n", k.toRawUTF8());
    }
    p.setPlayConfigDetails (2, 2, fs, block);
    p.prepareToPlay (fs, block);
    juce::MidiBuffer midi;
    // warm-up with silence so the parameter smoothers settle, then measure the latency
    {
        juce::AudioBuffer<float> z (2, block); z.clear();
        for (int i = 0; i < 64; ++i) { z.clear(); p.processBlock (z, midi); }
    }
    const int lat = p.getLatencySamples();
    std::vector<float> out ((size_t) (n + lat) * 2, 0.0f);
    juce::AudioBuffer<float> buf (2, block);
    for (int pos = 0; pos < n + lat; pos += block)
    {
        const int m = juce::jmin (block, n + lat - pos);
        buf.setSize (2, m, false, false, true);
        for (int i = 0; i < m; ++i)
        {
            const int s = pos + i;
            buf.setSample (0, i, s < n ? in[(size_t) s * 2] : 0.0f);
            buf.setSample (1, i, s < n ? in[(size_t) s * 2 + 1] : 0.0f);
        }
        p.processBlock (buf, midi);
        for (int i = 0; i < m; ++i) { out[(size_t) (pos + i) * 2] = buf.getSample (0, i); out[(size_t) (pos + i) * 2 + 1] = buf.getSample (1, i); }
    }
    FILE* o = std::fopen (argv[2], "wb");
    std::fwrite (out.data() + (size_t) lat * 2, 4, (size_t) n * 2, o);
    std::fclose (o);
    std::fprintf (stderr, "latency %d samples\n", lat);
    return 0;
}
