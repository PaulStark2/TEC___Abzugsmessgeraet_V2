'use strict';

import { navigate } from '../router.js';
import {
  clearCurrentResults,
  fetchCurrentResults,
  fetchResultsHistory,
  loadDemoResults,
} from '../api.js';
import {
  curveColor,
  createForcePathChart,
  destroyForcePathChart,
  updateForcePathHighlight,
  updateForcePathVisibility,
} from './forcePathChart.js';

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

function legLabel(leg) {
  return leg === LEG_INSERT ? 'Eingliedern' : 'Ausgliedern';
}

/** Compact, collapsible per-iteration peak list for one leg. */
function renderPeakDetails(point, leg) {
  const rows = (point.curves || [])
    .filter((c) => c.leg === leg && c.peakN != null && Number.isFinite(Number(c.peakN)))
    .slice()
    .sort((a, b) => a.iteration - b.iteration);
  if (!rows.length) {
    return `<details class="peak-details"><summary>Einzelspitzen</summary><p class="stat-sub">Keine Spitzenwerte.</p></details>`;
  }
  return `<details class="peak-details">
    <summary>Einzelspitzen (${rows.length})</summary>
    <div class="peak-details-scroll" role="list">
      ${rows
        .map(
          (c) =>
            `<div class="peak-details-row" role="listitem">Iter ${c.iteration} · ${escapeHtml(
              formatN(c.peakN)
            )}</div>`
        )
        .join('')}
    </div>
  </details>`;
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
  let chartInstance = null;
  let selectedIndex = 0;

  function setFeedback(text, isError) {
    feedbackEl.textContent = text || '';
    feedbackEl.classList.toggle('command-feedback--error', Boolean(isError));
  }

  function teardownChart() {
    destroyForcePathChart(chartInstance);
    chartInstance = null;
  }

  function formatHistoryLabel(h, i) {
    const when = h.completedAt
      ? (() => {
          try {
            return new Date(h.completedAt).toLocaleString();
          } catch {
            return h.completedAt;
          }
        })()
      : 'ohne Zeitstempel';
    const prog = h.programName || 'Programm —';
    const mode = h.mode === 'aging' ? 'Altern' : h.mode === 'test' ? 'Prüfen' : h.mode || '—';
    const outcome = h.outcome || '—';
    return `#${i + 1} · ${when} · ${prog} · ${mode} · ${outcome}`;
  }

  function buildHistorySelectHtml(history) {
    if (!history.length) return '';
    return `<label class="results-history-select">Gespeicherte Messungen (letzte ${history.length})
            <select id="results-history-index">
              ${history
                .map((h, i) => {
                  return `<option value="${i}" ${i === selectedIndex ? 'selected' : ''}>${escapeHtml(
                    formatHistoryLabel(h, i)
                  )}</option>`;
                })
                .join('')}
            </select>
          </label>
          <p class="placeholder-note">Unabhängig vom geladenen Programm — Historie bleibt nach Neustart erhalten.</p>`;
  }

  function wireHistorySelect() {
    const sel = contentEl.querySelector('#results-history-index');
    if (!sel) return;
    sel.addEventListener('change', () => {
      selectedIndex = Number(sel.value) || 0;
      refresh().catch((err) => setFeedback(err.message || String(err), true));
    });
  }

  async function refresh() {
    teardownChart();
    let history = [];
    try {
      const hist = await fetchResultsHistory();
      if (hist && hist.ok && Array.isArray(hist.history)) {
        history = hist.history;
      }
    } catch {
      history = [];
    }
    if (selectedIndex >= history.length) {
      selectedIndex = 0;
    }

    if (!history.length) {
      contentEl.innerHTML = `
        <div class="error-card">
          <h3>Keine Ergebnisse</h3>
          <p class="placeholder-note">
            Noch keine gespeicherte Messung. Bitte unter „Prüf-/Alterungsprozedur“ einen Lauf
            abschließen (oder abbrechen) — oder Demo-Testdaten laden.
            Ein geladenes Prüfprogramm ist für die Anzeige der Historie nicht erforderlich.
          </p>
          <div class="control-actions page-actions">
            <button type="button" id="btn-load-demo">Demo-Testdaten laden</button>
            <button type="button" id="btn-go-procedure">Zur Prozedur</button>
            <button type="button" class="btn-back" data-route="menu">Zurück</button>
          </div>
        </div>
      `;
      contentEl.querySelector('#btn-load-demo').addEventListener('click', () => {
        const ok = window.confirm(
          'Demo-Testdaten laden?\n\nDas sind keine Messwerte vom Prüfstand. Vor echten Läufen wieder löschen oder eine Prüfung starten.'
        );
        if (!ok) return;
        setFeedback('Lade Demo-Testdaten…', false);
        loadDemoResults()
          .then(() => refresh())
          .catch((err) => setFeedback(err.message || String(err), true));
      });
      contentEl.querySelector('#btn-go-procedure').addEventListener('click', () => navigate('procedure'));
      contentEl.querySelector('.btn-back').addEventListener('click', () => navigate('menu'));
      setFeedback('', false);
      return;
    }

    const result = await fetchCurrentResults(selectedIndex);
    if (!result.ok || !result.results) {
      contentEl.innerHTML = `
        ${buildHistorySelectHtml(history)}
        <div class="error-card">
          <h3>Messung nicht ladbar</h3>
          <p class="placeholder-note">
            ${escapeHtml(result.error || 'Die ausgewählte Historie-Messung konnte nicht geladen werden.')}
          </p>
          <div class="control-actions page-actions">
            <button type="button" class="btn-back" data-route="menu">Zurück</button>
          </div>
        </div>
      `;
      wireHistorySelect();
      contentEl.querySelector('.btn-back').addEventListener('click', () => navigate('menu'));
      setFeedback(result.error || 'Messung nicht ladbar', true);
      return;
    }

    const r = result.results;
    const analysis = r.analysis || { points: [], curves: r.curves || [] };
    const points = analysis.points || [];
    const historySelectHtml = buildHistorySelectHtml(history);

    if (!points.length) {
      contentEl.innerHTML = `
        ${historySelectHtml}
        <div class="success-card">
          <h3>Messung</h3>
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
      wireHistorySelect();
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

    function syncToggleCheckboxes() {
      contentEl.querySelectorAll('#curve-toggles input[data-curve]').forEach((input) => {
        const key = input.getAttribute('data-curve');
        input.checked = visibility.get(key) !== false;
      });
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

    function refreshTableOnly() {
      const host = contentEl.querySelector('#sample-table');
      if (!host) return;
      host.innerHTML = renderTable(selectedPoint());
      if (highlightSample) {
        const again = host.querySelector(
          `tr[data-curve="${CSS.escape(highlightSample.curveKey)}"][data-sample="${highlightSample.sampleIndex}"]`
        );
        if (again) again.scrollIntoView({ block: 'nearest' });
      }
    }

    function mountChart() {
      teardownChart();
      const canvas = contentEl.querySelector('#force-chart-canvas');
      if (!canvas) return;
      const point = selectedPoint();
      try {
        chartInstance = createForcePathChart(canvas, {
          curves: point.curves || [],
          visibility,
          highlight: highlightSample,
          onVisibilityChange(key, visible) {
            visibility.set(key, visible);
            syncToggleCheckboxes();
          },
          onPointClick({ curveKey, sampleIndex }) {
            highlightSample = { curveKey, sampleIndex };
            const parts = String(curveKey).split(':');
            const leg = Number(parts[2]);
            if (leg === LEG_INSERT || leg === LEG_EXTRACT) {
              tableLeg = leg;
              contentEl.querySelector('#btn-leg-insert')?.classList.toggle('is-active', tableLeg === LEG_INSERT);
              contentEl.querySelector('#btn-leg-extract')?.classList.toggle(
                'is-active',
                tableLeg === LEG_EXTRACT
              );
            }
            updateForcePathHighlight(chartInstance, highlightSample);
            refreshTableOnly();
          },
        });
      } catch (err) {
        const host = contentEl.querySelector('#force-chart');
        if (host) {
          host.innerHTML = `<p class="placeholder-note command-feedback--error">${escapeHtml(
            err.message || String(err)
          )}</p>`;
        }
      }
    }

    function paint() {
      teardownChart();
      const point = selectedPoint();
      const isDemo = r.backend === 'demo';
      contentEl.innerHTML = `
        ${historySelectHtml}
        ${
          isDemo
            ? `<div class="error-card results-demo-banner">
          <h3>Demo-Testdaten aktiv</h3>
          <p class="placeholder-note">
            Keine Messwerte vom Prüfstand. Vor realer Auswertung löschen oder eine echte Prüfung abschließen.
          </p>
        </div>`
            : ''
        }
        <div class="success-card">
          <h3>Messung</h3>
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
            <p class="stat-sub">Standardabweichung (Stichprobe, n−1)</p>
            ${renderPeakDetails(point, LEG_INSERT)}
          </div>
          <div class="stat-card">
            <h4>Ausgliedern — Spitzenkraft</h4>
            <p class="stat-value">${formatN(point.extract.meanPeakN)}</p>
            <p class="stat-sub">Mittelwert über ${point.extract.count} Iteration(en)</p>
            <p class="stat-value stat-value--secondary">${formatN(point.extract.stddevPeakN)}</p>
            <p class="stat-sub">Standardabweichung (Stichprobe, n−1)</p>
            ${renderPeakDetails(point, LEG_EXTRACT)}
          </div>
        </div>

        <div class="results-chart-block">
          <h3>Kraft-Weg-Diagramm</h3>
          <p class="placeholder-note">
            z (relativ): 0 = kraftfrei (Ausgliedern), negativ = Richtung Sitz (Eingliedern).
            Hell = Eingliedern (Druck), dunkel = Ausgliedern (Zug); Farbe = Iteration.
            Ausglieder-Spitze nur aus Zug (F&nbsp;&lt;&nbsp;0). Scrollen = Zoom, Ziehen = Pan, Doppelklick = Reset.
          </p>
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
          <div id="force-chart" class="force-chart-host">
            <canvas id="force-chart-canvas" aria-label="Kraft-Weg-Diagramm"></canvas>
          </div>
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
          <div id="sample-table" class="results-sample-scroll">${renderTable(point)}</div>
        </div>

        <div class="control-actions page-actions">
          ${
            isDemo
              ? `<button type="button" id="btn-clear-demo">Demo-Daten löschen</button>`
              : ''
          }
          <button type="button" id="btn-go-procedure">Zur Prozedur</button>
          <button type="button" class="btn-back" data-route="menu">Zurück</button>
        </div>
      `;

      const clearDemoBtn = contentEl.querySelector('#btn-clear-demo');
      if (clearDemoBtn) {
        clearDemoBtn.addEventListener('click', () => {
          setFeedback('Lösche Demo-Daten…', false);
          clearCurrentResults()
            .then(() => refresh())
            .catch((err) => setFeedback(err.message || String(err), true));
        });
      }
      contentEl.querySelector('#results-point-select').addEventListener('change', (e) => {
        selectedPointIndex = Number(e.target.value);
        highlightSample = null;
        paint();
      });
      contentEl.querySelector('#curve-toggles').addEventListener('change', (e) => {
        const input = e.target.closest('input[data-curve]');
        if (!input) return;
        visibility.set(input.getAttribute('data-curve'), input.checked);
        updateForcePathVisibility(chartInstance, visibility);
      });
      contentEl.querySelector('#btn-leg-insert').addEventListener('click', () => {
        tableLeg = LEG_INSERT;
        contentEl.querySelector('#btn-leg-insert').classList.add('is-active');
        contentEl.querySelector('#btn-leg-extract').classList.remove('is-active');
        refreshTableOnly();
      });
      contentEl.querySelector('#btn-leg-extract').addEventListener('click', () => {
        tableLeg = LEG_EXTRACT;
        contentEl.querySelector('#btn-leg-extract').classList.add('is-active');
        contentEl.querySelector('#btn-leg-insert').classList.remove('is-active');
        refreshTableOnly();
      });
      contentEl.querySelector('#sample-table').addEventListener('click', (e) => {
        const tr = e.target.closest('tr[data-curve]');
        if (!tr) return;
        highlightSample = {
          curveKey: tr.getAttribute('data-curve'),
          sampleIndex: Number(tr.getAttribute('data-sample')),
        };
        updateForcePathHighlight(chartInstance, highlightSample);
        refreshTableOnly();
      });
      contentEl.querySelector('#btn-go-procedure').addEventListener('click', () => navigate('procedure'));
      contentEl.querySelector('.btn-back').addEventListener('click', () => navigate('menu'));
      wireHistorySelect();

      mountChart();
    }

    paint();
    setFeedback(
      r.backend === 'demo'
        ? 'Demo-Testdaten (ohne Prüfstand) — zur GUI-Validierung.'
        : r.dataCompleteness === 'partial'
          ? 'Teilweise Controller-Daten — fehlende Kurven prüfen.'
          : 'Kraft-Weg-Auswertung aus gestreamten Messwerten.',
      false
    );
  }

  refresh().catch((err) => setFeedback(err.message || String(err), true));
  return () => {
    teardownChart();
  };
}
