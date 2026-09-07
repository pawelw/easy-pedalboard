import { useEffect } from "react";
import {
  Card,
  PresetBar,
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
  JuceFader,
  JucePill,
  JuceStageKnob,
  JuceStageRouter,
  useDelayMeter,
  useDelayTimesMs,
  useJuceSliderValue,
} from "./juceBindings.jsx";
import TimeControl from "./TimeControl.jsx";
import { installAutoResize } from "./autoSize.js";
import "./index.css";

/** The two level faders, stacked in the header's right-hand slot. They
    replaced a line of text that repeated what the face already said twice
    over - "STEREO · 1/8 · 1/8T", the same two readouts that sit beside the
    Time knobs - with the one pair of controls the pedal was missing. */
function HeaderLevels() {
  return (
    <div className="pd-levels">
      <JuceFader parameterId="ingain" label="In" />
      <JuceFader parameterId="outgain" label="Out" />
    </div>
  );
}

export default function App() {
  useEffect(() => installAutoResize(), []);

  const [leftMs, rightMs] = useDelayTimesMs();
  const [feedback01] = useJuceSliderValue("fb");
  const [mix01] = useJuceSliderValue("mix");
  const { strikes, level } = useDelayMeter();

  return (
    <div className="page">
      {/* theme="onyx" on the Delay face only - see main.jsx. Wah never
          passes a theme, so none of this reaches it (packages/pedal-ui/src/
          tokens.css's [data-pui-theme="onyx"] block). */}
      {/* 568 + the link bracket's own column - see --pd-link-col. */}
      <Card
        title="Peak Delay"
        headerCenter={<PresetBar />}
        headerRight={<HeaderLevels />}
        className="pd-card"
        width={626}
      >
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
                The two arms are pure decoration (see .pd-link in index.css);
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
                <TimeControl side="Left" parameterId="ltime" />
                <TimeControl side="Right" parameterId="rtime" />
              </div>
            </div>

            <div className="pd-pills">
              {/* "timeunit": false = note division, true = ms (unchanged -
                  see PluginProcessor.h). Lit means "synced to tempo", i.e.
                  timeunit is *false*, hence invert - the parameter's own
                  sense is "is this in ms mode", not "is this synced". */}
              <JucePill parameterId="timeunit" label="Sync" invert />
            </div>
          </div>
        </div>

        {/* The footer's three sections. The tape one full-bleeds its green
            band out to the card's left and bottom edge - see .pd-footer in
            index.css for how, and why the bleed lives here rather than inside
            StageGroup.

            Only Tape carries a router, because it is the only section with a
            choice to make. Mod's Drift is the delay line's own modulation,
            inside the feedback loop where it compounds with every repeat - it
            is not before or after the delay, it is part of it, so a Pre/Post
            there would have governed only half the section; the Phaser is fixed
            after the delay. Filter is fixed on the repeats: the in-loop,
            compounding version of a tone control is Drift, and a second one
            would only have blurred the first. "tape"/"mod" are the parameter
            ids Wear and Drift kept from the single-knob face. */}
        <div className="pd-footer">
          <div className="pd-footer__section pd-footer__section--tape">
            <StageGroup
              tone="tape"
              header={
                <StageHeader icon={<TapeIcon size={30} />} name="Tape">
                  <JuceStageRouter parameterId="tapepre" label="Tape" labels={["Post", "Pre"]} />
                </StageHeader>
              }
            >
              <JuceStageKnob parameterId="tape" name="Wear" />
              <JuceStageKnob parameterId="flutter" name="Flutter" />
            </StageGroup>
          </div>

          <div className="pd-footer__section">
            <StageGroup header={<StageHeader icon={<ModIcon size={30} />} name="Mod" />}>
              <JuceStageKnob parameterId="mod" name="Drift" />
              <JuceStageKnob parameterId="phaser" name="Phaser" />
            </StageGroup>
          </div>

          {/* High rests wide open at the top of its travel and counts down
              from there, so its scale fills from the maximum end - the same
              distinction Peak EQ draws with an inverted arc on its High Cut.
              Low is an ordinary knob: it rests at 0 Hz and fills as it opens. */}
          <div className="pd-footer__section">
            <StageGroup header={<StageHeader icon={<FilterIcon size={30} />} name="Filter" />}>
              <JuceStageKnob parameterId="locut" name="Low Cut" />
              <JuceStageKnob parameterId="hicut" name="High Cut" scaleFrom="max" />
            </StageGroup>
          </div>
        </div>
      </Card>
    </div>
  );
}
