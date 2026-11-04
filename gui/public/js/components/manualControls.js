'use strict';

import { sendCommand, teachProgramPoint, fetchProgramFiles } from '../api.js';
import { MCS_REQUIRED_MESSAGE } from '../homingGate.js';
import { reportCommandError } from '../messageBus.js';
import { bindJogAxes, jogAxesMarkup } from './jogControls.js';

const SPEED_OPTIONS = [5, 10, 15, 25, 40];

function formatCoord(value) {
  const n = Number(value);
  return Number.isFinite(n) ? n.toFixed(3) : '—';
}

/**
 * G7 manual controls: speed, jog (step/hold), spreader, homing stub.
 * @param {HTMLElement} container
 * @param {{ getStatus?: () => object }} [opts]
 * @returns {() => void} cleanup
 */
export function mountManualControls(container, opts) {
  const getStatus = (opts && opts.getStatus) || (() => ({}));

  container.innerHTML = `
    <div class="manual-section">
      <h3>Geschwindigkeit</h3>
      <div class="control-form manual-speed-form">
        <label>
          Jog-/Move-Geschwindigkeit (Einheiten/s)
          <select id="manual-speed">
            ${SPEED_OPTIONS.map((s) => `<option value="${s}"${s === 15 ? ' selected' : ''}>${s}</option>`).join('')}
          </select>
        </label>
        <button type="button" id="btn-apply-speed">Geschwindigkeit setzen</button>
      </div>
    </div>

    <div class="manual-section">
      <h3>Jog</h3>
      ${jogAxesMarkup()}
    </div>

    <div class="manual-section">
      <h3>Kraft</h3>
      <p class="placeholder-note">Ruheprobe im Stillstand: 20 Messwerte im Abstand von 100 ms, Dauer etwa 1,9 s. Die Achsen dürfen dabei nicht fahren.</p>
      <div class="control-actions">
        <button type="button" id="btn-force-tare">Ruheprobe</button>
      </div>
    </div>

    <div class="manual-section">
      <h3>Spreizer</h3>
      <div class="control-actions">
        <button type="button" id="btn-pneumatic-open">Spreizer öffnen</button>
        <button type="button" id="btn-pneumatic-close">Spreizer schließen</button>
      </div>
    </div>

    <div class="manual-section">
      <h3>Homing</h3>
      <p class="placeholder-note">Controller-gesteuertes Homing (CmdHome). Keine Sequenz in der GUI.</p>
      <div class="control-actions">
        <button type="button" id="btn-home">HOME</button>
      </div>
    </div>

    <div class="manual-section">
      <h3>Prüfphase (TestOrAge)</h3>
      <p class="placeholder-note">
        Startet nur SeatToForce und die Iterationen von der aktuellen Pose — ohne PreClamp/Programm.
        Spreizer wird geöffnet; TCP vorher händisch positionieren. Soft-STOP in der Statusleiste.
      </p>
      <div class="control-form manual-testorage-form">
        <label>
          Modus
          <select id="manual-toa-mode">
            <option value="test" selected>Prüfen</option>
            <option value="aging">Altern</option>
          </select>
        </label>
        <label>
          Prüfkraft (N)
          <input id="manual-toa-force" type="number" min="0.1" step="0.1" value="10">
        </label>
        <label>
          Iterationen
          <input id="manual-toa-iterations" type="number" min="1" step="1" value="1">
        </label>
        <button type="button" id="btn-start-testorage">Prüfphase starten</button>
      </div>
    </div>

    <div class="manual-section">
      <h3>Prüfpunkt teachen</h3>
      <p class="placeholder-note">
        Speichert die aktuelle TCP-Pose als Bauplatten-Koordinaten in einer JSON-Datei
        (gleicher Format wie beim Laden von Prüfpunkten). Bauplatte muss kalibriert sein.
      </p>
      <div class="control-actions">
        <button type="button" id="btn-teach-point">Prüfpunkt teachen</button>
      </div>
      <div id="teach-point-form" class="control-form manual-teach-form" hidden>
        <label>
          Punktname
          <input id="teach-point-name" type="text" maxlength="80" placeholder="z.B. Bohrung A">
        </label>
        <fieldset class="teach-mode-fieldset">
          <legend>Ziel</legend>
          <label class="teach-mode-option">
            <input type="radio" name="teach-mode" value="create" checked>
            Neue JSON-Datei
          </label>
          <label class="teach-mode-option">
            <input type="radio" name="teach-mode" value="append">
            In vorhandener JSON-Datei ergänzen
          </label>
        </fieldset>
        <label id="teach-program-name-wrap">
          Programmname (neue Datei)
          <input id="teach-program-name" type="text" maxlength="80" placeholder="z.B. Handteach_01">
        </label>
        <label id="teach-file-select-wrap" hidden>
          Vorhandene Datei
          <select id="teach-file-select"></select>
        </label>
        <div class="control-actions">
          <button type="button" id="btn-teach-save">Speichern</button>
          <button type="button" id="btn-teach-cancel">Abbrechen</button>
        </div>
      </div>
    </div>

    <p id="manual-feedback" class="command-feedback" aria-live="polite"></p>
  `;

  const speedSelect = container.querySelector('#manual-speed');
  const applySpeedBtn = container.querySelector('#btn-apply-speed');
  const openBtn = container.querySelector('#btn-pneumatic-open');
  const closeBtn = container.querySelector('#btn-pneumatic-close');
  const tareBtn = container.querySelector('#btn-force-tare');
  const homeBtn = container.querySelector('#btn-home');
  const toaModeEl = container.querySelector('#manual-toa-mode');
  const toaForceEl = container.querySelector('#manual-toa-force');
  const toaIterEl = container.querySelector('#manual-toa-iterations');
  const startToaBtn = container.querySelector('#btn-start-testorage');
  const teachOpenBtn = container.querySelector('#btn-teach-point');
  const teachForm = container.querySelector('#teach-point-form');
  const teachNameEl = container.querySelector('#teach-point-name');
  const teachProgramNameEl = container.querySelector('#teach-program-name');
  const teachProgramNameWrap = container.querySelector('#teach-program-name-wrap');
  const teachFileSelectWrap = container.querySelector('#teach-file-select-wrap');
  const teachFileSelect = container.querySelector('#teach-file-select');
  const teachSaveBtn = container.querySelector('#btn-teach-save');
  const teachCancelBtn = container.querySelector('#btn-teach-cancel');
  const feedbackEl = container.querySelector('#manual-feedback');

  function setFeedback(text, isError) {
    if (!feedbackEl) return;
    feedbackEl.textContent = text || '';
    feedbackEl.classList.toggle('command-feedback--error', Boolean(isError));
    if (isError && text) {
      reportCommandError(text);
    }
  }

  function selectedTeachMode() {
    const checked = container.querySelector('input[name="teach-mode"]:checked');
    return checked ? checked.value : 'create';
  }

  function syncTeachModeUi() {
    const mode = selectedTeachMode();
    const isCreate = mode === 'create';
    teachProgramNameWrap.hidden = !isCreate;
    teachFileSelectWrap.hidden = isCreate;
  }

  async function refreshTeachFileList() {
    teachFileSelect.innerHTML = '';
    try {
      const result = await fetchProgramFiles();
      const files = (result.files || []).filter((f) => f.valid && f.pointCount < 32);
      if (files.length === 0) {
        const opt = document.createElement('option');
        opt.value = '';
        opt.textContent = 'Keine ergänzbare Datei vorhanden';
        teachFileSelect.appendChild(opt);
        return;
      }
      for (const f of files) {
        const opt = document.createElement('option');
        opt.value = f.fileName;
        const labelName = f.programName ? `${f.programName} (${f.fileName})` : f.fileName;
        opt.textContent = `${labelName} — ${f.pointCount}/32`;
        teachFileSelect.appendChild(opt);
      }
    } catch (err) {
      const opt = document.createElement('option');
      opt.value = '';
      opt.textContent = err.message || 'Liste konnte nicht geladen werden';
      teachFileSelect.appendChild(opt);
    }
  }

  const onTeachOpen = async () => {
    teachForm.hidden = false;
    teachNameEl.value = '';
    teachProgramNameEl.value = '';
    syncTeachModeUi();
    await refreshTeachFileList();
    teachNameEl.focus();
  };

  const onTeachCancel = () => {
    teachForm.hidden = true;
  };

  const onTeachModeChange = () => {
    syncTeachModeUi();
  };

  const onTeachSave = async () => {
    const pointName = String(teachNameEl.value || '').trim();
    if (!pointName) {
      setFeedback('Bitte einen Punktnamen eingeben.', true);
      return;
    }
    const mode = selectedTeachMode();
    const body = { mode, pointName };
    if (mode === 'create') {
      const programName = String(teachProgramNameEl.value || '').trim() || pointName;
      body.programName = programName;
    } else {
      const fileName = String(teachFileSelect.value || '').trim();
      if (!fileName) {
        setFeedback('Bitte eine vorhandene Datei wählen.', true);
        return;
      }
      body.fileName = fileName;
    }
    try {
      const result = await teachProgramPoint(body);
      const p = result.point || {};
      setFeedback(
        `Gespeichert in ${result.fileName}: „${p.name}“ Seq ${p.sequenceNumber} ` +
          `(Plate ${formatCoord(p.x)}, ${formatCoord(p.y)}, ${formatCoord(p.z)})`,
        false
      );
      teachForm.hidden = true;
    } catch (err) {
      setFeedback(err.message || String(err), true);
    }
  };

  const onApplySpeed = async () => {
    const speed = Number(speedSelect.value);
    try {
      await sendCommand({ command: 'setSpeed', speed });
      setFeedback(`Geschwindigkeit auf ${speed} gesetzt`, false);
    } catch (err) {
      setFeedback(err.message || String(err), true);
    }
  };

  const onTare = async () => {
    try {
      await sendCommand({ command: 'forceTare' });
      setFeedback('Ruheprobe gestartet (etwa 1,9 s). Achsen nicht verfahren.', false);
    } catch (err) {
      setFeedback(err.message || String(err), true);
    }
  };

  const onOpen = async () => {
    try {
      await sendCommand({ command: 'pneumaticOpen' });
      setFeedback('Spreizer geöffnet', false);
    } catch (err) {
      setFeedback(err.message || String(err), true);
    }
  };

  const onClose = async () => {
    try {
      await sendCommand({ command: 'pneumaticClose' });
      setFeedback('Spreizer geschlossen', false);
    } catch (err) {
      setFeedback(err.message || String(err), true);
    }
  };

  const onHome = async () => {
    try {
      let mcsValid = false;
      try {
        const response = await fetch('/api/machine');
        const result = await response.json();
        mcsValid = Boolean(result && result.status && result.status.mcsValid === true);
      } catch {
        mcsValid = false;
      }
      if (!mcsValid) {
        setFeedback(MCS_REQUIRED_MESSAGE, true);
        return;
      }
      await sendCommand({ command: 'home' });
      setFeedback('HOME-Befehl gesendet', false);
    } catch (err) {
      setFeedback(err.message || String(err), true);
    }
  };

  const onStartTestOrAge = async () => {
    const mode = toaModeEl.value;
    const force = Number(toaForceEl.value);
    const iterations = Number(toaIterEl.value);
    if (!(force > 0) || !Number.isFinite(force)) {
      setFeedback('Prüfkraft muss > 0 sein', true);
      return;
    }
    if (!Number.isInteger(iterations) || iterations < 1) {
      setFeedback('Iterationen muss eine ganze Zahl ≥ 1 sein', true);
      return;
    }
    try {
      await sendCommand({
        command: 'setTestConfiguration',
        mode,
        target_force_N: force,
        iterations,
        active_point_index: 0,
      });
      await sendCommand({ command: 'startTestOrAge' });
      setFeedback(
        `Prüfphase gestartet (${mode === 'aging' ? 'Altern' : 'Prüfen'}, ${force} N, ${iterations}×)`,
        false
      );
    } catch (err) {
      setFeedback(err.message || String(err), true);
    }
  };

  applySpeedBtn.addEventListener('click', onApplySpeed);
  tareBtn.addEventListener('click', onTare);
  openBtn.addEventListener('click', onOpen);
  closeBtn.addEventListener('click', onClose);
  homeBtn.addEventListener('click', onHome);
  startToaBtn.addEventListener('click', onStartTestOrAge);
  teachOpenBtn.addEventListener('click', () => {
    onTeachOpen().catch((err) => setFeedback(err.message || String(err), true));
  });
  teachCancelBtn.addEventListener('click', onTeachCancel);
  teachSaveBtn.addEventListener('click', () => {
    onTeachSave().catch((err) => setFeedback(err.message || String(err), true));
  });
  for (const radio of container.querySelectorAll('input[name="teach-mode"]')) {
    radio.addEventListener('change', onTeachModeChange);
  }
  const unbindJog = bindJogAxes(container, { getStatus, setFeedback });

  return () => {
    applySpeedBtn.removeEventListener('click', onApplySpeed);
    tareBtn.removeEventListener('click', onTare);
    openBtn.removeEventListener('click', onOpen);
    closeBtn.removeEventListener('click', onClose);
    homeBtn.removeEventListener('click', onHome);
    startToaBtn.removeEventListener('click', onStartTestOrAge);
    teachOpenBtn.onclick = null;
    teachCancelBtn.onclick = null;
    teachSaveBtn.onclick = null;
    unbindJog();
  };
}
