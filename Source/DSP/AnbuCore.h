// Anbu v1 (R4N) - DSP core. JUCE-free, sample-rate independent.
//
//   base rate : input -> Color tone EQ (linear) -> linked sidechain -> optical cell model -> conductance G[n]
//   OS rate   : Color tone EQ -> Lindell-style harmonic generator (Color) -> LA-2A audio path
//               (HF bell, shunt cell with series cap, instantaneous cell modulation, output stage)
//   after OS  : 3D side lift, output, mix, noise (done by the caller / processor)
//
// All constants come from AnbuConstants.h, generated from the fits to the reference renders.
#pragma once
#include <cmath>
#include <algorithm>
#include <vector>
#include <random>
#include <complex>
#include "AnbuConstants.h"

namespace anbu {

constexpr double kPi = 3.14159265358979323846;

// ------------------------------------------------------------------------------------------ biquad
struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    inline double run(double x) noexcept { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    void reset() noexcept { z1 = z2 = 0; }
    void set(double B0, double B1, double B2, double A0, double A1, double A2) noexcept {
        b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
    }
    static double clampF(double f0, double fs) { return std::min(f0, 0.45 * fs); }
    void peaking(double fs, double f0, double q, double gdb) {      // magnitude-matched to the analog bell (Vicanek 2016)
        f0 = std::min(f0, 0.49 * fs);
        const double G = std::pow(10.0, gdb / 20.0), A = std::sqrt(G), w0 = 2 * kPi * f0 / fs, z = 1.0 / (2 * A * q);
        const double p_a1 = -2 * std::exp(-z * w0) * (z <= 1 ? std::cos(std::sqrt(1 - z * z) * w0) : std::cosh(std::sqrt(z * z - 1) * w0));
        const double p_a2 = std::exp(-2 * z * w0);
        const double s = std::pow(std::sin(w0 / 2), 2), p0 = 1 - s, p1 = s, p2 = 4 * p0 * p1;
        const double A0 = (1 + p_a1 + p_a2) * (1 + p_a1 + p_a2), A1 = (1 - p_a1 + p_a2) * (1 - p_a1 + p_a2), A2 = -4 * p_a2;
        const double R1 = (A0 * p0 + A1 * p1 + A2 * p2) * G * G, R2 = (-A0 + A1 + 4 * (p0 - p1) * A2) * G * G;
        const double B0 = A0, B2 = (R1 - R2 * p1 - B0) / (4 * p1 * p1), B1 = R2 + B0 + 4 * (p1 - p0) * B2;
        const double sB1 = std::sqrt(std::max(B1, 0.0)), W = 0.5 * (std::sqrt(B0) + sB1);
        const double nb0 = 0.5 * (W + std::sqrt(std::max(W * W + B2, 0.0)));
        b0 = nb0; b1 = 0.5 * (std::sqrt(B0) - sB1); b2 = -B2 / (4 * nb0);
        a1 = p_a1; a2 = p_a2;
    }
    void lowshelf(double fs, double f0, double q, double gdb) {
        f0 = clampF(f0, fs);
        const double A = std::pow(10.0, gdb / 40.0), w = 2 * kPi * f0 / fs, c = std::cos(w), al = std::sin(w) / (2 * q), sa = 2 * std::sqrt(A) * al;
        set(A * ((A + 1) - (A - 1) * c + sa), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sa),
            (A + 1) + (A - 1) * c + sa, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sa);
    }
    void highshelf(double fs, double f0, double q, double gdb) {
        f0 = clampF(f0, fs);
        const double A = std::pow(10.0, gdb / 40.0), w = 2 * kPi * f0 / fs, c = std::cos(w), al = std::sin(w) / (2 * q), sa = 2 * std::sqrt(A) * al;
        set(A * ((A + 1) + (A - 1) * c + sa), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sa),
            (A + 1) - (A - 1) * c + sa, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sa);
    }
    void highpass(double fs, double f0, double q) {
        f0 = clampF(f0, fs);
        const double w = 2 * kPi * f0 / fs, al = std::sin(w) / (2 * q), c = std::cos(w);
        set((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void lowpass(double fs, double f0, double q) {
        f0 = clampF(f0, fs);
        const double w = 2 * kPi * f0 / fs, al = std::sin(w) / (2 * q), c = std::cos(w);
        set((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void section(double fs, const EqSection& s) {
        if (s.kind == 0) lowshelf(fs, s.f0, s.q, s.g);
        else if (s.kind == 1) peaking(fs, s.f0, s.q, s.g);
        else highshelf(fs, s.f0, s.q, s.g);
    }
};

struct OnePole {               // y += a (x - y)
    double a = 1, y = 0;
    void lp(double fs, double fc) { a = 1.0 - std::exp(-2 * kPi * fc / fs); }
    void tau(double fs, double t) { a = 1.0 - std::exp(-1.0 / (t * fs)); }
    inline double run(double x) noexcept { y += a * (x - y); return y; }
};

inline double interpTable(const double* tab, double pr) {        // tables at PR 0,25,50,75,100
    pr = std::clamp(pr, 0.0, 100.0);
    const double p = pr / 25.0; const int i = std::min(3, (int)p); const double f = p - i;
    return tab[i] + f * (tab[i + 1] - tab[i]);
}
inline double interpTableLog(const double* tab, double pr) {     // geometric interpolation; tab[0] may be 0
    pr = std::clamp(pr, 0.0, 100.0);
    if (pr <= 0.0) return tab[0];
    const double p = pr / 25.0; const int i = std::min(3, (int)p); const double f = p - i;
    const double a = std::max(tab[i], tab[1] * 1e-3), b = std::max(tab[i + 1], 1e-12);
    if (i == 0) return tab[1] * std::pow(f, 1.5);                 // smooth onset from PR 0 to 25
    return std::exp(std::log(a) + f * (std::log(b) - std::log(a)));
}
inline double Tsh(double a, double th, double p, double q) noexcept {
    const double r = std::max(a / th, 1e-9), rp = std::pow(r, p);
    return rp / (1 + rp) * std::pow(r, q);
}

// ------------------------------------------------------------------------------------------ settings
struct Settings {
    double peakReduction = 50.0;   // 0..100 (LA-2A knob scale)
    bool limit = false;
    double color = 0.25;           // 0..1   (0 = off, ~0.25 = Lindell default, ~0.8 = Lindell THD max, 1 = overkill)
};

// ------------------------------------------------------------------------------------------ control path (base rate)
class OptoControl {
public:
    void prepare(double fs) {
        fs_ = fs; dt_ = 1.0 / fs;
        using namespace consts;
        aA_ = 1.0 - std::exp(-dt_ / TDA); aR_ = 1.0 - std::exp(-dt_ / TDR);
        aM_ = 1.0 - std::exp(-dt_ / RIP_TM);
        for (auto* b : { &hpL_, &hpR_ }) b->highpass(fs, SC_HPF, SC_HPQ);
        for (auto* b : { &shL_, &shR_ }) b->highshelf(fs, SC_SHF, 0.7071, SC_SHG);
        rip_.lowpass(fs, RIP_FC, 0.7071);
        aLF_ = 1.0 - std::exp(-2 * kPi * CAP_LF / fs); aW_ = 1.0 - std::exp(-dt_ / 0.05);
        for (int c = 0; c < 2; ++c) for (int k = 0; k < 4; ++k) eq_[c][k].section(fs, c == 0 ? LIN_EQ_L[k] : LIN_EQ_R[k]);
        reset();
    }
    double idleConductance() const noexcept { return Gidle_; }
    void reset() {
        env_ = um_ = 0; G_[0] = G_[1] = G_[2] = 0; lfy_ = eLF_ = eAll_ = 0;
        for (auto* b : { &hpL_, &hpR_, &shL_, &shR_, &rip_ }) b->reset();
        for (auto& c : eq_) for (auto& b : c) b.reset();
    }
    void setSettings(const Settings& s, double toneAmt) {
        using namespace consts;
        const bool L = s.limit;
        k_ = L ? K_L : K_C; a_ = L ? AEXP_L : AEXP_C; invAsat2_ = 1.0 / ((L ? ASAT_L : ASAT_C) * (L ? ASAT_L : ASAT_C));
        Pamp_ = std::pow(10.0, interpTable(P_TAB, s.peakReduction) / 20.0);
        Gidle_ = interpTableLog(GIDLE_TAB, s.peakReduction);
        if (s.peakReduction <= 0.0) Pamp_ = 0.0;
        toneAmt_ = toneAmt;
    }
    // x: input at base rate (after input gain). out: conductance (signal part, with ripple), slow part, and
    // capW = how far the series cap is allowed to act (bass dominance of the programme; 1 = a pure bass tone).
    void process(const float* xL, const float* xR, int n, double* Gout, double* Gslow, double* capW) noexcept {
        using namespace consts;
        const double q[3] = { Q1, Q2, 1.0 - Q1 - Q2 }, ta[3] = { TA1, TA2, TA3 }, tr[3] = { TR1, TR2, TR3 };
        for (int i = 0; i < n; ++i) {
            double l = xL[i], r = xR[i];
            const double xm = 0.5 * (l + r);
            lfy_ += aLF_ * (xm - lfy_);
            eLF_ += aW_ * (lfy_ * lfy_ - eLF_); eAll_ += aW_ * (xm * xm - eAll_);
            capW[i] = std::max(std::pow(std::clamp(eLF_ / (eAll_ + 1e-15), 0.0, 1.0), CAP_K), 1e-4);
            // the sidechain sees the Color tone curve (the cell sits after it)
            const double tl = eq_[0][3].run(eq_[0][2].run(eq_[0][1].run(eq_[0][0].run(l)))) * std::pow(10.0, LIN_EQ_G0[0] / 20.0);
            const double trr = eq_[1][3].run(eq_[1][2].run(eq_[1][1].run(eq_[1][0].run(r)))) * std::pow(10.0, LIN_EQ_G0[1] / 20.0);
            l += toneAmt_ * (tl - l); r += toneAmt_ * (trr - r);
            const double sL = shL_.run(hpL_.run(l)), sR = shR_.run(hpR_.run(r));
            const double u = std::max(std::fabs(sL), std::fabs(sR));
            env_ += (u > env_ ? aA_ : aR_) * (u - env_);
            const double A = Pamp_ * env_, Ae = A / std::sqrt(1.0 + A * A * invAsat2_);
            double Gs = 0.0;
            if (Ae > 1e-9) Gs = k_ * std::exp(a_ * std::log(Ae)) / (1.0 + std::exp(MEXP * std::log(ATH / Ae)));
            const double speed = 1.0 + CSLOW * std::exp(-Gs / GA);
            double tot = 0.0;
            for (int s = 0; s < 3; ++s) {
                const double Gt = q[s] * Gs;
                if (Gt > G_[s]) G_[s] += std::min(1.0, dt_ / (ta[s] * speed)) * (Gt - G_[s]);
                else {
                    const double rate = (1.0 / tr[s]) * (1.0 + std::exp(BETA * std::log(G_[s] / GR + 1e-30)));
                    G_[s] -= std::min(1.0, dt_ * rate) * (G_[s] - Gt);
                }
                tot += G_[s];
            }
            const double ul = rip_.run(u);
            um_ += aM_ * (u - um_);
            double rr = 0.0;
            if (um_ > 1e-6) rr = std::clamp(RIP_RHO * (ul - um_) / um_, -0.5, 1.0);
            Gslow[i] = Gidle_ + tot;
            Gout[i] = Gidle_ + tot * (1.0 + rr);
        }
    }
private:
    double fs_ = 48000, dt_ = 1 / 48000.0, aA_ = 0, aR_ = 0, aM_ = 0;
    double k_ = 1, a_ = 1, invAsat2_ = 1, Pamp_ = 1, Gidle_ = 0, toneAmt_ = 0;
    double env_ = 0, um_ = 0, G_[3] = { 0, 0, 0 }, aLF_ = 0, aW_ = 0, lfy_ = 0, eLF_ = 0, eAll_ = 0;
    Biquad hpL_, hpR_, shL_, shR_, rip_, eq_[2][4];
};

// ------------------------------------------------------------------------------------------ audio path (oversampled rate)
class AudioPath {
public:
    void prepare(double fsOS, int osFactor) {
        fs_ = fsOS; dt_ = 1.0 / fsOS; os_ = osFactor;
        using namespace consts;
        aE_ = 1.0 - std::exp(-dt_ / 0.02);
        aEA_ = 1.0 - std::exp(-dt_ / ENV_A); aER_ = 1.0 - std::exp(-dt_ / ENV_R);
        Cn_ = 1.0 / (2 * kPi * CAP_FP);
        M_ = std::pow(10.0, MAKEUP_DB / 20.0);
        for (int c = 0; c < 2; ++c) {
            ch_[c].hf0.peaking(fs_, HF0_F, 0.7, HF0_G);
            for (int k = 0; k < 4; ++k) ch_[c].eq[k].section(fs_, c == 0 ? LIN_EQ_L[k] : LIN_EQ_R[k]);
            ch_[c].phi.lp(fs_, LN_FC * (c == 0 ? 1.0 : LN_TOL_FC));
            ch_[c].ex.tau(fs_, 0.02); ch_[c].ep.tau(fs_, 0.02);
            ch_[c].dcd.lp(fs_, 8.0); ch_[c].dco.lp(fs_, 5.0); ch_[c].dch.lp(fs_, 5.0);
            ch_[c].post.lp(fs_, std::min(LN_FPOST, 0.45 * fs_));
        }
        reset();
    }
    void reset() {
        for (auto& c : ch_) {
            c.bell.reset(); c.bell2.reset(); c.hf0.reset(); for (auto& b : c.eq) b.reset();
            c.vc = 0; c.em2 = c.em3 = c.ef = 0; c.phi.y = 0; c.ex.y = c.ep.y = 1e-12; c.dcd.y = 0; c.dco.y = 0; c.dch.y = 0; c.post.y = 0;
        }
        Gprev_ = 0; Gsprev_ = 0;
    }
    void setSettings(const Settings& s, double toneAmt, double kappa) {
        using namespace consts;
        pr_ = s.peakReduction; toneAmt_ = toneAmt; kappa_ = kappa;
        mu1_ = interpTableLog(MU1_TAB, pr_); mu2_ = interpTableLog(MU2_TAB, pr_); mu3_ = interpTable(MU3_TAB, pr_);
        bpr_ = interpTable(BPR_TAB, pr_);
        if (pr_ <= 0.0) { mu1_ = mu2_ = mu3_ = 0.0; }
        eqG0_[0] = std::pow(10.0, LIN_EQ_G0[0] / 20.0); eqG0_[1] = std::pow(10.0, LIN_EQ_G0[1] / 20.0);
    }
    // G / Gslow are given at base rate for this block (nBase samples); audio has nBase*os samples.
    // harm (optional, nOS samples): mono sum of what the nonlinear stages add (for the display only).
    void process(float* L, float* R, int nOS, const double* G, const double* Gslow, const double* capW, float* harm = nullptr) noexcept {
        using namespace consts;
        float* io[2] = { L, R };
        for (int i = 0; i < nOS; ++i) {
            const int bi = i / os_; const double fr = (double)(i % os_ + 1) / os_;
            const double Gc = Gprev_ + fr * (G[bi] - Gprev_);
            const double Gs = Gsprev_ + fr * (Gslow[bi] - Gsprev_);
            if ((i % os_) == os_ - 1) { Gprev_ = G[bi]; Gsprev_ = Gslow[bi]; }
            if ((i & 31) == 0) {
                const double B = bpr_ / (1.0 + Gs / BELL_D) + BELL_G1 * Gs / (Gs + BELL_G2) * std::exp(-Gs / BELL_G3);
                for (auto& c : ch_) { c.bell.peaking(fs_, BELL_F, BELL_Q, B); c.bell2.peaking(fs_, 20600.0, 0.99, B * BELL2_R); }
            }
            double hsum = 0.0;
            for (int c = 0; c < 2; ++c) {
                Ch& s = ch_[c];
                double x = io[c][i];
                // ---- Color: tone curve (L/R component tolerance) + harmonic generator
                const double t = s.eq[3].run(s.eq[2].run(s.eq[1].run(s.eq[0].run(x)))) * eqG0_[c];
                x += toneAmt_ * (t - x);
                double hc = 0.0;
                if (kappa_ > 0.0) { hc = kappa_ * colorResidual(s, x, c); x += hc; }
                // ---- LA-2A audio path
                const double ax = std::fabs(x);
                s.ef += (ax > s.ef ? aEA_ : aER_) * (ax - s.ef);
                const double a = std::min(ax, s.ef);
                double d1 = 0, d2 = 0, d3 = 0;
                if (a > 0.25 * std::min(T1, T2) && pr_ > 0.0) {
                    d1 = (x >= 0 ? 1.0 : -1.0) * Tsh(a, T1, P1, Q1D);
                    d2 = Tsh(a, T2, P2, Q2D);
                    d3 = Tsh(a, T1, P1, Q3D);
                }
                s.em2 += aE_ * (d2 - s.em2); s.em3 += aE_ * (d3 - s.em3);
                const double tol = (c == 0 ? 1.0 : LA_TOL_MU);
                const double dG = tol * (mu1_ * d1 + mu2_ * (d2 - s.em2) + mu3_ * (d3 - s.em3));
                const double modg = (1.0 + Gc) / std::max(1.0 + Gc + dG, 1e-3);
                const double vin = s.hf0.run(s.bell2.run(s.bell.run(x)));
                const double Rc1 = 1.0 + 1.0 / std::max(Gc, 1e-9);
                const double k = 1.0 - std::exp(-dt_ / (Cn_ / capW[bi] * Rc1));
                s.vc += k * (vin - s.vc);
                const double v = modg * (vin + Gc * s.vc) / (Gc + 1.0);
                // ---- output stage (the LA-2A output tubes/transformer at their +5 dB operating level)
                const double uo = M_ * v;
                const double ul = 1.6 * std::tanh(uo / 1.6);
                const double u2 = ul * ul;
                double yo = uo + C2 * u2 + C3 * u2 * ul + C4 * u2 * u2 + C5 * u2 * u2 * ul + C7 * u2 * u2 * u2 * ul;
                s.dco.run(yo);
                io[c][i] = (float)((yo - s.dco.y) / M_);
                // display: Color harmonics (through the cell's gain), cell modulation, output-stage curvature
                const double vlin = (vin + Gc * s.vc) / (Gc + 1.0);
                s.dch.run(yo - uo);
                hsum += hc / (1.0 + Gc) + (v - vlin) + (yo - uo - s.dch.y) / M_;   // the curvature's own DC removed
            }
            if (harm != nullptr) harm[i] = (float)(0.5 * hsum);
        }
    }
private:
    struct Ch {
        Biquad bell, bell2, hf0, eq[4];
        OnePole phi, ex, ep, dcd, dco, dch, post;
        double vc = 0, em2 = 0, em3 = 0, ef = 0;
    };
    // Lindell-style residual (flux-modulated gain + amplifier curvature), fitted at THD max (kappa = 1)
    inline double colorResidual(Ch& s, double x, int c) noexcept {
        using namespace consts;
        const double phi = s.phi.run(x);
        // envelopes; on sudden transients they are floored so the level-normalised terms stay bounded
        double ex = std::sqrt(2.0 * s.ex.run(x * x)) + 1e-6;
        double ep = std::sqrt(2.0 * s.ep.run(phi * phi)) + 1e-6;
        ex = std::max({ ex, std::fabs(x) / 1.5, std::fabs(phi) / 1.5 });
        ep = std::max(ep, std::fabs(phi) / 1.5);
        const double tol2 = (c == 0 ? 1.0 : LN_TOL_K2), tol3 = (c == 0 ? 1.0 : LN_TOL_K3);
        // bounded copies keep the polynomial branches finite when the Color stage is driven very hard
        const double xb = 1.2 * std::tanh(x / 1.2), pb = 1.2 * std::tanh(phi / 1.2);
        double d = tol3 * LN_K3A * x * (phi * phi - 0.5 * ep * ep) / (ex * ex)
                 + tol3 * LN_K3B * x * (phi * std::fabs(phi) / (ex * ep) - 0.3175 * ep / ex)
                 + tol2 * LN_K2A * x * phi / ex
                 + tol2 * LN_K2B * x * phi / ep
                 + tol2 * LN_K2C * xb * pb + tol3 * LN_K3C * xb * (pb * pb - 0.5 * std::min(ep * ep, 1.44))
                 + LN_C2 * xb * xb + LN_C3 * xb * xb * xb + LN_C5 * xb * xb * xb * xb * xb;
        s.dcd.run(d);
        d -= s.dcd.y;
        return s.post.run(d);
    }
    double fs_ = 192000, dt_ = 1 / 192000.0, aE_ = 0, aEA_ = 0, aER_ = 0, Cn_ = 0.05, M_ = 1;
    int os_ = 4;
    double pr_ = 50, toneAmt_ = 0, kappa_ = 0, mu1_ = 0, mu2_ = 0, mu3_ = 0, bpr_ = 0, eqG0_[2] = { 1, 1 };
    double Gprev_ = 0, Gsprev_ = 0;
    Ch ch_[2];
};

// ------------------------------------------------------------------------------------------ Color knob mapping
// The last quarter of the knob pushes further: up to twice the harmonics (+6 dB) and 1.5x the tone curve at 100 %.
inline double colorPush(double c) { const double u = std::clamp((c - 0.75) / 0.25, 0.0, 1.0); return u * u * (3.0 - 2.0 * u); }
inline double colorKappa(double c) { c = std::clamp(c, 0.0, 1.0); return 2.0 * c * c * c * (1.0 + colorPush(c)); }   // 0.25 -> Lindell default, ~0.8 -> THD max, 1.0 -> +12 dB over THD max
inline double colorTone(double c) { c = std::clamp(c, 0.0, 1.0); return std::min(1.0, c / 0.25) + 0.5 * colorPush(c); }

// ------------------------------------------------------------------------------------------ display curve (editor)
// Steady-state frequency response of the linear part of the path for the current settings and slow cell
// conductance Gs: Color tone curve, PR/GR-dependent HF bell, and the cell/series-cap low-frequency shelf.
// Normalised to 0 dB at 1 kHz. Evaluated with near-analog digital sections (192 kHz).
inline std::complex<double> biquadResponse(const Biquad& b, double w) {
    const std::complex<double> z1 = std::polar(1.0, -w), z2 = z1 * z1;
    return (b.b0 + b.b1 * z1 + b.b2 * z2) / (1.0 + b.a1 * z1 + b.a2 * z2);
}
inline void displayCurve(double pr, double color, double Gs, double capW, const double* freqs, double* outDb, int n) {
    using namespace consts;
    constexpr double fsd = 192000.0;
    const double tone = colorTone(color);
    Biquad eq[4];
    for (int k = 0; k < 4; ++k) eq[k].section(fsd, LIN_EQ_L[k]);
    const double g0 = std::pow(10.0, LIN_EQ_G0[0] / 20.0);
    const double bpr = interpTable(BPR_TAB, pr);
    const double B = bpr / (1.0 + Gs / BELL_D) + BELL_G1 * Gs / (Gs + BELL_G2) * std::exp(-Gs / BELL_G3);
    Biquad bell, bell2, hf0;
    bell.peaking(fsd, BELL_F, BELL_Q, B); bell2.peaking(fsd, 20600.0, 0.99, B * BELL2_R); hf0.peaking(fsd, HF0_F, 0.7, HF0_G);
    const double G = std::max(Gs, 0.0), Cn = 1.0 / (2 * kPi * CAP_FP);
    auto H = [&](double f) {
        const double w = 2 * kPi * f / fsd;
        std::complex<double> t = g0;
        for (auto& b : eq) t *= biquadResponse(b, w);
        std::complex<double> h = 1.0 + tone * (t - 1.0);
        h *= biquadResponse(bell, w) * biquadResponse(bell2, w) * biquadResponse(hf0, w);
        if (G > 1e-6) {
            const double tau = Cn / std::max(capW, 1e-4) * (1.0 + 1.0 / G);
            const std::complex<double> st(0.0, 2 * kPi * f * tau);
            h *= (1.0 + G + st) / ((1.0 + G) * (1.0 + st));
        }
        return std::abs(h);
    };
    const double ref = 20.0 * std::log10(std::max(H(1000.0), 1e-9));
    for (int i = 0; i < n; ++i) outDb[i] = 20.0 * std::log10(std::max(H(freqs[i]), 1e-9)) - ref;
}

// ------------------------------------------------------------------------------------------ 3D (post, base rate)
class SideLift {          // gentle high-frequency emphasis of the Side signal; mono content is untouched
public:
    void prepare(double fs) { sh_.highshelf(fs, consts::SIDE_F, 0.7071, consts::SIDE_G); sh_.reset(); }
    void process(float* L, float* R, int n) noexcept {
        for (int i = 0; i < n; ++i) {
            const double m = 0.5 * (L[i] + R[i]), s = sh_.run(0.5 * (L[i] - R[i]));
            L[i] = (float)(m + s); R[i] = (float)(m - s);
        }
    }
private:
    Biquad sh_;
};

// ------------------------------------------------------------------------------------------ noise
class NoiseGen {
public:
    void prepare(double fs) {
        fs_ = fs; ph_ = 0; tilt_[0].lp(fs, 6000.0); tilt_[1].lp(fs, 6000.0);
        rng_.seed(0x52344E);
    }
    // levelDb: hiss RMS in dBFS; hum sits 8 dB below the hiss (50 Hz mains and its harmonics)
    void add(float* L, float* R, int n, double levelDb) noexcept {
        const double g = std::pow(10.0, levelDb / 20.0);
        const double hum = g * std::pow(10.0, -8.0 / 20.0);
        float* io[2] = { L, R };
        for (int i = 0; i < n; ++i) {
            ph_ += 2 * kPi * 50.0 / fs_; if (ph_ > 2 * kPi) ph_ -= 2 * kPi;
            const double h = hum * (0.55 * std::sin(ph_) + 0.35 * std::sin(2 * ph_ + 0.4) + 0.25 * std::sin(3 * ph_ + 1.1));
            for (int c = 0; c < 2; ++c) {
                const double w = nd_(rng_);
                const double t = w - 0.35 * tilt_[c].run(w);        // slightly bright, tube-like hiss
                io[c][i] += (float)(g * t * 1.12 + h);
            }
        }
    }
private:
    double fs_ = 48000, ph_ = 0;
    OnePole tilt_[2];
    std::mt19937 rng_;
    std::normal_distribution<double> nd_{ 0.0, 1.0 };
};

} // namespace anbu
