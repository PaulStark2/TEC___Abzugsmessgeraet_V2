'use strict';

import { navigate } from '../router.js';
import { fetchCurrentResults } from '../api.js';

function escapeHtml(str) {
  return String(str)
    .replaceAll('&', '&amp;')
    .replaceAll('<', '&lt;')
    .replaceAll('>', '&gt;')
    .replaceAll('"', '&quot;')
    .replaceAll("'", '&#039;');
}

function formatCell(value) {
  if (value === null || value === undefined || value === '') return '—';
  if (typeof value === 'boolean') return value ? 'Pass' : 'Fail';
  if (typeof value === 'number') return Number.isFinite(value) ? value.toFixed(3) : '—';
  return escapeHtml(String(value));
}

export function renderResults(container) {
  container.innerHTML = `
    <section class="panel page-panel">
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
            Noch keine Prozedur abgeschlossen. Bitte unter „Prüf-/Alterungsprozedur“ starten.
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
    const rows = r.rows || [];
    const curves = r.curves || [];

    function renderCurve(curve, idx) {
      const samples = curve.samples || [];
      if (samples.length < 2) {
        return `<p class="placeholder-note">Kurve ${idx + 1}: Punkt ${curve.pointIndex}, Iteration ${curve.iteration} — zu wenige Samples.</p>`;
      }
      const zs = samples.map((s) => Number(s.z));
      const fs = samples.map((s) => Number(s.fz));
      const zMin = Math.min(...zs);
      const zMax = Math.max(...zs);
      const fMin = Math.min(...fs);
      const fMax = Math.max(...fs);
      const zSpan = zMax - zMin || 1;
      const fSpan = fMax - fMin || 1;
      const w = 320;
      const h = 120;
      const pts = samples
        .map((s) => {
          const x = ((Number(s.z) - zMin) / zSpan) * (w - 8) + 4;
          const y = h - 4 - ((Number(s.fz) - fMin) / fSpan) * (h - 8);
          return `${x.toFixed(1)},${y.toFixed(1)}`;
        })
        .join(' ');
      return `
        <figure class="force-curve">
          <figcaption>Punkt ${escapeHtml(curve.pointIndex)} · Iteration ${escapeHtml(curve.iteration)} · ${samples.length} Samples${curve.dropped ? ` · dropped=${curve.dropped}` : ''}</figcaption>
          <svg viewBox="0 0 ${w} ${h}" width="${w}" height="${h}" role="img" aria-label="Kraft-Weg-Kurve">
            <polyline fill="none" stroke="#5b9fd4" stroke-width="1.5" points="${pts}" />
          </svg>
        </figure>`;
    }

    contentEl.innerHTML = `
      <div class="success-card">
        <h3>Letzter Lauf</h3>
        <p class="placeholder-note">
          Programm: <b>${escapeHtml(r.programName ?? '—')}</b><br/>
          Backend: <b>${escapeHtml(r.backend ?? '—')}</b><br/>
          Modus (GUI): <b>${escapeHtml(r.mode ?? '—')}</b><br/>
          Abgeschlossen: <b>${escapeHtml(r.completedAt ?? '—')}</b><br/>
          Datenvollständigkeit: <b>${escapeHtml(r.dataCompleteness ?? '—')}</b>
        </p>
        ${
          (r.notes || []).length
            ? `<ul class="capability-notes">${r.notes
                .map((n) => `<li>${escapeHtml(n)}</li>`)
                .join('')}</ul>`
            : ''
        }
      </div>

      <table class="program-table results-table">
        <thead>
          <tr>
            <th>Seq</th>
            <th>Name</th>
            <th>Iter</th>
            <th>X</th>
            <th>Y</th>
            <th>Z</th>
            <th>Fx</th>
            <th>Fy</th>
            <th>Fz</th>
            <th>Pass</th>
            <th>Quelle</th>
          </tr>
        </thead>
        <tbody>
          ${
            rows.length
              ? rows
                  .map(
                    (row) => `
            <tr>
              <td>${formatCell(row.sequenceNumber)}</td>
              <td>${escapeHtml(row.name ?? '')}</td>
              <td>${formatCell(row.iteration)}</td>
              <td>${formatCell(row.x)}</td>
              <td>${formatCell(row.y)}</td>
              <td>${formatCell(row.z)}</td>
              <td>${formatCell(row.fx)}</td>
              <td>${formatCell(row.fy)}</td>
              <td>${formatCell(row.fz)}</td>
              <td>${formatCell(row.pass)}</td>
              <td>${formatCell(row.source)}</td>
            </tr>`
                  )
                  .join('')
              : '<tr><td colspan="11">Keine Zeilen vorhanden.</td></tr>'
          }
        </tbody>
      </table>

      ${
        curves.length
          ? `<div class="curve-list"><h3>Kraft-Weg-Kurven</h3>${curves.map(renderCurve).join('')}</div>`
          : '<p class="placeholder-note">Keine Kraft-Weg-Kurven empfangen (Alterung oder noch keine Chunks).</p>'
      }

      <div class="control-actions page-actions">
        <a class="btn-link" href="/api/results/export" download="results-stub.csv">CSV exportieren (Stub)</a>
        <button type="button" id="btn-go-procedure">Zur Prozedur</button>
        <button type="button" class="btn-back" data-route="menu">Zurück</button>
      </div>
    `;

    contentEl.querySelector('#btn-go-procedure').addEventListener('click', () => navigate('procedure'));
    contentEl.querySelector('.btn-back').addEventListener('click', () => navigate('menu'));
    setFeedback(
      r.dataCompleteness === 'partial'
        ? 'Reduzierte Controller-Daten — fehlende Werte als „—“.'
        : r.backend === 'protocol'
          ? 'Controller-Daten (Core/Protocol, keine STM32-Hardware).'
          : 'Mock-Ergebnisse (simuliert).',
      false
    );
  }

  refresh().catch((err) => setFeedback(err.message || String(err), true));
  return () => {};
}
