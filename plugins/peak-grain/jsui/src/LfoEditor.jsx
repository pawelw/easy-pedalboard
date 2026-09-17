import { useCallback, useEffect, useRef, useState } from "react";
import * as Juce from "juce-framework-frontend";
import { Pill, Dropdown } from "@synthpeak/pedal-ui";
import { JuceKnob, useJuceToggleValue } from "@synthpeak/pedal-ui/juce";
import { evalBreakpoints, kMaxBreakpoints, LFO_PRESETS, parseBreakpointsJson, toBreakpointsJson } from "./lfoShapes.js";
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
  const [playheadPhase, setPlayheadPhase] = useState(null);
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
  useEffect(() => {
    const handle = window.__JUCE__?.backend?.addEventListener("lfoPhase", (phase) => {
      if (typeof phase === "number" && Number.isFinite(phase)) setPlayheadPhase(phase);
    });
    return () => {
      if (handle) window.__JUCE__.backend.removeEventListener(handle);
    };
  }, []);

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
      setPointsAnd((prev) => [...prev, { id: makePointId(), x, y, curve: 0, hold: false }]);
      commitBreakpoints(pointsRef.current);
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
    const handleMove = (moveEvent) => {
      const { x, y } = svgPointFromEvent(moveEvent);
      setPointsAnd((prev) => {
        const sorted = [...prev].sort((a, b) => a.x - b.x);
        const n = sorted.length;
        const i = sorted.findIndex((point) => point.id === id);
        if (i === -1 || n <= 1) return sorted;

        const prevNeighbourX = sorted[(i - 1 + n) % n].x - (i === 0 ? 1 : 0);
        const nextNeighbourX = sorted[(i + 1) % n].x + (i === n - 1 ? 1 : 0);
        const lo = prevNeighbourX + MIN_POINT_SPACING;
        const hi = nextNeighbourX - MIN_POINT_SPACING;
        const clampedX = ((Math.min(Math.max(x, lo), hi) % 1) + 1) % 1;

        sorted[i] = { ...sorted[i], x: clampedX, y };
        return sorted;
      });
    };

    const handleUp = () => {
      window.removeEventListener("pointermove", handleMove);
      window.removeEventListener("pointerup", handleUp);
      commitBreakpoints(pointsRef.current);
    };

    window.addEventListener("pointermove", handleMove);
    window.addEventListener("pointerup", handleUp);
  };

  const handlePointDoubleClick = (id) => (event) => {
    event.stopPropagation();
    setPointsAnd((prev) => (prev.length > 2 ? prev.filter((point) => point.id !== id) : prev));
    // Harmless even when the guard above left the list untouched (down to
    // its floor of 2 points) - re-sending the same shape is a no-op.
    commitBreakpoints(pointsRef.current);
  };

  const sortedPoints = [...points].sort((a, b) => a.x - b.x);

  return (
    <div className="pg-lfo">
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
        <div className="pg-lfo__rate">
          <JuceKnob parameterId="lforate" size={28} variant="flat" bare />
          <span className="pg-lfo__rate-label">Rate</span>
        </div>
        <Pill label="Sync" pressed={sync} onClick={() => setSync(!sync)} />
        <Dropdown
          options={LFO_PRESETS.map((preset) => ({ value: preset.id, label: preset.label }))}
          value={presetId}
          onChange={applyPreset}
          openDirection="up"
          tone="chrome"
        />
      </div>
    </div>
  );
}
