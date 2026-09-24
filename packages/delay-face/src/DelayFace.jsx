import {
  StageGroup,
  StageHeader,
  TapScope,
  LinkIcon,
  TapeIcon,
  ModIcon,
  FilterIcon,
} from "@synthpeak/pedal-ui";
import {
  JuceKnob,
  JucePill,
  JuceChoicePill,
  ParamScope,
} from "@synthpeak/pedal-ui/juce";
import { useDelayMeter, useDelayTimesMs } from "./juceBindings.jsx";
import { useJuceSliderValue } from "@synthpeak/pedal-ui/juce";
import TimeControl from "./TimeControl.jsx";
import "./DelayFace.css";

/**
 * BitBit Delay's face, minus its enclosure: the tap scope, the Mix/Feedback/time
 * row, and the three-cell stage footer. **No Card and no header** - the host
 * supplies those, because the two hosts want different ones. BitBit Delay wraps
 * this in a Card with its title, preset bar and level faders; BitBit Alpine
 * drops it into a `ModulePanel`.
 *
 * One component, not two looks. The whole reason this package exists is that
 * the Delay module in a multi-effect host is not a re-draw of BitBit Delay's
 * face, it *is* BitBit Delay's face - so a fix lands in both, and neither can
 * drift.
 *
 * `prefix` is the parameter-id prefix its controls bind through: "" for BitBit
 * Delay, whose parameters are plain (`mix`, `ltime`), and "dly." for BitBit
 * Alpine, whose are namespaced by module. Nothing below here takes an id map;
 * the `ParamScope` around it does the whole job. The leaf names are identical
 * in both plugins on purpose - that is what makes one prefix enough.
 *
 * The face is a fragment, not a wrapper div. The host's own body element is
 * the only box these three children need, and adding another would put a
 * second layout box between the card's padding and the content that has been
 * measured against it since the face was written.
 *
 * `stageKnobSize` is the footer knobs' dial, 38px - the size of the Mix and
 * Tone knobs in every other module's footer. The footer knobs are the same
 * `JuceKnob` those footers draw, in the same `knobVariant` ("flat" in every
 * host today), so a row of modules has one kind of footer knob. All six, Low
 * Cut and High Cut included, are this size.
 *
 * `mainKnobSize` is Mix and Feedback. 76px is what BitBit Delay's original
 * 528px card was laid out around; its 415px card passes 50 and BitBit Alpine's
 * 490px module passes 60.
 *
 * `scopeHeight` is the tap scope's well. BitBit Delay's own 110px is the
 * default; BitBit Alpine passes the height that takes the well from the top of
 * its neighbours' engine stepper to the bottom of their display, so the three
 * modules read as one row rather than as three stacks of their own.
 */
export default function DelayFace({
  prefix = "",
  stageKnobSize = 38,
  knobVariant = "flat",
  mainKnobSize = 76,
  scopeHeight = 110,
}) {
  return (
    <ParamScope prefix={prefix}>
      <DelayFaceBody
        stageKnobSize={stageKnobSize}
        knobVariant={knobVariant}
        mainKnobSize={mainKnobSize}
        scopeHeight={scopeHeight}
      />
    </ParamScope>
  );
}

/** Split out so its hooks resolve *inside* the ParamScope above - a hook in
    DelayFace itself would read the enclosing scope, not the one it declares. */
function DelayFaceBody({ stageKnobSize, knobVariant, mainKnobSize = 76, scopeHeight = 110 }) {
  const [leftMs, rightMs] = useDelayTimesMs();
  const [feedback01] = useJuceSliderValue("fb");
  const [mix01] = useJuceSliderValue("mix");
  const { strikes, level } = useDelayMeter();

  return (
    <>
      <TapScope
        height={scopeHeight}
        leftMs={leftMs}
        rightMs={rightMs}
        feedback01={feedback01}
        mix01={mix01}
        strikes={strikes}
        level={level}
      />

      <div className="pd-row">
        {/* sweepGap pinned to the 42px Time knobs' own value explicitly,
            rather than relying on Knob.jsx's size-based default, so the ring's
            distance from a neighbour is the same whatever `mainKnobSize` is
            (it is `gap` alone, independent of the knob's own diameter). */}
        {/* showValueLabel={false}: the value already has a permanent home
            below the caption (showValueBelow), so the caption swapping to a
            second copy of the same text mid-drag is redundant - Mix should
            keep reading "Mix" the whole time. */}
        <JuceKnob
          parameterId="mix"
          caption="Mix"
          variant="scale"
          size={mainKnobSize}
          sweepGap={4}
          showValueBelow
          showValueLabel={false}
        />
        <JuceKnob
          parameterId="fb"
          caption="Feedback"
          variant="scale"
          size={mainKnobSize}
          sweepGap={4}
          showValueBelow
          showValueLabel={false}
        />

        <div className="pd-time-col">
          {/* Linked is a bracket joining the two Time rows rather than one
              more pill in the row below: what it does is tie these two
              knobs together, so it reads better drawn as the tie itself.
              The two arms are pure decoration (see .pd-link in DelayFace.css);
              only the button in the middle is a control. */}
          <div className="pd-link-group">
            <div className="pd-link">
              <span className="pd-link__arm pd-link__arm--top" />
              <span className="pd-link__arm pd-link__arm--bottom" />
              <div className="pd-link__button">
                <JucePill parameterId="sync" icon={<LinkIcon size={15} />} />
              </div>
            </div>

            <div className="pd-time-rows">
              <TimeControl side="L" parameterId="ltime" />
              <TimeControl side="R" parameterId="rtime" />
            </div>
          </div>

          <div className="pd-pills">
            {/* "timeunit": false = note division, true = ms (unchanged -
                see PluginProcessor.h). Lit means "synced to tempo", i.e.
                timeunit is *false*, hence invert - the parameter's own
                sense is "is this in ms mode", not "is this synced". */}
            <JucePill parameterId="timeunit" label="Sync" invert />

            {/* How the two delay lines are wired: Normal is the stereo pair
                the pedal has always been, Wide spreads the repeats across
                the field, Ping Pong bounces them between the sides. The
                labels are in the parameter's index order and must stay in
                step with kTypeID's choices in PluginProcessor.cpp. */}
            <JuceChoicePill parameterId="dtype" labels={["Normal", "Wide", "Ping Pong"]} />
          </div>
        </div>
      </div>

      {/* The footer's three sections. They bleed out to the host's left, right
          and bottom edge - see .pd-footer in DelayFace.css for how, and why
          the bleed lives there rather than inside StageGroup.

          Each names itself with a coloured glyph and is otherwise identical to
          its neighbours: one ground, one set of knobs. Tape used to sit on a
          green band with green knobs of its own, which said Tape was the odd
          one out and left Mod and Filter with nothing separating them; three
          marks distinguish three sections, which is what the footer actually
          needs.

          None of them has a router. Tape used to carry a Pre/Post one; the tape
          is on the repeats now and nowhere else, which is what took the delay's
          latency to nothing. Mod's Drift is the delay line's own modulation,
          inside the feedback loop where it compounds with every repeat - it
          is not before or after the delay, it is part of it; the Phaser is
          fixed on the repeats. Filter is fixed on the repeats too: the in-loop,
          compounding version of a tone control is Drift, and a second one
          would only have blurred the first. "tape"/"mod" are the parameter
          ids Wear and Drift kept from the single-knob face. */}
      <div className="pd-footer">
        <div className="pd-footer__section">
          <StageGroup
            header={
              <StageHeader icon={<TapeIcon size={24} />} name="Tape" accent="var(--pui-stage-tape)" />
            }
          >
            <JuceKnob parameterId="tape" caption="Wear" variant={knobVariant} size={stageKnobSize} />
            <JuceKnob parameterId="flutter" caption="Flutter" variant={knobVariant} size={stageKnobSize} />
          </StageGroup>
        </div>

        <div className="pd-footer__section">
          <StageGroup
            header={<StageHeader icon={<ModIcon size={30} />} name="Mod" accent="var(--pui-stage-mod)" />}
          >
            <JuceKnob parameterId="mod" caption="Drift" variant={knobVariant} size={stageKnobSize} />
            <JuceKnob parameterId="phaser" caption="Phaser" variant={knobVariant} size={stageKnobSize} />
          </StageGroup>
        </div>

        {/* High rests wide open at the top of its travel and counts down
            from there, so its scale fills from the maximum end - the same
            distinction BitBit EQ draws with an inverted arc on its High Cut.
            Low is an ordinary knob: it rests at 0 Hz and fills as it opens.
            Same size as the Tape and Mod knobs beside them: they were 8px
            smaller (a cut as a supporting knob, the way SideModule.jsx's
            reverb/mod engines still draw their own locut/hicut pair), which
            left the Filter section's knobs looking undersized in a footer of
            equals. Only the Delay's cuts moved; those engines are untouched. */}
        <div className="pd-footer__section">
          <StageGroup
            header={<StageHeader icon={<FilterIcon size={30} />} name="Filter" accent="var(--pui-stage-filter)" />}
          >
            <JuceKnob parameterId="locut" caption="Low Cut" variant={knobVariant} size={stageKnobSize} />
            <JuceKnob parameterId="hicut" caption="High Cut" scaleFrom="max" variant={knobVariant} size={stageKnobSize} />
          </StageGroup>
        </div>
      </div>
    </>
  );
}
