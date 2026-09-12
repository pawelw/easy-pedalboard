// The Lehmer code (factorial number system) behind Peak Alpine's chain.order
// parameter - the exact same algorithm and module-id numbering as
// plugins/peak-alpine/src/ChainOrder.h, so the two sides can never drift
// apart. Index 0 decodes to [MODULE_ARTIFACT, MODULE_MODULATION,
// MODULE_DELAY, MODULE_REVERB] by construction - today's fixed order.

export const MODULE_ARTIFACT = 0;
export const MODULE_MODULATION = 1;
export const MODULE_DELAY = 2;
export const MODULE_REVERB = 3;

export const NUM_MODULES = 4;
export const NUM_PERMUTATIONS = 24; // 4!

// (NUM_MODULES - 1)! down to 0!, the place values of a Lehmer code for N = 4.
const PLACE_VALUE = [6, 2, 1, 1];

/** Lehmer decode: index (0-23, clamped) -> an ordering of the four modules. */
export function permutationForIndex(index) {
  let remaining = Math.min(Math.max(index, 0), NUM_PERMUTATIONS - 1);

  const pool = [0, 1, 2, 3];
  const order = [];

  for (let slot = 0; slot < NUM_MODULES; slot++) {
    const digit = Math.floor(remaining / PLACE_VALUE[slot]);
    remaining -= digit * PLACE_VALUE[slot];

    order.push(pool[digit]);
    pool.splice(digit, 1);
  }

  return order;
}

/** Lehmer encode: an ordering of the four modules -> its index (0-23). Inverse of permutationForIndex. */
export function indexForPermutation(order) {
  const pool = [0, 1, 2, 3];
  let index = 0;

  for (let slot = 0; slot < NUM_MODULES; slot++) {
    const digit = pool.indexOf(order[slot]);
    index += digit * PLACE_VALUE[slot];
    pool.splice(digit, 1);
  }

  return index;
}
