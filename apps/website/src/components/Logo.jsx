export default function Logo({ width = 28, height = 20, stroke = '#b9d3d9', strokeWidth = 1.6 }) {
  return (
    <svg width={width} height={height} viewBox="0 0 28 20" fill="none" aria-hidden="true">
      <polyline
        points="1,18 7,6 11,13 16,3 22,14 27,8"
        stroke={stroke}
        strokeWidth={strokeWidth}
        strokeLinejoin="round"
        strokeLinecap="round"
      />
    </svg>
  );
}
