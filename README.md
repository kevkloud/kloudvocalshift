# KloudVocalShift

The thing that happens to a vocal when you warp it, without the thing that
happens to the timing. VST3 / AU / Standalone.

Track at the song's tempo, hear the song's rhythm, and still get the sound you
were getting by tracking at 100 and playing it back at 120.

**This is a mixing plugin, not a tracking one.** It costs around 128 ms of
latency. Your DAW compensates for that on playback and everything stays in time,
but you cannot monitor yourself through it while you record. Record dry, then
reach for it.

## Status

MVP. It builds, it installs, it is tested, and the DSP does what this document
says it does. Nobody has put a real vocal through it yet — every number below is
measured on a synthetic voice, which is the only test that can run in CI and not
the test that decides whether it is any good. See [docs/plan.md](docs/plan.md).

## What the workflow is doing

The move is: record a vocal to a 100 BPM click, set the project to 120, let
Ableton's **Complex** warp mode stretch the clip to fit. The vocal comes out
thinner — less chest, more air, a smaller-sounding throat. 4batz, Drake and
Karri records are full of it.

There are two separate things going on in that, and they are worth separating
because only one of them is a plugin.

**The performance changes.** You sang against a slower click and you are hearing
it faster. The diction is clipped, the vibrato is quicker, the phrasing sits
differently. That is real and it is a large part of the sound — and it is also
the part that makes the take hard to judge, because the rhythm you are listening
to is not the rhythm of the record. This plugin does not do that, deliberately.

**The signal is damaged.** That part is a plugin, and it is what this is.

## What Complex actually does

Complex is a **phase vocoder**. It does not resample — that is Repitch, which is
tape varispeed and moves the pitch. Complex chops the audio into overlapping
short windows, takes the spectrum of each one, and lays those spectra back down
at a different spacing. Closer together is faster, further apart is slower, and
because it rebuilds each window from its own spectrum rather than replaying it,
the pitch stays where it was.

Three things happen on the way through. All three are the sound.

**Frames get thrown away.** Speeding up 1.2x means the output runs out of room
before the input runs out of windows: five go down for every six analysed, and
one in six is never used at all. This is the "losing data" part your friend is
describing. It is not sample rate — the sample rate never changes. It is frames.
Slowing down is the same problem wearing a different hat: windows get repeated
rather than dropped, and the repeats are just as much of a lie.

**The pulse comes apart.** This is the big one. A voice is a train of glottal
pulses — the vocal folds slamming shut a hundred-odd times a second — and what
makes it a *pulse* is that every harmonic arrives phase-aligned at the same
instant. A phase vocoder does not know that. It works out each frequency bin's
phase from that bin's own estimated frequency, independently of every other bin.
Individually each one is right. Collectively they stop arriving together, and the
sharp pulse spreads into a continuous drone.

The spectrum is intact. Point an analyser at it and almost nothing has changed.
What has gone is the *time* structure inside each period — and that is precisely
what the ear uses to hear chest, body and proximity. Take it away and the voice
gets thinner without getting quieter, brighter or EQ'd.

**Transients smear.** A consonant is a click, and a click spread over a 43 ms
analysis window is a softer consonant. Plosives lose their edge and sibilants
lose their point.

## How this does it in place

It warps, and then it unwarps.

Stage one is a phase vocoder at your ratio — literally what Live does. Stage two
is another one at the reciprocal, which puts the length back. The second stage
does not repair anything the first one lost; it is a phase vocoder too, so it
adds its own. What comes out is the same number of samples as what went in, sits
on the same grid, and has been through the mill twice.

That is not a model of the effect. It is the effect, with the duration change
removed.

Then a third stage, which is not a warp at all: a spectral-envelope shift
standing in for Complex Pro's **Formants** control, and for the fact that most of
these records are formant-shifted as well as warped. It only touches magnitudes,
so it stacks with the warp rather than fighting it, and at zero it is exactly
nothing.

### One thing that is not a naive phase vocoder

Propagating every bin on its own is also what makes a bad time-stretch sound
like a chorus pedal, and it costs a lot of level: bins that belong to the same
harmonic drift apart, the overlapping frames stop adding coherently, and an
early build of this lost **27 dB** at three passes.

So the phase is **identity phase-locked** (Laroche and Dolson), which is what
Live does too. Only spectral peaks are propagated; every other bin is pinned to
its peak with the offset it had in the analysis, so each harmonic stays
internally rigid. What is deliberately *not* locked is one harmonic to the next
— that drift is the effect. The same three passes now lose **0.4 dB**.

## The controls

| | |
|---|---|
| **Recorded** / **Playing** | the two tempos. The ratio between them is the warp. |
| **Follow Host** | takes Playing from the transport, so it cannot drift out of sync with the song. |
| **Amount** | interpolates the ratio toward 1. Not a dry/wet — half a warp is what warping to 110 instead of 120 sounds like, whereas half the wet signal is a chorus. |
| **Passes** | how many times to run the round trip. One is a warp. Three is a warp, a bounce, and a warp of the bounce. |
| **Formant** | envelope shift in semitones. Up is thinner. |
| **Window** | Short keeps consonants and hits harder; Long stops sounding like a warp and starts sounding like a freeze. |
| **Mix**, **Trim**, **Bypass** | output. |

## What is asserted

From `tests/DspTests.cpp`, which asserts every claim below.

| | |
|---|---|
| Equal tempos | null to **−138 dB** against the delayed input |
| Equal tempos, Amount 100 %, 3 passes | still null |
| Amount at 0 % | null |
| Bypass, Mix at 0 % | null |
| Reported latency | equals the **measured** impulse delay, exactly |
| Block size 64 vs 512 | **bitwise identical** |
| Block size 128 vs 480, formant engaged | **bitwise identical** |
| Same settings twice | **bitwise identical** |
| Silence in | silence out |
| Level change, whole ratio range | within **0.26 dB** |
| Loudest output over ratio × formant | **+3.1 dB** |
| 44.1 / 48 / 88.2 / 96 kHz | null at all four |
| Buffer underruns, whole suite | **zero**, across every ratio × window × pass × block size |

### What a warp costs the pulse

Crest factor is peak over RMS: a glottal pulse train scores high, and smearing
it pulls the peaks down and fills the gaps in. Regenerate with
`./build/measure profile`.

```
  100 BPM to   ratio    crest dB   pulse lost   level
  80           0.800    13.68      -3.45        -0.17 dB
  90           0.900    14.56      -2.57        -0.14 dB
  100          1.000    17.13       0.00        -0.00 dB
  110          1.100    14.01      -3.12        -0.16 dB
  120          1.200    13.88      -3.25        -0.20 dB
  140          1.400    12.78      -4.35        -0.26 dB
```

Three dB of pulse gone and a fifth of a dB of level: the plugin takes the body
out without touching the loudness, which is the whole point and is also why it
is hard to A/B any other way.

### Latency

Stacked short-time transforms are not free. Two per pass, plus a buffer for the
jitter between two stages whose ratios are reciprocal but whose frames do not
line up.

```
  window   samples   1 pass     3 passes
  Short    1024      64.0 ms    170.7 ms
  Normal   2048      128.0 ms   341.3 ms
  Long     4096      256.0 ms   682.7 ms
```

Reported to the host exactly — asserted against a measured impulse — so delay
compensation puts everything back on the grid in the arrangement. It is still
far too much to monitor a live take through. That is the trade for doing this in
place instead of on a warped clip, and it is why the header of the plugin says
so.

## What else is out there

Nothing sells this. The neighbours each do one part of it:

| | What it is | What it does not do |
|---|---|---|
| [Soundtoys Little AlterBoy](https://www.soundtoys.com/product/little-alterboy/) | the default "thin vocal" plugin: pitch + formant | no vocoder character at all — it is trying to be clean |
| [Waves Vocal Bender](https://www.waves.com/plugins/vocal-bender), [Polyverse Manipulator](https://polyversemusic.com/products/manipulator/) | pitch and formant, more of it | same — the artefacts are the enemy, not the product |
| [Baby Audio Warp](https://babyaud.io/) | time-stretch as an effect | changes the length, which is the whole thing being avoided here |
| Melodyne, Serato Pitch'n Time | best-in-class stretching | studio tools whose entire goal is that you cannot hear them |
| RC-20 Retro Color, SketchCassette | lo-fi character on a vocal | tape, noise and bit-crushing — a completely different kind of damage |

Every stretching tool on the market competes on how *little* of this you can
hear. This one is the artefact, on purpose, with the time change taken out.

## Install

Download from [Releases](https://github.com/kevkloud/kloudvocalshift/releases),
unzip, drag into your plugin folder. Full instructions, including the macOS
Gatekeeper step, are in [docs/INSTALL.md](docs/INSTALL.md).

## Build

```
git clone --recurse-submodules https://github.com/kevkloud/kloudvocalshift.git
cd kloudvocalshift
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --parallel
```

macOS builds VST3, AU and a standalone; Windows builds VST3 and the standalone.
A development build installs itself into your plugin folders;
`./scripts/build.sh` builds and then checks that it actually landed, which
Ableton will otherwise let fail silently.

The DSP core has no JUCE dependency, so the interesting half builds and tests on
a bare container in seconds:

```
cmake -B build-dsp -DKVS_DSP_ONLY=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-dsp --parallel
ctest --test-dir build-dsp --output-on-failure
```

## Licence

AGPL-3.0. © LT3 Audio.
