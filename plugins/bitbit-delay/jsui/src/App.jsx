import { useEffect } from "react";
import { Card, JucePresetBar } from "@synthpeak/pedal-ui";
import { installResizableFace } from "@synthpeak/pedal-ui/juce";
import { DelayFace } from "@synthpeak/delay-face";
import "./index.css";

/**
 * BitBit Delay's enclosure. The controls themselves are `DelayFace`, the
 * component BitBit Alpine's Delay module renders too - so this file is only
 * what is *this pedal's*: its card, its title and its preset bar. The In/Out
 * level faders that used to sit in the header's right-hand slot are Alpine's
 * chrome now - the whole-instrument trim, not this pedal's to repeat - so
 * there is no headerRight here any more (see .pd-card's header-left override).
 *
 * The parameters are unprefixed here (`mix`, `ltime`), which is DelayFace's
 * default, so nothing is passed.
 */
export default function App() {
  useEffect(() => installResizableFace(), []);

  return (
    <div className="page">
      {/* theme="onyx" on the Delay face only - see main.jsx. Wah never
          passes a theme, so none of this reaches it (packages/pedal-ui/src/
          tokens.css's [data-pui-theme="onyx"] block). */}
      {/* 526px is the width at which this card's content box is the 452px of
          BitBit Alpine's 490px Delay module - the card's own 26px sides and
          .pui-card__body's 10px, on top of it. The face is the same component
          in both, so it is drawn at the same size in both; at 415 it was the
          same controls laid out smaller, which read as a second face.

          That width is also what lets the preset bar share the title's row
          again (see .pd-card's header-left override for how it lands right
          rather than centred); at 415 it had to take one of its own. */}
      <Card
        title="BitBit Delay"
        headerCenter={<JucePresetBar variant="separated" showDice={false} />}
        className="pd-card"
        width={526}
      >
        <DelayFace />
      </Card>
    </div>
  );
}
