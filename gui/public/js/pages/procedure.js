'use strict';

import { navigate } from '../router.js';
import {
  fetchCurrentProgram,
  fetchProgramSelection,
  fetchProcedureStatus,
  startProcedure,
  pauseProcedure,
  resumeProcedure,
  stopProcedure,
} from '../api.js';

function escapeHtml(str) {
  return String(str)
    .replaceAll('&', '&amp;')
    .replaceAll('<', '&lt;')
    .replaceAll('>', '&gt;')
    .replaceAll('"', '&quot;')
    .replaceAll("'", '&#039;');
}

export function renderProcedure(container) {
  container.innerHTML = `
    <section class="panel page-panel">
      <h2>Prüf-/Alterungsprozedur</h2>
      <p id="procedure-feedback" class="command-feedback" aria-live="polite"></p>
      <div id="procedure-content"></div>
    </section>
  `;

  const feedbackEl = container.querySelector('#procedure-feedback');
  const contentEl = container.querySelector('#procedure-content');
  let pollTimer = null;
  let latestStatus = null;

  function setFeedback(text, isError) {
    feedbackEl.textContent = text || '';
    feedbackEl.classList.toggle('command-feedback--error', Boolean(isError));
  }

  function clearPoll() {
    if (pollTimer) {
      clearInterval(pollTimer);
      pollTimer = null;
    }
  }

  function renderCapabilityNotes(status) {
    const caps = status.capabilities || {};
    const notes = status.notes || [];
    const unsupported = [];
    if (!caps.modeSupported) unsupported.push('Modus Prüfen/Altern');
    if (!caps.insertionForceSupported) unsupported.push('Eingliederungskraft');
    if (!caps.iterationsSupported) unsupported.push('Iterationen');

    return `
      <div class="capability-card">
        <h3>Backend: ${escapeHtml(status.backend || '—')}</h3>
        <p class="placeholder-note">
          Controller-Status:
          <b>${escapeHtml(status.connected === false ? 'offline' : status.connected === true ? 'online' : '—')}</b>
          · Prozess: <b>${escapeHtml(status.processState || '—')}</b>
          · Zustand: <b>${escapeHtml(status.state || '—')}</b>
        </p>
        ${
          unsupported.length
            ? `<p class="placeholder-note capability-warn">
                Folgende Parameter werden vom aktuellen Core/Protocol <b>nicht</b> ausgeführt
                (nur GUI-seitig gespeichert): ${unsupported.map(escapeHtml).join(', ')}.
              </p>`
            : `<p class="placeholder-note">Mock-Backend: Parameter werden vollständig simuliert.</p>`
        }
        ${
          notes.length
            ? `<ul class="capability-notes">${notes
                .map((n) => `<li>${escapeHtml(n)}</li>`)
                .join('')}</ul>`
            : ''
        }
      </div>
    `;
  }

  function renderProgress(status) {
    const p = status.progress || {};
    return `
      <div class="progress-card">
        <h3>Fortschritt</h3>
        <p class="placeholder-note">
          Iteration: <b>${escapeHtml(p.currentIteration ?? '—')}</b> /
          <b>${escapeHtml(p.totalIterations ?? '—')}</b><br/>
          Punkt: <b>${escapeHtml(p.currentPointIndex ?? '—')}</b> /
          <b>${escapeHtml(p.totalPoints ?? '—')}</b><br/>
          Fortschritt: <b>${escapeHtml(p.percent ?? 0)}%</b>
        </p>
        <div class="progress-bar" aria-hidden="true">
          <div class="progress-bar__fill" style="width:${Number(p.percent) || 0}%"></div>
        </div>
      </div>
    `;
  }

  async function refresh() {
    const program = await fetchCurrentProgram();
    const selection = await fetchProgramSelection();
    const procedure = await fetchProcedureStatus();
    latestStatus = procedure.status || {};

    if (!program.ok) {
      contentEl.innerHTML = `
        <div class="error-card">
          <h3>Kein Programm geladen</h3>
          <p class="placeholder-note">Bitte zuerst Prüftstellen laden und auswählen.</p>
          <div class="control-actions page-actions">
            <button type="button" id="btn-go-load">Zu „Prüftstellen laden“</button>
            <button type="button" class="btn-back" data-route="menu">Zurück</button>
          </div>
        </div>
      `;
      contentEl.querySelector('#btn-go-load').addEventListener('click', () => navigate('load-points'));
      contentEl.querySelector('.btn-back').addEventListener('click', () => navigate('menu'));
      return;
    }

    if (!selection.ok || !selection.selection || !selection.selection.confirmed) {
      contentEl.innerHTML = `
        <div class="error-card">
          <h3>Keine bestätigte Punktauswahl</h3>
          <p class="placeholder-note">Bitte unter „Prüfstellen auswählen“ eine Auswahl bestätigen.</p>
          <div class="control-actions page-actions">
            <button type="button" id="btn-go-select">Zu „Prüfstellen auswählen“</button>
            <button type="button" class="btn-back" data-route="menu">Zurück</button>
          </div>
        </div>
      `;
      contentEl.querySelector('#btn-go-select').addEventListener('click', () => navigate('select-points'));
      contentEl.querySelector('.btn-back').addEventListener('click', () => navigate('menu'));
      return;
    }

    const status = latestStatus;
    const active = status.state === 'running' || status.state === 'paused';
    const completed = status.state === 'completed';
    const caps = status.capabilities || {};

    contentEl.innerHTML = `
      <p class="placeholder-note">
        Programm: <b>${escapeHtml(program.current.meta.programName ?? '—')}</b> ·
        Bestätigte Punkte: <b>${selection.selection.points.length}</b>
      </p>

      ${renderCapabilityNotes(status)}

      <form id="procedure-form" class="control-form procedure-form">
        <label>
          Modus
          <select id="procedure-mode" ${active ? 'disabled' : ''}>
            <option value="test" ${(status.params && status.params.mode === 'test') || !status.params ? 'selected' : ''}>Prüfen</option>
            <option value="aging" ${status.params && status.params.mode === 'aging' ? 'selected' : ''}>Altern</option>
          </select>
          ${!caps.modeSupported ? '<span class="field-hint">Core: nicht unterstützt</span>' : ''}
        </label>
        <label>
          Eingliederungskraft [N]
          <input id="procedure-force" type="number" min="0" step="any"
            value="${escapeHtml(status.params ? status.params.insertionForce : 50)}"
            ${active ? 'disabled' : ''} />
          ${!caps.insertionForceSupported ? '<span class="field-hint">Core: nicht unterstützt</span>' : ''}
        </label>
        <label>
          Iterationen
          <input id="procedure-iterations" type="number" min="1" step="1"
            value="${escapeHtml(status.params ? status.params.iterations : 1)}"
            ${active ? 'disabled' : ''} />
          ${!caps.iterationsSupported ? '<span class="field-hint">Core: nur 1× Punktfolge</span>' : ''}
        </label>
      </form>

      ${renderProgress(status)}

      <div class="control-actions page-actions">
        <button type="button" id="btn-proc-start" ${active ? 'disabled' : ''}>Start</button>
        <button type="button" id="btn-proc-pause" ${status.state === 'running' ? '' : 'disabled'}>Pause</button>
        <button type="button" id="btn-proc-resume" ${status.state === 'paused' ? '' : 'disabled'}>Fortsetzen</button>
        <button type="button" id="btn-proc-stop" ${active || status.state === 'unavailable' ? '' : 'disabled'}>Stop</button>
        ${
          completed
            ? '<button type="button" id="btn-proc-results">Ergebnisse anzeigen</button>'
            : ''
        }
        <button type="button" class="btn-back" data-route="menu">Zurück</button>
      </div>
    `;

    contentEl.querySelector('.btn-back').addEventListener('click', () => navigate('menu'));

    const resultsBtn = contentEl.querySelector('#btn-proc-results');
    if (resultsBtn) {
      resultsBtn.addEventListener('click', () => navigate('results'));
    }

    contentEl.querySelector('#btn-proc-start').addEventListener('click', async () => {
      try {
        const mode = contentEl.querySelector('#procedure-mode').value;
        const insertionForce = Number(contentEl.querySelector('#procedure-force').value);
        const iterations = Number(contentEl.querySelector('#procedure-iterations').value);
        await startProcedure({ mode, insertionForce, iterations });
        setFeedback('Prozedur gestartet.', false);
        await refresh();
        ensurePolling();
      } catch (err) {
        setFeedback(err.message || String(err), true);
        await refresh();
      }
    });

    contentEl.querySelector('#btn-proc-pause').addEventListener('click', async () => {
      try {
        await pauseProcedure();
        setFeedback('Prozedur pausiert.', false);
        await refresh();
      } catch (err) {
        setFeedback(err.message || String(err), true);
      }
    });

    contentEl.querySelector('#btn-proc-resume').addEventListener('click', async () => {
      try {
        await resumeProcedure();
        setFeedback('Prozedur fortgesetzt.', false);
        await refresh();
        ensurePolling();
      } catch (err) {
        setFeedback(err.message || String(err), true);
      }
    });

    contentEl.querySelector('#btn-proc-stop').addEventListener('click', async () => {
      try {
        await stopProcedure();
        setFeedback('Prozedur gestoppt.', false);
        await refresh();
      } catch (err) {
        setFeedback(err.message || String(err), true);
        await refresh();
      }
    });

    if (completed) {
      setFeedback('Prozedur abgeschlossen — Ergebnisse verfügbar.', false);
    } else if (status.state === 'unavailable') {
      setFeedback(status.error || 'Controller offline.', true);
    }
  }

  function ensurePolling() {
    clearPoll();
    pollTimer = setInterval(() => {
      refresh().catch(() => {});
    }, 500);
  }

  refresh()
    .then(() => {
      const state = latestStatus && latestStatus.state;
      if (state === 'running' || state === 'paused') {
        ensurePolling();
      }
    })
    .catch((err) => setFeedback(err.message || String(err), true));

  return () => {
    clearPoll();
  };
}
