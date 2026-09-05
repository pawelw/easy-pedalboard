// Small glyphs for the Sync/Ms buttons, matching the old ee::ui face's
// drawLinkIcon/drawMsIcon (see PluginProcessor.cpp's git history) so the
// React face reads the same even though the drawing code itself couldn't be
// reused (that one drew straight into a juce::Graphics context).

/** Two capsule outlines lying on the same diagonal, overlapping in the
    middle - a link rather than a word, since the button is too small to
    print "Sync" legibly. */
export function LinkIcon({ size = 16 }) {
  const linkW = size * 0.44;
  const linkH = size * 0.74;
  const offset = size * 0.19;
  const diagonal = (offset * Math.SQRT2) / 2;
  const stroke = Math.max(1.2, size * 0.11);
  const half = size / 2;

  const capsule = <rect x={-linkW / 2} y={-linkH / 2} width={linkW} height={linkH} rx={linkW / 2} />;

  return (
    <svg width={size} height={size} viewBox={`${-half} ${-half} ${size} ${size}`}>
      <g fill="none" stroke="currentColor" strokeWidth={stroke} strokeLinecap="round" strokeLinejoin="round">
        <g transform={`translate(${diagonal} ${-diagonal}) rotate(45)`}>{capsule}</g>
        <g transform={`translate(${-diagonal} ${diagonal}) rotate(45)`}>{capsule}</g>
      </g>
    </svg>
  );
}

/** A plain "ms" wordmark - there's no obvious glyph for "milliseconds" the
    way a chain link stands for "linked together". */
export function MsIcon({ size = 16 }) {
  return (
    <span style={{ fontFamily: "var(--pui-font-label)", fontWeight: 700, fontSize: size * 0.68, lineHeight: 1 }}>
      ms
    </span>
  );
}
