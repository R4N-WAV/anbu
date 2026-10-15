#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "PluginProcessor.h"

// Anbu v1 hardware panel. The artwork is Preview B itself (2400 x 1344 master, drawn at any size);
// everything that moves is drawn live on top of it:
//   knob pointers (rotated sprites), LED rings (additive light in linear space + lit cores),
//   VU needle with its shadow, GR LED strip, switch lamps, spectrum + EQ curve display.
class AnbuEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit AnbuEditor (AnbuProcessor&);
    ~AnbuEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

    // test hook for the snapshot tool: hold the meters at fixed values
    void freezeMeters (float vuDb, float grDb);

    static constexpr int baseW = 1200, baseH = 672;

private:
    void timerCallback() override;

    struct KnobSlider : public juce::Slider
    {
        float radius = 0.0f;
        bool hitTest (int x, int y) override;
    };
    struct InvisibleLook : public juce::LookAndFeel_V4
    {
        void drawRotarySlider (juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override {}
    };
    struct Knob
    {
        int index = 0;
        KnobSlider slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
        juce::RangedAudioParameter* param = nullptr;
        float pos = 0.0f;
        int litLo = 0, litHi = -1;   // lit LED indices [litLo, litHi)
        juce::Rectangle<int> region;   // 2x px
        juce::Image cache;             // background + LED light for the region
    };

    // geometry helpers (2x px <-> component px)
    float scale() const { return (float) getWidth() / 2400.0f; }
    void repaint2x (juce::Rectangle<float> r);
    juce::Point<float> to2x (juce::Point<float> p) const { return p / scale(); }
    juce::Point<float> ledPos (int knob, int i) const;

    void knobChanged (int k);
    static void litRange (int knob, float pos, int& lo, int& hi);
    void rebuildKnobCache (Knob&);
    void rebuildGrCache();
    void updateSpectrum();
    void updateEqCurve();
    int switchAt (juce::Point<float> p2x) const;
    bool switchOn (int i) const;
    void clickSwitch (int i);
    void setChoice (const char* id, int index);
    juce::Rectangle<float> iconRect() const;
    juce::Rectangle<float> plotRect() const;
    juce::Rectangle<float> analyserRegion() const;
    void drawAnalyser (juce::Graphics&);
    void showReadout (int knob);

    AnbuProcessor& proc;
    InvisibleLook invisible;
    std::array<Knob, 6> knobs;

    // artwork
    juce::Image bg2x, bgSoft, bg1x, ptrBig, ptrSmall, coreBig, coreSmall, needleBody, needleShadow, grLit, iconExpand, analyserBig;
    std::array<juce::Image, 9> swOn;
    juce::Typeface::Ptr inter;
    std::array<float, 256> toLin {};
    std::vector<juce::uint8> toSrgb;   // linear [0,1] in 1/16383 steps -> sRGB byte

    // meters
    bool frozen = false;
    float vuTarget = -60.0f, vuPos = -40.0f, vuVel = 0.0f, drawnVu = 1000.0f;
    float grDb = 0.0f;
    int grLitCount = -1;
    juce::Image grCache;

    // switches (polled from the parameters)
    std::array<bool, 9> swState {};

    // analyser
    static constexpr int fftOrder = 12, fftSize = 1 << fftOrder, nPts = 220;
    juce::dsp::FFT fft { fftOrder };
    std::vector<float> ring = std::vector<float> ((size_t) fftSize, 0.0f), fftData = std::vector<float> ((size_t) fftSize * 2, 0.0f),
                       window = std::vector<float> ((size_t) fftSize, 0.0f), pullBuf = std::vector<float> (1 << 15, 0.0f),
                       pullIn = std::vector<float> (1 << 15, 0.0f), pullHarm = std::vector<float> (1 << 15, 0.0f),
                       ringH = std::vector<float> ((size_t) fftSize, 0.0f);
    int ringPos = 0;
    std::array<double, nPts> freqs {};
    std::array<float, nPts> specDb {}, harmDb {}, eqDb {};
    bool specActive = false;
    float eqKeyPr = -1, eqKeyColor = -1, eqKeyGs = -1;
    bool expanded = false;

    // value readout
    int readoutKnob = -1;
    double readoutTime = 0.0;
    float readoutAlpha = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnbuEditor)
};
