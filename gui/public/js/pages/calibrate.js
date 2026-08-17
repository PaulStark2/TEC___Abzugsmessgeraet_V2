'use strict';

import { navigate } from '../router.js';
import { fetchCalibration, setPlateOrigin, startProbeCalibration } from '../api.js';

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
        Messtaster: startet den Core-Platzhalter <code>ProbeCalibration</code> (kein historisches Einmessprotokoll).
      </p>
      <p id="calibrate-feedback" class="command-feedback" aria-live="polite"></p>
      <div id="calibrate-content"></div>
    </section>
  `;

  const feedbackEl = container.querySelector('#calibrate-feedback');
  const contentEl = container.querySelector('#calibrate-content');

  function setFeedback(text, isError) {
    feedbackEl.textContent = text || '';
    feedbackEl.classList.toggle('command-feedback--error', Boolean(isError));
  }

  async function refresh() {
    const result = await fetchCalibration();
    const c = (result && result.calibration) || {};
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
        <h3>Messtaster (Platzhalter)</h3>
        <p class="placeholder-note">
          Gültig: <b>${c.probeCenterValid ? 'ja' : 'nein'}</b><br/>
          ProbeCenter: <b>${escapeHtml(formatPose(c.probeCenter))}</b>
        </p>
        <div class="control-actions">
          <button type="button" id="btn-probe">Platzhalter-Einmessung starten</button>
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
        await startProbeCalibration();
        setFeedback('ProbeCalibration-Platzhalter gestartet.', false);
      } catch (err) {
        setFeedback(err.message || String(err), true);
      }
    });
  }

  refresh().catch((err) => setFeedback(err.message || String(err), true));
  return () => {};
}
