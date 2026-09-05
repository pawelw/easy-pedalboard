/**
 * Two rounded capsules on a 45° axis, overlapping in the middle - a link
 * rather than a word, for controls too small to print "Sync"/"Linked"
 * legibly. Port of `drawLinkIcon` (`plugins/peak-delay/src/PluginProcessor.cpp`),
 * geometry measured off the onyx prototype rather than recomputed from the
 * original's trig, so the two stay visually identical without sharing code
 * (one draws into a juce::Graphics context, the other is markup).
 */
export default function LinkIcon({ size = 16 }) {
  const strokeWidth = size <= 14 ? 1.9 : 1.7;
  return (
    <svg width={size} height={size} viewBox="0 0 20 20" fill="none">
      <g stroke="currentColor" strokeWidth={strokeWidth} strokeLinejoin="round" transform="rotate(45 10 10)">
        <rect x="6.6" y="2.4" width="6.8" height="10.4" rx="3.4" />
        <rect x="6.6" y="7.2" width="6.8" height="10.4" rx="3.4" />
      </g>
    </svg>
  );
}
