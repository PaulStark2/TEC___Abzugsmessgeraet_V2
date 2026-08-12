'use strict';

import { navigate } from '../router.js';
import {
  fetchCurrentProgram,
  fetchProgramSelection,
  updateProgramSelection,
  confirmProgramSelection,
  clearProgramSelection,
} from '../api.js';

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

function renderConfirmedSummary(selection) {
  const points = selection.points || [];
  const previewCount = Math.min(5, points.length);

  return `
    <div class="success-card selection-summary">
      <h3>Auswahl bestätigt</h3>
      <p class="placeholder-note">
        ${points.length} Prüfstelle(n) ausgewählt.
        ${selection.confirmedAt ? `<br/>Bestätigt: ${escapeHtml(selection.confirmedAt)}` : ''}
      </p>
      ${
        previewCount
          ? `
        <table class="program-table">
          <thead>
            <tr>
              <th>Seq</th><th>Name</th><th>X</th><th>Y</th><th>Z</th>
            </tr>
          </thead>
          <tbody>
            ${points
              .slice(0, previewCount)
              .map(
                (p) => `
              <tr>
                <td>${p.sequenceNumber}</td>
                <td>${escapeHtml(p.name)}</td>
                <td>${formatNumber(p.x)}</td>
                <td>${formatNumber(p.y)}</td>
                <td>${formatNumber(p.z)}</td>
              </tr>`
              )
              .join('')}
          </tbody>
        </table>
        ${
          points.length > previewCount
            ? `<p class="placeholder-note">… und ${points.length - previewCount} weitere Punkt(e).</p>`
            : ''
        }
      `
          : ''
      }
    </div>
  `;
}

export function renderSelectPoints(container) {
  container.innerHTML = `
    <section class="panel page-panel">
      <h2>Prüfstellen auswählen</h2>
      <p id="select-status" class="command-feedback" aria-live="polite"></p>
      <div id="select-content"></div>
    </section>
  `;

  const statusEl = container.querySelector('#select-status');
  const contentEl = container.querySelector('#select-content');

  function setStatus(text, isError) {
    statusEl.textContent = text || '';
    statusEl.classList.toggle('command-feedback--error', Boolean(isError));
  }

  async function refresh() {
    setStatus('Programm wird geladen…', false);

    const programResult = await fetchCurrentProgram();
    if (!programResult.ok) {
      contentEl.innerHTML = `
        <div class="error-card">
          <h3>Kein Programm geladen</h3>
          <p class="placeholder-note">
            Bitte zuerst ein Testprogramm laden, bevor Prüfstellen ausgewählt werden können.
          </p>
          <div class="control-actions page-actions">
            <button type="button" id="btn-go-load">Zu „Prüftstellen laden“</button>
            <button type="button" class="btn-back" data-route="menu">Zurück zum Hauptmenü</button>
          </div>
        </div>
      `;
      contentEl.querySelector('#btn-go-load').addEventListener('click', () => navigate('load-points'));
      contentEl.querySelector('.btn-back').addEventListener('click', () => navigate('menu'));
      setStatus('', false);
      return;
    }

    const selectionResult = await fetchProgramSelection();
    const selection = selectionResult.selection || {
      confirmed: false,
      draftSequenceNumbers: [],
      points: [],
    };

    if (selection.confirmed) {
      contentEl.innerHTML = `
        ${renderConfirmedSummary(selection)}
        <div class="control-actions page-actions">
          <button type="button" id="btn-edit-selection">Auswahl ändern</button>
          <button type="button" class="btn-back" data-route="menu">Zurück zum Hauptmenü</button>
        </div>
      `;
      contentEl.querySelector('#btn-edit-selection').addEventListener('click', async () => {
        try {
          await clearProgramSelection();
          await refresh();
        } catch (err) {
          setStatus(err.message || String(err), true);
        }
      });
      contentEl.querySelector('.btn-back').addEventListener('click', () => navigate('menu'));
      setStatus('Bestätigte Auswahl ist für die Prozedur verfügbar.', false);
      return;
    }

    const points = programResult.current.testPoints || [];
    const selectedSet = new Set(selection.draftSequenceNumbers || []);

    contentEl.innerHTML = `
      <p class="placeholder-note">
        Programm: <b>${escapeHtml(programResult.current.meta.programName ?? '—')}</b>
        (Version ${escapeHtml(programResult.current.meta.version)}, ${points.length} Punkt(e))
      </p>

      <div class="placeholder-note selection-3d-hint">
        <strong>3D-Auswahl (Platzhalter):</strong> Interaktive Punktauswahl in der 3D-Ansicht ist
        für einen späteren Meilenstein vorgesehen (nicht G6). Aktuell nur Listen-Auswahl.
      </div>

      <div class="control-actions selection-toolbar">
        <button type="button" id="btn-select-all">Alles auswählen</button>
        <button type="button" id="btn-clear-selection">Auswahl löschen</button>
      </div>

      <table class="program-table selection-table">
        <thead>
          <tr>
            <th>Auswahl</th>
            <th>Seq</th>
            <th>Name</th>
            <th>X</th>
            <th>Y</th>
            <th>Z</th>
          </tr>
        </thead>
        <tbody>
          ${points
            .map(
              (p) => `
            <tr>
              <td>
                <input
                  type="checkbox"
                  class="point-checkbox"
                  data-seq="${p.sequenceNumber}"
                  ${selectedSet.has(p.sequenceNumber) ? 'checked' : ''}
                />
              </td>
              <td>${p.sequenceNumber}</td>
              <td>${escapeHtml(p.name)}</td>
              <td>${formatNumber(p.x)}</td>
              <td>${formatNumber(p.y)}</td>
              <td>${formatNumber(p.z)}</td>
            </tr>`
            )
            .join('')}
        </tbody>
      </table>

      <div class="control-actions page-actions">
        <button type="button" id="btn-confirm-selection">Auswahl bestätigen</button>
        <button type="button" class="btn-back" data-route="menu">Zurück</button>
      </div>
    `;

    const checkboxes = [...contentEl.querySelectorAll('.point-checkbox')];

    async function syncDraftFromUi() {
      const selectedSequenceNumbers = checkboxes
        .filter((cb) => cb.checked)
        .map((cb) => Number(cb.dataset.seq));
      await updateProgramSelection({ selectedSequenceNumbers });
    }

    checkboxes.forEach((cb) => {
      cb.addEventListener('change', () => {
        syncDraftFromUi().catch((err) => setStatus(err.message || String(err), true));
      });
    });

    contentEl.querySelector('#btn-select-all').addEventListener('click', async () => {
      try {
        await updateProgramSelection({ selectAll: true });
        await refresh();
        setStatus('Alle Punkte ausgewählt.', false);
      } catch (err) {
        setStatus(err.message || String(err), true);
      }
    });

    contentEl.querySelector('#btn-clear-selection').addEventListener('click', async () => {
      try {
        await clearProgramSelection();
        await refresh();
        setStatus('Auswahl gelöscht.', false);
      } catch (err) {
        setStatus(err.message || String(err), true);
      }
    });

    contentEl.querySelector('#btn-confirm-selection').addEventListener('click', async () => {
      try {
        await syncDraftFromUi();
        await confirmProgramSelection();
        await refresh();
        setStatus('Auswahl bestätigt.', false);
      } catch (err) {
        setStatus(err.message || String(err), true);
      }
    });

    contentEl.querySelector('.btn-back').addEventListener('click', () => navigate('menu'));
    setStatus('', false);
  }

  refresh().catch((err) => setStatus(err.message || String(err), true));

  return () => {};
}
