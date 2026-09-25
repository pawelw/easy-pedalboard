import { useEffect, useRef } from "react";
import "./CompScope.css";

/**
 * The Comp engine's display, after Ableton's Compressor: a scrolling history of
 * the input as a grey filled envelope, the gain reduction as a line hanging
 * from the top, and the threshold as a line across. A live readout of the
 * current reduction sits top left. No switches - the GR line is always on and
 * there is no output trace.
 *
 * Unlike the other scopes this one is a live feed, not a picture of the knobs.
 * `subscribe(onData)` is how it gets one: it is called once on mount with a
 * callback and returns an unsubscribe. Each call of the callback carries
 *
 *   { pts: [in, out, gr, in, out, gr, ...],  // linear peaks, GR in dB (<= 0)
 *     period: seconds per point,
 *     thr: the threshold on the input, dB }
 *
 * which is exactly the processor's "compMeter" event (ee::plugin::CompMeterFeed)
 * - see `useJuceCompMeter` in the JUCE entry point. `demoCompFeed` below is a
 * synthetic one for the gallery and the dev server, which have no processor.
 *
 * The points arrive in bursts at the editor's timer rate; the drawing scrolls
 * on its own clock at the audio's pace and only lets the newest points in as it
 * reaches them, so the trace glides rather than stepping 45 times a second.
 *
 * Canvas rather than SVG: a few hundred columns redrawn every frame.
 */

const HISTORY_SECONDS = 3;
// One dB scale for everything: 0 dB at the top, FLOOR_DB at the bottom. The
// envelopes are levels on it (dBFS); the GR line is a reduction hanging from
// the top on the same ruler, so 12 dB of GR reaches the -12 grid line.
const FLOOR_DB = -54;
// The strip along the top that carries the readout - its own
// band, as Ableton's header is, so the GR line never runs through the text.
const HEADER_PX = 12;
const GRID_DB = [-12, -24, -36, -48];
const MAX_POINTS = 2048;

function clamp(v, lo, hi) {
  return Math.max(lo, Math.min(hi, v));
}

function toDb(linear) {
  return linear > 1e-6 ? 20 * Math.log10(linear) : -120;
}

function gainReductionText(db) {
  if (db > -0.05) return "0.0";
  return db.toFixed(1);
}

export default function CompScope({
  subscribe,
  height = 64,
  inputColor = "rgba(170, 170, 176, 0.27)",
  grColor = "#c9c5c5",
  thresholdColor = "#e5504e",
}) {
  const wrapRef = useRef(null);
  const canvasRef = useRef(null);
  const readoutRef = useRef(null);

  // What the draw loop reads - kept out of React state so 45 events a second
  // do not re-render the face.
  const feed = useRef({
    pts: new Float32Array(MAX_POINTS * 3),
    received: 0, // points ever received
    head: 0, // points scrolled into view (fractional)
    period: 0.005,
    thr: -27,
    lastGr: 0,
  });

  useEffect(() => {
    if (typeof subscribe !== "function") return undefined;

    return subscribe((event) => {
      const f = feed.current;
      const pts = event?.pts;
      if (Array.isArray(pts) || ArrayBuffer.isView(pts)) {
        for (let i = 0; i + 2 < pts.length; i += 3) {
          const slot = (f.received % MAX_POINTS) * 3;
          f.pts[slot] = pts[i];
          f.pts[slot + 1] = pts[i + 1];
          f.pts[slot + 2] = pts[i + 2];
          f.received += 1;
          f.lastGr = pts[i + 2];
        }
      }
      if (Number.isFinite(event?.period) && event.period > 0) f.period = event.period;
      if (Number.isFinite(event?.thr)) f.thr = event.thr;
    });
  }, [subscribe]);

  useEffect(() => {
    const canvas = canvasRef.current;
    const wrap = wrapRef.current;
    if (!canvas || !wrap) return undefined;

    const ctx = canvas.getContext("2d");
    let raf = 0;
    let last = performance.now();
    let cssWidth = 0;
    let gridColor = "rgba(255,255,255,0.08)";

    const resize = () => {
      const dpr = window.devicePixelRatio || 1;
      cssWidth = wrap.clientWidth;
      canvas.width = Math.max(1, Math.round(cssWidth * dpr));
      canvas.height = Math.max(1, Math.round(height * dpr));
      canvas.style.height = `${height}px`;
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
      gridColor = getComputedStyle(wrap).getPropertyValue("--pui-scope-grid").trim() || gridColor;
    };
    resize();
    const observer = new ResizeObserver(resize);
    observer.observe(wrap);

    const yForDb = (db) => HEADER_PX + (clamp(db, FLOOR_DB, 0) / FLOOR_DB) * (height - HEADER_PX);

    const draw = (now) => {
      raf = requestAnimationFrame(draw);
      const f = feed.current;
      const dt = Math.min(0.1, (now - last) / 1000);
      last = now;

      // Scroll at the audio's pace, never past what has arrived, and never
      // more than a few timer ticks behind it.
      f.head = clamp(f.head + dt / f.period, f.received - 24, f.received);

      const w = cssWidth;
      ctx.clearRect(0, 0, w, height);
      if (w <= 0) return;

      ctx.fillStyle = "rgba(0, 0, 0, 0.28)";
      ctx.fillRect(0, 0, w, HEADER_PX);

      ctx.lineWidth = 1;
      ctx.strokeStyle = gridColor;
      ctx.beginPath();
      for (const db of GRID_DB) {
        const y = Math.round(yForDb(db)) + 0.5;
        ctx.moveTo(0, y);
        ctx.lineTo(w, y);
      }
      ctx.stroke();

      const span = HISTORY_SECONDS / f.period; // points across the width
      const perPx = span / w;
      const newest = Math.floor(f.head);
      const oldestKept = Math.max(0, f.received - MAX_POINTS);

      // One column per pixel: the loudest input / output and the deepest GR
      // among the points that land in it.
      const columns = Math.ceil(w);
      const inY = new Float32Array(columns);
      const grY = new Float32Array(columns);
      const has = new Uint8Array(columns);

      for (let x = 0; x < columns; x++) {
        const hi = newest - Math.floor((columns - 1 - x) * perPx);
        const lo = Math.max(oldestKept, hi - Math.max(1, Math.ceil(perPx)));
        let inPk = 0;
        let gr = 0;
        let any = false;
        for (let n = lo; n < hi; n++) {
          if (n < 0) continue;
          const slot = (n % MAX_POINTS) * 3;
          inPk = Math.max(inPk, f.pts[slot]);
          gr = Math.min(gr, f.pts[slot + 2]);
          any = true;
        }
        has[x] = any ? 1 : 0;
        inY[x] = yForDb(toDb(inPk));
        grY[x] = yForDb(gr);
      }

      // Input: filled from the floor.
      ctx.fillStyle = inputColor;
      ctx.beginPath();
      ctx.moveTo(0, height);
      for (let x = 0; x < columns; x++) ctx.lineTo(x, has[x] ? inY[x] : height);
      ctx.lineTo(w, height);
      ctx.closePath();
      ctx.fill();

      // Threshold, across the whole history.
      const ty = Math.round(yForDb(f.thr)) + 0.5;
      ctx.strokeStyle = thresholdColor;
      ctx.lineWidth = 1.5;
      ctx.beginPath();
      ctx.moveTo(0, ty);
      ctx.lineTo(w, ty);
      ctx.stroke();

      ctx.strokeStyle = grColor;
      ctx.lineWidth = 1.5;
      ctx.lineJoin = "round";
      ctx.beginPath();
      for (let x = 0; x < columns; x++) {
        const y = Math.max(HEADER_PX + 0.75, has[x] ? grY[x] : HEADER_PX);
        if (x === 0) ctx.moveTo(x, y);
        else ctx.lineTo(x, y);
      }
      ctx.stroke();

      if (readoutRef.current) readoutRef.current.textContent = gainReductionText(f.lastGr);
    };
    raf = requestAnimationFrame(draw);

    return () => {
      cancelAnimationFrame(raf);
      observer.disconnect();
    };
  }, [height, inputColor, grColor, thresholdColor]);

  return (
    <div ref={wrapRef} className="pui-scope pui-comp-scope" style={{ height }}>
      <canvas ref={canvasRef} className="pui-comp-scope__canvas" />
      <div className="pui-comp-scope__readout" style={{ color: grColor }}>
        <span ref={readoutRef}>0.0</span>
        <span className="pui-comp-scope__unit">dB</span>
      </div>
    </div>
  );
}

/**
 * A stand-in for the processor's feed, for the gallery and the dev server:
 * plucked notes at varying strength every half second or so, through a rough
 * copy of the engine's curve and ballistics. Same `subscribe` shape CompScope
 * takes. Not a model of anything - just enough movement to design against.
 */
export function demoCompFeed(onData) {
  const period = 0.005;
  const thr = -27;
  const sustainDb = 15;
  let t = 0;
  let noteStart = 0;
  let nextNote = 0;
  let noteAmp = 0.5;
  let gr = 0;
  const attack = 1 - Math.exp(-period / 0.005);
  const release = 1 - Math.exp(-period / 0.15);

  const curve = (levelDb) => {
    const over = levelDb - (thr + sustainDb);
    const knee = 12;
    const slope = 1 / 6 - 1;
    if (2 * over <= -knee) return 0;
    if (2 * over >= knee) return slope * over;
    const k = over + knee / 2;
    return (slope * k * k) / (2 * knee);
  };

  const timer = setInterval(() => {
    const pts = [];
    for (let i = 0; i < 4; i++) {
      t += period;
      if (t >= nextNote) {
        noteStart = t;
        nextNote = t + 0.35 + Math.random() * 0.45;
        noteAmp = 0.04 + Math.random() * 0.4;
      }
      const env = noteAmp * Math.exp(-(t - noteStart) / 0.22) * (0.8 + 0.2 * Math.random());
      const target = curve(toDb(env) + sustainDb);
      gr += (target < gr ? attack : release) * (target - gr);
      const out = env * Math.pow(10, (sustainDb + gr) / 20) * 0.5;
      pts.push(env, Math.min(1, out), gr);
    }
    onData({ pts, period, thr });
  }, 20);

  return () => clearInterval(timer);
}
