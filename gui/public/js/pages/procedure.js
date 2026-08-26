'use strict';

import { navigate } from '../router.js';
import {
  fetchCurrentProgram,
  fetchProgramSelection,
  fetchProcedureStatus,
  fetchMachineStatus,
  startProcedure,
  pauseProcedure,
  resumeProcedure,
  stopProcedure,
  sendCommand,
} from '../api.js';
import { runWithHomingGate } from '../homingGate.js';
import {
  describeRunningCoreProcess,
  formatProcedurePhaseLabel,
  isCoreProcessRunning,
  isProcedureStartButtonDisabled,
} from '../coreProcessStatus.js';

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
  let latestMachine = null;

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
            : `<p class="placeholder-note">Parameter werden an den Core gesendet (TestConfiguration / ProcedureController).</p>`
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

  function renderCoreProcessBanner(machine) {
    if (!machine || !isCoreProcessRunning(machine.processState)) {
      return '';
    }
    const described = describeRunningCoreProcess(machine.procedurePhase);
    return `
      <div class="progress-card" role="status">
        <h3>Maschinenstatus</h3>
        <p class="placeholder-note">
          ● <b>${escapeHtml(described.processLabel)}</b><br/>
          Phase: <b>${escapeHtml(described.phaseLabel)}</b>
        </p>
        <p class="placeholder-note">
          Die Prüfprozedur kann erst gestartet werden, wenn dieser Prozess beendet ist.
        </p>
      </div>
    `;
  }

  function renderProgress(status) {
    const p = status.progress || {};
    const complete = p.isComplete || status.state === 'completed';
    const rawPhase = p.procedurePhase || status.procedurePhase || '';
    const phaseLabel = formatProcedurePhaseLabel(rawPhase);
    return `
      <div class="progress-card${complete ? ' progress-card--complete' : ''}">
        <h3>Fortschritt</h3>
        ${
          complete
            ? `<p class="procedure-complete-banner" role="status">Prozedur abgeschlossen</p>`
            : ''
        }
        <p class="placeholder-note">
          Iteration: <b>${escapeHtml(p.currentIteration ?? '—')}</b> /
          <b>${escapeHtml(p.totalIterations ?? '—')}</b><br/>
          Punkt: <b>${escapeHtml(p.pointsDone ?? p.currentPointIndex ?? '—')}</b> /
          <b>${escapeHtml(p.totalPoints ?? '—')}</b><br/>
          Phase: <b>${escapeHtml(phaseLabel)}</b><br/>
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
    try {
      latestMachine = await fetchMachineStatus();
    } catch {
      latestMachine = null;
    }

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
    const machine = latestMachine || {};
    const active = status.state === 'running' || status.state === 'paused';
    const completed = status.state === 'completed';
    const coreRunning = isCoreProcessRunning(machine.processState);
    const startDisabled = isProcedureStartButtonDisabled({
      procedureState: status.state,
      coreProcessState: machine.processState,
    });
    const caps = status.capabilities || {};

    contentEl.innerHTML = `
      <p class="placeholder-note">
        Programm: <b>${escapeHtml(program.current.meta.programName ?? '—')}</b> ·
        Bestätigte Punkte: <b>${selection.selection.points.length}</b>
      </p>

      ${renderCapabilityNotes(status)}
      ${renderCoreProcessBanner(machine)}

      <form id="procedure-form" class="control-form procedure-form">
        <label>
          Modus
          <select id="procedure-mode" ${active || coreRunning ? 'disabled' : ''}>
            <option value="test" ${(status.params && status.params.mode === 'test') || !status.params ? 'selected' : ''}>Prüfen</option>
            <option value="aging" ${status.params && status.params.mode === 'aging' ? 'selected' : ''}>Altern</option>
          </select>
          ${!caps.modeSupported ? '<span class="field-hint">Core: nicht unterstützt</span>' : ''}
        </label>
        <label>
          Eingliederungskraft [N]
          <input id="procedure-force" type="number" min="1" max="50" step="any"
            value="${escapeHtml(status.params ? status.params.insertionForce : 5)}"
            ${active || coreRunning ? 'disabled' : ''} />
          ${!caps.insertionForceSupported ? '<span class="field-hint">Core: nicht unterstützt</span>' : ''}
          <span class="field-hint">Fachlich 1–50 N. Sim-Steifigkeit (Plant k [N/mm]) getrennt einstellen — Default 5 N/mm erreicht 50 N bei 10 mm Suchweg.</span>
        </label>
        <label>
          Iterationen (nur Phase 3)
          <input id="procedure-iterations" type="number" min="1" step="1"
            value="${escapeHtml(status.params ? status.params.iterations : 1)}"
            ${active || coreRunning ? 'disabled' : ''} />
          ${!caps.iterationsSupported ? '<span class="field-hint">Core: nicht unterstützt</span>' : ''}
          <span class="field-hint">Anzahl der Prüf-/Alterungszyklen in Phase 3. Phase 2 optimiert dynamisch bis Krafttoleranz.</span>
        </label>
      </form>

      ${renderProgress(status)}

      <div class="control-actions page-actions">
        <button type="button" id="btn-proc-start" ${startDisabled ? 'disabled' : ''} title="${
          coreRunning && !active
            ? escapeHtml(describeRunningCoreProcess(machine.procedurePhase).startBlockedMessage)
            : ''
        }">Prüfprozedur starten</button>
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

        const machine = await fetchMachineStatus();
        const isHomed = machine.isHomed === true;
        const mcsValid = machine.mcsValid === true;

        const gate = await runWithHomingGate({
          status: { connected: machine.connected !== false, isHomed, mcsValid },
          startHoming: () => sendCommand({ command: 'home' }),
          getStatus: async () => {
            const st = await fetchMachineStatus();
            return {
              connected: st.connected !== false,
              isHomed: st.isHomed === true,
              mcsValid: st.mcsValid === true,
              homingState: st.homingState,
              machineState: st.machineState,
              eStop: st.eStop === true,
            };
          },
          onHomingStarted: () => {
            setFeedback('Homing gestartet — warte auf erfolgreichen Abschluss…', false);
          },
          action: () => startProcedure({ mode, insertionForce, iterations }),
        });

        if (gate.result === 'mcs_required') {
          setFeedback(
            'Kein gültiges Maschinenkoordinatensystem — bitte unter Kalibrieren „MCS und Bauplatte/Messtaster definieren und kalibrieren“ durchführen.',
            true
          );
          return;
        }
        if (gate.result === 'cancelled') {
          setFeedback('Start abgebrochen — Homing erforderlich.', false);
          return;
        }
        if (gate.result === 'failed') {
          setFeedback('Homing fehlgeschlagen — Prüfprozedur nicht gestartet.', true);
          await refresh();
          return;
        }
        if (gate.result === 'timeout') {
          setFeedback('Homing-Timeout — Prüfprozedur nicht gestartet.', true);
          await refresh();
          return;
        }

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
    } else if (coreRunning && !active) {
      setFeedback(describeRunningCoreProcess(machine.procedurePhase).startBlockedMessage, false);
    }

    if (active || coreRunning) {
      ensurePolling();
    } else {
      clearPoll();
    }
  }

  function ensurePolling() {
    if (pollTimer) return;
    pollTimer = setInterval(() => {
      refresh().catch(() => {});
    }, 500);
  }

  refresh()
    .then(() => {
      const state = latestStatus && latestStatus.state;
      const coreRunning = latestMachine && isCoreProcessRunning(latestMachine.processState);
      if (state === 'running' || state === 'paused' || coreRunning) {
        ensurePolling();
      }
    })
    .catch((err) => setFeedback(err.message || String(err), true));

  return () => {
    clearPoll();
  };
}
