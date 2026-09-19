import { useEffect, useRef, useState } from "react";
import { useDroppable } from "@dnd-kit/core";
import { JuceKnob, useJuceSliderValue } from "@synthpeak/pedal-ui/juce";
import { useModAssignment, useModRouting } from "./ModRouting.jsx";
import { useLfoValue } from "./LfoPlayback.jsx";
import ModAssignmentPopover from "./ModAssignmentPopover.jsx";
import "./ModdableKnob.css";

/** The live "the LFO has this knob pushed to here right now" tick
    (Knob.jsx's `modIndicator`) - its own component, mounted only while this
    knob actually has an assignment, so an unassigned knob never subscribes
    to the LFO's live output (useLfoValue re-renders its subscriber at
    animation-frame rate; with up to 8 simultaneous assignments out of
    Grain's 15 targets, most knobs at any moment pay nothing for this).
    Mirrors PluginProcessor.cpp's own modulatedValue() exactly - base01 +
    depth * lfoValue, clamped to 0..1 - so the tick shown here tracks the
    real audio modulation rather than a separate UI-only approximation of
    it. The knob's own arc/needle is untouched: it keeps reading `value`
    from its own JuceKnob binding, never this. */
function ModulatedKnob({ parameterId, depth, ...knobProps }) {
  const [base01] = useJuceSliderValue(parameterId);
  const lfoValue = useLfoValue();
  const modIndicator = Math.min(1, Math.max(0, base01 + depth * lfoValue));

  return <JuceKnob parameterId={parameterId} modIndicator={modIndicator} {...knobProps} />;
}

/** A JuceKnob that also accepts the Mod tab's LFO chip being dropped onto it -
    used on Grain/Pitch/Random's own knobs and the Mixer's Filter/Drive/Bit -
    every modulation target this pedal has (Delay/Reverb are out of scope,
    see PluginProcessor.cpp's own note on kModChunk). Kept local to BitBit
    Grain rather than folded into the shared JuceKnob so this pedal's
    modulation-routing concerns do not leak into every other pedal that uses
    that component.

    `useDroppable`'s own ref/isOver are handed to JuceKnob's dropRef/dropActive
    props (threaded through to Knob.jsx's dial box - see its own note), so the
    drop target is the knob's dial specifically, not this wrapper. The lit
    badge below goes through the same dial-scoped `badge` prop, for the same
    reason: pinned to this wrapper's own box instead, it landed a different
    distance from the dial on every differently-sized or differently-shrunk
    knob (Pitch's tight three-across row shrinks the wrapper, Random's
    38px Stereo doesn't) - Knob.jsx's dial box never does either. */
export default function ModdableKnob({ parameterId, badgeStyle, ...knobProps }) {
  const { isOver, setNodeRef } = useDroppable({ id: parameterId });
  const assignment = useModAssignment(parameterId);
  const routing = useModRouting();
  const [popoverOpen, setPopoverOpen] = useState(false);
  const badgeRef = useRef(null);
  const popoverRef = useRef(null);

  // Opens this knob's own popover the instant a drop assigns it, rather than
  // making the user hunt for the new badge and click it separately.
  // clearJustAssigned resets the context's own flag immediately, so it can
  // never fire a second time for the same drop.
  useEffect(() => {
    if (routing?.justAssignedParamId === parameterId) {
      setPopoverOpen(true);
      routing.clearJustAssigned();
    }
  }, [routing?.justAssignedParamId, parameterId]);

  // A click anywhere outside the open popover (and outside the badge that
  // opens/closes it, which already toggles on its own) closes it.
  useEffect(() => {
    if (!popoverOpen) return undefined;

    const handlePointerDown = (event) => {
      if (popoverRef.current?.contains(event.target)) return;
      if (badgeRef.current?.contains(event.target)) return;
      setPopoverOpen(false);
    };

    document.addEventListener("pointerdown", handlePointerDown);
    return () => document.removeEventListener("pointerdown", handlePointerDown);
  }, [popoverOpen]);

  const badge = assignment && (
    <button
      ref={badgeRef}
      type="button"
      className="pg-moddable-knob__badge"
      onClick={() => setPopoverOpen((open) => !open)}
      title="Modulated by the LFO - click to edit"
      aria-label="Modulation depth"
    />
  );

  return (
    <div className="pg-moddable-knob">
      {assignment ? (
        <ModulatedKnob
          parameterId={parameterId}
          depth={assignment.depth}
          dropRef={setNodeRef}
          dropActive={isOver}
          badge={badge}
          badgeStyle={badgeStyle}
          {...knobProps}
        />
      ) : (
        <JuceKnob
          parameterId={parameterId}
          dropRef={setNodeRef}
          dropActive={isOver}
          badge={badge}
          badgeStyle={badgeStyle}
          {...knobProps}
        />
      )}
      {assignment && popoverOpen && (
        <ModAssignmentPopover
          popoverRef={popoverRef}
          depth={assignment.depth}
          onChangeDepth={(depth) => routing.setDepth(parameterId, depth)}
          onRemove={() => {
            routing.remove(parameterId);
            setPopoverOpen(false);
          }}
          onClose={() => setPopoverOpen(false)}
        />
      )}
    </div>
  );
}
