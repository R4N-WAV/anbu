#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <memory>
#include "DSP/AnbuCore.h"

namespace ids {
inline constexpr auto pr = "peakReduction";
inline constexpr auto color = "color";
inline constexpr auto input = "inputGain";
inline constexpr auto output = "outputGain";
inline constexpr auto mix = "mix";
inline constexpr auto mode = "mode";
inline constexpr auto noiseOn = "noiseOn";
inline constexpr auto noiseLevel = "noiseLevel";
inline constexpr auto os = "oversampling";
inline constexpr auto phase = "osPhase";
}

class AnbuProcessor : public juce::AudioProcessor
{
public:
    AnbuProcessor();
    ~AnbuProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Anbu"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // meters for the editor (written on the audio thread)
    //   meterGrDb : gain reduction of the optical cell (dB)
    //   meterVuDb : output level for the VU needle, 0 on the scale = -18 dBFS RMS
    //   meterGs   : slow cell conductance (drives the EQ curve in the display)
    std::atomic<float> meterGrDb { 0.0f }, meterVuDb { -100.0f }, meterGs { 0.0f }, meterCapW { 1.0f };

    // spectrum display feeds (mono): output, input (after input gain), added harmonics.
    // single producer (audio) / single consumer (editor)
    int pullAnalyser (float* out, float* in, float* harm, int maxSamples);
    double getAnalyserRate() const { return fs; }

    // editor-only state kept across editor open/close
    bool uiAnalyserExpanded = false;
    bool uiMeterOut = false;          // needle shows output level instead of GR (saved with the session)

private:
    void rebuildOversampling (int factorIndex, bool linearPhase);
    int osFactorFromIndex (int idx) const { return idx <= 0 ? 1 : (1 << idx); }

    double fs = 48000.0;
    int maxBlock = 512;
    int curOsIndex = -1;
    bool curLinear = false;

    std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, 8> oversamplers; // [phase*4 + index]
    juce::dsp::Oversampling<float>* activeOs = nullptr;

    anbu::OptoControl control;
    anbu::AudioPath audio;
    anbu::SideLift side;
    anbu::NoiseGen noise;

    std::vector<double> gBuf, gsBuf, capBuf;
    std::vector<float> monoR;
    std::vector<anbu::Settings> subSettings;
    juce::AudioBuffer<float> dryBuf;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> dryDelay { 1 << 15 };

    juce::SmoothedValue<float> inGain, outGain, mixAmt;
    juce::SmoothedValue<double, juce::ValueSmoothingTypes::Linear> prSmooth, colorSmooth;
    double vuMs = 0.0;
    static constexpr int anaSize = 1 << 15;
    juce::AbstractFifo anaFifo { anaSize };
    std::array<std::vector<float>, 3> anaBuf { std::vector<float> ((size_t) anaSize, 0.0f), std::vector<float> ((size_t) anaSize, 0.0f),
                                              std::vector<float> ((size_t) anaSize, 0.0f) };
    std::vector<float> inMono, harmBase, harmOS;
    float lastOutGain = 1.0f, lastMix = 1.0f;
};
