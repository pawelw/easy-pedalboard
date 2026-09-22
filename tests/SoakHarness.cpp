// The real soak harness release-plan.md 1.5 asks for: drives the real
// processor for a fixed wall-clock duration, randomising everything a player
// or a host would ever throw at it at once, and asserts on every block that
// nothing has gone wrong. Meant to run overnight, one per product - see
// CLAUDE.md's test list.
//
//   ee_soak_<Target> [--hours 8] [--seconds S] [--seed N] [--instances 1]
//                     [--report-interval 60] [--editor-cycles]
//
// --seconds overrides --hours - use it for a quick smoke test before
// committing a machine overnight. --instances runs that many processor
// instances concurrently, each on its own std::thread once instances > 1;
// pass 32 for the "shared static between instances" class of bug (the
// DaisySP myrand() race CLAUDE.md's Sanitizers section describes is exactly
// the shape this is aimed at) - run that pass under `cmake --preset tsan`
// too, since TSan sees a data race the checks below cannot.
// --editor-cycles opens and closes the real editor between renders; it only
// makes sense with --instances 1 (JUCE components are message-thread only)
// and is off by default because the five WebView faces construct a native
// platform webview that this console app never pumps a run loop for - it is
// a construct/destroy leak-and-crash check, not a rendered one.
//
// One binary per product, like ee_param_golden and ee_preset_fuzz: every
// pedal's PluginProcessor.cpp defines createPluginFilter(), so two of them
// cannot share a link.
//
// What every block is checked against, continuously, for the whole run:
//   - every output sample finite
//   - every output sample under kSafetyCeiling (BitBitAlpine's own
//     sanitizeOutput uses the same 4.0f - see its PluginProcessor.h - so this
//     is the general form of the guard that specific pedal already has)
//   - no heap allocation while inside processBlock (global operator new/
//     delete, gated on a thread_local flag - see AudioThreadGuards below)
//   - on macOS, no pthread mutex taken while inside processBlock (dyld
//     symbol interposition on pthread_mutex_lock - catches juce::CriticalSection,
//     not a juce::SpinLock, which never calls it)
//
// ...while continuously randomising, per instance:
//   - every parameter: a continuous one random-walks every block with an
//     occasional hard jump; a discrete one (booleans, engine choices) jumps
//     to a random step at low probability per block - which is what exercises
//     an engine switch mid-note, the click risk ee_module_stress covers a
//     slice of for Alpine's modules specifically
//   - block size, from 1 to the current prepare's maximum, including forced
//     1-sample and prime-length blocks
//   - sample rate and max block size, by re-entering prepareToPlay mid-run
//     the way a host does on a settings change
//   - transport: rolling, stopped, looping (with wraparound) and relocating
//     (a jump to a random position) - SoakPlayHead below, not
//     RegressHarness's FakePlayHead, because that one only ever rolls forward
//   - preset loads mid-processing, from the factory bank, without pausing the
//     signal - the exact shape of "a whole tree arriving at once is not a
//     knob being turned" (CLAUDE.md, Presets)
//   - instance recreation: the whole processor is torn down and rebuilt in
//     place periodically, simulating a host closing and reopening the plugin
//     without restarting the process
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <random>
#include <thread>
#include <vector>

#include "PluginProcessor.h"
#include "ee/plugin/PresetStore.h"

#if JUCE_MAC || JUCE_LINUX
#include <execinfo.h>
#define EE_SOAK_HAVE_BACKTRACE 1
#else
#define EE_SOAK_HAVE_BACKTRACE 0
#endif

namespace
{
using Processor = EE_SOAK_PROCESSOR;
using Clock = std::chrono::steady_clock;

// The general form of BitBitAlpineProcessor::kSafetyCeiling
// (plugins/bitbit-alpine/src/PluginProcessor.h) - a level nothing on this
// project's face can reach on purpose, so crossing it means a real blow-up
// rather than a loud preset.
constexpr float kSafetyCeiling = 4.0f;

//==============================================================================
// Audio-thread guards
//
// "The audio thread" here is whichever thread is inside a given instance's
// processBlock call - there is no realtime deadline in an offline tool, but
// the two properties that matter for a real host (no allocation, no lock) are
// still properties of *that call*, and this is the only place they can be
// checked from outside the DSP itself.
thread_local bool tls_inAudioCallback = false;
thread_local int tls_instanceIndex = -1;
thread_local bool tls_inViolationHandler = false;

std::atomic<long long> gAllocViolations { 0 };
std::atomic<long long> gLockViolations { 0 };

// Where the first of each kind came from - captured with backtrace(), which
// only touches the stack and a fixed buffer, so it is safe to call from
// inside operator new itself. Symbolicated later, at the end of main(), where
// allocating to do it no longer matters. macOS/Linux only - see
// EE_SOAK_HAVE_BACKTRACE; elsewhere the counts alone still get reported.
constexpr int kMaxFrames = 32;
std::atomic<bool> gAllocBacktraceCaptured { false };
std::atomic<bool> gLockBacktraceCaptured { false };
#if EE_SOAK_HAVE_BACKTRACE
void* gAllocBacktrace[kMaxFrames];
void* gLockBacktrace[kMaxFrames];
int gAllocBacktraceFrames = 0;
int gLockBacktraceFrames = 0;
#endif

void reportViolation (const char* what, std::size_t detail)
{
    // A violation handler that itself allocates or locks would recurse
    // straight back into this function - guard against that rather than
    // trusting fprintf never will.
    if (tls_inViolationHandler)
        return;

    tls_inViolationHandler = true;
    const bool isAlloc = std::strcmp (what, "allocation") == 0;
    const auto n = (isAlloc ? gAllocViolations : gLockViolations).fetch_add (1) + 1;

    auto& captured = isAlloc ? gAllocBacktraceCaptured : gLockBacktraceCaptured;
    bool expected = false;
#if EE_SOAK_HAVE_BACKTRACE
    if (captured.compare_exchange_strong (expected, true))
    {
        auto& frames = isAlloc ? gAllocBacktraceFrames : gLockBacktraceFrames;
        auto* buffer = isAlloc ? gAllocBacktrace : gLockBacktrace;
        frames = backtrace (buffer, kMaxFrames);
    }
#else
    captured.compare_exchange_strong (expected, true);
#endif

    if (n <= 20)
        std::fprintf (stderr, "  *** %s on instance %d during processBlock%s%zu%s\n", what, tls_instanceIndex,
                     detail > 0 ? " (" : "", detail, detail > 0 ? " bytes)" : "");

    tls_inViolationHandler = false;
}

struct AudioCallbackScope
{
    explicit AudioCallbackScope (int instanceIndex)
    {
        tls_instanceIndex = instanceIndex;
        tls_inAudioCallback = true;
    }
    ~AudioCallbackScope() { tls_inAudioCallback = false; }
};
} // namespace

void* operator new (std::size_t size)
{
    if (tls_inAudioCallback)
        reportViolation ("allocation", size);
    if (void* p = std::malloc (size))
        return p;
    throw std::bad_alloc();
}

void operator delete (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }

void* operator new[] (std::size_t size) { return ::operator new (size); }
void operator delete[] (void* p) noexcept { ::operator delete (p); }
void operator delete[] (void* p, std::size_t) noexcept { ::operator delete (p); }

#if JUCE_MAC
#include <pthread.h>

// dyld's own symbol-interposition mechanism - a __DATA,__interpose section dyld
// reads at load time and rewrites callers of the second symbol to the first.
// Not pulled from <mach-o/dyld-interposing.h>: recent SDKs do not reliably ship
// that header, but the section format it wraps is a stable dyld ABI, reproduced
// here directly.
#define EE_DYLD_INTERPOSE(replacement, original)                                                                   \
    __attribute__ ((used)) static struct                                                                           \
    {                                                                                                              \
        const void* replacementFn;                                                                                \
        const void* originalFn;                                                                                   \
    } _interpose_##original __attribute__ ((section ("__DATA,__interpose"))) = { (const void*) &replacement,      \
                                                                                  (const void*) &original }

namespace
{
int soakInterposedMutexLock (pthread_mutex_t* mutex)
{
    if (tls_inAudioCallback)
        reportViolation ("lock", 0);
    return pthread_mutex_lock (mutex);
}
} // namespace

EE_DYLD_INTERPOSE (soakInterposedMutexLock, pthread_mutex_lock);
#endif

//==============================================================================
namespace
{
/** Rolling, stopped, looping (with wraparound) or relocating - the shapes
    RegressHarness's FakePlayHead does not cover, because a fixed regression
    battery only ever needs a straight roll and one jump. A tempo- or
    phase-locked engine's alignment code is only reached with a playhead at
    all (see RegressHarness.h), and only fully exercised by one that changes
    shape under it. */
class SoakPlayHead final : public juce::AudioPlayHead
{
public:
    enum class Mode
    {
        rolling,
        stopped,
        looping
    };

    SoakPlayHead (double bpm, double sr) : tempo (bpm), sampleRate (sr) {}

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm (tempo);
        info.setIsPlaying (mode != Mode::stopped);
        info.setPpqPosition (ppq);
        info.setIsLooping (mode == Mode::looping);
        if (mode == Mode::looping)
            info.setLoopPoints (juce::AudioPlayHead::LoopPoints { loopStartPpq, loopStartPpq + loopLengthPpq });
        return info;
    }

    void advance (int numSamples)
    {
        if (mode == Mode::stopped)
            return;

        ppq += (tempo / 60.0) * (static_cast<double> (numSamples) / sampleRate);

        if (mode == Mode::looping && ppq >= loopStartPpq + loopLengthPpq)
            ppq = loopStartPpq + std::fmod (ppq - loopStartPpq, loopLengthPpq);
    }

    void setMode (Mode m) { mode = m; }
    void relocate (double toPpq) { ppq = toPpq; }
    void startLoop (double startPpq, double lengthPpq)
    {
        loopStartPpq = startPpq;
        loopLengthPpq = juce::jmax (0.25, lengthPpq);
        mode = Mode::looping;
    }

private:
    double tempo;
    double sampleRate;
    double ppq = 0.0;
    Mode mode = Mode::rolling;
    double loopStartPpq = 0.0;
    double loopLengthPpq = 4.0;
};

//==============================================================================
struct Config
{
    double durationSeconds = 8.0 * 3600.0;
    uint64_t seed = 1;
    int instances = 1;
    double reportIntervalSeconds = 60.0;
    bool editorCycles = false;
};

/** Read by the owning thread while running, read by the reporter thread while
    it is not - every field here is only ever written by the instance that
    owns it, so std::atomic is for the reporter's cross-thread reads, not for
    mutual exclusion between writers. */
struct InstanceStats
{
    std::atomic<long long> blocks { 0 };
    std::atomic<long long> samples { 0 };
    std::atomic<long long> nonFinite { 0 };
    std::atomic<long long> ceilingHits { 0 };
    std::atomic<long long> reprepares { 0 };
    std::atomic<long long> presetLoads { 0 };
    std::atomic<long long> recreations { 0 };
    std::atomic<long long> editorCycles { 0 };
    std::atomic<bool> alive { true };
};

const double kSampleRates[] = { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 };
const int kMaxBlockChoices[] = { 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192 };
const int kPrimeBlocks[] = { 1, 1, 1, 7, 13, 31, 61, 127, 257, 509, 1021, 2039, 4093, 8191 };

enum class InputMode
{
    noise,
    silence,
    dc,
    burst
};

/** Every parameter moving at once: continuous ones random-walk with an
    occasional hard jump, discrete ones (booleans, engine choices) jump to a
    random step at low probability - the mid-note engine switch the plan asks
    for, without knowing which parameter on which product is the engine
    selector. */
void randomiseParameters (Processor& processor, std::mt19937_64& rng)
{
    std::uniform_real_distribution<double> unit (0.0, 1.0);

    for (auto* parameter : processor.getParameters())
    {
        const bool discrete = parameter->isDiscrete();
        const int steps = parameter->getNumSteps();

        // convertTo0to1 (a native-value -> normalised conversion, needed to
        // land a discrete jump exactly on a step) is on RangedAudioParameter,
        // not the base AudioProcessorParameter getParameters() returns - every
        // parameter here comes from an APVTS layout, so the cast is safe.
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter);

        if (discrete && steps > 1 && ranged != nullptr)
        {
            if (unit (rng) < 0.0006)
                parameter->setValueNotifyingHost (ranged->convertTo0to1 (
                    static_cast<float> (rng() % static_cast<uint64_t> (steps))));
        }
        else
        {
            const float current = parameter->getValue();

            if (unit (rng) < 0.01)
            {
                // A hard jump - a knob grabbed and thrown, not turned.
                parameter->setValueNotifyingHost (static_cast<float> (unit (rng)));
            }
            else
            {
                const float step = static_cast<float> ((unit (rng) - 0.5) * 0.04);
                parameter->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, current + step));
            }
        }
    }
}

void fillInput (juce::AudioBuffer<float>& buffer, InputMode mode, float amplitude, long long sampleIndex,
                double sampleRate, std::mt19937_64& rng)
{
    std::uniform_real_distribution<float> noise (-amplitude, amplitude);
    const int channels = buffer.getNumChannels();
    const int n = buffer.getNumSamples();

    for (int i = 0; i < n; ++i)
    {
        float s = 0.0f;

        switch (mode)
        {
            case InputMode::noise:
                s = noise (rng);
                break;
            case InputMode::silence:
                s = 0.0f;
                break;
            case InputMode::dc:
                s = amplitude;
                break;
            case InputMode::burst:
            {
                const long long into = (sampleIndex + i) % static_cast<long long> (2.0 * sampleRate);
                const double t = static_cast<double> (into) / sampleRate;
                s = t < 0.8 ? amplitude * static_cast<float> (std::exp (-3.0 * t) *
                                                              std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * t))
                            : 0.0f;
                break;
            }
        }

        for (int ch = 0; ch < channels; ++ch)
            buffer.getWritePointer (ch)[i] = s;
    }
}

/** One product instance, driven for the whole run. Runs on the calling
    thread - main() spawns one std::thread per instance when instances > 1,
    or calls this directly for the common instances == 1 case, so a single-
    instance smoke test needs no thread machinery at all. */
void runInstance (int index, const Config& config, InstanceStats& stats, Clock::time_point deadline)
{
    tls_instanceIndex = index;

    std::mt19937_64 rng (config.seed ^ (0x9E3779B97F4A7C15ull * static_cast<uint64_t> (index + 1)));
    std::uniform_real_distribution<double> unit (0.0, 1.0);

    constexpr int channels = 2;

    auto processor = std::make_unique<Processor>();

    double sampleRate = kSampleRates[1]; // 48 kHz
    int maxBlock = 8192;

    auto reprepare = [&]
    {
        processor->setPlayConfigDetails (channels, channels, sampleRate, maxBlock);
        processor->prepareToPlay (sampleRate, maxBlock);
        stats.reprepares.fetch_add (1);
    };
    reprepare();

    auto playHead = std::make_unique<SoakPlayHead> (120.0, sampleRate);
    processor->setPlayHead (playHead.get());

    juce::AudioBuffer<float> buffer (channels, maxBlock);
    juce::MidiBuffer midi;

    InputMode inputMode = InputMode::noise;
    Clock::time_point nextInputSwitch = Clock::now();
    Clock::time_point nextTransportSwitch = Clock::now();
    Clock::time_point nextRecreate = Clock::now() + std::chrono::minutes (5 + index % 7);

    std::unique_ptr<juce::AudioProcessorEditor> editor;
    Clock::time_point nextEditorFlip = Clock::now();
    bool editorOpen = false;

    long long n = 0;

    while (Clock::now() < deadline)
    {
        const auto now = Clock::now();

        // Occasionally rebuild the whole processor in place - a host closing
        // and reopening the plugin without restarting the process.
        if (now >= nextRecreate)
        {
            editor.reset();
            editorOpen = false;
            processor = std::make_unique<Processor>();
            reprepare();
            playHead = std::make_unique<SoakPlayHead> (120.0, sampleRate);
            processor->setPlayHead (playHead.get());
            stats.recreations.fetch_add (1);
            nextRecreate = now + std::chrono::minutes (5 + static_cast<int> (rng() % 10));
        }

        // Occasionally change sample rate and/or maximum block size, the way
        // a host does on a settings change or device switch.
        if (unit (rng) < 0.0004)
        {
            sampleRate = kSampleRates[rng() % (sizeof (kSampleRates) / sizeof (kSampleRates[0]))];
            maxBlock = kMaxBlockChoices[rng() % (sizeof (kMaxBlockChoices) / sizeof (kMaxBlockChoices[0]))];
            buffer.setSize (channels, maxBlock, false, false, true);
            reprepare();
            playHead = std::make_unique<SoakPlayHead> (120.0, sampleRate);
            processor->setPlayHead (playHead.get());
        }

        // Transport mode: rolling, stopped, looping, or a relocate (a scrub to
        // a random position, then straight back to rolling).
        if (now >= nextTransportSwitch)
        {
            switch (rng() % 4)
            {
                case 0:
                    playHead->setMode (SoakPlayHead::Mode::stopped);
                    break;
                case 1:
                    playHead->startLoop (unit (rng) * 16.0, 1.0 + unit (rng) * 15.0);
                    break;
                case 2:
                    playHead->setMode (SoakPlayHead::Mode::rolling);
                    playHead->relocate (unit (rng) * 200.0);
                    break;
                default:
                    playHead->setMode (SoakPlayHead::Mode::rolling);
                    break;
            }

            nextTransportSwitch = now + std::chrono::seconds (2 + static_cast<int> (rng() % 18));
        }

        // Input character.
        if (now >= nextInputSwitch)
        {
            inputMode = static_cast<InputMode> (rng() % 4);
            nextInputSwitch = now + std::chrono::seconds (1 + static_cast<int> (rng() % 9));
        }

        // A preset arriving mid-stream, without pausing anything - see
        // PresetStore::installState's own reasoning in CLAUDE.md.
        if (unit (rng) < 0.00005)
        {
            const auto names = processor->presets.factoryNames();
            if (! names.isEmpty())
            {
                processor->presets.load (ee::plugin::PresetStore::Kind::factory,
                                         names[static_cast<int> (rng() % static_cast<uint64_t> (names.size()))]);
                stats.presetLoads.fetch_add (1);
            }
        }

        // The editor, single-instance only - see the file header for why.
        if (config.editorCycles && now >= nextEditorFlip)
        {
            if (editorOpen)
            {
                editor.reset();
                editorOpen = false;
            }
            else
            {
                editor.reset (processor->createEditor());
                editorOpen = editor != nullptr;
            }

            stats.editorCycles.fetch_add (1);
            nextEditorFlip = now + std::chrono::seconds (3 + static_cast<int> (rng() % 12));
        }

        randomiseParameters (*processor, rng);

        // Block size: mostly a random size up to whatever prepareToPlay was
        // last called with, occasionally forced to the extremes a ragged
        // stream and a single-sample callback are.
        int thisBlock = unit (rng) < 0.05
                            ? kPrimeBlocks[rng() % (sizeof (kPrimeBlocks) / sizeof (kPrimeBlocks[0]))]
                            : 1 + static_cast<int> (rng() % static_cast<uint64_t> (maxBlock));
        thisBlock = juce::jmin (thisBlock, maxBlock);

        buffer.setSize (channels, thisBlock, false, false, true);
        fillInput (buffer, inputMode, 0.15f, n, sampleRate, rng);

        {
            AudioCallbackScope scope (index);
            processor->processBlock (buffer, midi);
        }

        playHead->advance (thisBlock);
        n += thisBlock;

        bool blockHadNonFinite = false;
        bool blockHadCeilingHit = false;

        for (int ch = 0; ch < channels; ++ch)
        {
            const auto* data = buffer.getReadPointer (ch);
            for (int i = 0; i < thisBlock; ++i)
            {
                if (! std::isfinite (data[i]))
                    blockHadNonFinite = true;
                else if (std::abs (data[i]) > kSafetyCeiling)
                    blockHadCeilingHit = true;
            }
        }

        if (blockHadNonFinite)
        {
            const auto count = stats.nonFinite.fetch_add (1) + 1;
            if (count <= 20)
                std::fprintf (stderr,
                             "  *** instance %d: non-finite output at %.1f s (sr %.0f, block %d)\n", index,
                             static_cast<double> (n) / sampleRate, sampleRate, thisBlock);
        }

        if (blockHadCeilingHit)
        {
            const auto count = stats.ceilingHits.fetch_add (1) + 1;
            if (count <= 20)
                std::fprintf (stderr, "  *** instance %d: output over %.1f at %.1f s (sr %.0f, block %d)\n", index,
                             (double) kSafetyCeiling, static_cast<double> (n) / sampleRate, sampleRate, thisBlock);
        }

        stats.blocks.fetch_add (1);
        stats.samples.fetch_add (thisBlock);
    }

    stats.alive.store (false);
}

void reportOnce (const std::vector<std::unique_ptr<InstanceStats>>& stats, Clock::time_point start)
{
    long long blocks = 0, samples = 0, nonFinite = 0, ceilingHits = 0, reprepares = 0, presetLoads = 0, recreations = 0,
             editorCycles = 0;
    int aliveCount = 0;

    for (const auto& s : stats)
    {
        blocks += s->blocks.load();
        samples += s->samples.load();
        nonFinite += s->nonFinite.load();
        ceilingHits += s->ceilingHits.load();
        reprepares += s->reprepares.load();
        presetLoads += s->presetLoads.load();
        recreations += s->recreations.load();
        editorCycles += s->editorCycles.load();
        aliveCount += s->alive.load() ? 1 : 0;
    }

    const double elapsed =
        std::chrono::duration_cast<std::chrono::duration<double>> (Clock::now() - start).count();

    std::printf ("  %8.0f s  %2d/%zu alive  %10lld blocks  %14lld samples  reprepare %lld  preset %lld  "
                 "recreate %lld  editor %lld  alloc-viol %lld  lock-viol %lld  non-finite %lld  over-ceiling %lld\n",
                 elapsed, aliveCount, stats.size(), blocks, samples, reprepares, presetLoads, recreations,
                 editorCycles, gAllocViolations.load(), gLockViolations.load(), nonFinite, ceilingHits);
    std::fflush (stdout);
}

/** Symbolicated only here, at the end of a run - not in the handler itself,
    where allocating the symbol table would be one more violation. */
void printCapturedBacktrace (const char* what, bool captured)
{
    juce::ignoreUnused (what, captured);
#if EE_SOAK_HAVE_BACKTRACE
    if (! captured)
        return;

    const bool isAlloc = std::strcmp (what, "allocation") == 0;
    const auto frames = isAlloc ? gAllocBacktraceFrames : gLockBacktraceFrames;
    auto* const buffer = isAlloc ? gAllocBacktrace : gLockBacktrace;

    std::printf ("\nfirst %s, where it came from:\n", what);
    std::fflush (stdout); // backtrace_symbols_fd writes the fd directly, past stdio's own buffering
    backtrace_symbols_fd (buffer, frames, fileno (stdout));
#endif
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    Config config;
    bool secondsGiven = false;

    for (int i = 1; i < argc; ++i)
    {
        const juce::String arg (argv[i]);
        const auto next = [&] { return i + 1 < argc ? juce::String (argv[++i]) : juce::String(); };

        if (arg == "--hours")
            config.durationSeconds = next().getDoubleValue() * 3600.0;
        else if (arg == "--seconds")
        {
            config.durationSeconds = next().getDoubleValue();
            secondsGiven = true;
        }
        else if (arg == "--seed")
            config.seed = static_cast<uint64_t> (next().getLargeIntValue());
        else if (arg == "--instances")
            config.instances = juce::jmax (1, next().getIntValue());
        else if (arg == "--report-interval")
            config.reportIntervalSeconds = juce::jmax (1.0, next().getDoubleValue());
        else if (arg == "--editor-cycles")
            config.editorCycles = true;
    }

    if (config.editorCycles && config.instances != 1)
    {
        std::printf ("--editor-cycles needs --instances 1 (JUCE components are message-thread only)\n");
        return 1;
    }

    std::printf ("Soak: %s   seed %llu, %d instance%s, %.1f hour%s%s\n", EE_SOAK_NAME,
                 static_cast<unsigned long long> (config.seed), config.instances, config.instances == 1 ? "" : "s",
                 config.durationSeconds / 3600.0, config.durationSeconds == 3600.0 ? "" : "s",
                 secondsGiven ? " (--seconds override)" : "");
    std::printf ("  finite + under %.1f every block, no allocation or lock inside processBlock, "
                 "randomising params/block-size/sample-rate/transport/presets continuously\n\n",
                 (double) kSafetyCeiling);

    const auto start = Clock::now();
    const auto deadline = start + std::chrono::duration_cast<Clock::duration> (
                                       std::chrono::duration<double> (config.durationSeconds));

    std::vector<std::unique_ptr<InstanceStats>> stats;
    for (int i = 0; i < config.instances; ++i)
        stats.push_back (std::make_unique<InstanceStats>());

    std::vector<std::thread> threads;
    if (config.instances > 1)
    {
        for (int i = 0; i < config.instances; ++i)
            threads.emplace_back (runInstance, i, std::cref (config), std::ref (*stats[static_cast<size_t> (i)]), deadline);
    }

    std::atomic<bool> stopReporting { false };
    std::thread reporter (
        [&]
        {
            while (! stopReporting.load())
            {
                std::this_thread::sleep_for (
                    std::chrono::duration<double> (juce::jmin (config.reportIntervalSeconds, 5.0)));

                static auto lastReport = Clock::now();
                if (std::chrono::duration<double> (Clock::now() - lastReport).count() >= config.reportIntervalSeconds)
                {
                    reportOnce (stats, start);
                    lastReport = Clock::now();
                }
            }
        });

    if (config.instances == 1)
        runInstance (0, config, *stats[0], deadline);

    for (auto& t : threads)
        t.join();

    stopReporting.store (true);
    reporter.join();

    reportOnce (stats, start);

    long long totalNonFinite = 0, totalCeiling = 0;
    for (const auto& s : stats)
    {
        totalNonFinite += s->nonFinite.load();
        totalCeiling += s->ceilingHits.load();
    }

    const auto allocViolations = gAllocViolations.load();
    const auto lockViolations = gLockViolations.load();

    printCapturedBacktrace ("allocation", gAllocBacktraceCaptured.load());
    printCapturedBacktrace ("lock", gLockBacktraceCaptured.load());

    std::printf ("\n");

    if (totalNonFinite == 0 && totalCeiling == 0 && allocViolations == 0 && lockViolations == 0)
    {
        std::printf ("SOAK OK - clean for the whole run\n");
        return 0;
    }

    std::printf ("SOAK FAILED - %lld non-finite, %lld over-ceiling, %lld allocation, %lld lock violation%s\n",
                 totalNonFinite, totalCeiling, allocViolations, lockViolations,
                 (totalNonFinite + totalCeiling + allocViolations + lockViolations) == 1 ? "" : "s");
    return 1;
}
