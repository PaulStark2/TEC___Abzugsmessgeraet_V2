'use strict';

/**
 * Build CSV / PDF / ZIP downloads for Kraft-Weg evaluation results.
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

function csvEscape(value) {
  const s = String(value ?? '');
  if (/[",\n\r]/.test(s)) return `"${s.replace(/"/g, '""')}"`;
  return s;
}

function buildSamplesCsv(analysis) {
  const header = [
    'pointIndex',
    'pointName',
    'iteration',
    'leg',
    'legName',
    'sampleIndex',
    'z_mm',
    'z_rel_mm',
    'fz_N',
  ];
  const lines = [header.join(',')];
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
            Number(s.z).toFixed(4),
            Number(s.zRel).toFixed(4),
            Number(s.fz).toFixed(4),
          ].join(',')
        );
      }
    }
  }
  return `${lines.join('\n')}\n`;
}

function buildSummaryCsv(analysis) {
  const header = [
    'pointIndex',
    'pointName',
    'leg',
    'legName',
    'peakCount',
    'meanPeak_N',
    'stddevPeak_N',
  ];
  const lines = [header.join(',')];
  for (const point of analysis.points || []) {
    for (const [leg, stats] of [
      [LEG_INSERT, point.insert],
      [LEG_EXTRACT, point.extract],
    ]) {
      lines.push(
        [
          point.pointIndex,
          csvEscape(point.name),
          leg,
          csvEscape(legLabel(leg)),
          stats.count,
          stats.meanPeakN == null ? '' : Number(stats.meanPeakN).toFixed(4),
          stats.stddevPeakN == null ? '' : Number(stats.stddevPeakN).toFixed(4),
        ].join(',')
      );
    }
  }
  return `${lines.join('\n')}\n`;
}

function formatN(v) {
  if (v == null || !Number.isFinite(v)) return '—';
  return `${v.toFixed(3)} N`;
}

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
    doc.fontSize(10).text('Keine Messwerte.', originX, originY);
    return;
  }
  if (zMax - zMin < 1e-9) {
    zMin -= 0.5;
    zMax += 0.5;
  }
  if (fMax - fMin < 1e-9) {
    fMin -= 0.5;
    fMax += 0.5;
  }
  const pad = 0.05;
  const zSpan = zMax - zMin;
  const fSpan = fMax - fMin;
  zMin -= zSpan * pad;
  zMax += zSpan * pad;
  fMin -= fSpan * pad;
  fMax += fSpan * pad;

  doc.rect(originX, originY, width, height).stroke('#333333');

  const mapX = (z) => originX + ((z - zMin) / (zMax - zMin)) * width;
  const mapY = (f) => originY + height - ((f - fMin) / (fMax - fMin)) * height;

  for (const curve of curves) {
    const samples = curve.samples || [];
    if (samples.length < 2) continue;
    doc.save();
    doc.strokeColor(curveColor(curve.iteration, curve.leg)).lineWidth(1.2);
    doc.moveTo(mapX(samples[0].zRel), mapY(samples[0].fz));
    for (let i = 1; i < samples.length; ++i) {
      doc.lineTo(mapX(samples[i].zRel), mapY(samples[i].fz));
    }
    doc.stroke();
    doc.restore();
  }

  doc.fontSize(8).fillColor('#222');
  doc.text(`Weg rel. [mm]: ${zMin.toFixed(2)} … ${zMax.toFixed(2)}`, originX, originY + height + 4, {
    width,
  });
  doc.text(`Kraft Fz [N]: ${fMin.toFixed(2)} … ${fMax.toFixed(2)}`, originX, originY + height + 16, {
    width,
  });
}

function buildPointPdfBuffer(analysis, point) {
  return new Promise((resolve, reject) => {
    const doc = new PDFDocument({ size: 'A4', margin: 48 });
    const chunks = [];
    doc.on('data', (c) => chunks.push(c));
    doc.on('end', () => resolve(Buffer.concat(chunks)));
    doc.on('error', reject);

    const program = analysis.programName || '—';
    doc.fontSize(16).text('Kraft-Weg-Auswertung', { underline: true });
    doc.moveDown(0.5);
    doc.fontSize(11).text(`Prüfauftrag: ${program}`);
    doc.text(`Prüfpunkt: ${point.name} (Index ${point.pointIndex})`);
    doc.moveDown(0.5);
    doc.text(`Eingliedern — Spitzenkraft Mittel: ${formatN(point.insert.meanPeakN)}, StdAbw: ${formatN(point.insert.stddevPeakN)} (${point.insert.count} Iter.)`);
    doc.text(`Ausgliedern — Spitzenkraft Mittel: ${formatN(point.extract.meanPeakN)}, StdAbw: ${formatN(point.extract.stddevPeakN)} (${point.extract.count} Iter.)`);
    doc.moveDown(0.8);
    doc.fontSize(10).text('Überlagerte Kurven (hell = Eingliedern, dunkel = Ausgliedern; Farbe = Iteration)');
    drawPointChart(doc, point, 48, 200, 500, 280);

    doc.moveDown(18);
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
  buildSamplesCsv,
  buildSummaryCsv,
  buildPointPdfBuffer,
  buildExportZip,
};
