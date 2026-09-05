// Constants ported from shared/include/ee/dsp/AutoWahConfig.h - keep these
// numbers in sync by hand if that header's tuning changes (same arrangement
// as lfo.js for the LFO shape, for the same reason: a scope drawn from
// different numbers than the DSP uses is a picture of the wrong filter).
export const FREQ_MIN_HZ = 200;
export const FREQ_MAX_HZ = 1600;
export const FREQ_KNOB_SKEW = 0.8;
export const SWEEP_RATIO_MAX = 5.0;

/** The Freq knob's 0..1 position to the Hz it actually sets - matches
    freqHzFor() in plugins/peak-wah/src/PluginProcessor.cpp. */
export function freqHzFor01(freq01) {
  const t = Math.pow(Math.max(0, Math.min(1, freq01)), FREQ_KNOB_SKEW);
  return FREQ_MIN_HZ * Math.pow(FREQ_MAX_HZ / FREQ_MIN_HZ, t);
}
