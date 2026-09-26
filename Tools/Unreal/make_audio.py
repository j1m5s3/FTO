"""
Synthesises all of FTO's sound effects from code (100% in-house) and imports them. Run headless:

  UnrealEditor-Cmd.exe FTO.uproject -run=pythonscript -script="<repo>/Tools/Unreal/make_audio.py"

Writes 16-bit mono WAVs to Art/Source/Audio, imports them to /Game/FTO/Audio as SoundWaves
(loops flagged), and creates SA_FTOWorld, the shared 3D attenuation for world sounds.
Set FTO_SOUNDS=SW_Bonk,SW_Chime (comma-separated) to rebuild just those.
"""
import math
import os
import random
import struct
import wave

import unreal

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(REPO, "Art", "Source", "Audio")
DEST = "/Game/FTO/Audio"
RATE = 44100
TAU = 2.0 * math.pi

rng = random.Random(1234)


# ------------------------------------------------------------------------------------------
# Tiny synth toolkit
# ------------------------------------------------------------------------------------------
def silence(seconds):
    return [0.0] * int(seconds * RATE)


def tone(freq_fn, seconds, wave_fn=math.sin, amp=1.0):
    """freq_fn(t) -> Hz. Integrates phase so sweeps stay click-free."""
    out = []
    phase = 0.0
    n = int(seconds * RATE)
    for i in range(n):
        t = i / RATE
        phase += TAU * freq_fn(t) / RATE
        out.append(amp * wave_fn(phase))
    return out


def saw(phase):
    return 2.0 * ((phase / TAU) % 1.0) - 1.0


def square(phase):
    return 1.0 if (phase % TAU) < math.pi else -1.0


def tri(phase):
    return 2.0 * abs(saw(phase)) - 1.0


def noise(seconds, amp=1.0):
    return [amp * (rng.random() * 2.0 - 1.0) for _ in range(int(seconds * RATE))]


def lowpass(samples, cutoff):
    """One-pole low-pass."""
    rc = 1.0 / (TAU * cutoff)
    alpha = (1.0 / RATE) / (rc + 1.0 / RATE)
    out, y = [], 0.0
    for x in samples:
        y += alpha * (x - y)
        out.append(y)
    return out


def envelope(samples, attack=0.005, release=0.02, decay=None):
    """Linear attack/release; optional exponential decay time constant (seconds)."""
    n = len(samples)
    a = max(1, int(attack * RATE))
    r = max(1, int(release * RATE))
    out = []
    for i, x in enumerate(samples):
        g = 1.0
        if i < a:
            g = i / a
        if i > n - r:
            g *= max(0.0, (n - i) / r)
        if decay:
            g *= math.exp(-(i / RATE) / decay)
        out.append(x * g)
    return out


def mix(*tracks):
    n = max(len(t) for t in tracks)
    return [sum(t[i] if i < len(t) else 0.0 for t in tracks) for i in range(n)]


def concat(*tracks):
    out = []
    for t in tracks:
        out.extend(t)
    return out


def gain(samples, g):
    return [x * g for x in samples]


def normalize(samples, peak=0.85):
    top = max(1e-6, max(abs(x) for x in samples))
    return [x * peak / top for x in samples]


def note(name):
    names = {"C": -9, "C#": -8, "D": -7, "D#": -6, "E": -5, "F": -4, "F#": -3, "G": -2, "G#": -1, "A": 0, "A#": 1, "B": 2}
    pitch, octave = name[:-1], int(name[-1])
    return 440.0 * 2.0 ** ((names[pitch] + (octave - 4) * 12) / 12.0)


def write(name, samples):
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, name + ".wav")
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        frames = b"".join(struct.pack("<h", int(max(-1.0, min(1.0, x)) * 32767)) for x in samples)
        w.writeframes(frames)
    return path


# ------------------------------------------------------------------------------------------
# The sounds
# ------------------------------------------------------------------------------------------
def siren_loop():
    # Classic wail, 650-1500 Hz over 2 s. Mean 1075 Hz * 2 s = whole cycles, so it loops cleanly.
    T = 2.0
    f = lambda t: 1075.0 + 425.0 * math.sin(TAU * t / T - math.pi / 2)
    body = tone(f, T, lambda p: math.sin(p) + 0.25 * math.sin(2 * p) + 0.1 * math.sin(3 * p))
    return normalize(body, 0.7)


def whistle():
    # Pea-whistle trill: bright tone wobbled by the rattling pea.
    trill = 32.0
    f = lambda t: 2900.0 + 180.0 * math.sin(TAU * trill * t)
    body = tone(f, 0.55)
    body = [x * (0.7 + 0.3 * math.sin(TAU * trill * i / RATE)) for i, x in enumerate(body)]
    return normalize(envelope(mix(body, gain(lowpass(noise(0.55), 6000), 0.08)), 0.02, 0.08), 0.6)


def chime():
    # Cheerful two-note "cha-ching" for a job well done.
    a = envelope(tone(lambda t: note("E6"), 0.35, lambda p: math.sin(p) + 0.3 * math.sin(2 * p)), 0.003, 0.05, decay=0.18)
    b = envelope(tone(lambda t: note("A6"), 0.6, lambda p: math.sin(p) + 0.3 * math.sin(2 * p)), 0.003, 0.1, decay=0.3)
    return normalize(concat(a, b), 0.55)


def radio():
    # Dispatch squelch: a short beep then a burst of static.
    beep = envelope(tone(lambda t: 1050.0, 0.08, square), 0.002, 0.01)
    static = envelope(lowpass(noise(0.28), 3500), 0.005, 0.12, decay=0.12)
    return normalize(concat(gain(beep, 0.35), static), 0.5)


def alarm():
    # Chaos is spiking: three insistent two-tone pulses.
    pulses = []
    for i in range(3):
        pulses.append(envelope(tone(lambda t: 520.0 if i % 2 == 0 else 440.0, 0.16, square), 0.005, 0.02))
        pulses.append(silence(0.08))
    return normalize(lowpass(concat(*pulses), 2500), 0.5)


def brass(freq, seconds, vibrato=5.0):
    f = lambda t: freq * (1.0 + 0.006 * math.sin(TAU * vibrato * t) * min(1.0, t * 4))
    return lowpass(tone(f, seconds, saw), freq * 3.5)


def fanfare():
    parts = [envelope(brass(note(n), 0.14), 0.01, 0.03) for n in ("C5", "E5", "G5")]
    parts.append(envelope(brass(note("C6"), 0.7), 0.01, 0.25))
    return normalize(concat(*parts), 0.6)


def womp():
    # Sad trombone: wah, wah, wah, waaaah.
    parts = []
    for n, dur in (("G3", 0.32), ("F#3", 0.32), ("F3", 0.32)):
        parts.append(envelope(brass(note(n), dur, 0.0), 0.03, 0.08))
        parts.append(silence(0.05))
    parts.append(envelope(brass(note("E3"), 1.1, 6.0), 0.03, 0.35))
    return normalize(concat(*parts), 0.6)


def horn():
    body = mix(tone(lambda t: 392.0, 0.4, square), tone(lambda t: 494.0, 0.4, square))
    return normalize(envelope(lowpass(body, 1800), 0.01, 0.05), 0.5)


def engine_loop():
    # Idle burble; the game raises the pitch with speed. 55 Hz * 1 s loops cleanly.
    body = mix(tone(lambda t: 55.0, 1.0, saw), gain(tone(lambda t: 110.0, 1.0, square), 0.3))
    body = mix(lowpass(body, 400), gain(lowpass(noise(1.0), 300), 0.15))
    return normalize(body, 0.5)


def bugle():
    # Roll call: rising "ta-daa" to start the shift.
    a = envelope(brass(note("G4"), 0.18), 0.01, 0.04)
    b = envelope(brass(note("C5"), 0.55, 5.0), 0.01, 0.2)
    return normalize(concat(a, silence(0.03), b), 0.55)


def fail():
    a = envelope(tone(lambda t: note("A3"), 0.18, tri), 0.005, 0.04)
    b = envelope(tone(lambda t: note("E3"), 0.4, tri), 0.005, 0.2)
    return normalize(concat(a, b), 0.5)


def click():
    return normalize(envelope(tone(lambda t: 1800.0, 0.03), 0.001, 0.02, decay=0.01), 0.4)


def gunshot(body_hz, crack, tail, weight):
    """A cartoon bang: a sharp noise crack, a pitch-dropping thump, and a lowpassed rumble tail."""
    crack_part = envelope(noise(0.03), 0.0005, 0.02, decay=0.008)
    thump = envelope(tone(lambda t: body_hz * (0.4 + 0.6 * math.exp(-t * 30.0)), 0.18, tri), 0.001, 0.05, decay=0.05)
    rumble = envelope(lowpass(noise(tail), 500.0 + 1500.0 * weight), 0.002, tail * 0.5, decay=tail * 0.35)
    return normalize(mix(gain(crack_part, crack), gain(thump, 0.8), gain(rumble, 0.5 + weight)), 0.8)


def shot_pistol():
    return gunshot(220.0, 0.9, 0.35, 0.2)


def shot_shotgun():
    return gunshot(120.0, 0.7, 0.8, 0.8)


def shot_rifle():
    return gunshot(300.0, 1.2, 0.55, 0.45)


def taser():
    # Crackling electric buzz: a 60 Hz square with sparks of noise.
    buzz = tone(lambda t: 62.0, 0.5, square)
    sparks = [x * (1.0 if rng.random() < 0.3 else 0.2) for x in noise(0.5)]
    return normalize(envelope(mix(gain(lowpass(buzz, 3000), 0.5), gain(lowpass(sparks, 7000), 0.5)), 0.005, 0.08), 0.6)


def reload():
    # Mag out, mag in: click-clack.
    click = lambda f: envelope(mix(tone(lambda t: f, 0.04, square), gain(noise(0.04), 0.4)), 0.001, 0.02, decay=0.01)
    return normalize(concat(click(1400.0), silence(0.22), gain(click(900.0), 1.2), silence(0.08), click(1600.0)), 0.5)


def ricochet():
    # Pee-yow: a whistle falling in pitch.
    return normalize(envelope(tone(lambda t: 2600.0 * math.exp(-t * 2.2) + 500.0, 0.45), 0.002, 0.15, decay=0.2), 0.4)


def dry_fire():
    return normalize(envelope(mix(tone(lambda t: 2200.0, 0.02, square), noise(0.02)), 0.0005, 0.01, decay=0.006), 0.35)


def bonk():
    # Somebody got knocked flying: a dull thump with a springy cartoon boing on top.
    thump = mix(tone(lambda t: 45.0 + 120.0 * math.exp(-t * 14.0), 0.25),
                gain(lowpass(noise(0.25), 700), 0.7))
    thump = envelope(thump, 0.001, 0.05, decay=0.07)
    wobble = lambda t: 1.0 + 0.3 * math.sin(TAU * 17.0 * t) * math.exp(-t * 4.0)
    boing = envelope(tone(lambda t: 310.0 * wobble(t) * (1.0 - 0.4 * t), 0.55, tri), 0.004, 0.15, decay=0.25)
    return normalize(mix(thump, gain(boing, 0.5)), 0.6)


def cuffs():
    # Handcuffs going on: two quick ratchets, then a solid clunk.
    def tick(f):
        return envelope(mix(tone(lambda t: f, 0.012, square), gain(noise(0.012), 0.6)), 0.0005, 0.006, decay=0.004)

    def ratchet(base):
        return concat(*[concat(tick(base + 120.0 * k), silence(0.018)) for k in range(6)])

    clunk = envelope(mix(tone(lambda t: 700.0, 0.06, square), gain(noise(0.06), 0.5)), 0.0005, 0.03, decay=0.015)
    return normalize(lowpass(concat(ratchet(2600.0), silence(0.12), ratchet(2800.0), silence(0.05), gain(clunk, 1.3)), 9000), 0.55)


def scuffle():
    # A cartoon dust-up: a flurry of thumps and slaps, with a "hup!" in the middle.
    def hit():
        f = rng.uniform(80.0, 160.0)
        body = mix(tone(lambda t: f * (1.0 - 4.0 * t), 0.09, tri), gain(lowpass(noise(0.09), rng.uniform(900.0, 2500.0)), 0.8))
        return gain(envelope(body, 0.001, 0.04, decay=0.03), rng.uniform(0.5, 1.0))

    parts = []
    for _ in range(9):
        parts.append(hit())
        parts.append(silence(rng.uniform(0.03, 0.1)))
    grunt = envelope(lowpass(tone(lambda t: 140.0 - 60.0 * t, 0.16, saw), 900.0), 0.01, 0.06)
    return normalize(concat(*parts[:8], gain(grunt, 0.6), *parts[8:]), 0.6)


def squelch_open():
    # Keying the radio: a click and a quick hiss of static.
    click_ = envelope(tone(lambda t: 1800.0, 0.012, square), 0.0005, 0.006)
    hiss = envelope(lowpass(noise(0.12), 4500), 0.002, 0.08, decay=0.05)
    return normalize(concat(gain(click_, 0.5), hiss), 0.4)


def squelch_close():
    # Letting go: the "roger beep" and the static cutting off.
    hiss = envelope(lowpass(noise(0.09), 4500), 0.002, 0.01)
    beep = envelope(tone(lambda t: 1400.0, 0.07, square), 0.002, 0.01)
    return normalize(lowpass(concat(hiss, gain(beep, 0.35)), 5000), 0.4)


def callout():
    # A quick callout on the squad channel: a rising two-tone chirp over a crackle.
    lo = envelope(tone(lambda t: 880.0, 0.07, square), 0.002, 0.01)
    hi = envelope(tone(lambda t: 1320.0, 0.1, square), 0.002, 0.02)
    crackle = envelope(lowpass(noise(0.22), 3000), 0.005, 0.12, decay=0.1)
    return normalize(mix(lowpass(concat(lo, silence(0.02), hi), 4000), concat(silence(0.05), gain(crackle, 0.35))), 0.5)
def glass():
    # A window going: a bright crack, then a cascade of tinkling shards.
    crack = envelope(lowpass(noise(0.06), 9000), 0.0005, 0.03, decay=0.02)
    shards = [crack]
    for _ in range(18):
        f = rng.uniform(2500.0, 6500.0)
        tink = envelope(tone(lambda t, f=f: f, 0.08, tri), 0.001, 0.05, decay=0.02)
        shards.append(concat(silence(rng.uniform(0.02, 0.55)), gain(tink, rng.uniform(0.25, 0.7))))
    return normalize(mix(*shards), 0.6)


def crash():
    # Cars meeting things: a heavy crunch with a metal scrape on the end.
    thud = envelope(mix(tone(lambda t: 60.0 * (1.0 - 0.5 * t), 0.35, tri), gain(lowpass(noise(0.35), 400), 1.2)), 0.001, 0.1, decay=0.08)
    scrape = envelope(lowpass(mix(noise(0.5), gain(tone(lambda t: 900.0 + 300.0 * math.sin(TAU * 23.0 * t), 0.5, saw), 0.3)), 3000), 0.01, 0.2, decay=0.15)
    return normalize(mix(thud, gain(concat(silence(0.03), scrape), 0.6)), 0.7)


def clang():
    # Street furniture knocked flying: a hollow metal clang.
    partials = [gain(envelope(tone(lambda t, f=f: f, 0.6), 0.001, 0.2, decay=0.18 / (i + 1)), 1.0 / (i + 1))
                for i, f in enumerate((310.0, 820.0, 1450.0, 2330.0))]
    knock = gain(envelope(noise(0.03), 0.0005, 0.02, decay=0.008), 0.5)
    return normalize(mix(*partials, knock), 0.6)


def gush_loop():
    # A burst hydrant: rushing, burbling water. Loops (2 s of noise, burble at 7 Hz = whole cycles).
    water = mix(lowpass(noise(2.0), 2500), gain(lowpass(noise(2.0), 600), 0.8))
    burble = [x * (0.8 + 0.2 * math.sin(TAU * 7.0 * i / RATE)) for i, x in enumerate(water)]
    return normalize(burble, 0.45)


def fire_loop():
    # A car on fire: a low roar with crackles. Loops.
    roar = lowpass(noise(2.0), 350)
    crackle = lowpass([x if rng.random() < 0.004 else 0.0 for x in noise(2.0)], 5000)
    return normalize(mix(roar, gain(crackle, 3.0)), 0.5)


SOUNDS = {
    "SW_SirenLoop": (siren_loop, True),
    "SW_Whistle": (whistle, False),
    "SW_Chime": (chime, False),
    "SW_Radio": (radio, False),
    "SW_Alarm": (alarm, False),
    "SW_Fanfare": (fanfare, False),
    "SW_Womp": (womp, False),
    "SW_Horn": (horn, False),
    "SW_EngineLoop": (engine_loop, True),
    "SW_Bugle": (bugle, False),
    "SW_Fail": (fail, False),
    "SW_Click": (click, False),
    "SW_Bonk": (bonk, False),
    "SW_ShotPistol": (shot_pistol, False),
    "SW_ShotShotgun": (shot_shotgun, False),
    "SW_ShotRifle": (shot_rifle, False),
    "SW_Taser": (taser, False),
    "SW_Reload": (reload, False),
    "SW_Ricochet": (ricochet, False),
    "SW_DryFire": (dry_fire, False),
    "SW_Cuffs": (cuffs, False),
    "SW_Scuffle": (scuffle, False),
    "SW_SquelchOpen": (squelch_open, False),
    "SW_SquelchClose": (squelch_close, False),
    "SW_Callout": (callout, False),
    "SW_Glass": (glass, False),
    "SW_Crash": (crash, False),
    "SW_Clang": (clang, False),
    "SW_GushLoop": (gush_loop, True),
    "SW_FireLoop": (fire_loop, True),
}


# ------------------------------------------------------------------------------------------
# Import
# ------------------------------------------------------------------------------------------
eal = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()


def import_wav(path, name, looping):
    task = unreal.AssetImportTask()
    task.filename = path
    task.destination_path = DEST
    task.destination_name = name
    task.replace_existing = True
    task.automated = True
    task.save = True
    tools.import_asset_tasks([task])
    sound = eal.load_asset(f"{DEST}/{name}")
    if not sound:
        unreal.log_error(f"FTO: failed to import {name}")
        return
    sound.set_editor_property("looping", looping)
    eal.save_loaded_asset(sound)
    unreal.log(f"FTO: {name} {sound.get_editor_property('duration'):.2f}s{' (loop)' if looping else ''}")


def make_attenuation():
    path = f"{DEST}/SA_FTOWorld"
    att = eal.load_asset(path) if eal.does_asset_exist(path) else tools.create_asset(
        "SA_FTOWorld", DEST, unreal.SoundAttenuation, unreal.SoundAttenuationFactory())
    settings = att.get_editor_property("attenuation")
    settings.set_editor_property("attenuate", True)
    settings.set_editor_property("spatialize", True)
    settings.set_editor_property("attenuation_shape_extents", unreal.Vector(600.0, 0.0, 0.0))
    settings.set_editor_property("falloff_distance", 4500.0)
    att.set_editor_property("attenuation", settings)
    eal.save_loaded_asset(att)
    unreal.log("FTO: SA_FTOWorld ready")


ONLY = [name for name in os.environ.get("FTO_SOUNDS", "").split(",") if name]
for sound_name, (build, loops) in SOUNDS.items():
    if not ONLY or sound_name in ONLY:
        import_wav(write(sound_name, build()), sound_name, loops)
if not ONLY:
    make_attenuation()
