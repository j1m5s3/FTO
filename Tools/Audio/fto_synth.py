#!/usr/bin/env python3
"""
FTO's sound effects, synthesised from code (100% in-house, no samples). Pure Python: numpy + scipy, no Unreal.

  python Tools/Audio/fto_synth.py --out Art/Source/Audio [--only SW_A,SW_B] [--list] [--jobs N]

Writes 44.1 kHz 16-bit mono WAVs. Every sound is seeded from its own name, so a rebuild of one sound (--only) gives
the same file as a full rebuild. Tools/Unreal/make_audio.py imports the WAVs into the editor and reads SOUNDS below
for the names and which ones loop.

The recipes lean on a small toolkit: layered transients + body + tail, modal (resonant partial) banks for metal,
glass and wood, shaped noise, tanh saturation for weight, and short room / outdoor slap-back tails. Loops are made
seamless either by building them from whole periods and filtering circularly, or by crossfading the wrap.
"""
import argparse
import os
import sys
import time
import wave
import zlib

try:
    import numpy as np
    from scipy import signal
except ImportError:  # the registry below still imports, so Unreal can read names and loop flags without numpy
    np = None
    signal = None

RATE = 44100
TAU = 2.0 * 3.141592653589793

# name -> (builder(rng, variant) -> float array, loops, variant). variant is 0 for an unnumbered original.
SOUNDS = {}


def register(name, fn, loops=False, variants=0, base=True):
    if base:
        SOUNDS[name] = (fn, loops, 0)
    for v in range(1, variants + 1):
        SOUNDS[f"{name}_{v:02d}"] = (fn, loops, v)


def sound(name, loops=False, variants=0, base=True):
    def deco(fn):
        register(name, fn, loops, variants, base)
        return fn
    return deco


# ------------------------------------------------------------------------------------------
# Toolkit
# ------------------------------------------------------------------------------------------
def N(sec):
    return max(1, int(round(sec * RATE)))


def T(sec):
    return np.arange(N(sec)) / RATE


def zeros(sec):
    return np.zeros(N(sec))


def white(r, sec):
    return r.standard_normal(N(sec))


def colored(r, sec, slope):
    """Noise with a 1/f^slope power spectrum (1 = pink, 2 = brown), unit RMS."""
    n = N(sec)
    spec = np.fft.rfft(r.standard_normal(n))
    f = np.fft.rfftfreq(n, 1.0 / RATE)
    f[0] = f[1]
    x = np.fft.irfft(spec / f ** (slope / 2.0), n)
    return x / (np.std(x) + 1e-12)


def _fc(f):
    return float(np.clip(f, 10.0, RATE * 0.45))


def lp(x, f, order=2):
    return signal.sosfilt(signal.butter(order, _fc(f), "low", fs=RATE, output="sos"), x)


def hp(x, f, order=2):
    return signal.sosfilt(signal.butter(order, _fc(f), "high", fs=RATE, output="sos"), x)


def bp(x, lo, hi, order=2):
    lo, hi = _fc(lo), _fc(hi)
    if hi <= lo * 1.05:
        hi = lo * 1.05
    return signal.sosfilt(signal.butter(order, [lo, hi], "band", fs=RATE, output="sos"), x)


def peq(x, f, q, db):
    """RBJ peaking EQ, for speaker / body resonances."""
    a = 10 ** (db / 40.0)
    w = TAU * _fc(f) / RATE
    alpha = np.sin(w) / (2 * q)
    b = [1 + alpha * a, -2 * np.cos(w), 1 - alpha * a]
    den = [1 + alpha / a, -2 * np.cos(w), 1 - alpha / a]
    return signal.lfilter(b, den, x)


def dec(sec, tau, attack=0.0003):
    """Exponential decay envelope with a short linear attack."""
    t = T(sec)
    e = np.exp(-t / tau)
    a = N(attack)
    e[:a] *= np.linspace(0.0, 1.0, a)
    return e


def fade(x, fin=0.001, fout=0.01):
    x = x.copy()
    a, b = min(len(x), N(fin)), min(len(x), N(fout))
    x[:a] *= np.linspace(0.0, 1.0, a)
    x[len(x) - b:] *= np.linspace(1.0, 0.0, b)
    return x


def place(dst, src, at, g=1.0):
    i = int(round(at * RATE))
    if i >= len(dst) or i + len(src) <= 0:
        return dst
    s0 = max(0, -i)
    i = max(0, i)
    m = min(len(dst) - i, len(src) - s0)
    dst[i:i + m] += g * src[s0:s0 + m]
    return dst


def mix(*xs):
    out = np.zeros(max(len(x) for x in xs))
    for x in xs:
        out[:len(x)] += x
    return out


def cat(*xs):
    return np.concatenate(xs)


def sat(x, drive):
    return np.tanh(drive * x) / np.tanh(drive)


def norm(x, peak):
    return x * (peak / (np.max(np.abs(x)) + 1e-12))


def phase_of(freq):
    """freq: array of Hz per sample. Integrated phase, so sweeps stay click-free."""
    return TAU * np.cumsum(freq) / RATE


def sweep(f0, f1, sec, k=25.0):
    """Exponential glide from f0 towards f1 (rate k)."""
    t = T(sec)
    return f1 + (f0 - f1) * np.exp(-t * k)


def modal(r, freqs, taus, amps, sec):
    """A bank of decaying sines with random phases: the ring of a struck object."""
    t = T(sec)[None, :]
    f = np.asarray(freqs, float)[:, None]
    keep = f[:, 0] < RATE * 0.45
    ph = r.uniform(0, TAU, (len(freqs), 1))
    out = np.asarray(amps, float)[:, None] * np.sin(TAU * f * t + ph) * np.exp(-t / np.asarray(taus, float)[:, None])
    return out[keep].sum(axis=0)


def metal_bank(r, lo, hi, count, tau_lo, tau_hi):
    """Random inharmonic partials: something sheet- or bar-like made of metal."""
    f = np.exp(r.uniform(np.log(lo), np.log(hi), count))
    taus = np.exp(r.uniform(np.log(tau_lo), np.log(tau_hi), count)) * (lo / f) ** 0.25
    amps = r.uniform(0.3, 1.0, count) * (lo / f) ** 0.3
    return f, taus, amps


def strike(r, bank, sec, exc_tau=0.0008, exc_lp=12000):
    """Excite a modal bank with a short noise click rather than a perfect impulse."""
    f, taus, amps = bank
    ring = modal(r, f, taus, amps, sec)
    exc = lp(white(r, exc_tau * 6) * dec(exc_tau * 6, exc_tau, 0.0001), exc_lp)
    return signal.fftconvolve(ring, exc)[:len(ring)]


def thump(r, f0, f1, tau, sec=None, noise_lp=400.0, noise_amt=0.6, drive=2.0):
    """Low body of an impact: a pitch-dropping sine plus a burst of low noise, saturated."""
    sec = sec or tau * 7
    s = np.sin(phase_of(sweep(f0, f1, sec, 1.0 / (tau * 0.8)))) * dec(sec, tau, 0.0008)
    n = lp(white(r, sec), noise_lp) * dec(sec, tau * 0.6, 0.0005)
    n /= np.max(np.abs(n)) + 1e-12
    return sat(s + noise_amt * n, drive)


def burst(r, lo, hi, tau, sec=None, attack=0.0002):
    sec = sec or tau * 7
    x = bp(white(r, sec), lo, hi) * dec(sec, tau, attack)
    return x / (np.max(np.abs(x)) + 1e-12)


def room(x, r, rt60=0.4, wet=0.2, damp=6000.0, predelay=0.004, early=8, spread=0.025):
    """Early reflections + diffuse tail, convolved. Extends the sound by the tail."""
    n = N(rt60 * 1.1 + predelay + spread)
    ir = np.zeros(n)
    for k in range(early):
        i = N(predelay + r.uniform(0, spread))
        ir[i] += r.uniform(0.3, 0.8) * (0.85 ** k) * r.choice([-1, 1])
    t0 = N(predelay + spread * 0.5)
    tail = r.standard_normal(n - t0) * np.exp(-6.9 * np.arange(n - t0) / RATE / rt60)
    ir[t0:] += 0.25 * lp(tail, damp)
    ir /= np.sqrt(np.sum(ir ** 2)) + 1e-12
    w = signal.fftconvolve(x, ir)
    out = np.zeros(len(w))
    out[:len(x)] += x
    return out + wet * w * (np.max(np.abs(x)) / (np.max(np.abs(w)) + 1e-12))


def outdoor(x, r, slaps=((0.09, 0.35), (0.19, 0.22), (0.33, 0.14)), slap_lp=2500.0, rt60=1.2, wet=0.18,
            tail_lp=1500.0):
    """Street acoustics: a few discrete slap-backs off buildings plus a long, dark diffuse tail."""
    n = N(max(d for d, _ in slaps) + rt60 * 1.1)
    ir = np.zeros(n)
    ir[0] = 1.0
    for d, g in slaps:
        ir[N(d * r.uniform(0.9, 1.1))] += g
    t0 = N(slaps[0][0] * 0.6)
    tail = r.standard_normal(n - t0) * np.exp(-6.9 * np.arange(n - t0) / RATE / rt60)
    tail[:N(0.03)] *= np.linspace(0, 1, N(0.03))
    ir[t0:] += wet * 0.06 * lp(tail, tail_lp)
    dry = np.zeros(len(x) + n - 1)
    dry[:len(x)] = x
    wetsig = signal.fftconvolve(x, ir) - dry
    return dry + lp(wetsig, slap_lp)


def trim(x, floor=2e-4, tail=0.01):
    """Cut trailing near-silence and fade out whatever is left."""
    peak = np.max(np.abs(x)) + 1e-12
    idx = np.nonzero(np.abs(x) > floor * peak)[0]
    end = min(len(x), (idx[-1] + N(tail)) if len(idx) else len(x))
    return fade(x[:end], 0.0, min(tail, end / RATE))


def xfade_loop(x, xf):
    """Seamless loop: blend the last xf seconds into the start (equal power), return len(x) - xf."""
    f = N(xf)
    body = x[:len(x) - f].copy()
    w = np.linspace(0.0, 1.0, f)
    body[:f] = x[:f] * np.sqrt(w) + x[len(x) - f:] * np.sqrt(1.0 - w)
    return body


def circular(x, fn):
    """Run a filter chain over a periodic signal without a seam: process 3 copies, keep the middle."""
    n = len(x)
    y = fn(np.tile(x, 3))
    return y[n:2 * n]


def periodic_wander(r, sec, count=6, slope=1.0):
    """Smooth random wobble in [-1, 1] that repeats exactly every sec seconds."""
    t = T(sec)
    out = np.zeros(len(t))
    for k in range(1, count + 1):
        out += r.uniform(0.3, 1.0) / k ** slope * np.sin(TAU * k * t / sec + r.uniform(0, TAU))
    return out / (np.max(np.abs(out)) + 1e-12)


def periodic_phase(freq):
    """Phase for a frequency curve over a loop, nudged so it closes on a whole number of cycles."""
    cycles = np.sum(freq) / RATE
    freq = freq * (max(1, round(cycles)) / cycles)
    return TAU * (np.cumsum(freq) - freq[0]) / RATE


def grain_times(r, count, t0, t1, mode="exp", scale=None):
    if mode == "exp":
        t = t0 + r.exponential(scale or (t1 - t0) / 3.0, count)
        return t[t < t1]
    return r.uniform(t0, t1, count)


def jitter(r, v, amount):
    """Per-variant multiplier near 1."""
    return 1.0 + amount * r.uniform(-1.0, 1.0) if v else 1.0


# ------------------------------------------------------------------------------------------
# Shared recipes
# ------------------------------------------------------------------------------------------
def shard(r, amp=1.0, lo=2500.0, hi=11000.0):
    count = r.integers(3, 6)
    f = np.exp(r.uniform(np.log(lo), np.log(hi), count))
    ring = modal(r, f, r.uniform(0.004, 0.035, count), r.uniform(0.3, 1.0, count), 0.12)
    click = hp(white(r, 0.003), 3000) * dec(0.003, 0.0005)
    return amp * mix(ring, 0.5 * click)


def shatter(r, sec=1.4, count=70, fall=35):
    """Glass: a crack, a spray of shards, then pieces tinkling onto the ground."""
    x = zeros(sec)
    place(x, 1.2 * burst(r, 2000, 12000, 0.004), 0.0)
    place(x, 0.5 * np.sin(phase_of(np.full(N(0.1), r.uniform(160, 240)))) * dec(0.1, 0.018), 0.0)
    for t in grain_times(r, count, 0.002, sec * 0.5, "exp", 0.08):
        place(x, shard(r, r.uniform(0.3, 1.0) * np.exp(-t * 3)), t)
    for t in grain_times(r, fall, 0.2, sec - 0.1, "uni"):
        place(x, shard(r, r.uniform(0.1, 0.4) * np.exp(-(t - 0.2) * 1.8), 3000, 9000), t)
    hiss = hp(white(r, 0.35), 4000) * dec(0.35, 0.06, 0.001)
    return place(x, 0.25 * hiss, 0.0)


def crunch(r, sec, density, lo=180.0, hi=4200.0, tau=(0.04, 0.35), grain_tau=0.0015):
    """Crumpling metal: a dense stream of friction grains driving a sheet-metal modal bank."""
    exc = np.zeros(N(sec))
    t = np.cumsum(r.exponential(1.0 / density, int(density * sec * 2)))
    t = t[t < sec]
    for ti in t:
        g = white(r, grain_tau * 5) * dec(grain_tau * 5, grain_tau, 0.0001)
        place(exc, g, ti, r.uniform(0.2, 1.0) * np.exp(-ti / (sec * 0.35)))
    rough = np.abs(lp(white(r, sec), 60))
    exc += 0.3 * bp(white(r, sec), 500, 5000) * rough / (np.max(rough) + 1e-12) * np.exp(-T(sec) / (sec * 0.3))
    ring = modal(r, *metal_bank(r, lo, hi, 22, tau[0], tau[1]), min(sec, 0.6))
    return signal.fftconvolve(exc, ring)[:N(sec)]


def debris(r, sec, count, t0, lo=600.0, hi=5000.0, metal=True):
    x = zeros(sec)
    for t in grain_times(r, count, t0, sec - 0.05, "exp", (sec - t0) / 2.5):
        a = r.uniform(0.2, 1.0) * np.exp(-(t - t0) * 2.0)
        if metal and r.random() < 0.5:
            place(x, a * strike(r, metal_bank(r, lo * 2, hi * 1.5, 4, 0.01, 0.06), 0.1), t)
        else:
            place(x, a * burst(r, lo, hi, r.uniform(0.002, 0.01)), t)
    return x


def gunshot(r, v, crack, boom_f, boom_tau, blast_tau, blast_lp, body, tail, weight, mech):
    j = lambda a: jitter(r, v, a)
    x = zeros(0.4)
    # Supersonic crack: an N-wave a fraction of a millisecond long.
    if crack > 0:
        nw = np.linspace(1.0, -1.0, N(0.0005 * j(0.2)))
        place(x, crack * hp(cat(nw, np.zeros(N(0.004))), 1200), 0.0)
    # Muzzle blast: a violent broadband burst with a body band and a heavy low boom under it.
    blast = lp(white(r, 0.3), blast_lp * j(0.1)) * dec(0.3, blast_tau * j(0.15), 0.0002)
    band = bp(white(r, 0.3), body[0], body[1]) * dec(0.3, blast_tau * 2.5 * j(0.15), 0.0003)
    boom = np.sin(phase_of(sweep(boom_f[0] * j(0.08), boom_f[1], 0.4, 1.0 / (boom_tau * 0.7)))) * dec(0.4, boom_tau * j(0.1), 0.0006)
    blast = norm(blast, 1.0) + 0.7 * norm(band, 1.0)
    place(x, 0.9 * blast, 0.0004)
    place(x, weight * boom, 0.0004)
    x = sat(x, 4.0)
    # The action cycling (slide / bolt / pump), quiet under the report.
    if mech:
        place(x, mech * strike(r, metal_bank(r, 1200, 7000, 8, 0.01, 0.05), 0.12), r.uniform(0.02, 0.05))
    x = outdoor(x, r, slaps=((0.085 * j(0.2), 0.45), (0.17 * j(0.2), 0.3), (0.29 * j(0.2), 0.2), (0.46 * j(0.2), 0.12)),
                slap_lp=3000, rt60=tail * j(0.1), wet=1.0, tail_lp=1200)
    return trim(x)


def car_crash(r, v, sec=2.2, heavy=1.0, glass=0.7, metal=1.0):
    j = lambda a: jitter(r, v, a)
    x = zeros(sec)
    place(x, 1.4 * heavy * thump(r, 95 * j(0.1), 32, 0.11 * heavy * j(0.15), noise_lp=350, drive=2.5), 0.0)
    place(x, 0.8 * burst(r, 300, 3000, 0.02), 0.0)
    place(x, 0.9 * metal * norm(crunch(r, 0.8 * j(0.15), 900 * heavy), 1.0), 0.004)
    if glass:
        place(x, glass * norm(shatter(r, 1.2, 50, 25), 1.0), r.uniform(0.01, 0.05))
    place(x, 0.5 * norm(debris(r, sec - 0.2, int(22 * heavy), 0.25), 1.0), 0.2)
    scrape = bp(white(r, 0.9), 900, 4000) * np.abs(lp(white(r, 0.9), 40)) * dec(0.9, 0.25, 0.05)
    place(x, 0.35 * norm(scrape, 1.0), 0.15)
    x = outdoor(x, r, slaps=((0.1, 0.3), (0.22, 0.18)), slap_lp=2000, rt60=1.1, wet=0.8)
    return trim(x)


def stick_slip(r, sec, rate_lo, rate_hi, bank, swell=True):
    """Creaks and groans: friction releasing in pulses at a wandering rate, ringing a resonant body."""
    rate = rate_lo + (rate_hi - rate_lo) * (0.5 + 0.5 * np.interp(T(sec), np.linspace(0, sec, 8), r.uniform(-1, 1, 8)))
    ph = np.cumsum(rate) / RATE
    idx = np.nonzero(np.diff(np.floor(ph)) > 0)[0]
    exc = np.zeros(N(sec))
    exc[idx] = r.uniform(0.4, 1.0, len(idx))
    if swell:
        exc *= np.sin(np.pi * np.linspace(0, 1, len(exc))) ** 0.7
    ring = modal(r, *bank, 0.5)
    return signal.fftconvolve(exc, ring)[:len(exc)] + 0.05 * bp(white(r, sec), 800, 3000) * (exc.cumsum() > 0)


def radio_band(x, drive=2.5):
    return sat(peq(bp(x, 320, 3200, 3), 1800, 1.2, 4), drive)


def static(r, sec, crackle=0.02):
    s = white(r, sec)
    pops = (r.random(N(sec)) < crackle / 10) * r.uniform(-4, 4, N(sec))
    return s + lp(pops, 5000)


def brass(r, freq, sec, bright=1.0, vib=5.2, vib_depth=0.007, scoop=0.03, attack=0.035, release=0.09,
          formant=1200.0, harmonics=28):
    """Additive brass: harmonics open up as the note gets louder, a lip scoop and a delayed vibrato."""
    t = T(sec)
    a, rl = N(attack), N(release)
    env = np.ones(len(t)) * (1.0 - 0.15 * t / max(sec, 1e-3))
    env[:a] *= np.linspace(0, 1, a) ** 0.6
    env[len(t) - rl:] *= np.linspace(1, 0, rl) ** 1.5
    vib_on = np.clip((t - 0.15) / 0.25, 0, 1)
    f = freq * (1 - scoop * np.exp(-t / 0.025)) * (1 + vib_depth * vib_on * np.sin(TAU * vib * t))
    f *= 1 + 0.002 * lp(r.standard_normal(len(t)), 8) / 0.05
    ph = phase_of(f)
    k = np.arange(1, harmonics + 1)[:, None]
    k = k[k[:, 0] * freq < 14000]
    amps = np.exp(-(k - 1) / (0.6 + 6.0 * bright * env[None, :]))
    out = (amps * np.sin(k * ph[None, :])).sum(axis=0) * env
    buzz = bp(white(r, sec), freq * 2, min(freq * 6, 16000)) * dec(sec, 0.025, 0.002)
    out = peq(out + 0.08 * buzz * np.max(np.abs(out)), formant, 0.9, 5)
    return out / (np.max(np.abs(out)) + 1e-12)


def bell(r, freq, sec, tau=0.9, ratios=(0.5, 1.0, 1.19, 1.5, 2.0, 2.5, 2.66, 3.0, 4.07), hum=0.4):
    amps = [hum, 1.0, 0.5, 0.35, 0.4, 0.2, 0.15, 0.12, 0.08]
    taus = [tau * 1.6, tau, tau * 0.8, tau * 0.6, tau * 0.5, tau * 0.35, tau * 0.3, tau * 0.25, tau * 0.18]
    f = [freq * q * (1 + r.uniform(-0.002, 0.002)) for q in ratios]
    ring = modal(r, f, taus[:len(f)], amps[:len(f)], sec)
    click = hp(white(r, 0.004), 2000) * dec(0.004, 0.0006)
    return fade(mix(ring, 0.3 * click), 0.0005, 0.05)


def wood_bank(r, base, count=6):
    f = base * np.array([1.0, 2.3, 3.9, 5.2, 7.1, 9.6])[:count] * r.uniform(0.95, 1.05, count)
    return f, r.uniform(0.02, 0.07, count) * np.linspace(1.0, 0.4, count), np.linspace(1.0, 0.3, count)


# ------------------------------------------------------------------------------------------
# Weapons
# ------------------------------------------------------------------------------------------
@sound("SW_ShotPistol", variants=4)
def shot_pistol(r, v):
    x = gunshot(r, v, crack=0.35, boom_f=(170, 60), boom_tau=0.035, blast_tau=0.006, blast_lp=7000,
                body=(400, 2200), tail=1.2, weight=0.7, mech=0.12)
    return norm(x, 0.92)


@sound("SW_ShotRifle", variants=4)
def shot_rifle(r, v):
    x = gunshot(r, v, crack=1.0, boom_f=(150, 50), boom_tau=0.045, blast_tau=0.008, blast_lp=9000,
                body=(500, 3000), tail=1.8, weight=0.8, mech=0.1)
    return norm(x, 0.95)


@sound("SW_ShotShotgun", variants=4)
def shot_shotgun(r, v):
    x = gunshot(r, v, crack=0.15, boom_f=(120, 38), boom_tau=0.07, blast_tau=0.014, blast_lp=5000,
                body=(200, 1500), tail=1.9, weight=1.1, mech=0.0)
    # Pump: shuck back, shuck forward.
    for at, lo in ((0.42, 900), (0.55, 1400)):
        place(x, 0.1 * mix(burst(r, lo, 5000, 0.02), 0.8 * strike(r, metal_bank(r, lo, 6000, 6, 0.01, 0.04), 0.08)), at)
    return norm(x, 0.95)


@sound("SW_DryFire")
def dry_fire(r, v):
    x = mix(strike(r, metal_bank(r, 1800, 8000, 8, 0.004, 0.03), 0.12), 0.3 * thump(r, 400, 200, 0.004))
    return norm(trim(room(x, r, 0.2, 0.1)), 0.35)


@sound("SW_Reload")
def reload(r, v):
    x = zeros(1.0)
    clack = lambda lo, hi, tau: mix(strike(r, metal_bank(r, lo, hi, 8, tau * 0.3, tau), 0.15), 0.4 * thump(r, 300, 120, 0.006))
    place(x, 0.4 * clack(2500, 8000, 0.02), 0.0)                       # mag release
    place(x, 0.25 * burst(r, 1500, 5000, 0.03, 0.12, 0.02), 0.03)        # mag slides out
    place(x, 0.2 * burst(r, 1500, 5000, 0.02, 0.1, 0.02), 0.3)           # new mag slides in
    place(x, 1.0 * clack(900, 5000, 0.04), 0.36)                        # seated
    place(x, 0.35 * burst(r, 1200, 5000, 0.03, 0.1, 0.02), 0.6)          # slide back
    place(x, 0.5 * clack(1500, 6000, 0.02), 0.66)
    place(x, 0.9 * clack(1100, 6000, 0.035), 0.74)                      # slams home
    return norm(trim(room(x, r, 0.25, 0.12)), 0.55)


@sound("SW_Ricochet", variants=3)
def ricochet(r, v):
    sec = 0.6
    x = zeros(sec)
    place(x, 0.8 * burst(r, 2500, 12000, 0.0012), 0.0)
    place(x, 0.4 * strike(r, metal_bank(r, 2000, 9000, 6, 0.004, 0.02), 0.08), 0.0)
    t = T(0.5)
    f0 = r.uniform(2600, 3800)
    f = f0 * (0.45 + 0.55 * np.exp(-t * r.uniform(2.5, 4.5))) * (1 + 0.04 * np.sin(TAU * r.uniform(70, 130) * t))
    whine = (np.sin(phase_of(f)) + 0.3 * np.sin(2 * phase_of(f))) * np.minimum(1, t / 0.012) * np.exp(-t / 0.18)
    whine += 0.5 * bp(white(r, 0.5), f0 * 0.5, f0 * 1.3) * np.minimum(1, t / 0.012) * np.exp(-t / 0.12)
    place(x, 0.6 * whine, 0.006)
    return norm(trim(outdoor(x, r, slaps=((0.1, 0.25), (0.21, 0.12)), rt60=0.8, wet=0.5)), 0.5)


@sound("SW_Taser")
def taser(r, v):
    sec = 0.7
    x = zeros(sec)
    for t in np.arange(0.0, sec - 0.03, 1.0 / 21.0) + r.uniform(-0.004, 0.004, len(np.arange(0.0, sec - 0.03, 1.0 / 21.0))):
        spark = mix(hp(white(r, 0.02), 2500) * dec(0.02, 0.0006), 0.6 * burst(r, 3500, 7000, 0.003, 0.02))
        place(x, r.uniform(0.6, 1.0) * spark, max(0.0, t))
        place(x, 0.3 * thump(r, 300, 150, 0.003), max(0.0, t))
    hiss = hp(white(r, sec), 5000) * (0.6 + 0.4 * np.sin(TAU * 21 * T(sec)))
    x += 0.04 * hiss
    return norm(fade(trim(room(x, r, 0.3, 0.1)), 0.002, 0.03), 0.6)


# ------------------------------------------------------------------------------------------
# Police gear, radio and UI
# ------------------------------------------------------------------------------------------
@sound("SW_SirenLoop", loops=True)
def siren_loop(r, v):
    # US electronic wail: 650-1450 Hz over a 4 s rise and fall, square-ish drive into a horn speaker.
    sec = 4.0
    t = T(sec)
    shape = 0.5 - 0.5 * np.cos(TAU * t / sec)
    shape = shape ** 0.85
    f = 650.0 + 800.0 * shape
    ph = periodic_phase(f)
    # Band-limited square-ish drive (odd harmonics, faded out before Nyquist so the sweep doesn't alias).
    x = np.zeros(len(t))
    for k in range(1, 16, 2):
        x += np.clip((16000.0 - k * f) / 3000.0, 0.0, 1.0) * np.sin(k * ph) / k ** 1.1
    chain = lambda y: lp(sat(peq(peq(bp(y, 450, 5000, 2), 1100, 1.5, 5), 2600, 2.0, 4), 1.6), 7000, 4)
    x = circular(x, chain)
    echo = np.roll(lp(x, 2500), N(0.11)) * 0.18 + np.roll(lp(x, 1500), N(0.26)) * 0.1
    return norm(x + echo, 0.6)


@sound("SW_Horn")
def horn(r, v):
    # Two disc horns (~415 and ~500 Hz), bright and buzzy with a diaphragm resonance.
    sec = 0.5
    t = T(sec)
    x = np.zeros(len(t))
    for f0 in (415.0, 502.0):
        f = f0 * (1 - 0.05 * np.exp(-t / 0.02)) * (1 + 0.003 * lp(white(r, sec), 30) / 0.02)
        ph = phase_of(f)
        k = np.arange(1, 30)[:, None]
        x += (np.sin(k * ph[None, :]) / k ** 0.75).sum(axis=0)
    x = sat(peq(bp(x, 300, 7000), 2400, 2.0, 7), 2.0)
    x *= np.minimum(1, t / 0.012) * np.minimum(1, (sec - t) / 0.03)
    return norm(trim(outdoor(x, r, slaps=((0.12, 0.22), (0.25, 0.12)), rt60=0.7, wet=0.4)), 0.7)


@sound("SW_Whistle")
def whistle(r, v):
    sec = 0.65
    t = T(sec)
    trill = 30 + 8 * lp(white(r, sec), 5) / 0.02
    pea = 0.5 + 0.5 * np.sin(phase_of(trill))
    f = 3050 * (1 - 0.04 * np.exp(-t / 0.03)) * (1 + 0.035 * pea)
    tone = np.sin(phase_of(f)) + 0.08 * np.sin(2 * phase_of(f))
    breath = bp(white(r, sec), 2500, 3800) * 0.4 + hp(white(r, sec), 6000) * 0.08
    env = np.minimum(1, t / 0.02) * np.minimum(1, (sec - t) / 0.06)
    x = (tone * (0.35 + 0.65 * pea) + breath) * env
    return norm(trim(outdoor(x, r, slaps=((0.1, 0.2),), rt60=0.6, wet=0.3)), 0.6)


@sound("SW_Cuffs")
def cuffs(r, v):
    x = zeros(1.0)
    tick = lambda: mix(strike(r, metal_bank(r, 2500, 10000, 6, 0.003, 0.015), 0.05), 0.2 * burst(r, 3000, 9000, 0.0008))
    at = 0.0
    for ratchet in range(2):
        gap = 0.03
        for k in range(7):
            place(x, r.uniform(0.6, 1.0) * tick(), at)
            at += gap
            gap *= 0.9
        at += 0.15
    clunk = mix(strike(r, metal_bank(r, 700, 4000, 10, 0.02, 0.09), 0.2), 0.6 * thump(r, 250, 120, 0.01))
    place(x, 1.3 * clunk, at - 0.05)
    return norm(trim(room(x, r, 0.25, 0.12)), 0.55)


@sound("SW_Radio")
def radio(r, v):
    x = zeros(0.55)
    beep = np.sin(phase_of(np.full(N(0.09), 1000.0))) * np.minimum(1, T(0.09) / 0.003)
    place(x, 0.5 * fade(beep, 0.002, 0.004), 0.0)
    st = static(r, 0.36, 0.05) * dec(0.36, 0.25, 0.002)
    place(x, 0.6 * st / (np.max(np.abs(st)) + 1e-12), 0.11)
    return norm(radio_band(x), 0.45)


@sound("SW_SquelchOpen")
def squelch_open(r, v):
    x = zeros(0.2)
    place(x, 0.6 * strike(r, metal_bank(r, 1500, 6000, 5, 0.002, 0.01), 0.03), 0.0)   # PTT click
    st = static(r, 0.13, 0.08)
    place(x, 0.5 * fade(st / np.max(np.abs(st)), 0.0005, 0.02), 0.012)
    return norm(radio_band(x, 2.0), 0.4)


@sound("SW_SquelchClose")
def squelch_close(r, v):
    x = zeros(0.3)
    beep = np.sin(phase_of(np.full(N(0.06), 1250.0)))
    place(x, 0.4 * fade(beep, 0.002, 0.005), 0.0)
    tail = static(r, 0.14, 0.1) * np.linspace(0.6, 1.0, N(0.14))          # the "kssht" tail, cut hard
    place(x, 0.7 * fade(tail / np.max(np.abs(tail)), 0.001, 0.004), 0.07)
    return norm(radio_band(x, 2.0), 0.4)


@sound("SW_Callout")
def callout(r, v):
    x = zeros(0.45)
    for at, f, d in ((0.0, 853.0, 0.08), (0.1, 1280.0, 0.12)):
        place(x, 0.5 * fade(np.sin(phase_of(np.full(N(d), f))), 0.003, 0.008), at)
    st = static(r, 0.25, 0.06) * dec(0.25, 0.1, 0.003)
    place(x, 0.25 * st / np.max(np.abs(st)), 0.2)
    return norm(radio_band(x), 0.5)


@sound("SW_Alarm")
def alarm(r, v):
    # Chaos spiking: three pulses of a two-tone electronic alarm through a small horn speaker.
    x = zeros(0.85)
    for i in range(3):
        f = 780.0 if i % 2 == 0 else 620.0
        p = np.sign(np.sin(phase_of(np.full(N(0.2), f)))) + 0.4 * np.sin(phase_of(np.full(N(0.2), f * 2.01)))
        place(x, fade(p, 0.004, 0.015), i * 0.27)
    x = sat(peq(bp(x, 400, 4500), 1600, 1.5, 6), 1.5)
    return norm(trim(room(x, r, 0.6, 0.18)), 0.5)


@sound("SW_Click")
def click(r, v):
    x = mix(strike(r, metal_bank(r, 2000, 7000, 4, 0.002, 0.008), 0.03), 0.4 * burst(r, 1500, 6000, 0.0008, 0.01))
    return norm(fade(x, 0.0002, 0.005), 0.35)


@sound("SW_Chime")
def chime(r, v):
    # Cash register "cha-ching": the drawer mechanism, then the bell.
    x = zeros(1.3)
    place(x, 0.5 * strike(r, metal_bank(r, 800, 6000, 10, 0.01, 0.05), 0.15), 0.0)
    place(x, 0.4 * burst(r, 1500, 7000, 0.02, 0.1), 0.02)
    place(x, bell(r, 2100.0, 1.1, 0.7), 0.13)
    return norm(trim(room(x, r, 0.5, 0.15)), 0.55)


def brass_line(r, notes, voices=2, **kw):
    """notes: (freq, start, length). A small section: a second player slightly late and detuned."""
    end = max(s + d for _, s, d in notes) + 0.2
    x = zeros(end)
    for voice in range(voices):
        cents = 1.0 if voice == 0 else 2 ** (r.uniform(3, 7) / 1200)
        lag = 0.0 if voice == 0 else r.uniform(0.006, 0.014)
        for f, s, d in notes:
            place(x, (1.0 if voice == 0 else 0.7) * brass(r, f * cents, d, **kw), s + lag)
    return x


def note(name):
    names = {"C": -9, "C#": -8, "D": -7, "D#": -6, "E": -5, "F": -4, "F#": -3, "G": -2, "G#": -1, "A": 0, "A#": 1, "B": 2}
    pitch, octave = name[:-1], int(name[-1])
    return 440.0 * 2.0 ** ((names[pitch] + (octave - 4) * 12) / 12.0)


@sound("SW_Fanfare")
def fanfare(r, v):
    x = brass_line(r, [(note("C5"), 0.0, 0.16), (note("E5"), 0.15, 0.16), (note("G5"), 0.3, 0.16),
                       (note("C6"), 0.45, 0.8)], bright=1.1, formant=1400)
    x = mix(x, 0.5 * brass_line(r, [(note("C4"), 0.45, 0.8), (note("G4"), 0.45, 0.8)], voices=1, bright=0.7, formant=700))
    return norm(trim(room(x, r, 1.3, 0.3, 5000)), 0.6)


@sound("SW_Bugle")
def bugle(r, v):
    x = brass_line(r, [(note("G4"), 0.0, 0.2), (note("C5"), 0.23, 0.6)], voices=1, bright=0.9, vib_depth=0.004, formant=1100)
    return norm(trim(room(x, r, 1.4, 0.3, 4500)), 0.55)


@sound("SW_Womp")
def womp(r, v):
    # Sad trombone with a plunger mute: wah, wah, wah, waaaah.
    notes = [(note("G3"), 0.0, 0.34), (note("F#3"), 0.38, 0.34), (note("F3"), 0.76, 0.34), (note("E3"), 1.14, 1.2)]
    x = zeros(2.5)
    for i, (f, s, d) in enumerate(notes):
        tone = brass(r, f, d, bright=0.9, vib=6.0 if i == 3 else 0.0, vib_depth=0.02 if i == 3 else 0.0, formant=550)
        open_ = np.clip(np.sin(np.pi * np.linspace(0, 1, len(tone))) * 1.5, 0, 1)   # plunger opening then closing
        muted = lp(tone, 450)
        place(x, muted * (1 - open_) * 1.6 + tone * open_, s)
    return norm(trim(room(x, r, 1.0, 0.25, 4000)), 0.6)


@sound("SW_Fail")
def fail(r, v):
    # A low tuba "bwaa-bwomp".
    x = brass_line(r, [(note("A2"), 0.0, 0.22), (note("E2"), 0.25, 0.55)], voices=1, bright=0.6, scoop=0.05, formant=350)
    return norm(trim(room(x, r, 0.8, 0.2, 3000)), 0.5)


@sound("SW_Bonk")
def bonk(r, v):
    # Someone knocked flying: a meaty body thump with a hollow coconut-like knock on top.
    x = zeros(0.6)
    place(x, thump(r, 120, 55, 0.05, noise_lp=500, drive=2.5), 0.0)
    knock = strike(r, wood_bank(r, r.uniform(330, 380), 4), 0.3)
    f = np.exp(-T(0.3) * 3)
    place(x, 0.8 * knock * f / (np.max(np.abs(knock)) + 1e-12), 0.002)
    place(x, 0.3 * burst(r, 800, 4000, 0.004), 0.0)
    return norm(trim(room(x, r, 0.35, 0.15)), 0.65)


# ------------------------------------------------------------------------------------------
# Hand to hand
# ------------------------------------------------------------------------------------------
@sound("SW_Punch", variants=4, base=False)
def punch(r, v):
    j = lambda a: jitter(r, v, a)
    x = zeros(0.4)
    place(x, thump(r, 110 * j(0.15), 50, 0.04 * j(0.2), noise_lp=450, drive=3.0), 0.0)
    place(x, 0.7 * j(0.2) * burst(r, 900, 3500 * j(0.2), 0.005), 0.0)       # skin / jacket slap
    place(x, 0.3 * burst(r, 2500, 7000, 0.015), 0.002)                     # cloth
    return norm(trim(room(x, r, 0.25, 0.1)), 0.62)


@sound("SW_Kick", variants=3, base=False)
def kick(r, v):
    j = lambda a: jitter(r, v, a)
    x = zeros(0.5)
    place(x, 1.2 * thump(r, 85 * j(0.15), 38, 0.06 * j(0.2), noise_lp=380, drive=3.5), 0.0)
    place(x, 0.6 * burst(r, 500, 2500 * j(0.2), 0.008), 0.0)                # shoe leather
    place(x, 0.3 * burst(r, 2000, 6000, 0.02), 0.004)
    return norm(trim(room(x, r, 0.3, 0.1)), 0.68)


@sound("SW_BodyFall", variants=3, base=False)
def body_fall(r, v):
    x = zeros(1.0)
    at = 0.0
    for i in range(r.integers(3, 6)):
        a = 1.0 if i == 0 else r.uniform(0.25, 0.6)
        place(x, a * thump(r, r.uniform(70, 110) * (1 + 0.3 * i), 35, r.uniform(0.04, 0.08), noise_lp=500, drive=2.5), at)
        place(x, a * 0.3 * burst(r, 1500, 6000, 0.01), at)
        at += r.uniform(0.03, 0.09)
    grit = zeros(0.4)
    for t in grain_times(r, 25, 0.0, 0.35, "uni"):
        place(grit, r.uniform(0.2, 1.0) * burst(r, 3000, 10000, 0.0008, 0.005), t)
    place(x, 0.2 * grit, 0.0)
    for t in r.uniform(0.02, 0.2, 2):                                        # belt / gear rattle
        place(x, 0.12 * strike(r, metal_bank(r, 2000, 8000, 4, 0.005, 0.03), 0.06), t)
    return norm(trim(room(x, r, 0.3, 0.12)), 0.75)


@sound("SW_Whoosh", variants=3, base=False)
def whoosh(r, v):
    sec = r.uniform(0.22, 0.35)
    t = T(sec)
    n = white(r, sec)
    peak = r.uniform(0.45, 0.6)
    pos = t / sec
    env = np.exp(-((pos - peak) / 0.22) ** 2)
    centre = 500 * 2 ** (2.2 * np.exp(-((pos - peak) / 0.3) ** 2))
    bands = [250, 500, 1000, 2000, 4000]
    x = np.zeros(len(t))
    for b in bands:
        w = np.exp(-(np.log2(centre / b)) ** 2 / 0.6)
        x += bp(n, b / 1.5, b * 1.5) * w
    return norm(fade(x * env, 0.005, 0.02), 0.45)


@sound("SW_Scuffle")
def scuffle(r, v):
    # A dust-up: cloth, muffled blows, shoes scrabbling, gear rattling. No voices.
    sec = 1.5
    x = zeros(sec)
    rough = np.abs(lp(white(r, sec), 14))
    cloth = bp(white(r, sec), 1500, 6000) * rough / np.max(rough)
    x += 0.35 * cloth
    for t in np.sort(r.uniform(0.0, sec - 0.2, 8)):
        place(x, r.uniform(0.5, 1.0) * thump(r, r.uniform(80, 140), 45, r.uniform(0.03, 0.06), noise_lp=600, drive=2.5), t)
    for t in r.uniform(0.0, sec - 0.3, 3):
        place(x, 0.4 * scuff_sound(r, r.uniform(0.12, 0.2)), t)
    for t in r.uniform(0.0, sec - 0.1, 4):
        place(x, 0.12 * strike(r, metal_bank(r, 2000, 8000, 4, 0.005, 0.03), 0.06), t)
    return norm(trim(room(x, r, 0.35, 0.12)), 0.65)


# ------------------------------------------------------------------------------------------
# Footsteps
# ------------------------------------------------------------------------------------------
SURFACES = {
    #            thud(f0, tau, amt)   click(lo, hi, tau, amt)       ring                     grit  room(rt60, wet)
    "Concrete": ((120, 0.018, 0.8), (1800, 9000, 0.003, 0.8), None, 0.25, (0.25, 0.08)),
    "Wood":     ((110, 0.025, 0.9), (700, 4000, 0.004, 0.45), ("wood", 160), 0.05, (0.4, 0.12)),
    "Tile":     ((140, 0.014, 0.6), (2500, 11000, 0.002, 1.0), ("tile", 2800), 0.05, (0.55, 0.16)),
    "Carpet":   ((90, 0.03, 0.9), (300, 1500, 0.006, 0.15), None, 0.0, (0.2, 0.04)),
    "Metal":    ((130, 0.02, 0.6), (1500, 8000, 0.002, 0.6), ("metal", 320), 0.05, (0.5, 0.12)),
    "Grass":    ((80, 0.025, 0.5), (1500, 6000, 0.01, 0.2), None, 1.0, (0.1, 0.02)),
}


def surface_hit(r, surface, amp, heavy):
    (tf, ttau, tamt), (clo, chi, ctau, camt), ring, grit, _ = SURFACES[surface]
    x = zeros(0.4)
    place(x, tamt * heavy * thump(r, tf * r.uniform(0.9, 1.1), tf * 0.5, ttau * heavy ** 0.5, noise_lp=350, drive=2.0), 0.0)
    place(x, camt * burst(r, clo, chi, ctau * r.uniform(0.8, 1.2)), 0.0)
    if ring:
        kind, base = ring
        if kind == "wood":
            bank = wood_bank(r, base * r.uniform(0.9, 1.1))
        elif kind == "tile":
            bank = metal_bank(r, base, base * 3, 5, 0.004, 0.015)
        else:
            bank = metal_bank(r, base, base * 8, 12, 0.03, 0.16)
        rn = strike(r, bank, 0.35)
        place(x, (0.5 if kind != "metal" else 0.8) * rn / (np.max(np.abs(rn)) + 1e-12), 0.0)
    if grit:
        for t in grain_times(r, int(40 * grit), 0.0, 0.09, "exp", 0.025):
            lo = 2500 if surface == "Grass" else 3000
            place(x, grit * r.uniform(0.1, 0.5) * burst(r, lo, 11000, 0.0006, 0.004), t)
        if surface == "Grass":
            place(x, 0.3 * burst(r, 2000, 8000, 0.04, 0.15, 0.01), 0.0)
    return amp * x


def footstep(r, surface, heavy=1.0, gap=(0.035, 0.07)):
    x = zeros(0.5)
    place(x, surface_hit(r, surface, 1.0, heavy), 0.0)                              # heel
    place(x, surface_hit(r, surface, r.uniform(0.35, 0.6), heavy * 0.8), r.uniform(*gap))  # toe
    rt, wet = SURFACES[surface][4]
    return trim(room(x, r, rt, wet))


def scuff_sound(r, sec):
    t = T(sec)
    friction = np.abs(lp(white(r, sec), 90)) ** 1.5
    x = bp(white(r, sec), 1500, 7000) * friction / (np.max(friction) + 1e-12)
    env = np.sin(np.pi * t / sec) ** 0.8
    return x * env


def make_step(surface, run):
    def build(r, v):
        x = footstep(r, surface, 1.5 if run else 1.0, (0.02, 0.04) if run else (0.035, 0.07))
        if run and r.random() < 0.5:
            place(x, 0.15 * scuff_sound(r, 0.08), 0.01)
        return norm(x, (0.42 if run else 0.32) * (0.8 if surface in ("Carpet", "Grass") else 1.0))
    return build


for _surface in SURFACES:
    register(f"SW_Step_{_surface}", make_step(_surface, False), variants=6, base=False)
    register(f"SW_StepRun_{_surface}", make_step(_surface, True), variants=4, base=False)


@sound("SW_Land_Concrete", variants=2, base=False)
def land_concrete(r, v):
    x = zeros(0.6)
    place(x, surface_hit(r, "Concrete", 1.0, 2.2), 0.0)
    place(x, surface_hit(r, "Concrete", 0.8, 2.0), r.uniform(0.008, 0.025))
    place(x, 0.3 * burst(r, 1500, 6000, 0.03), 0.0)
    for t in r.uniform(0.01, 0.12, 3):
        place(x, 0.1 * strike(r, metal_bank(r, 2000, 8000, 4, 0.005, 0.03), 0.06), t)
    return norm(trim(room(x, r, 0.25, 0.08)), 0.55)


@sound("SW_Scuff", variants=3, base=False)
def scuff(r, v):
    sec = r.uniform(0.15, 0.35)
    x = scuff_sound(r, sec)
    if v == 3:                                           # one rubber-sole squeak
        t = T(sec * 0.6)
        f = 1900 * (1 + 0.05 * np.sin(TAU * 18 * t))
        place(x, 0.4 * np.sin(phase_of(f)) * np.sin(np.pi * t / t[-1]) * np.max(np.abs(x)), sec * 0.2)
    for t in grain_times(r, 15, 0.0, sec, "uni"):
        place(x, 0.2 * np.max(np.abs(x)) * burst(r, 3000, 10000, 0.0006, 0.004), t)
    return norm(fade(room(x, r, 0.2, 0.05), 0.003, 0.02), 0.25)


# ------------------------------------------------------------------------------------------
# Vehicles
# ------------------------------------------------------------------------------------------
@sound("SW_EngineLoop", loops=True)
def engine_loop(r, v):
    # Cross-plane V8 at ~800 rpm: 8 uneven exhaust pulses per 0.15 s cycle through exhaust resonances.
    # 10 cycles = 1.5 s, built as one period and filtered circularly so the wrap is seamless.
    # The game pitches it 0.7x-2.2x with speed.
    cycle, cycles = 0.15, 10
    sec = cycle * cycles
    exc = np.zeros(N(sec))
    amps = np.array([1.0, 0.7, 0.9, 0.62, 1.0, 0.75, 0.85, 0.66])
    offs = np.array([0.0, 0.11, 0.24, 0.37, 0.5, 0.61, 0.76, 0.87]) * cycle
    pulse_len = N(0.012)
    for c in range(cycles):
        for a, o in zip(amps, offs):
            i = N(c * cycle + o + r.uniform(-0.0006, 0.0006)) % len(exc)
            p = r.standard_normal(pulse_len) * np.exp(-np.arange(pulse_len) / N(0.003))
            p[0] += 3.0
            idx = (i + np.arange(pulse_len)) % len(exc)
            exc[idx] += a * r.uniform(0.9, 1.1) * p

    def chain(y):
        body = peq(peq(peq(lp(y, 1800, 2), 95, 2.0, 12), 230, 3.0, 8), 560, 4.0, 5)
        return lp(sat(body / (np.max(np.abs(body)) + 1e-12), 2.2), 2500, 4)

    x = circular(exc, chain)
    intake = circular(bp(exc, 900, 3500), lambda y: y)
    x += 0.06 * intake / (np.max(np.abs(intake)) + 1e-12)
    return norm(x, 0.6)


@sound("SW_TireSkidLoop", loops=True)
def tire_skid_loop(r, v):
    sec = 3.0
    f = 950 * (1 + 0.1 * periodic_wander(r, sec, 12))
    ph = periodic_phase(f)
    ph2 = periodic_phase(f * 1.37)
    stick = 0.55 + 0.45 * periodic_wander(r, sec, 40, 0.5)
    squeal = (np.sin(ph) + 0.5 * np.sin(2 * ph) + 0.25 * np.sin(3 * ph) + 0.3 * np.sin(ph2)) * stick
    squeal = circular(squeal, lambda y: sat(peq(y, 1900, 2.0, 4), 1.5))
    noise_part = xfade_loop(bp(white(r, sec + 0.5), 1000, 6000) * 0.25 + lp(white(r, sec + 0.5), 150) * 0.6, 0.5)
    return norm(squeal + noise_part[:len(squeal)] * 0.8, 0.4)


@sound("SW_Crash", variants=4)
def crash(r, v):
    return norm(car_crash(r, v, 2.4, 1.0, 0.8), 0.9)


@sound("SW_CarImpactHeavy", variants=3, base=False)
def car_impact_heavy(r, v):
    return norm(car_crash(r, v, 1.8, 1.2, 0.35 if v == 2 else 0.0, 1.2), 0.9)


@sound("SW_CarImpactLight", variants=3, base=False)
def car_impact_light(r, v):
    x = zeros(0.8)
    place(x, 0.9 * thump(r, 130 * jitter(r, v, 0.1), 70, 0.035, noise_lp=500), 0.0)
    place(x, 0.7 * burst(r, 1000, 5000, 0.008), 0.0)                       # plastic bumper crack
    place(x, 0.5 * strike(r, metal_bank(r, 400, 2500, 8, 0.02, 0.06), 0.3), 0.0)
    place(x, 0.4 * norm(debris(r, 0.6, 8, 0.03, 1200, 6000), 1.0), 0.02)
    return norm(trim(outdoor(x, r, slaps=((0.1, 0.2),), rt60=0.6, wet=0.4)), 0.6)


@sound("SW_MetalCreak", variants=2, base=False)
def metal_creak(r, v):
    sec = r.uniform(1.6, 2.4)
    x = stick_slip(r, sec, 12, 60, metal_bank(r, 250, 3000, 14, 0.08, 0.35))
    return norm(trim(room(x, r, 0.8, 0.2)), 0.5)


@sound("SW_Clang", variants=3)
def clang(r, v):
    # Street furniture knocked flying: a hollow steel pole ringing, partials beating in pairs.
    base = r.uniform(260, 420)
    ratios = np.array([1.0, 2.76, 5.40, 8.93, 13.34])
    f = np.concatenate([base * ratios, base * ratios * 1.006])
    taus = np.tile([0.9, 0.5, 0.35, 0.22, 0.15], 2)
    amps = np.tile([1.0, 0.7, 0.5, 0.35, 0.2], 2)
    x = strike(r, (f, taus, amps), 1.6)
    x = mix(x / np.max(np.abs(x)), 0.5 * thump(r, 200, 90, 0.015), 0.3 * burst(r, 2000, 9000, 0.002))
    return norm(trim(outdoor(x, r, slaps=((0.1, 0.25), (0.22, 0.12)), rt60=0.9, wet=0.4)), 0.65)


@sound("SW_Glass", variants=3)
def glass(r, v):
    return norm(trim(room(shatter(r), r, 0.3, 0.1)), 0.65)


# ------------------------------------------------------------------------------------------
# Destruction
# ------------------------------------------------------------------------------------------
def chunk(r, amp, big):
    lo = r.uniform(250, 1500) / (1 + big)
    g = burst(r, lo, lo * r.uniform(3, 6), r.uniform(0.005, 0.03))
    if big:
        g = mix(g, 0.8 * thump(r, r.uniform(80, 140), 45, 0.03, noise_lp=400))
    return amp * g


def rubble_stream(r, sec, count, t0=0.0, scale=None, big_frac=0.15):
    x = zeros(sec)
    for t in grain_times(r, count, t0, sec - 0.05, "exp", scale or (sec - t0) / 3):
        big = r.random() < big_frac
        place(x, chunk(r, r.uniform(0.2, 1.0) * np.exp(-(t - t0) * 1.2), big), t)
    trickle = zeros(sec)
    for t in grain_times(r, count // 2, t0 + 0.1, sec - 0.05, "uni"):
        place(trickle, r.uniform(0.1, 0.4) * burst(r, 2500, 10000, 0.0008, 0.005), t)
    return x + 0.4 * trickle * np.exp(-T(sec) * 0.8)


@sound("SW_Rubble", variants=4, base=False)
def rubble(r, v):
    sec = r.uniform(1.2, 1.8)
    x = rubble_stream(r, sec, int(r.uniform(60, 110)))
    place(x, 0.5 * thump(r, 70, 35, 0.12, noise_lp=200), 0.0)
    return norm(trim(outdoor(x, r, slaps=((0.1, 0.2), (0.2, 0.1)), rt60=1.0, wet=0.4)), 0.7)


@sound("SW_WallBreak", variants=3, base=False)
def wall_break(r, v):
    sec = 3.6
    x = zeros(sec)
    place(x, 1.6 * thump(r, 60 * jitter(r, v, 0.1), 26, 0.25, noise_lp=250, noise_amt=1.0, drive=3.5), 0.0)
    place(x, 1.2 * burst(r, 1500, 12000, 0.004), 0.0)                        # the crack
    place(x, 0.8 * burst(r, 200, 2500, 0.08, 0.4), 0.0)
    place(x, 1.0 * norm(rubble_stream(r, 0.8, 220, 0.0, 0.12, 0.25), 1.0), 0.005)
    place(x, 0.25 * norm(stick_slip(r, 0.9, 25, 90, metal_bank(r, 300, 2500, 10, 0.05, 0.2), False), 1.0), 0.1)   # rebar
    place(x, 0.6 * norm(rubble_stream(r, sec - 0.5, 120, 0.0, 0.8, 0.1), 1.0), 0.4)
    dust = hp(white(r, sec), 3500) * dec(sec, 0.7, 0.05)
    x += 0.05 * dust
    return norm(trim(outdoor(x, r, slaps=((0.1, 0.35), (0.22, 0.22), (0.38, 0.12)), rt60=1.6, wet=0.5)), 0.95)


@sound("SW_Explosion", variants=3, base=False)
def explosion(r, v):
    """A car going up: a crack and a huge low thump, a whoomp of flame, then bits of car and glass raining down."""
    sec = 5.0
    t = T(sec)
    x = zeros(sec)
    place(x, 2.2 * thump(r, 55 * jitter(r, v, 0.12), 18, 0.45, noise_lp=180, noise_amt=1.2, drive=4.0), 0.0)
    place(x, 1.4 * burst(r, 900, 14000, 0.006), 0.0)                                             # the crack
    place(x, 0.9 * burst(r, 120, 1800, 0.12, 0.6), 0.002)
    whoomp = lp(colored(r, sec, 1.0), 700, 2) * np.clip(t / 0.08, 0, 1) * np.exp(-t / 0.9)       # the fireball
    x += 0.5 * whoomp / np.max(np.abs(whoomp))
    rumble = lp(colored(r, sec, 2.0), 90, 4) * np.exp(-t / 1.6)
    x += 0.5 * rumble / np.max(np.abs(rumble))
    place(x, 0.35 * norm(debris(r, 2.6, int(r.uniform(30, 45)), 0.35, metal=True), 1.0), 0.0)     # bits of car
    place(x, 0.2 * norm(shatter(r, 1.2, 40, 30), 1.0), 0.05)                                    # its windows
    return norm(trim(outdoor(x, r, slaps=((0.11, 0.45), (0.24, 0.3), (0.42, 0.18)), rt60=2.2, wet=0.5)), 0.97)


@sound("SW_Collapse", variants=1, base=False)
def collapse(r, v):
    sec = 9.0
    t = T(sec)
    x = zeros(sec)
    swell = np.clip(t / 2.0, 0, 1) ** 1.5 * np.exp(-np.clip(t - 3.5, 0, None) / 1.8)
    rumble = lp(colored(r, sec, 2.0), 140, 4) * swell
    x += 0.7 * rumble / np.max(np.abs(rumble))
    for at in np.sort(np.concatenate([[0.0, 0.8], r.uniform(1.2, 4.5, 6)])):
        a = 1.0 if at < 0.1 else r.uniform(0.4, 0.8)
        place(x, a * thump(r, r.uniform(45, 70), 25, r.uniform(0.15, 0.3), noise_lp=220, noise_amt=1.0, drive=3.0), at)
        place(x, a * 0.6 * burst(r, 1200, 9000, 0.005), at)
    cascade = zeros(sec)
    for tt in grain_times(r, 1600, 0.3, sec - 0.2, "uni"):
        w = np.interp(tt, t, swell) + 0.05
        if r.random() < w:
            place(cascade, chunk(r, r.uniform(0.2, 1.0), r.random() < 0.1), tt)
    x += 0.8 * norm(cascade, 1.0)
    for at in (1.0, 2.8):
        place(x, 0.3 * norm(stick_slip(r, 2.0, 8, 35, metal_bank(r, 120, 1500, 14, 0.1, 0.5)), 1.0), at)
    place(x, 0.35 * norm(rubble_stream(r, 3.0, 80, 0.0, 1.0, 0.05), 1.0), 5.8)                  # settling
    dust = hp(white(r, sec), 3000) * swell
    x += 0.04 * dust
    return norm(trim(outdoor(x, r, slaps=((0.12, 0.3), (0.3, 0.2)), rt60=2.5, wet=0.6, tail_lp=900)), 0.95)


# ------------------------------------------------------------------------------------------
# Buildings
# ------------------------------------------------------------------------------------------
@sound("SW_DoorOpen")
def door_open(r, v):
    x = zeros(1.0)
    latch = lambda: strike(r, metal_bank(r, 1500, 7000, 8, 0.008, 0.04), 0.1)
    place(x, 0.7 * latch(), 0.0)
    place(x, 0.5 * latch(), 0.07)
    place(x, 0.25 * norm(stick_slip(r, 0.45, 25, 55, metal_bank(r, 500, 2500, 6, 0.02, 0.08)), 1.0), 0.12)  # hinge
    place(x, 0.2 * burst(r, 200, 1200, 0.2, 0.5, 0.1), 0.1)                                              # air moving
    return norm(trim(room(x, r, 0.5, 0.18)), 0.5)


@sound("SW_DoorClose")
def door_close(r, v):
    x = zeros(0.8)
    place(x, 0.2 * burst(r, 200, 1200, 0.06, 0.2, 0.05), 0.0)
    slam = mix(strike(r, wood_bank(r, r.uniform(85, 100)), 0.4), thump(r, 110, 55, 0.04, noise_lp=400))
    place(x, slam / np.max(np.abs(slam)), 0.12)
    place(x, 0.5 * strike(r, metal_bank(r, 1500, 7000, 8, 0.008, 0.04), 0.1), 0.125)                     # latch
    return norm(trim(room(x, r, 0.55, 0.22)), 0.6)


@sound("SW_ElevatorDing")
def elevator_ding(r, v):
    x = bell(r, 1320.0, 1.8, 0.9, ratios=(1.0, 2.0, 2.76, 4.0, 5.4), hum=1.0)
    return norm(trim(room(x, r, 0.8, 0.2)), 0.45)


@sound("SW_ElevatorLoop", loops=True)
def elevator_loop(r, v):
    # Motor hum on 60 Hz mains harmonics (whole cycles in 4 s), ventilation and a soft cable rumble.
    sec = 4.0
    t = T(sec)
    hum = sum(a * np.sin(TAU * f * t + r.uniform(0, TAU)) for f, a in ((60, 0.3), (120, 1.0), (180, 0.35), (240, 0.25), (360, 0.12)))
    hum *= 1 + 0.05 * periodic_wander(r, sec, 4)
    whine = 0.05 * np.sin(TAU * 1175 * t)
    air = xfade_loop(lp(colored(r, sec + 1.0, 1.0), 1800) * 0.5 + lp(white(r, sec + 1.0), 80) * 0.8, 1.0)
    return norm(hum * 0.25 + whine + air[:len(hum)] * 0.3, 0.3)


@sound("SW_CityAmbienceLoop", loops=True)
def city_ambience_loop(r, v):
    # Distant traffic bed with cars passing, an occasional far-off horn and truck air brake. 24 s loop.
    xf = 2.0
    sec = 24.0 + xf
    t = T(sec)
    bed = lp(colored(r, sec, 1.6), 700) * 0.6 + lp(colored(r, sec, 1.0), 2500) * 0.12
    bed *= 1 + 0.25 * np.interp(t, np.linspace(0, sec, 14), r.uniform(-1, 1, 14))
    x = bed
    for at in np.sort(r.uniform(0.0, sec - 5.0, 9)):
        d = r.uniform(3.0, 6.0)
        tt = T(d)
        bell_env = np.exp(-((tt - d / 2) / (d / 5)) ** 2)
        car = bp(white(r, d), 300, 1500) * bell_env
        f0 = r.uniform(70, 120)
        doppler = f0 * (1 + 0.04 * np.tanh(-(tt - d / 2) * 3))
        car += 0.5 * lp(np.sin(phase_of(doppler)) + 0.5 * np.sin(2 * phase_of(doppler)), 400) * bell_env
        place(x, r.uniform(0.3, 0.8) * car, at)
    for at in r.uniform(1.0, sec - 3.0, 2):
        h = lp(horn(r, 0), 1500)
        place(x, 0.06 * h / np.max(np.abs(h)), at)
    at = r.uniform(3.0, sec - 3.0)
    place(x, 0.1 * hp(white(r, 0.8), 2500) * dec(0.8, 0.35, 0.05), at)
    x = room(x, r, 1.8, 0.35, 3000)[:len(t)]
    return norm(xfade_loop(x, xf), 0.35)


@sound("SW_GushLoop", loops=True)
def gush_loop(r, v):
    # A burst hydrant: rushing water with bubbles and splashes. 3 s, crossfaded wrap.
    sec = 3.6
    x = lp(colored(r, sec, 0.8), 3500) * 0.5 + lp(white(r, sec), 500) * 0.4
    for tt in grain_times(r, 260, 0.0, sec - 0.05, "uni"):
        d = 0.03
        f = r.uniform(350, 1400) * (1 + 1.5 * T(d) / d)
        place(x, r.uniform(0.2, 0.8) * np.sin(phase_of(f)) * dec(d, 0.008, 0.001), tt)
    splash = hp(white(r, sec), 2000) * np.abs(lp(white(r, sec), 6))
    x += 0.8 * splash / np.max(np.abs(splash))
    return norm(xfade_loop(x, 0.6), 0.45)


@sound("SW_FireLoop", loops=True)
def fire_loop(r, v):
    # A burning car: a breathing low roar with crackles and the odd pop. 3 s, crossfaded wrap.
    sec = 3.6
    roar = lp(colored(r, sec, 1.3), 450) * (1 + 0.4 * np.interp(T(sec), np.linspace(0, sec, 12), r.uniform(-1, 1, 12)))
    x = roar / np.max(np.abs(roar))
    for tt in grain_times(r, 180, 0.0, sec - 0.02, "uni"):
        place(x, r.uniform(0.05, 0.3) * hp(white(r, 0.004), 1500) * dec(0.004, 0.0006), tt)
    for tt in grain_times(r, 6, 0.0, sec - 0.1, "uni"):
        place(x, r.uniform(0.2, 0.4) * mix(burst(r, 300, 4000, 0.008), 0.5 * thump(r, 200, 90, 0.01)), tt)
    return norm(xfade_loop(x, 0.6), 0.5)


# ------------------------------------------------------------------------------------------
# Rendering
# ------------------------------------------------------------------------------------------
def render(name):
    if np is None:
        raise RuntimeError("fto_synth needs numpy and scipy: python -m pip install numpy scipy")
    fn, loops, variant = SOUNDS[name]
    rng = np.random.default_rng(zlib.crc32(name.encode()))
    x = np.asarray(fn(rng, variant), float)
    if not np.all(np.isfinite(x)):
        raise RuntimeError(f"{name}: produced non-finite samples")
    peak = np.max(np.abs(x))
    if loops:
        x = circular(x - np.mean(x), lambda y: hp(y, 20))       # DC block without breaking the wrap
    else:
        x = fade(hp(x, 20), 0.0, 0.005)                          # DC block (sub-bass rumble drifts otherwise)
    return np.clip(x * (peak / (np.max(np.abs(x)) + 1e-12)), -1.0, 1.0)


def write_wav(path, x):
    pcm = np.round(x * 32767.0).astype("<i2")
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(pcm.tobytes())


def build(name, out_dir):
    x = render(name)
    path = os.path.join(out_dir, name + ".wav")
    write_wav(path, x)
    return name, len(x) / RATE, float(np.max(np.abs(x)))


def _build_star(args):
    return build(*args)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.strip().splitlines()[0])
    ap.add_argument("--out", default=os.path.join(os.path.dirname(__file__), "..", "..", "Art", "Source", "Audio"))
    ap.add_argument("--only", default="", help="comma-separated SW_* names")
    ap.add_argument("--list", action="store_true", help="print every sound name (and loop flag) and exit")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 1)
    args = ap.parse_args(argv)

    if args.list:
        for name, (_, loops, _) in SOUNDS.items():
            print(name + (" (loop)" if loops else ""))
        return 0
    names = [n.strip() for n in args.only.split(",") if n.strip()] or list(SOUNDS)
    unknown = [n for n in names if n not in SOUNDS]
    if unknown:
        print("unknown sounds: " + ", ".join(unknown), file=sys.stderr)
        return 2
    os.makedirs(args.out, exist_ok=True)
    start = time.time()
    work = [(n, args.out) for n in names]
    if args.jobs > 1 and len(work) > 1:
        from multiprocessing import Pool
        with Pool(args.jobs) as pool:
            results = pool.map(_build_star, work, chunksize=1)
    else:
        results = [build(*w) for w in work]
    for name, dur, peak in results:
        print(f"{name:28s} {dur:6.2f}s peak {peak:.2f}{'  (loop)' if SOUNDS[name][1] else ''}")
    print(f"{len(results)} sounds in {time.time() - start:.1f}s -> {os.path.abspath(args.out)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
