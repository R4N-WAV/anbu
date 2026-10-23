#include "PluginEditor.h"
#include <cstring>
#include "HwAssets.h"
#include "BinaryData.h"

using namespace anbu::ui;
namespace HW = anbu::hw;

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
        o.label = hex (0xf6d283);     o.dim = hex (0xe3a596);       o.value = hex (0xfff1d8);
        o.deboss = hex (0x6a1819);    o.debHi = rgba (255, 160, 150, 0.3f); o.debLo = rgba (30, 0, 0, 0.55f);
        o.logoLit = hex (0xffe9b0);   o.zari = rgba (240, 198, 104, 0.6f);
        o.lamp = hex (0xffb341);      o.glow = rgba (255, 179, 65, 0.55f);
        o.keyOn = hex (0xffe08a);
        o.screen = hex (0x130506);    o.grid = hex (0x2a1212);      o.gridMaj = hex (0x3e1b1b);
        return o;
    }();
    return t;
}

namespace
{
juce::Typeface::Ptr loadTypeface (const char* name)
{
    int size = 0;
    if (auto* data = BinaryData::getNamedResource (name, size))
        return juce::Typeface::createSystemTypefaceFor (data, (size_t) size);
    return {};
}
juce::Image loadImage (const char* name)
{
    int size = 0;
    if (auto* data = BinaryData::getNamedResource (name, size))
        return juce::ImageFileFormat::loadFrom (data, (size_t) size).convertedToFormat (juce::Image::ARGB);
    return {};
}
}

anbu::ui::Fonts::Fonts()
{
    c600 = loadTypeface ("catamaran600_ttf");
    c800 = loadTypeface ("catamaran800_ttf");
    b500 = loadTypeface ("barlow500_ttf");
    b600 = loadTypeface ("barlow600_ttf");
    b700 = loadTypeface ("barlow700_ttf");
}

juce::Font anbu::ui::Fonts::logo (int weight, float size) const
{
    auto tf = weight >= 800 ? c800 : c600;
    if (tf == nullptr) return juce::Font (juce::FontOptions().withPointHeight (size));
    return juce::Font (juce::FontOptions (tf).withPointHeight (size));
}

juce::Font anbu::ui::Fonts::ui (int weight, float size) const
{
    auto tf = weight >= 700 ? b700 : weight >= 600 ? b600 : b500;
    if (tf == nullptr) return juce::Font (juce::FontOptions().withPointHeight (size));
    return juce::Font (juce::FontOptions (tf).withPointHeight (size));
}

// ---------------------------------------------------------------- resampled art
namespace
{
// area-average resampling (for downscaling the 2x renders without aliasing); bilinear-quality upscale otherwise
juce::Image resample (const juce::Image& src, int w, int h)
{
    w = juce::jmax (1, w); h = juce::jmax (1, h);
    if (w >= src.getWidth() && h >= src.getHeight())
        return src.rescaled (w, h, juce::Graphics::highResamplingQuality);
    const int sw = src.getWidth(), sh = src.getHeight();
    const juce::Image::BitmapData in (src, juce::Image::BitmapData::readOnly);
    // horizontal pass -> float buffer (premultiplied ARGB)
    std::vector<float> tmp ((size_t) (w * sh * 4), 0.0f);
    const double fx = (double) sw / w, fy = (double) sh / h;
    for (int y = 0; y < sh; ++y)
    {
        const auto* row = in.getLinePointer (y);
        for (int x = 0; x < w; ++x)
        {
            const double a0 = x * fx, a1 = (x + 1) * fx;
            float acc[4] = { 0, 0, 0, 0 };
            for (int sx = (int) a0; sx < juce::jmin (sw, (int) std::ceil (a1)); ++sx)
            {
                const double wgt = juce::jmin (a1, (double) sx + 1) - juce::jmax (a0, (double) sx);
                const auto* p = row + sx * in.pixelStride;
                for (int c = 0; c < 4; ++c) acc[c] += (float) wgt * p[c];
            }
            for (int c = 0; c < 4; ++c) tmp[(size_t) ((y * w + x) * 4 + c)] = acc[c] / (float) fx;
        }
    }
    juce::Image out (juce::Image::ARGB, w, h, false, juce::SoftwareImageType());
    juce::Image::BitmapData o (out, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < h; ++y)
    {
        const double b0 = y * fy, b1 = (y + 1) * fy;
        auto* orow = o.getLinePointer (y);
        for (int x = 0; x < w; ++x)
        {
            float acc[4] = { 0, 0, 0, 0 };
            for (int sy = (int) b0; sy < juce::jmin (sh, (int) std::ceil (b1)); ++sy)
            {
                const double wgt = juce::jmin (b1, (double) sy + 1) - juce::jmax (b0, (double) sy);
                for (int c = 0; c < 4; ++c) acc[c] += (float) wgt * tmp[(size_t) ((sy * w + x) * 4 + c)];
            }
            auto* p = orow + x * o.pixelStride;
            for (int c = 0; c < 4; ++c) p[c] = (juce::uint8) juce::jlimit (0, 255, juce::roundToInt (acc[c] / (float) fy));
        }
    }
    return out;
}
}

const juce::Image& anbu::ui::Art::at (float ppu)
{
    if (! src.isValid()) return src;
    for (size_t i = 0; i < scaled.size(); ++i)
        if (std::abs (scaled[i].first - ppu) < 1.0e-3f)
        {
            if (i > 0) std::rotate (scaled.begin(), scaled.begin() + (std::ptrdiff_t) i, scaled.begin() + (std::ptrdiff_t) i + 1);
            return scaled.front().second;
        }
    const float f = ppu / HW::scale;
    juce::Image img = std::abs (f - 1.0f) < 1.0e-3f ? src
                    : resample (src, juce::roundToInt ((float) src.getWidth() * f), juce::roundToInt ((float) src.getHeight() * f));
    scaled.insert (scaled.begin(), { ppu, img });
    if (scaled.size() > 3) scaled.pop_back();
    return scaled.front().second;
}

anbu::ui::Art& anbu::ui::ArtLibrary::get (const char* name)
{
    auto& slot = items[name];
    if (slot == nullptr)
    {
        slot = std::make_unique<Art>();
        slot->src = loadImage (name);
    }
    return *slot;
}

// ============================================================================================ helpers
namespace
{
constexpr float lineL = 1.64f;   // Catamaran: (1100 + 540) / 1000
constexpr float lineU = 1.2f;    // Barlow: (1000 + 200) / 1000

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

juce::Path roundRect (juce::Rectangle<float> r, float radius)
{
    juce::Path p;
    p.addRoundedRectangle (r, radius);
    return p;
}

void text (juce::Graphics& g, const juce::Font& f, const juce::String& s, juce::Rectangle<float> r, juce::Justification j, juce::Colour c)
{
    g.setFont (f);
    g.setColour (c);
    g.drawText (s, r, j, false);
}

void debossText (juce::Graphics& g, const juce::Font& f, const juce::String& s, juce::Rectangle<float> r, juce::Justification j, float alpha = 1.0f)
{
    const auto& T = theme();
    text (g, f, s, r.translated (0.0f, 1.0f), j, T.debHi.withMultipliedAlpha (alpha));
    text (g, f, s, r.translated (0.0f, -1.0f), j, T.debLo.withMultipliedAlpha (alpha));
    text (g, f, s, r, j, T.deboss.withMultipliedAlpha (alpha));
}

juce::Rectangle<float> R (const HW::Rect& r) { return { (float) r.x, (float) r.y, (float) r.w, (float) r.h }; }

// ---------------------------------------------------------------- layout (design units, 1000 x 700)
constexpr float faderX = 134.0f, faderY0 = 350.0f, faderY1 = 528.0f;
const juce::Rectangle<float> faderHit { 104.0f, 334.0f, 60.0f, 210.0f };
const juce::Rectangle<float> modeArea { 36.0f, 588.0f, 190.0f, 76.0f };
constexpr float lampYC = 608.5f, lampYL = 645.0f;
constexpr float modeBtnX = 204.0f, modeBtnY = 626.5f;

const juce::Rectangle<float> faceR = R (HW::face);
constexpr float pivX = 555.0f, pivY = 356.0f;
const float meterSpan = juce::degreesToRadians (38.0f);
const juce::Rectangle<float> pillGR { 732.0f, 66.0f, 36.0f, 22.0f }, pillOut { 770.0f, 66.0f, 40.0f, 22.0f };

const juce::Rectangle<float> screenR = R (HW::screen);
constexpr float plotX0 = 320.0f, plotX1 = 814.0f, plotY0 = 356.0f, plotY1 = 508.0f;
constexpr float specTop = -36.0f, specBot = -96.0f, toneRange = 12.0f;
const juce::Rectangle<float> expandR { 798.0f, 327.0f, 22.0f, 22.0f };
const juce::Rectangle<float> readoutR { 297.0f, 326.0f, 300.0f, 24.0f };

juce::Rectangle<float> keyRect (int i)
{
    if (i < 4) return { 284.0f + 69.0f * (float) i, 584.0f, 65.0f, 32.0f };
    if (i < 6) return { 594.0f + 118.0f * (float) (i - 4), 584.0f, 114.0f, 32.0f };
    if (i == 6) return { 889.0f, 462.0f, 64.0f, 32.0f };
    return { 896.0f, 617.0f, 50.0f, 22.0f };      // bypass
}
const juce::Rectangle<float> rockerR { HW::rockerCx - HW::rockerW / 2, HW::rockerCy - HW::rockerH / 2, HW::rockerW, HW::rockerH };
const juce::Rectangle<float> rockerHit = rockerR.expanded (4.0f).withLeft (rockerR.getX() - 92.0f);   // the AUTO GAIN legend too
const juce::Rectangle<float> stripDispR = R (HW::stripDisplay);
static_assert (HW::stripH == AnbuEditor::panelY, "the preset strip height must match the render");
float keyFlashX (int i) { return i < 4 ? 300.0f + 70.0f * (float) i : i < 6 ? 610.0f + 120.0f * (float) (i - 4) : 921.0f; }

const juce::Rectangle<float> logoWordR { 24.0f, 52.0f, 220.0f, 48.4f }, logoByR { 24.0f, 102.0f, 220.0f, 21.3f };
const juce::String logoWord = juce::String (juce::CharPointer_UTF8 ("\xe0\xae\x85\xe0\xae\xa9\xe0\xaf\x8d\xe0\xae\xaa\xe0\xaf\x81"));  // அன்பு
const juce::String logoA = juce::String (juce::CharPointer_UTF8 ("\xe0\xae\x85"));                                                     // அ
const juce::String logoRest = juce::String (juce::CharPointer_UTF8 ("\xe0\xae\xa9\xe0\xaf\x8d\xe0\xae\xaa\xe0\xaf\x81"));             // ன்பு

// needle scale
float vuFrac (float d) { return (std::pow (10.0f, d / 20.0f) - 0.1f) / (std::pow (10.0f, 3.0f / 20.0f) - 0.1f); }
// GR scale: progressive (same as the rendered face, tools/hw): every dB is wide near the top of the scale
float grFrac (float d) { d = juce::jlimit (0.0f, 21.5f, d); return std::log (1.0f + d / 3.0f) / std::log (1.0f + 20.0f / 3.0f); }
float fracAngle (float f) { return -meterSpan + 2.0f * meterSpan * f; }

// analyser maths shared by the panel and the window
float xOfF (double f, float x0, float x1) { return x0 + (x1 - x0) * (float) (std::log10 (f / 20.0) / 3.0); }

juce::Path specPath (const Spectrum& s, const std::array<float, Spectrum::nPts>& v, float x0, float x1, float y0, float y1,
                     float top, float bot, bool closed)
{
    juce::Path p;
    for (int i = 0; i < Spectrum::nPts; ++i)
    {
        const float x = xOfF (s.freqs[(size_t) i], x0, x1);
        const float y = juce::jlimit (y0 - 2.0f, y1 + 2.0f, y0 + (top - v[(size_t) i]) / (top - bot) * (y1 - y0));
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
const char* const knobIds[5] = { "peakReduction", "inputGain", "outputGain", "mix", "noiseLevel" };
}

// ============================================================================================ preset menu
namespace anbu::ui
{
// A dark menu in the panel's palette (after FabFilter's preset menus): roomy rows, quiet separators, gold
// section headers, an amber bar under the pointer and an amber dot on the loaded preset.
class PresetMenuLook : public juce::LookAndFeel_V4
{
public:
    explicit PresetMenuLook (const Fonts& f) : fonts (f) {}
    float uiScale = 1.0f;

    juce::Font getPopupMenuFont() override { return fonts.ui (500, 15.0f * uiScale); }
    int getPopupMenuBorderSize() override { return juce::roundToInt (6.0f * uiScale); }
    void drawPopupMenuBackground (juce::Graphics& g, int w, int h) override
    {
        const juce::Rectangle<float> r (0.0f, 0.0f, (float) w, (float) h);
        g.setGradientFill (juce::ColourGradient::vertical (hex (0x1d0b0b), 0.0f, hex (0x130606), (float) h));
        g.fillRect (r);
        g.setColour (theme().zari.withAlpha (0.45f));
        g.drawRect (r, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.fillRect (1.0f, 1.0f, (float) w - 2.0f, 1.0f);
    }
    void getIdealPopupMenuItemSize (const juce::String& t, bool isSeparator, int, int& w, int& h) override
    {
        if (isSeparator) { w = 50; h = juce::roundToInt (11.0f * uiScale); return; }
        const auto f = getPopupMenuFont();
        h = juce::roundToInt (26.0f * uiScale);
        w = juce::roundToInt (juce::GlyphArrangement::getStringWidth (f, t)) + juce::roundToInt (64.0f * uiScale);
    }
    void drawPopupMenuSectionHeader (juce::Graphics& g, const juce::Rectangle<int>& area, const juce::String& name) override
    {
        const auto f = fonts.ui (600, 11.0f * uiScale);
        g.setFont (f);
        g.setColour (theme().label.withAlpha (0.9f));
        juce::String spaced;
        for (auto c : name.toUpperCase()) { spaced << juce::String::charToString (c) << juce::String::charToString (0x2009); }
        g.drawText (spaced.trimEnd(), area.toFloat().withTrimmedLeft (14.0f * uiScale).withTrimmedTop (4.0f * uiScale), juce::Justification::centredLeft, false);
    }
    void drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive, bool isHighlighted,
                            bool isTicked, bool hasSubMenu, const juce::String& t, const juce::String& shortcut, const juce::Drawable*,
                            const juce::Colour*) override
    {
        const auto& T = theme();
        auto r = area.toFloat();
        if (isSeparator)
        {
            g.setColour (T.zari.withAlpha (0.22f));
            g.fillRect (r.getX() + 10.0f * uiScale, r.getCentreY(), r.getWidth() - 20.0f * uiScale, 1.0f);
            return;
        }
        if (isHighlighted && isActive)
        {
            g.setColour (T.lamp.withAlpha (0.17f));
            g.fillRoundedRectangle (r.reduced (4.0f * uiScale, 1.0f * uiScale), 3.0f * uiScale);
            g.setColour (T.lamp.withAlpha (0.8f));
            g.fillRoundedRectangle (r.getX() + 4.0f * uiScale, r.getY() + 5.0f * uiScale, 2.0f * uiScale, r.getHeight() - 10.0f * uiScale, 1.0f);
        }
        if (isTicked)
        {
            const float d = 6.0f * uiScale;
            g.setColour (T.lamp.withAlpha (0.35f));
            g.fillEllipse (r.getX() + 13.0f * uiScale - d, r.getCentreY() - d, 2 * d, 2 * d);
            g.setColour (T.lamp);
            g.fillEllipse (r.getX() + 13.0f * uiScale - 3.0f * uiScale, r.getCentreY() - 3.0f * uiScale, 6.0f * uiScale, 6.0f * uiScale);
        }
        const auto col = ! isActive ? hex (0x6f6656) : isHighlighted ? T.value : isTicked ? hex (0xffd79a) : hex (0xe2d2b3);
        g.setFont (getPopupMenuFont());
        g.setColour (col);
        auto tr = r.withTrimmedLeft (26.0f * uiScale).withTrimmedRight (26.0f * uiScale);
        g.drawText (t, tr, juce::Justification::centredLeft, true);
        if (shortcut.isNotEmpty())
        {
            g.setFont (fonts.ui (500, 12.5f * uiScale));
            g.setColour (hex (0x8f8574));
            g.drawText (shortcut, tr, juce::Justification::centredRight, true);
        }
        if (hasSubMenu)
        {
            juce::Path c;
            const float x = r.getRight() - 15.0f * uiScale, y = r.getCentreY(), k = 3.5f * uiScale;
            c.startNewSubPath (x - k * 0.6f, y - k); c.lineTo (x + k * 0.6f, y); c.lineTo (x - k * 0.6f, y + k);
            g.setColour (isHighlighted ? T.value : T.label.withAlpha (0.8f));
            g.strokePath (c, juce::PathStrokeType (1.4f * uiScale, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }
    void drawPopupMenuUpDownArrow (juce::Graphics& g, int w, int h, bool isScrollUpArrow) override
    {
        juce::Path c;
        const float x = (float) w * 0.5f, y = (float) h * 0.5f, k = 4.0f * uiScale;
        if (isScrollUpArrow) { c.startNewSubPath (x - k, y + k * 0.5f); c.lineTo (x, y - k * 0.5f); c.lineTo (x + k, y + k * 0.5f); }
        else { c.startNewSubPath (x - k, y - k * 0.5f); c.lineTo (x, y + k * 0.5f); c.lineTo (x + k, y - k * 0.5f); }
        g.setColour (theme().label);
        g.strokePath (c, juce::PathStrokeType (1.4f * uiScale));
    }

private:
    const Fonts& fonts;
};
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

        // header: a strip of the faceplate
        const juce::Rectangle<float> head (0.0f, 0.0f, (float) W, 40.0f);
        g.setGradientFill (juce::ColourGradient::vertical (T.plateTop, 0.0f, T.plateBot, 40.0f));
        g.fillRect (head);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.fillRect (0.0f, 0.0f, (float) W, 1.0f);
        g.setColour (T.zari.withAlpha (0.55f));
        g.fillRect (0.0f, 39.0f, (float) W, 1.0f);
        const auto fLogo = fonts.logo (800, 22.0f), fHead = fonts.ui (600, 13.0f);
        const float wLogo = juce::GlyphArrangement::getStringWidth (fLogo, logoWord);
        debossText (g, fLogo, logoWord, { 18.0f, 20.0f - 0.5f * 22.0f * lineL, wLogo + 4.0f, 22.0f * lineL }, juce::Justification::centredLeft);
        text (g, fHead, "A N A L Y S E R", { 18.0f + wLogo + 14.0f, 20.0f - 0.5f * 13.0f * lineU + 1.0f, 160.0f, 13.0f * lineU },
              juce::Justification::centredLeft, T.label);
        const float ra = readoutAlpha ? readoutAlpha() : 0.0f;
        if (ra > 0.0f && readout)
            text (g, fonts.ui (600, 15.0f), readout(), { 460.0f, 20.0f - 9.0f + 1.0f, 400.0f, 18.0f }, juce::Justification::centredRight, T.value.withAlpha (ra));
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
        const auto fLab = fonts.ui (500, 13.0f);
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
            g.fillPath (specPath (spec, spec.out, x0, x1, y0, y1, top, bot, true));
            g.setColour (hex (0xc8b08a));
            g.strokePath (specPath (spec, spec.out, x0, x1, y0, y1, top, bot, false), juce::PathStrokeType (1.4f, juce::PathStrokeType::curved));
            if (spec.inputActive)
            {
                juce::Path dashed;
                const float dash[] = { 4.0f, 3.0f };
                juce::PathStrokeType (1.2f).createDashedStroke (dashed, specPath (spec, spec.in, x0, x1, y0, y1, top, bot, false), dash, 2);
                g.setColour (hex (0x8e97bf));
                g.fillPath (dashed);
            }
            g.setColour (hex (0xf1e3c6).withAlpha (0.6f));
            g.strokePath (specPath (spec, spec.peak, x0, x1, y0, y1, top, bot, false), juce::PathStrokeType (1.0f));
            g.setColour (hex (0xf07a3a).withAlpha (0.55f));
            g.fillPath (specPath (spec, spec.harm, x0, x1, y0, y1, top, bot, true));
            g.setColour (hex (0xf58a4e));
            g.strokePath (specPath (spec, spec.harm, x0, x1, y0, y1, top, bot, false), juce::PathStrokeType (1.3f, juce::PathStrokeType::curved));
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
        const auto fLeg = fonts.ui (500, 14.0f);
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
            text (g, fLeg, t, { lx + 21.0f, fy - 9.0f + 1.0f, tw + 4.0f, 18.0f }, juce::Justification::centredLeft, hex (0x9a907e));
            lx += 21.0f + tw + 20.0f;
        };
        item (0, "Output"); item (1, "Input"); item (2, "Peak hold"); item (3, "Harmonics"); item (4, "Tone");

        const auto fBtn = fonts.ui (600, 14.0f);
        juce::Rectangle<float> b60, b90, box;
        rangeRects (b60, b90, box);
        text (g, fLeg, "Range", { box.getX() - 70.0f, fy - 9.0f + 1.0f, 60.0f, 18.0f }, juce::Justification::centredRight, T.label);
        g.setColour (hex (0x0e0303));
        g.fillRoundedRectangle (box, 4.0f);
        for (int i = 0; i < 2; ++i)
        {
            const auto r = i == 0 ? b60 : b90;
            const bool on = (i == 1) == range90;
            if (on) { g.setColour (T.label.withAlpha (0.22f)); g.fillRoundedRectangle (r, 3.0f); }
            text (g, fBtn, i == 0 ? "60 dB" : "90 dB", r.translated (0.0f, 0.5f), juce::Justification::centred, on ? T.label : hex (0x7c7464));
        }
        g.setColour (T.zari.withAlpha (0.4f));
        g.drawRect (all, 1.0f);
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
        const auto f = fonts.ui (600, 14.0f);
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

    bg = &library->get ("bg_jpg");
    faceOut = &library->get ("face_out_jpg");
    pivot = &library->get ("pivot_png");
    glassMeter = &library->get ("glass_meter_png");
    glassDisp = &library->get ("glass_disp_png");
    faderCap = &library->get ("fader_cap_png");
    lampC = &library->get ("lamp_c_png");
    lampL = &library->get ("lamp_l_png");
    noLogo = &library->get ("nologo_jpg");
    for (int i = 0; i < 8; ++i) keyArt[(size_t) i] = &library->get (("key_" + juce::String (i) + "_png").toRawUTF8());
    stripArt = &library->get ("strip_jpg");
    rockerOn = &library->get ("rocker_on_png");
    for (int i = 0; i < 3; ++i) stripKeyArt[(size_t) i] = &library->get (("hkey_" + juce::String (i) + "_png").toRawUTF8());
    menuLook = std::make_unique<anbu::ui::PresetMenuLook> (fonts);

    // preset name entry (Save): typed straight into the strip's display
    nameEditor.setLookAndFeel (&invisible);
    nameEditor.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    nameEditor.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    nameEditor.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    nameEditor.setColour (juce::TextEditor::textColourId, hex (0xffd79a));
    nameEditor.setColour (juce::TextEditor::highlightColourId, theme().lamp.withAlpha (0.35f));
    nameEditor.setColour (juce::TextEditor::highlightedTextColourId, hex (0xfff1d8));
    nameEditor.setColour (juce::CaretComponent::caretColourId, theme().lamp);
    nameEditor.setJustification (juce::Justification::centred);
    nameEditor.setInputRestrictions (48, {});
    nameEditor.setTextToShowWhenEmpty ("Name your preset", hex (0x8a6a4a));
    nameEditor.setSelectAllWhenFocused (true);
    nameEditor.onReturnKey = [this] { endSaveAs (true); };
    nameEditor.onEscapeKey = [this] { endSaveAs (false); };
    nameEditor.onFocusLost = [this] { endSaveAs (false); };
    addChildComponent (nameEditor);

    for (int k = 0; k < 5; ++k)
    {
        auto& kn = knobs[(size_t) k];
        const auto& a = HW::knobs[k];
        kn.art = k;
        kn.id = knobIds[k];
        kn.name = knobNames[k];
        kn.strip = &library->get (a.strip);
        kn.ring = &library->get (a.ring);
        kn.slider.setLookAndFeel (&invisible);
        kn.slider.setTitle (kn.name);          // screen readers / host accessibility
        kn.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        kn.slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        kn.slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        kn.slider.setMouseDragSensitivity (k == 0 ? 320 : 260);
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
    fader.setTitle ("Color");
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
        repaintDesign (faderHit.expanded (16.0f, 30.0f));
        if (fader.isMouseOverOrDragging()) setReadout (5);
    };
    fader.onDragStart = [this] { if (introRunning) skipIntro(); setReadout (5); };

    // analyser
    for (int i = 0; i < fftSize; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) fftSize);
    for (int i = 0; i < Spectrum::nPts; ++i)
        spec.freqs[(size_t) i] = 20.0 * std::pow (1000.0, (double) i / (Spectrum::nPts - 1));
    spec.out.fill (-140.0f); spec.in.fill (-140.0f); spec.harm.fill (-140.0f); spec.peak.fill (-140.0f); spec.tone.fill (0.0f);

    for (int i = 0; i < 8; ++i) keyState[(size_t) i] = keyOn (i);
    agState = proc.apvts.getRawParameterValue (ids::autoGain)->load() > 0.5f;
    shownAgDb = proc.meterAgDb.load();
    shownPreset = proc.presets.currentName();
    shownModified = proc.presets.isModified();
    greyTarget = keyState[7];
    greyP = greyTarget ? 1.0f : 0.0f;
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
    resizingSince = 0.0;
    startTimerHz (60);
}

AnbuEditor::~AnbuEditor()
{
    stopTimer();
    analyserWindow = nullptr;
    for (auto& kn : knobs) kn.slider.setLookAndFeel (nullptr);
    fader.setLookAndFeel (nullptr);
    nameEditor.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void AnbuEditor::resized()
{
    resizingSince = juce::Time::getMillisecondCounterHiRes();
    const float s = scale();
    for (auto& kn : knobs)
    {
        const auto& a = HW::knobs[kn.art];
        const float r = (a.rr + 6.0f) * s;
        kn.slider.radius = r;
        kn.slider.setBounds (juce::Rectangle<float> (a.cx * s - r, (a.cy + (float) panelY) * s - r, 2 * r, 2 * r).toNearestInt());
    }
    fader.setBounds ((faderHit.translated (0.0f, (float) panelY) * s).toNearestInt());
    nameEditor.setBounds ((stripDispR.reduced (40.0f, 3.0f) * s).toNearestInt());
    nameEditor.applyFontToAllText (fonts.ui (600, 15.0f * s));
}

void AnbuEditor::repaintDesign (juce::Rectangle<float> r)
{
    repaint ((r.translated (0.0f, (float) panelY) * scale()).getSmallestIntegerContainer().expanded (2));
}

void AnbuEditor::repaintStrip (juce::Rectangle<float> r)
{
    repaint ((r * scale()).getSmallestIntegerContainer().expanded (2));
}

float AnbuEditor::pos01 (const juce::Slider& s) const
{
    return clamp01 ((float) ((s.getValue() - s.getMinimum()) / juce::jmax (1.0e-9, s.getMaximum() - s.getMinimum())));
}

juce::Rectangle<float> AnbuEditor::knobArea (const Knob& kn) const
{
    return R (HW::knobs[kn.art].ringRect).expanded (2.0f);
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
void AnbuEditor::drawArt (juce::Graphics& g, anbu::ui::Art& art, juce::Rectangle<float> dest, float alpha)
{
    if (! art.src.isValid() || alpha <= 0.0f) return;
    const auto& img = art.at (artScale (g));
    g.setOpacity (juce::jmin (1.0f, alpha));
    g.drawImageTransformed (img, juce::AffineTransform::scale (dest.getWidth() / (float) img.getWidth(), dest.getHeight() / (float) img.getHeight())
                                     .translated (dest.getX(), dest.getY()));
    g.setOpacity (1.0f);
}

void AnbuEditor::renderStatic (juce::Graphics& g, float ppu)
{
    const auto& T = theme();
    drawArt (g, *stripArt, { 0.0f, 0.0f, (float) baseW, (float) panelY });
    g.addTransform (juce::AffineTransform::translation (0.0f, (float) panelY));
    {
        const auto& img = bg->at (ppu);
        g.drawImageTransformed (img, juce::AffineTransform::scale ((float) baseW / (float) img.getWidth(), 700.0f / (float) img.getHeight()));
    }
    if (staticOut) drawArt (g, *faceOut, faceR);

    // analyser screen: grid, scale, legend, expand icon (drawn on the screen, under the glass)
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

    const auto fL = fonts.ui (500, 11.5f);
    const auto lab = hex (0x9a8e7c);
    text (g, fL, "+12", { plotX0 - 7.0f - 40.0f, ey (12.0f) - 8.0f, 40.0f, 16.0f }, juce::Justification::centredRight, lab);
    text (g, fL, "0", { plotX0 - 7.0f - 40.0f, ey (0.0f) - 8.0f, 40.0f, 16.0f }, juce::Justification::centredRight, lab);
    text (g, fL, minus + "12", { plotX0 - 7.0f - 40.0f, ey (-12.0f) - 8.0f, 40.0f, 16.0f }, juce::Justification::centredRight, lab);
    const std::pair<int, const char*> fl[] = { { 100, "100" }, { 1000, "1k" }, { 10000, "10k" } };
    for (auto& [f, t] : fl)
        text (g, fL, t, { xOfF (f, plotX0, plotX1) - 22.0f, plotY1 + 5.0f, 44.0f, 16.0f }, juce::Justification::centred, lab);

    const auto fG = fonts.ui (500, 11.5f);
    const auto legCol = hex (0x8f8574);
    const juce::String names[3] = { "Output", "Harmonics", "Tone" };
    float x = expandR.getX() - 14.0f;
    const float cy = expandR.getCentreY();
    for (int i = 2; i >= 0; --i)
    {
        const float tw = juce::GlyphArrangement::getStringWidth (fG, names[i]);
        x -= tw;
        text (g, fG, names[i], { x, cy - 8.0f, tw + 2.0f, 16.0f }, juce::Justification::centredLeft, legCol);
        x -= 6.0f + 12.0f;
        if (i == 0) { g.setColour (hex (0xa89272).withAlpha (0.7f)); g.fillRoundedRectangle (x, cy - 4.0f, 12.0f, 8.0f, 1.0f); }
        else if (i == 1) { g.setColour (hex (0xf07a3a)); g.fillRoundedRectangle (x, cy - 4.0f, 12.0f, 8.0f, 1.0f); }
        else { g.setColour (hex (0xf4eada)); g.fillRect (x, cy - 1.0f, 12.0f, 2.0f); }
        x -= 14.0f;
    }
    drawExpandIcon (g, expandR.getX() + 4.0f, expandR.getY() + 4.0f, hex (0xb89a6c));
}

// ============================================================================================ dynamic parts
void AnbuEditor::drawKnob (juce::Graphics& g, Knob& kn, float pos, float flash)
{
    const auto& a = HW::knobs[kn.art];
    const float cx = a.cx, cy = a.cy;
    // lamp ring: the lit render, revealed along the value arc (dark at the knob's zero; bipolar knobs from 0 dB)
    auto litArc = [&] (float d0, float d1, float alpha)
    {
        const float lo = juce::jmin (d0, d1), hi = juce::jmax (d0, d1);
        if (hi - lo < 0.5f || alpha <= 0.0f) return;
        const float soft = juce::jmin (4.0f, hi - lo);
        const float big = (float) a.ringRect.w;
        auto wedge = [&] (float w0, float w1, float al)
        {
            if (w1 <= w0) return;
            juce::Path p;
            p.addPieSegment (cx - big, cy - big, 2 * big, 2 * big, juce::degreesToRadians (w0), juce::degreesToRadians (w1), 0.0f);
            g.saveState();
            g.reduceClipRegion (p);
            drawArt (g, *kn.ring, R (a.ringRect), al);
            g.restoreState();
        };
        wedge (lo + 0.5f * soft, hi - 0.5f * soft, alpha);
        for (int j = 0; j < 4; ++j)
        {
            const float f = ((float) j + 0.5f) / 4.0f;
            wedge (lo - 0.5f * soft + soft * (float) j / 4.0f, lo - 0.5f * soft + soft * (float) (j + 1) / 4.0f, alpha * f);
            wedge (hi + 0.5f * soft - soft * (float) (j + 1) / 4.0f, hi + 0.5f * soft - soft * (float) j / 4.0f, alpha * f);
        }
    };
    const float a0 = a.bipolar ? 0.0f : -135.0f, a1 = -135.0f + 270.0f * pos;
    litArc (a0, a1, 1.0f);
    if (flash > 0.0f) litArc (-135.0f, 135.0f, flash);

    // knob: frame of the rendered filmstrip, clipped to the knob's round silhouette
    if (kn.strip != nullptr && kn.strip->src.isValid())
    {
        const int n = a.frames;
        const int f = juce::jlimit (0, n - 1, juce::roundToInt (pos * (float) (n - 1)));
        const auto& img = kn.strip->at (artScale (g));
        const float fh = (float) img.getHeight() / (float) n;
        const auto dest = R (a.sprite);
        g.saveState();
        juce::Path c;
        c.addEllipse (cx - a.radius - 0.5f, cy - a.radius - 0.5f, 2 * a.radius + 1.0f, 2 * a.radius + 1.0f);
        g.reduceClipRegion (c);
        const float sx = dest.getWidth() / (float) img.getWidth(), sy = dest.getHeight() / fh;
        g.drawImageTransformed (img, juce::AffineTransform::translation (0.0f, -fh * (float) f).scaled (sx, sy).translated (dest.getX(), dest.getY()));
        g.restoreState();
    }
}

void AnbuEditor::drawFaderCap (juce::Graphics& g, float pos)
{
    const float yc = faderY1 - (faderY1 - faderY0) * pos;
    drawArt (g, *faderCap, R (HW::faderCap).translated (0.0f, yc - HW::faderRefY));
}

void AnbuEditor::drawModeLamps (juce::Graphics& g, float flash)
{
    float onAlpha = 1.0f;
    if (introRunning) onAlpha = lerp (0.35f, 1.0f, easeOut (((float) introT() - 1.762f) / 0.6f));
    drawArt (g, *lampC, R (HW::lampC), juce::jmax (modeLimit ? 0.0f : onAlpha, flash));
    drawArt (g, *lampL, R (HW::lampL), juce::jmax (modeLimit ? onAlpha : 0.0f, flash));
}

float AnbuEditor::meterFrac() const
{
    // fraction of the scale from the left end. GR reads right to left: no reduction rests at the right.
    return proc.uiMeterOut ? juce::jlimit (-0.04f, 1.06f, vuFrac (vuDb)) : 1.0f - grFrac (grDb);
}

void AnbuEditor::drawMeterDynamic (juce::Graphics& g)
{
    float f = needlePos;
    if (introRunning)
    {
        // the needle swings across the scale and settles: from its rest end to the far end and back
        const float rest = proc.uiMeterOut ? 0.0f : 1.0f, far = 1.0f - rest;
        const float u = ((float) introT() - 0.95f) / 1.1f;
        if (u <= 0.0f) f = rest;
        else if (u < 0.45f) f = lerp (rest, far, bezier (0.3f, 0.6f, 0.3f, 1.0f, u / 0.45f));
        else if (u < 1.0f) f = lerp (far, needlePos, bezier (0.3f, 0.6f, 0.3f, 1.0f, (u - 0.45f) / 0.55f));
    }
    const float a = fracAngle (juce::jlimit (-0.06f, 1.08f, f));
    g.saveState();
    g.reduceClipRegion (roundRect (faceR, 3.0f));
    const auto rot = juce::AffineTransform::rotation (a, pivX, pivY);
    juce::Path nd;
    nd.startNewSubPath (pivX, pivY); nd.lineTo (pivX, faceR.getY() + 66.0f);
    // soft shadow on the face, then the needle
    g.setColour (juce::Colours::black.withAlpha (0.16f));
    g.strokePath (nd, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded), rot.translated (4.0f, 5.0f));
    g.setColour (juce::Colours::black.withAlpha (0.28f));
    g.strokePath (nd, juce::PathStrokeType (2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded), rot.translated (3.0f, 4.0f));
    g.setColour (hex (0xf3e8d2));
    g.strokePath (nd, juce::PathStrokeType (1.7f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded), rot);
    g.restoreState();
    drawArt (g, *pivot, R (HW::pivot));
    drawArt (g, *glassMeter, faceR);
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
        text (g, fonts.ui (600, 14.0f), readoutText(), readoutR, juce::Justification::centredLeft, hex (0xf1e3c6).withAlpha (readoutAlpha));
    else if (agState)
    {
        const int tenths = juce::roundToInt (shownAgDb * 10.0f);
        const auto v = (tenths > 0 ? "+" : tenths < 0 ? minus : juce::String()) + juce::String (std::abs (tenths) / 10) + "." + juce::String (std::abs (tenths) % 10);
        text (g, fonts.ui (600, 14.0f), "Auto gain  " + v + " dB", readoutR, juce::Justification::centredLeft, hex (0xf1e3c6).withAlpha (0.55f));
    }

    g.saveState();
    g.reduceClipRegion (juce::Rectangle<float> (plotX0, plotY0, plotX1 - plotX0, plotY1 - plotY0).toNearestInt());
    if (spec.active)
    {
        g.setColour (hex (0xa89272).withAlpha (0.3f));
        g.fillPath (specPath (spec, spec.out, plotX0, plotX1, plotY0, plotY1, specTop, specBot, true));
        g.setColour (hex (0xc8b08a));
        g.strokePath (specPath (spec, spec.out, plotX0, plotX1, plotY0, plotY1, specTop, specBot, false), juce::PathStrokeType (1.3f, juce::PathStrokeType::curved));
        g.setColour (hex (0xf07a3a).withAlpha (0.55f));
        g.fillPath (specPath (spec, spec.harm, plotX0, plotX1, plotY0, plotY1, specTop, specBot, true));
        g.setColour (hex (0xf58a4e));
        g.strokePath (specPath (spec, spec.harm, plotX0, plotX1, plotY0, plotY1, specTop, specBot, false), juce::PathStrokeType (1.2f, juce::PathStrokeType::curved));
    }
    const auto tp = tonePath (spec, plotX0, plotX1, plotY0, plotY1, toneRange);
    g.setColour (hex (0xf4eada).withAlpha (0.16f));
    g.strokePath (tp, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (hex (0xf4eada));
    g.strokePath (tp, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.restoreState();
    drawArt (g, *glassDisp, screenR);
}

void AnbuEditor::drawKeys (juce::Graphics& g)
{
    const auto& T = theme();
    for (int i = 0; i < 8; ++i)
    {
        const auto r = keyRect (i);
        if (! g.clipRegionIntersects (r.expanded (8.0f).getSmallestIntegerContainer())) continue;
        if (keyState[(size_t) i]) drawArt (g, *keyArt[(size_t) i], R (HW::keys[i]));
        const float flash = flashAt (keyFlashX (i));
        if (flash > 0.0f)
        {
            const auto p = roundRect (r, 3.0f);
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
}

void AnbuEditor::drawIntroOverlay (juce::Graphics& g)
{
    const auto& T = theme();
    const float t = (float) introT();
    const float deb = easeIn ((t - 1.0f) / 0.65f);

    // the plate's own (debossed) logo appears as the travelling logo lands on it
    drawArt (g, *noLogo, R (HW::logoPatch), 1.0f - deb);

    // panel hidden behind the chassis colour until the logo leaves the centre
    float ov = 1.0f;
    if (t >= 0.9f) ov = 1.0f - easeInOut ((t - 0.9f) / 0.45f);
    if (ov > 0.0f)
    {
        g.setColour (T.chassis.withAlpha (ov));
        g.fillRect (0.0f, (float) -panelY, (float) baseW, (float) baseH);
    }

    // அ ignites like a lamp, the rest of the word grows out of it, then it shrinks to its place and debosses
    const auto font = fonts.logo (800, 44.0f);
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
    const float cx = logoWordR.getCentreX() + 366.0f * (1.0f - move);
    const float cy = logoWordR.getCentreY() + 274.0f * (1.0f - move);
    const float sc = 2.2f - 1.2f * move;
    const float fade = 1.0f - deb;          // hands over to the rendered deboss

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
        alpha *= fade;
        if (alpha <= 0.0f) return;
        shadow (g, p, T.glow.withMultipliedAlpha (alpha), 16.0f * (1.0f - deb) + 0.5f, {});
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
    const float by = easeOut ((t - 0.95f) / 0.4f) * fade;
    if (by > 0.0f)
        text (g, fonts.logo (600, 13.0f), "by R4N", logoByR.translated (0.0f, 4.0f * (1.0f - by)), juce::Justification::centred, lit.withMultipliedAlpha (by));
}

// ============================================================================================ paint
void AnbuEditor::paint (juce::Graphics& g)
{
    const float p = greyProgress();
    const bool animating = greyStart >= 0.0 || greyFixedT >= 0.0;
    if (p <= 0.0005f && ! animating) { paintPanel (g); return; }

    const float dpr = juce::jmax (0.25f, g.getInternalContext().getPhysicalPixelScaleFactor());
    const auto clip = g.getClipBounds().getIntersection (getLocalBounds());
    if (clip.isEmpty()) return;

    if (animating)
    {
        // the colour drains out of (or flows back into) a still of the panel taken when the key was pressed:
        // per frame only the mask moves, so the panel is not repainted while it runs
        const int W = juce::roundToInt ((float) getWidth() * dpr), H = juce::roundToInt ((float) getHeight() * dpr);
        if (! snapValid || snapColour.getWidth() != W || snapColour.getHeight() != H || std::abs (snapDpr - dpr) > 1.0e-3f)
            captureDrainSnapshot (dpr, W, H);
        const auto dev = juce::Rectangle<int>::leftTopRightBottom ((int) std::floor ((float) clip.getX() * dpr), (int) std::floor ((float) clip.getY() * dpr),
                                                                   (int) std::ceil ((float) clip.getRight() * dpr), (int) std::ceil ((float) clip.getBottom() * dpr))
                             .getIntersection ({ 0, 0, W, H });
        const double t = greyFixedT >= 0.0 ? greyFixedT : (juce::Time::getMillisecondCounterHiRes() - greyStart) * 0.001;
        composeDrain (dev, p, t);
        g.drawImageTransformed (fxDst, juce::AffineTransform::scale (1.0f / dpr));
        return;
    }

    // bypassed and settled: paint just the area that changed, in grey
    const int w = juce::jmax (1, (int) std::ceil ((float) clip.getWidth() * dpr)), h = juce::jmax (1, (int) std::ceil ((float) clip.getHeight() * dpr));
    if (fxSrc.getWidth() != w || fxSrc.getHeight() != h) fxSrc = juce::Image (juce::Image::RGB, w, h, true, juce::SoftwareImageType());
    else fxSrc.clear (fxSrc.getBounds());
    {
        juce::Graphics gi (fxSrc);
        gi.addTransform (juce::AffineTransform::translation ((float) -clip.getX(), (float) -clip.getY()).scaled (dpr));
        paintPanel (gi);
    }
    greyRegion (fxSrc, { juce::roundToInt ((float) clip.getX() * dpr), juce::roundToInt ((float) clip.getY() * dpr) }, dpr);
    g.drawImageTransformed (fxSrc, juce::AffineTransform::scale ((float) clip.getWidth() / (float) w, (float) clip.getHeight() / (float) h)
                                       .translated ((float) clip.getX(), (float) clip.getY()));
}

void AnbuEditor::paintPanel (juce::Graphics& g)
{
    const float s = scale();
    const float phys = s * g.getInternalContext().getPhysicalPixelScaleFactor();
    if (! staticLayer.isValid() || std::abs (phys - staticScale) > 1.0e-3f || staticOut != proc.uiMeterOut || staticFast != (resizingSince > 0.0))
    {
        staticScale = phys;
        staticFast = resizingSince > 0.0;
        staticOut = proc.uiMeterOut;
        staticLayer = juce::Image (juce::Image::ARGB, juce::roundToInt ((float) baseW * phys), juce::roundToInt ((float) baseH * phys), true);
        juce::Graphics gs (staticLayer);
        gs.addTransform (juce::AffineTransform::scale (phys));
        gs.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        renderStatic (gs, staticFast ? HW::scale : phys);
    }
    g.addTransform (juce::AffineTransform::scale (s));
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImageTransformed (staticLayer, juce::AffineTransform::scale ((float) baseW / (float) staticLayer.getWidth(),
                                                                       (float) baseH / (float) staticLayer.getHeight()));
    if (g.clipRegionIntersects (juce::Rectangle<int> (0, 0, baseW, panelY)))
        drawStripDynamic (g);
    g.addTransform (juce::AffineTransform::translation (0.0f, (float) panelY));

    for (auto& kn : knobs)
        if (g.clipRegionIntersects (knobArea (kn).getSmallestIntegerContainer()))
            drawKnob (g, kn, pos01 (kn.slider) * valueEase (HW::knobs[kn.art].cx), flashAt (HW::knobs[kn.art].cx));

    if (g.clipRegionIntersects (faderHit.expanded (16.0f, 30.0f).getSmallestIntegerContainer()))
    {
        const float e = introRunning ? bezier (0.2f, 0.75f, 0.25f, 1.0f, ((float) introT() - (1.75f + 0.0002f * faderX + 0.08f)) / 0.8f) : 1.0f;
        drawFaderCap (g, pos01 (fader) * e);
    }
    if (g.clipRegionIntersects (modeArea.getSmallestIntegerContainer()))
        drawModeLamps (g, flashAt (56.0f));
    if (g.clipRegionIntersects (faceR.getSmallestIntegerContainer()))
        drawMeterDynamic (g);
    if (g.clipRegionIntersects (screenR.getSmallestIntegerContainer()))
        drawDisplayDynamic (g);
    drawKeys (g);
    if (g.clipRegionIntersects (rockerR.expanded (6.0f).getSmallestIntegerContainer()))
        drawRocker (g);

    if (introRunning)
        drawIntroOverlay (g);
}

// ============================================================================================ bypass colour drain
namespace
{
constexpr float drainMs = 900.0f;                         // one full drain (or return) takes 0.9 s
const juce::Point<float> drainCentre { 921.0f, 628.0f };  // the bypass key (panel units)
const juce::Rectangle<float> drainKeep { 894.0f, 615.0f, 54.0f, 26.0f };   // the key keeps its colour
constexpr float drainEdge = 22.0f, drainSmear = 20.0f;    // soft front and inward pull (panel units)

inline void toGrey (const juce::uint8* c, juce::uint8* d) noexcept
{
    // B G R (A) in memory. Rec.709 luma with a touch of contrast, integer maths. Alpha (if any) is left alone.
    const int l = (19 * c[0] + 183 * c[1] + 54 * c[2]) >> 8;
    const int v = juce::jlimit (0, 255, l + (((l - 128) * 15) >> 8));
    d[0] = d[1] = d[2] = (juce::uint8) v;
}
}

static double greyDurationMs (float from, bool toGrey)
{
    return drainMs * juce::jmax (0.35f, std::abs ((toGrey ? 1.0f : 0.0f) - from));
}

void AnbuEditor::startGrey (bool toGrey)
{
    greyFrom = greyProgress();
    greyTarget = toGrey;
    greyStart = juce::Time::getMillisecondCounterHiRes();
    greyFixedT = -1.0;
    snapValid = false;                                     // a fresh still of the panel as it is now
}

float AnbuEditor::greyProgress() const
{
    if (greyStart < 0.0) return greyP;
    const float target = greyTarget ? 1.0f : 0.0f;
    const float u = (float) ((juce::Time::getMillisecondCounterHiRes() - greyStart) / greyDurationMs (greyFrom, greyTarget));
    // slow start, a fast pull, a soft landing
    return greyFrom + (target - greyFrom) * bezier (0.6f, 0.0f, 0.3f, 1.0f, u);
}

void AnbuEditor::setBypassFrame (float progress, double seconds)
{
    greyStart = -1.0;
    greyP = juce::jlimit (0.0f, 1.0f, progress);
    greyFixedT = seconds;                                  // the still of the panel is kept between frames
    repaint();
}

// grey an image of the window (origin: its top-left in device pixels); the bypass key keeps its colour
void AnbuEditor::greyRegion (juce::Image& img, juce::Point<int> origin, float dpr)
{
    juce::Image::BitmapData bm (img, juce::Image::BitmapData::readWrite);
    const float ppu = scale() * dpr;
    const auto keep = (drainKeep.translated (0.0f, (float) panelY) * ppu).toNearestInt().translated (-origin.x, -origin.y);
    for (int y = 0; y < bm.height; ++y)
    {
        auto* row = bm.getLinePointer (y);
        const bool keepRow = y >= keep.getY() && y < keep.getBottom();
        for (int x = 0; x < bm.width; ++x)
        {
            if (keepRow && x >= keep.getX() && x < keep.getRight()) continue;
            auto* px = row + x * bm.pixelStride;
            toGrey (px, px);
        }
    }
}

void AnbuEditor::captureDrainSnapshot (float dpr, int W, int H)
{
    snapColour = juce::Image (juce::Image::RGB, W, H, true, juce::SoftwareImageType());   // opaque: drawn as a plain copy
    {
        juce::Graphics gi (snapColour);
        gi.addTransform (juce::AffineTransform::scale (dpr));
        paintPanel (gi);
    }
    snapGrey = snapColour.createCopy();
    greyRegion (snapGrey, {}, dpr);
    if (fxDst.getWidth() != W || fxDst.getHeight() != H) fxDst = juce::Image (juce::Image::RGB, W, H, false, juce::SoftwareImageType());

    // angle and distance from the key at every grid node (gridQ device pixels apart)
    const float ppu = scale() * dpr;
    gridW = W / gridQ + 2; gridH = H / gridQ + 2;
    gridAng.assign ((size_t) (gridW * gridH), 0.0f);
    gridDist.assign ((size_t) (gridW * gridH), 0.0f);
    gridE.assign ((size_t) (gridW * gridH), 0.0f);
    for (int gy = 0; gy < gridH; ++gy)
        for (int gx = 0; gx < gridW; ++gx)
        {
            const float dx = ((float) (gx * gridQ) + 0.5f) / ppu - drainCentre.x;
            const float dy = ((float) (gy * gridQ) + 0.5f) / ppu - (float) panelY - drainCentre.y;
            const size_t i = (size_t) (gy * gridW + gx);
            gridAng[i] = (std::atan2 (dy, dx) + juce::MathConstants<float>::pi) / juce::MathConstants<float>::twoPi * 1024.0f;
            gridDist[i] = std::sqrt (dx * dx + dy * dy);
        }
    snapDpr = dpr;
    snapValid = true;
}

// write the drained frame into fxDst over dev (device pixels). p: 0 = full colour, 1 = fully grey.
void AnbuEditor::composeDrain (juce::Rectangle<int> dev, float p, double t)
{
    if (dev.isEmpty()) return;
    const float dpr = snapDpr, ppu = scale() * dpr;

    // the drain front: a radius per direction, wobbling with tendrils that change shape while it moves
    constexpr int nA = 1024;
    float Rtab[nA + 1];
    const float Dmax = 1120.0f;                             // farthest corner from the key
    const float Rb = p * (Dmax + 2.0f * drainEdge);
    const float A = 0.42f * std::pow (juce::jmax (0.0f, std::sin (juce::MathConstants<float>::pi * juce::jlimit (0.0f, 1.0f, p))), 0.8f);
    const float tt = (float) t;
    for (int a = 0; a < nA; ++a)
    {
        const float th = juce::MathConstants<float>::twoPi * (float) a / (float) nA;
        float f = 0.30f * std::sin (2.0f * th + 1.3f * tt)
                + 0.55f * std::sin (3.0f * th + 2.1f * tt + 0.7f)
                + 0.35f * std::sin (5.0f * th - 3.3f * tt + 1.9f)
                + 0.25f * std::sin (8.0f * th + 4.7f * tt + 0.3f)
                + 0.18f * std::sin (13.0f * th - 6.1f * tt + 2.6f);
        f /= 1.63f;
        f = (f >= 0.0f ? 1.0f : -1.0f) * std::pow (std::abs (f), 0.65f);   // sharpen into tendrils
        Rtab[a] = Rb * (1.0f + A * f);
    }
    Rtab[nA] = Rtab[0];

    // mask at the grid nodes the clip touches
    const int gx0 = juce::jmax (0, dev.getX() / gridQ), gx1 = juce::jmin (gridW - 1, dev.getRight() / gridQ + 1);
    const int gy0 = juce::jmax (0, dev.getY() / gridQ), gy1 = juce::jmin (gridH - 1, dev.getBottom() / gridQ + 1);
    const float invEdge = 1.0f / drainEdge;
    for (int gy = gy0; gy <= gy1; ++gy)
        for (int gx = gx0; gx <= gx1; ++gx)
        {
            const size_t i = (size_t) (gy * gridW + gx);
            const float af = gridAng[i];
            const int ai = juce::jlimit (0, nA - 1, (int) af);
            const float r = Rtab[ai] + (Rtab[ai + 1] - Rtab[ai]) * (af - (float) ai);
            gridE[i] = (r - gridDist[i]) * invEdge;
        }

    const juce::Image::BitmapData col (snapColour, juce::Image::BitmapData::readOnly);
    const juce::Image::BitmapData gry (snapGrey, juce::Image::BitmapData::readOnly);
    juce::Image::BitmapData out (fxDst, juce::Image::BitmapData::writeOnly);
    const int ps = out.pixelStride;
    const int Wd = col.width, Hd = col.height;
    const float cxDev = drainCentre.x * ppu, cyDev = (drainCentre.y + (float) panelY) * ppu;
    const float smearPx = drainSmear * ppu;
    for (int y = dev.getY(); y < dev.getBottom(); ++y)
    {
        const int gy = y / gridQ;
        const float fy = (float) (y - gy * gridQ) / (float) gridQ;
        const auto* crow = col.getLinePointer (y);
        const auto* grow = gry.getLinePointer (y);
        auto* orow = out.getLinePointer (y);
        const float* e0 = gridE.data() + (size_t) gy * (size_t) gridW;
        const float* e1 = e0 + gridW;
        int x = dev.getX();
        while (x < dev.getRight())
        {
            const int gx = x / gridQ;
            const float eL = e0[gx] + (e1[gx] - e0[gx]) * fy, eR = e0[gx + 1] + (e1[gx + 1] - e0[gx + 1]) * fy;
            const int kind = (eL <= 0.0f && eR <= 0.0f) ? 0 : (eL >= 1.0f && eR >= 1.0f) ? 2 : 1;
            if (kind != 1)
            {
                // a run of cells that are all colour (or all grey): one copy
                int gxe = gx + 1;
                while (gxe * gridQ < dev.getRight())
                {
                    const float a = e0[gxe] + (e1[gxe] - e0[gxe]) * fy, b = e0[gxe + 1] + (e1[gxe + 1] - e0[gxe + 1]) * fy;
                    if (kind == 0 ? (a > 0.0f || b > 0.0f) : (a < 1.0f || b < 1.0f)) break;
                    ++gxe;
                }
                const int xe = juce::jmin (dev.getRight(), gxe * gridQ);
                std::memcpy (orow + x * ps, (kind == 0 ? crow : grow) + x * ps, (size_t) ((xe - x) * ps));
                x = xe;
                continue;
            }
            const int xEnd = juce::jmin (dev.getRight(), (gx + 1) * gridQ);
            for (; x < xEnd; ++x)
            {
                const float e = eL + (eR - eL) * ((float) (x - gx * gridQ) / (float) gridQ);
                auto* dp = orow + x * ps;
                if (e <= 0.0f) { std::memcpy (dp, crow + x * ps, (size_t) ps); continue; }
                if (e >= 1.0f) { std::memcpy (dp, grow + x * ps, (size_t) ps); continue; }
                // in the front: colour is pulled in towards the key (sampled from further out) and drained
                const float dx = (float) x + 0.5f - cxDev, dy = (float) y + 0.5f - cyDev;
                const float d = std::sqrt (dx * dx + dy * dy);
                const float bell = 4.0f * e * (1.0f - e);                         // 0 at the front's edges, 1 mid-front
                const float k = bell * smearPx / juce::jmax (d, 1.0f);
                const int sx = juce::jlimit (0, Wd - 1, x + (int) (dx * k)), sy = juce::jlimit (0, Hd - 1, y + (int) (dy * k));
                const auto* cp = col.getPixelPointer (sx, sy);
                juce::uint8 gp[3];
                toGrey (cp, gp);
                const float m = e * e * (3.0f - 2.0f * e);
                const float rim = 1.0f - 0.22f * bell;
                dp[0] = (juce::uint8) juce::jlimit (0.0f, 255.0f, ((float) cp[0] + m * ((float) gp[0] - (float) cp[0])) * rim);
                dp[1] = (juce::uint8) juce::jlimit (0.0f, 255.0f, ((float) cp[1] + m * ((float) gp[1] - (float) cp[1])) * rim);
                dp[2] = (juce::uint8) juce::jlimit (0.0f, 255.0f, ((float) cp[2] + m * ((float) gp[2] - (float) cp[2])) * rim);
            }
        }
    }
    // the key keeps its colour
    const auto keep = (drainKeep.translated (0.0f, (float) panelY) * ppu).toNearestInt().getIntersection (dev);
    for (int y = keep.getY(); y < keep.getBottom(); ++y)
        std::memcpy (out.getLinePointer (y) + keep.getX() * ps, col.getLinePointer (y) + keep.getX() * ps, (size_t) (keep.getWidth() * ps));
}

// ============================================================================================ preset strip + rocker
void AnbuEditor::drawStripDynamic (juce::Graphics& g)
{
    const auto& T = theme();
    for (int i = 0; i < 3; ++i)
        if (stripPressed == i) drawArt (g, *stripKeyArt[(size_t) i], R (HW::stripKeySprites[i]));

    const auto d = stripDispR;
    // the display glows faintly when the pointer is over it
    if (stripHover == 3 && ! nameEditor.isVisible())
    {
        g.setColour (T.lamp.withAlpha (0.07f));
        g.fillRoundedRectangle (d.reduced (1.0f), 2.0f);
    }
    const auto amber = hex (0xffd08a), dim = hex (0xb4875a);
    const auto fName = fonts.ui (600, 14.5f), fSmall = fonts.ui (600, 9.5f);
    auto glowText = [&] (const juce::Font& f, const juce::String& t, juce::Rectangle<float> r, juce::Justification j, juce::Colour c)
    {
        juce::GlyphArrangement ga;
        ga.addFittedText (f, t, r.getX(), r.getY(), r.getWidth(), r.getHeight(), j, 1, 1.0f);
        juce::Path pth;
        ga.createPath (pth);
        shadow (g, pth, c.withAlpha (0.45f), 6.0f, {});
        g.setColour (c);
        g.fillPath (pth);
    };
    if (nameEditor.isVisible())
    {
        text (g, fSmall, "SAVE AS", d.withTrimmedLeft (10.0f).withWidth (60.0f), juce::Justification::centredLeft, dim);
        text (g, fSmall, "ENTER", d.withTrimmedRight (10.0f), juce::Justification::centredRight, dim);
        return;
    }
    const double age = juce::Time::getMillisecondCounterHiRes() - stripMessageTime;
    if (stripMessage.isNotEmpty() && age < 1600.0)
    {
        glowText (fName, stripMessage, d.reduced (40.0f, 0.0f), juce::Justification::centred, amber.withMultipliedAlpha (age < 1200.0 ? 1.0f : (float) (1.0 - (age - 1200.0) / 400.0)));
        return;
    }
    auto name = shownPreset.isEmpty() ? juce::String ("Untitled") : shownPreset;
    const auto cat = proc.presets.currentCategory();
    if (cat.isNotEmpty() || proc.presets.currentIsUser())
    {
        juce::String c = (cat.isNotEmpty() ? cat : juce::String ("My presets")).toUpperCase();
        text (g, fSmall, c, d.withTrimmedLeft (10.0f).withWidth (78.0f), juce::Justification::centredLeft, dim);
    }
    const auto nameR = d.reduced (84.0f, 0.0f);
    glowText (fName, name, nameR, juce::Justification::centred, amber);
    if (shownModified)
    {
        const float w = juce::jmin (nameR.getWidth(), juce::GlyphArrangement::getStringWidth (fName, name));
        text (g, fonts.ui (600, 13.0f), "*", { nameR.getCentreX() + 0.5f * w + 2.0f, d.getY() + 3.0f, 12.0f, 14.0f }, juce::Justification::centredLeft, dim);
    }
    // menu caret
    juce::Path c;
    const float cx = d.getRight() - 14.0f, cy = d.getCentreY();
    c.startNewSubPath (cx - 4.0f, cy - 2.0f); c.lineTo (cx, cy + 2.0f); c.lineTo (cx + 4.0f, cy - 2.0f);
    g.setColour (stripHover == 3 ? amber : dim);
    g.strokePath (c, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void AnbuEditor::drawRocker (juce::Graphics& g)
{
    if (agState) drawArt (g, *rockerOn, R (HW::rocker));
}

int AnbuEditor::stripKeyAt (juce::Point<float> sp) const
{
    if (sp.y < 0.0f || sp.y >= (float) panelY) return -1;
    for (int i = 0; i < 3; ++i)
        if (R (HW::stripKeys[i]).expanded (1.0f).contains (sp)) return i;
    if (stripDispR.contains (sp)) return 3;
    return -1;
}

bool AnbuEditor::rockerAt (juce::Point<float> p) const { return rockerHit.contains (p); }

void AnbuEditor::showPresetMenu()
{
    auto& pm = proc.presets;
    pm.rescan();
    const auto& all = pm.all();
    const int cur = pm.currentListIndex();
    menuLook->uiScale = juce::jlimit (0.85f, 1.6f, scale());

    juce::PopupMenu menu;
    menu.setLookAndFeel (menuLook.get());
    menu.addItem (1, all[0].name, true, cur == 0);
    // factory bank, one sub-menu per category (in the bank's order)
    menu.addSectionHeader ("Factory");
    juce::StringArray cats;
    for (size_t i = 1; i < all.size(); ++i) if (all[i].factory) cats.addIfNotAlreadyThere (all[i].category);
    for (auto& c : cats)
    {
        juce::PopupMenu sub;
        bool has = false;
        for (size_t i = 1; i < all.size(); ++i)
            if (all[i].factory && all[i].category == c) { sub.addItem ((int) i + 1, all[i].name, true, (int) i == cur); has = has || (int) i == cur; }
        menu.addSubMenu (c, sub, true, nullptr, has);
    }
    // the user's presets: loose ones first, then one sub-menu per folder
    menu.addSectionHeader ("My presets");
    bool anyUser = false;
    juce::StringArray ucats;
    for (size_t i = 0; i < all.size(); ++i)
        if (! all[i].factory)
        {
            anyUser = true;
            if (all[i].category.isEmpty()) menu.addItem ((int) i + 1, all[i].name, true, (int) i == cur);
            else ucats.addIfNotAlreadyThere (all[i].category);
        }
    for (auto& c : ucats)
    {
        juce::PopupMenu sub;
        bool has = false;
        for (size_t i = 0; i < all.size(); ++i)
            if (! all[i].factory && all[i].category == c) { sub.addItem ((int) i + 1, all[i].name, true, (int) i == cur); has = has || (int) i == cur; }
        menu.addSubMenu (c, sub, true, nullptr, has);
    }
    if (! anyUser) menu.addItem (-1, "None saved yet", false, false);
    menu.addSeparator();
    menu.addItem (10001, "Save as...");
    const bool curUser = cur >= 0 && ! all[(size_t) cur].factory;
    if (curUser)
    {
        menu.addItem (10002, "Save \"" + all[(size_t) cur].name + "\"", pm.isModified());
        menu.addItem (10003, "Delete \"" + all[(size_t) cur].name + "\"");
    }
    menu.addItem (10004, "Show preset folder");

    const auto area = (stripDispR * scale()).toNearestInt();
    juce::Component::SafePointer<AnbuEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withParentComponent (this).withTargetScreenArea (localAreaToGlobal (area))
                            .withMinimumWidth (area.getWidth()).withStandardItemHeight (juce::roundToInt (26.0f * menuLook->uiScale)),
                        [safe] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto& pm2 = safe->proc.presets;
        const auto& l = pm2.all();
        if (r >= 1 && r <= (int) l.size()) { pm2.load (l[(size_t) r - 1]); return; }
        if (r == 10001) { safe->beginSaveAs(); return; }
        const int c2 = pm2.currentListIndex();
        if (c2 < 0 || l[(size_t) c2].factory)
        {
            if (r == 10004) { auto d = anbu::PresetManager::userFolder(); d.createDirectory(); d.startAsProcess(); }
            return;
        }
        const auto p = l[(size_t) c2];
        if (r == 10002) { juce::String err; if (pm2.saveUser (p.name, err)) { safe->stripMessage = "Saved"; safe->stripMessageTime = juce::Time::getMillisecondCounterHiRes(); } }
        else if (r == 10003) { if (pm2.deleteUser (p)) { safe->stripMessage = "Deleted"; safe->stripMessageTime = juce::Time::getMillisecondCounterHiRes(); } }
        else if (r == 10004) { auto d = anbu::PresetManager::userFolder(); d.createDirectory(); d.startAsProcess(); }
        safe->repaintStrip (stripDispR.expanded (4.0f));
    });
}

// test hook: the menu (and one open sub-menu) drawn item by item through the menu's look-and-feel
juce::Image AnbuEditor::renderPresetMenuPreview (float sc, const juce::String& openCategory)
{
    auto& pm = proc.presets;
    const auto& all = pm.all();
    const int cur = pm.currentListIndex();
    menuLook->uiScale = sc;
    struct Item { juce::String text; bool sep = false, header = false, sub = false, ticked = false, active = true, hi = false; };
    std::vector<Item> main, sub;
    main.push_back ({ all[0].name, false, false, false, cur == 0 });
    main.push_back ({ "Factory", false, true });
    juce::StringArray cats;
    for (size_t i = 1; i < all.size(); ++i) if (all[i].factory) cats.addIfNotAlreadyThere (all[i].category);
    for (auto& c : cats)
    {
        Item it { c, false, false, true }; it.hi = c == openCategory; main.push_back (it);
        if (c == openCategory)
            for (size_t i = 1; i < all.size(); ++i)
                if (all[i].factory && all[i].category == c) sub.push_back ({ all[i].name, false, false, false, (int) i == cur });
    }
    main.push_back ({ "My presets", false, true });
    main.push_back ({ "None saved yet", false, false, false, false, false });
    main.push_back ({ {}, true });
    main.push_back ({ "Save as..." });
    main.push_back ({ "Show preset folder" });
    auto draw = [&] (juce::Graphics& g, const std::vector<Item>& items, int x0, int y0, int w)
    {
        int h = 0;
        for (auto& it : items) { int iw, ih; menuLook->getIdealPopupMenuItemSize (it.text, it.sep, 0, iw, ih); h += it.header ? juce::roundToInt (26.0f * sc) : ih; }
        const int b = menuLook->getPopupMenuBorderSize();
        g.saveState(); g.setOrigin (x0, y0);
        menuLook->drawPopupMenuBackground (g, w, h + 2 * b);
        int y = b;
        for (auto& it : items)
        {
            int iw, ih; menuLook->getIdealPopupMenuItemSize (it.text, it.sep, 0, iw, ih);
            if (it.header) { ih = juce::roundToInt (26.0f * sc); menuLook->drawPopupMenuSectionHeader (g, { 0, y, w, ih }, it.text); }
            else menuLook->drawPopupMenuItem (g, { 0, y, w, ih }, it.sep, it.active, it.hi, it.ticked, it.sub, it.text, {}, nullptr, nullptr);
            y += ih;
        }
        g.restoreState();
        return h + 2 * b;
    };
    const int w1 = juce::roundToInt (290.0f * sc), w2 = juce::roundToInt (240.0f * sc);
    juce::Image img (juce::Image::ARGB, w1 + w2 + juce::roundToInt (40.0f * sc), juce::roundToInt (620.0f * sc), true);
    juce::Graphics g (img);
    g.fillAll (theme().chassis);
    draw (g, main, juce::roundToInt (10.0f * sc), juce::roundToInt (10.0f * sc), w1);
    if (! sub.empty()) draw (g, sub, w1 + juce::roundToInt (14.0f * sc), juce::roundToInt (60.0f * sc), w2);
    return img;
}

void AnbuEditor::beginSaveAs()
{
    nameEditor.setText (proc.presets.currentIsUser() ? proc.presets.currentName() : juce::String(), juce::dontSendNotification);
    nameEditor.applyFontToAllText (fonts.ui (600, 15.0f * scale()));
    nameEditor.setVisible (true);
    nameEditor.grabKeyboardFocus();
    nameEditor.selectAll();
    repaintStrip (stripDispR.expanded (4.0f));
}

void AnbuEditor::endSaveAs (bool commit)
{
    if (! nameEditor.isVisible()) return;
    const auto name = nameEditor.getText().trim();
    nameEditor.setVisible (false);
    if (commit && name.isNotEmpty())
    {
        juce::String err;
        stripMessage = proc.presets.saveUser (name, err) ? juce::String ("Saved") : err;
        stripMessageTime = juce::Time::getMillisecondCounterHiRes();
    }
    repaintStrip (stripDispR.expanded (4.0f));
}

// ============================================================================================ interaction
bool AnbuEditor::keyOn (int i) const
{
    auto v = [this] (const char* id) { return (int) std::lround (proc.apvts.getRawParameterValue (id)->load()); };
    if (i < 4) return v (ids::os) == i;
    if (i < 6) return v (ids::phase) == i - 4;
    if (i == 6) return v (ids::noiseOn) != 0;
    return v (ids::bypass) != 0;
}

int AnbuEditor::keyAt (juce::Point<float> p) const
{
    for (int i = 0; i < 8; ++i)
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
    else if (i == 6) setChoice (ids::noiseOn, keyOn (6) ? 0 : 1);
    else setChoice (ids::bypass, keyOn (7) ? 0 : 1);
}

void AnbuEditor::toggleMeterMode (bool out)
{
    if (proc.uiMeterOut == out) return;
    proc.uiMeterOut = out;
    needlePos = meterFrac();
    needleVel = 0.0f;
    repaintDesign (faceR.expanded (4.0f));
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
    repaintDesign (readoutR.expanded (2.0f));
    if (analyserWindow != nullptr && analyserWindow->isVisible()) analyserWindow->view->repaint();
}

void AnbuEditor::mouseDown (const juce::MouseEvent& e)
{
    if (introRunning) { skipIntro(); return; }
    const auto sp = toStrip (e.position);
    if (sp.y < (float) panelY)
    {
        const int k = stripKeyAt (sp);
        if (k == 3) { showPresetMenu(); return; }
        if (k >= 0)
        {
            stripPressed = k;
            repaintStrip (R (HW::stripKeySprites[k]).expanded (2.0f));
            if (k == 0) proc.presets.step (-1);
            else if (k == 1) proc.presets.step (1);
            else beginSaveAs();
        }
        return;
    }
    const auto p = toDesign (e.position);
    if (rockerAt (p)) { setChoice (ids::autoGain, agState ? 0 : 1); return; }
    if (expandR.contains (p)) { openAnalyser(); return; }
    if (pillGR.contains (p)) { toggleMeterMode (false); return; }
    if (pillOut.contains (p)) { toggleMeterMode (true); return; }
    if (p.getDistanceFrom ({ modeBtnX, modeBtnY }) <= 19.0f) { setChoice (ids::mode, modeLimit ? 0 : 1); return; }
    if (juce::Rectangle<float> (44.0f, lampYC - 11.0f, 130.0f, 22.0f).contains (p)) { setChoice (ids::mode, 0); return; }
    if (juce::Rectangle<float> (44.0f, lampYL - 11.0f, 130.0f, 22.0f).contains (p)) { setChoice (ids::mode, 1); return; }
    const int k = keyAt (p);
    if (k >= 0) clickKey (k);
}

void AnbuEditor::mouseUp (const juce::MouseEvent&)
{
    if (stripPressed >= 0)
    {
        repaintStrip (R (HW::stripKeySprites[stripPressed]).expanded (2.0f));
        stripPressed = -1;
    }
}

void AnbuEditor::mouseExit (const juce::MouseEvent&)
{
    if (stripHover >= 0) { stripHover = -1; repaintStrip (stripDispR.expanded (4.0f)); }
}

bool AnbuEditor::clickableAt (juce::Point<float> p) const
{
    return rockerAt (p) || expandR.contains (p) || pillGR.contains (p) || pillOut.contains (p) || p.getDistanceFrom ({ modeBtnX, modeBtnY }) <= 19.0f
        || juce::Rectangle<float> (44.0f, lampYC - 11.0f, 130.0f, 22.0f).contains (p)
        || juce::Rectangle<float> (44.0f, lampYL - 11.0f, 130.0f, 22.0f).contains (p) || keyAt (p) >= 0;
}

void AnbuEditor::mouseMove (const juce::MouseEvent& e)
{
    const int h = stripKeyAt (toStrip (e.position));
    if (h != stripHover) { stripHover = h; repaintStrip (stripDispR.expanded (4.0f)); }
    setMouseCursor (h >= 0 || clickableAt (toDesign (e.position)) ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
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
float AnbuEditor::artScale (juce::Graphics& g) const
{
    if (resizingSince > 0.0) return HW::scale;                       // live resize: the 2x art, scaled while drawing
    return juce::jmax (0.25f, g.getInternalContext().getPhysicalPixelScaleFactor());
}

void AnbuEditor::timerCallback()
{
    // a resize has settled: rebuild the art at the exact pixel scale
    if (resizingSince > 0.0 && juce::Time::getMillisecondCounterHiRes() - resizingSince > 300.0)
    {
        resizingSince = 0.0;
        repaint();
    }

    // intro clock starts once the panel is actually on screen
    if (introRunning)
    {
        if (introStart < 0.0 && isShowing()) introStart = juce::Time::getMillisecondCounterHiRes();
        if (introFixed < 0.0 && introStart >= 0.0 && introT() > 2.85) introRunning = false;
        repaint();
    }

    // keys and mode (host automation or clicks)
    for (int i = 0; i < 8; ++i)
    {
        const bool on = keyOn (i);
        if (on != keyState[(size_t) i]) { keyState[(size_t) i] = on; repaintDesign (keyRect (i).expanded (8.0f)); }
    }
    // bypass (panel key or the host's bypass): drain the colour / let it flow back
    if (keyState[7] != greyTarget) startGrey (keyState[7]);
    if (greyStart >= 0.0)
    {
        greyP = greyProgress();
        if (juce::Time::getMillisecondCounterHiRes() - greyStart > greyDurationMs (greyFrom, greyTarget))
        {
            greyP = greyTarget ? 1.0f : 0.0f;
            greyStart = -1.0;
            snapValid = false;                               // the still is no longer needed
            snapColour = juce::Image(); snapGrey = juce::Image(); fxDst = juce::Image();
        }
        repaint();
    }

    // preset strip: name / modified mark (presets, host automation, undo ...), messages fading out
    {
        const auto name = proc.presets.currentName();
        const bool mod = proc.presets.isModified();
        if (name != shownPreset || mod != shownModified) { shownPreset = name; shownModified = mod; repaintStrip (stripDispR.expanded (4.0f)); }
        if (stripMessage.isNotEmpty())
        {
            repaintStrip (stripDispR.expanded (4.0f));
            if (juce::Time::getMillisecondCounterHiRes() - stripMessageTime > 1700.0) stripMessage.clear();
        }
    }
    // Auto Gain rocker and its readout
    {
        const bool ag = proc.apvts.getRawParameterValue (ids::autoGain)->load() > 0.5f;
        if (ag != agState) { agState = ag; repaintDesign (rockerR.expanded (6.0f)); repaintDesign (readoutR.expanded (2.0f)); }
        const float agDb = proc.meterAgDb.load();
        if (agState && std::abs (agDb - shownAgDb) >= 0.05f) { shownAgDb = agDb; repaintDesign (readoutR.expanded (2.0f)); }
    }
    const bool lim = (int) std::lround (proc.apvts.getRawParameterValue (ids::mode)->load()) == 1;
    if (lim != modeLimit) { modeLimit = lim; repaintDesign (modeArea); }

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
            repaintDesign (readoutR.expanded (2.0f));
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
