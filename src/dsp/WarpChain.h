#pragma once

#include "SpectralStage.h"

#include <array>
#include <vector>

namespace kloudvocalshift
{

enum class Window { shortWindow = 0, normal = 1, longWindow = 2 };

//==============================================================================
/** The warp, and then the warp undone.

    Recording at 100 BPM and playing back at 120 with Complex warp does two
    things at once: it changes when everything happens, and it costs the sound
    something on the way through. The rhythm is the part you can hear straight
    away and the part you least want to give up, because a vocal you tracked
    against a 100 BPM click and are now hearing at 120 is not a vocal you can
    judge.

    So this stretches by the ratio and then stretches back. Stage one is exactly
    what Live does. Stage two puts the timing back where it started -- and,
    being another phase vocoder, does not restore anything stage one lost; it
    adds its own. The output is the same length as the input, sits on the same
    grid, and has been through the mill twice.

    That is not an approximation of the effect. It is the effect, with the
    duration change removed.

    Passes runs the pair more than once, for material that needs more than one
    warp's worth. The cost is latency: every stage in the chain is a window.

    The formant shift rides on the last stage rather than getting a transform of
    its own. It only touches magnitudes and never reads or writes phase, so it
    composes with the warp instead of fighting it -- and folding it in saves a
    whole window of latency and a third of the CPU, which on a plugin that costs
    this much delay is not a saving to leave on the table.
*/
class WarpChain
{
public:
    // One warp and one unwarp. There was a Passes control that ran the pair
    // more than once; it was removed. Each extra pass re-analyses the previous
    // pass's already-decohered output, so the peak estimates get worse and the
    // phase locking degrades -- which does not deepen the effect, it just adds
    // chorusing. Lock is the control for wanting more, and it costs nothing.
    static constexpr int kStages = 2;

    void prepare (double sampleRate, int fftSize, int maxBlockSize, bool deliveryEnabled);
    void reset();

    /** ratio is playing tempo over recorded tempo. Cheap: it only retargets
        the stages, so it is safe to call every block. Changing the pass count
        is not -- that is a prepare(), because it changes the latency. */
    void setWarp (double ratio, double formantSemis, double lock, double delivery) noexcept;

    int getWindow() const noexcept  { return windowSize; }

    /** Constant for a given window, and for whether Delivery is engaged at all.
        It does not move with the ratio, the formant shift, the lock, or how far
        Delivery is turned up. */
    int getLatencySamples() const noexcept { return latency; }

    /** In place. Input and output are the same length, always. */
    void process (float* samples, int numSamples);

    /** Should stay at zero. Asserted in the tests: an underrun is a click, and
        the padding exists precisely so that one can never happen. */
    int getUnderruns() const noexcept { return underruns; }

private:
    void rebuild() noexcept;

    std::array<SpectralStage, kStages> stages;

    int windowSize = 0;
    int analysisHop = 0;
    int latency = 0;
    bool deliveryReserved = false;

    double ratio = 1.0;
    double formantSemis = 0.0;

    std::vector<float> ping, pong;

    // Input is fed to the stages a hop at a time whatever the host's block size
    // is. That is what makes the output block-size invariant: the stages then
    // see exactly the same sequence of pushes in a 64-sample session as in a
    // 2048-sample one, so they produce exactly the same samples.
    std::vector<float> inQueue;
    int inCount = 0;

    // Output queue, preceded by a short fixed run of silence.
    //
    // The pipeline is primed with silence at reset rather than left to fill
    // from the first real sample, so by the time audio arrives every stage is
    // already in steady state and hands back one sample for every sample it is
    // given. That is what makes the latency a number the host can be told
    // rather than something that settles differently at every block size; the
    // margin then only has to cover the burst jitter between two stages whose
    // ratios are reciprocal but whose frames do not line up.
    std::vector<float> fifo;
    int fifoRead = 0, fifoWrite = 0, fifoCount = 0;
    int margin = 0, padRemaining = 0;
    int underruns = 0;

    void pump (const float* chunk);
    void fifoPush (const float* data, int n);
    void fifoPop (float* data, int n) noexcept;
};

} // namespace kloudvocalshift
