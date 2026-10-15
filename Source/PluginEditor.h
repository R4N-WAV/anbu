#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "PluginProcessor.h"

// அன்பு v1 panel: a lunchbox of three modules (1000 x 700 design units), drawn entirely as vectors.
// Oxblood faceplates with a gold double border, dim lamp rings on the knobs, an SSL-style needle meter,
// a small analyser (spectrum, added harmonics, tone curve) that pops out into its own window,
// and a start-up sequence the first time the panel opens.

namespace anbu::ui
{
struct Theme
{
    juce::Colour chassis, plateTop, plateBot, edge, hi, label, dim, value, recess, slot, seat,
                 deboss, debHi, debLo, logoLit, zari, lamp, glow, lampOff, track,
                 keyUp0, keyUp1, keyDn0, keyDn1, keyOn, screen, screenEdge, grid, gridMaj;
};
const Theme& theme();

struct Fonts
{
    Fonts();
    juce::Font get (int weight, float size) const;   // size = CSS px (em size); line height is 1.64 em
    juce::Typeface::Ptr w500, w600, w700, w800;
};

// shared analyser data (filled by the editor's timer, drawn by the panel and by the pop-out window)
struct Spectrum
{
    static constexpr int nPts = 240;
    std::array<double, nPts> freqs {};
    std::array<float, nPts> out {}, in {}, harm {}, peak {}, tone {};
    bool active = false, inputActive = false;
};

class AnalyserView;
}

class AnbuEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit AnbuEditor (AnbuProcessor&);
    ~AnbuEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

    // snapshot tool hooks
    void freezeMeters (float vuDb, float grDb);
    void skipIntro();
    void setIntroFrame (double seconds);     // render a fixed frame of the start-up
    void setReadout (int knob);
    std::unique_ptr<juce::Component> makeAnalyserView();

    static constexpr int baseW = 1000, baseH = 700;

private:
    void timerCallback() override;

    struct InvisibleLook : public juce::LookAndFeel_V4
    {
        void drawRotarySlider (juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override {}
        void drawLinearSlider (juce::Graphics&, int, int, int, int, float, float, float, juce::Slider::SliderStyle, juce::Slider&) override {}
        juce::Slider::SliderLayout getSliderLayout (juce::Slider&) override;
        void drawCornerResizer (juce::Graphics&, int w, int h, bool isMouseOver, bool isMouseDragging) override;
    };
    struct RoundSlider : public juce::Slider
    {
        float radius = 0.0f;
        bool hitTest (int x, int y) override;
    };
    struct Knob
    {
        const char* id = nullptr;
        juce::String name;
        float cx = 0, cy = 0, D = 0, rr = 0;
        bool bipolar = false, big = false;
        float labelGap = 2;
        RoundSlider slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
        juce::RangedAudioParameter* param = nullptr;
    };

    float scale() const { return (float) getWidth() / (float) baseW; }
    juce::Point<float> toDesign (juce::Point<float> p) const { return p / scale(); }
    void repaintDesign (juce::Rectangle<float> r);
    float pos01 (const juce::Slider&) const;
    juce::Rectangle<float> knobArea (const Knob&) const;

    // drawing
    void renderStatic (juce::Graphics&);
    void drawKnobDynamic (juce::Graphics&, const Knob&, float pos, float flash);
    void drawFaderCap (juce::Graphics&, float pos);
    void drawModeLamps (juce::Graphics&, float flash);
    void drawMeterDynamic (juce::Graphics&);
    void drawDisplayDynamic (juce::Graphics&);
    void drawKeys (juce::Graphics&);
    void drawLogo (juce::Graphics&);
    void drawIntroOverlay (juce::Graphics&);
    void drawKey (juce::Graphics&, juce::Rectangle<float>, const juce::String&, bool active, float flash, float size);

    // interaction
    int keyAt (juce::Point<float>) const;          // 0-3 oversampling, 4-5 phase, 6 noise, -1 none
    bool keyOn (int) const;
    void clickKey (int);
    void setChoice (const char* id, int index);
    void toggleMeterMode (bool out);
    void openAnalyser();
    bool clickableAt (juce::Point<float>) const;
    juce::String readoutText() const;

    // analysis
    void updateSpectrum();
    void updateTone();

    // intro timeline (seconds since the panel opened)
    double introT() const;
    float flashAt (float x) const;
    float valueEase (float x) const;
    float meterFrac() const;

    AnbuProcessor& proc;
    InvisibleLook invisible;
    anbu::ui::Fonts fonts;
    std::array<Knob, 5> knobs;
    juce::Slider fader;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> faderAtt;
    juce::RangedAudioParameter* colorParam = nullptr;

    juce::Image staticLayer;
    float staticScale = 0.0f;
    bool staticOut = false;

    // meter
    bool frozen = false;
    float vuDb = -60.0f, grDb = 0.0f, needlePos = 0.0f, needleVel = 0.0f, drawnNeedle = -1.0f;

    // keys
    std::array<bool, 7> keyState {};
    bool modeLimit = false;

    // intro
    double introStart = -1.0, introFixed = -1.0;
    bool introRunning = false;

    // readout
    int readoutKnob = -1;
    double readoutTime = 0.0;
    float readoutAlpha = 0.0f;

    // analysis
    static constexpr int fftOrder = 12, fftSize = 1 << fftOrder;
    juce::dsp::FFT fft { fftOrder };
    std::vector<float> ringOut = std::vector<float> ((size_t) fftSize, 0.0f), ringIn = std::vector<float> ((size_t) fftSize, 0.0f),
                       ringHarm = std::vector<float> ((size_t) fftSize, 0.0f), fftData = std::vector<float> ((size_t) fftSize * 2, 0.0f),
                       window = std::vector<float> ((size_t) fftSize, 0.0f), pullOut = std::vector<float> (1 << 15, 0.0f),
                       pullIn = std::vector<float> (1 << 15, 0.0f), pullHarm = std::vector<float> (1 << 15, 0.0f);
    int ringPos = 0;
    anbu::ui::Spectrum spec;
    bool specDirty = false;
    float toneKeyPr = -1, toneKeyColor = -1, toneKeyGs = -1;

    class AnalyserWindow;
    std::unique_ptr<AnalyserWindow> analyserWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnbuEditor)
};
