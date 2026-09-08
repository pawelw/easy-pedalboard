import PeakWahFace from "peak-wah-jsui/src/App.jsx";
import PeakDelayFace from "peak-delay-jsui/src/App.jsx";
import PeakAlpineFace from "peak-alpine-jsui/src/App.jsx";

// One entry per pedal in plugins/. `face` is the pedal's own real App.jsx,
// imported straight from its jsui project - nothing here re-implements a
// pedal's UI, so there's nothing to keep in sync by hand. Add a pedal here
// once its jsui exists; leave `face: null` for one that doesn't yet.
//
// `theme` picks the palette this page renders that face in - App.jsx is
// imported directly here, bypassing the pedal's own main.jsx entirely, so
// this is the one piece of per-pedal metadata the gallery has to carry
// rather than reuse. Omit it for the default light theme. It mirrors what the
// pedal's main.jsx passes to PedalUIProvider, so what the gallery shows is
// what the plugin ships - Delay was deliberately off that for a while, drawn
// in grey while its onyx face was still being worked out.
export const pedals = [
  // The multi-effect host, not a pedal - listed first because it is the one
  // that contains the others rather than sitting beside them.
  { slug: "peak-alpine", name: "Peak Alpine", face: PeakAlpineFace, theme: "onyx" },

  { slug: "peak-chorus", name: "Peak Chorus", face: null },
  { slug: "peak-delay", name: "Peak Delay", face: PeakDelayFace, theme: "onyx" },
  { slug: "peak-eq", name: "Peak EQ", face: null },
  { slug: "peak-grain", name: "Peak Grain", face: null },
  { slug: "peak-overdrive", name: "Peak Overdrive", face: null },
  { slug: "peak-phase", name: "Peak Phase", face: null },
  { slug: "peak-reverb", name: "Peak Reverb", face: null },
  { slug: "peak-spring", name: "Peak Spring", face: null },
  { slug: "peak-tape", name: "Peak Tape", face: null },
  { slug: "peak-trem-pan", name: "Peak Trem-Pan", face: null },
  { slug: "peak-wah", name: "Peak Wah", face: PeakWahFace },
];
