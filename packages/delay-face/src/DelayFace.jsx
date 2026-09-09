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
  JuceStageKnob,
  JuceStageRouter,
  ParamScope,
} from "@synthpeak/pedal-ui/juce";
import { useDelayMeter, useDelayTimesMs } from "./juceBindings.jsx";
import { useJuceSliderValue } from "@synthpeak/pedal-ui/juce";
import TimeControl from "./TimeControl.jsx";
import "./DelayFace.css";

/**
 * Peak Delay's face, minus its enclosure: the tap scope, the Mix/Feedback/time
 * row, and the three-cell stage footer. **No Card and no header** - the host
 * supplies those, because the two hosts want different ones. Peak Delay wraps
 * this in a Card with its title, preset bar and level faders; Peak Alpine
 * drops it into a `ModulePanel`.
 *
 * One component, not two looks. The whole reason this package exists is that
 * the Delay module in a multi-effect host is not a re-draw of Peak Delay's
 * face, it *is* Peak Delay's face - so a fix lands in both, and neither can
 * drift.
 *
 * `prefix` is the parameter-id prefix its controls bind through: "" for Peak
 * Delay, whose parameters are plain (`mix`, `ltime`), and "dly." for Peak
 * Machine, whose are namespaced by module. Nothing below here takes an id map;
 * the `ParamScope` around it does the whole job. The leaf names are identical
 * in both plugins on purpose - that is what makes one prefix enough.
 *
 * The face is a fragment, not a wrapper div. The host's own body element is
 * the only box these three children need, and adding another would put a
 * second layout box between the card's padding and the content that has been
 * measured against it since the face was written.
 */
export default function DelayFace({ prefix = "" }) {
  return (
    <ParamScope prefix={prefix}>
      <DelayFaceBody />
    </ParamScope>
  );
}

/** Split out so its hooks resolve *inside* the ParamScope above - a hook in
    DelayFace itself would read the enclosing scope, not the one it declares. */
function DelayFaceBody() {
  const [leftMs, rightMs] = useDelayTimesMs();
  const [feedback01] = useJuceSliderValue("fb");
  const [mix01] = useJuceSliderValue("mix");
  const { strikes, level } = useDelayMeter();

  return (
    <>
      <TapScope
        height={78}
        leftMs={leftMs}
        rightMs={rightMs}
        feedback01={feedback01}
        mix01={mix01}
        strikes={strikes}
        level={level}
      />

      <div className="pd-row">
        {/* sweepGap pinned to the 42px Time knobs' own value explicitly,
            rather than relying on Knob.jsx's size-based default (which
            would put an 84px+ knob in the "big" bucket and its ring - a
            wider gap there - would then sit visibly closer to the scope
            above than Left/Right Time's despite an identical row
            layout). The ring's distance from a neighbour is `gap` alone,
            independent of the knob's own diameter, so this keeps that
            distance identical while leaving the size free to be whatever
            reads best here. */}
        {/* showValueLabel={false}: the value already has a permanent home
            below the caption (showValueBelow) now, so caption swapping to
            a second copy of the same text mid-drag is redundant - Mix
            should keep reading "Mix" the whole time. */}
        <JuceKnob
          parameterId="mix"
          caption="Mix"
          variant="scale"
          size={76}
          sweepGap={4}
          showValueBelow
          showValueLabel={false}
        />
        <JuceKnob
          parameterId="fb"
          caption="Feedback"
          variant="scale"
          size={76}
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

          Only Tape carries a router, because it is the only section with a
          choice to make. Mod's Drift is the delay line's own modulation,
          inside the feedback loop where it compounds with every repeat - it
          is not before or after the delay, it is part of it, so a Pre/Post
          there would have governed only half the section; the Phaser is fixed
          on the repeats. Filter is fixed on the repeats too: the in-loop,
          compounding version of a tone control is Drift, and a second one
          would only have blurred the first. "tape"/"mod" are the parameter
          ids Wear and Drift kept from the single-knob face. */}
      <div className="pd-footer">
        <div className="pd-footer__section">
          <StageGroup
            header={
              <StageHeader icon={<TapeIcon size={30} />} name="Tape" accent="var(--pui-stage-tape)">
                <JuceStageRouter parameterId="tapepre" label="Tape" labels={["Post", "Pre"]} />
              </StageHeader>
            }
          >
            <JuceStageKnob parameterId="tape" name="Wear" />
            <JuceStageKnob parameterId="flutter" name="Flutter" />
          </StageGroup>
        </div>

        <div className="pd-footer__section">
          <StageGroup
            header={<StageHeader icon={<ModIcon size={30} />} name="Mod" accent="var(--pui-stage-mod)" />}
          >
            <JuceStageKnob parameterId="mod" name="Drift" />
            <JuceStageKnob parameterId="phaser" name="Phaser" />
          </StageGroup>
        </div>

        {/* High rests wide open at the top of its travel and counts down
            from there, so its scale fills from the maximum end - the same
            distinction Peak EQ draws with an inverted arc on its High Cut.
            Low is an ordinary knob: it rests at 0 Hz and fills as it opens. */}
        <div className="pd-footer__section">
          <StageGroup
            header={<StageHeader icon={<FilterIcon size={30} />} name="Filter" accent="var(--pui-stage-filter)" />}
          >
            <JuceStageKnob parameterId="locut" name="Low Cut" />
            <JuceStageKnob parameterId="hicut" name="High Cut" scaleFrom="max" />
          </StageGroup>
        </div>
      </div>
    </>
  );
}
