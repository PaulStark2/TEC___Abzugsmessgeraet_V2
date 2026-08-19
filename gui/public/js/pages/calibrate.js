'use strict';

import { navigate } from '../router.js';
import {
  fetchCalibration,
  fetchMachineConfig,
  setPlateOrigin,
  startProbeCalibration,
  syncCalibrationFromController,
} from '../api.js';

function escapeHtml(str) {
  return String(str)
    .replaceAll('&', '&amp;')
    .replaceAll('<', '&lt;')
    .replaceAll('>', '&gt;');
}

function formatPose(p) {
  if (!p) return '—';
  return `x=${Number(p.x).toFixed(3)}  y=${Number(p.y).toFixed(3)}  z=${Number(p.z).toFixed(3)}`;
}

export function renderCalibrate(container) {
  container.innerHTML = `
    <section class="panel page-panel">
      <h2>Kalibrieren</h2>
      <p class="placeholder-note">
        Bauplatten-Ursprung: aktuelle Maschinenpose speichern (überschreibt den bisherigen Wert).
        Messtaster: Werkzeug manuell in die Nähe der Messscheibe fahren, dann
        <code>CmdStartProbeCalibration</code> starten. ProbeApproach entfällt im Manual-Modus.
        Der Offset wird bei Erfolg ersetzt, nicht aufaddiert.
      </p>
      <p id="calibrate-feedback" class="command-feedback" aria-live="polite"></p>
      <div id="calibrate-content"></div>
    </section>
  `;

  const feedbackEl = container.querySelector('#calibrate-feedback');
  const contentEl = container.querySelector('#calibrate-content');
  let pollTimer = null;
  let waitingForCalib = false;

  function setFeedback(text, isError) {
    feedbackEl.textContent = text || '';
    feedbackEl.classList.toggle('command-feedback--error', Boolean(isError));
  }

  async function persistIfComplete(status) {
    if (!waitingForCalib || !status) {
      return;
    }
    if (status.processState === 'Aborted' || status.machineState === 'Fault') {
      waitingForCalib = false;
      setFeedback('Einmessung abgebrochen — bisherige gültige Kalibrierung bleibt erhalten.', true);
      return;
    }
    if (status.probeCenterValid && (status.processState === 'Completed' || status.procedurePhase === 'Completed')) {
      waitingForCalib = false;
      try {
        await syncCalibrationFromController();
        setFeedback('Einmessung abgeschlossen. probe_center und tool_offset gespeichert (ersetzt).', false);
        await refresh();
      } catch (err) {
        setFeedback(err.message || String(err), true);
      }
    }
  }

  async function pollMachineStatus() {
    if (!waitingForCalib) {
      return;
    }
    try {
      const response = await fetch('/api/machine');
      const result = await response.json();
      await persistIfComplete(result && result.status);
    } catch (err) {
      setFeedback(err.message || String(err), true);
    }
  }

  async function refresh() {
    const [calibResult, cfgResult] = await Promise.all([fetchCalibration(), fetchMachineConfig()]);
    const c = (calibResult && calibResult.calibration) || {};
    const cfg = (cfgResult && cfgResult.config) || {};
    const toolOffset = c.toolOffset || {
      x: cfg.tool_offset_x,
      y: cfg.tool_offset_y,
      z: cfg.tool_offset_z,
    };
    contentEl.innerHTML = `
      <div class="success-card">
        <h3>Bauplatte</h3>
        <p class="placeholder-note">
          Gültig: <b>${c.plateOriginValid ? 'ja' : 'nein'}</b><br/>
          PlateOrigin: <b>${escapeHtml(formatPose(c.plateOrigin))}</b>
        </p>
        <div class="control-actions">
          <button type="button" id="btn-set-plate">Aktuelle Pose als PlateOrigin speichern</button>
        </div>
      </div>
      <div class="success-card">
        <h3>Messtaster</h3>
        <p class="placeholder-note">
          Gültig: <b>${c.probeCenterValid ? 'ja' : 'nein'}</b><br/>
          ProbeCenter: <b>${escapeHtml(formatPose(c.probeCenter))}</b><br/>
          ToolOffset: <b>${escapeHtml(formatPose(toolOffset))}</b><br/>
          NominalProbe: <b>${escapeHtml(formatPose({
            x: cfg.nominal_probe_x,
            y: cfg.nominal_probe_y,
            z: cfg.nominal_probe_z,
          }))}</b>
        </p>
        <div class="control-actions">
          <button type="button" id="btn-probe">Manuelle Einmessung starten</button>
        </div>
      </div>
      <div class="control-actions page-actions">
        <button type="button" class="btn-back" data-route="menu">Zurück</button>
      </div>
    `;
    contentEl.querySelector('.btn-back').addEventListener('click', () => navigate('menu'));
    contentEl.querySelector('#btn-set-plate').addEventListener('click', async () => {
      try {
        await setPlateOrigin();
        setFeedback('PlateOrigin gesetzt (überschrieben).', false);
        await refresh();
      } catch (err) {
        setFeedback(err.message || String(err), true);
      }
    });
    contentEl.querySelector('#btn-probe').addEventListener('click', async () => {
      try {
        waitingForCalib = true;
        await startProbeCalibration();
        setFeedback('Einmessung gestartet. Bitte warten, bis Z und ±X/±Y abgeschlossen sind.', false);
      } catch (err) {
        waitingForCalib = false;
        setFeedback(err.message || String(err), true);
      }
    });
  }

  pollTimer = window.setInterval(() => {
    pollMachineStatus();
  }, 400);

  refresh().catch((err) => setFeedback(err.message || String(err), true));
  return () => {
    if (pollTimer) {
      window.clearInterval(pollTimer);
    }
  };
}
