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
  resetProcedureUi,
  sendProcedureDecision,
  sendCommand,
  subscribeLiveStatus,
} from '../api.js';
import { runWithHomingGate } from '../homingGate.js';
import { reportCommandError } from '../messageBus.js';
import {
  describeRunningCoreProcess,
  formatProcedurePhaseLabel,
  isCoreProcessRunning,
  isProcedureStartButtonDisabled,
} from '../coreProcessStatus.js';
import { createGgDiagram, createVelocityEstimator } from './ggDiagram.js';

function escapeHtml(str) {
  return String(str)
    .replaceAll('&', '&amp;')
    .replaceAll('<', '&lt;')
    .replaceAll('>', '&gt;')
    .replaceAll('"', '&quot;')
    .replaceAll("'", '&#039;');
}

function fmtNum(v, digits = 2) {
  const n = Number(v);
  return Number.isFinite(n) ? n.toFixed(digits) : '—';
}

function spreaderChip(state) {
  if (state === 'Extended' || state === 'Extending') return { text: 'Spreizer aktiv', on: true };
  if (state === 'Retracting') return { text: 'Spreizer fährt ein', on: false };
  if (state === 'SafeDeenergized') return { text: 'Spreizer sicher', on: false };
  return { text: 'Spreizer inaktiv', on: false };
}

export function renderProcedure(container) {
  container.innerHTML = `
    <section class="panel page-panel procedure-dash">
      <h2>Prüf-/Alterungsprozedur</h2>
      <p id="procedure-feedback" class="command-feedback" aria-live="polite"></p>
      <div id="procedure-content"></div>
    </section>
  `;

  const feedbackEl = container.querySelector('#procedure-feedback');
  const contentEl = container.querySelector('#procedure-content');

  let pollTimer = null;
  let rafId = 0;
  let latestStatus = null;
  let latestMachine = null;
  let programMeta = null;
  let selectionMeta = null;
  let mounted = false;
  let startBusy = false;
  let forceGg = null;
  let velGg = null;
  // Higher alpha keeps ~0.01 mm/s fine motion visible (less EMA lag).
  let velEst = createVelocityEstimator(0.55);
  let unsubLive = null;
  let lastLiveMachine = null;

  function setFeedback(text, isError) {
    feedbackEl.textContent = text || '';
    feedbackEl.classList.toggle('command-feedback--error', Boolean(isError));
    if (isError && text) reportCommandError(text);
  }

  function clearPoll() {
    if (pollTimer) {
      clearInterval(pollTimer);
      pollTimer = null;
    }
  }

  function ensurePolling() {
    if (pollTimer) return;
    pollTimer = setInterval(() => {
      refreshStore().catch(() => {});
    }, 500);
  }

  function conditionalHints(status) {
    const hints = [];
    const caps = status.capabilities || {};
    const params = status.params || {};
    const force = Number(params.insertionForce);
    const iters = Number(params.iterations);
    if (Number.isFinite(force) && (force < 1 || force > 50)) {
      hints.push('Prüfkraft außerhalb 1–50 N — fachlich ungünstig.');
    }
    if (Number.isFinite(iters) && iters < 1) {
      hints.push('Iterationen müssen ≥ 1 sein.');
    }
    if (caps.modeSupported === false) {
      hints.push('Aktuelles Backend unterstützt den Modus nicht vollständig.');
    }
    return hints;
  }

  function mountDashboard() {
    contentEl.innerHTML = `
      <p class="procedure-meta" id="proc-meta"></p>
      <div id="proc-warnings" class="procedure-warnings" hidden></div>
      <div id="proc-core-banner" class="progress-card" hidden></div>

      <form id="procedure-form" class="control-form procedure-form procedure-settings">
        <label>
          Modus
          <select id="procedure-mode">
            <option value="test">Prüfen</option>
            <option value="aging">Altern</option>
          </select>
        </label>
        <label>
          Prüfkraft [N]
          <input id="procedure-force" type="number" min="1" max="50" step="any" value="5" />
        </label>
        <label>
          Iterationen
          <input id="procedure-iterations" type="number" min="1" step="1" value="1" />
        </label>
      </form>

      <div class="procedure-actions control-actions page-actions">
        <button type="button" id="btn-proc-start">Starten</button>
        <button type="button" id="btn-proc-pause" disabled>Pausieren</button>
        <button type="button" id="btn-proc-resume" disabled>Fortsetzen</button>
        <button type="button" id="btn-proc-stop" disabled>Stoppen</button>
        <button type="button" id="btn-proc-reset">Zurücksetzen</button>
        <button type="button" id="btn-proc-results" hidden>Ergebnisse</button>
        <button type="button" class="btn-back" data-route="menu">Zurück</button>
      </div>

      <div class="status-chips" id="proc-chips" aria-live="polite"></div>

      <div class="progress-stack">
        <div class="progress-block">
          <div class="progress-block__head">
            <span>Prüfpunkte</span>
            <b id="proc-points-label">—</b>
          </div>
          <div class="progress-bar progress-bar--green" aria-hidden="true">
            <div class="progress-bar__fill" id="proc-bar-green" style="width:0%"></div>
          </div>
          <div class="progress-milestones" id="proc-point-milestones"></div>
        </div>
        <div class="progress-block">
          <div class="progress-block__head">
            <span>Phase</span>
            <b id="proc-phase-pct">—</b>
          </div>
          <div class="phase-strip" id="proc-phase-strip">
            <span class="phase-strip__prev" id="proc-phase-prev">—</span>
            <span class="phase-strip__current" id="proc-phase-current">—</span>
            <span class="phase-strip__next" id="proc-phase-next">—</span>
          </div>
          <div class="progress-bar progress-bar--blue" aria-hidden="true">
            <div class="progress-bar__fill" id="proc-bar-blue" style="width:0%"></div>
          </div>
          <div class="progress-milestones" id="proc-phase-milestones"></div>
        </div>
        <div class="progress-block" id="proc-iter-block" hidden>
          <div class="progress-block__head">
            <span>Iterationen</span>
            <b id="proc-iter-label">—</b>
          </div>
          <div class="progress-bar progress-bar--iter" aria-hidden="true">
            <div class="progress-bar__fill" id="proc-bar-iter" style="width:0%"></div>
          </div>
        </div>
      </div>

      <div class="gg-grid">
        <section class="gg-panel">
          <h3>Kräfte</h3>
          <div class="gg-layout">
            <canvas id="gg-force-xy" class="gg-canvas" width="320" height="320"></canvas>
            <canvas id="gg-force-z" class="gg-strip" width="72" height="320"></canvas>
          </div>
          <div class="gg-read" id="gg-force-read"></div>
        </section>
        <section class="gg-panel">
          <h3>Geschwindigkeit</h3>
          <div class="gg-layout">
            <canvas id="gg-vel-xy" class="gg-canvas" width="320" height="320"></canvas>
            <canvas id="gg-vel-z" class="gg-strip" width="72" height="320"></canvas>
          </div>
          <div class="gg-read" id="gg-vel-read"></div>
        </section>
      </div>

      <div id="proc-dialogs"></div>
    `;

    forceGg = createGgDiagram({
      xyCanvas: contentEl.querySelector('#gg-force-xy'),
      zCanvas: contentEl.querySelector('#gg-force-z'),
      readEl: contentEl.querySelector('#gg-force-read'),
      readLabels: ['Fx', 'Fy', '|Fxy|'],
      axisLabels: ['+Fy', '−Fy', '−Fx', '+Fx'],
      zLabel: 'Fz',
      unit: 'N',
      autoScale: true,
      // Floor ~0.05 N bore-search steps; ceiling for seat/test peaks.
      minScaleXy: 0.05,
      minScaleZ: 0.05,
      maxScaleXy: 50,
      maxScaleZ: 50,
      initialScaleXy: 0.05,
      initialScaleZ: 0.05,
    });
    velGg = createGgDiagram({
      xyCanvas: contentEl.querySelector('#gg-vel-xy'),
      zCanvas: contentEl.querySelector('#gg-vel-z'),
      readEl: contentEl.querySelector('#gg-vel-read'),
      readLabels: ['vx', 'vy', '|vxy|'],
      axisLabels: ['+vy', '−vy', '−vx', '+vx'],
      zLabel: 'vz',
      unit: 'mm/s',
      autoScale: true,
      // Floor for ~0.01 mm/s fine motion; ceiling for approach moves.
      minScaleXy: 0.01,
      minScaleZ: 0.01,
      maxScaleXy: 40,
      maxScaleZ: 40,
      initialScaleXy: 0.01,
      initialScaleZ: 0.01,
    });

    contentEl.querySelector('.btn-back').addEventListener('click', () => navigate('menu'));
    wireActions();
    mounted = true;
    startRaf();
  }

  function wireActions() {
    contentEl.querySelector('#btn-proc-start').addEventListener('click', onStart);
    contentEl.querySelector('#btn-proc-pause').addEventListener('click', async () => {
      try {
        await pauseProcedure();
        setFeedback('Prozedur pausiert.', false);
        await refreshStore();
      } catch (err) {
        setFeedback(err.message || String(err), true);
      }
    });
    contentEl.querySelector('#btn-proc-resume').addEventListener('click', async () => {
      try {
        await resumeProcedure();
        setFeedback('Prozedur fortgesetzt.', false);
        await refreshStore();
        ensurePolling();
      } catch (err) {
        setFeedback(err.message || String(err), true);
      }
    });
    contentEl.querySelector('#btn-proc-stop').addEventListener('click', async () => {
      try {
        await stopProcedure();
        setFeedback('Prozedur gestoppt.', false);
        await refreshStore();
      } catch (err) {
        setFeedback(err.message || String(err), true);
        await refreshStore();
      }
    });
    contentEl.querySelector('#btn-proc-reset').addEventListener('click', async () => {
      try {
        await resetProcedureUi();
        if (forceGg) forceGg.clear();
        if (velGg) velGg.clear();
        velEst.reset();
        setFeedback('Dashboard zurückgesetzt (Messergebnisse bleiben erhalten).', false);
        await refreshStore();
      } catch (err) {
        setFeedback(err.message || String(err), true);
      }
    });
    contentEl.querySelector('#btn-proc-results').addEventListener('click', () => navigate('results'));
  }

  async function onStart() {
    if (startBusy) return;
    startBusy = true;
    updateControls();
    setFeedback('Start wird vorbereitet…', false);
    try {
      const mode = contentEl.querySelector('#procedure-mode').value;
      const insertionForce = Number(contentEl.querySelector('#procedure-force').value);
      const iterations = Number(contentEl.querySelector('#procedure-iterations').value);

      const machine = await fetchMachineStatus();
      const gate = await runWithHomingGate({
        status: {
          connected: machine.connected !== false,
          isHomed: machine.isHomed === true,
          mcsValid: machine.mcsValid === true,
        },
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
          'Kein gültiges Maschinenkoordinatensystem — bitte unter Kalibrieren MCS/Bauplatte definieren.',
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
        return;
      }
      if (gate.result === 'timeout') {
        setFeedback('Homing-Timeout — Prüfprozedur nicht gestartet.', true);
        return;
      }

      if (forceGg) forceGg.clear();
      if (velGg) velGg.clear();
      velEst.reset();
      // Dynamic autoscale from mins; optional Z ceiling hint from Prüfkraft.
      const fTarget = Number.isFinite(insertionForce) ? insertionForce : 5;
      if (forceGg) {
        forceGg.setScale({
          minXy: 0.05,
          minZ: 0.05,
          maxXy: 50,
          maxZ: Math.min(50, Math.max(10, fTarget * 2)),
          xy: 0.05,
          z: 0.05,
        });
      }
      if (velGg) {
        velGg.setScale({
          minXy: 0.01,
          minZ: 0.01,
          maxXy: 40,
          maxZ: 40,
          xy: 0.01,
          z: 0.01,
        });
      }
      setFeedback('Prozedur gestartet.', false);
      await refreshStore();
      ensurePolling();
    } catch (err) {
      setFeedback(err.message || String(err), true);
      await refreshStore();
    } finally {
      startBusy = false;
      updateControls();
    }
  }

  function renderMilestones(el, labels, activeIndex) {
    if (!el) return;
    if (!labels || !labels.length) {
      el.innerHTML = '';
      return;
    }
    el.innerHTML = labels
      .map((label, i) => {
        const cls =
          i === activeIndex
            ? 'milestone milestone--active'
            : i < activeIndex
              ? 'milestone milestone--done'
              : 'milestone';
        return `<span class="${cls}" title="${escapeHtml(label)}">${i + 1}</span>`;
      })
      .join('');
  }

  function updateControls() {
    const status = latestStatus || {};
    const machine = latestMachine || {};
    const active = status.state === 'running' || status.state === 'paused';
    const completed = status.state === 'completed';
    const coreRunning = isCoreProcessRunning(machine.processState);
    const startDisabled =
      startBusy ||
      isProcedureStartButtonDisabled({
        procedureState: status.state,
        coreProcessState: machine.processState,
      });

    const modeEl = contentEl.querySelector('#procedure-mode');
    const forceEl = contentEl.querySelector('#procedure-force');
    const iterEl = contentEl.querySelector('#procedure-iterations');
    if (modeEl && forceEl && iterEl && status.params && !active && !startBusy) {
      // keep user edits while idle unless params just loaded
    }
    if (modeEl) modeEl.disabled = active || coreRunning || startBusy;
    if (forceEl) forceEl.disabled = active || coreRunning || startBusy;
    if (iterEl) iterEl.disabled = active || coreRunning || startBusy;

    const startBtn = contentEl.querySelector('#btn-proc-start');
    const pauseBtn = contentEl.querySelector('#btn-proc-pause');
    const resumeBtn = contentEl.querySelector('#btn-proc-resume');
    const stopBtn = contentEl.querySelector('#btn-proc-stop');
    const resetBtn = contentEl.querySelector('#btn-proc-reset');
    const resultsBtn = contentEl.querySelector('#btn-proc-results');

    if (startBtn) {
      startBtn.disabled = startDisabled;
      startBtn.title =
        coreRunning && !active
          ? describeRunningCoreProcess(machine.procedurePhase).startBlockedMessage
          : startBusy
            ? 'Start wird vorbereitet…'
            : '';
      startBtn.textContent = startBusy ? 'Starten…' : 'Starten';
    }
    if (pauseBtn) pauseBtn.disabled = status.state !== 'running';
    if (resumeBtn) resumeBtn.disabled = status.state !== 'paused';
    if (stopBtn) stopBtn.disabled = !(active || status.state === 'unavailable');
    if (resetBtn) resetBtn.disabled = active || startBusy;
    if (resultsBtn) {
      resultsBtn.hidden = !(completed || status.state === 'stopped');
    }
  }

  function updateProgressUi() {
    const status = latestStatus || {};
    const p = status.progress || {};
    const active = status.state === 'running' || status.state === 'paused';
    const complete = status.state === 'completed';
    const green = active || complete ? Number(p.percent) || 0 : complete ? 100 : Number(p.percent) || 0;
    const blue = Number(p.phasePercent) || 0;
    const totalIter = Math.max(1, Number(p.totalIterations) || 1);
    const curIter = Number(p.currentIteration) || 0;
    const showIter = totalIter > 1 || (latestMachine && latestMachine.procedurePhase === 'TestOrAge');

    const greenEl = contentEl.querySelector('#proc-bar-green');
    const blueEl = contentEl.querySelector('#proc-bar-blue');
    const iterEl = contentEl.querySelector('#proc-bar-iter');
    if (greenEl) greenEl.style.width = `${Math.max(0, Math.min(100, green))}%`;
    if (blueEl) blueEl.style.width = `${Math.max(0, Math.min(100, blue))}%`;
    if (iterEl) {
      const iterPct =
        totalIter <= 1 ? (showIter ? Math.min(99, (curIter / totalIter) * 100) : 0) : Math.min(100, (curIter / totalIter) * 100);
      iterEl.style.width = `${iterPct}%`;
    }

    const pointsLabel = contentEl.querySelector('#proc-points-label');
    if (pointsLabel) {
      pointsLabel.textContent = `${p.pointsDone ?? '—'} / ${p.totalPoints ?? '—'}${
        complete ? ' · fertig' : ''
      }`;
    }
    const phasePct = contentEl.querySelector('#proc-phase-pct');
    if (phasePct) phasePct.textContent = `${Math.round(blue)}%`;

    contentEl.querySelector('#proc-phase-prev').textContent = p.phasePrev || '—';
    contentEl.querySelector('#proc-phase-current').textContent = p.phaseCurrent || '—';
    contentEl.querySelector('#proc-phase-next').textContent = p.phaseNext || '—';

    const iterBlock = contentEl.querySelector('#proc-iter-block');
    if (iterBlock) iterBlock.hidden = !showIter;
    const iterLabel = contentEl.querySelector('#proc-iter-label');
    if (iterLabel) iterLabel.textContent = `${curIter} / ${totalIter}`;

    const totalPoints = Number(p.totalPoints) || 0;
    const pointLabels = [];
    pointLabels.push('Vorbereitung');
    const names = (selectionMeta && selectionMeta.points) || [];
    for (let i = 1; i <= totalPoints; i += 1) {
      const point = names[i - 1];
      pointLabels.push(point && point.name ? point.name : `Punkt ${i}`);
    }
    pointLabels.push('Abschluss');
    let pointActive = 0;
    if (p.scope === 'setup') pointActive = 0;
    else if (p.scope === 'end' || complete) pointActive = pointLabels.length - 1;
    else pointActive = Math.min(totalPoints, (Number(p.pointsDone) || 0) + 1);
    renderMilestones(contentEl.querySelector('#proc-point-milestones'), pointLabels, pointActive);
    renderMilestones(
      contentEl.querySelector('#proc-phase-milestones'),
      p.milestoneLabels || [],
      Number.isFinite(Number(p.milestoneIndex)) ? Number(p.milestoneIndex) : -1
    );
  }

  function updateChips() {
    const m = lastLiveMachine || latestMachine || {};
    const status = latestStatus || {};
    const p = status.progress || {};
    const spr = spreaderChip(m.spreaderState || p.spreaderState);
    const moving = m.motionState === 'Moving';
    const chips = [
      { label: status.state || 'idle', on: status.state === 'running' || status.state === 'paused' },
      {
        label: formatProcedurePhaseLabel(m.procedurePhase || p.procedurePhase) || '—',
        on: true,
      },
      { label: spr.text, on: spr.on },
      { label: moving ? 'Achsen aktiv' : 'Achsen still', on: moving },
      { label: m.isHomed ? 'Gehomed' : 'Nicht gehomed', on: m.isHomed === true },
      {
        label:
          m.faultReason && m.faultReason !== 'None'
            ? `Fault: ${m.faultReason}`
            : m.eStop
              ? 'E-Stop'
              : 'OK',
        on: !(m.faultReason && m.faultReason !== 'None') && !m.eStop,
        bad: (m.faultReason && m.faultReason !== 'None') || m.eStop,
      },
      {
        label: m.forceSampleFresh === false ? 'Kraft alt' : 'Kraft frisch',
        on: m.forceSampleFresh !== false,
      },
    ];
    const el = contentEl.querySelector('#proc-chips');
    if (!el) return;
    el.innerHTML = chips
      .map((c) => {
        const cls = c.bad ? 'status-chip status-chip--bad' : c.on ? 'status-chip status-chip--on' : 'status-chip';
        return `<span class="${cls}">${escapeHtml(c.label)}</span>`;
      })
      .join('');
  }

  function anomalyTypeLabel(type) {
    const map = {
      CoordDrift: 'Koordinatenabweichung',
      LateralForce: 'Laterale Kraft',
      ZTravelLimit: 'Z-Weglimit',
      TcpHang: 'TCP hängt',
    };
    return map[type] || type || 'Anomalie';
  }

  function updateDialogs() {
    const status = latestStatus || {};
    const machine = latestMachine || {};
    const host = contentEl.querySelector('#proc-dialogs');
    if (!host) return;

    let html = '';
    const anom = status.toolOffsetAnomaly;
    if (anom && anom.active) {
      html += `
        <div class="progress-card progress-card--error" role="alertdialog">
          <h3>Werkzeugversatz-Anomalie</h3>
          <p class="placeholder-note">
            Δ=${escapeHtml(fmtNum(anom.deltaMm, 3))} mm
            (Toleranz ${escapeHtml(fmtNum(anom.toleranceMm, 3))} mm)
          </p>
        </div>`;
    }
    const pa = machine.procedureAnomaly;
    if (pa) {
      if (pa.skipApplied === true || machine.procedureAnomalySkipApplied === true) {
        html += `
          <div class="progress-card progress-card--error" role="status">
            <h3>Anomalie — Punkt übersprungen</h3>
            <p class="placeholder-note">
              ${escapeHtml(anomalyTypeLabel(pa.type))}: gemessen
              <b>${escapeHtml(fmtNum(pa.measured, 3))}</b>, Limit
              <b>${escapeHtml(fmtNum(pa.limit, 3))}</b>
            </p>
          </div>`;
      } else if (pa.confirmPending === true || machine.procedureAnomalyConfirmPending === true) {
        html += `
          <div class="progress-card progress-card--error" role="dialog">
            <h3>Prozedur-Anomalie</h3>
            <p class="placeholder-note">
              ${escapeHtml(anomalyTypeLabel(pa.type))}<br/>
              Gemessen <b>${escapeHtml(fmtNum(pa.measured, 3))}</b>
              · Limit <b>${escapeHtml(fmtNum(pa.limit, 3))}</b>
            </p>
            <div class="control-actions">
              <button type="button" id="btn-anom-continue">Weiter</button>
              <button type="button" id="btn-anom-next">Nächster Punkt</button>
              <button type="button" id="btn-anom-repeat">Punkt wiederholen</button>
              <button type="button" id="btn-anom-stop">Stop</button>
            </div>
          </div>`;
      }
    }
    if (machine.boreConfirmPending === true) {
      html += `
        <div class="progress-card progress-card--error" role="dialog">
          <h3>Bohrungsmitte prüfen</h3>
          <p class="placeholder-note">
            Am Sehnenmittelpunkt liegt noch Kraft über der Schwelle.
            Weiter fährt mit Clamp fort. Stopp bricht die Prozedur ab.
          </p>
          <div class="control-actions">
            <button type="button" id="btn-bore-continue">Fortsetzen</button>
            <button type="button" id="btn-bore-stop">Stop</button>
          </div>
        </div>`;
    }
    host.innerHTML = html;

    const decide = async (action, label) => {
      try {
        await sendProcedureDecision(action);
        setFeedback(label, false);
        await refreshStore();
        ensurePolling();
      } catch (err) {
        setFeedback(err.message || String(err), true);
      }
    };
    const anomContinue = host.querySelector('#btn-anom-continue');
    if (anomContinue) anomContinue.addEventListener('click', () => decide(0, 'Anomalie: Weiter'));
    const anomNext = host.querySelector('#btn-anom-next');
    if (anomNext) anomNext.addEventListener('click', () => decide(1, 'Anomalie: Nächster Punkt'));
    const anomRepeat = host.querySelector('#btn-anom-repeat');
    if (anomRepeat) anomRepeat.addEventListener('click', () => decide(2, 'Anomalie: Punkt wiederholen'));
    const anomStop = host.querySelector('#btn-anom-stop');
    if (anomStop) {
      anomStop.addEventListener('click', async () => {
        try {
          await stopProcedure();
          setFeedback('Prozedur gestoppt.', false);
          await refreshStore();
        } catch (err) {
          setFeedback(err.message || String(err), true);
        }
      });
    }
    const boreContinue = host.querySelector('#btn-bore-continue');
    if (boreContinue) {
      boreContinue.addEventListener('click', async () => {
        try {
          await resumeProcedure();
          setFeedback('Bohrungsmitte bestätigt — Prozedur fortgesetzt.', false);
          await refreshStore();
          ensurePolling();
        } catch (err) {
          setFeedback(err.message || String(err), true);
        }
      });
    }
    const boreStop = host.querySelector('#btn-bore-stop');
    if (boreStop) {
      boreStop.addEventListener('click', async () => {
        try {
          await stopProcedure();
          setFeedback('Prozedur gestoppt.', false);
          await refreshStore();
        } catch (err) {
          setFeedback(err.message || String(err), true);
        }
      });
    }
  }

  function applyLiveTelemetry(machine) {
    if (!machine || !forceGg || !velGg) return;
    lastLiveMachine = machine;
    forceGg.pushSample(machine.fx, machine.fy, machine.fz);
    const v = velEst.update(
      { x: machine.x, y: machine.y, z: machine.z },
      machine.motionState
    );
    velGg.pushSample(v.vx, v.vy, v.vz);
    updateChips();
  }

  function startRaf() {
    const tick = () => {
      if (forceGg) forceGg.draw();
      if (velGg) velGg.draw();
      rafId = requestAnimationFrame(tick);
    };
    rafId = requestAnimationFrame(tick);
  }

  function syncFormFromStatus(force) {
    const status = latestStatus || {};
    const params = status.params || {};
    const modeEl = contentEl.querySelector('#procedure-mode');
    const forceEl = contentEl.querySelector('#procedure-force');
    const iterEl = contentEl.querySelector('#procedure-iterations');
    const active = status.state === 'running' || status.state === 'paused';
    if (!force && active) return;
    if (modeEl && document.activeElement !== modeEl) {
      modeEl.value = params.mode === 'aging' ? 'aging' : 'test';
    }
    if (forceEl && document.activeElement !== forceEl && params.insertionForce != null) {
      forceEl.value = String(params.insertionForce);
    }
    if (iterEl && document.activeElement !== iterEl && params.iterations != null) {
      iterEl.value = String(params.iterations);
    }
  }

  function paint() {
    if (!mounted) return;
    const status = latestStatus || {};
    const machine = latestMachine || {};

    const meta = contentEl.querySelector('#proc-meta');
    if (meta) {
      meta.innerHTML = `Baujob: <b>${escapeHtml(
        (programMeta && (programMeta.buildJobNumber || programMeta.programName)) || '—'
      )}</b> · Punkte: <b>${
        selectionMeta && selectionMeta.points ? selectionMeta.points.length : '—'
      }</b>`;
    }

    const hints = conditionalHints(status);
    const warnEl = contentEl.querySelector('#proc-warnings');
    if (warnEl) {
      if (hints.length) {
        warnEl.hidden = false;
        warnEl.innerHTML = hints.map((h) => `<p class="field-hint">${escapeHtml(h)}</p>`).join('');
      } else {
        warnEl.hidden = true;
        warnEl.innerHTML = '';
      }
    }

    const banner = contentEl.querySelector('#proc-core-banner');
    if (banner) {
      if (isCoreProcessRunning(machine.processState) && status.state !== 'running' && status.state !== 'paused') {
        const d = describeRunningCoreProcess(machine.procedurePhase);
        banner.hidden = false;
        banner.innerHTML = `<h3>Maschinenstatus</h3><p class="placeholder-note">● <b>${escapeHtml(
          d.processLabel
        )}</b> · Phase: <b>${escapeHtml(d.phaseLabel)}</b></p>`;
      } else {
        banner.hidden = true;
        banner.innerHTML = '';
      }
    }

    syncFormFromStatus(false);
    updateControls();
    updateProgressUi();
    updateChips();
    updateDialogs();

    const active = status.state === 'running' || status.state === 'paused';
    const coreRunning = isCoreProcessRunning(machine.processState);
    if (active || coreRunning) ensurePolling();
    else clearPoll();

    if (status.state === 'completed') {
      setFeedback('Letzte Prozedur abgeschlossen — Ergebnisse verfügbar.', false);
    } else if (status.state === 'unavailable') {
      setFeedback(status.error || 'Controller offline.', true);
    }
  }

  async function refreshStore() {
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
      mounted = false;
      contentEl.innerHTML = `
        <div class="error-card">
          <h3>Kein Programm geladen</h3>
          <p class="placeholder-note">Bitte zuerst Prüfstellen laden und auswählen.</p>
          <div class="control-actions page-actions">
            <button type="button" id="btn-go-load">Zu „Prüfstellen laden“</button>
            <button type="button" class="btn-back" data-route="menu">Zurück</button>
          </div>
        </div>`;
      contentEl.querySelector('#btn-go-load').addEventListener('click', () => navigate('load-points'));
      contentEl.querySelector('.btn-back').addEventListener('click', () => navigate('menu'));
      return;
    }
    if (!selection.ok || !selection.selection || !selection.selection.confirmed) {
      mounted = false;
      contentEl.innerHTML = `
        <div class="error-card">
          <h3>Keine bestätigte Punktauswahl</h3>
          <p class="placeholder-note">Bitte unter „Prüfstellen auswählen“ eine Auswahl bestätigen.</p>
          <div class="control-actions page-actions">
            <button type="button" id="btn-go-select">Zu „Prüfstellen auswählen“</button>
            <button type="button" class="btn-back" data-route="menu">Zurück</button>
          </div>
        </div>`;
      contentEl.querySelector('#btn-go-select').addEventListener('click', () => navigate('select-points'));
      contentEl.querySelector('.btn-back').addEventListener('click', () => navigate('menu'));
      return;
    }

    programMeta = program.current && program.current.meta;
    selectionMeta = selection.selection;
    if (!mounted) {
      mountDashboard();
      syncFormFromStatus(true);
    }
    paint();
  }

  unsubLive = subscribeLiveStatus((msg) => {
    // WS payload is { type:'status', ...fields } or nested — accept both.
    const machine = msg && (msg.x !== undefined || msg.fx !== undefined) ? msg : msg && msg.status;
    if (!machine) return;
    applyLiveTelemetry(machine);
    if (mounted) {
      latestMachine = { ...(latestMachine || {}), ...machine };
    }
  });

  refreshStore().catch((err) => setFeedback(err.message || String(err), true));

  return () => {
    clearPoll();
    if (rafId) cancelAnimationFrame(rafId);
    if (unsubLive) unsubLive();
  };
}
