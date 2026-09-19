/**
 * The BitBit mark: a waveform drawn cell by cell. The blockiness is the point,
 * so it is rects on a grid rather than a stroked path — a path smooths out the
 * moment it is scaled.
 */

const W = 60;
const H = 22;
const TOP = 11; // baseline occupies rows TOP and TOP + 1
const BAR_X = 3;
const BAR_W = 2;
const BAR_STRIDE = 3;

// [cells above the baseline, cells below] per bar, left to right.
const BARS = [
  [1, 0], [3, 2], [1, 1], [4, 2], [2, 1], [6, 4], [1, 1], [5, 3],
  [9, 7],
  [4, 3], [6, 4], [1, 1], [4, 2], [2, 2], [3, 1], [1, 1], [2, 0],
];

const CELLS = (() => {
  const cells = new Set();

  for (let x = 1; x < W - 3; x += 1) {
    cells.add(`${x},${TOP}`);
    cells.add(`${x},${TOP + 1}`);
  }

  BARS.forEach(([up, down], i) => {
    for (let w = 0; w < BAR_W; w += 1) {
      const x = BAR_X + i * BAR_STRIDE + w;
      for (let y = TOP - up; y < TOP; y += 1) cells.add(`${x},${y}`);
      for (let y = TOP + 2; y <= TOP + 1 + down; y += 1) cells.add(`${x},${y}`);
    }
  });

  return [...cells].map((key) => key.split(',').map(Number));
})();

export default function Logo({ size = 30, color = 'currentColor', className }) {
  return (
    <svg
      width={size}
      height={(size * H) / W}
      viewBox={`0 0 ${W} ${H}`}
      fill={color}
      className={className}
      aria-hidden="true"
      shapeRendering="crispEdges"
    >
      {CELLS.map(([x, y]) => (
        <rect key={`${x},${y}`} x={x} y={y} width="1" height="1" />
      ))}
    </svg>
  );
}
