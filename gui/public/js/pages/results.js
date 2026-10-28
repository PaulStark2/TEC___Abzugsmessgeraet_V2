'use strict';

import { navigate } from '../router.js';
import { fetchCurrentResults } from '../api.js';

const LEG_INSERT = 0;
const LEG_EXTRACT = 1;

function escapeHtml(str) {
  return String(str)
    .replaceAll('&', '&amp;')
    .replaceAll('<', '&lt;')
    .replaceAll('>', '&gt;')
    .replaceAll('"', '&quot;')
    .replaceAll("'", '&#039;');
}

function formatN(value) {
  if (value === null || value === undefined || !Number.isFinite(Number(value))) return '—';
  return `${Number(value).toFixed(3)} N`;
}

function formatMm(value) {
  if (value === null || value === undefined || !Number.isFinite(Number(value))) return '—';
  return `${Number(value).toFixed(3)} mm`;
}

/** Same palette as server export: hue per iteration, light insert / dark extract. */
function curveColor(iteration, leg) {
  const hue = (Number(iteration) * 47) % 360;
  const light = leg === LEG_INSERT ? 62 : 38;
  return `hsl(${hue} 70% ${light}%)`;
}

function legLabel(leg) {
  return leg === LEG_INSERT ? 'Eingliedern' : 'Ausgliedern';
}

export function renderResults(container) {
  container.innerHTML = `
    <section class="panel page-panel results-page">
      <h2>Prüfergebnisse</h2>
      <p id="results-feedback" class="command-feedback" aria-live="polite"></p>
      <div id="results-content"></div>
    </section>
  `;

  const feedbackEl = container.querySelector('#results-feedback');
  const contentEl = container.querySelector('#results-content');

  function setFeedback(text, isError) {
    feedbackEl.textContent = text || '';
    feedbackEl.classList.toggle('command-feedback--error', Boolean(isError));
  }

  async function refresh() {
    const result = await fetchCurrentResults();
    if (!result.ok) {
      contentEl.innerHTML = `
        <div class="error-card">
          <h3>Keine Ergebnisse</h3>
          <p class="placeholder-note">
            Noch keine Prozedur abgeschlossen. Bitte unter „Prüf-/Alterungsprozedur“ starten
            (Modus Prüfung, nicht Alterung).
          </p>
          <div class="control-actions page-actions">
            <button type="button" id="btn-go-procedure">Zur Prozedur</button>
            <button type="button" class="btn-back" data-route="menu">Zurück</button>
          </div>
        </div>
      `;
      contentEl.querySelector('#btn-go-procedure').addEventListener('click', () => navigate('procedure'));
      contentEl.querySelector('.btn-back').addEventListener('click', () => navigate('menu'));
      setFeedback('', false);
      return;
    }

    const r = result.results;
    const analysis = r.analysis || { points: [], curves: r.curves || [] };
    const points = analysis.points || [];

    if (!points.length) {
      contentEl.innerHTML = `
        <div class="success-card">
          <h3>Letzter Lauf</h3>
          <p class="placeholder-note">
            Programm: <b>${escapeHtml(r.programName ?? '—')}</b><br/>
            Backend: <b>${escapeHtml(r.backend ?? '—')}</b><br/>
            Modus: <b>${escapeHtml(r.mode ?? '—')}</b><br/>
            Abgeschlossen: <b>${escapeHtml(r.completedAt ?? '—')}</b><br/>
            Datenvollständigkeit: <b>${escapeHtml(r.dataCompleteness ?? '—')}</b>
          </p>
          <p class="placeholder-note">
            Keine Kraft-Weg-Kurven empfangen (Alterung, Firmware ohne Stream, oder noch keine Chunks).
          </p>
        </div>
        <div class="control-actions page-actions">
          <button type="button" id="btn-go-procedure">Zur Prozedur</button>
          <button type="button" class="btn-back" data-route="menu">Zurück</button>
        </div>
      `;
      contentEl.querySelector('#btn-go-procedure').addEventListener('click', () => navigate('procedure'));
      contentEl.querySelector('.btn-back').addEventListener('click', () => navigate('menu'));
      setFeedback(
        r.dataCompleteness === 'partial'
          ? 'Keine Kurven — Stream prüfen (STM32 muss MsgForcePathChunk senden).'
          : '',
        true
      );
      return;
    }

    let selectedPointIndex = points[0].pointIndex;
    let tableLeg = LEG_INSERT;
    const visibility = new Map();
    for (const p of points) {
      for (const c of p.curves) {
        visibility.set(`${c.pointIndex}:${c.iteration}:${c.leg}`, true);
      }
    }
    let highlightSample = null;

    function selectedPoint() {
      return points.find((p) => p.pointIndex === selectedPointIndex) || points[0];
    }

    function renderChartSvg(point) {
      const curves = (point.curves || []).filter(
        (c) => visibility.get(`${c.pointIndex}:${c.iteration}:${c.leg}`) !== false
      );
      const w = 640;
      const h = 320;
      const ml = 52;
      const mr = 16;
      const mt = 16;
      const mb = 40;
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
        return `<p class="placeholder-note">Keine sichtbaren Kurven.</p>`;
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
      const mapX = (z) => ml + ((z - zMin) / (zMax - zMin)) * (w - ml - mr);
      const mapY = (f) => mt + (1 - (f - fMin) / (fMax - fMin)) * (h - mt - mb);

      const polylines = curves
        .map((c) => {
          const pts = (c.samples || [])
            .map((s) => `${mapX(s.zRel).toFixed(1)},${mapY(s.fz).toFixed(1)}`)
            .join(' ');
          const color = curveColor(c.iteration, c.leg);
          const key = `${c.pointIndex}:${c.iteration}:${c.leg}`;
          return `<polyline class="force-curve-line" data-curve="${escapeHtml(key)}" fill="none" stroke="${color}" stroke-width="2" points="${pts}" />`;
        })
        .join('');

      let markers = '';
      if (highlightSample) {
        const { curveKey, sampleIndex } = highlightSample;
        const curve = point.curves.find(
          (c) => `${c.pointIndex}:${c.iteration}:${c.leg}` === curveKey
        );
        const s = curve && curve.samples[sampleIndex];
        if (s) {
          markers = `<circle cx="${mapX(s.zRel).toFixed(1)}" cy="${mapY(s.fz).toFixed(1)}" r="5" fill="#111" stroke="#fff" stroke-width="1.5" />`;
        }
      }

      return `
        <svg class="force-chart-svg" viewBox="0 0 ${w} ${h}" role="img" aria-label="Kraft-Weg-Diagramm">
          <rect x="${ml}" y="${mt}" width="${w - ml - mr}" height="${h - mt - mb}" fill="#fafafa" stroke="#ccc" />
          <text x="12" y="${h / 2}" class="axis-label" transform="rotate(-90 12 ${h / 2})">Kraft Fz [N]</text>
          <text x="${w / 2}" y="${h - 8}" class="axis-label" text-anchor="middle">Weg relativ [mm]</text>
          <text x="${ml}" y="${h - 12}" class="axis-tick">${zMin.toFixed(2)}</text>
          <text x="${w - mr}" y="${h - 12}" class="axis-tick" text-anchor="end">${zMax.toFixed(2)}</text>
          <text x="${ml - 6}" y="${mt + 10}" class="axis-tick" text-anchor="end">${fMax.toFixed(2)}</text>
          <text x="${ml - 6}" y="${h - mb}" class="axis-tick" text-anchor="end">${fMin.toFixed(2)}</text>
          ${polylines}
          ${markers}
        </svg>`;
    }

    function renderTable(point) {
      const curves = (point.curves || []).filter((c) => c.leg === tableLeg);
      const rows = [];
      for (const c of curves) {
        for (const s of c.samples || []) {
          rows.push({ curve: c, sample: s });
        }
      }
      if (!rows.length) {
        return `<p class="placeholder-note">Keine Messpaare für ${escapeHtml(legLabel(tableLeg))}.</p>`;
      }
      return `
        <table class="program-table results-table results-sample-table">
          <thead>
            <tr>
              <th>Iter</th>
              <th>Index</th>
              <th>z abs [mm]</th>
              <th>z rel [mm]</th>
              <th>Fz [N]</th>
            </tr>
          </thead>
          <tbody>
            ${rows
              .map(({ curve, sample }) => {
                const key = `${curve.pointIndex}:${curve.iteration}:${curve.leg}`;
                const active =
                  highlightSample &&
                  highlightSample.curveKey === key &&
                  highlightSample.sampleIndex === sample.index;
                return `<tr class="${active ? 'is-highlighted' : ''}" data-curve="${escapeHtml(key)}" data-sample="${sample.index}">
                  <td>${curve.iteration}</td>
                  <td>${sample.index}</td>
                  <td>${formatMm(sample.z).replace(' mm', '')}</td>
                  <td>${formatMm(sample.zRel).replace(' mm', '')}</td>
                  <td>${Number(sample.fz).toFixed(3)}</td>
                </tr>`;
              })
              .join('')}
          </tbody>
        </table>`;
    }

    function paint() {
      const point = selectedPoint();
      contentEl.innerHTML = `
        <div class="success-card">
          <h3>Letzter Lauf</h3>
          <p class="placeholder-note">
            Programm: <b>${escapeHtml(r.programName ?? '—')}</b><br/>
            Backend: <b>${escapeHtml(r.backend ?? '—')}</b><br/>
            Modus: <b>${escapeHtml(r.mode ?? '—')}</b><br/>
            Abgeschlossen: <b>${escapeHtml(r.completedAt ?? '—')}</b><br/>
            Datenvollständigkeit: <b>${escapeHtml(r.dataCompleteness ?? '—')}</b>
          </p>
        </div>

        <div class="results-toolbar">
          <label>
            Prüfpunkt
            <select id="results-point-select">
              ${points
                .map(
                  (p) =>
                    `<option value="${p.pointIndex}" ${
                      p.pointIndex === selectedPointIndex ? 'selected' : ''
                    }>${escapeHtml(p.name)}</option>`
                )
                .join('')}
            </select>
          </label>
          <a class="btn-link" href="/api/results/export" download>Download (ZIP: CSV + PDF)</a>
        </div>

        <div class="results-stats">
          <div class="stat-card">
            <h4>Eingliedern — Spitzenkraft</h4>
            <p class="stat-value">${formatN(point.insert.meanPeakN)}</p>
            <p class="stat-sub">Mittelwert über ${point.insert.count} Iteration(en)</p>
            <p class="stat-value stat-value--secondary">${formatN(point.insert.stddevPeakN)}</p>
            <p class="stat-sub">Standardabweichung</p>
          </div>
          <div class="stat-card">
            <h4>Ausgliedern — Spitzenkraft</h4>
            <p class="stat-value">${formatN(point.extract.meanPeakN)}</p>
            <p class="stat-sub">Mittelwert über ${point.extract.count} Iteration(en)</p>
            <p class="stat-value stat-value--secondary">${formatN(point.extract.stddevPeakN)}</p>
            <p class="stat-sub">Standardabweichung</p>
          </div>
        </div>

        <div class="results-chart-block">
          <h3>Kraft-Weg-Diagramm</h3>
          <p class="placeholder-note">Weg relativ zum Kurvenstart. Hell = Eingliedern, dunkel = Ausgliedern; Farbe = Iteration.</p>
          <div class="curve-toggles" id="curve-toggles">
            ${(point.curves || [])
              .map((c) => {
                const key = `${c.pointIndex}:${c.iteration}:${c.leg}`;
                const checked = visibility.get(key) !== false ? 'checked' : '';
                const color = curveColor(c.iteration, c.leg);
                return `<label class="curve-toggle">
                  <input type="checkbox" data-curve="${escapeHtml(key)}" ${checked} />
                  <span class="swatch" style="background:${color}"></span>
                  Iter ${c.iteration} · ${escapeHtml(legLabel(c.leg))}
                </label>`;
              })
              .join('')}
          </div>
          <div id="force-chart">${renderChartSvg(point)}</div>
        </div>

        <div class="results-table-block">
          <div class="results-toolbar">
            <h3>Messwertpaare</h3>
            <div class="leg-toggle" role="group" aria-label="Richtung">
              <button type="button" id="btn-leg-insert" class="${
                tableLeg === LEG_INSERT ? 'is-active' : ''
              }">Eingliedern</button>
              <button type="button" id="btn-leg-extract" class="${
                tableLeg === LEG_EXTRACT ? 'is-active' : ''
              }">Ausgliedern</button>
            </div>
          </div>
          <div id="sample-table">${renderTable(point)}</div>
        </div>

        <div class="control-actions page-actions">
          <button type="button" id="btn-go-procedure">Zur Prozedur</button>
          <button type="button" class="btn-back" data-route="menu">Zurück</button>
        </div>
      `;

      contentEl.querySelector('#results-point-select').addEventListener('change', (e) => {
        selectedPointIndex = Number(e.target.value);
        highlightSample = null;
        paint();
      });
      contentEl.querySelector('#curve-toggles').addEventListener('change', (e) => {
        const input = e.target.closest('input[data-curve]');
        if (!input) return;
        visibility.set(input.getAttribute('data-curve'), input.checked);
        paint();
      });
      contentEl.querySelector('#btn-leg-insert').addEventListener('click', () => {
        tableLeg = LEG_INSERT;
        paint();
      });
      contentEl.querySelector('#btn-leg-extract').addEventListener('click', () => {
        tableLeg = LEG_EXTRACT;
        paint();
      });
      contentEl.querySelector('#sample-table').addEventListener('click', (e) => {
        const tr = e.target.closest('tr[data-curve]');
        if (!tr) return;
        highlightSample = {
          curveKey: tr.getAttribute('data-curve'),
          sampleIndex: Number(tr.getAttribute('data-sample')),
        };
        paint();
        const again = contentEl.querySelector(
          `tr[data-curve="${CSS.escape(highlightSample.curveKey)}"][data-sample="${highlightSample.sampleIndex}"]`
        );
        if (again) again.scrollIntoView({ block: 'nearest' });
      });
      contentEl.querySelector('#btn-go-procedure').addEventListener('click', () => navigate('procedure'));
      contentEl.querySelector('.btn-back').addEventListener('click', () => navigate('menu'));
    }

    paint();
    setFeedback(
      r.dataCompleteness === 'partial'
        ? 'Teilweise Controller-Daten — fehlende Kurven prüfen.'
        : 'Kraft-Weg-Auswertung aus gestreamten Messwerten.',
      false
    );
  }

  refresh().catch((err) => setFeedback(err.message || String(err), true));
  return () => {};
}
