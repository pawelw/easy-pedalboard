import { useMemo } from "react";
import "./CrushScope.css";

/**
 * The Amp engine's display: a reference sine put through the Amp's own drive
 * curve and then held by its Bit knob, in the order ee::fx::ArtifactModule
 * runs them. The clean sine is drawn faintly behind, so what Drive does to the
 * wave reads as the gap between the two.
 *
 * The drive is ee::dsp::TubeDrive's transfer, ported by hand - the constants
 * below MUST match ee/dsp/TubeDriveConfig.h:
 *
 *   gain   kDriveMinGain -> kDriveMaxGain, walked by voicingFor (kDriveSkew)
 *   pre    the pre-emphasis shelf's lift at the drawn tone's frequency (its
 *          pole walks kPreShelfZeroHz -> kPreShelfPoleHz, as in updateDrive)
 *   curve  asinh, the negative side softened by kNegScale
 *   bias   kBias x voicing, with the resting offset taken back out
 *   DC     the blocker after it, as the wave's mean removed
 *
 * So the picture is honest about the things the ear hears in it: Drive 0 is an
 * untouched sine (the engine is a bit-exact pass-through there), turning it up
 * rounds the peaks into a logarithmic squash rather than a hard clip, and the
 * bias makes the two halves squash differently - the asymmetry that is the
 * even-harmonic part of the sound. The level make-up and the de-emphasis shelf
 * are left out: the trace is scaled to fill the well whatever the drive,
 * because the make-up exists precisely so the knob does not change the volume,
 * and a filter's tilt on the harmonics is not something a two-cycle trace can
 * show. Mids and Tone come after the crush and are equalisers, so they are not
 * in the picture either.
 *
 * Bit is the sample-and-hold of ArtifactModule::ampRateHzFor: it holds N
 * samples, N running 1 -> 32 at 48 kHz as (1500/48000)^(bit^kAmpCrushRateSkew).
 * The trace is SAMPLES_ACROSS samples wide, so the step count is the engine's
 * own ratio - a 600 Hz tone at 48 kHz.
 */

// ee/dsp/TubeDriveConfig.h
const DRIVE_MIN_GAIN = 0.25;
const DRIVE_MAX_GAIN = 19.9;
const DRIVE_SKEW = 0.6;
const PRE_SHELF_ZERO_HZ = 164.9;
const PRE_SHELF_POLE_HZ = 1618.0;
const BIAS = -0.461;
const NEG_SCALE = 0.961;

// ee::fx::ArtifactModule
const CRUSH_RATE_HZ = 1500;
const CRUSH_RATE_SKEW = 1.1;

const HOST_RATE = 48000;
const TONE_HZ = 600;
const CYCLES = 2;
const SAMPLES_ACROSS = (HOST_RATE / TONE_HZ) * CYCLES; // 160

// The reference tone's peak, ~-10 dBFS: a played note, not a full-scale test
// tone, so the lower half of the Drive knob is where it starts to bend.
const INPUT_PEAK = 0.3;

const VIEW_WIDTH = 320;
const PAD = 6;

function clamp(v, lo, hi) {
  return Math.max(lo, Math.min(hi, Number.isFinite(v) ? v : lo));
}

function curve(v) {
  return v >= 0 ? Math.asinh(v) : -NEG_SCALE * Math.asinh(-v / NEG_SCALE);
}

/** The drive's transfer at one knob position, as a function of the input. */
function driveTransfer(drive01) {
  if (drive01 <= 0) return (x) => x;

  const t = Math.pow(drive01, DRIVE_SKEW);
  const gain = DRIVE_MIN_GAIN * Math.pow(DRIVE_MAX_GAIN / DRIVE_MIN_GAIN, t);
  const bias = BIAS * t;
  const rest = curve(bias);

  // |(1 + jf/fz) / (1 + jf/fp)| at the drawn tone - what the pre-emphasis
  // does to a sine's level on its way into the curve.
  const poleHz = PRE_SHELF_ZERO_HZ * Math.pow(PRE_SHELF_POLE_HZ / PRE_SHELF_ZERO_HZ, t);
  const pre = Math.hypot(1, TONE_HZ / PRE_SHELF_ZERO_HZ) / Math.hypot(1, TONE_HZ / poleHz);

  return (x) => curve(gain * pre * x + bias) - rest;
}

export default function AmpScope({
  drive01 = 0,
  bit01 = 0,
  height = 64,
  baseColor = "var(--pui-scope-base)",
  fillColor = "var(--pui-scope-fill)",
}) {
  const plot = { x: PAD, top: PAD, bottom: height - PAD, w: VIEW_WIDTH - PAD * 2 };
  const midY = (plot.top + plot.bottom) / 2;
  const ampY = (plot.bottom - plot.top) / 2;

  const drive = clamp(drive01, 0, 1);
  const bit = clamp(bit01, 0, 1);

  const { line, area, clean } = useMemo(() => {
    const shape = driveTransfer(drive);

    // One value per host sample across the well, driven, DC-blocked (the
    // mean over whole cycles is exactly what the blocker takes out) and
    // scaled so the larger half just fills it.
    const raw = Array.from({ length: SAMPLES_ACROSS + 1 }, (_, i) =>
      shape(INPUT_PEAK * Math.sin((2 * Math.PI * CYCLES * i) / SAMPLES_ACROSS)),
    );
    const mean = raw.slice(0, SAMPLES_ACROSS).reduce((a, b) => a + b, 0) / SAMPLES_ACROSS;
    const peak = Math.max(1e-9, ...raw.map((v) => Math.abs(v - mean)));
    const wave = raw.map((v) => (v - mean) / peak);

    const xFor = (i) => plot.x + (i / SAMPLES_ACROSS) * plot.w;
    const yFor = (v) => midY - clamp(v, -1, 1) * ampY;

    // The hold: N host samples per step, N = host / rate.
    const rateHz = HOST_RATE * Math.pow(CRUSH_RATE_HZ / HOST_RATE, Math.pow(bit, CRUSH_RATE_SKEW));
    const hold = HOST_RATE / rateHz;

    let d;
    if (hold < 1.5) {
      d = wave.map((v, i) => `${i === 0 ? "M" : "L"} ${xFor(i).toFixed(1)} ${yFor(v).toFixed(1)}`).join(" ");
    } else {
      d = `M ${xFor(0).toFixed(1)} ${yFor(wave[0]).toFixed(1)}`;
      for (let s = 0; s < SAMPLES_ACROSS; s += hold) {
        const v = wave[Math.floor(s)];
        const end = Math.min(SAMPLES_ACROSS, s + hold);
        d += ` L ${xFor(s).toFixed(1)} ${yFor(v).toFixed(1)}`;
        d += ` L ${xFor(end).toFixed(1)} ${yFor(v).toFixed(1)}`;
      }
    }

    let c = "";
    for (let i = 0; i <= SAMPLES_ACROSS; i++) {
      const v = Math.sin((2 * Math.PI * CYCLES * i) / SAMPLES_ACROSS);
      c += `${i === 0 ? "M" : "L"} ${xFor(i).toFixed(1)} ${yFor(v).toFixed(1)} `;
    }

    const filled = `${d} L ${(plot.x + plot.w).toFixed(1)} ${midY} L ${plot.x.toFixed(1)} ${midY} Z`;
    return { line: d, area: filled, clean: c };
  }, [drive, bit, midY, ampY, plot.x, plot.w]);

  return (
    <div className="pui-scope pui-crush-scope" style={{ height }}>
      <svg className="pui-scope__svg" viewBox={`0 0 ${VIEW_WIDTH} ${height}`} preserveAspectRatio="none">
        <line
          x1={plot.x}
          y1={midY}
          x2={plot.x + plot.w}
          y2={midY}
          stroke="var(--pui-scope-grid)"
          strokeWidth="1"
        />
        <path d={clean} fill="none" stroke={baseColor} strokeWidth="1" opacity="0.55" strokeDasharray="3 3" />
        <path d={area} fill={fillColor} stroke="none" />
        <path d={line} fill="none" stroke={baseColor} strokeWidth="2" strokeLinejoin="round" />
      </svg>
    </div>
  );
}
