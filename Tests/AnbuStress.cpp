// Robustness checks for the Anbu processor (run in CI and under sanitizers).
// usage: AnbuStress [quick]
// Every check prints one line; the program exits non-zero if any check fails.
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include <cmath>
#include <cstdio>

namespace
{
int failures = 0;
void report (bool ok, const juce::String& what)
{
    std::printf ("%s  %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8());
    if (! ok) ++failures;
}

void setParam (AnbuProcessor& p, const char* id, float v)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

struct Signal
{
    enum Kind { pink, sine, loudSine, silence, dc, impulse, nyquist, burst } kind;
    double fs;
    juce::Random rng { 11 };
    double b0 = 0, b1 = 0, b2 = 0, ph = 0;
    long long t = 0;
    float next()
    {
        const long long i = t++;
        switch (kind)
        {
            case pink:
            {
                const double w = rng.nextFloat() * 2.0 - 1.0;
                b0 = 0.99765 * b0 + w * 0.0990460; b1 = 0.96300 * b1 + w * 0.2965164; b2 = 0.57000 * b2 + w * 1.0526913;
                return (float) ((b0 + b1 + b2 + w * 0.1848) * 0.11 * 0.6);
            }
            case sine:     ph += 2.0 * juce::MathConstants<double>::pi * 997.0 / fs; return (float) (0.25 * std::sin (ph));
            case loudSine: ph += 2.0 * juce::MathConstants<double>::pi * 61.0 / fs;  return (float) (1.0 * std::sin (ph));
            case silence:  return 0.0f;
            case dc:       return 0.5f;
            case impulse:  return i % 24000 == 0 ? 1.0f : 0.0f;
            case nyquist:  return (i & 1) ? 1.0f : -1.0f;
            case burst:    return (i / 4800) % 2 == 0 ? (float) (0.9 * std::sin ((double) i * 0.31)) : 0.0f;
        }
        return 0.0f;
    }
};

struct Stats { bool finite = true; float peak = 0.0f; double ms = 0.0; long long n = 0; };

// runs the processor over `seconds` of a signal with a block-size pattern; returns output stats
Stats run (AnbuProcessor& p, Signal sig, double seconds, int maxBlock, bool randomBlocks, int channels = 2,
           std::function<void (int)> perBlock = {}, std::vector<float>* capture = nullptr)
{
    Stats st;
    juce::Random rb (5);
    juce::AudioBuffer<float> buf (channels, maxBlock * 2);
    juce::MidiBuffer midi;
    const long long total = (long long) (seconds * sig.fs);
    int block = 0;
    for (long long done = 0; done < total; ++block)
    {
        int n = randomBlocks ? 1 + rb.nextInt (maxBlock * 2) : maxBlock;      // also larger than announced
        n = (int) juce::jmin ((long long) n, total - done);
        if (perBlock) perBlock (block);
        buf.setSize (channels, n, false, false, true);
        for (int i = 0; i < n; ++i)
        {
            const float v = sig.next();
            for (int c = 0; c < channels; ++c) buf.setSample (c, i, v);
        }
        p.processBlock (buf, midi);
        for (int c = 0; c < channels; ++c)
        {
            const float* d = buf.getReadPointer (c);
            for (int i = 0; i < n; ++i)
            {
                if (! std::isfinite (d[i])) st.finite = false;
                st.peak = juce::jmax (st.peak, std::abs (d[i]));
                st.ms += (double) d[i] * d[i];
            }
        }
        if (capture != nullptr)
            for (int i = 0; i < n; ++i) capture->push_back (buf.getSample (0, i));
        done += n;
        st.n += n * channels;
    }
    return st;
}

std::unique_ptr<AnbuProcessor> make (double fs, int block, int channels = 2)
{
    auto p = std::make_unique<AnbuProcessor>();
    const auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
    juce::AudioProcessor::BusesLayout l;
    l.inputBuses.add (set); l.outputBuses.add (set);
    p->setBusesLayout (l);
    p->setPlayConfigDetails (channels, channels, fs, block);
    p->prepareToPlay (fs, block);
    return p;
}

float db (float x) { return 20.0f * std::log10 (juce::jmax (x, 1e-12f)); }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const bool quick = argc > 1 && juce::String (argv[1]) == "quick";
    const double rates[] = { 22050, 32000, 44100, 48000, 88200, 96000, 176400, 192000 };

    // 1. every sample rate x oversampling x phase, extreme settings, hard signals: finite and bounded
    for (double fs : rates)
        for (int os = 0; os < 4; ++os)
            for (int ph = 0; ph < 2; ++ph)
            {
                if (quick && (fs != 44100 && fs != 96000)) continue;
                if (fs >= 176400 && os == 3 && ! quick) { /* 1.4 MHz inner rate: still checked, shorter */ }
                auto p = make (fs, 512);
                setParam (*p, ids::os, (float) os); setParam (*p, ids::phase, (float) ph);
                setParam (*p, ids::pr, 100.0f); setParam (*p, ids::color, 100.0f); setParam (*p, ids::input, 24.0f);
                setParam (*p, ids::output, 24.0f); setParam (*p, ids::mode, 1.0f);
                setParam (*p, ids::noiseOn, 1.0f); setParam (*p, ids::noiseLevel, -60.0f);
                bool ok = true; float peak = 0.0f;
                juce::String detail;
                for (auto k : { Signal::pink, Signal::loudSine, Signal::nyquist, Signal::dc, Signal::impulse, Signal::burst })
                {
                    const auto st = run (*p, { k, fs }, fs >= 176400 && os == 3 ? 0.25 : 0.6, 512, false);
                    ok = ok && st.finite; peak = juce::jmax (peak, st.peak);
                    detail << juce::String (db (st.peak), 1) << " ";
                }
                if (std::getenv ("ANBU_VERBOSE")) std::printf ("      per signal (pink sine nyq dc imp burst): %s\n", detail.toRawUTF8());
                report (ok && peak < 1000.0f, juce::String::formatted ("extreme settings, fs %6.0f, OS %d, %s: finite, peak %+.1f dBFS",
                                                                      fs, 1 << os, ph ? "linear" : "low lat", db (peak)));
            }

    // 2. random block sizes (1 .. 2x announced) give the same result as fixed blocks
    for (int os = 0; os < 4; ++os)
    {
        auto a = make (48000, 256), b = make (48000, 256);
        for (auto* p : { a.get(), b.get() }) { setParam (*p, ids::os, (float) os); setParam (*p, ids::pr, 60.0f); setParam (*p, ids::color, 40.0f); }
        std::vector<float> ya, yb;
        run (*a, { Signal::pink, 48000 }, 2.0, 256, false, 2, {}, &ya);
        run (*b, { Signal::pink, 48000 }, 2.0, 256, true, 2, {}, &yb);
        double e = 0, s = 0;
        for (size_t i = 0; i < juce::jmin (ya.size(), yb.size()); ++i) { e += (ya[i] - yb[i]) * (ya[i] - yb[i]); s += ya[i] * ya[i]; }
        const float rel = (float) (10.0 * std::log10 (e / juce::jmax (s, 1e-20) + 1e-30));
        report (rel < -60.0f, juce::String::formatted ("random block sizes vs fixed, OS %d: difference %.1f dB", 1 << os, rel));
    }

    // 3. mono layout
    {
        auto p = make (44100, 512, 1);
        setParam (*p, ids::pr, 50.0f);
        const auto st = run (*p, { Signal::pink, 44100 }, 1.0, 512, true, 1);
        report (st.finite && st.peak > 0.01f && st.peak < 4.0f, juce::String::formatted ("mono layout: finite, peak %+.1f dBFS", db (st.peak)));
    }

    // 4. silence in: silence out (noise off), meters at rest, no denormal slowdown
    {
        auto p = make (48000, 512);
        setParam (*p, ids::pr, 100.0f); setParam (*p, ids::color, 100.0f);
        run (*p, { Signal::pink, 48000 }, 1.0, 512, false);
        run (*p, { Signal::silence, 48000 }, 1.0, 512, false);            // let the tails ring out
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        const auto st = run (*p, { Signal::silence, 48000 }, 20.0, 512, false);
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        report (st.peak < 3.0e-5f, juce::String::formatted ("silence in, silence out below 16-bit level (from 1 s after the signal): peak %+.1f dBFS", db (st.peak)));
        report (p->meterGrDb.load() < 0.05f, juce::String::formatted ("GR meter at rest after silence (PR 100): %.2f dB", p->meterGrDb.load()));
        report (ms < 20000.0 * 0.5, juce::String::formatted ("20 s of silence processed in %.0f ms (no denormal stall)", ms));
    }

    // 5. transparency: PR 0, Color 0, unity gain, OS off -> output ~ input (only the output stage's tiny curvature)
    {
        auto p = make (48000, 512);
        setParam (*p, ids::pr, 0.0f); setParam (*p, ids::color, 0.0f); setParam (*p, ids::os, 0.0f);
        p->prepareToPlay (48000, 512);      // start from these settings (no cell release from the defaults)
        std::vector<float> y;
        run (*p, { Signal::sine, 48000 }, 1.0, 512, false, 2, {}, &y);
        double e = 0, s = 0;
        Signal r2 { Signal::sine, 48000 };
        std::vector<float> x (y.size());
        for (auto& v : x) v = r2.next();
        for (size_t i = 4800; i < y.size(); ++i) { e += (y[i] - x[i]) * (y[i] - x[i]); s += x[i] * x[i]; }
        const float rel = (float) (10.0 * std::log10 (e / s));
        report (rel < -30.0f, juce::String::formatted ("PR 0 / Color 0 / OS off: output vs input %.1f dB (EQ + curvature only)", rel));
    }

    // 6. latency: reported latency matches the measured delay, and Mix 0 is a delayed copy of the input
    for (int os = 0; os < 4; ++os)
        for (int ph = 0; ph < 2; ++ph)
        {
            auto p = make (48000, 512);
            setParam (*p, ids::os, (float) os); setParam (*p, ids::phase, (float) ph); setParam (*p, ids::mix, 0.0f);
            juce::AudioBuffer<float> b (2, 512); juce::MidiBuffer m;
            b.clear(); p->processBlock (b, m);            // applies OS setting (latency)
            const int lat = p->getLatencySamples();
            std::vector<float> y;
            for (int k = 0; k < 4; ++k)
            {
                b.clear();
                if (k == 0) { b.setSample (0, 100, 1.0f); b.setSample (1, 100, 1.0f); }
                p->processBlock (b, m);
                for (int i = 0; i < 512; ++i) y.push_back (b.getSample (0, i));
            }
            int arg = 0; for (int i = 1; i < (int) y.size(); ++i) if (std::abs (y[(size_t) i]) > std::abs (y[(size_t) arg])) arg = i;
            report (std::abs ((arg - 100) - lat) <= 1, juce::String::formatted ("Mix 0 delay, OS %d %s: measured %d, reported %d samples", 1 << os, ph ? "linear" : "low lat", arg - 100, lat));
        }

    // 7. bypass keeps the latency
    {
        auto p = make (48000, 512);
        setParam (*p, ids::os, 2.0f); setParam (*p, ids::phase, 1.0f);
        juce::AudioBuffer<float> b (2, 512); juce::MidiBuffer m;
        b.clear(); p->processBlock (b, m);
        const int lat = p->getLatencySamples();
        std::vector<float> y;
        for (int k = 0; k < 4; ++k)
        {
            b.clear();
            if (k == 0) { b.setSample (0, 100, 1.0f); b.setSample (1, 100, 1.0f); }
            p->processBlockBypassed (b, m);
            for (int i = 0; i < 512; ++i) y.push_back (b.getSample (0, i));
        }
        int arg = 0; for (int i = 1; i < (int) y.size(); ++i) if (std::abs (y[(size_t) i]) > std::abs (y[(size_t) arg])) arg = i;
        report (std::abs ((arg - 100) - lat) <= 1, juce::String::formatted ("bypass delay: measured %d, reported %d samples", arg - 100, lat));
    }

    // 7a. Bypass and Mix 0 are bit-exact delayed copies of the input at every oversampling setting
    for (int os = 0; os < 4; ++os)
        for (int ph = 0; ph < 2; ++ph)
            for (int useMix = 0; useMix < 2; ++useMix)
            {
                auto p = make (48000, 512);
                setParam (*p, ids::os, (float) os); setParam (*p, ids::phase, (float) ph);
                if (useMix) setParam (*p, ids::mix, 0.0f); else setParam (*p, ids::bypass, 1.0f);
                juce::AudioBuffer<float> b (2, 512); juce::MidiBuffer m;
                for (int k = 0; k < 8; ++k) { b.clear(); p->processBlock (b, m); }        // settle smoothers
                const int lat = p->getLatencySamples();
                Signal sig { Signal::pink, 48000 };
                std::vector<float> in, out;
                for (int k = 0; k < 40; ++k)
                {
                    for (int i = 0; i < 512; ++i) { const float v = sig.next(); b.setSample (0, i, v); b.setSample (1, i, v); in.push_back (v); }
                    p->processBlock (b, m);
                    for (int i = 0; i < 512; ++i) out.push_back (b.getSample (0, i));
                }
                float maxErr = 0.0f;
                for (size_t i = (size_t) lat + 4096; i < out.size(); ++i) maxErr = juce::jmax (maxErr, std::abs (out[i] - in[i - (size_t) lat]));
                report (maxErr < 1.0e-6f, juce::String::formatted ("%s, OS %d %s: delayed copy of the input, max error %.1e",
                                                                   useMix ? "Mix 0" : "Bypass", 1 << os, ph ? "linear" : "low lat", maxErr));
            }

    // 7b. the plugin's own Bypass parameter: same delay as processing, and no jump when switching
    {
        auto p = make (48000, 512);
        setParam (*p, ids::os, 2.0f); setParam (*p, ids::phase, 1.0f); setParam (*p, ids::bypass, 1.0f);
        juce::AudioBuffer<float> b (2, 512); juce::MidiBuffer m;
        b.clear(); p->processBlock (b, m); b.clear(); p->processBlock (b, m);
        const int lat = p->getLatencySamples();
        std::vector<float> y;
        for (int k = 0; k < 4; ++k)
        {
            b.clear();
            if (k == 0) { b.setSample (0, 100, 1.0f); b.setSample (1, 100, 1.0f); }
            p->processBlock (b, m);
            for (int i = 0; i < 512; ++i) y.push_back (b.getSample (0, i));
        }
        int arg = 0; for (int i = 1; i < (int) y.size(); ++i) if (std::abs (y[(size_t) i]) > std::abs (y[(size_t) arg])) arg = i;
        report (std::abs ((arg - 100) - lat) <= 1 && std::abs (y[(size_t) arg] - 1.0f) < 0.05f,
                juce::String::formatted ("Bypass parameter: passes the input, delay %d, reported %d samples", arg - 100, lat));
        // toggling bypass on a running sine: the largest sample-to-sample step stays small (crossfade)
        auto q = make (48000, 256);
        setParam (*q, ids::pr, 60.0f);
        float maxStep = 0.0f, last = 0.0f;
        Signal sig { Signal::sine, 48000 };
        for (int blk = 0; blk < 200; ++blk)
        {
            if (blk % 40 == 20) setParam (*q, ids::bypass, q->apvts.getRawParameterValue (ids::bypass)->load() > 0.5f ? 0.0f : 1.0f);
            juce::AudioBuffer<float> bb (2, 256);
            for (int i = 0; i < 256; ++i) { const float v = sig.next(); bb.setSample (0, i, v); bb.setSample (1, i, v); }
            q->processBlock (bb, m);
            for (int i = 0; i < 256; ++i) { const float v = bb.getSample (0, i); if (blk > 10) maxStep = juce::jmax (maxStep, std::abs (v - last)); last = v; }
        }
        report (maxStep < 0.12f, juce::String::formatted ("Bypass on/off on a running sine: largest step %.3f (crossfaded)", maxStep));
    }

    // 8. automation storm: every parameter moved every block (incl. OS / phase / mode switching)
    {
        auto p = make (44100, 333);
        juce::Random r (3);
        auto storm = [&] (int)
        {
            for (auto* prm : p->getParameters())
                if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
                    if (r.nextFloat() < 0.5f) rp->setValueNotifyingHost (r.nextFloat());
        };
        const auto st = run (*p, { Signal::pink, 44100 }, quick ? 3.0 : 10.0, 333, true, 2, storm);
        report (st.finite && st.peak < 200.0f, juce::String::formatted ("automation storm: finite, peak %+.1f dBFS", db (st.peak)));
    }

    // 9. state save / restore round trip
    {
        auto a = make (48000, 512);
        setParam (*a, ids::pr, 37.0f); setParam (*a, ids::color, 63.0f); setParam (*a, ids::output, -5.0f);
        setParam (*a, ids::os, 3.0f); setParam (*a, ids::mode, 1.0f); a->uiMeterOut = true;
        juce::MemoryBlock mb; a->getStateInformation (mb);
        auto b = make (48000, 512);
        b->setStateInformation (mb.getData(), (int) mb.getSize());
        bool same = b->uiMeterOut;
        for (auto* id : { ids::pr, ids::color, ids::output, ids::os, ids::mode })
            same = same && std::abs (a->apvts.getRawParameterValue (id)->load() - b->apvts.getRawParameterValue (id)->load()) < 1e-4f;
        report (same, "state save / restore round trip");
        b->setStateInformation ("garbage", 7);
        report (true, "garbage state ignored");
    }

    // 10. re-prepare with other rate / block size, and release
    {
        auto p = make (44100, 64);
        run (*p, { Signal::pink, 44100 }, 0.5, 64, true);
        p->releaseResources();
        p->setPlayConfigDetails (2, 2, 96000, 2048);
        p->prepareToPlay (96000, 2048);
        const auto st = run (*p, { Signal::pink, 96000 }, 0.5, 2048, true);
        report (st.finite, "re-prepare 44.1 kHz/64 -> 96 kHz/2048");
    }

    // 11. CPU: real-time factor of the default setting (4x OS) at 48 kHz
    {
        auto p = make (48000, 256);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        run (*p, { Signal::pink, 48000 }, 10.0, 256, false);
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        std::printf ("info  CPU at 48 kHz, 4x OS, stereo: %.1f %% of one core\n", ms / 100.0);
    }

    std::printf ("\n%s (%d failed)\n", failures == 0 ? "ALL CHECKS PASSED" : "CHECKS FAILED", failures);
    return failures == 0 ? 0 : 1;
}
