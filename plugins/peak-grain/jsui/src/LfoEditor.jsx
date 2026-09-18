import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import * as Juce from "juce-framework-frontend";
import { Knob, Pill, Dropdown, PowerToggle } from "@synthpeak/pedal-ui";
import { useFormattedText, useJuceSliderValue, useJuceToggleValue, useParamId } from "@synthpeak/pedal-ui/juce";
import { evalBreakpoints, kMaxBreakpoints, LFO_PRESETS, parseBreakpointsJson, toBreakpointsJson } from "./lfoShapes.js";
import ModSourceChip from "./ModSourceChip.jsx";
import { useSetLfoValue } from "./LfoPlayback.jsx";
import "./LfoEditor.css";

// Resolved once at module scope, the same way packages/pedal-ui/src/juce.jsx
// resolves "formatKnobValue" - a dev-server preview with no JUCE backend
// behind it (see the project's own note on this) just never resolves these,
// which is why every read below has a "leave the screen alone" fallback
// rather than assuming an answer is coming.
const lfoGetBreakpoints = Juce.getNativeFunction("lfoGetBreakpoints");
const lfoSetBreakpoints = Juce.getNativeFunction("lfoSetBreakpoints");

// The graph's own coordinate space tracks the SVG's actual rendered size
// (via a ResizeObserver, see useGraphSize below) rather than a fixed
// viewBox - a fixed viewBox stretched to fill a differently-shaped box would
// scale x and y by different factors, and a breakpoint's circle marker (one
// radius, both axes) would draw as an ellipse. Matching the viewBox to the
// real box 1:1 means no scaling ever happens, so a circle stays a circle.
const DEFAULT_SIZE = { width: 480, height: 150 };
const MARGIN_Y = 12;
const CURVE_STEPS = 200;
// How close two points may sit in phase - keeps a segment from collapsing to
// zero width (a div-by-zero in evalBreakpoints's segmentValue) and keeps a
// dragged point from ever passing a neighbour, which would silently reorder
// the list mid-drag.
const MIN_POINT_SPACING = 0.012;
// How far the pointer may move between down and up on empty graph space and
// still count as "click to add a point" rather than an aborted drag -
// deliberately not using the browser's own click event for this (see
// handleBackgroundPointerDown's note).
const CLICK_MOVE_THRESHOLD = 4;

// A stable identity per point, independent of its position in the sorted
// array - a point's sorted rank (and so its array index) can change as
// points are added/removed/dragged past each other, and keying off that
// index made React reconcile the wrong DOM node to the wrong point on
// occasion. Point data crossing into JSON (lfoShapes.js, and later the C++
// bridge) never carries this - it is stripped there and is a pure UI concern.
let nextPointId = 0;
function makePointId() {
  return `bp-${nextPointId++}`;
}
function withIds(points) {
  return points.map((point) => ({ ...point, id: makePointId() }));
}

/** The SVG's own rendered size in CSS pixels, kept in sync via
    ResizeObserver - read with getBoundingClientRect rather than trusting
    ResizeObserver's own contentRect, so this always agrees exactly with
    what svgPointFromEvent divides by for pointer conversion. */
function useGraphSize(ref) {
  const [size, setSize] = useState(DEFAULT_SIZE);

  useEffect(() => {
    const el = ref.current;
    if (!el) return undefined;

    const measure = () => {
      const rect = el.getBoundingClientRect();
      if (rect.width > 0 && rect.height > 0) setSize({ width: rect.width, height: rect.height });
    };

    measure();
    const observer = new ResizeObserver(measure);
    observer.observe(el);
    return () => observer.disconnect();
  }, [ref]);

  return size;
}

/** The Mod tab's body: the breakpoint graph (click empty space to add a
    point, drag any point, double-click to remove one) and its toolbar - a
    Rate knob, a Sync toggle and the waveform-preset dropdown. Rate/Sync are
    real APVTS parameters (lforate/lfosync, auto-bound via RelaySet); the
    breakpoint shape itself is not a parameter, so it travels over its own
    pair of native functions (lfoGetBreakpoints/lfoSetBreakpoints) - see
    PluginProcessor.cpp's kLfoBreakpointsProp for why. A commit only happens
    at the end of a gesture (a drag release, an add, a preset pick), never on
    every pointermove - see handlePointPointerDown's own note. */
export default function LfoEditor() {
  const [points, setPoints] = useState(() => withIds(LFO_PRESETS[0].make()));
  const [presetId, setPresetId] = useState(LFO_PRESETS[0].id);
  const [sync, setSync] = useJuceToggleValue("lfosync");
  // The Mod tab's own master switch - off silences every assignment at once
  // (PluginProcessor.cpp's modulatedValue(), gated on "lfoon") without
  // having to remove them one by one. Same useSectionPower shape Delay and
  // Reverb's own PowerToggle use (GrainFace.jsx), inlined here rather than
  // imported - it's three lines and this file has no other reason to share
  // a helper with GrainFace.jsx.
  const [lfoOn, setLfoOn] = useJuceToggleValue("lfoon", true);
  const [playheadPhase, setPlayheadPhase] = useState(null);
  // The Rate knob's own label, in whichever unit the Sync toggle currently
  // means (a note division or a millisecond count) - "1/2" etc. synced,
  // "500 ms" free. The backend already computes exactly this string
  // (PluginProcessor.cpp's lfoRateReadout(), wired to
  // formatKnobValue("lforate") - see PeakGrainWebEditor.cpp), reading the
  // sync flag itself, so this only has to re-fetch it on either input
  // changing; the composite dependency below is there purely to trigger
  // that (useFormattedText re-fetches whenever its own `value` argument
  // changes, whatever type it is). Shown in place of the "Rate" caption
  // only while the knob is actually being dragged (rateDragging below) -
  // this file uses the raw Knob rather than JuceKnob for this one control
  // so it can track that drag itself and drive the label beside the knob,
  // rather than JuceKnob's own built-in caption-swap, which renders the
  // caption under the dial, not beside it (see the .pg-lfo__rate layout).
  const rateId = useParamId("lforate");
  const [rate01, setRate01, rateSliderState] = useJuceSliderValue("lforate");
  const rateText = useFormattedText(rateId, `${rate01}:${sync}`);
  const [rateDragging, setRateDragging] = useState(false);
  const svgRef = useRef(null);
  // Mirrors `points` synchronously (written inside every setPoints updater,
  // not via a separate effect) so a gesture-end handler can read the exact
  // array a preceding setPoints call just computed without waiting on
  // React's own render timing - see setPointsAnd's note.
  const pointsRef = useRef(points);
  const { width: graphW, height: graphH } = useGraphSize(svgRef);
  const midY = graphH / 2;
  const amp = graphH / 2 - MARGIN_Y;

  // Every points update goes through this, so pointsRef always mirrors the
  // exact array the last setPoints call computed - a plain effect keyed on
  // `points` would run one render late, which is exactly one commit later
  // than a gesture-end handler needs it.
  const setPointsAnd = useCallback((updater) => {
    setPoints((prev) => {
      const next = typeof updater === "function" ? updater(prev) : updater;
      pointsRef.current = next;
      return next;
    });
  }, []);

  const commitBreakpoints = useCallback((pointsToSend) => {
    lfoSetBreakpoints(toBreakpointsJson(pointsToSend));
  }, []);

  // Seed from whatever the processor currently has on mount, and resync
  // whenever it says the shape changed out from under this page - a preset
  // load or a host session restore (PeakGrainWebEditor's "lfoBreakpoints"
  // event, bumped by installState, not by this page's own edits). Neither
  // path re-commits: the processor already has what it just sent.
  useEffect(() => {
    let live = true;

    lfoGetBreakpoints().then((json) => {
      if (!live) return;
      const parsed = parseBreakpointsJson(json);
      if (parsed) setPointsAnd(withIds(parsed));
    });

    const handle = window.__JUCE__?.backend?.addEventListener("lfoBreakpoints", (json) => {
      const parsed = parseBreakpointsJson(json);
      if (parsed) setPointsAnd(withIds(parsed));
    });

    return () => {
      live = false;
      if (handle) window.__JUCE__.backend.removeEventListener(handle);
    };
  }, [setPointsAnd]);

  // The live playhead marker - the processor's own phase, not something this
  // page derives from the Rate knob itself (which would drift the moment the
  // engine's tempo-sync alignment nudges the real phase).
  //
  // The processor only emits its phase at PeakGrainWebEditor's own timer
  // rate (10 Hz), which would read as the dot stepping once a tick rather
  // than riding the curve smoothly. Each tick below instead records that
  // phase, when it arrived, and the cycles-per-second observed since the
  // previous tick (unwrapping the 1 -> 0 jump first); a requestAnimationFrame
  // loop then extrapolates forward from the latest tick every frame, so the
  // motion looks continuous even though the ground truth under it only moves
  // ten times a second. It briefly lags the true velocity right after a rate
  // change (still riding the previous tick's estimate until the next one
  // lands), which is the trade this makes for smoothness elsewhere.
  const lastPhaseTickRef = useRef({ phase: 0, time: 0, velocity: 0, received: false });

  useEffect(() => {
    const handle = window.__JUCE__?.backend?.addEventListener("lfoPhase", (phase) => {
      if (typeof phase !== "number" || !Number.isFinite(phase)) return;

      const now = performance.now();
      const prev = lastPhaseTickRef.current;
      const dt = (now - prev.time) / 1000;

      let delta = phase - prev.phase;
      delta -= Math.round(delta); // wrap to [-0.5, 0.5] - the 1 -> 0 jump
      const velocity = prev.received && dt > 0 ? delta / dt : 0;

      lastPhaseTickRef.current = { phase, time: now, velocity, received: true };
    });

    return () => {
      if (handle) window.__JUCE__.backend.removeEventListener(handle);
    };
  }, []);

  // Broadcasts the same interpolated phase's evaluated value (not just the
  // phase itself) to every ModdableKnob's live modulation indicator - see
  // LfoPlayback.jsx. Reads pointsRef rather than `points` so this effect
  // never needs to restart as the shape is edited.
  const setLfoValue = useSetLfoValue();

  useEffect(() => {
    let raf;

    const tick = () => {
      const { phase, time, velocity, received } = lastPhaseTickRef.current;
      if (received) {
        const dt = (performance.now() - time) / 1000;
        const interpolated = (((phase + velocity * dt) % 1) + 1) % 1;
        setPlayheadPhase(interpolated);
        setLfoValue?.(evalBreakpoints(pointsRef.current, interpolated));
      }
      raf = requestAnimationFrame(tick);
    };

    raf = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(raf);
  }, [setLfoValue]);

  // 15 dividers at 1/16, 2/16, ... 15/16 of the width - 16 equal sections.
  const gridX = useMemo(() => Array.from({ length: 15 }, (_, i) => ((i + 1) / 16) * graphW), [graphW]);

  const toSvgX = useCallback((x) => x * graphW, [graphW]);
  const toSvgY = useCallback((y) => midY - y * amp, [midY, amp]);
  const fromSvgX = useCallback((px) => Math.min(1, Math.max(0, px / graphW)), [graphW]);
  const fromSvgY = useCallback((py) => Math.min(1, Math.max(-1, (midY - py) / amp)), [midY, amp]);

  const pathFor = useCallback(
    (points) => {
      let d = "";
      for (let i = 0; i <= CURVE_STEPS; i++) {
        const t = i / CURVE_STEPS;
        const x = toSvgX(t);
        const y = toSvgY(evalBreakpoints(points, t));
        d += (i === 0 ? "M" : "L") + x.toFixed(2) + " " + y.toFixed(2) + " ";
      }
      return d;
    },
    [toSvgX, toSvgY],
  );

  const applyPreset = useCallback(
    (id) => {
      const preset = LFO_PRESETS.find((entry) => entry.id === id);
      if (!preset) return;
      setPresetId(id);
      const next = withIds(preset.make());
      setPointsAnd(next);
      commitBreakpoints(next);
    },
    [setPointsAnd, commitBreakpoints],
  );

  const svgPointFromEvent = (event) => {
    const rect = svgRef.current.getBoundingClientRect();
    const px = ((event.clientX - rect.left) / rect.width) * graphW;
    const py = ((event.clientY - rect.top) / rect.height) * graphH;
    return { x: fromSvgX(px), y: fromSvgY(py) };
  };

  // "Click to add a point" is detected ourselves, on pointerdown/move/up,
  // rather than via the browser's native click event: a native click fires
  // (or not) based on where the pointer happens to be at release, which
  // isn't something this component controls or can predict, and made a
  // point-add fire or misfire depending on exactly where a drag elsewhere
  // ended. Mirroring the same pointerdown-then-window-listeners shape
  // handlePointPointerDown already uses below keeps both gestures on one
  // consistent, self-contained model.
  const handleBackgroundPointerDown = (event) => {
    const start = { x: event.clientX, y: event.clientY, moved: false };

    const handleMove = (moveEvent) => {
      const dx = moveEvent.clientX - start.x;
      const dy = moveEvent.clientY - start.y;
      if (Math.hypot(dx, dy) > CLICK_MOVE_THRESHOLD) start.moved = true;
    };

    const handleUp = (upEvent) => {
      window.removeEventListener("pointermove", handleMove);
      window.removeEventListener("pointerup", handleUp);
      if (start.moved || pointsRef.current.length >= kMaxBreakpoints) return;
      const { x, y } = svgPointFromEvent(upEvent);
      // Built here rather than inside a setPointsAnd(prev => ...) updater and
      // read back off pointsRef on the next line: React only calls a
      // functional updater once it gets around to processing the update,
      // which is not guaranteed to have happened yet by the very next
      // statement - pointsRef.current could still be the pre-add array,
      // committing a shape to the backend that's silently missing the point
      // that just appeared on screen (confirmed - this was happening on
      // every add). Computing the array plainly means it's already correct
      // the instant it's built, so the state update and the commit below
      // are guaranteed to agree.
      const next = [...pointsRef.current, { id: makePointId(), x, y, curve: 0, hold: false }];
      setPointsAnd(next);
      commitBreakpoints(next);
    };

    window.addEventListener("pointermove", handleMove);
    window.addEventListener("pointerup", handleUp);
  };

  const handlePointPointerDown = (id) => (event) => {
    event.preventDefault();

    // `id` is captured directly from this closure's own argument, not read
    // back off a ref - React batches the setPoints calls a fast drag queues
    // up and only actually runs each updater once it gets around to
    // rendering, which can be after this same drag's pointerup already ran.
    // A ref mutated to null in that pointerup (the previous approach) is
    // shared, mutable state, so an updater invoked after that point read
    // null instead of the id it was dragging - this is what actually made a
    // point vanish (a crash inside the state update, unmounting the whole
    // editor with no error boundary to catch it). `id` is a captured
    // primitive, immune to that regardless of when React gets to it.
    // Every move updates local state only, for a smooth drag - the backend
    // only hears about the final position, on release below, so a fast drag
    // does not flood the native bridge with one call per pointermove.
    //
    // `current` mirrors the array through the gesture in a plain local
    // variable, computed synchronously by every handleMove call, rather than
    // being read back off pointsRef in handleUp below - pointsRef only
    // catches up once React gets around to actually running each queued
    // update, which the final commit can't rely on having already happened
    // (see handleBackgroundPointerDown's identical note on this - it can
    // drop the last move's position from what gets committed to the backend
    // if a drag ends fast enough that the very last update hasn't flushed
    // yet). A plain local variable has no such delay.
    let current = pointsRef.current;

    const handleMove = (moveEvent) => {
      const { x, y } = svgPointFromEvent(moveEvent);
      const sorted = [...current].sort((a, b) => a.x - b.x);
      const n = sorted.length;
      const i = sorted.findIndex((point) => point.id === id);
      if (i === -1 || n <= 1) return;

      const prevNeighbourX = sorted[(i - 1 + n) % n].x - (i === 0 ? 1 : 0);
      const nextNeighbourX = sorted[(i + 1) % n].x + (i === n - 1 ? 1 : 0);
      const lo = prevNeighbourX + MIN_POINT_SPACING;
      const hi = nextNeighbourX - MIN_POINT_SPACING;
      const clampedX = ((Math.min(Math.max(x, lo), hi) % 1) + 1) % 1;

      sorted[i] = { ...sorted[i], x: clampedX, y };
      current = sorted;
      setPointsAnd(current);
    };

    const handleUp = () => {
      window.removeEventListener("pointermove", handleMove);
      window.removeEventListener("pointerup", handleUp);
      commitBreakpoints(current);
    };

    window.addEventListener("pointermove", handleMove);
    window.addEventListener("pointerup", handleUp);
  };

  const handlePointDoubleClick = (id) => (event) => {
    event.stopPropagation();
    // Computed directly rather than via pointsRef, same reason as the two
    // gestures above - reading pointsRef back on the very next line isn't
    // guaranteed to see this removal yet.
    const next = points.length > 2 ? points.filter((point) => point.id !== id) : points;
    setPointsAnd(next);
    // Harmless when the guard above left the list untouched (down to its
    // floor of 2 points) - re-sending the same shape is a no-op.
    commitBreakpoints(next);
  };

  const sortedPoints = [...points].sort((a, b) => a.x - b.x);

  return (
    <div className="pg-lfo" data-off={!lfoOn || undefined}>
      <svg ref={svgRef} className="pg-lfo__graph" viewBox={`0 0 ${graphW} ${graphH}`}>
        <rect
          className="pg-lfo__bg"
          x={0}
          y={0}
          width={graphW}
          height={graphH}
          onPointerDown={handleBackgroundPointerDown}
        />
        <line className="pg-lfo__midline" x1={0} y1={midY} x2={graphW} y2={midY} />
        {/* 15 evenly-spaced dividers, the same 16th-note grid a DAW's own
            piano roll draws - purely a visual guide for placing breakpoints
            by eye, no snapping tied to it. */}
        {gridX.map((x, i) => (
          <line key={i} className="pg-lfo__gridline" x1={x} y1={0} x2={x} y2={graphH} />
        ))}
        {/* Purely a relabel, not a rescale: a point's own y is still -1..1
            internally (lfoShapes.js) and still feeds PluginProcessor.cpp's
            modulatedValue() as base01 + depth * y unchanged - top still
            pushes a modulated knob up, bottom still pushes it down, the
            midline is still "no push". Only these three numbers change, from
            "1 / 0 / -1" to "100 / 50 / 0", to read on the same 0-100 scale
            Depth's own slider already uses (ModAssignmentPopover.jsx) - so
            the two controls stop looking like they disagree about what
            range this is, even though neither's actual behaviour moved. */}
        <text className="pg-lfo__axis-label" x={4} y={midY - amp + 10}>
          100
        </text>
        <text className="pg-lfo__axis-label" x={4} y={midY + 3}>
          50
        </text>
        <text className="pg-lfo__axis-label" x={4} y={midY + amp - 4}>
          0
        </text>
        <path className="pg-lfo__curve" d={pathFor(points)} />
        {playheadPhase != null && (
          <circle
            className="pg-lfo__playhead"
            cx={toSvgX(playheadPhase)}
            cy={toSvgY(evalBreakpoints(points, playheadPhase))}
            r={4}
            pointerEvents="none"
          />
        )}
        {sortedPoints.map((point) => (
          <circle
            key={point.id}
            className="pg-lfo__point"
            cx={toSvgX(point.x)}
            cy={toSvgY(point.y)}
            r={5.5}
            onPointerDown={handlePointPointerDown(point.id)}
            onDoubleClick={handlePointDoubleClick(point.id)}
          />
        ))}
      </svg>
      <div className="pg-lfo__toolbar">
        <ModSourceChip />
        <div className="pg-lfo__rate">
          <Knob
            size={28}
            variant="flat"
            bare
            value={rate01}
            onChange={setRate01}
            onDragStart={() => {
              rateSliderState.sliderDragStarted();
              setRateDragging(true);
            }}
            onDragEnd={() => {
              rateSliderState.sliderDragEnded();
              setRateDragging(false);
            }}
          />
          <span className="pg-lfo__rate-label">{rateDragging ? rateText : "Rate"}</span>
        </div>
        <Pill label="Sync" pressed={sync} onClick={() => setSync(!sync)} />
        <Dropdown
          options={LFO_PRESETS.map((preset) => ({ value: preset.id, label: preset.label }))}
          value={presetId}
          onChange={applyPreset}
          openDirection="up"
          tone="chrome"
        />
        {/* Pinned to the row's own right edge (margin-left: auto on the
            wrapper, LfoEditor.css) while everything else here stays centred
            as a group - the standard flex trick for "one item breaks out of
            an otherwise-centred row" (no `justify-content` restructuring
            needed). PowerToggle takes no className of its own, hence the
            wrapper rather than a prop straight through. The Mod tab's own
            master switch - see the `lfoOn` state above for what it does. */}
        <div className="pg-lfo__power">
          <PowerToggle on={lfoOn} onToggle={setLfoOn} ariaLabel="Mod on" />
        </div>
      </div>
    </div>
  );
}
