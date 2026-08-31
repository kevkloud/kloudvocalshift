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
is not a signal-processing effect, it is a different take.

**Settled 2026-08-30:** the timbre is the whole point. If the delivery is what is
wanted, the answer is to track at another tempo and warp normally, without this.
The plugin does not need to chase it.

## 2. What is built

- Two-stage round trip: phase vocoder at the ratio, phase vocoder at the
  reciprocal. Same length in as out. Symmetric — 100 to 80 and 100 to 120 both
  go through both stages, which is correct: a slow-down duplicates frames where
  a speed-up drops them, and the round trip undoes neither.
- Identity phase locking, so the level holds within 0.26 dB across the whole
  ratio range instead of collapsing 27 dB at three passes.
- Cepstral spectral-envelope shift, folded onto the last warp stage rather than
  given a transform of its own. Exact identity at 0 st. Manual, not derived from
  the ratio — plain Complex does not move formants, and matching that is the
  decision.
- Amount as a ratio interpolator rather than a dry/wet.
- Passes, 1 to 3.
- Window: Short / Normal / Long, scaled by session rate so the *time* stays put.
- Host tempo follow, with the stored Playing value as the offline fallback.
- Exact, block-size-invariant, ratio-independent reported latency, asserted
  against a measured impulse.
- JUCE-free DSP core, tested on a bare container in CI.
- Offline profiler (`measure profile`, `windows`, `formant`).
- Offline panel renderer (`panel out.png`), so a layout change can be looked at
  without loading a DAW.
- pluginval at strictness 10 and `auval` in CI; tagged releases built as
  universal macOS binaries by GitHub Actions, never by hand.

## 3. What is missing, in priority order

### 3.1 Somebody has to listen to it

Every number in the README is measured on a synthetic voice: a phase-aligned
harmonic series with three formants, vibrato, drift and a syllable envelope. It
is a good test signal and it is not a person. Nothing below this line is worth
doing before a real vocal has been through it, because the answer might be "the
window is wrong" or "one pass is already too much".

### 3.2 Latency

Two rounds of work done. What is left has quality costs, so it should be a
control rather than a decision made on somebody's behalf.

**Done.** 149.3 ms to 128.0 ms at Normal / 1 pass:

- Folded the formant shift onto the last warp stage instead of giving it a
  transform of its own. −43 ms and a third off the CPU, for a bit-identical
  output. Free.
- Then gave 21 ms of it straight back: the jitter margin had to grow from two
  hops to stages + 2, because a sweep over every ratio, window, pass count and
  block size found the chain underrunning at small block sizes. An underrun
  shifts everything after it a sample off the grid, silently. Not a trade worth
  making.

**Still available, with what each one costs.**

| Change | Saves | Costs |
|---|---|---|
| Default to **Short** window | 128 → 64 ms | a different sound, not obviously worse — Short measured *more* pulse loss (−4.52 dB vs −3.25) and sharper consonants. Wants an ear. |
| Asymmetric windows: Normal in, Short out | 128 → 96 ms | the second stage does less damage than the first, so the character stops being symmetrical. Unknown until heard. |
| Hop from N/4 to N/2 | nothing | overlap-add stops summing to a constant; the identity claim dies. **No.** |
| Drop to one stage and emulate the round trip | 128 → 64 ms | this was the first design. It was measured and it did almost nothing — a vocoder at a 1:1 frame rate is nearly transparent. **No.** |
| Shrink the window at high sample rates | 0 | the window is scaled by rate so the *time* stays put, which is what fixes the character. **No.** |

**The RT vs Quality control.** The honest version of the above is a mode switch
whose Realtime setting picks Short window and one pass and reports ~64 ms, and
whose Quality setting leaves everything as it is. 64 ms is still not
monitorable — for that the window would have to come down to 256 samples, which
is 5 ms of analysis and cannot resolve a male fundamental, so the effect stops
being this effect. **There is no version of this that monitors a live take.**
The physics of a phase vocoder is that it needs a window long enough to see a
pitch period, and a chain of them needs several.

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

### 3.7 GUI

Compacted to 568 x 208 with the selectors carrying their own labels ("Normal",
"2 passes") so nothing needs a caption. Renderable offline with `./build/panel`.

Not yet decided: what it should actually look like. It currently borrows
FrostyEQ's finish without having its own idea. Candidates worth trying —

- A waveform or crest meter showing the pulse flattening in real time. That is
  the one thing this plugin does that nothing else does, and there is currently
  no picture of it anywhere on the panel.
- Recorded and Playing as typed number fields rather than knobs. They are
  tempos; nobody wants to dial 100 with a mouse.
- Collapsing WARP into a single "100 → 120" widget.

### 3.8 Release engineering

Done: universal binaries on tags, pluginval at strictness 10, `auval`, draft
GitHub Releases with install instructions in the zip.

Left:

- **Code signing and notarisation.** Needs an Apple Developer account ($99/yr).
  Without it, every macOS user has to run an `xattr` command from the terminal,
  which is documented in `docs/INSTALL.md` but is a real drop-off point and
  reads as untrustworthy to exactly the audience being built here.
- Windows code signing. Cheaper to skip; SmartScreen warnings on a plain zip of
  a `.vst3` are milder than Gatekeeper's.
- A macOS `.pkg` and a Windows installer, so nobody has to know where the plugin
  folder is.
- Linux VST3. Cheap in CI, and Bitwig/Reaper users notice.
- AAX would need a paid Avid signature. Not worth it yet.

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

1. **Is one pass at 1.2 enough?** Three passes is measurably more damage but it
   triples the latency. A **Lock** control (3.3) is the cheaper way to get more
   character, and it does not cost a millisecond.

2. **Short or Normal as the default?** Short is half the latency and measured
   *more* pulse loss. If it sounds right, the default should move and the
   latency problem mostly goes away.

3. **What should the panel be a picture of?** See 3.7. There is no visual for
   the one thing this plugin does.

4. **Signing.** $99/yr buys away the single worst moment in the install, on the
   platform most of this audience uses. Probably the highest-leverage money in
   the whole project, and it is worth deciding before the first release rather
   than after a wave of "it says the developer cannot be verified" comments.
