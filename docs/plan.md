# KloudVocalShift — plan

Status: MVP built and tested, never heard on a real vocal. This document is what
it should become, what in it is a guess, and which decisions are open.

Written 2026-08-30.

---

## 1. What the plugin is for

You record at 100 BPM and play back at 120 with Complex warp because the vocal
comes out thinner. The cost is that you are now writing to a rhythm that is not
the record's rhythm, and you cannot judge a take that way.

So: get the timbre, keep the grid. Track at the song's tempo, put this on the
channel, tell it what the tempo relationship would have been.

The part it explicitly does **not** reproduce is the performance change — the
clipped diction and quicker vibrato you get from singing to a slower click. That
is not a signal-processing effect, it is a different take. If that turns out to
be most of what you were after, this plugin is the wrong tool and the answer is
to keep tracking slow. Worth finding out early.

## 2. What is built

- Two-stage round trip: phase vocoder at the ratio, phase vocoder at the
  reciprocal. Same length in as out.
- Identity phase locking, so the level holds within 0.26 dB across the whole
  ratio range instead of collapsing 27 dB at three passes.
- Cepstral spectral-envelope shift as a third stage, exact identity at 0 st.
- Amount as a ratio interpolator rather than a dry/wet.
- Passes, 1 to 3.
- Window: Short / Normal / Long, scaled by session rate so the *time* stays put.
- Host tempo follow, with the stored Playing value as the offline fallback.
- Exact, block-size-invariant, ratio-independent reported latency, asserted
  against a measured impulse.
- JUCE-free DSP core, tested on a bare container in CI.
- Offline profiler (`measure profile`, `windows`, `formant`).

## 3. What is missing, in priority order

### 3.1 Somebody has to listen to it

Every number in the README is measured on a synthetic voice: a phase-aligned
harmonic series with three formants, vibrato, drift and a syllable envelope. It
is a good test signal and it is not a person. Nothing below this line is worth
doing before a real vocal has been through it, because the answer might be "the
window is wrong" or "one pass is already too much".

### 3.2 Latency

149 ms at Normal, 320 ms at three passes. Reported honestly and compensated in
the arrangement, but far too much to monitor a take through, which is an
uncomfortable thing to say about a plugin whose whole pitch is "do it while you
track".

Three things would cut it:

- The chain currently delays a full window per stage. A tighter overlap-add
  hands back all but a hop, which is a 25 % saving for free.
- The formant stage does not need to be a separate transform. Folded into the
  last warp stage it is a third off.
- Short window at one pass is already 75 ms, which is borderline usable. If
  Short turns out to sound right, the default should probably move.

Best case those get Normal/1 to roughly 75 ms, which is monitorable with a bit
of discipline. Worth doing before anything cosmetic.

### 3.3 The ratio is not the only axis

Right now the only way to get more effect is a bigger ratio or more passes, and
both of those are blunt. The things that actually decide how much damage a phase
vocoder does are the window, the overlap, and how strictly the phase is locked
— and the last one is a continuous parameter that is currently hardcoded to
"fully locked".

A **Lock** control that relaxes phase locking from rigid to none would give a
direct handle on how much of the chorusy, hollow phase-vocoder character comes
through, independent of the tempo relationship. That is probably the single most
useful knob not yet on the panel, and it is a one-line change to the peak loop.

### 3.4 Transient handling

Complex Pro has an Envelope control that trades timing accuracy against tonal
smoothness. There is no equivalent here: the window is the only lever, and it
moves both at once. Transient detection with a per-frame window switch is the
real answer and it is a lot of work; a simpler version is to reset the phase
accumulator on detected onsets, which keeps consonants sharp while letting the
sustains smear, and that is what people actually want.

### 3.5 The formant stage is a first guess

The lifter cutoff is 1.8 ms of quefrency, picked to sit above a 555 Hz
fundamental. That is a reasonable default for a sung male vocal and probably
wrong for a rapped one an octave up. KloudFormant solved this properly, with
pivots and consonant protection and a resolution control, and it is measured to
2 % formant placement. Reusing that core instead of the simplified version in
here is a straight upgrade and the code already exists.

### 3.6 Ratio automation

Changing the ratio mid-block retargets the stages immediately. The two
reciprocal ratios then briefly disagree about throughput, and the margin is two
hops. That should hold for a knob move; it will not hold for a fast automation
sweep, and the failure mode is an underrun, which is a click. There is a counter
for it (`WarpChain::getUnderruns`) and the tests assert it stays at zero, but
nothing smooths the ratio yet.

### 3.7 Release engineering

- Universal binaries on tags — in CI, untested.
- Code signing and notarisation. Needs an Apple Developer account.
- pluginval in CI before anything is tagged.

## 4. Suggested order of work

1. Put a real vocal through it and decide whether any of this is right
2. Latency (3.2) — overlap-add tightening, then folding the formant stage in
3. Lock control (3.3)
4. Ratio smoothing (3.6)
5. Retune the window sizes and defaults by ear
6. KloudFormant core in place of the simplified envelope shift (3.5)
7. Onset-aware phase reset (3.4)
8. pluginval, notarisation, first tagged release

---

## Still open

1. **Is the timbre the whole point, or is the performance change half of it?**
   The plugin can only give you one of the two. If a take through this does not
   land the way tracking slow does, the answer is probably that the delivery was
   doing more work than the vocoder was, and the design should change to
   something that helps you *keep* tracking slow rather than replace it.

2. **One pass or three?** Three passes is measurably more damage but it is not
   obviously *better* damage, and it triples the latency. If one pass at a 1.2
   ratio is already too subtle, the fix might be a Lock control (3.3) rather
   than more round trips.

3. **Should Formant follow the ratio automatically?** In Live, warping alone
   does not move the formants — you have to reach for Complex Pro. But if the
   sound you are chasing is always warp *plus* formant, an "auto" mode that
   derives the shift from the ratio would make the plugin one knob instead of
   two. It would also stop being an emulation of anything.

4. **Which way does the ratio go?** Recording at 100 and playing at 120 is a
   speed-up. Recording at 100 and playing at 80 is a slow-down, and the two do
   not sound the same even though they lose the same proportion of frames. If
   only one direction is ever used in practice, half of the range is clutter.
