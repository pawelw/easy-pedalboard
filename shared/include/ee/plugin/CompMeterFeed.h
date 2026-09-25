#pragma once

#include <juce_core/juce_core.h>

#include "ee/dsp/Compressor.h"

namespace ee::plugin
{

/** The Artifact Comp display's feed, editor side - BitBit Artifact's and BitBit
 *  Alpine's editors both own one and call `poll` from their timer.
 *
 *  It hands the page every meter point the compressor has written since the
 *  last poll, as one "compMeter" event:
 *
 *    { pts: [in, out, gr, in, out, gr, ...],   // linear peaks, GR in dB
 *      period: seconds per point,
 *      thr: the input threshold in dB }        // where the knee sits, for the line
 *
 *  Flat rather than an array of objects: a poll is a handful of points, but the
 *  var tree is built on the message thread 30-60 times a second.
 *
 *  `poll` returns a void var when there is nothing to send - the editor is not
 *  showing, or the Comp engine is not selected - and in that case skips its
 *  cursor to the present, so a display that comes back does not get a backlog
 *  replayed at it all at once.
 */
class CompMeterFeed
{
public:
    /** At most this many points in one event. A poll that has fallen further
        behind than this (the host stalled the message thread) skips ahead. */
    static constexpr uint32_t kMaxPointsPerPoll = 64;

    juce::var poll (const ee::dsp::Compressor& comp, bool wanted, float inputThresholdDb)
    {
        const uint32_t count = comp.meterCount();

        if (! wanted)
        {
            last = count;
            return {};
        }

        if (count - last > kMaxPointsPerPoll)
            last = count - kMaxPointsPerPoll;

        juce::Array<juce::var> pts;
        pts.ensureStorageAllocated (static_cast<int> ((count - last) * 3));

        for (; last != count; ++last)
        {
            const auto p = comp.meterPointAt (last);
            pts.add (p.inPeak);
            pts.add (p.outPeak);
            pts.add (p.grDb);
        }

        auto* payload = new juce::DynamicObject();
        payload->setProperty ("pts", pts);
        payload->setProperty ("period", comp.meterPeriodSeconds());
        payload->setProperty ("thr", inputThresholdDb);
        return juce::var (payload);
    }

private:
    uint32_t last = 0;
};

} // namespace ee::plugin
