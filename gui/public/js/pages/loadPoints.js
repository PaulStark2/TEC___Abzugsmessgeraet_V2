'use strict';

import { navigate } from '../router.js';
import { uploadProgram, fetchCurrentProgram, deleteCurrentProgram, fetchProcedureStatus } from '../api.js';

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

function renderProgramSummary(summary) {
  const points = summary.testPoints || summary.pointsPreview || [];
  const count = summary.count ?? points.length;

  return `
    <div class="success-card">
      <h3>Erfolgreich geladen</h3>
      <p class="placeholder-note">
        Prüfprogramm: <b>${escapeHtml(summary.programName ?? '—')}</b><br/>
        Version: <b>${escapeHtml(summary.version)}</b><br/>
        Punkte: <b>${escapeHtml(count)}</b>
      </p>
      <p class="placeholder-note">
        Koordinaten sind <b>Bauplatten-Koordinaten</b> (nicht Maschinenkoordinaten).
        Zur Bestimmung der Maschinenpositionen muss die Bauplatte kalibriert sein.
      </p>
      <p class="placeholder-note">
        X-Bereich: ${escapeHtml(summary.xRange.min)} … ${escapeHtml(summary.xRange.max)}<br/>
        Y-Bereich: ${escapeHtml(summary.yRange.min)} … ${escapeHtml(summary.yRange.max)}<br/>
        Z-Bereich: ${escapeHtml(summary.zRange.min)} … ${escapeHtml(summary.zRange.max)}
      </p>

      <h4>Geladene Prüfstellen (${count}) — Bauplatten-Koordinaten [mm]</h4>
      ${
        points.length
          ? `
        <div class="program-table-scroll">
          <table class="program-table">
            <thead>
              <tr>
                <th>#</th><th>Name</th><th>Seq</th><th>X (Plate)</th><th>Y (Plate)</th><th>Z (Plate)</th>
              </tr>
            </thead>
            <tbody>
              ${points
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
        </div>
      `
          : ''
      }
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
          <button type="button" id="btn-delete-program" disabled>Prüfstellen löschen</button>
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
  const deleteBtn = container.querySelector('#btn-delete-program');

  function setStatus(text, isError) {
    statusEl.textContent = text || '';
    statusEl.classList.toggle('command-feedback--error', Boolean(isError));
  }

  async function refreshDeleteButton() {
    const [programResult, procedureResult] = await Promise.all([
      fetchCurrentProgram(),
      fetchProcedureStatus(),
    ]);
    const hasProgram = programResult.ok && programResult.current;
    const procState = procedureResult.status?.state;
    const procActive =
      procState === 'running' || procState === 'paused' || procState === 'unavailable';
    deleteBtn.disabled = !hasProgram || procActive;
  }

  async function onDeleteProgram() {
    setStatus('Prüfstellen werden gelöscht…', false);
    try {
      const result = await deleteCurrentProgram();
      if (!result.ok) {
        setStatus(result.error || 'Löschen fehlgeschlagen', true);
        await refreshDeleteButton();
        return;
      }
      summaryRoot.innerHTML = '';
      fileEl.value = '';
      setStatus('Geladenes Programm und Auswahl wurden gelöscht.', false);
      await refreshDeleteButton();
    } catch (err) {
      setStatus(err.message || String(err), true);
      await refreshDeleteButton();
    }
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

    setStatus('Programm erfolgreich geladen.', false);
    summaryRoot.innerHTML = renderProgramSummary(result.summary);
    await refreshDeleteButton();
  }

  uploadBtn.addEventListener('click', () => {
    onUpload().catch((err) => setStatus(err.message || String(err), true));
  });

  deleteBtn.addEventListener('click', () => {
    onDeleteProgram().catch((err) => setStatus(err.message || String(err), true));
  });

  fetchCurrentProgram()
    .then((programResult) => {
      if (programResult.ok && programResult.current) {
        const points = programResult.current.testPoints || [];
        summaryRoot.innerHTML = renderProgramSummary({
          programName: programResult.current.meta?.programName,
          version: programResult.current.meta?.version,
          count: points.length,
          testPoints: points,
          xRange: {
            min: Math.min(...points.map((p) => p.x)),
            max: Math.max(...points.map((p) => p.x)),
          },
          yRange: {
            min: Math.min(...points.map((p) => p.y)),
            max: Math.max(...points.map((p) => p.y)),
          },
          zRange: {
            min: Math.min(...points.map((p) => p.z)),
            max: Math.max(...points.map((p) => p.z)),
          },
        });
      }
      return refreshDeleteButton();
    })
    .catch(() => refreshDeleteButton());

  return () => {
    uploadBtn.onclick = null;
    deleteBtn.onclick = null;
  };
}
