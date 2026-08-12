'use strict';

import { navigate } from '../router.js';
import { uploadProgram } from '../api.js';

function escapeHtml(str) {
  return String(str)
    .replaceAll('&', '&amp;')
    .replaceAll('<', '&lt;')
    .replaceAll('>', '&gt;')
    .replaceAll('"', '&quot;')
    .replaceAll("'", '&#039;');
}

function formatNumber(value) {
  const n = Number(value);
  if (!Number.isFinite(n)) return '—';
  return n.toFixed(2);
}

function renderErrorList(errors) {
  const items = (errors || [])
    .map(
      (e) =>
        `<li><span class="error-path">${escapeHtml(e.path || '')}</span> ${escapeHtml(e.message || '')}</li>`
    )
    .join('');

  return `
    <div class="error-card">
      <h3>Validierungsfehler</h3>
      <ul class="error-list">${items}</ul>
    </div>
  `;
}

export function renderLoadPoints(container) {
  container.innerHTML = `
    <section class="panel page-panel">
      <h2>Prüftstellen laden</h2>
      <p class="placeholder-note">
        JSON-Testprogramm hochladen. Der Server validiert und speichert das Programm nur im RAM.
      </p>

      <div class="upload-card">
        <label class="file-label">
          Testprogramm-Datei (.json)
          <input id="program-file" type="file" accept="application/json,.json" />
        </label>
        <div class="control-actions">
          <button type="button" id="btn-upload-program">Upload / Laden</button>
        </div>
        <p id="upload-status" class="command-feedback" aria-live="polite"></p>
      </div>

      <div id="program-summary-root" class="summary-root"></div>

      <div class="control-actions page-actions">
        <button type="button" class="btn-back" data-route="menu">Zurück zum Hauptmenü</button>
      </div>
    </section>
  `;

  const backBtn = container.querySelector('.btn-back');
  backBtn.addEventListener('click', () => navigate('menu'));

  const fileEl = container.querySelector('#program-file');
  const statusEl = container.querySelector('#upload-status');
  const summaryRoot = container.querySelector('#program-summary-root');
  const uploadBtn = container.querySelector('#btn-upload-program');

  function setStatus(text, isError) {
    statusEl.textContent = text || '';
    statusEl.classList.toggle('command-feedback--error', Boolean(isError));
  }

  async function onUpload() {
    summaryRoot.innerHTML = '';

    const file = fileEl.files && fileEl.files[0];
    if (!file) {
      setStatus('Bitte eine .json Datei auswählen.', true);
      return;
    }

    setStatus('Datei wird geladen…', false);
    const text = await file.text();

    let parsed;
    try {
      parsed = JSON.parse(text);
    } catch {
      setStatus('Ungültiges JSON: Parsing fehlgeschlagen.', true);
      return;
    }

    setStatus('Upload wird validiert…', false);
    const result = await uploadProgram(parsed);

    if (!result.ok) {
      setStatus('Upload abgelehnt (siehe Fehlerliste).', true);
      summaryRoot.innerHTML = renderErrorList(result.errors);
      return;
    }

    const s = result.summary;
    setStatus('Programm erfolgreich geladen.', false);
    const preview = s.pointsPreview || [];

    summaryRoot.innerHTML = `
      <div class="success-card">
        <h3>Erfolgreich geladen</h3>
        <p class="placeholder-note">
          Prüfprogramm: <b>${escapeHtml(s.programName ?? '—')}</b><br/>
          Version: <b>${escapeHtml(s.version)}</b><br/>
          Punkte: <b>${escapeHtml(s.count)}</b>
        </p>
        <p class="placeholder-note">
          X-Bereich: ${escapeHtml(s.xRange.min)} … ${escapeHtml(s.xRange.max)}<br/>
          Y-Bereich: ${escapeHtml(s.yRange.min)} … ${escapeHtml(s.yRange.max)}<br/>
          Z-Bereich: ${escapeHtml(s.zRange.min)} … ${escapeHtml(s.zRange.max)}
        </p>

        <h4>Vorschau (erste ${preview.length} Punkte)</h4>
        ${
          preview.length
            ? `
          <table class="program-table">
            <thead>
              <tr>
                <th>#</th><th>Name</th><th>Seq</th><th>X</th><th>Y</th><th>Z</th>
              </tr>
            </thead>
            <tbody>
              ${preview
                .map(
                  (p, idx) => `
                <tr>
                  <td>${idx + 1}</td>
                  <td>${escapeHtml(p.name)}</td>
                  <td>${p.sequenceNumber}</td>
                  <td>${formatNumber(p.x)}</td>
                  <td>${formatNumber(p.y)}</td>
                  <td>${formatNumber(p.z)}</td>
                </tr>`
                )
                .join('')}
            </tbody>
          </table>
        `
            : ''
        }
      </div>
    `;
  }

  uploadBtn.addEventListener('click', () => {
    onUpload().catch((err) => setStatus(err.message || String(err), true));
  });

  return () => {
    uploadBtn.onclick = null;
  };
}

