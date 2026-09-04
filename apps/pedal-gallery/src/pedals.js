import PeakWahFace from "peak-wah-jsui/src/App.jsx";

// One entry per pedal in plugins/. `face` is the pedal's own real App.jsx,
// imported straight from its jsui project - nothing here re-implements a
// pedal's UI, so there's nothing to keep in sync by hand. Add a pedal here
// once its jsui exists; leave `face: null` for one that doesn't yet.
export const pedals = [
  { slug: "peak-chorus", name: "Peak Chorus", face: null },
  { slug: "peak-delay", name: "Peak Delay", face: null },
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
