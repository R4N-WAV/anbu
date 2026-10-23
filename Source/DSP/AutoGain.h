// Anbu v1 (R4N) - Auto Gain: loudness match between what comes in and what goes out. JUCE-free.
//
// Both signals are measured the way loudness meters measure programme loudness (ITU-R BS.1770: K-weighting,
// 400 ms blocks every 100 ms, absolute gate -70 LUFS and relative gate -10 LU, each signal gated on its own,
// exactly as an integrated-loudness meter does). The input is the time-aligned dry signal, so both measurements
// see exactly the same stretch of music. The gain is the difference of the two integrated loudness values.
//
// Memory: everything since the last change of a setting counts equally (an integrated measurement, up to the
// last 120 s), so once the settings stay put the gain settles and does not ride the music (it does not undo the
// compression). After a change only audio from after it is used, as soon as two full blocks exist (about half
// a second; the gain holds until then), and as the measurement grows its first stretch is left out, where the
// opto cell is still settling (the first 40 % of the audio since the change, at most 3 s of it). While settings keep moving (a sweep, automation) for more than a
// second, the last second is used, so the gain follows. The measurement runs all the time, also with Auto Gain off, so switching it on is instant.
// The processed side is measured before the Auto Gain gain is applied: no feedback loop.
#pragma once
#include <cmath>
#include <algorithm>

namespace anbu {

class AutoGain {
public:
    void prepare(double fs) {
        fs_ = fs;
        blockLen_ = std::max(1, (int) std::lround(0.1 * fs));
        // K-weighting (BS.1770 stage 1 high shelf + stage 2 high-pass), derived for any sample rate
        setShelf(fs); setHp(fs);
        reset();
    }
    void reset() {
        for (auto& f : kf_) f = {};
        accD_ = accW_ = 0.0; n_ = 0; head_ = count_ = 0; subPos_ = subCount_ = 0;
        now_ = 0.0; changeTime_ = 0.0; lastNudge_ = -10.0; seriesStart_ = 0.0;
        gTarget_ = gPrev_ = 1.0; valid_ = false;
    }
    // a setting changed: forget the old match quickly
    void nudge() noexcept
    {
        if (now_ - lastNudge_ > 0.5) seriesStart_ = now_;
        lastNudge_ = now_;
        changeTime_ = now_ + (double) blockLen_ / fs_;     // the 100 ms block in progress still holds audio from before
    }

    // dry/wet: stereo (time-aligned). Measures; returns nothing. Call gainAt() for the per-sample gain.
    // Per sample, so the block boundaries do not depend on the host's buffer size.
    inline void push(double dL, double dR, double wL, double wR) noexcept {
        const double a = kw(0, dL), b = kw(1, dR), c = kw(2, wL), d = kw(3, wR);
        accD_ += a * a + b * b; accW_ += c * c + d * d;
        if (++n_ >= blockLen_) endBlock();
    }
    // gain to apply to the current sample (linear interpolation across a block, in dB)
    inline double gain() const noexcept {
        if (std::abs (gTarget_ - gPrev_) <= 0.0) return gTarget_;   // exact: keeps Mix 0 bit-exact
        const double f = (double) n_ / (double) blockLen_;
        return gPrev_ * std::pow(gTarget_ / gPrev_, f);
    }
    double targetDb() const noexcept { return 20.0 * std::log10(gTarget_); }
    bool valid() const noexcept { return valid_; }

private:
    struct BQ { double z1 = 0, z2 = 0; };
    double b_[2][3] {}, a_[2][3] {};
    BQ kf_[4 * 2];
    inline double kw(int ch, double x) noexcept {
        for (int s = 0; s < 2; ++s) {
            BQ& z = kf_[ch * 2 + s];
            const double y = b_[s][0] * x + z.z1;
            z.z1 = b_[s][1] * x - a_[s][1] * y + z.z2;
            z.z2 = b_[s][2] * x - a_[s][2] * y;
            x = y;
        }
        return x;
    }
    void setShelf(double fs) {          // same design as the reference implementation of BS.1770 (pyloudnorm)
        const double G = 3.99984385397, Q = 0.7071752369554193, fc = 1681.9744509555319;
        const double A = std::pow(10.0, G / 40.0), w0 = 2.0 * 3.14159265358979323846 * fc / fs;
        const double al = std::sin(w0) / (2.0 * Q), c = std::cos(w0), sa = 2.0 * std::sqrt(A) * al;
        const double b0 = A * ((A + 1) + (A - 1) * c + sa), b1 = -2 * A * ((A - 1) + (A + 1) * c), b2 = A * ((A + 1) + (A - 1) * c - sa);
        const double a0 = (A + 1) - (A - 1) * c + sa, a1 = 2 * ((A - 1) - (A + 1) * c), a2 = (A + 1) - (A - 1) * c - sa;
        b_[0][0] = b0 / a0; b_[0][1] = b1 / a0; b_[0][2] = b2 / a0; a_[0][0] = 1; a_[0][1] = a1 / a0; a_[0][2] = a2 / a0;
    }
    void setHp(double fs) {
        const double Q = 0.5003270373253953, fc = 38.13547087613982;
        const double w0 = 2.0 * 3.14159265358979323846 * fc / fs, al = std::sin(w0) / (2.0 * Q), c = std::cos(w0);
        const double a0 = 1 + al;
        b_[1][0] = 1.0; b_[1][1] = -2.0; b_[1][2] = 1.0;        // unnormalised high-pass: overall gain cancels in the ratio
        a_[1][0] = 1; a_[1][1] = -2 * c / a0; a_[1][2] = (1 - al) / a0;
    }
    void endBlock() noexcept {
        const double T = (double) n_ / fs_;
        sub_[subPos_][0] = accD_ / n_; sub_[subPos_][1] = accW_ / n_;
        subPos_ = (subPos_ + 1) & 3; if (subCount_ < 4) ++subCount_;
        accD_ = accW_ = 0.0; n_ = 0;
        gPrev_ = gTarget_;
        now_ += T;
        // the new 400 ms block (75 % overlap)
        double ed = 0, ew = 0;
        for (int k = 0; k < subCount_; ++k) { ed += sub_[k][0]; ew += sub_[k][1]; }
        ed /= subCount_; ew /= subCount_;
        while (count_ > 0 && at(0).t < now_ - kWindow) { head_ = (head_ + 1) % kBlocks; --count_; }
        if (count_ == kBlocks) { head_ = (head_ + 1) % kBlocks; --count_; }
        blk_[(head_ + count_) % kBlocks] = Blk { ed, ew, now_, now_ - 0.1 * subCount_ };
        ++count_;
        // which blocks count: those wholly after the last change; while settings keep moving, the last second
        // (the opto cell keeps settling for a few seconds after a big change: that stretch is left out as the
        //  measurement grows - the first 40 % of the blocks with signal since the change, at most 3 s of them,
        //  and less again once there is plenty (450 s^2 / signal time: 1.5 s after 30 s, 0.4 s after 2 min);
        //  counted in blocks with signal, so silence in between does not move the window)
        constexpr double absE = 1.1724653045822964e-07;     // -70 LUFS
        int first = count_, active = 0;
        for (int k = 0; k < count_; ++k)
            if (at(k).tStart >= changeTime_ - 1.0e-9) { if (first == count_) first = k; if (at(k).ed > absE) ++active; }
        const int skip = std::min({ (int) (0.4 * active), 30, 4500 / std::max(active, 1) });   // shrinks again as the measurement grows
        double from = changeTime_;
        for (int k = first, seen = 0; k < count_; ++k)
            if (at(k).ed > absE) { if (seen == skip) { from = at(k).tStart; break; } ++seen; }
        const int fresh = count_ - first;
        if (fresh >= 2)
        {
            while (count_ > 0 && at(0).tStart < changeTime_ - 1.0e-9) { head_ = (head_ + 1) % kBlocks; --count_; }   // no longer needed
        }
        else if (now_ - lastNudge_ < 0.5 && now_ - seriesStart_ > 1.0) from = now_ - 1.0;
        else return;                                                                   // just changed: hold
        const double Ld = gated (0, from), Lw = gated (1, from);
        if (Ld <= 0.0 || Lw <= 0.0) return;          // nothing above the gates yet (silence): hold
        double g = std::sqrt(Ld / Lw);
        g = std::clamp(g, 0.015848931924611134, 63.09573444801933);   // +-36 dB
        gTarget_ = g; valid_ = true;
    }
    // BS.1770 integrated loudness of one signal over the weighted blocks, as a mean energy (0 = nothing passes)
    double gated (int which, double from) const noexcept {
        constexpr double absE = 1.1724653045822964e-07;        // -70 LUFS as energy: 10^((-70 + 0.691) / 10)
        double sw = 0, se = 0;
        for (int k = 0; k < count_; ++k) { const Blk& q = at(k); if (q.tStart < from - 1.0e-9) continue; const double e = which ? q.ew : q.ed; if (e > absE) { sw += 1.0; se += e; } }
        if (sw <= 0.0) return 0.0;
        const double rel = 0.1 * se / sw;                     // relative gate: -10 LU
        sw = se = 0;
        for (int k = 0; k < count_; ++k) { const Blk& q = at(k); if (q.tStart < from - 1.0e-9) continue; const double e = which ? q.ew : q.ed; if (e > absE && e > rel) { sw += 1.0; se += e; } }
        return sw > 0.0 ? se / sw : 0.0;
    }
    struct Blk { double ed, ew, t, tStart; };          // energies, end and start time of the block (s)
    static constexpr int kBlocks = 1200;                     // 120 s of blocks
    static constexpr double kWindow = 120.0;
    Blk blk_[kBlocks] {};
    int head_ = 0, count_ = 0;
    double now_ = 0, changeTime_ = 0, lastNudge_ = -10, seriesStart_ = 0;
    Blk& at(int k) noexcept { return blk_[(head_ + k) % kBlocks]; }
    const Blk& at(int k) const noexcept { return blk_[(head_ + k) % kBlocks]; }
    double sub_[4][2] {};
    int subPos_ = 0, subCount_ = 0;
    double fs_ = 48000.0;
    int blockLen_ = 4800, n_ = 0;
    double accD_ = 0, accW_ = 0;
    double gTarget_ = 1.0, gPrev_ = 1.0;
    bool valid_ = false;
};

} // namespace anbu
