'use strict';

import { navigate } from '../router.js';
import {
  sendCommand,
  fetchCalibration,
  setPlateOrigin,
  defineMcs,
  persistMcs,
  rollbackFullCalibration,
  startProbeCalibration,
  syncCalibrationFromController,
  fixtureOpen,
  fixtureClose,
} from '../api.js';
import { mountJogPanel } from '../components/jogPanel.js';
import { MCS_REQUIRED_MESSAGE, GEOMETRY_INCOMPLETE_MESSAGE } from '../homingGate.js';

function formatCoord(v) {
  return Number(v).toFixed(3);
}

const MODE = {
  NONE: 'none',
  FULL: 'full',
  GEOMETRY: 'geometry',
  FORCE: 'force',
};

const STEPS = {
  MENU: 'menu',
  DEFINE_MCS: 'define_mcs',
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
  let selectedMode = MODE.NONE;
  let liveStatus = null;
  let savedPlateOrigin = null;
  let savedProbeCenter = null;
  let mcsValidInStore = false;
  /** True after persistMcs in this Full-Calibration session (commit done). */
  let mcsCommittedThisSession = false;
  let cleanupFn = null;

  function getStatus() {
    return liveStatus || {};
  }

  async function abortCalibrationWizard() {
    try {
      if (selectedMode === MODE.FULL && !mcsCommittedThisSession) {
        await rollbackFullCalibration();
      }
    } catch (err) {
      console.error(err);
    }
    const wasCommittedIncomplete = selectedMode === MODE.FULL && mcsCommittedThisSession;
    selectedMode = MODE.NONE;
    mcsCommittedThisSession = false;
    setStep(STEPS.MENU);
    window.setTimeout(() => {
      const fb = contentEl.querySelector('#menu-feedback');
      if (!fb) {
        return;
      }
      if (wasCommittedIncomplete) {
        fb.textContent = GEOMETRY_INCOMPLETE_MESSAGE;
        fb.classList.add('command-feedback--error');
      }
    }, 0);
  }

  async function pollMachineStatus() {
    try {
      const response = await fetch('/api/machine');
      const result = await response.json();
      if (result && result.status) {
        liveStatus = result.status;
      }
    } catch {
      /* ignore */
    }
  }

  async function refreshStoreFlags() {
    try {
      const result = await fetchCalibration();
      const c = (result && result.calibration) || {};
      mcsValidInStore = Boolean(c.machineCoordinateSystem && c.machineCoordinateSystem.valid);
    } catch {
      mcsValidInStore = false;
    }
  }

  function renderStep() {
    switch (currentStep) {
      case STEPS.MENU:
        return renderCalibMenu();
      case STEPS.DEFINE_MCS:
        return renderDefineMcs();
      case STEPS.HOMING:
        return renderHoming();
      case STEPS.PLATE_QUESTION:
        return renderPlateQuestion();
      case STEPS.PLATE_INSERT:
        return renderPlateInsert();
      case STEPS.PLATE_ORIGIN:
        return renderPlateOriginStep();
      case STEPS.PROBE_APPROACH:
        return renderProbeApproach();
      case STEPS.PROBE_CALIBRATE:
        return renderProbeCalibrate();
      case STEPS.COMPLETE:
        return renderComplete();
      default:
        return renderCalibMenu();
    }
  }

  function setStep(step) {
    currentStep = step;
    renderStep();
  }

  function renderCalibMenu() {
    void refreshStoreFlags().then(() => {
      if (currentStep !== STEPS.MENU) {
        return;
      }
      const geomDisabled = !mcsValidInStore;
      contentEl.innerHTML = `
      <div class="success-card">
        <h3>Kalibrierung wählen</h3>
        <fieldset class="calib-mode-fieldset" style="border:none;padding:0;margin:0 0 16px;">
          <label style="display:block;margin:10px 0;">
            <input type="radio" name="calib-mode" value="full" />
            <strong>MCS und Bauplatte/Messtaster definieren und kalibrieren</strong>
            <p style="margin:4px 0 0 24px;opacity:0.85;">
              Definiert den Maschinenursprung neu. Die bisherige Bauplatten- und Messtasterkalibrierung
              wird dadurch ungültig und muss anschließend neu durchgeführt werden.
            </p>
          </label>
          <label style="display:block;margin:10px 0;${geomDisabled ? 'opacity:0.55;' : ''}">
            <input type="radio" name="calib-mode" value="geometry" ${geomDisabled ? 'disabled' : ''} />
            <strong>Bauplatte/Messtaster kalibrieren</strong>
            <p style="margin:4px 0 0 24px;opacity:0.85;">
              Das bestehende Maschinenkoordinatensystem bleibt unverändert.
              Nur Bauplatte und Messtaster werden neu eingemessen.
            </p>
            ${
              geomDisabled
                ? `<p style="margin:8px 0 0 24px;color:var(--clr-err,red);">
                    Es ist noch kein gültiges Maschinenkoordinatensystem definiert.
                    Bitte zuerst „MCS und Bauplatte/Messtaster definieren und kalibrieren“ durchführen.
                  </p>`
                : ''
            }
          </label>
          <label style="display:block;margin:10px 0;opacity:0.55;">
            <input type="radio" name="calib-mode" value="force" disabled />
            <strong>Kraftmessdose kalibrieren</strong>
            <p style="margin:4px 0 0 24px;">Noch nicht verfügbar.</p>
          </label>
        </fieldset>
        <div class="control-actions" style="flex-direction:column;gap:8px;">
          <button type="button" id="btn-start" class="menu-nav__item">Start</button>
        </div>
        <p id="menu-feedback" class="command-feedback" aria-live="polite"></p>
      </div>
      <div class="control-actions page-actions">
        <button type="button" class="btn-back" data-route="menu">Zurück zum Hauptmenü</button>
      </div>
    `;
      const feedbackEl = contentEl.querySelector('#menu-feedback');
      void fetchCalibration()
        .then((result) => {
          const c = (result && result.calibration) || {};
          if (
            c.machineCoordinateSystem &&
            c.machineCoordinateSystem.valid &&
            (!c.plateOriginValid || !c.probeCenterValid)
          ) {
            feedbackEl.textContent = GEOMETRY_INCOMPLETE_MESSAGE;
            feedbackEl.classList.add('command-feedback--error');
          }
        })
        .catch(() => {});
      contentEl.querySelector('#btn-start').addEventListener('click', () => {
        const chosen = contentEl.querySelector('input[name="calib-mode"]:checked');
        if (!chosen) {
          feedbackEl.textContent = 'Bitte eine Kalibrierfunktion auswählen.';
          feedbackEl.classList.add('command-feedback--error');
          return;
        }
        feedbackEl.textContent = '';
        feedbackEl.classList.remove('command-feedback--error');
        if (chosen.value === 'full') {
          selectedMode = MODE.FULL;
          mcsCommittedThisSession = false;
          savedPlateOrigin = null;
          savedProbeCenter = null;
          setStep(STEPS.DEFINE_MCS);
          return;
        }
        if (chosen.value === 'geometry') {
          if (!mcsValidInStore) {
            feedbackEl.textContent = MCS_REQUIRED_MESSAGE;
            feedbackEl.classList.add('command-feedback--error');
            return;
          }
          selectedMode = MODE.GEOMETRY;
          savedPlateOrigin = null;
          savedProbeCenter = null;
          setStep(STEPS.HOMING);
          return;
        }
        feedbackEl.textContent = 'Die Kalibrierung der Kraftmessdose ist derzeit noch nicht implementiert.';
        feedbackEl.classList.add('command-feedback--error');
      });
      contentEl.querySelector('.btn-back').addEventListener('click', () => navigate('menu'));
    });
  }

  function renderDefineMcs() {
    const st = getStatus();
    const mcsOk = st.mcsValid === true && st.isHomed === true;
    contentEl.innerHTML = `
      <div class="success-card" style="display:flex;gap:32px;align-items:flex-start;">
        <div style="flex:1;">
          <h3>Schritt 1 – Maschinenkoordinatensystem definieren</h3>
          <p>
            Die Maschine fährt an die Endanschläge und legt den Homing-Punkt neu fest.
            Bestehende Bauplatten- und Messtasterwerte werden im Core ungültig.
            Die gespeicherte Kalibrierung bleibt bis zur Bestätigung unverändert.
          </p>
          ${mcsOk ? '<p class="homed-marker" style="color:var(--clr-ok,green);font-weight:600;">✔ MCS definiert und referenziert (noch nicht gespeichert).</p>' : ''}
          <p id="define-feedback" class="command-feedback" aria-live="polite"></p>
        </div>
        <div class="control-actions" style="flex-direction:column;gap:8px;">
          <button type="button" id="btn-define" ${mcsOk ? 'disabled' : ''}>MCS definieren</button>
          <button type="button" id="btn-next" ${mcsOk ? '' : 'disabled'}>Bestätigen und Weiter</button>
          <button type="button" id="btn-cancel">Abbrechen</button>
        </div>
      </div>
    `;
    const feedbackEl = contentEl.querySelector('#define-feedback');
    contentEl.querySelector('#btn-define').addEventListener('click', async () => {
      try {
        feedbackEl.textContent = 'MCS-Definition gestartet …';
        feedbackEl.classList.remove('command-feedback--error');
        await defineMcs();
      } catch (err) {
        feedbackEl.textContent = err.message || String(err);
        feedbackEl.classList.add('command-feedback--error');
      }
    });
    contentEl.querySelector('#btn-next').addEventListener('click', async () => {
      try {
        const s = getStatus();
        const pose = s.measuredHome || { x: s.x, y: s.y, z: s.z };
        await persistMcs(pose);
        mcsCommittedThisSession = true;
        setStep(STEPS.PLATE_QUESTION);
      } catch (err) {
        feedbackEl.textContent = err.message || String(err);
        feedbackEl.classList.add('command-feedback--error');
      }
    });
    contentEl.querySelector('#btn-cancel').addEventListener('click', () => {
      void abortCalibrationWizard();
    });
  }

  function renderHoming() {
    const st = getStatus();
    const homed = st.isHomed === true;
    contentEl.innerHTML = `
      <div class="success-card" style="display:flex;gap:32px;align-items:flex-start;">
        <div style="flex:1;">
          <h3>Schritt 1 – Referenzfahrt</h3>
          <p>Führe bei Bedarf eine Referenzfahrt durch. Das Maschinenkoordinatensystem bleibt unverändert.</p>
          ${homed ? '<p class="homed-marker" style="color:var(--clr-ok,green);font-weight:600;">✔ Maschine ist referenziert.</p>' : ''}
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
        const live = getStatus();
        if (live.mcsValid !== true) {
          feedbackEl.textContent = MCS_REQUIRED_MESSAGE;
          feedbackEl.classList.add('command-feedback--error');
          return;
        }
        feedbackEl.textContent = 'Homing gestartet …';
        feedbackEl.classList.remove('command-feedback--error');
        await sendCommand({ command: 'home' });
      } catch (err) {
        feedbackEl.textContent = err.message || String(err);
        feedbackEl.classList.add('command-feedback--error');
      }
    });
    contentEl.querySelector('#btn-next').addEventListener('click', () => setStep(STEPS.PLATE_QUESTION));
    contentEl.querySelector('#btn-cancel').addEventListener('click', () => {
      void abortCalibrationWizard();
    });
  }

  function renderPlateQuestion() {
    contentEl.innerHTML = `
      <div class="success-card" style="display:flex;gap:32px;align-items:flex-start;">
        <div style="flex:1;">
          <h3>Bauplatte vorhanden?</h3>
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
    contentEl.querySelector('#btn-cancel').addEventListener('click', () => {
      void abortCalibrationWizard();
    });
  }

  function renderPlateInsert() {
    contentEl.innerHTML = `
      <div class="success-card" style="display:flex;gap:32px;align-items:flex-start;">
        <div style="flex:1;">
          <h3>Bauplatte einsetzen</h3>
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
    contentEl.querySelector('#btn-cancel').addEventListener('click', () => {
      void abortCalibrationWizard();
    });
  }

  function renderPlateOriginStep() {
    contentEl.innerHTML = `
      <div class="success-card" style="display:flex;gap:32px;align-items:flex-start;">
        <div style="flex:1;">
          <h3>Kalibrierpunkt Bauplatte</h3>
          <p>Fahre die Maschine von Hand an den gewünschten Kalibrierpunkt der Bauplatte.</p>
          <p>Dieser Punkt wird im Maschinenkoordinatensystem gespeichert.</p>
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
    cleanupFn = () => {
      jogCleanup();
      if (origCleanup) origCleanup();
    };

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
    contentEl.querySelector('#btn-cancel').addEventListener('click', () => {
      void abortCalibrationWizard();
    });
  }

  function showPlateInfo(el, pose) {
    if (!pose) {
      el.innerHTML = '';
      return;
    }
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
          <h3>Messtaster grob anfahren</h3>
          <p>Fahre den Messtaster von Hand grob in die Nähe der Messposition.</p>
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
    cleanupFn = () => {
      jogCleanup();
      if (origCleanup) origCleanup();
    };

    contentEl.querySelector('#btn-next').addEventListener('click', () => setStep(STEPS.PROBE_CALIBRATE));
    contentEl.querySelector('#btn-cancel').addEventListener('click', () => {
      void abortCalibrationWizard();
    });
  }

  function renderProbeCalibrate() {
    const st = getStatus();
    const running = st.processState === 'Running' && st.procedurePhase === 'ProbeCalibrate';
    contentEl.innerHTML = `
      <div class="success-card" style="display:flex;gap:32px;align-items:flex-start;">
        <div style="flex:1;">
          <h3>Messtaster einmessen</h3>
          <p>Der Messtaster wird jetzt automatisch eingemessen.</p>
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

    contentEl.querySelector('#btn-cancel').addEventListener('click', () => {
      void abortCalibrationWizard();
    });

    const checkCalibComplete = async () => {
      if (!waitingForCalib) return;
      const s = getStatus();
      if (s.processState === 'Aborted' || s.machineState === 'Fault') {
        waitingForCalib = false;
        btnStart.disabled = false;
        statusEl.textContent = 'Einmessung abgebrochen.';
        statusEl.style.color = 'var(--clr-err,red)';
        if (s.faultReason === 'ProbeSearchNoContact') {
          feedbackEl.textContent =
            'Kein Messtasterkontakt (Timeout). Bisherige gültige Kalibrierung bleibt erhalten.';
        } else {
          feedbackEl.textContent = 'Bisherige gültige Kalibrierung bleibt erhalten.';
        }
        feedbackEl.classList.add('command-feedback--error');
        return;
      }
      if (s.probeCenterValid && (s.processState === 'Completed' || s.procedurePhase === 'Completed')) {
        waitingForCalib = false;
        try {
          const syncResult = await syncCalibrationFromController({ persistProbeCenter: true });
          const c = (syncResult && syncResult.calibration) || {};
          savedProbeCenter = c.probeCenter;
          if (syncResult && syncResult.legacySemanticsWarning) {
            feedbackEl.textContent =
              'Hinweis: alte Kalibrierdaten erkannt (Legacy toolOffset). Bitte FullCalib neu durchführen.';
            feedbackEl.classList.add('command-feedback--error');
          }
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
    const mcs = calib.machineCoordinateSystem;

    contentEl.innerHTML = `
      <div class="success-card">
        <h3 style="color:var(--clr-ok,green);">Kalibrierung erfolgreich abgeschlossen</h3>
        <p style="margin-top:8px;">
          Modus: ${selectedMode === MODE.FULL ? 'MCS + Bauplatte/Messtaster' : 'Bauplatte/Messtaster'}
        </p>
        <div style="display:flex;gap:32px;flex-wrap:wrap;margin-top:12px;">
          <div>
            <strong>MCS Homing-Punkt</strong><br/>
            ${
              mcs && mcs.homePosition
                ? `X = ${formatCoord(mcs.homePosition.x)}<br/>Y = ${formatCoord(mcs.homePosition.y)}<br/>Z = ${formatCoord(mcs.homePosition.z)}`
                : '—'
            }
          </div>
          <div>
            <strong>Kalibrierpunkt Bauplatte</strong><br/>
            ${po ? `X = ${formatCoord(po.x)}<br/>Y = ${formatCoord(po.y)}<br/>Z = ${formatCoord(po.z)}` : '—'}
          </div>
          <div>
            <strong>Messtaster-Referenz (probeCenter)</strong><br/>
            ${pc ? `X = ${formatCoord(pc.x)}<br/>Y = ${formatCoord(pc.y)}<br/>Z = ${formatCoord(pc.z)}` : '—'}
          </div>
          <div>
            <strong>Werkzeugoffset (nach Install = 0)</strong><br/>
            ${
              calib.toolOffset
                ? `X = ${formatCoord(calib.toolOffset.x)}<br/>Y = ${formatCoord(calib.toolOffset.y)}<br/>Z = ${formatCoord(calib.toolOffset.z)}`
                : '—'
            }
          </div>
        </div>
        <div class="control-actions page-actions" style="margin-top:16px;">
          <button type="button" id="btn-back-menu">Zurück zum Kalibrieren-Menü</button>
        </div>
      </div>
    `;
    contentEl.querySelector('#btn-back-menu').addEventListener('click', () => setStep(STEPS.MENU));
  }

  pollTimer = window.setInterval(async () => {
    await pollMachineStatus();
    if (currentStep === STEPS.DEFINE_MCS) {
      const st = getStatus();
      if (st.mcsValid === true && st.isHomed === true) {
        const btnNext = contentEl.querySelector('#btn-next');
        const btnDefine = contentEl.querySelector('#btn-define');
        if (btnNext) btnNext.disabled = false;
        if (btnDefine) btnDefine.disabled = true;
        if (!contentEl.querySelector('.homed-marker')) {
          const marker = contentEl.querySelector('h3');
          if (marker) {
            marker.insertAdjacentHTML(
              'afterend',
              '<p class="homed-marker" style="color:var(--clr-ok,green);font-weight:600;">✔ MCS definiert und referenziert.</p>'
            );
          }
        }
      }
    }
    if (currentStep === STEPS.HOMING) {
      const st = getStatus();
      if (st.isHomed === true) {
        const btnNext = contentEl.querySelector('#btn-next');
        const btnHome = contentEl.querySelector('#btn-home');
        if (btnNext) btnNext.disabled = false;
        if (btnHome) btnHome.disabled = true;
        if (!contentEl.querySelector('.homed-marker')) {
          const marker = contentEl.querySelector('h3');
          if (marker) {
            marker.insertAdjacentHTML(
              'afterend',
              '<p class="homed-marker" style="color:var(--clr-ok,green);font-weight:600;">✔ Maschine ist referenziert.</p>'
            );
          }
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
