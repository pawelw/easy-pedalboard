#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace ee::plugin
{

/** One "drag the LFO onto a knob" assignment: which parameter, and how much
    of the LFO's -1..1 swing to add on top of it (bipolar - a negative depth
    inverts the LFO on that target). */
struct ModAssignment
{
    juce::String paramID;
    float depth = 0.0f;
};

/** The Mod tab's modulation-routing table: which parameters an LFO is
    currently assigned to, and how deep. Message-thread writes (a knob drop,
    a depth edit, a preset load), audio-thread reads every chunk - guarded by
    a juce::SpinLock rather than a lock-free atomic swap, the same reason and
    the same pattern ee::dsp::BreakpointLfo's own breakpoint list uses (this
    toolchain's libc++ has no std::atomic<std::shared_ptr<T>> to reach for
    instead). The critical section is a linear scan of at most
    kMaxAssignments short strings, never audio math, so a rare message-thread
    write contending with it costs the audio thread nothing worth measuring. */
class ModRouter
{
public:
    static constexpr int kMaxAssignments = 8;

    /** Message-thread only. Silently drops anything past kMaxAssignments
        rather than refusing the whole set - a JSON blob from a stale preset
        or a future version with more slots should still load as much of
        itself as fits. */
    void setAssignments (std::vector<ModAssignment> next)
    {
        if (next.size() > static_cast<size_t> (kMaxAssignments))
            next.resize (static_cast<size_t> (kMaxAssignments));

        const juce::SpinLock::ScopedLockType lock (assignmentsLock);
        assignments = std::move (next);
    }

    /** For the UI - the routing bar's own picture of what is currently
        assigned. */
    std::vector<ModAssignment> currentAssignments() const
    {
        const juce::SpinLock::ScopedLockType lock (assignmentsLock);
        return assignments;
    }

    /** The cheap audio-thread fast path: most blocks have nothing assigned
        at all, and this is one bool check rather than a per-parameter scan. */
    bool hasAssignments() const noexcept
    {
        const juce::SpinLock::ScopedLockType lock (assignmentsLock);
        return ! assignments.empty();
    }

    /** 0 when `paramID` has no assignment - which is also a correct "no
        modulation" answer on its own, so a caller does not need a separate
        isAssigned() check before using this. */
    float depthFor (const juce::String& paramID) const noexcept
    {
        const juce::SpinLock::ScopedLockType lock (assignmentsLock);
        for (const auto& a : assignments)
            if (a.paramID == paramID)
                return a.depth;
        return 0.0f;
    }

private:
    mutable juce::SpinLock assignmentsLock;
    std::vector<ModAssignment> assignments;
};

} // namespace ee::plugin
