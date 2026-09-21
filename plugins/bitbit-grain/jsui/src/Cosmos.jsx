import { useEffect, useRef } from "react";
import { useJuceSliderValue, useJuceToggleValue } from "@synthpeak/pedal-ui/juce";
import "./Cosmos.css";

/**
 * The cosmos panel: a display-only column at the plate's right edge. Nothing
 * on it is a control.
 *
 * What it draws is the real grain cloud. BitBitGrainWebEditor's timer sends a
 * "grainCosmos" event ~30 times a second carrying every grain the engine has
 * born since the last one - [octaves, pan, level, seconds, backwards, spread] -
 * plus the level of the cloud. Every grain becomes one glow, and each thing
 * about it is a thing about the grain:
 *
 *   Density  -> how many are born per second (each birth flashes and pulses
 *               the core, so the rate is felt as well as counted)
 *   Size     -> the grain's real duration: a 20 ms grain is a small spark that
 *               is gone in a blink, a 1 s grain a big orb that holds its light
 *   Spray    -> (the Stereo parameter) its pan is its left/right position,
 *               exactly: at 0 every grain sits in the middle, turned up they
 *               are thrown to the left and right
 *   Pitch    -> its height, and its colour: low octaves low (indigo), high
 *               octaves high (rose, amber). Unison lives in the 100 px window
 *               either side of the core's light line; each octave up or down
 *               steps out of it
 *   Scatter  -> the height it takes inside that window: at 0 every unison grain
 *               is on the line, turned up they scatter up and down the window
 *   Reverse  -> nothing here: a reversed grain is drawn exactly like any other
 *   Feedback -> a longer afterglow; Freeze turns everything ice-blue
 *
 * Nothing moves when nothing is heard. Grains are only drawn while the cloud is
 * audible, and once everything has faded the scene clock stops and the loop
 * stops redrawing: silence is a still image.
 *
 * Outside a real host (the Vite dev server, the gallery) no event ever
 * arrives, so there - and only there - the panel invents grains from the knobs,
 * otherwise a browser preview would be an empty box. In a host it never does:
 * if the feed is missing the panel says so rather than pretending.
 *
 * All animation state lives in refs and one requestAnimationFrame loop: React
 * renders once, and again only when a knob it reads moves.
 */

// The face's own accents (GrainFace.jsx) plus the deep indigo the low end
// needs. Literal rather than CSS variables: they are used as canvas colours.
const PALETTE_STOPS = [
  { oct: -2.0, rgb: [92, 104, 236] }, // indigo
  { oct: -1.0, rgb: [127, 210, 216] }, // reverb teal
  { oct: 0.0, rgb: [204, 190, 244] }, // lavender-white
  { oct: 1.0, rgb: [231, 143, 179] }, // pitch rose
  { oct: 1.8, rgb: [240, 190, 130] }, // amber
];
const ICE = [176, 232, 255];

const OCT_MIN = PALETTE_STOPS[0].oct;
const OCT_MAX = PALETTE_STOPS[PALETTE_STOPS.length - 1].oct;
const SPRITE_STEPS = 40;
const SPRITE_PX = 64;

const MAX_PARTICLES = 320;

// Half the height of the window unison grains live in, either side of the core.
const BAND = 50;

// How far, up or down, a grain may land from the height its pitch gives it.
const ROW_JITTER = 30;

// The longest, in seconds, any grain is ever drawn for.
const MAX_LIFE = 2.2;
const MAX_SHOCKS = 20;

// The cloud's level (peak, 0..1) below which nothing is audible - no grain is
// drawn and the scene settles. About -50 dB.
const AUDIBLE = 0.003;

// A gap this long between animation frames means the page was not being drawn
// (hidden, or behind another window).
const STALE_MS = 250;

// No event for this long in a real host means the feed is broken or the editor
// is hidden: treat it as silence.
const FEED_TIMEOUT_MS = 500;

const TAU = Math.PI * 2;
const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));
const lerp = (a, b, t) => a + (b - a) * t;
const easeOut = (t) => 1 - (1 - t) * (1 - t) * (1 - t);
const easeIn = (t) => t * t * t;

function paletteAt(oct) {
  const o = clamp(oct, OCT_MIN, OCT_MAX);
  for (let i = 1; i < PALETTE_STOPS.length; i += 1) {
    const b = PALETTE_STOPS[i];
    if (o <= b.oct) {
      const a = PALETTE_STOPS[i - 1];
      const t = (o - a.oct) / (b.oct - a.oct);
      return a.rgb.map((c, k) => lerp(c, b.rgb[k], t));
    }
  }
  return PALETTE_STOPS[PALETTE_STOPS.length - 1].rgb;
}

const rgba = (rgb, a) => `rgba(${rgb[0] | 0},${rgb[1] | 0},${rgb[2] | 0},${a})`;

/** One soft orb per palette step, drawn once and stamped per grain - a fresh
    gradient per grain per frame would be the whole budget. */
function makeSprites() {
  const colours = Array.from({ length: SPRITE_STEPS }, (_, i) =>
    paletteAt(lerp(OCT_MIN, OCT_MAX, i / (SPRITE_STEPS - 1))),
  );
  colours.push(ICE); // the last one, for Freeze

  return colours.map((rgb) => {
    const canvas = document.createElement("canvas");
    canvas.width = SPRITE_PX;
    canvas.height = SPRITE_PX;
    const g = canvas.getContext("2d");
    const half = SPRITE_PX / 2;

    const glow = g.createRadialGradient(half, half, 0, half, half, half);
    glow.addColorStop(0, "rgba(255,255,255,1)");
    glow.addColorStop(0.16, rgba(rgb, 0.95));
    glow.addColorStop(0.45, rgba(rgb, 0.28));
    glow.addColorStop(1, rgba(rgb, 0));
    g.fillStyle = glow;
    g.fillRect(0, 0, SPRITE_PX, SPRITE_PX);
    return canvas;
  });
}

/** Cheap deterministic hash in 0..1, for a lattice dot's fixed jitter. */
function hash(a, b) {
  const s = Math.sin(a * 127.1 + b * 311.7) * 43758.5453;
  return s - Math.floor(s);
}

function buildLattice(w, h) {
  const step = 17;
  const dots = [];
  for (let gy = -1; gy * step < h + step; gy += 1) {
    for (let gx = -1; gx * step < w + step; gx += 1) {
      const jx = (hash(gx, gy) - 0.5) * step * 0.5;
      const jy = (hash(gy + 9.1, gx + 3.7) - 0.5) * step * 0.5;
      dots.push({
        x: gx * step + step / 2 + jx,
        y: gy * step + step / 2 + jy,
        size: 0.5 + hash(gx * 1.7, gy * 2.3) * 0.9,
        base: 0.08 + hash(gx + 41, gy + 17) * 0.3,
        phase: hash(gx, gy + 5) * TAU,
        speed: 0.4 + hash(gy, gx + 8) * 1.4,
      });
    }
  }
  return dots;
}

// Real host or plain browser? A host registers its native functions before any
// page script runs; the dev server's stub does not.
const inHost = () =>
  window.__JUCE__?.initialisationData?.__juce__functions?.includes?.("formatKnobValue") === true;

export default function Cosmos() {
  const canvasRef = useRef(null);
  const countRef = useRef(null);
  const statusRef = useRef(null);
  const pipRef = useRef(null);
  const paramsRef = useRef({});

  // Knobs, all 0..1, read into a ref so the loop is never torn down when one
  // moves. Feedback, the Reverb pair and Freeze shade the scene; the other four
  // only steer the browser-preview stand-in for a host.
  const [feedback] = useJuceSliderValue("feedback");
  const [rmix] = useJuceSliderValue("rmix");
  const [decay] = useJuceSliderValue("decay");
  const [revOn] = useJuceToggleValue("revon", true);
  const [frozen] = useJuceToggleValue("freeze");
  const [density] = useJuceSliderValue("density");
  const [size] = useJuceSliderValue("size");
  const [scatter] = useJuceSliderValue("scatter");
  const [stereo] = useJuceSliderValue("stereo");

  paramsRef.current = { feedback, rmix, decay, revOn, frozen, density, size, scatter, stereo };

  useEffect(() => {
    const canvas = canvasRef.current;
    const ctx = canvas.getContext("2d");
    const sprites = makeSprites();
    const hosted = inHost();

    let width = 0;
    let height = 0;
    let dpr = 1;
    let lattice = [];

    const particles = [];
    const shocks = [];

    const core = { act: 0, pulse: 0, flare: 0 };
    let level = 0; // smoothed, what the core shows
    let hostLevel = 0; // latest from the engine
    let freezeAmt = 0;
    let lastEventAt = -1;
    let births = [];
    let lastHud = 0;
    let lastFrameAt = performance.now();

    // The scene's own clock. It only advances while something is happening, so
    // every time-driven shimmer (star twinkle, nebula drift, the light line's
    // shiver, the ring marker) holds still in silence.
    let sceneTime = 0;
    let wasActive = true;
    let dirty = true;
    let paramKey = "";

    const resize = () => {
      const rect = canvas.getBoundingClientRect();
      dpr = Math.min(2, window.devicePixelRatio || 1);
      width = Math.max(1, Math.round(rect.width));
      height = Math.max(1, Math.round(rect.height));
      canvas.width = Math.round(width * dpr);
      canvas.height = Math.round(height * dpr);
      lattice = buildLattice(width, height);
      dirty = true;
    };

    const geometry = () => ({ cx: width / 2, cy: height / 2, R: Math.min(width, height) * 0.36 });

    // Pixels of height per octave, so the lowest (-2) and highest (+1.8) grains
    // just fit above and below the unison window.
    const octaveStep = () => Math.max(20, (height / 2 - 26 - BAND) / 2);

    /** One grain -> one glow. See the header for what each argument becomes. */
    const spawn = (oct, pan, lvl, seconds, spread) => {
      // A grain born into a silent cloud is not sounding, so it is not shown.
      if (hostLevel < AUDIBLE) return;

      const p = paramsRef.current;
      const { cx, cy } = geometry();
      const dur = clamp(seconds, 0.01, 2);

      // Where it settles: pan across, pitch up and down, Scatter within the window.
      const tx = cx + clamp(pan, -1, 1) * (width / 2 - 24) + (Math.random() - 0.5) * 8;
      // Every grain also lands a random +-30 px off its pitch's height, so grains of
      // one pitch make a loose band rather than a ruled line.
      const ty = cy - oct * octaveStep() + clamp(spread, -1, 1) * BAND + (Math.random() * 2 - 1) * ROW_JITTER;

      // Louder cloud, brighter grains; a quiet one still shows.
      const gate = 0.4 + 0.6 * clamp(hostLevel * 4, 0, 1);
      const bright = clamp((0.45 + 0.35 * clamp(lvl, 0, 1)) * gate, 0, 1);

      // Swell over the first fifth of the grain, hold through its body, then
      // ring out - so a long grain stays lit for as long as it lasts.
      // Lit for about as long as the grain really sounds, then a short afterglow
      // (Feedback lengthens it). Nothing is drawn for longer than MAX_LIFE.
      const vis = Math.max(dur, 0.15);
      const attack = Math.min(0.03, 0.2 * vis);
      const hold = 0.35 * vis;
      const tau = 0.18 * vis + 0.04 * (1 + 2 * p.feedback);

      // Everything else about a grain's motion is along the ray from the core
      // through where it settles.
      const dx = tx - cx;
      const dy = ty - cy;
      const d = Math.hypot(dx, dy);

      if (particles.length >= MAX_PARTICLES) particles.shift();
      particles.push({
        theta: d < 4 ? Math.random() * TAU : Math.atan2(dy, dx),
        r0: d,
        oct,
        sprite: Math.round(((clamp(oct, OCT_MIN, OCT_MAX) - OCT_MIN) / (OCT_MAX - OCT_MIN)) * (SPRITE_STEPS - 1)),
        // Bigger for a longer grain, and bigger the lower it sits: a low grain
        // is a large heavy dot near the bottom, a high one a small bright
        // prick near the top.
        px: (2.6 + 12 * Math.sqrt(clamp(dur, 0.02, 1))) * clamp(1 - 0.27 * oct, 0.5, 1.6),
        bright,
        attack,
        hold,
        tau,
        life: Math.min(MAX_LIFE, hold + 3 * tau),
        age: 0,
      });

      core.pulse = Math.min(1.5, core.pulse + 0.03 + bright * 0.05);
      if (shocks.length >= MAX_SHOCKS) shocks.shift();
      shocks.push({ age: 0, amp: bright * (0.4 + 0.6 * Math.min(1, dur / 0.3)) });
      births.push(performance.now());
    };

    const onCosmos = (payload) => {
      let data = payload;
      if (typeof data === "string") {
        try {
          data = JSON.parse(data);
        } catch {
          return;
        }
      }
      if (!data) return;

      const now = performance.now();
      lastEventAt = now;
      hostLevel = Number.isFinite(data.level) ? data.level : 0;

      // Only draw grains while the page is actually being animated. A window
      // that is hidden or behind another app has its animation paused but keeps
      // receiving events; queueing those up would put hundreds of grains on
      // the panel, all at age zero, the moment it comes back.
      const animating = !document.hidden && now - lastFrameAt < STALE_MS;
      if (animating && Array.isArray(data.grains))
        for (const g of data.grains) spawn(g[0] || 0, g[1] || 0, g[2] || 0, g[3] || 0.1, g[5] || 0);
    };

    const handle = window.__JUCE__?.backend?.addEventListener("grainCosmos", onCosmos);

    // ---- the browser-preview stand-in for a host ---------------------------
    // Uses the same mappings the engine's events would, from the knobs.
    let demoClock = 0;
    let demoAccumulator = 0;
    const runDemo = (dt) => {
      const p = paramsRef.current;
      demoClock += dt;
      hostLevel = 0.32 + 0.22 * Math.sin(demoClock * 0.7) + 0.1 * Math.sin(demoClock * 2.3);

      demoAccumulator += dt * (1 + p.density * 40);
      while (demoAccumulator >= 1) {
        demoAccumulator -= 1;
        const pick = Math.random();
        const oct = pick < 0.5 ? 0 : pick < 0.72 ? -1 : pick < 0.9 ? 1 : pick < 0.96 ? -2 : 1.58;
        const pan = (Math.random() * 2 - 1) * p.stereo;
        const seconds = 0.02 * Math.pow(50, p.size) * (1 + p.scatter * 0.4 * (Math.random() * 2 - 1));
        const spread = p.scatter * (Math.random() * 2 - 1);
        spawn(oct, pan, 0.4 + Math.random() * 0.5, seconds, spread);
      }
    };

    // ---- drawing -----------------------------------------------------------
    const drawNebula = (t, p) => {
      const { cx, cy, R } = geometry();
      const wet = (p.revOn ? 1 : 0.4) * (0.35 + 0.65 * clamp(p.rmix * (0.4 + p.decay), 0, 1));
      const blobs = [
        { x: 0.22, y: 0.24, r: 0.95, rgb: [96, 84, 200], a: 0.16, s: 0.11 },
        { x: 0.82, y: 0.74, r: 0.85, rgb: [70, 170, 190], a: 0.13, s: 0.08 },
        { x: 0.7, y: 0.18, r: 0.6, rgb: [200, 100, 160], a: 0.09, s: 0.14 },
        { x: 0.3, y: 0.86, r: 0.7, rgb: [110, 90, 210], a: 0.1, s: 0.09 },
      ];
      ctx.globalCompositeOperation = "lighter";
      blobs.forEach((b, i) => {
        const bx = width * b.x + Math.sin(t * b.s + i * 2.1) * 26;
        const by = height * b.y + Math.cos(t * b.s * 1.3 + i) * 34;
        const br = Math.max(width, height) * b.r * 0.5;
        const grad = ctx.createRadialGradient(bx, by, 0, bx, by, br);
        const a = b.a * (0.55 + wet * 0.9);
        grad.addColorStop(0, rgba(b.rgb, a));
        grad.addColorStop(1, rgba(b.rgb, 0));
        ctx.fillStyle = grad;
        ctx.fillRect(0, 0, width, height);
      });

      // The halo the core sits in.
      const halo = ctx.createRadialGradient(cx, cy, 0, cx, cy, R * 1.25);
      halo.addColorStop(0, rgba(freezeAmt > 0.5 ? ICE : [190, 170, 255], 0.16 + core.act * 0.22));
      halo.addColorStop(0.5, rgba([120, 110, 220], 0.05 + core.act * 0.06));
      halo.addColorStop(1, "rgba(0,0,0,0)");
      ctx.fillStyle = halo;
      ctx.fillRect(0, 0, width, height);
    };

    const drawLattice = (t) => {
      const { cx, cy, R } = geometry();
      const mass = 0.3 + 0.7 * core.act;

      for (const dot of lattice) {
        let x = dot.x;
        let y = dot.y;
        const dx = cx - x;
        const dy = cy - y;
        const d = Math.hypot(dx, dy) || 1;

        // Sag into the core: strong up close, gone by about one ring away.
        const pull = Math.min(d * 0.55, mass * 30 * Math.exp(-d / (R * 0.9)));
        x += (dx / d) * pull;
        y += (dy / d) * pull;

        let glow = 0;
        for (const s of shocks) {
          const off = d - s.age * 250;
          if (off > -22 && off < 22) {
            const k = Math.exp(-(off * off) / 120) * s.amp * (1 - s.age / 0.9) * 0.9;
            x -= (dx / d) * k * 5;
            y -= (dy / d) * k * 5;
            glow += k;
          }
        }

        const twinkle = 0.65 + 0.35 * Math.sin(t * dot.speed + dot.phase);
        const near = Math.exp(-d / (R * 1.1)) * 0.35 * core.act;
        const a = clamp(dot.base * twinkle + near + glow * 0.9, 0, 1);
        if (a < 0.02) continue;

        ctx.fillStyle =
          glow > 0.08 ? rgba([236, 226, 255], a) : rgba(freezeAmt > 0.5 ? [200, 235, 255] : [214, 220, 250], a);
        const s = dot.size + glow * 1.4;
        ctx.fillRect(x - s / 2, y - s / 2, s, s);
      }
    };

    const drawGuides = () => {
      const { cx, cy, R } = geometry();
      ctx.globalCompositeOperation = "source-over";

      // The hairline ring round the core, indigo -> teal like a portal rim.
      const rim = ctx.createLinearGradient(cx - R, cy - R, cx + R, cy + R);
      rim.addColorStop(0, rgba(freezeAmt > 0.5 ? ICE : [150, 130, 230], 0.85));
      rim.addColorStop(0.5, "rgba(120,120,200,0.25)");
      rim.addColorStop(1, rgba(freezeAmt > 0.5 ? ICE : [110, 210, 200], 0.85));
      ctx.strokeStyle = rim;
      ctx.lineWidth = 1.1;
      ctx.beginPath();
      ctx.arc(cx, cy, R, 0, TAU);
      ctx.stroke();

      // The unison window: where a grain at its own pitch sits, Scatter moving
      // it up and down between the two lines.
      ctx.strokeStyle = "rgba(200,190,240,0.09)";
      ctx.lineWidth = 1;
      ctx.setLineDash([2, 6]);
      ctx.beginPath();
      ctx.moveTo(12, cy - BAND);
      ctx.lineTo(width - 12, cy - BAND);
      ctx.moveTo(12, cy + BAND);
      ctx.lineTo(width - 12, cy + BAND);
      ctx.stroke();
      ctx.setLineDash([]);
    };

    const drawParticles = () => {
      const { cx, cy } = geometry();
      ctx.globalCompositeOperation = "lighter";

      for (const q of particles) {
        const e = q.age < q.attack ? q.age / q.attack : q.age < q.hold ? 1 : Math.exp(-(q.age - q.hold) / q.tau);
        const a = q.bright * e;
        if (a < 0.01) continue;

        // A grain flashes where it sits.
        const x = cx + Math.cos(q.theta) * q.r0;
        const y = cy + Math.sin(q.theta) * q.r0;

        // A soft orb: a touch larger while it is at full strength.
        const glow = q.px * 3.4 * (0.75 + 0.25 * e);

        ctx.globalAlpha = clamp(a * 1.1 * (1 - 0.75 * freezeAmt), 0, 1);
        ctx.drawImage(sprites[q.sprite], x - glow / 2, y - glow / 2, glow, glow);
        if (freezeAmt > 0.01) {
          ctx.globalAlpha = clamp(a * 1.1 * 0.75 * freezeAmt, 0, 1);
          ctx.drawImage(sprites[SPRITE_STEPS], x - glow / 2, y - glow / 2, glow, glow);
        }
        ctx.globalAlpha = 1;
      }
    };

    const drawCore = (t) => {
      const { cx, cy, R } = geometry();
      const act = core.act;
      ctx.globalCompositeOperation = "lighter";

      // The horizontal light line through the core: a string that shivers with
      // the level, pinned at both edges. Dead straight in silence.
      const amp = act * 6 + core.pulse * 3;
      const rgb = freezeAmt > 0.5 ? ICE : [205, 195, 255];
      const makeLine = (peak) => {
        const g = ctx.createLinearGradient(0, 0, width, 0);
        g.addColorStop(0, rgba(rgb, 0));
        g.addColorStop(0.5, rgba(rgb, peak));
        g.addColorStop(1, rgba(rgb, 0));
        return g;
      };
      const glowLine = makeLine(0.16 + act * 0.2);
      const line = makeLine(0.55 + act * 0.4);

      for (let pass = 0; pass < 2; pass += 1) {
        ctx.strokeStyle = pass === 0 ? glowLine : line;
        ctx.lineWidth = pass === 0 ? 5 + act * 6 : 1.1;
        ctx.beginPath();
        const steps = 48;
        for (let i = 0; i <= steps; i += 1) {
          const u = i / steps;
          const pin = Math.sin(u * Math.PI);
          const wob =
            Math.sin(u * 17 + t * 5.5) * 0.6 + Math.sin(u * 41 - t * 8.7) * 0.4 + Math.sin(u * 5 + t * 1.3) * 0.8;
          const y = cy + wob * amp * pin * pin;
          if (i === 0) ctx.moveTo(0, y);
          else ctx.lineTo(u * width, y);
        }
        ctx.stroke();
      }

      // Orb: wide halo, mid bloom, white-hot centre.
      const r1 = 16 + act * 22 + core.pulse * 10 + core.flare * 14;
      const bloom = ctx.createRadialGradient(cx, cy, 0, cx, cy, r1 * 2.6);
      bloom.addColorStop(0, rgba(rgb, 0.5 + act * 0.3));
      bloom.addColorStop(0.35, rgba([160, 140, 240], 0.2 + act * 0.15));
      bloom.addColorStop(1, "rgba(0,0,0,0)");
      ctx.fillStyle = bloom;
      ctx.fillRect(cx - r1 * 3, cy - r1 * 3, r1 * 6, r1 * 6);

      const r2 = 5.5 + act * 4.5 + core.pulse * 2.5 + core.flare * 4;
      const hot = ctx.createRadialGradient(cx, cy, 0, cx, cy, r2 * 1.7);
      hot.addColorStop(0, "rgba(255,255,255,1)");
      hot.addColorStop(0.5, "rgba(255,250,255,0.85)");
      hot.addColorStop(1, "rgba(255,255,255,0)");
      ctx.fillStyle = hot;
      ctx.beginPath();
      ctx.arc(cx, cy, r2 * 1.7, 0, TAU);
      ctx.fill();

      // A four-point flare that opens with the pulse.
      const spike = R * (0.16 + core.pulse * 0.16 + act * 0.1);
      ctx.lineWidth = 1;
      for (const [sx, sy, o] of [
        [1, 0, 0.5],
        [0, 1, 0.28],
      ]) {
        const g = ctx.createLinearGradient(cx - sx * spike, cy - sy * spike, cx + sx * spike, cy + sy * spike);
        g.addColorStop(0, rgba(rgb, 0));
        g.addColorStop(0.5, rgba(rgb, o + core.pulse * 0.3));
        g.addColorStop(1, rgba(rgb, 0));
        ctx.strokeStyle = g;
        ctx.beginPath();
        ctx.moveTo(cx - sx * spike, cy - sy * spike);
        ctx.lineTo(cx + sx * spike, cy + sy * spike);
        ctx.stroke();
      }
    };

    // ---- the loop ----------------------------------------------------------
    let raf = 0;
    let last = performance.now();

    // Throw away everything in flight: it belongs to a moment nobody saw.
    const clearScene = () => {
      particles.length = 0;
      shocks.length = 0;
      births = [];
      core.pulse = 0;
      core.flare = 0;
      dirty = true;
    };

    const onVisibility = () => {
      lastFrameAt = performance.now();
      clearScene();
    };
    document.addEventListener("visibilitychange", onVisibility);

    const frame = (now) => {
      raf = requestAnimationFrame(frame);
      lastFrameAt = now;
      if (width === 0) return;

      // The loop was paused (window hidden, behind another app): whatever was in
      // flight is stale, and its clock did not advance, so drop it rather than
      // resume a frozen crowd.
      if (now - last > STALE_MS) clearScene();

      const dt = Math.min(0.05, (now - last) / 1000);
      last = now;
      const p = paramsRef.current;

      const feedFresh = now - lastEventAt < FEED_TIMEOUT_MS;
      if (!hosted) runDemo(dt);
      else if (!feedFresh) hostLevel = 0;

      // A knob that shades the still picture moved: redraw once even in silence.
      const key = `${p.rmix}|${p.decay}|${p.revOn}|${p.frozen}`;
      if (key !== paramKey) {
        paramKey = key;
        dirty = true;
      }

      const audible = hostLevel >= AUDIBLE;
      const freezeTarget = p.frozen ? 1 : 0;
      const moving =
        audible ||
        particles.length > 0 ||
        shocks.length > 0 ||
        core.act > 0.004 ||
        core.pulse > 0.004 ||
        core.flare > 0.004 ||
        Math.abs(freezeAmt - freezeTarget) > 0.002;

      // Silence is a still image: once it has been drawn, do nothing at all.
      const needDraw = moving || wasActive || dirty;
      wasActive = moving;
      dirty = false;

      if (moving) {
        // Freeze tints to ice, smoothly both ways. It does not slow anything: a
        // grain is drawn for as long as it sounds, frozen buffer or not.
        freezeAmt += (freezeTarget - freezeAmt) * Math.min(1, dt * 3.5);
        if (Math.abs(freezeAmt - freezeTarget) < 0.002) freezeAmt = freezeTarget;
        const sdt = dt;
        sceneTime += sdt;

        // Level as seen: instant up, eased down, and snapped to rest.
        const target = audible ? clamp(hostLevel * 1.6, 0, 1) : 0;
        level += (target - level) * Math.min(1, dt * (target > level ? 14 : 3.2));
        const want = audible ? Math.min(1, level + core.pulse * 0.35) : 0;
        core.act += (want - core.act) * Math.min(1, dt * 6);
        if (!audible && core.act < 0.004) core.act = 0;
        core.pulse *= Math.exp(-dt * 6);
        core.flare *= Math.exp(-dt * 4);
        if (core.pulse < 0.004) core.pulse = 0;
        if (core.flare < 0.004) core.flare = 0;

        for (let i = shocks.length - 1; i >= 0; i -= 1) {
          shocks[i].age += sdt;
          if (shocks[i].age > 0.9) shocks.splice(i, 1);
        }

        for (let i = particles.length - 1; i >= 0; i -= 1) {
          const q = particles[i];
          q.age += sdt;
          if (q.age >= q.life) {
            particles.splice(i, 1);
          }
        }
      }

      if (needDraw) {
        ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
        ctx.globalCompositeOperation = "source-over";
        ctx.clearRect(0, 0, width, height);

        drawNebula(sceneTime, p);
        drawLattice(sceneTime);
        drawGuides();
        drawParticles();
        drawCore(sceneTime);
        ctx.globalCompositeOperation = "source-over";
      }

      // The text and the pip: a few times a second.
      if (now - lastHud > 250) {
        lastHud = now;
        births = births.filter((b) => now - b < 1000);
        if (countRef.current) countRef.current.textContent = String(births.length).padStart(2, "0");
        if (statusRef.current) statusRef.current.textContent = !hosted ? "demo" : feedFresh ? "" : "no feed";
        if (pipRef.current) pipRef.current.dataset.live = audible ? "1" : "";
      }
    };

    resize();
    const observer = new ResizeObserver(resize);
    observer.observe(canvas);
    raf = requestAnimationFrame(frame);

    return () => {
      cancelAnimationFrame(raf);
      document.removeEventListener("visibilitychange", onVisibility);
      observer.disconnect();
      if (handle) window.__JUCE__.backend.removeEventListener(handle);
    };
  }, []);

  return (
    <section className="pg-cosmos" aria-hidden="true" data-frozen={frozen || undefined}>
      <div className="pg-cosmos__display">
        <canvas ref={canvasRef} className="pg-cosmos__canvas" />
        <span className="pg-cosmos__label pg-cosmos__label--tl">Field</span>
        <span ref={statusRef} className="pg-cosmos__label pg-cosmos__label--tr" />
        <span className="pg-cosmos__label pg-cosmos__label--bl">
          <b ref={countRef}>00</b> grains/s
        </span>
        <span ref={pipRef} className="pg-cosmos__pip" />
      </div>
    </section>
  );
}
