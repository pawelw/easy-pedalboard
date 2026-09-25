#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "ee/plugin/SafeParse.h"

#include <atomic>
#include <functional>

/**
 * BitBit Alpine's pre-EQ as one setting for the whole machine rather than one
 * per instance - the way a separate EQ app in front of the pedal would be.
 *
 * The `eq.*` parameters stay ordinary APVTS parameters (the dialog binds them,
 * a host can automate them), but what they hold is the user's, not the
 * session's or the preset's:
 *
 * - **On disk**, one small file (`defaultFile()`), rewritten half a second
 *   after the last change to any of them - but only a change made while
 *   `shouldRemember` says so, which the processor answers with "the editor is
 *   open". What is remembered is what somebody dialled in; a validator
 *   (auval, pluginval), a host's plugin scan, or automation playing with the
 *   window shut all move these parameters too, and none of that is theirs.
 * - **A new instance** reads it, so it opens on the last EQ that was set in
 *   any instance.
 * - **Loading a preset or reopening a session** leaves the EQ as it is:
 *   `keepCurrent` rewrites the incoming tree's `eq.*` values to the ones in
 *   force before it is installed. Both still *write* them - harmless, and a
 *   state from before this is read the same way.
 * - **Another open instance** changing it is picked up by the same timer, off
 *   the file's modification time, so two Alpines in one set stay in step.
 *
 * Only a processor built by a real plugin wrapper attaches one (see the
 * processor's constructor). The offline tools instantiate the processor
 * directly, and a regression checksum must never depend on what somebody last
 * dialled into the EQ on this machine.
 */
class GlobalEq : private juce::Timer, private juce::AudioProcessorValueTreeState::Listener
{
public:
    explicit GlobalEq (juce::AudioProcessorValueTreeState& state) : apvts (state)
    {
        for (auto* p : apvts.processor.getParameters())
            if (auto* withId = dynamic_cast<juce::RangedAudioParameter*> (p))
                if (withId->getParameterID().startsWith (kPrefix))
                    params.add (withId);
    }

    ~GlobalEq() override
    {
        stopTimer();
        if (attached)
        {
            if (dirty.exchange (false) && remember())
                save();
            for (auto* p : params)
                apvts.removeParameterListener (p->getParameterID(), this);
        }
    }

    static juce::File defaultFile()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
            .getChildFile ("BitBit/BitBit Alpine/PreEQ.xml");
    }

    /** Message thread, once. Loads `f` into the parameters and from then on
        keeps the two in step. */
    void attach (const juce::File& f)
    {
        if (attached)
            return;

        file = f;
        load();

        for (auto* p : params)
            apvts.addParameterListener (p->getParameterID(), this);

        attached = true;
        startTimer (kPollMs);
    }

    bool isAttached() const noexcept { return attached; }

    /** `tree` with every `eq.*` value replaced by the one in force now. A
        parameter the tree does not carry is left out - APVTS keeps its current
        value for those anyway. Identity when not attached. */
    juce::ValueTree keepCurrent (const juce::ValueTree& tree) const
    {
        if (! attached)
            return tree;

        auto copy = tree.createCopy();
        for (auto child : copy)
        {
            const auto id = child.getProperty ("id").toString();
            if (! id.startsWith (kPrefix))
                continue;

            if (auto* p = apvts.getParameter (id))
                child.setProperty ("value", p->convertFrom0to1 (p->getValue()), nullptr);
        }
        return copy;
    }

    /** Whether a change is the user's to keep - see the class note. Unset,
        every change is. */
    std::function<bool()> shouldRemember;

    /** Message thread: write any pending change now rather than at the next
        tick, whatever shouldRemember says - the editor calls it as it closes,
        so the last half-second of turning a knob is not lost with it. */
    void flush()
    {
        if (dirty.exchange (false))
            save();
    }

private:
    static constexpr const char* kPrefix = "eq.";
    static constexpr const char* kTag = "PreEQ";
    static constexpr int kPollMs = 500;

    void parameterChanged (const juce::String&, float) override
    {
        // Any thread - a host's automation arrives on the audio one. Only a
        // flag here; the file is written from the timer.
        if (! applying.load (std::memory_order_relaxed))
            dirty.store (true, std::memory_order_relaxed);
    }

    void timerCallback() override
    {
        if (dirty.exchange (false))
        {
            if (remember())
                save();
            return;
        }

        // Another instance wrote it.
        if (file.existsAsFile() && file.getLastModificationTime() != lastSeen)
            load();
    }

    bool remember() const { return ! shouldRemember || shouldRemember(); }

    void save()
    {
        juce::XmlElement xml (kTag);
        xml.setAttribute ("stateVersion", 1);
        for (auto* p : params)
        {
            auto* e = xml.createNewChildElement ("PARAM");
            e->setAttribute ("id", p->getParameterID());
            e->setAttribute ("value", p->convertFrom0to1 (p->getValue()));
        }

        file.getParentDirectory().createDirectory();
        if (xml.writeTo (file)) // through a temporary file, so a reader never sees half of it
            lastSeen = file.getLastModificationTime();
    }

    void load()
    {
        lastSeen = file.getLastModificationTime();

        const auto xml = ee::plugin::parseXmlFile (file);
        if (xml == nullptr || ! xml->hasTagName (kTag))
            return;

        applying.store (true, std::memory_order_relaxed);
        for (auto* e : xml->getChildWithTagNameIterator ("PARAM"))
        {
            auto* p = apvts.getParameter (e->getStringAttribute ("id"));
            if (p == nullptr || ! params.contains (p))
                continue;

            const double v = e->getDoubleAttribute ("value", std::numeric_limits<double>::quiet_NaN());
            if (! std::isfinite (v))
                continue;

            const float normalised = p->convertTo0to1 (static_cast<float> (v)); // clamps to the range
            if (std::abs (normalised - p->getValue()) > 1.0e-6f)
                p->setValueNotifyingHost (normalised);
        }
        applying.store (false, std::memory_order_relaxed);
    }

    juce::AudioProcessorValueTreeState& apvts;
    juce::Array<juce::RangedAudioParameter*> params;
    juce::File file;
    juce::Time lastSeen;
    bool attached = false;
    std::atomic<bool> dirty { false };
    std::atomic<bool> applying { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlobalEq)
};
