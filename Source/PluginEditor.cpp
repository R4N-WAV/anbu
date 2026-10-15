#include "PluginEditor.h"
#include "BinaryData.h"

using namespace anbu::ui;

// ============================================================================================ theme + fonts
namespace
{
juce::Colour hex (juce::uint32 rgb) { return juce::Colour (0xff000000u | rgb); }
juce::Colour rgba (int r, int g, int b, float a) { return juce::Colour ((juce::uint8) r, (juce::uint8) g, (juce::uint8) b, a); }
const juce::String minus = juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92"));
juce::String fixMinus (const juce::String& s) { return s.replace ("-", minus); }
}

const Theme& anbu::ui::theme()
{
    static const Theme t = []
    {
        Theme o;
        o.chassis = hex (0x1c0707);   o.plateTop = hex (0x9a2a26);  o.plateBot = hex (0x771b1d);
        o.edge = rgba (30, 0, 0, 0.5f); o.hi = rgba (255, 170, 150, 0.25f);
        o.label = hex (0xf6d283);     o.dim = hex (0xe3a596);       o.value = hex (0xfff1d8);
        o.recess = hex (0x4f0f10);    o.slot = hex (0x1c0707);      o.seat = hex (0x5f1415);
        o.deboss = hex (0x6a1819);    o.debHi = rgba (255, 160, 150, 0.3f); o.debLo = rgba (30, 0, 0, 0.55f);
        o.logoLit = hex (0xffe9b0);   o.zari = rgba (240, 198, 104, 0.6f);
        o.lamp = hex (0xffb341);      o.glow = rgba (255, 179, 65, 0.55f); o.lampOff = hex (0x561112);
        o.track = rgba (246, 210, 131, 0.2f);
        o.keyUp0 = hex (0xa8352f);    o.keyUp1 = hex (0x8a2424);    o.keyDn0 = hex (0x5c1416); o.keyDn1 = hex (0x6a1a1b);
        o.keyOn = hex (0xffe08a);
        o.screen = hex (0x130506);    o.screenEdge = rgba (240, 198, 104, 0.6f);
        o.grid = hex (0x2a1212);      o.gridMaj = hex (0x3e1b1b);
        return o;
    }();
    return t;
}

anbu::ui::Fonts::Fonts()
{
    auto load = [] (const char* name)
    {
        int size = 0;
        if (auto* data = BinaryData::getNamedResource (name, size))
            return juce::Typeface::createSystemTypefaceFor (data, (size_t) size);
        return juce::Typeface::Ptr();
    };
    w500 = load ("catamaran500_ttf");
    w600 = load ("catamaran600_ttf");
    w700 = load ("catamaran700_ttf");
    w800 = load ("catamaran800_ttf");
}

juce::Font anbu::ui::Fonts::get (int weight, float size) const
{
    auto tf = weight >= 800 ? w800 : weight >= 700 ? w700 : weight >= 600 ? w600 : w500;
    if (tf == nullptr) return juce::Font (juce::FontOptions().withPointHeight (size));
    return juce::Font (juce::FontOptions (tf).withPointHeight (size));
}

// ============================================================================================ helpers
namespace
{
constexpr float lineH = 1.64f;   // Catamaran: (1100 + 540) / 1000

float clamp01 (float x) { return juce::jlimit (0.0f, 1.0f, x); }
float lerp (float a, float b, float t) { return a + (b - a) * t; }

// CSS cubic-bezier timing
float bezier (float x1, float y1, float x2, float y2, float x)
{
    if (x <= 0.0f) return 0.0f;
    if (x >= 1.0f) return 1.0f;
    auto bx = [&] (float t) { const float u = 1.0f - t; return 3 * u * u * t * x1 + 3 * u * t * t * x2 + t * t * t; };
    auto by = [&] (float t) { const float u = 1.0f - t; return 3 * u * u * t * y1 + 3 * u * t * t * y2 + t * t * t; };
    float lo = 0.0f, hi = 1.0f, t = x;
    for (int i = 0; i < 24; ++i)
    {
        t = 0.5f * (lo + hi);
        if (bx (t) < x) lo = t; else hi = t;
    }
    return by (t);
}
float easeOut (float x)   { return bezier (0.0f, 0.0f, 0.58f, 1.0f, x); }
float easeIn (float x)    { return bezier (0.42f, 0.0f, 1.0f, 1.0f, x); }
float easeInOut (float x) { return bezier (0.42f, 0.0f, 0.58f, 1.0f, x); }

// Gaussian-looking blur (3 box passes) of a single-channel image
void blurAlpha (juce::Image& img, float sigma)
{
    if (sigma < 0.35f) return;
    const int r = juce::jmax (1, juce::roundToInt ((std::sqrt (4.0f * sigma * sigma + 1.0f) - 1.0f) * 0.5f));
    juce::Image::BitmapData bm (img, juce::Image::BitmapData::readWrite);
    const int w = bm.width, h = bm.height;
    std::vector<float> line ((size_t) juce::jmax (w, h)), tmp ((size_t) juce::jmax (w, h));
    auto pass = [r] (std::vector<float>& a, std::vector<float>& b, int n)
    {
        const float inv = 1.0f / (float) (2 * r + 1);
        float acc = 0.0f;
        for (int i = -r; i <= r; ++i) acc += (i >= 0 && i < n) ? a[(size_t) i] : 0.0f;
        for (int i = 0; i < n; ++i)
        {
            b[(size_t) i] = acc * inv;
            const int add = i + r + 1, sub = i - r;
            if (add < n) acc += a[(size_t) add];
            if (sub >= 0) acc -= a[(size_t) sub];
        }
    };
    for (int y = 0; y < h; ++y)
    {
        auto* p = bm.getLinePointer (y);
        for (int x = 0; x < w; ++x) line[(size_t) x] = p[x * bm.pixelStride];
        pass (line, tmp, w); pass (tmp, line, w); pass (line, tmp, w);
        for (int x = 0; x < w; ++x) p[x * bm.pixelStride] = (juce::uint8) juce::jlimit (0, 255, juce::roundToInt (tmp[(size_t) x]));
    }
    for (int x = 0; x < w; ++x)
    {
        for (int y = 0; y < h; ++y) line[(size_t) y] = bm.getPixelPointer (x, y)[0];
        pass (line, tmp, h); pass (tmp, line, h); pass (line, tmp, h);
        for (int y = 0; y < h; ++y) bm.getPixelPointer (x, y)[0] = (juce::uint8) juce::jlimit (0, 255, juce::roundToInt (tmp[(size_t) y]));
    }
}

// CSS-like shadow of a path, rendered at device resolution. blur = CSS blur radius.
void shadow (juce::Graphics& g, const juce::Path& p, juce::Colour c, float blur, juce::Point<float> off)
{
    if (c.getAlpha() == 0) return;
    if (blur <= 0.01f)
    {
        g.setColour (c);
        g.fillPath (p, juce::AffineTransform::translation (off));
        return;
    }
    const float sc = juce::jmax (0.25f, g.getInternalContext().getPhysicalPixelScaleFactor());
    auto area = p.getBounds().translated (off.x, off.y).expanded (blur * 1.6f + 2.0f)
                    .getIntersection (g.getClipBounds().toFloat().expanded (blur * 1.6f + 2.0f));
    if (area.getWidth() < 1.0f || area.getHeight() < 1.0f) return;
    area = area.getSmallestIntegerContainer().toFloat();
    const int w = juce::jmax (3, (int) std::ceil (area.getWidth() * sc)), h = juce::jmax (3, (int) std::ceil (area.getHeight() * sc));
    juce::Image img (juce::Image::SingleChannel, w, h, true, juce::SoftwareImageType());
    {
        juce::Graphics gi (img);
        gi.setColour (juce::Colours::white);
        gi.fillPath (p, juce::AffineTransform::translation (off.x - area.getX(), off.y - area.getY()).scaled (sc));
    }
    blurAlpha (img, 0.5f * blur * sc);
    g.setColour (c);
    g.drawImageTransformed (img, juce::AffineTransform::scale (1.0f / sc).translated (area.getX(), area.getY()), true);
}

// CSS inset shadow
void insetShadow (juce::Graphics& g, const juce::Path& shape, juce::Colour c, float blur, juce::Point<float> off)
{
    g.saveState();
    g.reduceClipRegion (shape);
    juce::Path ring;
    ring.addRectangle (shape.getBounds().expanded (blur * 2.0f + 8.0f + std::abs (off.x) + std::abs (off.y)));
    ring.addPath (shape);
    ring.setUsingNonZeroWinding (false);
    shadow (g, ring, c, blur, off);
    g.restoreState();
}

// "0 1px 0 colour" outer hairline under a shape
void hairlineBelow (juce::Graphics& g, const juce::Path& shape, juce::Colour c)
{
    juce::Path ring;
    ring.addRectangle (shape.getBounds().expanded (4.0f));
    ring.addPath (shape);
    ring.setUsingNonZeroWinding (false);
    g.saveState();
    g.reduceClipRegion (ring);
    g.setColour (c);
    g.fillPath (shape, juce::AffineTransform::translation (0.0f, 1.0f));
    g.restoreState();
}

// inside 1 px ring ("inset 0 0 0 1px")
void innerRing (juce::Graphics& g, juce::Rectangle<float> r, float radius, juce::Colour c)
{
    g.setColour (c);
    g.drawRoundedRectangle (r.reduced (0.5f), juce::jmax (0.0f, radius - 0.5f), 1.0f);
}

juce::Path roundRect (juce::Rectangle<float> r, float radius)
{
    juce::Path p;
    p.addRoundedRectangle (r, radius);
    return p;
}
juce::Path circle (float cx, float cy, float r)
{
    juce::Path p;
    p.addEllipse (cx - r, cy - r, 2 * r, 2 * r);
    return p;
}

void text (juce::Graphics& g, const juce::Font& f, const juce::String& s, juce::Rectangle<float> r, juce::Justification j, juce::Colour c)
{
    g.setFont (f);
    g.setColour (c);
    g.drawText (s, r, j, false);
}

juce::Path textPath (const juce::Font& f, const juce::String& s, juce::Rectangle<float> r, juce::Justification j)
{
    juce::GlyphArrangement ga;
    ga.addFittedText (f, s, r.getX(), r.getY(), r.getWidth(), r.getHeight(), j, 1, 1.0f);
    juce::Path p;
    ga.createPath (p);
    return p;
}

// debossed lettering: same hue as the plate, darker, a hairline of light below and shade above
void debossText (juce::Graphics& g, const juce::Font& f, const juce::String& s, juce::Rectangle<float> r, juce::Justification j, float alpha = 1.0f)
{
    const auto& T = theme();
    text (g, f, s, r.translated (0.0f, 1.0f), j, T.debHi.withMultipliedAlpha (alpha));
    text (g, f, s, r.translated (0.0f, -1.0f), j, T.debLo.withMultipliedAlpha (alpha));
    text (g, f, s, r, j, T.deboss.withMultipliedAlpha (alpha));
}

void strokeGlow (juce::Graphics& g, const juce::Path& p, juce::Colour c, float core, float alpha)
{
    if (alpha <= 0.002f) return;
    const juce::PathStrokeType::EndCapStyle cap = juce::PathStrokeType::rounded;
    g.setColour (c.withAlpha (0.08f * alpha)); g.strokePath (p, juce::PathStrokeType (core + 7.0f, juce::PathStrokeType::curved, cap));
    g.setColour (c.withAlpha (0.16f * alpha)); g.strokePath (p, juce::PathStrokeType (core + 3.5f, juce::PathStrokeType::curved, cap));
    g.setColour (c.withAlpha (0.66f * alpha)); g.strokePath (p, juce::PathStrokeType (core, juce::PathStrokeType::curved, cap));
}

// ---------------------------------------------------------------- layout (design units, 1000 x 700)
struct Module { float x, w; };
constexpr Module modules[3] = { { 24.0f, 220.0f }, { 256.0f, 598.0f }, { 866.0f, 110.0f } };
constexpr float modTop = 24.0f, modH = 652.0f;

constexpr float faderX = 134.0f, faderY0 = 350.0f, faderY1 = 528.0f;
const juce::Rectangle<float> faderHit { 104.0f, 334.0f, 60.0f, 210.0f };

constexpr float lampX = 56.5f, lampYC = 608.5f, lampYL = 645.0f;
constexpr float modeBtnX = 204.0f, modeBtnY = 626.5f;

const juce::Rectangle<float> bezelR { 280.0f, 48.0f, 550.0f, 252.0f };
const juce::Rectangle<float> faceR { 288.0f, 56.0f, 534.0f, 236.0f };
constexpr float pivX = 555.0f, pivY = 356.0f, meterR = 222.0f;
const float meterSpan = juce::degreesToRadians (38.0f);
const juce::Rectangle<float> pillGR { 732.0f, 66.0f, 36.0f, 22.0f }, pillOut { 770.0f, 66.0f, 40.0f, 22.0f };

const juce::Rectangle<float> dispR { 280.0f, 318.0f, 550.0f, 220.0f };
constexpr float plotX0 = 320.0f, plotX1 = 814.0f, plotY0 = 356.0f, plotY1 = 508.0f;
constexpr float specTop = -36.0f, specBot = -96.0f, toneRange = 12.0f;
constexpr float harmBoostDb = 24.0f;    // added harmonics are drawn magnified (x16) so they can be seen
const juce::Rectangle<float> expandR { 796.0f, 324.0f, 26.0f, 24.0f };

const juce::Rectangle<float> bankOs { 280.0f, 580.0f, 280.0f, 40.0f }, bankPh { 590.0f, 580.0f, 240.0f, 40.0f };
const juce::Rectangle<float> noiseKeyR { 889.0f, 462.0f, 64.0f, 32.0f };

juce::Rectangle<float> keyRect (int i)
{
    if (i < 4) return { bankOs.getX() + 4.0f + 69.0f * (float) i, bankOs.getY() + 4.0f, 65.0f, 32.0f };
    if (i < 6) return { bankPh.getX() + 4.0f + 118.0f * (float) (i - 4), bankPh.getY() + 4.0f, 114.0f, 32.0f };
    return noiseKeyR;
}
float keyFlashX (int i) { return i < 4 ? 300.0f + 70.0f * (float) i : i < 6 ? 610.0f + 120.0f * (float) (i - 4) : 921.0f; }
const char* const keyText[7] = { "Off", "2\xc3\x97", "4\xc3\x97", "8\xc3\x97", "Low latency", "Linear", "Noise" };

const juce::Rectangle<float> logoWordR { 24.0f, 52.0f, 220.0f, 48.4f }, logoByR { 24.0f, 102.0f, 220.0f, 21.3f };
const juce::String logoWord = juce::String (juce::CharPointer_UTF8 ("\xe0\xae\x85\xe0\xae\xa9\xe0\xaf\x8d\xe0\xae\xaa\xe0\xaf\x81"));  // அன்பு
const juce::String logoA = juce::String (juce::CharPointer_UTF8 ("\xe0\xae\x85"));                                                     // அ
const juce::String logoRest = juce::String (juce::CharPointer_UTF8 ("\xe0\xae\xa9\xe0\xaf\x8d\xe0\xae\xaa\xe0\xaf\x81"));             // ன்பு

// needle scale
float vuFrac (float d) { return (std::pow (10.0f, d / 20.0f) - 0.1f) / (std::pow (10.0f, 3.0f / 20.0f) - 0.1f); }
juce::Point<float> meterPt (float r, float a) { return { pivX + r * std::sin (a), pivY - r * std::cos (a) }; }
float fracAngle (float f) { return -meterSpan + 2.0f * meterSpan * f; }

// analyser maths shared by the panel and the window
float xOfF (double f, float x0, float x1) { return x0 + (x1 - x0) * (float) (std::log10 (f / 20.0) / 3.0); }

juce::Path specPath (const Spectrum& s, const std::array<float, Spectrum::nPts>& v, float offsetDb, float x0, float x1, float y0, float y1,
                     float top, float bot, bool closed)
{
    juce::Path p;
    for (int i = 0; i < Spectrum::nPts; ++i)
    {
        const float x = xOfF (s.freqs[(size_t) i], x0, x1);
        const float y = juce::jlimit (y0 - 2.0f, y1 + 2.0f, y0 + (top - (v[(size_t) i] + offsetDb)) / (top - bot) * (y1 - y0));
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    if (closed)
    {
        p.lineTo (x1, y1 + 2.0f);
        p.lineTo (x0, y1 + 2.0f);
        p.closeSubPath();
    }
    return p;
}

juce::Path tonePath (const Spectrum& s, float x0, float x1, float y0, float y1, float range)
{
    juce::Path p;
    const float ym = 0.5f * (y0 + y1), k = 0.5f * (y1 - y0) / range;
    for (int i = 0; i < Spectrum::nPts; ++i)
    {
        const float x = xOfF (s.freqs[(size_t) i], x0, x1);
        const float y = ym - k * juce::jlimit (-range * 1.1f, range * 1.1f, s.tone[(size_t) i]);
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    return p;
}

void drawExpandIcon (juce::Graphics& g, float x, float y, juce::Colour c)
{
    juce::Path p;
    p.startNewSubPath (x + 8.0f, y + 1.5f); p.lineTo (x + 12.5f, y + 1.5f); p.lineTo (x + 12.5f, y + 6.0f);
    p.startNewSubPath (x + 12.5f, y + 1.5f); p.lineTo (x + 7.5f, y + 6.5f);
    p.startNewSubPath (x + 2.0f, y + 6.0f); p.lineTo (x + 2.0f, y + 12.0f); p.lineTo (x + 8.0f, y + 12.0f);
    p.startNewSubPath (x + 1.5f, y + 12.5f); p.lineTo (x + 6.5f, y + 7.5f);
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

const char* const knobNames[6] = { "Peak reduction", "Input", "Output", "Mix", "Noise level", "Color" };
}

// ============================================================================================ analyser window
namespace anbu::ui
{
class AnalyserView : public juce::Component
{
public:
    static constexpr int W = 920, H = 540;

    AnalyserView (const Spectrum& s, const Fonts& f, std::function<juce::String()> readoutFn, std::function<float()> readoutAlphaFn,
                  std::function<void()> closeFn)
        : spec (s), fonts (f), readout (std::move (readoutFn)), readoutAlpha (std::move (readoutAlphaFn)), onClose (std::move (closeFn))
    {
        setOpaque (true);
        setSize (W, H);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& T = theme();
        g.addTransform (juce::AffineTransform::scale ((float) getWidth() / (float) W, (float) getHeight() / (float) H));
        const juce::Rectangle<float> all (0.0f, 0.0f, (float) W, (float) H);
        g.setColour (T.screen);
        g.fillRect (all);

        // header
        const juce::Rectangle<float> head (0.0f, 0.0f, (float) W, 40.0f);
        g.setGradientFill (juce::ColourGradient::vertical (T.plateTop, 0.0f, T.plateBot, 40.0f));
        g.fillRect (head);
        g.setColour (T.zari.withAlpha (0.55f));
        g.fillRect (0.0f, 39.0f, (float) W, 1.0f);
        const auto fLogo = fonts.get (800, 22.0f), fHead = fonts.get (700, 15.0f);
        const float wLogo = juce::GlyphArrangement::getStringWidth (fLogo, logoWord);
        debossText (g, fLogo, logoWord, { 18.0f, 20.0f - 0.5f * 22.0f * lineH, wLogo + 4.0f, 22.0f * lineH }, juce::Justification::centredLeft);
        text (g, fHead, "Analyser", { 18.0f + wLogo + 12.0f, 20.0f - 0.5f * 15.0f * lineH + 1.5f, 120.0f, 15.0f * lineH }, juce::Justification::centredLeft, T.label);
        const float ra = readoutAlpha ? readoutAlpha() : 0.0f;
        if (ra > 0.0f && readout)
            text (g, fHead, readout(), { 460.0f, 20.0f - 0.5f * 15.0f * lineH + 1.0f, 400.0f, 15.0f * lineH }, juce::Justification::centredRight, T.value.withAlpha (ra));
        {
            const auto c = closeRect();
            juce::Path x;
            const float x0 = c.getX() + 7.0f, y0 = c.getY() + 7.0f;
            x.startNewSubPath (x0 + 2.0f, y0 + 2.0f); x.lineTo (x0 + 12.0f, y0 + 12.0f);
            x.startNewSubPath (x0 + 12.0f, y0 + 2.0f); x.lineTo (x0 + 2.0f, y0 + 12.0f);
            g.setColour (T.label.withAlpha (hoverClose ? 1.0f : 0.85f));
            g.strokePath (x, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // plot
        const float x0 = 58.0f, x1 = 862.0f, y0 = 62.0f, y1 = 436.0f;
        const float top = -24.0f, bot = range90 ? -114.0f : -84.0f, step = range90 ? 18.0f : 12.0f;
        auto sy = [&] (float d) { return y0 + (top - d) / (top - bot) * (y1 - y0); };
        const float ym = 0.5f * (y0 + y1);
        auto ey = [&] (float d) { return ym - d / 6.0f * 0.5f * (y1 - y0); };
        {
            juce::Path grid, major;
            for (int f : { 30, 40, 50, 60, 70, 80, 90, 200, 300, 400, 500, 600, 700, 800, 900, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000 })
            { grid.startNewSubPath (xOfF (f, x0, x1), y0); grid.lineTo (xOfF (f, x0, x1), y1); }
            for (int f : { 100, 1000, 10000 }) { major.startNewSubPath (xOfF (f, x0, x1), y0); major.lineTo (xOfF (f, x0, x1), y1); }
            for (float d = top - step; d > bot + 0.1f; d -= step) { grid.startNewSubPath (x0, sy (d)); grid.lineTo (x1, sy (d)); }
            major.addRectangle (x0, y0, x1 - x0, y1 - y0);
            major.startNewSubPath (x0, ey (0.0f)); major.lineTo (x1, ey (0.0f));
            g.setColour (T.grid);    g.strokePath (grid, juce::PathStrokeType (1.0f));
            g.setColour (T.gridMaj); g.strokePath (major, juce::PathStrokeType (1.0f));
        }
        const auto fLab = fonts.get (500, 13.0f);
        for (float d = top; d >= bot - 0.1f; d -= step)
            text (g, fLab, fixMinus (juce::String ((int) d)), { x0 - 66.0f, sy (d) - 9.0f, 56.0f, 18.0f }, juce::Justification::centredRight, hex (0x8f8574));
        for (int d : { -6, -3, 0, 3, 6 })
            text (g, fLab, d > 0 ? "+" + juce::String (d) : fixMinus (juce::String (d)), { x1 + 10.0f, ey ((float) d) - 9.0f, 56.0f, 18.0f },
                  juce::Justification::centredLeft, hex (0xd9ccb4));
        const std::pair<int, const char*> fl[] = { { 20, "20" }, { 50, "50" }, { 100, "100" }, { 200, "200" }, { 500, "500" }, { 1000, "1k" },
                                                   { 2000, "2k" }, { 5000, "5k" }, { 10000, "10k" }, { 20000, "20k" } };
        for (auto& [f, t] : fl)
            text (g, fLab, t, { xOfF (f, x0, x1) - 28.0f, y1 + 13.0f, 56.0f, 18.0f }, juce::Justification::centred, hex (0xa89e8b));
        text (g, fLab, "dB", { x0 - 66.0f, 43.0f, 56.0f, 18.0f }, juce::Justification::centredRight, hex (0x6f6858));
        text (g, fLab, "Tone", { x1 + 10.0f, 43.0f, 56.0f, 18.0f }, juce::Justification::centredLeft, hex (0x6f6858));

        g.saveState();
        g.reduceClipRegion (juce::Rectangle<float> (x0, y0, x1 - x0, y1 - y0).toNearestInt());
        if (spec.active)
        {
            g.setColour (hex (0xa89272).withAlpha (0.28f));
            g.fillPath (specPath (spec, spec.out, 0.0f, x0, x1, y0, y1, top, bot, true));
            g.setColour (hex (0xc8b08a));
            g.strokePath (specPath (spec, spec.out, 0.0f, x0, x1, y0, y1, top, bot, false), juce::PathStrokeType (1.4f, juce::PathStrokeType::curved));
            if (spec.inputActive)
            {
                juce::Path dashed;
                const float dash[] = { 4.0f, 3.0f };
                juce::PathStrokeType (1.2f).createDashedStroke (dashed, specPath (spec, spec.in, 0.0f, x0, x1, y0, y1, top, bot, false), dash, 2);
                g.setColour (hex (0x8e97bf));
                g.fillPath (dashed);
            }
            g.setColour (hex (0xf1e3c6).withAlpha (0.6f));
            g.strokePath (specPath (spec, spec.peak, 0.0f, x0, x1, y0, y1, top, bot, false), juce::PathStrokeType (1.0f));
            g.setColour (hex (0xf07a3a).withAlpha (0.55f));
            g.fillPath (specPath (spec, spec.harm, harmBoostDb, x0, x1, y0, y1, top, bot, true));
            g.setColour (hex (0xf58a4e));
            g.strokePath (specPath (spec, spec.harm, harmBoostDb, x0, x1, y0, y1, top, bot, false), juce::PathStrokeType (1.3f, juce::PathStrokeType::curved));
        }
        {
            const auto tp = tonePath (spec, x0, x1, y0, y1, 6.0f);
            g.setColour (hex (0xf4eada).withAlpha (0.15f));
            g.strokePath (tp, juce::PathStrokeType (9.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (hex (0xf4eada));
            g.strokePath (tp, juce::PathStrokeType (2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.restoreState();

        // footer
        g.setColour (T.zari.withAlpha (0.25f));
        g.fillRect (0.0f, (float) H - 48.0f, (float) W, 1.0f);
        const auto fLeg = fonts.get (500, 14.0f);
        const float fy = (float) H - 24.0f;
        float lx = 18.0f;
        auto item = [&] (int kind, const juce::String& t)
        {
            switch (kind)
            {
                case 0: g.setColour (hex (0xa89272).withAlpha (0.7f)); g.fillRoundedRectangle (lx, fy - 4.5f, 14.0f, 9.0f, 1.0f); break;
                case 1: g.setColour (hex (0x8e97bf)); for (int i = 0; i < 3; ++i) g.fillRect (lx + 5.0f * (float) i, fy - 1.0f, 3.0f, 2.0f); break;
                case 2: g.setColour (hex (0xf1e3c6)); g.fillRect (lx, fy - 0.5f, 14.0f, 1.0f); break;
                case 3: g.setColour (hex (0xf07a3a)); g.fillRoundedRectangle (lx, fy - 4.5f, 14.0f, 9.0f, 1.0f); break;
                default: g.setColour (hex (0xf4eada)); g.fillRect (lx, fy - 1.0f, 14.0f, 2.0f); break;
            }
            const float tw = juce::GlyphArrangement::getStringWidth (fLeg, t);
            text (g, fLeg, t, { lx + 21.0f, fy - 0.5f * 14.0f * lineH + 1.0f, tw + 4.0f, 14.0f * lineH }, juce::Justification::centredLeft, hex (0x9a907e));
            lx += 21.0f + tw + 20.0f;
        };
        item (0, "Output"); item (1, "Input"); item (2, "Peak hold");
        item (3, juce::String (juce::CharPointer_UTF8 ("Harmonics \xc3\x97" "16"))); item (4, "Tone");

        const auto fBtn = fonts.get (600, 14.0f);
        juce::Rectangle<float> b60, b90, box;
        rangeRects (b60, b90, box);
        text (g, fLeg, "Range", { box.getX() - 70.0f, fy - 0.5f * 14.0f * lineH + 1.0f, 60.0f, 14.0f * lineH }, juce::Justification::centredRight, T.label);
        const auto boxPath = roundRect (box, 4.0f);
        g.setColour (hex (0x0e0303));
        g.fillPath (boxPath);
        insetShadow (g, boxPath, juce::Colours::black.withAlpha (0.9f), 3.0f, { 0.0f, 1.0f });
        for (int i = 0; i < 2; ++i)
        {
            const auto r = i == 0 ? b60 : b90;
            const bool on = (i == 1) == range90;
            if (on) { g.setColour (T.label.withAlpha (0.22f)); g.fillRoundedRectangle (r, 3.0f); }
            text (g, fBtn, i == 0 ? "60 dB" : "90 dB", r.translated (0.0f, 1.0f), juce::Justification::centred, on ? T.label : hex (0x7c7464));
        }
        innerRing (g, all, 0.0f, T.zari.withAlpha (0.4f));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto p = toDesign (e.position);
        dragging = false;
        if (closeRect().contains (p)) { if (onClose) onClose(); return; }
        juce::Rectangle<float> b60, b90, box;
        rangeRects (b60, b90, box);
        if (b60.contains (p)) { range90 = false; repaint(); return; }
        if (b90.contains (p)) { range90 = true; repaint(); return; }
        if (p.y < 40.0f)
            if (auto* top = getTopLevelComponent())
            {
                dragging = true;
                dragger.startDraggingComponent (top, e.getEventRelativeTo (top));
            }
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging)
            if (auto* top = getTopLevelComponent())
                dragger.dragComponent (top, e.getEventRelativeTo (top), nullptr);
    }
    void mouseMove (const juce::MouseEvent& e) override
    {
        const auto p = toDesign (e.position);
        juce::Rectangle<float> b60, b90, box;
        rangeRects (b60, b90, box);
        const bool hc = closeRect().contains (p);
        if (hc != hoverClose) { hoverClose = hc; repaint(); }
        setMouseCursor (hc || b60.contains (p) || b90.contains (p) ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    }
    void mouseExit (const juce::MouseEvent&) override
    {
        if (hoverClose) { hoverClose = false; repaint(); }
    }

private:
    juce::Point<float> toDesign (juce::Point<float> p) const
    {
        return { p.x * (float) W / (float) juce::jmax (1, getWidth()), p.y * (float) H / (float) juce::jmax (1, getHeight()) };
    }
    juce::Rectangle<float> closeRect() const { return { (float) W - 42.0f, 6.0f, 28.0f, 28.0f }; }
    void rangeRects (juce::Rectangle<float>& b60, juce::Rectangle<float>& b90, juce::Rectangle<float>& box) const
    {
        const auto f = fonts.get (600, 14.0f);
        const float w60 = juce::GlyphArrangement::getStringWidth (f, "60 dB") + 20.0f, w90 = juce::GlyphArrangement::getStringWidth (f, "90 dB") + 20.0f;
        const float bw = w60 + w90 + 3.0f + 6.0f, right = (float) W - 18.0f, cy = (float) H - 24.0f;
        box = { right - bw, cy - 16.0f, bw, 32.0f };
        b60 = { box.getX() + 3.0f, cy - 13.0f, w60, 26.0f };
        b90 = { b60.getRight() + 3.0f, cy - 13.0f, w90, 26.0f };
    }

    const Spectrum& spec;
    const Fonts& fonts;
    std::function<juce::String()> readout;
    std::function<float()> readoutAlpha;
    std::function<void()> onClose;
    bool range90 = true, dragging = false, hoverClose = false;
    juce::ComponentDragger dragger;
};
}

class AnbuEditor::AnalyserWindow : public juce::DocumentWindow
{
public:
    explicit AnalyserWindow (AnbuEditor& e)
        : juce::DocumentWindow ("Anbu analyser", theme().screen, 0, true)
    {
        setUsingNativeTitleBar (false);
        setTitleBarHeight (0);
        view = new AnalyserView (e.spec, e.fonts, [&e] { return e.readoutText(); }, [&e] { return e.readoutAlpha; },
                                 [this] { setVisible (false); });
        setContentOwned (view, true);
        setResizable (true, true);
        setResizeLimits (640, 376, 1840, 1080);
        getConstrainer()->setFixedAspectRatio ((double) AnalyserView::W / (double) AnalyserView::H);
        setAlwaysOnTop (true);
    }
    void closeButtonPressed() override { setVisible (false); }
    AnalyserView* view = nullptr;
};

// ============================================================================================ construction
juce::Slider::SliderLayout AnbuEditor::InvisibleLook::getSliderLayout (juce::Slider& s)
{
    juce::Slider::SliderLayout l;
    auto b = s.getLocalBounds();
    if (s.isVertical())
    {
        const int trim = juce::roundToInt ((float) b.getHeight() * 16.0f / 210.0f);
        b = b.reduced (0, trim);
    }
    l.sliderBounds = b;
    return l;
}

// window-corner grip: three short gold hairlines
void AnbuEditor::InvisibleLook::drawCornerResizer (juce::Graphics& g, int w, int h, bool isMouseOver, bool isMouseDragging)
{
    const float s = (float) juce::jmin (w, h);
    g.setColour (theme().zari.withAlpha (isMouseOver || isMouseDragging ? 0.7f : 0.35f));
    for (int i = 1; i <= 3; ++i)
    {
        const float d = s * 0.22f * (float) i;
        g.drawLine ((float) w - d, (float) h - 2.0f, (float) w - 2.0f, (float) h - d, 1.0f);
    }
}

bool AnbuEditor::RoundSlider::hitTest (int x, int y)
{
    const auto c = getLocalBounds().toFloat().getCentre();
    return c.getDistanceFrom ({ (float) x, (float) y }) <= radius;
}

AnbuEditor::AnbuEditor (AnbuProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setOpaque (true);
    setLookAndFeel (&invisible);
    struct Def { const char* id; float cx, cy, D, rr; bool bipolar, big; float gap; };
    const Def defs[5] = {
        { ids::pr,         134.0f, 226.0f, 98.0f, 62.0f, false, true,  4.0f },
        { ids::input,      921.0f, 112.0f, 54.0f, 36.0f, true,  false, 2.0f },
        { ids::output,     921.0f, 238.0f, 54.0f, 36.0f, true,  false, 2.0f },
        { ids::mix,        921.0f, 364.0f, 54.0f, 36.0f, false, false, 2.0f },
        { ids::noiseLevel, 921.0f, 556.0f, 40.0f, 28.0f, false, false, 2.0f } };

    for (int k = 0; k < 5; ++k)
    {
        auto& kn = knobs[(size_t) k];
        const auto& d = defs[k];
        kn.id = d.id; kn.name = knobNames[k];
        kn.cx = d.cx; kn.cy = d.cy; kn.D = d.D; kn.rr = d.rr; kn.bipolar = d.bipolar; kn.big = d.big; kn.labelGap = d.gap;
        kn.slider.setLookAndFeel (&invisible);
        kn.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        kn.slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        kn.slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        kn.slider.setMouseDragSensitivity (kn.big ? 320 : 260);
        kn.slider.setVelocityBasedMode (false);
        addAndMakeVisible (kn.slider);
        kn.param = proc.apvts.getParameter (kn.id);
        kn.att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, kn.id, kn.slider);
        if (kn.param != nullptr)
            kn.slider.setDoubleClickReturnValue (true, kn.param->convertFrom0to1 (kn.param->getDefaultValue()));
        kn.slider.onValueChange = [this, k]
        {
            auto& me = knobs[(size_t) k];
            repaintDesign (knobArea (me));
            if (me.slider.isMouseOverOrDragging()) setReadout (k);
        };
        kn.slider.onDragStart = [this, k] { if (introRunning) skipIntro(); setReadout (k); };
    }

    fader.setLookAndFeel (&invisible);
    fader.setSliderStyle (juce::Slider::LinearVertical);
    fader.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    fader.setSliderSnapsToMousePosition (false);
    addAndMakeVisible (fader);
    faderAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, ids::color, fader);
    colorParam = proc.apvts.getParameter (ids::color);
    if (colorParam != nullptr)
        fader.setDoubleClickReturnValue (true, colorParam->convertFrom0to1 (colorParam->getDefaultValue()));
    fader.onValueChange = [this]
    {
        repaintDesign (faderHit.expanded (10.0f, 14.0f));
        if (fader.isMouseOverOrDragging()) setReadout (5);
    };
    fader.onDragStart = [this] { if (introRunning) skipIntro(); setReadout (5); };

    // analyser
    for (int i = 0; i < fftSize; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) fftSize);
    for (int i = 0; i < Spectrum::nPts; ++i)
        spec.freqs[(size_t) i] = 20.0 * std::pow (1000.0, (double) i / (Spectrum::nPts - 1));
    spec.out.fill (-140.0f); spec.in.fill (-140.0f); spec.harm.fill (-140.0f); spec.peak.fill (-140.0f); spec.tone.fill (0.0f);

    for (int i = 0; i < 7; ++i) keyState[(size_t) i] = keyOn (i);
    modeLimit = (int) std::lround (proc.apvts.getRawParameterValue (ids::mode)->load()) == 1;
    grDb = proc.meterGrDb.load();
    needlePos = meterFrac();
    updateTone();

    if (! proc.uiIntroPlayed)
    {
        proc.uiIntroPlayed = true;
        introRunning = true;
    }

    setResizable (true, true);
    setResizeLimits (700, 490, 1600, 1120);
    getConstrainer()->setFixedAspectRatio ((double) baseW / (double) baseH);
    setSize (baseW, baseH);
    startTimerHz (60);
}

AnbuEditor::~AnbuEditor()
{
    stopTimer();
    analyserWindow = nullptr;
    for (auto& kn : knobs) kn.slider.setLookAndFeel (nullptr);
    fader.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void AnbuEditor::resized()
{
    const float s = scale();
    for (auto& kn : knobs)
    {
        const float r = (kn.rr + 8.0f) * s;
        kn.slider.radius = r;
        kn.slider.setBounds (juce::Rectangle<float> (kn.cx * s - r, kn.cy * s - r, 2 * r, 2 * r).toNearestInt());
    }
    fader.setBounds ((faderHit * s).toNearestInt());
}

void AnbuEditor::repaintDesign (juce::Rectangle<float> r)
{
    repaint ((r * scale()).getSmallestIntegerContainer().expanded (2));
}

float AnbuEditor::pos01 (const juce::Slider& s) const
{
    return clamp01 ((float) ((s.getValue() - s.getMinimum()) / juce::jmax (1.0e-9, s.getMaximum() - s.getMinimum())));
}

juce::Rectangle<float> AnbuEditor::knobArea (const Knob& kn) const
{
    const float R = kn.rr + 8.0f + 6.0f;
    return { kn.cx - R, kn.cy - R, 2 * R, 2 * R };
}

// ============================================================================================ intro timeline
double AnbuEditor::introT() const
{
    if (! introRunning) return 10.0;
    if (introFixed >= 0.0) return introFixed;
    if (introStart < 0.0) return 0.0;
    return (juce::Time::getMillisecondCounterHiRes() - introStart) * 0.001;
}

float AnbuEditor::flashAt (float x) const
{
    if (! introRunning) return 0.0f;
    const float u = ((float) introT() - (0.95f + 0.00045f * x)) / 0.55f;
    if (u <= 0.0f || u >= 1.0f) return 0.0f;
    return u < 0.3f ? easeInOut (u / 0.3f) : 1.0f - easeInOut ((u - 0.3f) / 0.7f);
}

float AnbuEditor::valueEase (float x) const
{
    if (! introRunning) return 1.0f;
    return bezier (0.2f, 0.75f, 0.25f, 1.0f, ((float) introT() - (1.75f + 0.0002f * x)) / 0.8f);
}

void AnbuEditor::skipIntro()
{
    introRunning = false;
    introFixed = -1.0;
    proc.uiIntroPlayed = true;
    repaint();
}

void AnbuEditor::setIntroFrame (double seconds)
{
    introRunning = true;
    introFixed = seconds;
    repaint();
}

// ============================================================================================ static layer
void AnbuEditor::renderStatic (juce::Graphics& g)
{
    const auto& T = theme();
    g.fillAll (T.chassis);

    // modules
    for (const auto& m : modules)
    {
        const juce::Rectangle<float> r (m.x, modTop, m.w, modH);
        const auto p = roundRect (r, 4.0f);
        shadow (g, p, juce::Colours::black.withAlpha (0.5f), 10.0f, { 0.0f, 4.0f });
        g.setGradientFill (juce::ColourGradient::vertical (T.plateTop, r.getY(), T.plateBot, r.getBottom()));
        g.fillPath (p);
        innerRing (g, r, 4.0f, T.edge);
        g.saveState();
        g.reduceClipRegion (p);
        g.setColour (T.hi);
        g.fillRect (r.getX(), r.getY(), r.getWidth(), 1.0f);
        g.restoreState();
        g.setColour (T.zari);
        g.drawRoundedRectangle (r.reduced (7.5f), 2.0f, 1.0f);
        g.setColour (T.zari.withMultipliedAlpha (0.55f));
        g.drawRoundedRectangle (r.reduced (10.5f), 1.0f, 1.0f);

        // screws: plate-toned heads with a slot
        std::vector<float> xs;
        if (m.w > 300.0f) xs = { m.x + 36.0f, m.x + m.w - 36.0f };
        else xs = { m.x + 0.5f * m.w };
        for (float sx : xs)
            for (float sy : { 36.0f, 664.0f })
            {
                const auto head = circle (sx, sy, 5.0f);
                hairlineBelow (g, head, T.hi);
                g.setColour (T.seat);
                g.fillPath (head);
                g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.22f), sx - 1.2f, sy - 1.8f,
                                                         juce::Colours::white.withAlpha (0.0f), sx - 1.2f + 4.3f, sy - 1.8f, true));
                g.fillPath (head);
                g.saveState();
                g.reduceClipRegion (head);
                const float a = juce::degreesToRadians (125.0f);
                juce::Path slot;
                slot.addRectangle (-6.0f, -0.98f, 12.0f, 1.96f);
                g.setColour (juce::Colours::black.withAlpha (0.55f));
                g.fillPath (slot, juce::AffineTransform::rotation (a - juce::MathConstants<float>::halfPi).translated (sx, sy));
                g.restoreState();
                g.setColour (juce::Colours::black.withAlpha (0.35f));
                g.drawEllipse (sx - 4.5f, sy - 4.5f, 9.0f, 9.0f, 1.0f);
            }
    }

    // knobs: seat, track, body, knurl, cap (the pointer and the lit ring are dynamic)
    for (const auto& kn : knobs)
    {
        const float cx = kn.cx, cy = kn.cy, D = kn.D;
        const auto seat = circle (cx, cy, 0.5f * D + 6.0f);
        hairlineBelow (g, seat, T.hi);
        g.setColour (T.seat);
        g.fillPath (seat);
        insetShadow (g, seat, rgba (60, 25, 0, 0.55f), 4.0f, { 0.0f, 2.0f });

        juce::Path arc;
        arc.addCentredArc (cx, cy, kn.rr, kn.rr, 0.0f, juce::degreesToRadians (-135.0f), juce::degreesToRadians (135.0f), true);
        g.setColour (T.track);
        g.strokePath (arc, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const auto body = circle (cx, cy, 0.5f * D);
        shadow (g, body, juce::Colours::black.withAlpha (0.65f), 0.18f * D, { 0.0f, 0.09f * D });
        g.setColour (hex (0x0c0c10));
        g.fillPath (body);
        const float rk = 0.5f * D - 1.0f;
        g.setColour (hex (0x121319));
        g.fillEllipse (cx - rk, cy - rk, 2 * rk, 2 * rk);
        {
            juce::Path wedges;
            for (int i = 0; i < 90; ++i)
            {
                const float a0 = juce::degreesToRadians (4.0f * (float) i), a1 = juce::degreesToRadians (4.0f * (float) i + 2.0f);
                wedges.addPieSegment (cx - rk, cy - rk, 2 * rk, 2 * rk, a0, a1, 0.0f);
            }
            g.setColour (hex (0x262833));
            g.fillPath (wedges);
        }
        const float c = 0.74f * D, cl = cx - 0.5f * c, ct = cy - 0.5f * c;
        const auto cap = circle (cx, cy, 0.5f * c);
        shadow (g, cap, juce::Colours::black.withAlpha (0.7f), 2.0f, { 0.0f, 1.0f });
        {
            juce::ColourGradient cg (hex (0x33363f), cl + 0.44f * c, ct + 0.32f * c, hex (0x15161c), cl + 0.44f * c + 0.881f * c, ct + 0.32f * c, true);
            cg.addColour (0.5, hex (0x1f2129));
            g.setGradientFill (cg);
            g.fillPath (cap);
        }
        g.saveState();
        g.reduceClipRegion (cap);
        {
            juce::Path ring;
            ring.addRectangle (cap.getBounds().expanded (4.0f));
            ring.addPath (cap, juce::AffineTransform::translation (0.0f, 1.0f));
            ring.setUsingNonZeroWinding (false);
            g.setColour (juce::Colours::white.withAlpha (0.08f));
            g.fillPath (ring);
        }
        g.restoreState();

        // label
        const float R = kn.rr + 8.0f, size = kn.big ? 15.0f : 13.0f;
        text (g, fonts.get (500, size), kn.name, { cx - 70.0f, cy + R + kn.labelGap, 140.0f, size * lineH }, juce::Justification::centred, T.label);
    }

    // Color fader: slot, ticks, numbers, label
    {
        const juce::Rectangle<float> slotR (faderX - 3.0f, faderY0 - 8.0f, 6.0f, faderY1 - faderY0 + 16.0f);
        const auto slot = roundRect (slotR, 3.0f);
        hairlineBelow (g, slot, T.hi);
        g.setColour (T.slot);
        g.fillPath (slot);
        insetShadow (g, slot, juce::Colours::black.withAlpha (0.9f), 3.0f, { 0.0f, 2.0f });
        const auto fNum = fonts.get (500, 12.0f);
        for (int i = 0; i <= 10; ++i)
        {
            const float y = faderY1 - (faderY1 - faderY0) * (float) i / 10.0f;
            const bool maj = i % 5 == 0;
            g.setColour (T.label.withAlpha (maj ? 0.8f : 0.45f));
            g.fillRect (faderX + 28.0f, y - 0.5f, maj ? 12.0f : 7.0f, 1.0f);
            if (maj)
                text (g, fNum, juce::String (i * 10), { faderX + 46.0f, y - 9.0f, 40.0f, 12.0f * lineH }, juce::Justification::centredLeft, T.dim);
        }
        text (g, fonts.get (500, 15.0f), "Color", { faderX - 70.0f, 548.0f, 140.0f, 15.0f * lineH }, juce::Justification::centred, T.label);
    }

    // mode button
    {
        const auto rec = circle (modeBtnX, modeBtnY, 19.0f);
        g.setColour (T.recess);
        g.fillPath (rec);
        const auto btn = circle (modeBtnX, modeBtnY, 14.0f);
        shadow (g, btn, juce::Colours::black.withAlpha (0.6f), 6.0f, { 0.0f, 3.0f });
        g.setGradientFill (juce::ColourGradient (hex (0x343742), modeBtnX - 14.0f + 0.44f * 28.0f, modeBtnY - 14.0f + 0.34f * 28.0f,
                                                 hex (0x191b22), modeBtnX - 14.0f + 0.44f * 28.0f + 0.72f * 21.6f, modeBtnY - 14.0f + 0.34f * 28.0f, true));
        g.fillPath (btn);
        g.saveState();
        g.reduceClipRegion (btn);
        juce::Path ring;
        ring.addRectangle (btn.getBounds().expanded (4.0f));
        ring.addPath (btn, juce::AffineTransform::translation (0.0f, 1.0f));
        ring.setUsingNonZeroWinding (false);
        g.setColour (juce::Colours::white.withAlpha (0.09f));
        g.fillPath (ring);
        g.restoreState();
    }

    // meter: bezel, face, scale
    {
        const auto bez = roundRect (bezelR, 6.0f);
        hairlineBelow (g, bez, T.hi);
        g.setColour (T.slot);
        g.fillPath (bez);
        insetShadow (g, bez, juce::Colours::black.withAlpha (0.9f), 8.0f, { 0.0f, 3.0f });
        innerRing (g, bezelR, 6.0f, T.screenEdge);

        const auto face = roundRect (faceR, 3.0f);
        g.saveState();
        g.reduceClipRegion (face);
        {
            const float cx = faceR.getCentreX(), cy = faceR.getBottom(), rx = 0.7f * faceR.getWidth(), ry = 0.85f * faceR.getHeight();
            g.addTransform (juce::AffineTransform::scale (1.0f, ry / rx, cx, cy));
            juce::ColourGradient cg (hex (0x2c1a12), cx, cy, hex (0x110905), cx + rx, cy, true);
            cg.addColour (0.6, hex (0x1a0f09));
            g.setGradientFill (cg);
            g.fillRect (juce::Rectangle<float> (cx - 2 * rx, cy - 2 * rx, 4 * rx, 4 * rx));
        }
        g.restoreState();

        const auto ink = hex (0xe6dcc8);
        g.saveState();
        g.reduceClipRegion (face);
        juce::Path arc, ticks, minor;
        arc.addCentredArc (pivX, pivY, meterR, meterR, 0.0f, -meterSpan, meterSpan, true);
        auto seg = [] (juce::Path& p, float a, float r0, float r1)
        {
            const auto p0 = meterPt (r0, a), p1 = meterPt (r1, a);
            p.startNewSubPath (p0); p.lineTo (p1);
        };
        std::vector<std::pair<float, juce::String>> labels;
        if (! staticOut)
        {
            for (int d = 0; d <= 20; ++d)
            {
                const float a = fracAngle ((float) d / 20.0f);
                if (d % 4 == 0) seg (ticks, a, meterR - 14.0f, meterR); else seg (minor, a, meterR - 7.0f, meterR);
            }
            for (int d : { 0, 4, 8, 12, 16, 20 }) labels.push_back ({ fracAngle ((float) d / 20.0f), juce::String (d) });
        }
        else
        {
            for (float d : { -20.0f, -10.0f, -7.0f, -5.0f, -3.0f, -2.0f, -1.0f, 0.0f, 1.0f, 2.0f, 3.0f }) seg (ticks, fracAngle (vuFrac (d)), meterR - 14.0f, meterR);
            for (float d : { -15.0f, -6.0f, -4.0f, -0.5f, 0.5f, 1.5f, 2.5f }) seg (minor, fracAngle (vuFrac (d)), meterR - 7.0f, meterR);
            for (int d : { -20, -10, -5, -3, 0, 3 })
                labels.push_back ({ fracAngle (vuFrac ((float) d)), d > 0 ? "+" + juce::String (d) : fixMinus (juce::String (d)) });
        }
        g.setColour (ink.withAlpha (0.9f)); g.strokePath (arc, juce::PathStrokeType (1.2f));
        g.setColour (ink);                  g.strokePath (ticks, juce::PathStrokeType (1.5f));
        g.setColour (ink.withAlpha (0.7f)); g.strokePath (minor, juce::PathStrokeType (1.0f));
        const auto fM = fonts.get (600, 17.0f);
        for (auto& [a, t] : labels)
        {
            const auto p = meterPt (meterR + 18.0f, a);
            text (g, fM, t, { p.x - 24.0f, p.y - 12.0f, 48.0f, 24.0f }, juce::Justification::centred, ink);
        }
        const auto fC = fonts.get (500, 13.0f);
        text (g, fC, staticOut ? "Output" : "Gain reduction", { faceR.getX() + 28.0f, faceR.getY() + 198.0f, 200.0f, 13.0f * lineH },
              juce::Justification::centredLeft, hex (0xa58f6b));
        text (g, fC, "dB", { faceR.getRight() - 28.0f - 60.0f, faceR.getY() + 198.0f, 60.0f, 13.0f * lineH }, juce::Justification::centredRight, hex (0xa58f6b));
        g.restoreState();
    }

    // display: screen, grid, labels, legend, expand icon
    {
        const auto box = roundRect (dispR, 6.0f);
        hairlineBelow (g, box, T.hi);
        g.setColour (T.screen);
        g.fillPath (box);
        insetShadow (g, box, juce::Colours::black.withAlpha (0.9f), 10.0f, { 0.0f, 3.0f });
        innerRing (g, dispR, 6.0f, T.screenEdge);

        const float ym = 0.5f * (plotY0 + plotY1);
        auto ey = [&] (float d) { return ym - d / toneRange * 0.5f * (plotY1 - plotY0); };
        juce::Path grid, major;
        for (int f : { 50, 200, 500, 2000, 5000 }) { const float x = xOfF (f, plotX0, plotX1); grid.startNewSubPath (x, plotY0); grid.lineTo (x, plotY1); }
        for (int f : { 100, 1000, 10000 }) { const float x = xOfF (f, plotX0, plotX1); major.startNewSubPath (x, plotY0); major.lineTo (x, plotY1); }
        for (float d : { 6.0f, -6.0f }) { grid.startNewSubPath (plotX0, ey (d)); grid.lineTo (plotX1, ey (d)); }
        for (float d : { 12.0f, 0.0f, -12.0f }) { major.startNewSubPath (plotX0, ey (d)); major.lineTo (plotX1, ey (d)); }
        major.startNewSubPath (plotX0, plotY0); major.lineTo (plotX0, plotY1);
        major.startNewSubPath (plotX1, plotY0); major.lineTo (plotX1, plotY1);
        g.setColour (T.grid);    g.strokePath (grid, juce::PathStrokeType (1.0f));
        g.setColour (T.gridMaj); g.strokePath (major, juce::PathStrokeType (1.0f));

        const auto fL = fonts.get (500, 12.0f);
        const auto lab = hex (0x9a8e7c);
        text (g, fL, "+12", { plotX0 - 7.0f - 44.0f, ey (12.0f) - 9.0f, 44.0f, 18.0f }, juce::Justification::centredRight, lab);
        text (g, fL, "0", { plotX0 - 7.0f - 44.0f, ey (0.0f) - 9.0f, 44.0f, 18.0f }, juce::Justification::centredRight, lab);
        text (g, fL, minus + "12", { plotX0 - 7.0f - 44.0f, ey (-12.0f) - 9.0f, 44.0f, 18.0f }, juce::Justification::centredRight, lab);
        const std::pair<int, const char*> fl[] = { { 100, "100" }, { 1000, "1k" }, { 10000, "10k" } };
        for (auto& [f, t] : fl)
            text (g, fL, t, { xOfF (f, plotX0, plotX1) - 22.0f, plotY1 + 5.0f, 44.0f, 18.0f }, juce::Justification::centred, lab);

        // legend, right-aligned before the expand button
        const auto fG = fonts.get (500, 12.0f);
        const auto legCol = hex (0x8f8574);
        const juce::String names[3] = { "Output", "Harmonics", "Tone" };
        float x = expandR.getX() - 16.0f;
        const float cy = expandR.getCentreY();
        for (int i = 2; i >= 0; --i)
        {
            const float tw = juce::GlyphArrangement::getStringWidth (fG, names[i]);
            x -= tw;
            text (g, fG, names[i], { x, cy - 0.5f * 12.0f * lineH + 1.0f, tw + 2.0f, 12.0f * lineH }, juce::Justification::centredLeft, legCol);
            x -= 6.0f + 12.0f;
            if (i == 0) { g.setColour (hex (0xa89272).withAlpha (0.7f)); g.fillRoundedRectangle (x, cy - 4.0f, 12.0f, 8.0f, 1.0f); }
            else if (i == 1) { g.setColour (hex (0xf07a3a)); g.fillRoundedRectangle (x, cy - 4.0f, 12.0f, 8.0f, 1.0f); }
            else { g.setColour (hex (0xf4eada)); g.fillRect (x, cy - 1.0f, 12.0f, 2.0f); }
            x -= 14.0f;
        }
        drawExpandIcon (g, expandR.getX() + 6.0f, expandR.getY() + 5.0f, hex (0xb89a6c));
    }

    // key banks and captions
    {
        const auto fC = fonts.get (500, 13.0f);
        text (g, fC, "Oversampling", { bankOs.getX(), 560.0f, bankOs.getWidth(), 13.0f * lineH }, juce::Justification::centredLeft, T.label);
        text (g, fC, "Phase", { bankPh.getX(), 560.0f, bankPh.getWidth(), 13.0f * lineH }, juce::Justification::centredLeft, T.label);
        for (auto r : { bankOs, bankPh })
        {
            const auto p = roundRect (r, 5.0f);
            hairlineBelow (g, p, T.hi);
            g.setColour (T.recess);
            g.fillPath (p);
            insetShadow (g, p, rgba (60, 20, 0, 0.55f), 4.0f, { 0.0f, 2.0f });
        }
        g.setColour (rgba (246, 210, 131, 0.2f));
        g.fillRect (884.0f, 444.0f, 74.0f, 1.0f);
    }
}

// ============================================================================================ dynamic parts
void AnbuEditor::drawLogo (juce::Graphics& g)
{
    debossText (g, fonts.get (800, 44.0f), logoWord, logoWordR, juce::Justification::centred);
    debossText (g, fonts.get (600, 13.0f), "by R4N", logoByR, juce::Justification::centred);
}

void AnbuEditor::drawKnobDynamic (juce::Graphics& g, const Knob& kn, float pos, float flash)
{
    const auto& T = theme();
    const float cx = kn.cx, cy = kn.cy, D = kn.D;
    // lit ring: dark at the knob's zero; bipolar knobs light from the centre (0 dB) outwards
    const float a0 = kn.bipolar ? 0.0f : -135.0f, a1 = -135.0f + 270.0f * pos;
    if (std::abs (a1 - a0) > 0.5f)
    {
        juce::Path arc;
        arc.addCentredArc (cx, cy, kn.rr, kn.rr, 0.0f, juce::degreesToRadians (juce::jmin (a0, a1)), juce::degreesToRadians (juce::jmax (a0, a1)), true);
        strokeGlow (g, arc, T.lamp, 2.4f, 1.0f);
    }
    if (flash > 0.0f)
    {
        juce::Path arc;
        arc.addCentredArc (cx, cy, kn.rr, kn.rr, 0.0f, juce::degreesToRadians (-135.0f), juce::degreesToRadians (135.0f), true);
        strokeGlow (g, arc, T.lamp, 2.4f, flash);
    }
    // pointer
    juce::Path ptr;
    ptr.addRoundedRectangle (-1.5f, -0.34f * D, 3.0f, 0.22f * D, 1.5f);
    g.setColour (hex (0xe9d9b8));
    g.fillPath (ptr, juce::AffineTransform::rotation (juce::degreesToRadians (-135.0f + 270.0f * pos)).translated (cx, cy));
}

void AnbuEditor::drawFaderCap (juce::Graphics& g, float pos)
{
    const float yc = faderY1 - (faderY1 - faderY0) * pos;
    const juce::Rectangle<float> r (faderX - 22.0f, yc - 16.0f, 44.0f, 32.0f);
    const auto p = roundRect (r, 4.0f);
    shadow (g, p, juce::Colours::black.withAlpha (0.6f), 12.0f, { 0.0f, 7.0f });
    juce::ColourGradient cg (hex (0x2e313b), 0.0f, r.getY(), hex (0x1b1d24), 0.0f, r.getBottom(), false);
    cg.addColour (0.46, hex (0x1d1f27));
    cg.addColour (0.54, hex (0x121319));
    g.setGradientFill (cg);
    g.fillPath (p);
    g.saveState();
    g.reduceClipRegion (p);
    g.setColour (juce::Colours::white.withAlpha (0.1f));
    g.fillRect (r.getX(), r.getY(), r.getWidth(), 1.0f);
    g.restoreState();
    g.setColour (hex (0xe9d9b8));
    g.fillRoundedRectangle (faderX - 15.0f, yc - 1.0f, 30.0f, 2.0f, 1.0f);
}

void AnbuEditor::drawModeLamps (juce::Graphics& g, float flash)
{
    const auto& T = theme();
    float onAlpha = 1.0f;
    if (introRunning) onAlpha = lerp (0.35f, 1.0f, easeOut (((float) introT() - 1.762f) / 0.6f));
    for (int i = 0; i < 2; ++i)
    {
        const float y = i == 0 ? lampYC : lampYL;
        const bool on = (i == 1) == modeLimit;
        const auto dot = circle (lampX, y, 4.5f);
        if (on)
        {
            g.setColour (T.lampOff);
            g.fillPath (dot);
            shadow (g, dot, T.glow.withMultipliedAlpha (onAlpha), 10.0f, {});
            shadow (g, dot, T.lamp.withMultipliedAlpha (onAlpha), 4.0f, {});
            g.setColour (T.lamp.withAlpha (onAlpha));
            g.fillPath (dot);
        }
        else
        {
            g.setColour (T.lampOff);
            g.fillPath (dot);
            insetShadow (g, dot, rgba (60, 20, 0, 0.6f), 2.0f, { 0.0f, 1.0f });
        }
        if (flash > 0.0f)
        {
            shadow (g, dot, T.lamp.withAlpha (flash), 6.0f, {});
            g.setColour (T.lamp.withAlpha (flash));
            g.fillPath (dot);
        }
        text (g, fonts.get (on ? 600 : 500, 15.0f), i == 0 ? "Compress" : "Limit", { 71.0f, y - 0.5f * 15.0f * lineH + 1.0f, 110.0f, 15.0f * lineH },
              juce::Justification::centredLeft, on ? T.value : T.dim);
    }
}

float AnbuEditor::meterFrac() const
{
    return proc.uiMeterOut ? juce::jlimit (-0.04f, 1.06f, vuFrac (vuDb)) : juce::jlimit (0.0f, 1.04f, grDb / 20.0f);
}

void AnbuEditor::drawMeterDynamic (juce::Graphics& g)
{
    float f = needlePos;
    if (introRunning)
    {
        const float u = ((float) introT() - 0.95f) / 1.1f;
        if (u <= 0.0f) f = 0.0f;
        else if (u < 0.45f) f = bezier (0.3f, 0.6f, 0.3f, 1.0f, u / 0.45f);
        else if (u < 1.0f) f = lerp (1.0f, needlePos, bezier (0.3f, 0.6f, 0.3f, 1.0f, (u - 0.45f) / 0.55f));
    }
    const float a = fracAngle (juce::jlimit (-0.06f, 1.08f, f));
    const auto face = roundRect (faceR, 3.0f);
    g.saveState();
    g.reduceClipRegion (face);
    const auto rot = juce::AffineTransform::rotation (a, pivX, pivY);
    juce::Path sh, nd;
    sh.startNewSubPath (pivX + 4.0f, pivY + 5.0f); sh.lineTo (pivX + 4.0f, faceR.getY() + 63.0f);
    nd.startNewSubPath (pivX, pivY); nd.lineTo (pivX, faceR.getY() + 66.0f);
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.strokePath (sh, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded), rot);
    g.setColour (hex (0xf3e8d2));
    g.strokePath (nd, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded), rot);
    g.setColour (hex (0x0a0604));
    g.fillEllipse (pivX - 74.0f, pivY - 74.0f, 148.0f, 148.0f);
    g.setColour (rgba (230, 220, 200, 0.08f));
    g.drawEllipse (pivX - 74.0f, pivY - 74.0f, 148.0f, 148.0f, 1.0f);
    // glass
    {
        const float rad = juce::degreesToRadians (172.0f);
        const auto c = faceR.getCentre();
        const float half = 0.5f * (std::abs (faceR.getWidth() * std::sin (rad)) + std::abs (faceR.getHeight() * std::cos (rad)));
        const juce::Point<float> dir (std::sin (rad), -std::cos (rad));
        const auto p0 = c - dir * half, p1 = c + dir * half;
        juce::ColourGradient lg (juce::Colours::white.withAlpha (0.07f), p0.x, p0.y, juce::Colours::white.withAlpha (0.0f), p1.x, p1.y, false);
        lg.clearColours();
        lg.addColour (0.0, juce::Colours::white.withAlpha (0.07f));
        lg.addColour (0.3, juce::Colours::white.withAlpha (0.0f));
        lg.addColour (1.0, juce::Colours::white.withAlpha (0.0f));
        g.setGradientFill (lg);
        g.fillRect (faceR);
        g.saveState();
        const float rx = 0.6f * faceR.getWidth(), ry = 0.4f * faceR.getHeight();
        g.addTransform (juce::AffineTransform::scale (1.0f, ry / rx, c.x, faceR.getY()));
        g.setGradientFill (juce::ColourGradient (rgba (255, 190, 110, 0.06f), c.x, faceR.getY(), rgba (255, 190, 110, 0.0f), c.x + rx * 0.7f, faceR.getY(), true));
        g.fillRect (juce::Rectangle<float> (c.x - rx, faceR.getY() - rx, 2 * rx, 2 * rx));
        g.restoreState();
    }
    // GR / Out pills
    const auto fP = fonts.get (600, 13.0f);
    for (int i = 0; i < 2; ++i)
    {
        const auto r = i == 0 ? pillGR : pillOut;
        const bool on = (i == 1) == proc.uiMeterOut;
        if (on) { g.setColour (rgba (201, 168, 119, 0.16f)); g.fillRoundedRectangle (r, 3.0f); }
        text (g, fP, i == 0 ? "GR" : "Out", r.translated (0.0f, 1.0f), juce::Justification::centred, on ? hex (0xf1e3c6) : hex (0x7c7464));
    }
    g.restoreState();
}

juce::String AnbuEditor::readoutText() const
{
    if (readoutKnob < 0) return {};
    const auto* prm = readoutKnob == 5 ? colorParam : knobs[(size_t) readoutKnob].param;
    return juce::String (knobNames[readoutKnob]) + "  " + fixMinus (prm != nullptr ? prm->getCurrentValueAsText() : juce::String());
}

void AnbuEditor::drawDisplayDynamic (juce::Graphics& g)
{
    if (readoutKnob >= 0 && readoutAlpha > 0.0f)
        text (g, fonts.get (600, 14.0f), readoutText(), { 294.0f, 324.0f, 300.0f, 24.0f }, juce::Justification::centredLeft, hex (0xf1e3c6).withAlpha (readoutAlpha));

    g.saveState();
    g.reduceClipRegion (juce::Rectangle<float> (plotX0, plotY0, plotX1 - plotX0, plotY1 - plotY0).toNearestInt());
    if (spec.active)
    {
        g.setColour (hex (0xa89272).withAlpha (0.3f));
        g.fillPath (specPath (spec, spec.out, 0.0f, plotX0, plotX1, plotY0, plotY1, specTop, specBot, true));
        g.setColour (hex (0xc8b08a));
        g.strokePath (specPath (spec, spec.out, 0.0f, plotX0, plotX1, plotY0, plotY1, specTop, specBot, false), juce::PathStrokeType (1.3f, juce::PathStrokeType::curved));
        g.setColour (hex (0xf07a3a).withAlpha (0.55f));
        g.fillPath (specPath (spec, spec.harm, harmBoostDb, plotX0, plotX1, plotY0, plotY1, specTop, specBot, true));
        g.setColour (hex (0xf58a4e));
        g.strokePath (specPath (spec, spec.harm, harmBoostDb, plotX0, plotX1, plotY0, plotY1, specTop, specBot, false), juce::PathStrokeType (1.2f, juce::PathStrokeType::curved));
    }
    const auto tp = tonePath (spec, plotX0, plotX1, plotY0, plotY1, toneRange);
    g.setColour (hex (0xf4eada).withAlpha (0.16f));
    g.strokePath (tp, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (hex (0xf4eada));
    g.strokePath (tp, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.restoreState();
}

void AnbuEditor::drawKey (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& t, bool active, float flash, float size)
{
    const auto& T = theme();
    const auto p = roundRect (r, 3.0f);
    if (active)
    {
        g.setGradientFill (juce::ColourGradient::vertical (T.keyDn0, r.getY(), T.keyDn1, r.getBottom()));
        g.fillPath (p);
        insetShadow (g, p, rgba (60, 20, 0, 0.55f), 3.0f, { 0.0f, 2.0f });
    }
    else
    {
        shadow (g, p, rgba (60, 20, 0, 0.45f), 3.0f, { 0.0f, 2.0f });
        g.setGradientFill (juce::ColourGradient::vertical (T.keyUp0, r.getY(), T.keyUp1, r.getBottom()));
        g.fillPath (p);
        g.saveState();
        g.reduceClipRegion (p);
        g.setColour (T.hi);
        g.fillRect (r.getX(), r.getY(), r.getWidth(), 1.0f);
        g.restoreState();
    }
    const auto f = fonts.get (active ? 700 : 600, size);
    const auto tr = r.translated (0.0f, 1.0f);
    if (active)
        shadow (g, textPath (f, t, tr, juce::Justification::centred), T.glow, 8.0f, {});
    text (g, f, t, tr, juce::Justification::centred, active ? T.keyOn : T.dim);
    if (flash > 0.0f)
    {
        g.saveState();
        g.reduceClipRegion (p);
        juce::Path ring;
        ring.addRectangle (r.expanded (20.0f));
        ring.addPath (p);
        ring.setUsingNonZeroWinding (false);
        shadow (g, ring, T.glow.withMultipliedAlpha (flash), 12.0f, {});
        g.restoreState();
        g.setColour (T.lamp.withAlpha (flash));
        g.drawRoundedRectangle (r.reduced (0.5f), 2.5f, 1.0f);
    }
}

void AnbuEditor::drawKeys (juce::Graphics& g)
{
    for (int i = 0; i < 7; ++i)
    {
        const auto r = keyRect (i);
        if (! g.clipRegionIntersects (r.expanded (6.0f).getSmallestIntegerContainer())) continue;
        drawKey (g, r, juce::String (juce::CharPointer_UTF8 (keyText[i])), keyState[(size_t) i], flashAt (keyFlashX (i)), 14.0f);
    }
}

void AnbuEditor::drawIntroOverlay (juce::Graphics& g)
{
    const auto& T = theme();
    const float t = (float) introT();

    // panel hidden behind the chassis colour until the logo leaves the centre
    float ov = 1.0f;
    if (t >= 0.9f) ov = 1.0f - easeInOut ((t - 0.9f) / 0.45f);
    if (ov > 0.0f)
    {
        g.setColour (T.chassis.withAlpha (ov));
        g.fillRect (0.0f, 0.0f, (float) baseW, (float) baseH);
    }

    // அ ignites like a lamp, the rest of the word grows out of it, then it shrinks to its place and debosses
    const auto font = fonts.get (800, 44.0f);
    const float wA = juce::GlyphArrangement::getStringWidth (font, logoA);
    const float wRest = juce::GlyphArrangement::getStringWidth (font, logoRest);
    float ignite = 1.0f;
    if (t < 0.6f)
    {
        static const float kt[] = { 0.0f, 0.14f, 0.2f, 0.32f, 0.4f, 0.7f, 1.0f };
        static const float kv[] = { 0.0f, 0.14f, 0.05f, 0.28f, 0.16f, 0.75f, 1.0f };
        const float u = t / 0.6f;
        for (int i = 0; i < 6; ++i)
            if (u <= kt[i + 1]) { ignite = lerp (kv[i], kv[i + 1], (u - kt[i]) / (kt[i + 1] - kt[i])); break; }
    }
    const float grow = easeOut ((t - 0.6f) / 0.38f);
    const float reveal = juce::jmin (wRest, 132.0f * grow);
    const float move = bezier (0.65f, 0.0f, 0.25f, 1.0f, (t - 0.95f) / 0.65f);
    const float deb = easeIn ((t - 1.0f) / 0.65f);
    const float cx = logoWordR.getCentreX() + 366.0f * (1.0f - move);
    const float cy = logoWordR.getCentreY() + 274.0f * (1.0f - move);
    const float sc = 2.2f - 1.2f * move;

    const float total = wA + reveal;
    const float left = cx - 0.5f * total;
    const float baseline = cy - 0.5f * font.getHeight() + font.getAscent();
    juce::Path pA, pRest;
    {
        juce::GlyphArrangement ga;
        ga.addLineOfText (font, logoA, left, baseline);
        ga.createPath (pA);
    }
    {
        juce::GlyphArrangement ga;
        ga.addLineOfText (font, logoRest, left + wA, baseline);
        ga.createPath (pRest);
    }
    g.saveState();
    g.addTransform (juce::AffineTransform::scale (sc, sc, cx, cy));
    const auto lit = T.logoLit.interpolatedWith (T.deboss, deb);
    auto drawPart = [&] (const juce::Path& p, float alpha)
    {
        if (alpha <= 0.0f) return;
        if (deb < 0.999f) shadow (g, p, T.glow.withMultipliedAlpha (alpha * (1.0f - deb)), 16.0f * (1.0f - deb) + 0.5f, {});
        if (deb > 0.0f)
        {
            g.setColour (T.debHi.withMultipliedAlpha (alpha * deb)); g.fillPath (p, juce::AffineTransform::translation (0.0f, 1.0f));
            g.setColour (T.debLo.withMultipliedAlpha (alpha * deb)); g.fillPath (p, juce::AffineTransform::translation (0.0f, -1.0f));
        }
        g.setColour (lit.withMultipliedAlpha (alpha));
        g.fillPath (p);
    };
    drawPart (pA, ignite);
    if (reveal > 0.0f)
    {
        g.saveState();
        g.reduceClipRegion (juce::Rectangle<float> (left + wA, cy - 60.0f, reveal, 120.0f).getSmallestIntegerContainer());
        drawPart (pRest, grow);
        g.restoreState();
    }
    g.restoreState();

    // "by R4N"
    const float by = easeOut ((t - 0.95f) / 0.4f);
    if (by > 0.0f)
    {
        const auto fb = fonts.get (600, 13.0f);
        const auto r = logoByR.translated (0.0f, 4.0f * (1.0f - by));
        if (deb > 0.0f)
        {
            text (g, fb, "by R4N", r.translated (0.0f, 1.0f), juce::Justification::centred, T.debHi.withMultipliedAlpha (by * deb));
            text (g, fb, "by R4N", r.translated (0.0f, -1.0f), juce::Justification::centred, T.debLo.withMultipliedAlpha (by * deb));
        }
        text (g, fb, "by R4N", r, juce::Justification::centred, lit.withMultipliedAlpha (by));
    }
}

// ============================================================================================ paint
void AnbuEditor::paint (juce::Graphics& g)
{
    const float s = scale();
    const float phys = s * g.getInternalContext().getPhysicalPixelScaleFactor();
    if (! staticLayer.isValid() || std::abs (phys - staticScale) > 1.0e-3f || staticOut != proc.uiMeterOut)
    {
        staticScale = phys;
        staticOut = proc.uiMeterOut;
        staticLayer = juce::Image (juce::Image::ARGB, juce::roundToInt ((float) baseW * phys), juce::roundToInt ((float) baseH * phys), true);
        juce::Graphics gs (staticLayer);
        gs.addTransform (juce::AffineTransform::scale (phys));
        renderStatic (gs);
    }
    g.addTransform (juce::AffineTransform::scale (s));
    g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);
    g.drawImageTransformed (staticLayer, juce::AffineTransform::scale ((float) baseW / (float) staticLayer.getWidth(),
                                                                       (float) baseH / (float) staticLayer.getHeight()));

    if (! introRunning && g.clipRegionIntersects (logoWordR.getUnion (logoByR).expanded (4.0f).getSmallestIntegerContainer()))
        drawLogo (g);

    for (const auto& kn : knobs)
        if (g.clipRegionIntersects (knobArea (kn).getSmallestIntegerContainer()))
            drawKnobDynamic (g, kn, pos01 (kn.slider) * valueEase (kn.cx), flashAt (kn.cx));

    if (g.clipRegionIntersects (faderHit.expanded (10.0f, 14.0f).getSmallestIntegerContainer()))
    {
        const float e = introRunning ? bezier (0.2f, 0.75f, 0.25f, 1.0f, ((float) introT() - (1.75f + 0.0002f * faderX + 0.08f)) / 0.8f) : 1.0f;
        drawFaderCap (g, pos01 (fader) * e);
    }
    if (g.clipRegionIntersects (juce::Rectangle<int> (40, 590, 150, 70)))
        drawModeLamps (g, flashAt (56.0f));
    if (g.clipRegionIntersects (faceR.getSmallestIntegerContainer()))
        drawMeterDynamic (g);
    if (g.clipRegionIntersects (dispR.getSmallestIntegerContainer()))
        drawDisplayDynamic (g);
    drawKeys (g);

    if (introRunning)
        drawIntroOverlay (g);
}

// ============================================================================================ interaction
bool AnbuEditor::keyOn (int i) const
{
    auto v = [this] (const char* id) { return (int) std::lround (proc.apvts.getRawParameterValue (id)->load()); };
    if (i < 4) return v (ids::os) == i;
    if (i < 6) return v (ids::phase) == i - 4;
    return v (ids::noiseOn) != 0;
}

int AnbuEditor::keyAt (juce::Point<float> p) const
{
    for (int i = 0; i < 7; ++i)
        if (keyRect (i).contains (p)) return i;
    return -1;
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

void AnbuEditor::clickKey (int i)
{
    if (i < 4) setChoice (ids::os, i);
    else if (i < 6) setChoice (ids::phase, i - 4);
    else setChoice (ids::noiseOn, keyOn (6) ? 0 : 1);
}

void AnbuEditor::toggleMeterMode (bool out)
{
    if (proc.uiMeterOut == out) return;
    proc.uiMeterOut = out;
    needlePos = meterFrac();
    needleVel = 0.0f;
    repaintDesign (bezelR);
}

void AnbuEditor::openAnalyser()
{
    if (analyserWindow == nullptr)
    {
        analyserWindow = std::make_unique<AnalyserWindow> (*this);
        const auto me = getScreenBounds();
        auto area = juce::Rectangle<int> (me.getX() + me.getWidth() / 2 - AnalyserView::W / 2, me.getY() + 60, AnalyserView::W, AnalyserView::H);
        if (auto* d = juce::Desktop::getInstance().getDisplays().getDisplayForRect (me))
            area = area.constrainedWithin (d->userArea);
        analyserWindow->setBounds (area);
    }
    spec.in.fill (-140.0f);
    spec.peak = spec.out;
    analyserWindow->setVisible (true);
    analyserWindow->toFront (true);
}

std::unique_ptr<juce::Component> AnbuEditor::makeAnalyserView()
{
    return std::make_unique<AnalyserView> (spec, fonts, [this] { return readoutText(); }, [this] { return readoutAlpha; }, [] {});
}

void AnbuEditor::setReadout (int knob)
{
    readoutKnob = knob;
    readoutTime = juce::Time::getMillisecondCounterHiRes();
    readoutAlpha = 1.0f;
    repaintDesign ({ 290.0f, 322.0f, 320.0f, 28.0f });
    if (analyserWindow != nullptr && analyserWindow->isVisible()) analyserWindow->view->repaint();
}

void AnbuEditor::mouseDown (const juce::MouseEvent& e)
{
    if (introRunning) { skipIntro(); return; }
    const auto p = toDesign (e.position);
    if (expandR.contains (p)) { openAnalyser(); return; }
    if (pillGR.contains (p)) { toggleMeterMode (false); return; }
    if (pillOut.contains (p)) { toggleMeterMode (true); return; }
    if (p.getDistanceFrom ({ modeBtnX, modeBtnY }) <= 19.0f) { setChoice (ids::mode, modeLimit ? 0 : 1); return; }
    if (juce::Rectangle<float> (50.0f, lampYC - 11.0f, 120.0f, 22.0f).contains (p)) { setChoice (ids::mode, 0); return; }
    if (juce::Rectangle<float> (50.0f, lampYL - 11.0f, 120.0f, 22.0f).contains (p)) { setChoice (ids::mode, 1); return; }
    const int k = keyAt (p);
    if (k >= 0) clickKey (k);
}

bool AnbuEditor::clickableAt (juce::Point<float> p) const
{
    return expandR.contains (p) || pillGR.contains (p) || pillOut.contains (p) || p.getDistanceFrom ({ modeBtnX, modeBtnY }) <= 19.0f
        || juce::Rectangle<float> (50.0f, lampYC - 11.0f, 120.0f, 22.0f).contains (p)
        || juce::Rectangle<float> (50.0f, lampYL - 11.0f, 120.0f, 22.0f).contains (p) || keyAt (p) >= 0;
}

void AnbuEditor::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (clickableAt (toDesign (e.position)) ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void AnbuEditor::freezeMeters (float vu, float gr)
{
    frozen = true;
    vuDb = vu;
    grDb = gr;
    needlePos = meterFrac();
    needleVel = 0.0f;
    repaint();
}

// ============================================================================================ analysis
void AnbuEditor::updateSpectrum()
{
    const bool wantIn = analyserWindow != nullptr && analyserWindow->isVisible();
    const int got = proc.pullAnalyser (pullOut.data(), pullIn.data(), pullHarm.data(), (int) pullOut.size());
    for (int i = 0; i < got; ++i)
    {
        ringOut[(size_t) ringPos] = pullOut[(size_t) i];
        ringIn[(size_t) ringPos] = pullIn[(size_t) i];
        ringHarm[(size_t) ringPos] = pullHarm[(size_t) i];
        ringPos = (ringPos + 1) & (fftSize - 1);
    }
    constexpr float floorDb = -110.0f;
    if (got > 0)
    {
        const double fs = juce::jmax (8000.0, proc.getAnalyserRate());
        const double norm = 2.0 / (0.5 * fftSize);           // full-scale sine -> 0 dB (Hann coherent gain 0.5)
        const double binHz = fs / fftSize;
        auto analyse = [&] (const std::vector<float>& src, std::array<float, Spectrum::nPts>& dest)
        {
            bool any = false;
            for (int i = 0; i < fftSize; ++i)
                fftData[(size_t) i] = src[(size_t) ((ringPos + i) & (fftSize - 1))] * window[(size_t) i];
            std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);
            fft.performFrequencyOnlyForwardTransform (fftData.data(), true);
            auto powerAt = [&] (double bin)
            {
                const int b0 = juce::jlimit (0, fftSize / 2 - 1, (int) bin);
                const double f = juce::jlimit (0.0, 1.0, bin - b0);
                const double m0 = fftData[(size_t) b0] * norm, m1 = fftData[(size_t) (b0 + 1)] * norm;
                return (1 - f) * m0 * m0 + f * m1 * m1;
            };
            for (int p = 0; p < Spectrum::nPts; ++p)
            {
                const double f = spec.freqs[(size_t) p];
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
                float& s = dest[(size_t) p];
                s += (db > s ? 0.6f : 0.12f) * (db - s);
                any = any || s > floorDb;
            }
            return any;
        };
        spec.active = analyse (ringOut, spec.out);
        analyse (ringHarm, spec.harm);
        if (wantIn) spec.inputActive = analyse (ringIn, spec.in);
        for (int i = 0; i < Spectrum::nPts; ++i)
            spec.peak[(size_t) i] = juce::jmax (spec.peak[(size_t) i] - 0.25f, spec.out[(size_t) i]);
        specDirty = true;
    }
    else if (spec.active)
    {
        bool any = false;
        for (auto* arr : { &spec.out, &spec.harm, &spec.in })
            for (auto& s : *arr) { s -= 1.5f; any = any || s > floorDb; }
        for (auto& s : spec.peak) s -= 0.5f;
        spec.active = any;
        spec.inputActive = spec.inputActive && any;
        specDirty = true;
    }
}

void AnbuEditor::updateTone()
{
    const float pr = proc.apvts.getRawParameterValue (ids::pr)->load();
    const float color = proc.apvts.getRawParameterValue (ids::color)->load() / 100.0f;
    const float gs = proc.meterGs.load();
    if (std::abs (pr - toneKeyPr) < 0.05f && std::abs (color - toneKeyColor) < 0.0005f && std::abs (gs - toneKeyGs) < 0.002f * (1.0f + gs)) return;
    toneKeyPr = pr; toneKeyColor = color; toneKeyGs = gs;
    std::array<double, Spectrum::nPts> out {};
    anbu::displayCurve (pr, color, gs, proc.meterCapW.load(), spec.freqs.data(), out.data(), Spectrum::nPts);
    for (int i = 0; i < Spectrum::nPts; ++i) spec.tone[(size_t) i] = (float) out[(size_t) i];
    specDirty = true;
}

// ============================================================================================ timer
void AnbuEditor::timerCallback()
{
    // intro clock starts once the panel is actually on screen
    if (introRunning)
    {
        if (introStart < 0.0 && isShowing()) introStart = juce::Time::getMillisecondCounterHiRes();
        if (introFixed < 0.0 && introStart >= 0.0 && introT() > 2.85) introRunning = false;
        repaint();
    }

    // keys and mode (host automation or clicks)
    for (int i = 0; i < 7; ++i)
    {
        const bool on = keyOn (i);
        if (on != keyState[(size_t) i]) { keyState[(size_t) i] = on; repaintDesign (keyRect (i).expanded (8.0f)); }
    }
    const bool lim = (int) std::lround (proc.apvts.getRawParameterValue (ids::mode)->load()) == 1;
    if (lim != modeLimit) { modeLimit = lim; repaintDesign ({ 40.0f, 590.0f, 150.0f, 70.0f }); }

    // needle: VU-like ballistics, ~300 ms rise with a slight overshoot
    if (! frozen)
    {
        vuDb = juce::jlimit (-40.0f, 10.0f, proc.meterVuDb.load());
        grDb = proc.meterGrDb.load();
        const float target = meterFrac();
        const float w = juce::MathConstants<float>::twoPi * 2.1f, z = 0.78f, dt = 1.0f / 240.0f;
        for (int k = 0; k < 4; ++k)
        {
            const float acc = w * w * (target - needlePos) - 2.0f * z * w * needleVel;
            needleVel += acc * dt;
            needlePos += needleVel * dt;
        }
    }
    if (std::abs (needlePos - drawnNeedle) > 0.0004f)
    {
        drawnNeedle = needlePos;
        repaintDesign (faceR);
    }

    updateSpectrum();
    updateTone();

    if (readoutKnob >= 0)
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        const bool held = readoutKnob == 5 ? fader.isMouseButtonDown() : knobs[(size_t) readoutKnob].slider.isMouseButtonDown();
        if (held) readoutTime = now;
        const double age = now - readoutTime;
        const float a = age < 1200.0 ? 1.0f : juce::jmax (0.0f, 1.0f - (float) (age - 1200.0) / 400.0f);
        if (std::abs (a - readoutAlpha) > 1.0e-4f)
        {
            readoutAlpha = a;
            repaintDesign ({ 290.0f, 322.0f, 320.0f, 28.0f });
            specDirty = true;
        }
        if (a <= 0.0f) readoutKnob = -1;
    }

    if (specDirty)
    {
        specDirty = false;
        repaintDesign ({ plotX0 - 4.0f, plotY0 - 4.0f, plotX1 - plotX0 + 8.0f, plotY1 - plotY0 + 8.0f });
        if (analyserWindow != nullptr && analyserWindow->isVisible()) analyserWindow->view->repaint();
    }
}
