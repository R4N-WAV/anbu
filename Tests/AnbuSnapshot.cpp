// Renders the editor to PNG (headless check of the panel against Preview B).
// usage: AnbuSnapshot out.png [scale] [expanded 0/1] [preview 0/1]
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"

static void setParam (AnbuProcessor& p, const char* id, float v)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const float scale = argc > 2 ? (float) std::atof (argv[2]) : 2.0f;
    const bool expanded = argc > 3 && std::atoi (argv[3]) != 0;
    const bool preview = argc > 4 ? std::atoi (argv[4]) != 0 : true;

    AnbuProcessor p;
    p.setPlayConfigDetails (2, 2, 48000.0, 512);
    if (preview)   // control positions of Preview B
    {
        setParam (p, ids::pr, 64.2f);   setParam (p, ids::color, 45.0f);
        setParam (p, ids::input, 0.0f); setParam (p, ids::output, 3.4f);
        setParam (p, ids::mix, 100.0f); setParam (p, ids::noiseLevel, -107.0f);
        setParam (p, ids::os, 2.0f);    setParam (p, ids::phase, 0.0f); setParam (p, ids::mode, 0.0f);
    }
    p.uiAnalyserExpanded = expanded;
    p.prepareToPlay (48000.0, 512);

    // vocal-ish test signal: pink-ish noise with a few harmonics, about -20 dBFS RMS
    juce::Random rng (7);
    double b0 = 0, b1 = 0, b2 = 0, ph = 0;
    juce::AudioBuffer<float> b (2, 512); juce::MidiBuffer midi;
    auto run = [&] (int blocks)
    {
        for (int i = 0; i < blocks; ++i)
        {
            for (int s = 0; s < 512; ++s)
            {
                const double w = rng.nextFloat() * 2.0 - 1.0;
                b0 = 0.99765 * b0 + w * 0.0990460; b1 = 0.96300 * b1 + w * 0.2965164; b2 = 0.57000 * b2 + w * 1.0526913;
                const double pink = (b0 + b1 + b2 + w * 0.1848) * 0.11;
                ph += 2.0 * juce::MathConstants<double>::pi * 180.0 / 48000.0;
                const double voice = 0.05 * (std::sin (ph) + 0.6 * std::sin (2 * ph) + 0.4 * std::sin (3 * ph) + 0.25 * std::sin (5 * ph));
                const float v = (float) (0.6 * pink + voice);
                b.setSample (0, s, v); b.setSample (1, s, v);
            }
            p.processBlock (b, midi);
        }
    };
    run (200);
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    auto* ae = dynamic_cast<AnbuEditor*> (ed.get());
    for (int i = 0; i < 60; ++i)
    {
        run (2);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (17);
    }
    if (preview && ae != nullptr) ae->freezeMeters (-7.97f, 7.0f);
    for (int i = 0; i < 4; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (17);

    auto t0 = juce::Time::getMillisecondCounterHiRes();
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, scale);
    auto t1 = juce::Time::getMillisecondCounterHiRes();
    juce::File f (argc > 1 ? argv[1] : "anbu_panel.png");
    f.deleteFile();
    if (auto os = f.createOutputStream()) { juce::PNGImageFormat png; png.writeImageToStream (img, *os); }
    std::printf ("wrote %s %dx%d (full paint %.1f ms)\n", f.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight(), t1 - t0);
    ed = nullptr;
    return 0;
}
