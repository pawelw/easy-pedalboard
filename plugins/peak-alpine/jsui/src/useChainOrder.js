import { useJuceChoiceValue } from "@synthpeak/pedal-ui/juce";
import { permutationForIndex, indexForPermutation, NUM_PERMUTATIONS } from "./chainOrder.js";

/** The live module order behind Peak Alpine's drag-to-reorder panel, bound to
    the `chain.order` choice parameter through the same relay hook every other
    choice control here uses (useJuceChoiceValue) - see ChainSlot.jsx for what
    drives `setOrder`. */
export function useChainOrder() {
  const [index, setIndex] = useJuceChoiceValue("chain.order", NUM_PERMUTATIONS, 0);
  const order = permutationForIndex(index);
  const setOrder = (nextOrder) => setIndex(indexForPermutation(nextOrder));
  return [order, setOrder];
}
