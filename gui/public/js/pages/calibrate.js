'use strict';

import { navigate } from '../router.js';
import {
  sendCommand,
  fetchCalibration,
  fetchMachineConfig,
  setPlateOrigin,
  startProbeCalibration,
  syncCalibrationFromController,
  fixtureOpen,
  fixtureClose,
} from '../api.js';
import { mountJogPanel } from '../components/jogPanel.js';

function escapeHtml(str) {
  return String(str)
    .replaceAll('&', '&amp;')
    .replaceAll('<', '&lt;')
    .replaceAll('>', '&gt;');
}

function formatCoord(v) {
  return Number(v).toFixed(3);
}

const STEPS = {
  MENU: 'menu',
  HOMING: 'homing',
  PLATE_QUESTION: 'plate_question',
  PLATE_INSERT: 'plate_insert',
  PLATE_ORIGIN: 'plate_origin',
  PROBE_APPROACH: 'probe_approach',
  PROBE_CALIBRATE: 'probe_calibrate',
  COMPLETE: 'complete',
};

export function renderCalibrate(container) {
  container.innerHTML = `
    <section class="panel page-panel">
      <h2>Kalibrieren</h2>
      <div id="calibrate-content"></div>
    </section>
  `;

  const contentEl = container.querySelector('#calibrate-content');
  let pollTimer = null;
  let currentStep = STEPS.MENU;
  let liveStatus = null;
  let savedPlateOrigin = null;
  let savedProbeCenter = null;

  function getStatus() {
    return liveStatus || {};
  }

  async function pollMachineStatus() {
    try {
      const response = await fetch('/api/machine');
      const result = await response.json();
      if (result && result.status) {
        liveStatus = result.status;
      }
    } catch { /* ignore */ }
  }

  function renderStep() {
    switch (currentStep) {
      case STEPS.MENU: return renderCalibMenu();
      case STEPS.HOMING: return renderHoming();
      case STEPS.PLATE_QUESTION: return renderPlateQuestion();
      case STEPS.PLATE_INSERT: return renderPlateInsert();
      case STEPS.PLATE_ORIGIN: return renderPlateOriginStep();
      case STEPS.PROBE_APPROACH: return renderProbeApproach();
      case STEPS.PROBE_CALIBRATE: return renderProbeCalibrate();
      case STEPS.COMPLETE: return renderComplete();
    }
  }

  function setStep(step) {
    currentStep = step;
    renderStep();
  }

  function renderCalibMenu() {
    contentEl.innerHTML = `
      <div class="success-card">
        <h3>Kalibrierung wählen</h3>
        <div class="control-actions" style="flex-direction:column;gap:12px;">
          <button type="button" id="btn-wizard" class="menu-nav__item">Kalibrierung Messtaster / Bauplatte</button>
          <button type="button" id="btn-force-placeholder" class="menu-nav__item">Kalibrierung Kraftmessdose</button>
        </div>
      </div>
      <div class="control-actions page-actions">
        <button type="button" class="btn-back" data-route="menu">Zurück zum Hauptmenü</button>
      </div>
    `;
    contentEl.querySelector('#btn-wizard').addEventListener('click', () => setStep(STEPS.HOMING));
    contentEl.querySelector('#btn-force-placeholder').addEventListener('click', () => {
      contentEl.querySelector('#btn-force-placeholder').insertAdjacentHTML(
        'afterend',
        '<p class="command-feedback" style="margin-top:8px;">Die Kalibrierung der Kraftmessdose ist derzeit noch nicht implementiert.</p>'
      );
    });
    contentEl.querySelector('.btn-back').addEventListener('click', () => navigate('menu'));
  }

  function renderHoming() {
    const st = getStatus();
    const homed = st.isHomed === true;
    contentEl.innerHTML = `
      <div class="success-card" style="display:flex;gap:32px;align-items:flex-start;">
        <div style="flex:1;">
          <h3>Schritt 1 – Referenzfahrt</h3>
          <p>Führe zuerst eine Referenzfahrt der Maschine durch.</p>
          ${homed ? '<p style="color:var(--clr-ok,green);font-weight:600;">✔ Maschine ist referenziert.</p>' : ''}
          <p id="homing-feedback" class="command-feedback" aria-live="polite"></p>
        </div>
        <div class="control-actions" style="flex-direction:column;gap:8px;">
          <button type="button" id="btn-home" ${homed ? 'disabled' : ''}>Homing durchführen</button>
          <button type="button" id="btn-next" ${homed ? '' : 'disabled'}>Bestätigen und Weiter</button>
          <button type="button" id="btn-cancel">Abbrechen</button>
        </div>
      </div>
    `;
    const feedbackEl = contentEl.querySelector('#homing-feedback');
    contentEl.querySelector('#btn-home').addEventListener('click', async () => {
      try {
        feedbackEl.textContent = 'Homing gestartet …';
        feedbackEl.classList.remove('command-feedback--error');
        await sendCommand({ command: 'home' });
      } catch (err) {
        feedbackEl.textContent = err.message || String(err);
        feedbackEl.classList.add('command-feedback--error');
      }
    });
    contentEl.querySelector('#btn-next').addEventListener('click', () => setStep(STEPS.PLATE_QUESTION));
    contentEl.querySelector('#btn-cancel').addEventListener('click', () => setStep(STEPS.MENU));
  }

  function renderPlateQuestion() {
    contentEl.innerHTML = `
      <div class="success-card" style="display:flex;gap:32px;align-items:flex-start;">
        <div style="flex:1;">
          <h3>Schritt 2 – Bauplatte vorhanden?</h3>
          <p>Ist bereits eine Bauplatte eingespannt?</p>
        </div>
        <div class="control-actions" style="flex-direction:column;gap:8px;">
          <button type="button" id="btn-yes">Ja</button>
          <button type="button" id="btn-no">Nein</button>
          <button type="button" id="btn-cancel">Abbrechen</button>
        </div>
      </div>
    `;
    contentEl.querySelector('#btn-yes').addEventListener('click', () => setStep(STEPS.PLATE_ORIGIN));
    contentEl.querySelector('#btn-no').addEventListener('click', () => setStep(STEPS.PLATE_INSERT));
    contentEl.querySelector('#btn-cancel').addEventListener('click', () => setStep(STEPS.MENU));
  }

  function renderPlateInsert() {
    contentEl.innerHTML = `
      <div class="success-card" style="display:flex;gap:32px;align-items:flex-start;">
        <div style="flex:1;">
          <h3>Schritt 3 – Bauplatte einsetzen</h3>
          <p>Öffne das Bauplattenspannsystem.</p>
          <p>Lege die Bauplatte von Hand ein.</p>
          <p>Schließe anschließend das Spannsystem.</p>
          <p id="fixture-feedback" class="command-feedback" aria-live="polite"></p>
        </div>
        <div class="control-actions" style="flex-direction:column;gap:8px;">
          <button type="button" id="btn-fixture-open">Spannsystem öffnen</button>
          <button type="button" id="btn-fixture-close">Spannsystem schließen</button>
          <button type="button" id="btn-plate-done">Bauplatte ist eingespannt</button>
          <button type="button" id="btn-cancel">Abbrechen</button>
        </div>
      </div>
    `;
    const feedbackEl = contentEl.querySelector('#fixture-feedback');
    contentEl.querySelector('#btn-fixture-open').addEventListener('click', async () => {
      try {
        feedbackEl.textContent = '';
        feedbackEl.classList.remove('command-feedback--error');
        await fixtureOpen();
        feedbackEl.textContent = 'Spannsystem geöffnet.';
      } catch (err) {
        feedbackEl.textContent = err.message || String(err);
        feedbackEl.classList.add('command-feedback--error');
      }
    });
    contentEl.querySelector('#btn-fixture-close').addEventListener('click', async () => {
      try {
        feedbackEl.textContent = '';
        feedbackEl.classList.remove('command-feedback--error');
        await fixtureClose();
        feedbackEl.textContent = 'Spannsystem geschlossen.';
      } catch (err) {
        feedbackEl.textContent = err.message || String(err);
        feedbackEl.classList.add('command-feedback--error');
      }
    });
    contentEl.querySelector('#btn-plate-done').addEventListener('click', () => setStep(STEPS.PLATE_ORIGIN));
    contentEl.querySelector('#btn-cancel').addEventListener('click', () => setStep(STEPS.MENU));
  }

  function renderPlateOriginStep() {
    contentEl.innerHTML = `
      <div class="success-card" style="display:flex;gap:32px;align-items:flex-start;">
        <div style="flex:1;">
          <h3>Schritt 4 – Kalibrierpunkt Bauplatte</h3>
          <p>Fahre die Maschine von Hand an den gewünschten Kalibrierpunkt der Bauplatte.</p>
          <p>Dieser Punkt wird im Maschinenkoordinatensystem gespeichert und dient zukünftig als Referenzpunkt der Bauplatte.</p>
          <div id="plate-jog-panel"></div>
          <div id="plate-saved-info"></div>
          <p id="plate-feedback" class="command-feedback" aria-live="polite"></p>
        </div>
        <div class="control-actions" style="flex-direction:column;gap:8px;">
          <button type="button" id="btn-save-plate">Aktuelle Position als Kalibrierpunkt Bauplatte speichern</button>
          <button type="button" id="btn-next" disabled>Bestätigen und Weiter</button>
          <button type="button" id="btn-cancel">Abbrechen</button>
        </div>
      </div>
    `;
    const jogCleanup = mountJogPanel(contentEl.querySelector('#plate-jog-panel'), { getStatus });
    const origCleanup = cleanupFn;
    cleanupFn = () => { jogCleanup(); if (origCleanup) origCleanup(); };

    const feedbackEl = contentEl.querySelector('#plate-feedback');
    const infoEl = contentEl.querySelector('#plate-saved-info');
    const btnNext = contentEl.querySelector('#btn-next');

    if (savedPlateOrigin) {
      showPlateInfo(infoEl, savedPlateOrigin);
      btnNext.disabled = false;
    }

    contentEl.querySelector('#btn-save-plate').addEventListener('click', async () => {
      try {
        feedbackEl.textContent = '';
        feedbackEl.classList.remove('command-feedback--error');
        const result = await setPlateOrigin();
        const c = (result && result.calibration) || {};
        savedPlateOrigin = c.plateOrigin;
        showPlateInfo(infoEl, savedPlateOrigin);
        feedbackEl.textContent = 'Kalibrierpunkt Bauplatte gespeichert.';
        btnNext.disabled = false;
      } catch (err) {
        feedbackEl.textContent = err.message || String(err);
        feedbackEl.classList.add('command-feedback--error');
      }
    });
    contentEl.querySelector('#btn-next').addEventListener('click', () => setStep(STEPS.PROBE_APPROACH));
    contentEl.querySelector('#btn-cancel').addEventListener('click', () => setStep(STEPS.MENU));
  }

  function showPlateInfo(el, pose) {
    if (!pose) { el.innerHTML = ''; return; }
    el.innerHTML = `
      <div style="margin-top:8px;padding:8px;background:var(--clr-surface,#f5f5f5);border-radius:4px;">
        <strong>Kalibrierpunkt Bauplatte</strong><br/>
        X = ${formatCoord(pose.x)}<br/>
        Y = ${formatCoord(pose.y)}<br/>
        Z = ${formatCoord(pose.z)}
      </div>`;
  }

  function renderProbeApproach() {
    contentEl.innerHTML = `
      <div class="success-card" style="display:flex;gap:32px;align-items:flex-start;">
        <div style="flex:1;">
          <h3>Schritt 5 – Messtaster grob anfahren</h3>
          <p>Fahre den Messtaster von Hand grob in die Nähe der Messposition.</p>
          <p>Das genaue Einmessen wird im nächsten Schritt automatisch durchgeführt.</p>
          <div id="probe-jog-panel"></div>
        </div>
        <div class="control-actions" style="flex-direction:column;gap:8px;">
          <button type="button" id="btn-next">Bestätigen und Weiter</button>
          <button type="button" id="btn-cancel">Abbrechen</button>
        </div>
      </div>
    `;
    const jogCleanup = mountJogPanel(contentEl.querySelector('#probe-jog-panel'), { getStatus });
    const origCleanup = cleanupFn;
    cleanupFn = () => { jogCleanup(); if (origCleanup) origCleanup(); };

    contentEl.querySelector('#btn-next').addEventListener('click', () => setStep(STEPS.PROBE_CALIBRATE));
    contentEl.querySelector('#btn-cancel').addEventListener('click', () => setStep(STEPS.MENU));
  }

  function renderProbeCalibrate() {
    const st = getStatus();
    const running = st.processState === 'Running' && st.procedurePhase === 'ProbeCalibrate';
    contentEl.innerHTML = `
      <div class="success-card" style="display:flex;gap:32px;align-items:flex-start;">
        <div style="flex:1;">
          <h3>Schritt 6 – Messtaster einmessen</h3>
          <p>Der Messtaster wird jetzt automatisch eingemessen.</p>
          <p>Das Einmessprotokoll bestimmt die Position des Messtasters anhand der vorhandenen Messgeometrie.</p>
          <p id="probe-status" style="font-weight:600;"></p>
          <p id="probe-feedback" class="command-feedback" aria-live="polite"></p>
        </div>
        <div class="control-actions" style="flex-direction:column;gap:8px;">
          <button type="button" id="btn-start-m19" ${running ? 'disabled' : ''}>Einmessprotokoll starten</button>
          <button type="button" id="btn-stop">STOP</button>
          <button type="button" id="btn-cancel">Abbrechen</button>
        </div>
      </div>
    `;
    const feedbackEl = contentEl.querySelector('#probe-feedback');
    const statusEl = contentEl.querySelector('#probe-status');
    const btnStart = contentEl.querySelector('#btn-start-m19');
    let waitingForCalib = false;

    contentEl.querySelector('#btn-start-m19').addEventListener('click', async () => {
      try {
        feedbackEl.textContent = '';
        feedbackEl.classList.remove('command-feedback--error');
        statusEl.textContent = 'Einmessung läuft …';
        statusEl.style.color = '';
        btnStart.disabled = true;
        waitingForCalib = true;
        await startProbeCalibration();
      } catch (err) {
        waitingForCalib = false;
        btnStart.disabled = false;
        statusEl.textContent = '';
        feedbackEl.textContent = err.message || String(err);
        feedbackEl.classList.add('command-feedback--error');
      }
    });

    contentEl.querySelector('#btn-stop').addEventListener('click', async () => {
      try {
        await sendCommand({ command: 'stop' });
      } catch (err) {
        feedbackEl.textContent = err.message || String(err);
        feedbackEl.classList.add('command-feedback--error');
      }
    });

    contentEl.querySelector('#btn-cancel').addEventListener('click', () => setStep(STEPS.MENU));

    const checkCalibComplete = async () => {
      if (!waitingForCalib) return;
      const s = getStatus();
      if (s.processState === 'Aborted' || s.machineState === 'Fault') {
        waitingForCalib = false;
        btnStart.disabled = false;
        statusEl.textContent = 'Einmessung abgebrochen.';
        statusEl.style.color = 'var(--clr-err,red)';
        feedbackEl.textContent = 'Bisherige gültige Kalibrierung bleibt erhalten.';
        feedbackEl.classList.add('command-feedback--error');
        return;
      }
      if (s.probeCenterValid && (s.processState === 'Completed' || s.procedurePhase === 'Completed')) {
        waitingForCalib = false;
        try {
          const syncResult = await syncCalibrationFromController();
          const c = (syncResult && syncResult.calibration) || {};
          savedProbeCenter = c.probeCenter;
          setStep(STEPS.COMPLETE);
        } catch (err) {
          feedbackEl.textContent = err.message || String(err);
          feedbackEl.classList.add('command-feedback--error');
        }
      }
    };

    const calibPoll = window.setInterval(checkCalibComplete, 400);
    const origCleanup = cleanupFn;
    cleanupFn = () => {
      window.clearInterval(calibPoll);
      if (origCleanup) origCleanup();
    };
  }

  async function renderComplete() {
    let calib;
    try {
      const result = await fetchCalibration();
      calib = (result && result.calibration) || {};
    } catch {
      calib = {};
    }
    const po = calib.plateOrigin || savedPlateOrigin;
    const pc = calib.probeCenter || savedProbeCenter;

    contentEl.innerHTML = `
      <div class="success-card">
        <h3 style="color:var(--clr-ok,green);">Kalibrierung erfolgreich abgeschlossen</h3>
        <div style="display:flex;gap:32px;flex-wrap:wrap;margin-top:12px;">
          <div>
            <strong>Kalibrierpunkt Bauplatte</strong><br/>
            ${po ? `X = ${formatCoord(po.x)}<br/>Y = ${formatCoord(po.y)}<br/>Z = ${formatCoord(po.z)}` : '—'}
          </div>
          <div>
            <strong>Kalibrierpunkt Messtaster</strong><br/>
            ${pc ? `X = ${formatCoord(pc.x)}<br/>Y = ${formatCoord(pc.y)}<br/>Z = ${formatCoord(pc.z)}` : '—'}
          </div>
        </div>
        <div class="control-actions page-actions" style="margin-top:16px;">
          <button type="button" id="btn-back-menu">Zurück zum Kalibrieren-Menü</button>
        </div>
      </div>
    `;
    contentEl.querySelector('#btn-back-menu').addEventListener('click', () => setStep(STEPS.MENU));
  }

  let cleanupFn = null;

  pollTimer = window.setInterval(async () => {
    await pollMachineStatus();
    if (currentStep === STEPS.HOMING) {
      const st = getStatus();
      if (st.isHomed === true) {
        const btnNext = contentEl.querySelector('#btn-next');
        const btnHome = contentEl.querySelector('#btn-home');
        if (btnNext) btnNext.disabled = false;
        if (btnHome) btnHome.disabled = true;
        const fb = contentEl.querySelector('#homing-feedback');
        if (fb && !fb.classList.contains('command-feedback--error')) {
          fb.textContent = '';
        }
        const marker = contentEl.querySelector('h3');
        if (marker && !contentEl.querySelector('.homed-marker')) {
          marker.insertAdjacentHTML('afterend', '<p class="homed-marker" style="color:var(--clr-ok,green);font-weight:600;">✔ Maschine ist referenziert.</p>');
        }
      }
    }
  }, 500);

  renderStep();

  return () => {
    if (pollTimer) window.clearInterval(pollTimer);
    if (cleanupFn) cleanupFn();
  };
}
