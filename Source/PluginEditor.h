#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <map>
#include "PluginProcessor.h"

// அன்பு v1 panel: a lunchbox of three modules (1000 x 700 design units).
// The faceplate, knobs, keys, lamps, fader cap and meter faces are pre-rendered 3D assets
// (tools/hw, rendered at 2 px per unit); the needle, the analyser screen and the start-up
// sequence are drawn live on top.

namespace anbu::ui
{
struct Theme
{
    juce::Colour chassis, plateTop, plateBot, label, dim, value, deboss, debHi, debLo, logoLit, zari, lamp, glow,
                 keyOn, screen, grid, gridMaj;
};
const Theme& theme();

struct Fonts
{
    Fonts();
    juce::Font logo (int weight, float size) const;   // Catamaran (Tamil logo), line height 1.64 em
    juce::Font ui (int weight, float size) const;     // Barlow Semi Condensed, line height 1.2 em
    juce::Typeface::Ptr c600, c800, b500, b600, b700;
};

// shared analyser data (filled by the editor's timer, drawn by the panel and by the pop-out window)
struct Spectrum
{
    static constexpr int nPts = 240;
    std::array<double, nPts> freqs {};
    std::array<float, nPts> out {}, in {}, harm {}, peak {}, tone {};
    bool active = false, inputActive = false;
};

// a rendered image kept at its source size plus copies resampled to the pixel scales in use
struct Art
{
    juce::Image src;
    std::vector<std::pair<float, juce::Image>> scaled;   // most recently used first (a few window sizes at once)
    const juce::Image& at (float pixelsPerUnit);         // pixelsPerUnit: device pixels per design unit
};

// all panel art, decoded once per process and shared by every open Anbu window
struct ArtLibrary
{
    Art& get (const char* resourceName);
    std::map<std::string, std::unique_ptr<Art>> items;
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
        int art = 0;                         // index into anbu::hw::knobs
        const char* id = nullptr;
        juce::String name;
        RoundSlider slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
        juce::RangedAudioParameter* param = nullptr;
        anbu::ui::Art* strip = nullptr;
        anbu::ui::Art* ring = nullptr;
    };

    float scale() const { return (float) getWidth() / (float) baseW; }
    juce::Point<float> toDesign (juce::Point<float> p) const { return p / scale(); }
    void repaintDesign (juce::Rectangle<float> r);
    float pos01 (const juce::Slider&) const;
    juce::Rectangle<float> knobArea (const Knob&) const;

    // drawing
    void renderStatic (juce::Graphics&, float pixelsPerUnit);
    void drawArt (juce::Graphics&, anbu::ui::Art&, juce::Rectangle<float> dest, float alpha = 1.0f);
    void drawKnob (juce::Graphics&, Knob&, float pos, float flash);
    void drawFaderCap (juce::Graphics&, float pos);
    void drawModeLamps (juce::Graphics&, float flash);
    void drawMeterDynamic (juce::Graphics&);
    void drawDisplayDynamic (juce::Graphics&);
    void drawKeys (juce::Graphics&);
    void drawIntroOverlay (juce::Graphics&);

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

    // rendered art
    juce::SharedResourcePointer<anbu::ui::ArtLibrary> library;
    anbu::ui::Art *bg = nullptr, *faceOut = nullptr, *pivot = nullptr, *glassMeter = nullptr, *glassDisp = nullptr,
                  *faderCap = nullptr, *lampC = nullptr, *lampL = nullptr, *noLogo = nullptr;
    std::array<anbu::ui::Art*, 7> keyArt {};

    juce::Image staticLayer;
    float staticScale = 0.0f;
    bool staticOut = false, staticFast = false;
    double resizingSince = 0.0;                          // > 0 while the window is being resized
    float artScale (juce::Graphics&) const;

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
