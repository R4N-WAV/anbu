#include "PluginEditor.h"
#include "GuiGeometry.h"
#include "BinaryData.h"

namespace G = anbu::gui;

namespace {
juce::Image loadPng (const char* name)
{
    int size = 0;
    if (auto* data = BinaryData::getNamedResource (name, size))
        return juce::ImageFileFormat::loadFrom (data, (size_t) size);
    return {};
}
const float* kernelFor (bool big) { return big ? G::kernelBig : G::kernelSmall; }
float ringOf (bool big) { return big ? G::Size_big::ring : G::Size_small::ring; }
float seatOf (bool big) { return big ? G::Size_big::seat : G::Size_small::seat; }

const char* const knobNames[6] = { "PEAK REDUCTION", "COLOR", "INPUT", "OUTPUT", "MIX", "NOISE LEVEL" };

// switch bodies (2x px): 144 x 72 centred in each lamp crop
juce::Rectangle<float> switchBody (int i)
{
    const auto& s = G::switches[i];
    const float cx = 0.5f * (float) (s.x0 + s.x1);
    return { cx - 72.0f, 1248.0f - 36.0f, 144.0f, 72.0f };
}

constexpr float specTopDb = -40.0f, specBottomDb = -100.0f;  // spectrum level at the +12 / -12 grid lines (3 dB/oct tilt)
}

// ============================================================================================ construction
AnbuEditor::AnbuEditor (AnbuProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    bg2x = loadPng ("bg2x_png");          bg1x = loadPng ("bg1x_png");
    bgSoft = juce::SoftwareImageType().convert (bg2x);   // CPU copy for the light compositing (no GPU read-backs)
    ptrBig = loadPng ("ptr_big_png");     ptrSmall = loadPng ("ptr_small_png");
    coreBig = loadPng ("core_big_png");   coreSmall = loadPng ("core_small_png");
    needleBody = loadPng ("needle_body_png"); needleShadow = loadPng ("needle_shadow_png");
    grLit = loadPng ("gr_lit_png");       iconExpand = loadPng ("icon_expand_png");
    analyserBig = loadPng ("analyser_big_png");
    for (int i = 0; i < 9; ++i) swOn[(size_t) i] = loadPng (("sw_" + juce::String (i) + "_on_png").toRawUTF8());
    {
        int size = 0;
        if (auto* data = BinaryData::getNamedResource ("intermedium_otf", size))
            inter = juce::Typeface::createSystemTypefaceFor (data, (size_t) size);
    }

    // sRGB <-> linear tables
    for (int i = 0; i < 256; ++i)
    {
        const double c = i / 255.0;
        toLin[(size_t) i] = (float) (c <= 0.04045 ? c / 12.92 : std::pow ((c + 0.055) / 1.055, 2.4));
    }
    toSrgb.resize (16384);
    for (int i = 0; i < 16384; ++i)
    {
        const double l = i / 16383.0;
        const double s = l <= 0.0031308 ? l * 12.92 : 1.055 * std::pow (l, 1.0 / 2.4) - 0.055;
        toSrgb[(size_t) i] = (juce::uint8) juce::jlimit (0, 255, (int) std::lround (s * 255.0));
    }

    // knobs: invisible sliders over the artwork carry the interaction and the parameter attachment
    for (int k = 0; k < 6; ++k)
    {
        auto& kn = knobs[(size_t) k];
        kn.index = k;
        const auto& kg = G::knobs[k];
        kn.slider.setLookAndFeel (&invisible);
        kn.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        kn.slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        kn.slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        kn.slider.setMouseDragSensitivity (kg.big ? 320 : 260);
        addAndMakeVisible (kn.slider);
        kn.param = proc.apvts.getParameter (kg.id);
        kn.att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, kg.id, kn.slider);
        if (kn.param != nullptr)
            kn.slider.setDoubleClickReturnValue (true, kn.param->convertFrom0to1 (kn.param->getDefaultValue()));
    }
    for (auto& kn : knobs)
    {
        kn.pos = (float) kn.slider.valueToProportionOfLength (kn.slider.getValue());
        const int n = G::knobs[kn.index].leds;
        kn.lit = juce::jlimit (1, n, (int) std::floor (kn.pos * (float) (n - 1) + 1e-3f) + 1);
    }
    for (auto& kn : knobs) rebuildKnobCache (kn);
    for (auto& kn : knobs)
    {
        const int k = kn.index;
        kn.slider.onValueChange = [this, k]
        {
            knobChanged (k);
            if (knobs[(size_t) k].slider.isMouseOverOrDragging()) showReadout (k);
        };
        kn.slider.onDragStart = [this, k] { showReadout (k); };
    }

    // analyser
    for (int i = 0; i < fftSize; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) fftSize);
    for (int i = 0; i < nPts; ++i)
        freqs[(size_t) i] = 20.0 * std::pow (1000.0, (double) i / (nPts - 1));
    specDb.fill (-140.0f);
    eqDb.fill (0.0f);
    expanded = proc.uiAnalyserExpanded;
    for (int i = 0; i < 9; ++i) swState[(size_t) i] = switchOn (i);
    grDb = proc.meterGrDb.load();
    rebuildGrCache();
    updateEqCurve();

    setResizable (true, true);
    setResizeLimits (840, 470, 2400, 1344);
    getConstrainer()->setFixedAspectRatio (2400.0 / 1344.0);
    setSize (baseW, baseH);
    startTimerHz (60);
}

AnbuEditor::~AnbuEditor()
{
    stopTimer();
    for (auto& kn : knobs) kn.slider.setLookAndFeel (nullptr);
}

bool AnbuEditor::KnobSlider::hitTest (int x, int y)
{
    const auto c = getLocalBounds().toFloat().getCentre();
    return c.getDistanceFrom ({ (float) x, (float) y }) <= radius;
}

void AnbuEditor::resized()
{
    const float s = scale();
    for (auto& kn : knobs)
    {
        const auto& kg = G::knobs[kn.index];
        const float r = seatOf (kg.big) * s;
        kn.slider.radius = r;
        kn.slider.setBounds (juce::Rectangle<float> (kg.cx * s - r, kg.cy * s - r, 2 * r, 2 * r).toNearestInt());
    }
}

void AnbuEditor::repaint2x (juce::Rectangle<float> r)
{
    const float s = scale();
    repaint ((r * s).getSmallestIntegerContainer().expanded (2));
}

juce::Point<float> AnbuEditor::ledPos (int knob, int i) const
{
    const auto& kg = G::knobs[knob];
    const float a = juce::degreesToRadians (225.0f - 270.0f * (float) i / (float) (kg.leds - 1));
    const float r = ringOf (kg.big);
    return { kg.cx + r * std::cos (a), kg.cy - r * std::sin (a) };
}

// ============================================================================================ knobs
void AnbuEditor::knobChanged (int k)
{
    auto& kn = knobs[(size_t) k];
    const auto& kg = G::knobs[k];
    kn.pos = (float) kn.slider.valueToProportionOfLength (kn.slider.getValue());
    const int lit = juce::jlimit (1, kg.leds, (int) std::floor (kn.pos * (float) (kg.leds - 1) + 1e-3f) + 1);
    if (lit != kn.lit)
    {
        kn.lit = lit;
        auto dirty = kn.region.toFloat();
        for (auto& other : knobs)
            if (&other == &kn || other.region.intersects (kn.region))
            {
                rebuildKnobCache (other);
                dirty = dirty.getUnion (other.region.toFloat());
            }
        repaint2x (dirty);
    }
    else
    {
        const float h = (kg.big ? G::Size_big::ptrHalf : G::Size_small::ptrHalf) + 6.0f;
        const float cx = kg.cx + (kg.big ? G::Size_big::capDx : G::Size_small::capDx);
        const float cy = kg.cy + (kg.big ? G::Size_big::capDy : G::Size_small::capDy);
        repaint2x ({ cx - h, cy - h, 2 * h, 2 * h });
    }
}

// LED light is added in linear light (the kernels were measured on Preview B), then the lit cores go on top.
void AnbuEditor::rebuildKnobCache (Knob& kn)
{
    const auto& kg = G::knobs[kn.index];
    const float half = ringOf (kg.big) + (float) G::kernelLen + 4.0f;
    kn.region = juce::Rectangle<float> (kg.cx - half, kg.cy - half, 2 * half, 2 * half)
                    .getSmallestIntegerContainer().getIntersection ({ 0, 0, G::W2, G::H2 });
    const auto R = kn.region;
    const int w = R.getWidth(), h = R.getHeight();
    std::vector<float> acc ((size_t) (w * h * 3), 0.0f);

    for (const auto& src : knobs)
    {
        if (src.lit <= 0) continue;
        const auto& sg = G::knobs[src.index];
        const float* K = kernelFor (sg.big);
        for (int i = 0; i < src.lit; ++i)
        {
            const auto p = ledPos (src.index, i);
            const auto box = juce::Rectangle<int> ((int) p.x - G::kernelLen, (int) p.y - G::kernelLen, 2 * G::kernelLen + 2, 2 * G::kernelLen + 2)
                                 .getIntersection (R);
            for (int y = box.getY(); y < box.getBottom(); ++y)
            {
                const float dy = (float) y - p.y;
                float* row = acc.data() + (size_t) ((y - R.getY()) * w * 3);
                for (int x = box.getX(); x < box.getRight(); ++x)
                {
                    const float dx = (float) x - p.x;
                    const float d = std::sqrt (dx * dx + dy * dy);
                    if (d >= (float) (G::kernelLen - 1)) continue;
                    const int j = (int) d;
                    const float f = d - (float) j;
                    float* a = row + (x - R.getX()) * 3;
                    for (int c = 0; c < 3; ++c)
                        a[c] += K[j * 3 + c] * (1.0f - f) + K[(j + 1) * 3 + c] * f;
                }
            }
        }
    }

    juce::Image out (juce::Image::ARGB, w, h, true, juce::SoftwareImageType());
    {
        const juce::Image::BitmapData srcData (bgSoft, R.getX(), R.getY(), w, h, juce::Image::BitmapData::readOnly);
        juce::Image::BitmapData dst (out, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                const auto c = srcData.getPixelColour (x, y);
                const float* a = acc.data() + (size_t) ((y * w + x) * 3);
                auto conv = [this] (juce::uint8 v, float add)
                {
                    const float l = juce::jlimit (0.0f, 1.0f, toLin[v] + add);
                    return toSrgb[(size_t) (l * 16383.0f + 0.5f)];
                };
                const int e = juce::jmin (juce::jmin (x, y), juce::jmin (w - 1 - x, h - 1 - y));
                const float alpha = juce::jlimit (0.0f, 1.0f, ((float) e + 0.5f) / 8.0f);
                dst.setPixelColour (x, y, juce::Colour (conv (c.getRed(), a[0]), conv (c.getGreen(), a[1]), conv (c.getBlue(), a[2])).withAlpha (alpha));
            }
    }
    {
        juce::Graphics gc (out);
        gc.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        for (const auto& src : knobs)
        {
            const auto& sg = G::knobs[src.index];
            const auto& core = sg.big ? coreBig : coreSmall;
            const float ccx = sg.big ? G::Size_big::coreCx : G::Size_small::coreCx;
            const float ccy = sg.big ? G::Size_big::coreCy : G::Size_small::coreCy;
            for (int i = 0; i < src.lit; ++i)
            {
                const auto p = ledPos (src.index, i);
                if (! R.toFloat().expanded (24.0f).contains (p)) continue;
                gc.drawImageTransformed (core, juce::AffineTransform::translation (p.x - ccx - (float) R.getX(), p.y - ccy - (float) R.getY()));
            }
        }
    }
    kn.cache = juce::NativeImageType().convert (out);
}

// ============================================================================================ GR strip
void AnbuEditor::rebuildGrCache()
{
    grLitCount = juce::jlimit (0, 20, (int) std::floor (grDb + 0.5f));
    const int w = G::grX1 - G::grX0, h = G::grY1 - G::grY0;
    std::vector<float> acc ((size_t) (w * h * 3), 0.0f);
    for (int i = 20 - grLitCount; i < 20; ++i)
    {
        const float x0 = G::segX0 + G::segPitch * (float) i, x1 = x0 + G::segW, y0 = G::segY0, y1 = G::segY0 + G::segH;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                const float gx = (float) (x + G::grX0), gy = (float) (y + G::grY0);
                const float dx = juce::jmax (juce::jmax (x0 - gx, gx - x1), 0.0f), dy = juce::jmax (juce::jmax (y0 - gy, gy - y1), 0.0f);
                const float d = std::sqrt (dx * dx + dy * dy);
                if (d >= (float) (G::grGlowLen - 1)) continue;
                const int j = (int) d;
                const float f = d - (float) j;
                float* a = acc.data() + (size_t) ((y * w + x) * 3);
                for (int c = 0; c < 3; ++c)
                    a[c] += G::grGlow[j * 3 + c] * (1.0f - f) + G::grGlow[(j + 1) * 3 + c] * f;
            }
    }
    juce::Image out (juce::Image::ARGB, w, h, true, juce::SoftwareImageType());
    {
        const juce::Image::BitmapData srcData (bgSoft, G::grX0, G::grY0, w, h, juce::Image::BitmapData::readOnly);
        juce::Image::BitmapData dst (out, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                const auto c = srcData.getPixelColour (x, y);
                const float* a = acc.data() + (size_t) ((y * w + x) * 3);
                auto conv = [this] (juce::uint8 v, float add)
                {
                    const float l = juce::jlimit (0.0f, 1.0f, toLin[v] + add);
                    return toSrgb[(size_t) (l * 16383.0f + 0.5f)];
                };
                dst.setPixelColour (x, y, juce::Colour (conv (c.getRed(), a[0]), conv (c.getGreen(), a[1]), conv (c.getBlue(), a[2])));
            }
    }
    {
        juce::Graphics gc (out);
        gc.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        for (int i = 20 - grLitCount; i < 20; ++i)
            gc.drawImageTransformed (grLit, juce::AffineTransform::translation (G::segX0 + G::segPitch * (float) i - 3.0f - (float) G::grX0,
                                                                              G::segY0 - 3.0f - (float) G::grY0));
    }
    grCache = juce::NativeImageType().convert (out);
}

// ============================================================================================ switches
bool AnbuEditor::switchOn (int i) const
{
    auto v = [this] (const char* id) { return (int) std::lround (proc.apvts.getRawParameterValue (id)->load()); };
    switch (i)
    {
        case 0: return v (ids::mode) == 0;
        case 1: return v (ids::mode) == 1;
        case 2: case 3: case 4: case 5: return v (ids::os) == i - 2;
        case 6: return v (ids::phase) == 0;
        case 7: return v (ids::phase) == 1;
        case 8: return v (ids::noiseOn) != 0;
        default: return false;
    }
}

void AnbuEditor::setChoice (const char* id, int index)
{
    if (auto* prm = proc.apvts.getParameter (id))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 ((float) index));
        prm->endChangeGesture();
    }
}

void AnbuEditor::clickSwitch (int i)
{
    if (i <= 1) setChoice (ids::mode, i);
    else if (i <= 5) setChoice (ids::os, i - 2);
    else if (i <= 7) setChoice (ids::phase, i - 6);
    else setChoice (ids::noiseOn, switchOn (8) ? 0 : 1);
}

int AnbuEditor::switchAt (juce::Point<float> p) const
{
    for (int i = 0; i < 9; ++i)
        if (switchBody (i).contains (p)) return i;
    return -1;
}

juce::Rectangle<float> AnbuEditor::iconRect() const
{
    const float dy = expanded ? (float) (G::bigY0 - G::dispY0) : 0.0f;
    return { (float) G::iconX - 6.0f, (float) G::iconY - 6.0f + dy, 44.0f, 44.0f };
}

juce::Rectangle<float> AnbuEditor::plotRect() const
{
    return { G::plotX0, expanded ? G::bigPlotY0 : G::plotY0, G::plotX1 - G::plotX0, (expanded ? G::bigPlotY1 : G::plotY1) - (expanded ? G::bigPlotY0 : G::plotY0) };
}

juce::Rectangle<float> AnbuEditor::analyserRegion() const
{
    return expanded ? juce::Rectangle<float> ((float) G::bigX0, (float) G::bigY0, (float) (G::bigX1 - G::bigX0), (float) (G::bigY1 - G::bigY0))
                    : juce::Rectangle<float> ((float) G::dispX0, (float) G::dispY0, (float) (G::dispX1 - G::dispX0), (float) (G::dispY1 - G::dispY0));
}

void AnbuEditor::mouseDown (const juce::MouseEvent& e)
{
    const auto p = to2x (e.position);
    if (iconRect().contains (p))
    {
        const auto before = analyserRegion();
        expanded = ! expanded;
        proc.uiAnalyserExpanded = expanded;
        repaint2x (before.getUnion (analyserRegion()));
        return;
    }
    const int sw = switchAt (p);
    if (sw >= 0) clickSwitch (sw);
}

void AnbuEditor::mouseMove (const juce::MouseEvent& e)
{
    const auto p = to2x (e.position);
    setMouseCursor (switchAt (p) >= 0 || iconRect().contains (p) ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void AnbuEditor::showReadout (int knob)
{
    readoutKnob = knob;
    readoutTime = juce::Time::getMillisecondCounterHiRes();
    readoutAlpha = 1.0f;
    repaint2x (plotRect());
}

void AnbuEditor::freezeMeters (float vuDb, float gr)
{
    frozen = true;
    vuTarget = vuPos = vuDb;
    vuVel = 0.0f;
    grDb = gr;
    rebuildGrCache();
    repaint();
}

// ============================================================================================ analyser
void AnbuEditor::updateSpectrum()
{
    const int got = proc.pullAnalyser (pullBuf.data(), (int) pullBuf.size());
    for (int i = 0; i < got; ++i)
    {
        ring[(size_t) ringPos] = pullBuf[(size_t) i];
        ringPos = (ringPos + 1) & (fftSize - 1);
    }
    if (got > 0)
    {
        for (int i = 0; i < fftSize; ++i)
            fftData[(size_t) i] = ring[(size_t) ((ringPos + i) & (fftSize - 1))] * window[(size_t) i];
        std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (fftData.data(), true);
        const double fs = juce::jmax (8000.0, proc.getAnalyserRate());
        const double norm = 2.0 / (0.5 * fftSize);           // full-scale sine -> 0 dB (Hann coherent gain 0.5)
        const double binHz = fs / fftSize;
        auto powerAt = [&] (double bin)
        {
            const int b0 = juce::jlimit (0, fftSize / 2 - 1, (int) bin);
            const double f = juce::jlimit (0.0, 1.0, bin - b0);
            const double m0 = fftData[(size_t) b0] * norm, m1 = fftData[(size_t) (b0 + 1)] * norm;
            return (1 - f) * m0 * m0 + f * m1 * m1;
        };
        bool any = false;
        for (int p = 0; p < nPts; ++p)
        {
            const double f = freqs[(size_t) p];
            const double kl = f * std::pow (2.0, -1.0 / 12.0) / binHz, kh = f * std::pow (2.0, 1.0 / 12.0) / binHz;
            double pw = 0.0;
            if (f >= 0.5 * fs) pw = 1e-14;
            else if (kh - kl < 2.0) pw = powerAt (f / binHz);
            else
            {
                int cnt = 0;
                for (int b = (int) std::ceil (kl); b <= (int) std::floor (kh) && b < fftSize / 2; ++b, ++cnt)
                    pw += (double) fftData[(size_t) b] * fftData[(size_t) b] * norm * norm;
                pw /= juce::jmax (1, cnt);
            }
            const float db = (float) (10.0 * std::log10 (pw + 1e-14) + 3.0 * std::log2 (f / 1000.0));
            float& s = specDb[(size_t) p];
            s += (db > s ? 0.6f : 0.12f) * (db - s);
            any = any || s > specBottomDb - 6.0f;
        }
        specActive = any;
        repaint2x (plotRect().expanded (6));
    }
    else if (specActive)
    {
        bool any = false;
        for (auto& s : specDb) { s -= 1.5f; any = any || s > specBottomDb - 6.0f; }
        specActive = any;
        repaint2x (plotRect().expanded (6));
    }
}

void AnbuEditor::updateEqCurve()
{
    const float pr = proc.apvts.getRawParameterValue (ids::pr)->load();
    const float color = proc.apvts.getRawParameterValue (ids::color)->load() / 100.0f;
    const float gs = proc.meterGs.load();
    if (std::abs (pr - eqKeyPr) < 0.05f && std::abs (color - eqKeyColor) < 0.0005f && std::abs (gs - eqKeyGs) < 0.002f * (1.0f + gs)) return;
    eqKeyPr = pr; eqKeyColor = color; eqKeyGs = gs;
    std::array<double, nPts> out {};
    anbu::displayCurve (pr, color, gs, freqs.data(), out.data(), nPts);
    for (int i = 0; i < nPts; ++i) eqDb[(size_t) i] = (float) out[(size_t) i];
    repaint2x (plotRect().expanded (12));
}

void AnbuEditor::drawAnalyser (juce::Graphics& g)
{
    const auto pr = plotRect();
    auto xOf = [&] (double f) { return pr.getX() + pr.getWidth() * (float) (std::log10 (f / 20.0) / 3.0); };

    if (expanded)
    {
        g.drawImageAt (analyserBig, G::bigX0, G::bigY0);
        // grid: decades + 2/5 lines, every 3 dB
        const int fl[] = { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };
        for (int f : fl)
        {
            const bool major = f == 100 || f == 1000 || f == 10000;
            g.setColour (juce::Colour (major ? G::gridMajor : G::gridMinor));
            g.fillRect (juce::Rectangle<float> (xOf (f) - 1.1f, pr.getY(), 2.2f, pr.getHeight()));
        }
        g.setFont (juce::Font (juce::FontOptions (inter).withPointHeight (G::labelSize)));
        for (int db = 12; db >= -12; db -= 3)
        {
            const float y = pr.getY() + pr.getHeight() * (float) (12 - db) / 24.0f;
            g.setColour (juce::Colour (G::gridMinor));
            g.fillRect (juce::Rectangle<float> (pr.getX(), y - 1.1f, pr.getWidth(), 2.2f));
            if (db % 6 == 0)
            {
                g.setColour (juce::Colour (G::labelColour));
                const juce::String t = db > 0 ? "+" + juce::String (db) : (db < 0 ? juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) + juce::String (-db) : "0");
                g.drawText (t, juce::Rectangle<float> (G::labelRightX - 80.0f, y - 14.0f, 80.0f, 28.0f), juce::Justification::centredRight, false);
            }
        }
        g.drawImageAt (iconExpand, G::iconX, G::iconY + (G::bigY0 - G::dispY0));
    }

    g.saveState();
    g.reduceClipRegion (pr.toNearestInt());
    // spectrum
    if (specActive)
    {
        auto yOf = [&] (float db) { return pr.getY() + pr.getHeight() * (specTopDb - db) / (specTopDb - specBottomDb); };
        juce::Path line, fill;
        fill.startNewSubPath (pr.getX() - 2.0f, pr.getBottom() + 4.0f);
        for (int p = 0; p < nPts; ++p)
        {
            const float x = xOf (freqs[(size_t) p]), y = juce::jlimit (pr.getY() - 8.0f, pr.getBottom() + 4.0f, yOf (specDb[(size_t) p]));
            if (p == 0) line.startNewSubPath (x, y); else line.lineTo (x, y);
            fill.lineTo (x, y);
        }
        fill.lineTo (pr.getRight() + 2.0f, pr.getBottom() + 4.0f);
        fill.closeSubPath();
        g.setGradientFill (juce::ColourGradient (juce::Colour (156, 112, 58).withAlpha (0.86f), 0.0f, pr.getY(),
                                                 juce::Colour (60, 47, 31).withAlpha (0.86f), 0.0f, pr.getBottom(), false));
        g.fillPath (fill);
        g.setColour (juce::Colour (220, 153, 69));
        g.strokePath (line, juce::PathStrokeType (2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    // EQ curve
    {
        const float mid = pr.getCentreY(), k = pr.getHeight() / 24.0f;
        juce::Path e;
        for (int p = 0; p < nPts; ++p)
        {
            const float x = xOf (freqs[(size_t) p]), y = mid - k * juce::jlimit (-13.0f, 13.0f, eqDb[(size_t) p]);
            if (p == 0) e.startNewSubPath (x, y); else e.lineTo (x, y);
        }
        const juce::Colour core (251, 243, 225);
        g.setColour (core.withAlpha (0.05f)); g.strokePath (e, juce::PathStrokeType (16.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (core.withAlpha (0.10f)); g.strokePath (e, juce::PathStrokeType (9.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (core.withAlpha (0.35f)); g.strokePath (e, juce::PathStrokeType (5.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (core);                   g.strokePath (e, juce::PathStrokeType (3.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    // value readout
    if (readoutKnob >= 0 && readoutAlpha > 0.0f)
    {
        const auto& kn = knobs[(size_t) readoutKnob];
        const juce::String txt = juce::String (knobNames[readoutKnob]) + "   " + (kn.param != nullptr ? kn.param->getCurrentValueAsText() : juce::String());
        g.setFont (juce::Font (juce::FontOptions (inter).withPointHeight (22.0f)).withExtraKerningFactor (0.06f));
        g.setColour (juce::Colour (242, 225, 190).withAlpha (readoutAlpha));
        g.drawText (txt.replace ("-", juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92"))),
                    juce::Rectangle<float> (pr.getX() + 18.0f, pr.getY() + 10.0f, 600.0f, 34.0f), juce::Justification::centredLeft, false);
    }
    g.restoreState();
}

// ============================================================================================ paint
void AnbuEditor::paint (juce::Graphics& g)
{
    const float s = scale();
    const float phys = s * g.getInternalContext().getPhysicalPixelScaleFactor();
    g.addTransform (juce::AffineTransform::scale (s));
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);

    if (phys < 0.75f && bg1x.isValid()) g.drawImage (bg1x, juce::Rectangle<float> (0.0f, 0.0f, (float) G::W2, (float) G::H2));
    else g.drawImageAt (bg2x, 0, 0);

    // LED rings
    for (const auto& kn : knobs)
        if (kn.cache.isValid() && g.clipRegionIntersects (kn.region))
            g.drawImageAt (kn.cache, kn.region.getX(), kn.region.getY());
    // pointers
    for (const auto& kn : knobs)
    {
        const auto& kg = G::knobs[kn.index];
        const auto& img = kg.big ? ptrBig : ptrSmall;
        const float half = kg.big ? G::Size_big::ptrHalf : G::Size_small::ptrHalf;
        const float capX = kg.cx + (kg.big ? G::Size_big::capDx : G::Size_small::capDx);
        const float capY = kg.cy + (kg.big ? G::Size_big::capDy : G::Size_small::capDy);
        if (! g.clipRegionIntersects (juce::Rectangle<float> (capX - half, capY - half, 2 * half, 2 * half).getSmallestIntegerContainer())) continue;
        const float sx = half + (capX - std::round (capX)), sy = half + (capY - std::round (capY));
        const float ang = juce::degreesToRadians (270.0f * kn.pos - 135.0f);
        g.drawImageTransformed (img, juce::AffineTransform::translation (-(sx + 0.5f), -(sy + 0.5f)).rotated (ang).translated (capX + 0.5f, capY + 0.5f));
    }

    if (! expanded)
    {
        // GR strip
        if (grCache.isValid()) g.drawImageAt (grCache, G::grX0, G::grY0);
        // VU needle (clipped to the face window), shadow first with a fixed light direction
        const juce::Rectangle<int> face (G::faceX0, G::faceY0, G::faceX1 - G::faceX0, G::faceY1 - G::faceY0);
        if (g.clipRegionIntersects (face))
        {
            g.saveState();
            g.reduceClipRegion (face);
            const float db = juce::jlimit (-33.5f, 7.5f, vuPos);
            const float ang = juce::degreesToRadians ((db - G::needleDbVertical) * G::needleDegPerDb);
            const auto t = juce::AffineTransform::translation (-(G::needleSpriteW * 0.5f + 0.5f), -(G::needleSpriteH + 0.5f)).rotated (ang)
                               .translated (G::needlePivotX + 0.5f, G::needlePivotY + 0.5f);
            g.drawImageTransformed (needleShadow, t.translated (G::needleShadowDx, G::needleShadowDy));
            g.drawImageTransformed (needleBody, t);
            g.restoreState();
        }
    }

    // switch lamps
    for (int i = 0; i < 9; ++i)
        if (swState[(size_t) i])
            g.drawImageAt (swOn[(size_t) i], G::switches[i].x0, G::switches[i].y0);

    if (g.clipRegionIntersects (analyserRegion().getSmallestIntegerContainer()))
        drawAnalyser (g);
}

// ============================================================================================ timer
void AnbuEditor::timerCallback()
{
    // switches (host automation or clicks)
    bool swChanged = false;
    for (int i = 0; i < 9; ++i)
    {
        const bool on = switchOn (i);
        if (on != swState[(size_t) i]) { swState[(size_t) i] = on; swChanged = true; }
    }
    if (swChanged) repaint2x ({ 360.0f, 1190.0f, 1680.0f, 116.0f });

    // meters
    if (! frozen)
    {
        vuTarget = juce::jlimit (-40.0f, 10.0f, proc.meterVuDb.load());
        grDb = proc.meterGrDb.load();
        // VU ballistics: ~300 ms rise, slight overshoot
        const float w = juce::MathConstants<float>::twoPi * 2.1f, z = 0.78f, dt = 1.0f / 240.0f;
        for (int k = 0; k < 4; ++k)
        {
            const float acc = w * w * (vuTarget - vuPos) - 2.0f * z * w * vuVel;
            vuVel += acc * dt;
            vuPos += vuVel * dt;
        }
        if (vuPos < -40.0f) { vuPos = -40.0f; vuVel = juce::jmax (0.0f, vuVel); }
    }
    if (! expanded && std::abs (vuPos - drawnVu) > 0.01f)
    {
        drawnVu = vuPos;
        repaint2x ({ (float) G::faceX0, (float) G::faceY0, (float) (G::faceX1 - G::faceX0), (float) (G::faceY1 - G::faceY0) });
    }
    const int lit = juce::jlimit (0, 20, (int) std::floor (grDb + 0.5f));
    if (lit != grLitCount)
    {
        rebuildGrCache();
        if (! expanded) repaint2x ({ (float) G::grX0, (float) G::grY0, (float) (G::grX1 - G::grX0), (float) (G::grY1 - G::grY0) });
    }

    updateSpectrum();
    updateEqCurve();

    if (readoutKnob >= 0)
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (knobs[(size_t) readoutKnob].slider.isMouseButtonDown()) readoutTime = now;
        const double age = now - readoutTime;
        const float a = age < 1200.0 ? 1.0f : juce::jmax (0.0f, 1.0f - (float) (age - 1200.0) / 400.0f);
        if (std::abs (a - readoutAlpha) > 1.0e-4f) { readoutAlpha = a; repaint2x (plotRect()); }
        if (a <= 0.0f) readoutKnob = -1;
    }
}
