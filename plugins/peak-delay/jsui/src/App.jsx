import { useEffect } from "react";
import { Card, TapScope, LinkIcon, TapeIcon, ModIcon } from "@synthpeak/pedal-ui";
import {
  JuceKnob,
  JucePill,
  JuceStageControl,
  useDelayTimesMs,
  useJuceSliderValue,
  useTimeReadoutText,
} from "./juceBindings.jsx";
import TimeControl from "./TimeControl.jsx";
import { installAutoResize } from "./autoSize.js";
import "./index.css";

/** "STEREO · 1/8 · 1/8T" - the same toggle-aware text each Time row's own
    Readout shows for its value, just concatenated into the header's meta
    line rather than fetched a third time some other way. */
function HeaderMeta() {
  const [leftText] = useTimeReadoutText("ltime");
  const [rightText] = useTimeReadoutText("rtime");
  return <span className="pd-meta">Stereo · {leftText} · {rightText}</span>;
}

export default function App() {
  useEffect(() => installAutoResize(), []);

  const [leftMs, rightMs] = useDelayTimesMs();
  const [feedback01] = useJuceSliderValue("fb");
  const [mix01] = useJuceSliderValue("mix");

  return (
    <div className="page">
      {/* theme="onyx" on the Delay face only - see main.jsx. Wah never
          passes a theme, so none of this reaches it (packages/pedal-ui/src/
          tokens.css's [data-pui-theme="onyx"] block). */}
      {/* 568 + the link bracket's own column - see --pd-link-col. */}
      <Card title="Peak Delay" headerRight={<HeaderMeta />} className="pd-card" width={626}>
        <TapScope height={78} leftMs={leftMs} rightMs={rightMs} feedback01={feedback01} mix01={mix01} />

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
            sweepGap={6}
            showValueBelow
            showValueLabel={false}
          />
          <JuceKnob
            parameterId="fb"
            caption="Feedback"
            variant="scale"
            size={76}
            sweepGap={6}
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

        {/* The footer's two halves, split down the middle. The tape half
            full-bleeds its green band out to the card's left and bottom
            edge - see .pd-footer in index.css for how, and why the bleed
            lives here rather than inside StageControl. */}
        <div className="pd-footer">
          <div className="pd-footer__half pd-footer__half--tape">
            <JuceStageControl
              parameterId="tape"
              label="Pre-stage"
              name="Tape"
              tone="tape"
              icon={<TapeIcon />}
            />
          </div>

          <div className="pd-footer__half pd-footer__half--mode">
            <JuceStageControl parameterId="mod" label="Post-stage" name="Mod" icon={<ModIcon />} />
          </div>
        </div>
      </Card>
    </div>
  );
}
