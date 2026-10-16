#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

APVTS::ParameterLayout AnbuProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    // whole-number steps everywhere
    auto pct = [] (float v, int) { return String (juce::roundToInt (v)) + " %"; };
    auto dbs = [] (float v, int) { const int i = juce::roundToInt (v); return (i > 0 ? "+" : "") + String (i) + " dB"; };
    auto num = [] (const String& t) { return t.retainCharacters ("-+0123456789.").getFloatValue(); };
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::pr, 1 }, "Peak Reduction", NormalisableRange<float> (0.0f, 100.0f, 1.0f), 40.0f,
                                                        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (juce::roundToInt (v)); })
                                                                                       .withValueFromStringFunction (num)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::color, 1 }, "Color", NormalisableRange<float> (0.0f, 100.0f, 1.0f), 25.0f,
                                                        AudioParameterFloatAttributes().withStringFromValueFunction (pct).withValueFromStringFunction (num)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::input, 1 }, "Input", NormalisableRange<float> (-24.0f, 24.0f, 1.0f), 0.0f,
                                                        AudioParameterFloatAttributes().withStringFromValueFunction (dbs).withValueFromStringFunction (num)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::output, 1 }, "Output", NormalisableRange<float> (-24.0f, 24.0f, 1.0f), 0.0f,
                                                        AudioParameterFloatAttributes().withStringFromValueFunction (dbs).withValueFromStringFunction (num)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::mix, 1 }, "Mix", NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f,
                                                        AudioParameterFloatAttributes().withStringFromValueFunction (pct).withValueFromStringFunction (num)));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids::mode, 1 }, "Mode", StringArray { "Compress", "Limit" }, 0));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::noiseOn, 1 }, "Noise", false));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::noiseLevel, 1 }, "Noise Level", NormalisableRange<float> (-120.0f, -60.0f, 1.0f), -96.0f,
                                                        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (juce::roundToInt (v)) + " dB"; })
                                                                                       .withValueFromStringFunction (num)));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::bypass, 1 }, "Bypass", false));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids::os, 1 }, "Oversampling", StringArray { "Off", "2x", "4x", "8x" }, 2));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids::phase, 1 }, "OS Phase", StringArray { "Low Latency", "Linear" }, 0));
    return { p.begin(), p.end() };
}

AnbuProcessor::AnbuProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "ANBU", createLayout())
{
}

bool AnbuProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
    return layouts.getMainInputChannelSet() == out;
}

void AnbuProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    fs = sampleRate;
    maxBlock = juce::jmax (1, samplesPerBlock);
    for (int ph = 0; ph < 2; ++ph)
        for (int idx = 1; idx < 4; ++idx)
        {
            auto type = ph == 0 ? juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR
                                : juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple;
            auto os = std::make_unique<juce::dsp::Oversampling<float>> (2, (size_t) idx, type, true, ph == 1);
            os->initProcessing ((size_t) maxBlock);
            oversamplers[(size_t) (ph * 4 + idx)] = std::move (os);
        }
    gBuf.assign ((size_t) maxBlock, 0.0);
    gsBuf.assign ((size_t) maxBlock, 0.0);
    capBuf.assign ((size_t) maxBlock, 1.0);
    dryBuf.setSize (2, maxBlock);
    monoR.assign ((size_t) maxBlock, 0.0f);
    inMono.assign ((size_t) maxBlock, 0.0f);
    harmBase.assign ((size_t) maxBlock, 0.0f);
    harmOS.assign ((size_t) maxBlock * 8, 0.0f);
    subSettings.assign ((size_t) (maxBlock / 32 + 2), anbu::Settings {});
    dryDelay.prepare ({ sampleRate, (juce::uint32) maxBlock, 2 });
    dryDelay.reset();

    control.prepare (fs);
    side.prepare (fs);
    noise.prepare (fs, noiseSeed);

    inGain.reset (fs, 0.03); outGain.reset (fs, 0.03); mixAmt.reset (fs, 0.03); bypassMix.reset (fs, 0.02);
    bypassMix.setCurrentAndTargetValue (apvts.getRawParameterValue (ids::bypass)->load() > 0.5f ? 1.0f : 0.0f);
    prSmooth.reset (fs, 0.05); colorSmooth.reset (fs, 0.05);
    inGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue (ids::input)->load()));
    outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue (ids::output)->load()));
    mixAmt.setCurrentAndTargetValue (apvts.getRawParameterValue (ids::mix)->load() / 100.0f);
    prSmooth.setCurrentAndTargetValue (apvts.getRawParameterValue (ids::pr)->load());
    colorSmooth.setCurrentAndTargetValue (apvts.getRawParameterValue (ids::color)->load() / 100.0);

    curOsIndex = -1;
    rebuildOversampling ((int) apvts.getRawParameterValue (ids::os)->load(), apvts.getRawParameterValue (ids::phase)->load() > 0.5f);
}

void AnbuProcessor::rebuildOversampling (int idx, bool linear)
{
    curOsIndex = idx; curLinear = linear;
    activeOs = idx > 0 ? oversamplers[(size_t) ((linear ? 4 : 0) + idx)].get() : nullptr;
    if (activeOs != nullptr) activeOs->reset();
    const int factor = osFactorFromIndex (idx);
    audio.prepare (fs * factor, factor);
    const float lat = activeOs != nullptr ? activeOs->getLatencyInSamples() : 0.0f;
    setLatencySamples ((int) std::round (lat));
    dryDelay.setDelay (lat);
}

void AnbuProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n <= 0) return;
    if (n > maxBlock) { // host sent a larger block than announced: process in pieces
        for (int start = 0; start < n; start += maxBlock)
        {
            const int m = juce::jmin (maxBlock, n - start);
            juce::AudioBuffer<float> part (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), start, m);
            juce::MidiBuffer dummy;
            processBlock (part, dummy);
        }
        return;
    }

    const int osIdx = (int) apvts.getRawParameterValue (ids::os)->load();
    const bool linear = apvts.getRawParameterValue (ids::phase)->load() > 0.5f;
    if (osIdx != curOsIndex || linear != curLinear) rebuildOversampling (osIdx, linear);

    // bypass: fully bypassed blocks only delay the input (by the current latency); switching crossfades over 20 ms
    bypassMix.setTargetValue (apvts.getRawParameterValue (ids::bypass)->load() > 0.5f ? 1.0f : 0.0f);
    if (bypassMix.getTargetValue() > 0.5f && ! bypassMix.isSmoothing())
    {
        passThrough (buffer);
        return;
    }


    // mono hosts: run the stereo engine on a duplicated channel
    const bool mono = buffer.getNumChannels() < 2;
    float* L = buffer.getWritePointer (0);
    float* R = mono ? monoR.data() : buffer.getWritePointer (1);
    if (mono) std::copy (L, L + n, R);

    inGain.setTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue (ids::input)->load()));
    outGain.setTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue (ids::output)->load()));
    mixAmt.setTargetValue (apvts.getRawParameterValue (ids::mix)->load() / 100.0f);
    prSmooth.setTargetValue (apvts.getRawParameterValue (ids::pr)->load());
    colorSmooth.setTargetValue (apvts.getRawParameterValue (ids::color)->load() / 100.0);
    const bool limit = apvts.getRawParameterValue (ids::mode)->load() > 0.5f;

    // dry copy (pre input gain) for the Mix control, delayed to line up with the oversampled path
    float* dL = dryBuf.getWritePointer (0);
    float* dR = dryBuf.getWritePointer (1);
    for (int i = 0; i < n; ++i) { dryDelay.pushSample (0, L[i]); dL[i] = dryDelay.popSample (0); }
    for (int i = 0; i < n; ++i) { dryDelay.pushSample (1, R[i]); dR[i] = dryDelay.popSample (1); }

    for (int i = 0; i < n; ++i) { const float g = inGain.getNextValue(); L[i] *= g; R[i] *= g; inMono[(size_t) i] = 0.5f * (L[i] + R[i]); }

    // ---- control path at the base rate, settings refreshed every 32 samples
    constexpr int SUB = 32;
    const int nSub = (n + SUB - 1) / SUB;
    for (int sb = 0; sb < nSub; ++sb)
    {
        const int i0 = sb * SUB, m = juce::jmin (SUB, n - i0);
        anbu::Settings s;
        s.peakReduction = prSmooth.skip (m);
        s.color = colorSmooth.skip (m);
        s.limit = limit;
        subSettings[(size_t) sb] = s;
        control.setSettings (s, anbu::colorTone (s.color));
        control.process (L + i0, R + i0, m, gBuf.data() + i0, gsBuf.data() + i0, capBuf.data() + i0);
    }

    // ---- audio path at the oversampled rate
    const int factor = osFactorFromIndex (curOsIndex);
    auto runAudio = [&] (float* aL, float* aR)
    {
        for (int sb = 0; sb < nSub; ++sb)
        {
            const int i0 = sb * SUB, m = juce::jmin (SUB, n - i0);
            const auto& s = subSettings[(size_t) sb];
            audio.setSettings (s, anbu::colorTone (s.color), anbu::colorKappa (s.color));
            audio.process (aL + i0 * factor, aR + i0 * factor, m * factor, gBuf.data() + i0, gsBuf.data() + i0, capBuf.data() + i0, harmOS.data() + i0 * factor);
        }
    };
    if (activeOs != nullptr)
    {
        float* chans[2] = { L, R };
        juce::dsp::AudioBlock<float> block (chans, 2, (size_t) n);
        auto up = activeOs->processSamplesUp (block);
        runAudio (up.getChannelPointer (0), up.getChannelPointer (1));
        activeOs->processSamplesDown (block);
    }
    else
        runAudio (L, R);

    // added harmonics, back to the base rate (box average; display only)
    for (int i = 0; i < n; ++i)
    {
        float acc = 0.0f;
        for (int k = 0; k < factor; ++k) acc += harmOS[(size_t) (i * factor + k)];
        harmBase[(size_t) i] = acc / (float) factor;
    }

    // ---- 3D side lift, noise, output, mix
    side.process (L, R, n);
    if (apvts.getRawParameterValue (ids::noiseOn)->load() > 0.5f)
        noise.add (L, R, n, apvts.getRawParameterValue (ids::noiseLevel)->load());

    double ss = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const float g = outGain.getNextValue(), w = mixAmt.getNextValue();
        lastOutGain = g; lastMix = w;
        const float b = bypassMix.getNextValue();
        L[i] = (1.0f - b) * (w * L[i] * g + (1.0f - w) * dL[i]) + b * dL[i];
        R[i] = (1.0f - b) * (w * R[i] * g + (1.0f - w) * dR[i]) + b * dR[i];
        ss += 0.5 * ((double) L[i] * L[i] + (double) R[i] * R[i]);
    }

    // ---- meters
    double gmax = 0.0;
    for (int i = 0; i < n; ++i) gmax = juce::jmax (gmax, gsBuf[(size_t) i]);
    // the meter is zeroed at idle, like the LA-2A's GR-zero trim (high PR drops the idle level; that is not compression)
    meterGrDb.store ((float) juce::jmax (0.0, 20.0 * std::log10 ((1.0 + gmax) / (1.0 + control.idleConductance()))));
    meterGs.store ((float) gsBuf[(size_t) (n - 1)]);
    meterCapW.store ((float) capBuf[(size_t) (n - 1)]);
    const double a = std::exp (-(double) n / (0.05 * fs));               // 50 ms mean square; the needle adds VU ballistics
    vuMs = a * vuMs + (1.0 - a) * ss / n;
    meterVuDb.store ((float) (10.0 * std::log10 (vuMs + 1e-20) + 18.0));

    // ---- analyser feed: output, input (after input gain), added harmonics (all mono sums)
    {
        const auto scope = anaFifo.write (n);
        int k = 0;
        for (int b = 0; b < 2; ++b)
        {
            const int start = b == 0 ? scope.startIndex1 : scope.startIndex2;
            const int m = b == 0 ? scope.blockSize1 : scope.blockSize2;
            for (int j = 0; j < m; ++j, ++k)
            {
                anaBuf[0][(size_t) (start + j)] = 0.5f * (L[k] + R[k]);
                anaBuf[1][(size_t) (start + j)] = inMono[(size_t) k];
                anaBuf[2][(size_t) (start + j)] = harmBase[(size_t) k] * lastOutGain * lastMix;
            }
        }
    }
}

// Host bypass (hosts that don't use the Bypass parameter): same as the plugin's own bypass.
void AnbuProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    passThrough (buffer);
}

void AnbuProcessor::passThrough (juce::AudioBuffer<float>& buffer)
{
    const int n = buffer.getNumSamples();
    const int nch = juce::jmin (2, buffer.getNumChannels());
    for (int c = 0; c < nch; ++c)
    {
        float* x = buffer.getWritePointer (c);
        for (int i = 0; i < n; ++i) { dryDelay.pushSample (c, x[i]); x[i] = dryDelay.popSample (c); }
    }
    for (int c = nch; c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, n);

    // meters and display keep showing the (unprocessed) signal
    const float* L = buffer.getReadPointer (0);
    const float* R = buffer.getReadPointer (nch > 1 ? 1 : 0);
    double ss = 0.0;
    for (int i = 0; i < n; ++i) ss += 0.5 * ((double) L[i] * L[i] + (double) R[i] * R[i]);
    const double a = std::exp (-(double) n / (0.05 * fs));
    vuMs = a * vuMs + (1.0 - a) * ss / juce::jmax (1, n);
    meterVuDb.store ((float) (10.0 * std::log10 (vuMs + 1e-20) + 18.0));
    meterGrDb.store (0.0f);
    const auto scope = anaFifo.write (n);
    int k = 0;
    for (int b = 0; b < 2; ++b)
    {
        const int start = b == 0 ? scope.startIndex1 : scope.startIndex2;
        const int m = b == 0 ? scope.blockSize1 : scope.blockSize2;
        for (int j = 0; j < m; ++j, ++k)
        {
            const float v = 0.5f * (L[k] + R[k]);
            anaBuf[0][(size_t) (start + j)] = v;
            anaBuf[1][(size_t) (start + j)] = v;
            anaBuf[2][(size_t) (start + j)] = 0.0f;
        }
    }
}

int AnbuProcessor::pullAnalyser (float* out, float* in, float* harm, int maxSamples)
{
    const auto scope = anaFifo.read (juce::jmin (maxSamples, anaFifo.getNumReady()));
    float* dst[3] = { out, in, harm };
    for (int c = 0; c < 3; ++c)
    {
        if (scope.blockSize1 > 0) std::copy (anaBuf[(size_t) c].data() + scope.startIndex1, anaBuf[(size_t) c].data() + scope.startIndex1 + scope.blockSize1, dst[c]);
        if (scope.blockSize2 > 0) std::copy (anaBuf[(size_t) c].data() + scope.startIndex2, anaBuf[(size_t) c].data() + scope.startIndex2 + scope.blockSize2, dst[c] + scope.blockSize1);
    }
    return scope.blockSize1 + scope.blockSize2;
}

juce::AudioProcessorEditor* AnbuProcessor::createEditor() { return new AnbuEditor (*this); }

void AnbuProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();                       // hosts may call this from any thread: write into the copy
    state.setProperty ("meterOut", uiMeterOut, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void AnbuProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            uiMeterOut = (bool) apvts.state.getProperty ("meterOut", false);
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AnbuProcessor(); }
