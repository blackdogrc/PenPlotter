#pragma once
#include <Arduino.h>

static const char SVG_CONVERTER_JS[] PROGMEM = R"SVGJS(
class SvgConverter {
  static NS = 'http://www.w3.org/2000/svg';

  static numberWithUnit(value, fallback) {
    if (!value) return fallback;

    const m = String(value).trim().match(
      /^([+-]?(?:\d+\.?\d*|\.\d+))(mm|cm|in|px|pt|pc)?$/i
    );
    if (!m) return fallback;

    const n = Number(m[1]);
    switch ((m[2] || '').toLowerCase()) {
      case 'cm': return n * 10;
      case 'in': return n * 25.4;
      case 'px': return n * 25.4 / 96;
      case 'pt': return n * 25.4 / 72;
      case 'pc': return n * 25.4 / 6;
      default:   return n; // mm or unitless
    }
  }

  static clipSegment(a, b, width, height) {
    // Liang-Barsky clipping against the plotter's usable area.
    const dx = b.x - a.x;
    const dy = b.y - a.y;
    const p = [-dx, dx, -dy, dy];
    const q = [a.x, width - a.x, a.y, height - a.y];
    let t0 = 0;
    let t1 = 1;

    for (let i = 0; i < 4; i++) {
      if (p[i] === 0) {
        if (q[i] < 0) return null;
        continue;
      }
      const r = q[i] / p[i];
      if (p[i] < 0) {
        if (r > t1) return null;
        if (r > t0) t0 = r;
      } else {
        if (r < t0) return null;
        if (r < t1) t1 = r;
      }
    }

    return [
      { x: a.x + t0 * dx, y: a.y + t0 * dy },
      { x: a.x + t1 * dx, y: a.y + t1 * dy }
    ];
  }

  static async convert(file, width, height, mode,drawingFeed) {
    const source = await file.text();
    const doc = new DOMParser().parseFromString(source, 'image/svg+xml');

    if (doc.querySelector('parsererror')) {
      throw new Error('Could not parse this SVG file.');
    }

    const root = doc.documentElement;
    if (!root || root.localName !== 'svg') {
      throw new Error('The file does not contain an SVG document.');
    }

    const viewBoxText = root.getAttribute('viewBox');
    let vb;

    if (viewBoxText) {
      const values = viewBoxText.trim().split(/[\s,]+/).map(Number);
      if (values.length !== 4 || values.some(v => !Number.isFinite(v)) ||
          values[2] <= 0 || values[3] <= 0) {
        throw new Error('The SVG has an invalid viewBox.');
      }
      vb = { x: values[0], y: values[1], w: values[2], h: values[3] };
    } else {
      const w = SvgConverter.numberWithUnit(root.getAttribute('width'), 0);
      const h = SvgConverter.numberWithUnit(root.getAttribute('height'), 0);
      if (w <= 0 || h <= 0) {
        throw new Error('SVG needs a viewBox or explicit width and height.');
      }
      vb = { x: 0, y: 0, w, h };
    }

    // Determine the drawing's physical size. Explicit SVG units are converted
    // to mm. Without physical units, viewBox units are treated as millimeters.
    const physicalW = SvgConverter.numberWithUnit(
      root.getAttribute('width'), vb.w
    );
    const physicalH = SvgConverter.numberWithUnit(
      root.getAttribute('height'), vb.h
    );

    let scale = 1;
    if (mode === 'fit') {
      scale = Math.min(width / physicalW, height / physicalH);
    }

    const drawnW = physicalW * scale;
    const drawnH = physicalH * scale;
    const offsetX = (width - drawnW) / 2;
    const offsetY = (height - drawnH) / 2;

    // Attach the parsed SVG temporarily so browser SVG geometry methods and
    // transforms can be evaluated.
    const host = document.createElementNS(SvgConverter.NS, 'svg');
    host.setAttribute('viewBox',
      `${vb.x} ${vb.y} ${vb.w} ${vb.h}`);
    host.setAttribute('width', '1000');
    host.setAttribute('height', '1000');
    host.style.cssText =
      'position:fixed;left:-2000px;top:-2000px;width:1000px;height:1000px';
    document.body.appendChild(host);

    const imported = document.importNode(root, true);
    host.appendChild(imported);

    const rootMatrix = imported.getScreenCTM();
    if (!rootMatrix) {
      host.remove();
      throw new Error('Could not read SVG transforms.');
    }
    const inverse = rootMatrix.inverse();

    const drawable = imported.querySelectorAll(
      'path,line,polyline,polygon,rect,circle,ellipse'
    );
    if (!drawable.length) {
      host.remove();
      throw new Error('No supported drawing shapes were found in the SVG.');
    }

    const paths = [];
    let unsupported = false;

    for (const shape of drawable) {
      if (typeof shape.getTotalLength !== 'function' ||
          typeof shape.getPointAtLength !== 'function') {
        unsupported = true;
        continue;
      }

      let length;
      try {
        length = shape.getTotalLength();
      } catch (_) {
        unsupported = true;
        continue;
      }
      if (!Number.isFinite(length) || length <= 0) continue;

      // Sample curves into short straight moves. Limit the number of samples
      // per shape to keep generated G-code manageable.
      const steps = Math.max(1, Math.min(3000, Math.ceil(length / 2)));
      const points = [];

      for (let i = 0; i <= steps; i++) {
        const p = shape.getPointAtLength(length * i / steps);
        const shapeMatrix = shape.getScreenCTM();
        if (!shapeMatrix) continue;

        const screenPoint =
          new DOMPoint(p.x, p.y).matrixTransform(shapeMatrix);
        const q = screenPoint.matrixTransform(inverse);
        const x = (q.x - vb.x) * physicalW / vb.w * scale + offsetX;
        const y = (q.y - vb.y) * physicalH / vb.h * scale + offsetY;
        points.push({ x, y });
      }
      paths.push(points);
    }

    host.remove();

    if (!paths.length) {
      throw new Error('No usable paths were found in the SVG.');
    }

   const out = ['G21', 'G90', 'M5'];
let drawnSegments = 0;
let feedWritten = false;

for (const points of paths) {
  let penDown = false;
  let lastEnd = null;

  for (let i = 1; i < points.length; i++) {
    const clipped = SvgConverter.clipSegment(
      points[i - 1], points[i], width, height
    );

    if (!clipped) {
      // The path leaves the plot area. Lift the pen before the next
      // visible segment, which may require a new rapid move.
      if (penDown) out.push('M5');
      penDown = false;
      lastEnd = null;
      continue;
    }

    const [a, b] = clipped;

    // Skip degenerate segments.
    if (Math.hypot(b.x - a.x, b.y - a.y) < 0.001) continue;

    const samePoint = lastEnd &&
      Math.hypot(lastEnd.x - a.x, lastEnd.y - a.y) < 0.01;

    // Start a new contour only when the pen is up or this segment
    // does not continue from the previous segment's endpoint.
    if (!penDown || !samePoint) {
      if (penDown) out.push('M5');

      const ax = a.x.toFixed(3);
      const ay = (height - a.y).toFixed(3);

      out.push(`G0 X${ax} Y${ay}`, 'M3');
      penDown = true;
    }

    const bx = b.x.toFixed(3);
    const by = (height - b.y).toFixed(3);

    if (!feedWritten) {
      out.push(
        `G1 X${bx} Y${by} F${Math.round(drawingFeed)}`
      );
      feedWritten = true;
    } else {
      out.push(`G1 X${bx} Y${by}`);
    }

    lastEnd = b;
    drawnSegments++;
  }

  // Lift before moving on to the next SVG shape.
  if (penDown) out.push('M5');
}

out.push('M30');


    if (!drawnSegments) {
      throw new Error('Nothing from this SVG falls inside the plot area.');
    }

    return {
      gcode: out.join('\n'),
      warning: unsupported
        ? 'Some SVG shapes were skipped because they could not be converted.'
        : ''
    };
  }
}
)SVGJS";
