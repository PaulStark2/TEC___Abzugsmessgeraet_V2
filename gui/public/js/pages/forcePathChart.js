'use strict';

/**
 * Chart.js Kraft-Weg diagram for Prüfergebnisse.
 * Expects Chart (+ zoom plugin) on window from UMD vendor scripts in index.html.
 */

const LEG_INSERT = 0;

function getChartCtor() {
  const Chart = typeof window !== 'undefined' ? window.Chart : null;
  if (!Chart) {
    throw new Error('Chart.js not loaded (check /vendor/chart.js UMD scripts)');
  }
  return Chart;
}

function curveKey(curve) {
  return `${curve.pointIndex}:${curve.iteration}:${curve.leg}`;
}

function legLabel(leg) {
  return Number(leg) === LEG_INSERT ? 'Eingliedern' : 'Ausgliedern';
}

/** GUI dark-chart palette: hue per iteration, lighter insert / darker extract. */
export function curveColor(iteration, leg) {
  const hue = (Number(iteration) * 47) % 360;
  const light = Number(leg) === LEG_INSERT ? 72 : 48;
  return `hsl(${hue} 75% ${light}%)`;
}

function buildDatasets(curves, visibility) {
  return (curves || []).map((c) => {
    const key = curveKey(c);
    const color = curveColor(c.iteration, c.leg);
    const hidden = visibility.get(key) === false;
    const data = (c.samples || []).map((s) => ({
      x: Number(s.zRel),
      y: Number(s.fz),
      sampleIndex: s.index,
      zAbs: Number(s.z),
      curveKey: key,
    }));
    return {
      label: `Iter ${c.iteration} · ${legLabel(c.leg)}`,
      curveKey: key,
      data,
      hidden,
      borderColor: color,
      backgroundColor: color,
      borderWidth: 2,
      pointRadius: 0,
      pointHoverRadius: 4,
      pointHitRadius: 8,
      tension: 0,
      showLine: true,
      parsing: false,
    };
  });
}

function applyHighlight(chart, highlight) {
  if (!chart) return;
  for (const ds of chart.data.datasets) {
    const n = ds.data.length;
    const radii = new Array(n).fill(0);
    const borders = new Array(n).fill(ds.borderColor);
    const widths = new Array(n).fill(0);
    if (
      highlight &&
      ds.curveKey === highlight.curveKey &&
      Number.isInteger(highlight.sampleIndex) &&
      highlight.sampleIndex >= 0 &&
      highlight.sampleIndex < n
    ) {
      radii[highlight.sampleIndex] = 5;
      borders[highlight.sampleIndex] = '#f1f3f5';
      widths[highlight.sampleIndex] = 2;
    }
    ds.pointRadius = radii;
    ds.pointBorderColor = borders;
    ds.pointBorderWidth = widths;
    ds.pointBackgroundColor = ds.borderColor;
  }
}

/**
 * @param {HTMLCanvasElement} canvas
 * @param {{ curves: object[], visibility: Map, highlight?: object|null, onPointClick?: Function, onVisibilityChange?: Function }} opts
 */
export function createForcePathChart(canvas, opts) {
  const Chart = getChartCtor();
  const { curves, visibility, highlight = null, onPointClick, onVisibilityChange } = opts;
  const chart = new Chart(canvas, {
    type: 'line',
    data: { datasets: buildDatasets(curves, visibility) },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      animation: false,
      interaction: {
        mode: 'nearest',
        intersect: false,
        axis: 'xy',
      },
      plugins: {
        title: {
          display: true,
          text: 'Kraft über Weg (0 = kraftfrei)',
          color: '#e8eaed',
          font: { size: 14, weight: '600' },
        },
        legend: {
          // Curve visibility is controlled by the scrollable #curve-toggles list.
          display: false,
        },
        tooltip: {
          backgroundColor: 'rgba(16, 24, 32, 0.94)',
          titleColor: '#f1f3f5',
          bodyColor: '#d7dbe2',
          borderColor: 'rgba(255,255,255,0.15)',
          borderWidth: 1,
          callbacks: {
            title(items) {
              if (!items.length) return '';
              const raw = items[0].raw || {};
              return `Weg ${Number(raw.x).toFixed(3)} mm (0 = kraftfrei)`;
            },
            label(ctx) {
              const raw = ctx.raw || {};
              const parts = [ctx.dataset.label || '', `Fz ${Number(raw.y).toFixed(3)} N`];
              if (Number.isFinite(raw.zAbs)) {
                parts.push(`z ${Number(raw.zAbs).toFixed(3)} mm`);
              }
              return parts.join(' · ');
            },
          },
        },
        decimation: {
          enabled: true,
          algorithm: 'lttb',
          samples: 800,
          threshold: 1000,
        },
        zoom: {
          limits: {
            x: { min: 'original', max: 'original' },
            y: { min: 'original', max: 'original' },
          },
          pan: {
            enabled: true,
            mode: 'xy',
          },
          zoom: {
            wheel: { enabled: true },
            pinch: { enabled: true },
            drag: { enabled: false },
            mode: 'xy',
          },
        },
      },
      scales: {
        x: {
          type: 'linear',
          title: {
            display: true,
            text: 'z (relativ) [mm]',
            color: '#c5c9d0',
          },
          grid: { color: 'rgba(255,255,255,0.12)' },
          border: { color: 'rgba(255,255,255,0.25)' },
          ticks: {
            color: '#aeb4be',
            callback(v) {
              return Number(v).toFixed(2);
            },
          },
        },
        y: {
          type: 'linear',
          title: {
            display: true,
            text: 'F [N]',
            color: '#c5c9d0',
          },
          grid: { color: 'rgba(255,255,255,0.12)' },
          border: { color: 'rgba(255,255,255,0.25)' },
          ticks: {
            color: '#aeb4be',
            callback(v) {
              return Number(v).toFixed(2);
            },
          },
        },
      },
      onClick(_event, elements) {
        if (!elements.length || typeof onPointClick !== 'function') return;
        const el = elements[0];
        const ds = chart.data.datasets[el.datasetIndex];
        const raw = ds.data[el.index];
        if (!raw) return;
        onPointClick({
          curveKey: ds.curveKey || raw.curveKey,
          sampleIndex: raw.sampleIndex,
        });
      },
    },
  });

  applyHighlight(chart, highlight);
  chart.update('none');

  canvas.addEventListener('dblclick', () => {
    if (typeof chart.resetZoom === 'function') {
      chart.resetZoom();
    }
  });

  return chart;
}

export function updateForcePathVisibility(chart, visibility) {
  if (!chart) return;
  chart.data.datasets.forEach((ds, index) => {
    const show = visibility.get(ds.curveKey) !== false;
    ds.hidden = !show;
    const meta = chart.getDatasetMeta(index);
    if (meta) meta.hidden = !show;
  });
  chart.update('none');
}

export function updateForcePathHighlight(chart, highlight) {
  if (!chart) return;
  applyHighlight(chart, highlight);
  chart.update('none');
}

export function destroyForcePathChart(chart) {
  if (chart && typeof chart.destroy === 'function') {
    chart.destroy();
  }
}
