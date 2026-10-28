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
  sendProcedureDecision,
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

  function fmtNum(v, digits = 2) {
    const n = Number(v);
    return Number.isFinite(n) ? n.toFixed(digits) : '—';
  }


  function anomalyTypeLabel(type) {
    switch (Number(type)) {
      case 0:
        return 'Koordinaten-Drift';
      case 1:
        return 'Lateralkraft';
      case 2:
        return 'Z-Weglimit';
      case 3:
        return 'TCP-Hänger (Offset)';
      default:
        return `Anomalie ${type}`;
    }
  }

  function renderProcedureAnomaly(machine) {
    const a = machine && machine.procedureAnomaly;
    if (!a) {
      return '';
    }
    if (a.skipApplied === true || machine.procedureAnomalySkipApplied === true) {
      return `
      <div class="progress-card progress-card--error" role="status" aria-label="Prozedur-Anomalie">
        <h3>Anomalie — Punkt übersprungen</h3>
        <p class="placeholder-note">
          ${escapeHtml(anomalyTypeLabel(a.type))}: gemessen
          <b>${escapeHtml(fmtNum(a.measured, 3))}</b>, Limit
          <b>${escapeHtml(fmtNum(a.limit, 3))}</b>
          (Punkt ${escapeHtml(String(a.pointIndex ?? '—'))}, Iter
          ${escapeHtml(String(a.iteration ?? '—'))}).
          anomalySkip ist aktiv — nächster Punkt wird angefahren.
        </p>
      </div>`;
    }
    if (a.confirmPending !== true && machine.procedureAnomalyConfirmPending !== true) {
      return '';
    }
    return `
      <div class="progress-card progress-card--error" role="dialog" aria-label="Prozedur-Anomalie">
        <h3>Prozedur-Anomalie</h3>
        <p class="placeholder-note">
          ${escapeHtml(anomalyTypeLabel(a.type))}<br/>
          Gemessen <b>${escapeHtml(fmtNum(a.measured, 3))}</b>
          · Limit <b>${escapeHtml(fmtNum(a.limit, 3))}</b><br/>
          Z=<b>${escapeHtml(fmtNum(a.z, 3))}</b>
          · Fx=<b>${escapeHtml(fmtNum(a.fx, 3))}</b>
          · Fy=<b>${escapeHtml(fmtNum(a.fy, 3))}</b> N<br/>
          Punkt ${escapeHtml(String(a.pointIndex ?? '—'))}
          · Iteration ${escapeHtml(String(a.iteration ?? '—'))}
        </p>
        <div class="control-actions">
          <button type="button" id="btn-anom-stop">Stopp</button>
          <button type="button" id="btn-anom-continue">Weiter</button>
          <button type="button" id="btn-anom-next">Nächster Punkt</button>
          <button type="button" id="btn-anom-repeat">Punkt wiederholen</button>
        </div>
      </div>
    `;
  }

  function renderBoreConfirm(machine) {
    if (!machine || machine.boreConfirmPending !== true) {
      return '';
    }
    return `
      <div class="progress-card progress-card--error" role="dialog" aria-label="Bohrungsmitte">
        <h3>Bohrungsmitte prüfen</h3>
        <p class="placeholder-note">
          Am Sehnenmittelpunkt liegt noch Kraft über der Schwelle.
          Weiter fährt mit Clamp fort. Stopp bricht die Prozedur ab.<br/>
          Mitte X=<b>${escapeHtml(fmtNum(machine.boreCx, 3))}</b>
          Y=<b>${escapeHtml(fmtNum(machine.boreCy, 3))}</b>
          · Radius <b>${escapeHtml(fmtNum(machine.boreRadius, 3))}</b> mm<br/>
          Fx=<b>${escapeHtml(fmtNum(machine.boreFx, 3))}</b> N
          · Fy=<b>${escapeHtml(fmtNum(machine.boreFy, 3))}</b> N
        </p>
        <div class="control-actions">
          <button type="button" id="btn-bore-continue">Weiter</button>
          <button type="button" id="btn-bore-stop">Stopp</button>
        </div>
      </div>
    `;
  }

  function renderToolOffsetAnomaly(status) {
    const a = status && status.toolOffsetAnomaly;
    if (!a || a.active !== true) {
      return '';
    }
    const prev = a.previous || {};
    const cur = a.current || {};
    return `
      <div class="progress-card progress-card--error" role="status" aria-label="Werkzeugoffset-Anomalie">
        <h3>Werkzeugoffset-Anomalie</h3>
        <p class="placeholder-note">
          Der toolOffset hat sich beim Einmessen um
          <b>${escapeHtml(fmtNum(a.deltaMm, 3))}</b> mm geändert
          (Toleranz <b>${escapeHtml(fmtNum(a.toleranceMm, 3))}</b> mm).
          Vermutlich wurde das Werkzeug gewechselt oder neu montiert.
          Der neue Offset wurde übernommen; die Prozedur läuft weiter.<br/>
          Vorher: X=${escapeHtml(fmtNum(prev.x, 3))}
          Y=${escapeHtml(fmtNum(prev.y, 3))}
          Z=${escapeHtml(fmtNum(prev.z, 3))} mm<br/>
          Neu: X=${escapeHtml(fmtNum(cur.x, 3))}
          Y=${escapeHtml(fmtNum(cur.y, 3))}
          Z=${escapeHtml(fmtNum(cur.z, 3))} mm
        </p>
      </div>
    `;
  }

  function renderProgress(status, machine) {
    const p = status.progress || {};
    const m = machine || {};
    const active = status.state === 'running' || status.state === 'paused';
    const complete = status.state === 'completed';
    const stopped = status.state === 'stopped';
    const rawPhase =
      (active && m.procedurePhase) ||
      p.procedurePhase ||
      status.procedurePhase ||
      m.procedurePhase ||
      '';
    const phaseLabel = formatProcedurePhaseLabel(rawPhase);
    const processLabel = active
      ? describeRunningCoreProcess(rawPhase).processLabel
      : complete
        ? 'Letzte Prozedur abgeschlossen'
        : stopped
          ? 'Prozedur abgebrochen / gestoppt'
          : 'Bereit — keine Prozedur aktiv';
    const fault =
      m.faultReason && m.faultReason !== 'None'
        ? m.faultReason
        : p.faultReason && p.faultReason !== 'None'
          ? p.faultReason
          : null;
    const percent = active ? Number(p.percent) || 0 : complete ? 100 : 0;

    return `
      <div class="progress-card${complete ? ' progress-card--complete' : ''}${
        stopped ? ' progress-card--error' : ''
      }">
        <h3>Fortschritt</h3>
        <p class="procedure-complete-banner" role="status">${escapeHtml(processLabel)}</p>
        <p class="placeholder-note">
          Core-Prozess: <b>${escapeHtml(m.processState || status.processState || '—')}</b>
          · Maschinenstatus: <b>${escapeHtml(m.machineState || p.machineState || '—')}</b><br/>
          Phase: <b>${escapeHtml(phaseLabel)}</b>
          ${fault ? `<br/>Fault: <b>${escapeHtml(fault)}</b>` : ''}<br/>
          Punkt: <b>${escapeHtml(p.pointsDone ?? p.currentPointIndex ?? '—')}</b> /
          <b>${escapeHtml(p.totalPoints ?? '—')}</b>
          · Iteration: <b>${escapeHtml(p.currentIteration ?? '—')}</b> /
          <b>${escapeHtml(p.totalIterations ?? '—')}</b><br/>
          Fortschritt: <b>${escapeHtml(percent)}%</b>
        </p>
        <div class="progress-bar" aria-hidden="true">
          <div class="progress-bar__fill" style="width:${percent}%"></div>
        </div>
        <h4 style="margin:12px 0 4px;">Live-Signale</h4>
        <p class="placeholder-note">
          Pose TCP: X=<b>${escapeHtml(fmtNum(m.x))}</b>
          Y=<b>${escapeHtml(fmtNum(m.y))}</b>
          Z=<b>${escapeHtml(fmtNum(m.z))}</b> mm<br/>
          Kraft: Fx=<b>${escapeHtml(fmtNum(m.fx))}</b>
          Fy=<b>${escapeHtml(fmtNum(m.fy))}</b>
          Fz=<b>${escapeHtml(fmtNum(m.fz))}</b> N<br/>
          Spreizer: <b>${escapeHtml(m.spreaderState || p.spreaderState || '—')}</b>
          · Homed: <b>${m.isHomed === true ? 'ja' : m.isHomed === false ? 'nein' : '—'}</b>
        </p>
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
    const stopped = status.state === 'stopped';
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

      ${renderProgress(status, machine)}
      ${renderToolOffsetAnomaly(status)}
      ${renderProcedureAnomaly(machine)}${renderBoreConfirm(machine)}

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

    const boreContinue = contentEl.querySelector('#btn-bore-continue');
    if (boreContinue) {
      boreContinue.addEventListener('click', async () => {
        try {
          await resumeProcedure();
          setFeedback('Bohrungsmitte bestätigt — Prozedur fortgesetzt.', false);
          await refresh();
          ensurePolling();
        } catch (err) {
          setFeedback(err.message || String(err), true);
        }
      });
    }
    const boreStop = contentEl.querySelector('#btn-bore-stop');
    if (boreStop) {
      boreStop.addEventListener('click', async () => {
        try {
          await stopProcedure();
          setFeedback('Prozedur gestoppt.', false);
          await refresh();
        } catch (err) {
          setFeedback(err.message || String(err), true);
        }
      });
    }

    const anomStop = contentEl.querySelector('#btn-anom-stop');
    if (anomStop) {
      anomStop.addEventListener('click', async () => {
        try {
          await stopProcedure();
          setFeedback('Prozedur gestoppt.', false);
          await refresh();
        } catch (err) {
          setFeedback(err.message || String(err), true);
        }
      });
    }
    async function decideAnomaly(action, label) {
      try {
        await sendProcedureDecision(action);
        setFeedback(label, false);
        await refresh();
        ensurePolling();
      } catch (err) {
        setFeedback(err.message || String(err), true);
      }
    }
    const anomContinue = contentEl.querySelector('#btn-anom-continue');
    if (anomContinue) {
      anomContinue.addEventListener('click', () => decideAnomaly(0, 'Anomalie: Weiter'));
    }
    const anomNext = contentEl.querySelector('#btn-anom-next');
    if (anomNext) {
      anomNext.addEventListener('click', () => decideAnomaly(1, 'Anomalie: Nächster Punkt'));
    }
    const anomRepeat = contentEl.querySelector('#btn-anom-repeat');
    if (anomRepeat) {
      anomRepeat.addEventListener('click', () => decideAnomaly(2, 'Anomalie: Punkt wiederholen'));
    }


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

    const fault =
      machine.faultReason && machine.faultReason !== 'None' ? machine.faultReason : null;
    if (fault === 'ProbeApproachContact') {
      setFeedback(
        'Messtaster-Kontakt während der Approach-Z-Fahrt (ProbeApproach). ' +
          'Gespeichertes probeCenter.z liegt vermutlich zu tief oder die Anfahrt war zu aggressiv. ' +
          'STOP/Homing, Kalibrierkennwerte prüfen, dann erneut.',
        true
      );
    } else if (stopped && fault === 'ProbeSearchNoContact') {
      setFeedback(
        'Kein Messtasterkontakt während der Suche (Timeout). Kalibrierung/Geometrie prüfen.',
        true
      );
    } else if (completed) {
      setFeedback('Letzte Prozedur abgeschlossen — Ergebnisse verfügbar.', false);
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
