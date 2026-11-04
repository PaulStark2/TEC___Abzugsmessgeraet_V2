'use strict';

/**
 * Build CSV / PDF / ZIP downloads for Kraft-Weg evaluation results.
 * CSV is Excel-DE friendly (BOM, ';', decimal ',').
 * PDF rendering uses pdfkit (vector paths, no Chromium).
 */

const PDFDocument = require('pdfkit');
const JSZip = require('jszip');
const { LEG_INSERT, LEG_EXTRACT } = require('./forcePathStore');

/** Distinct hue per iteration; insert = lighter, extract = darker of the same hue. */
function curveColor(iteration, leg) {
  const hue = (Number(iteration) * 47) % 360;
  const light = leg === LEG_INSERT ? 62 : 38;
  const sat = 70;
  return hslToHex(hue, sat, light);
}

function hslToHex(h, s, l) {
  s /= 100;
  l /= 100;
  const k = (n) => (n + h / 30) % 12;
  const a = s * Math.min(l, 1 - l);
  const f = (n) => l - a * Math.max(-1, Math.min(k(n) - 3, Math.min(9 - k(n), 1)));
  const to = (x) =>
    Math.round(255 * Math.min(1, Math.max(0, x)))
      .toString(16)
      .padStart(2, '0');
  return `#${to(f(0))}${to(f(8))}${to(f(4))}`;
}

function legLabel(leg) {
  return leg === LEG_INSERT ? 'Eingliedern' : 'Ausgliedern';
}

/** Excel-DE number: fixed decimals with comma separator. */
function excelNum(value, digits = 4) {
  if (value == null || !Number.isFinite(Number(value))) return '';
  return Number(value).toFixed(digits).replace('.', ',');
}

function csvEscape(value) {
  const s = String(value ?? '');
  if (/[";\n\r]/.test(s)) return `"${s.replace(/"/g, '""')}"`;
  return s;
}

function buildSamplesCsv(analysis) {
  const header = [
    'PunktIndex',
    'PunktName',
    'Iteration',
    'Richtung',
    'RichtungsName',
    'SampleIndex',
    'z_abs_mm',
    'z_rel_mm',
    'F_N',
  ];
  const lines = [header.join(';')];
  for (const point of analysis.points || []) {
    for (const curve of point.curves || []) {
      for (const s of curve.samples || []) {
        lines.push(
          [
            point.pointIndex,
            csvEscape(point.name),
            curve.iteration,
            curve.leg,
            csvEscape(legLabel(curve.leg)),
            s.index,
            excelNum(s.z),
            excelNum(s.zRel),
            excelNum(s.fz),
          ].join(';')
        );
      }
    }
  }
  return `\uFEFF${lines.join('\n')}\n`;
}

function buildSummaryCsv(analysis) {
  const header = [
    'PunktIndex',
    'PunktName',
    'Richtung',
    'RichtungsName',
    'AnzahlSpitzen',
    'MittelSpitze_N',
    'StdAbwSpitze_N',
    'Spitzen_N',
  ];
  const lines = [header.join(';')];
  for (const point of analysis.points || []) {
    for (const [leg, stats] of [
      [LEG_INSERT, point.insert],
      [LEG_EXTRACT, point.extract],
    ]) {
      const peaksList = (stats.peaksN || []).map((p) => excelNum(p)).join(';');
      lines.push(
        [
          point.pointIndex,
          csvEscape(point.name),
          leg,
          csvEscape(legLabel(leg)),
          stats.count,
          stats.meanPeakN == null ? '' : excelNum(stats.meanPeakN),
          stats.stddevPeakN == null ? '' : excelNum(stats.stddevPeakN),
          peaksList ? csvEscape(peaksList) : '',
        ].join(';')
      );
    }
  }
  return `\uFEFF${lines.join('\n')}\n`;
}

function formatN(v) {
  if (v == null || !Number.isFinite(v)) return '—';
  return `${v.toFixed(3)} N`;
}

function niceStep(span, targetTicks) {
  if (!(span > 0) || !Number.isFinite(span)) return 1;
  const raw = span / Math.max(2, targetTicks);
  const pow = Math.pow(10, Math.floor(Math.log10(raw)));
  const n = raw / pow;
  let nice = 1;
  if (n <= 1) nice = 1;
  else if (n <= 2) nice = 2;
  else if (n <= 5) nice = 5;
  else nice = 10;
  return nice * pow;
}

function tickValues(min, max, step) {
  const out = [];
  if (!(step > 0)) return out;
  const start = Math.ceil(min / step - 1e-9) * step;
  for (let v = start; v <= max + step * 1e-6; v += step) {
    out.push(v);
  }
  return out;
}

/**
 * Draw a proper Kraft-Weg chart with axes, ticks, grid, and zero lines.
 * @returns {number} Y position just below the caption block
 */
function drawPointChart(doc, point, originX, originY, width, height) {
  const curves = point.curves || [];
  let zMin = Infinity;
  let zMax = -Infinity;
  let fMin = Infinity;
  let fMax = -Infinity;
  for (const c of curves) {
    for (const s of c.samples || []) {
      zMin = Math.min(zMin, s.zRel);
      zMax = Math.max(zMax, s.zRel);
      fMin = Math.min(fMin, s.fz);
      fMax = Math.max(fMax, s.fz);
    }
  }
  if (!Number.isFinite(zMin)) {
    doc.fontSize(10).fillColor('#222').text('Keine Messwerte.', originX, originY);
    return originY + 20;
  }
  if (zMax - zMin < 1e-9) {
    zMin -= 0.5;
    zMax += 0.5;
  }
  if (fMax - fMin < 1e-9) {
    fMin -= 0.5;
    fMax += 0.5;
  }
  // Ensure zero lines can appear when data crosses them.
  if (zMin > 0) zMin = Math.min(zMin, -0.05);
  if (zMax < 0) zMax = Math.max(zMax, 0.05);
  if (fMin > 0) fMin = Math.min(fMin, -0.05);
  if (fMax < 0) fMax = Math.max(fMax, 0.05);

  const pad = 0.06;
  const zSpan = zMax - zMin;
  const fSpan = fMax - fMin;
  zMin -= zSpan * pad;
  zMax += zSpan * pad;
  fMin -= fSpan * pad;
  fMax += fSpan * pad;

  const leftPad = 48;
  const rightPad = 12;
  const topPad = 10;
  const bottomPad = 42;
  const plotX = originX + leftPad;
  const plotY = originY + topPad;
  const plotW = width - leftPad - rightPad;
  const plotH = height - topPad - bottomPad;

  const mapX = (z) => plotX + ((z - zMin) / (zMax - zMin)) * plotW;
  const mapY = (f) => plotY + plotH - ((f - fMin) / (fMax - fMin)) * plotH;

  const zStep = niceStep(zMax - zMin, 6);
  const fStep = niceStep(fMax - fMin, 6);
  const zTicks = tickValues(zMin, zMax, zStep);
  const fTicks = tickValues(fMin, fMax, fStep);

  // Grid
  doc.save();
  doc.strokeColor('#d0d4da').lineWidth(0.5);
  for (const z of zTicks) {
    const x = mapX(z);
    doc.moveTo(x, plotY).lineTo(x, plotY + plotH).stroke();
  }
  for (const f of fTicks) {
    const y = mapY(f);
    doc.moveTo(plotX, y).lineTo(plotX + plotW, y).stroke();
  }
  doc.restore();

  // Zero lines
  doc.save();
  doc.strokeColor('#888888').lineWidth(0.9);
  if (zMin <= 0 && zMax >= 0) {
    const x0 = mapX(0);
    doc.moveTo(x0, plotY).lineTo(x0, plotY + plotH).stroke();
  }
  if (fMin <= 0 && fMax >= 0) {
    const y0 = mapY(0);
    doc.moveTo(plotX, y0).lineTo(plotX + plotW, y0).stroke();
  }
  doc.restore();

  // Curves
  for (const curve of curves) {
    const samples = curve.samples || [];
    if (samples.length < 2) continue;
    doc.save();
    doc.strokeColor(curveColor(curve.iteration, curve.leg)).lineWidth(1.3);
    doc.moveTo(mapX(samples[0].zRel), mapY(samples[0].fz));
    for (let i = 1; i < samples.length; ++i) {
      doc.lineTo(mapX(samples[i].zRel), mapY(samples[i].fz));
    }
    doc.stroke();
    doc.restore();
  }

  // Frame
  doc.rect(plotX, plotY, plotW, plotH).stroke('#222222');

  // Tick labels
  doc.fontSize(7).fillColor('#333');
  for (const z of zTicks) {
    const x = mapX(z);
    const label = z.toFixed(2);
    doc.text(label, x - 14, plotY + plotH + 4, { width: 28, align: 'center' });
  }
  for (const f of fTicks) {
    const y = mapY(f);
    const label = f.toFixed(2);
    doc.text(label, originX, y - 4, { width: leftPad - 6, align: 'right' });
  }

  // Axis titles
  doc.fontSize(9).fillColor('#111');
  doc.text('z (relativ) [mm]', plotX, plotY + plotH + 18, { width: plotW, align: 'center' });
  doc.save();
  doc.rotate(-90, { origin: [originX + 12, plotY + plotH / 2] });
  doc.text('F [N]', originX + 12 - 30, plotY + plotH / 2 - 8, { width: 60, align: 'center' });
  doc.restore();

  const captionY = originY + height + 4;
  doc.fontSize(8).fillColor('#333');
  doc.text(
    'F: Ein- bzw. Ausgliederkräfte.  z (relativ): Verfahrweg beim Eingliedern (−z) und Ausgliedern (+z); Start z = 0 am kraftfreien Ausgliederpunkt.',
    originX,
    captionY,
    { width }
  );
  return captionY + 28;
}

function buildPointPdfBuffer(analysis, point) {
  return new Promise((resolve, reject) => {
    const doc = new PDFDocument({ size: 'A4', margin: 48 });
    const chunks = [];
    doc.on('data', (c) => chunks.push(c));
    doc.on('end', () => resolve(Buffer.concat(chunks)));
    doc.on('error', reject);

    const program = analysis.programName || '—';
    doc.fontSize(16).fillColor('#000').text('Kraft-Weg-Auswertung', { underline: true });
    doc.moveDown(0.5);
    doc.fontSize(11).text(`Prüfauftrag: ${program}`);
    doc.text(`Prüfpunkt: ${point.name} (Index ${point.pointIndex})`);
    doc.moveDown(0.4);
    doc.text(
      `Eingliedern — Spitzenkraft Mittel: ${formatN(point.insert.meanPeakN)}, StdAbw: ${formatN(point.insert.stddevPeakN)} (${point.insert.count} Iter.)`
    );
    if ((point.insert.peaksN || []).length) {
      doc.fontSize(9).fillColor('#444').text(
        `  Einzelspitzen: ${(point.insert.peaksN || []).map((p) => p.toFixed(3)).join(' · ')} N`
      );
    }
    doc.fontSize(11).fillColor('#000').text(
      `Ausgliedern — Spitzenkraft Mittel: ${formatN(point.extract.meanPeakN)}, StdAbw: ${formatN(point.extract.stddevPeakN)} (${point.extract.count} Iter.)`
    );
    if ((point.extract.peaksN || []).length) {
      doc.fontSize(9).fillColor('#444').text(
        `  Einzelspitzen: ${(point.extract.peaksN || []).map((p) => p.toFixed(3)).join(' · ')} N`
      );
    }
    doc.moveDown(0.6);
    doc.fontSize(10).fillColor('#000').text(
      'Überlagerte Kurven (hell = Eingliedern, dunkel = Ausgliedern; Farbe = Iteration).'
    );
    doc.text('Ausglieder-Spitze nur Zug (F < 0); Restdruck nur im Diagramm.');

    const chartTop = doc.y + 8;
    const chartBottom = drawPointChart(doc, point, 48, chartTop, 500, 300);
    doc.y = chartBottom + 8;

    doc.fontSize(9);
    for (const curve of point.curves || []) {
      doc.fillColor(curveColor(curve.iteration, curve.leg));
      doc.text(
        `Iter ${curve.iteration} · ${legLabel(curve.leg)} · n=${curve.samples.length} · Spitze ${formatN(curve.peakN)}`,
        { continued: false }
      );
    }
    doc.fillColor('#000');
    doc.end();
  });
}

async function buildExportZip(analysis) {
  const zip = new JSZip();
  const safeProgram = String(analysis.programName || 'pruefung')
    .replace(/[^\w\-]+/g, '_')
    .slice(0, 40);
  zip.file(`${safeProgram}_samples.csv`, buildSamplesCsv(analysis));
  zip.file(`${safeProgram}_summary.csv`, buildSummaryCsv(analysis));
  for (const point of analysis.points || []) {
    const pdf = await buildPointPdfBuffer(analysis, point);
    const safePoint = String(point.name || `punkt_${point.pointIndex}`)
      .replace(/[^\w\-]+/g, '_')
      .slice(0, 40);
    zip.file(`${safeProgram}_${safePoint}.pdf`, pdf);
  }
  return zip.generateAsync({ type: 'nodebuffer' });
}

module.exports = {
  curveColor,
  legLabel,
  excelNum,
  buildSamplesCsv,
  buildSummaryCsv,
  buildPointPdfBuffer,
  buildExportZip,
};
