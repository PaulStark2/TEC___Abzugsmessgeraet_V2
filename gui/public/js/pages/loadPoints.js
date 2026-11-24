'use strict';

import { navigate } from '../router.js';
import {
  uploadProgram,
  fetchCurrentProgram,
  deleteCurrentProgram,
  fetchProcedureStatus,
  fetchProgramFiles,
  loadProgramFile,
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

function renderErrorList(errors) {
  const items = (errors || [])
    .map(
      (e) =>
        `<li><span class="error-path">${escapeHtml(e.path || '')}</span> ${escapeHtml(e.message || '')}</li>`
    )
    .join('');

  return `
    <div class="error-card">
      <h3>Validierungsfehler</h3>
      <ul class="error-list">${items}</ul>
    </div>
  `;
}

function renderCluster(summary) {
  const plates = Array.isArray(summary.plates) ? summary.plates : null;
  const points = summary.testPoints || summary.pointsPreview || [];
  if (plates && plates.length) {
    return plates
      .map(
        (plate) => `
        <section class="job-plate">
          <h4>Platte ${escapeHtml(plate.plate)}</h4>
          ${(plate.orders || [])
            .map(
              (order) => `
              <div class="job-order">
                <h5>Auftrag ${escapeHtml(order.orderUid)}</h5>
                <table class="program-table">
                  <thead>
                    <tr><th>Zahn</th><th>X</th><th>Y</th><th>Z Boden</th></tr>
                  </thead>
                  <tbody>
                    ${(order.teeth || [])
                      .map(
                        (tooth) => `
                      <tr>
                        <td>${escapeHtml(tooth.toothNumber)}</td>
                        <td>${formatNumber(tooth.x)}</td>
                        <td>${formatNumber(tooth.y)}</td>
                        <td>${formatNumber(tooth.z)}</td>
                      </tr>`
                      )
                      .join('')}
                  </tbody>
                </table>
              </div>`
            )
            .join('')}
        </section>`
      )
      .join('');
  }
  return `
    <table class="program-table">
      <thead>
        <tr><th>Platte</th><th>Auftrag</th><th>Zahn</th><th>X</th><th>Y</th><th>Z Boden</th></tr>
      </thead>
      <tbody>
        ${points
          .map(
            (p) => `
          <tr>
            <td>${escapeHtml(p.plate ?? '')}</td>
            <td>${escapeHtml(p.orderUid ?? '')}</td>
            <td>${escapeHtml(p.toothNumber ?? '')}</td>
            <td>${formatNumber(p.x)}</td>
            <td>${formatNumber(p.y)}</td>
            <td>${formatNumber(p.z)}</td>
          </tr>`
          )
          .join('')}
      </tbody>
    </table>`;
}

function renderProgramSummary(summary) {
  const points = summary.testPoints || summary.pointsPreview || [];
  const count = summary.count ?? points.length;
  const range = (span) =>
    span && Number.isFinite(span.min) ? `${formatNumber(span.min)} … ${formatNumber(span.max)}` : '—';

  return `
    <div class="success-card">
      <h3>Erfolgreich geladen</h3>
      <p class="placeholder-note">
        Baujob: <b>${escapeHtml(summary.buildJobNumber ?? summary.programName ?? '—')}</b><br/>
        Zähne: <b>${escapeHtml(count)}</b>
      </p>
      <p class="placeholder-note">
        Koordinaten sind <b>Bauplatten-Koordinaten</b> [mm].
        Z ist die maximale Tiefe (Boden), nicht die angefahrene Höhe.
        Die Anfahrt liegt um den Parameter „Z-Abstand Anfahrt“ darüber.
      </p>
      <p class="placeholder-note">
        X-Bereich: ${range(summary.xRange)}<br/>
        Y-Bereich: ${range(summary.yRange)}<br/>
        Z-Boden: ${range(summary.zRange)}
      </p>
      <h4>Geladene Prüfstellen (${count})</h4>
      ${renderCluster(summary)}
    </div>
  `;
}

export function renderLoadPoints(container) {
  container.innerHTML = `
    <section class="panel page-panel">
      <h2>Prüfstellen laden</h2>
      <p class="placeholder-note">
        Job-JSON des Datenverarbeitungssystems laden (Baujob, Platte, Auftrag, Zahn).
        Der Server prüft die Datei und hält sie nur im RAM. Ohne Prüfstand reicht der Beispiel-Job.
      </p>

      <div class="upload-card">
        <label class="file-label">
          Job-Datei (.json)
          <input id="program-file" type="file" accept="application/json,.json" />
        </label>
        <div class="control-actions">
          <button type="button" id="btn-upload-program">Upload / Laden</button>
          <button type="button" id="btn-load-sample">Beispiel-Job laden</button>
          <button type="button" id="btn-delete-program" disabled>Prüfstellen löschen</button>
        </div>
        <p id="upload-status" class="command-feedback" aria-live="polite"></p>
      </div>

      <div class="upload-card">
        <h3>Programme auf dem Gerät</h3>
        <p class="placeholder-note">
          Dateien aus dem Handmodus-Teach (<code>gui/server/data/programs</code>).
        </p>
        <div class="control-form">
          <label>
            Gespeicherte Datei
            <select id="device-program-select"></select>
          </label>
        </div>
        <div class="control-actions">
          <button type="button" id="btn-load-device-program">Vom Gerät laden</button>
          <button type="button" id="btn-refresh-device-programs">Aktualisieren</button>
        </div>
      </div>

      <div id="program-summary-root" class="summary-root"></div>

      <div class="control-actions page-actions">
        <button type="button" class="btn-back" data-route="menu">Zurück zum Hauptmenü</button>
      </div>
    </section>
  `;

  const backBtn = container.querySelector('.btn-back');
  backBtn.addEventListener('click', () => navigate('menu'));

  const fileEl = container.querySelector('#program-file');
  const statusEl = container.querySelector('#upload-status');
  const summaryRoot = container.querySelector('#program-summary-root');
  const uploadBtn = container.querySelector('#btn-upload-program');
  const sampleBtn = container.querySelector('#btn-load-sample');
  const deleteBtn = container.querySelector('#btn-delete-program');
  const deviceSelect = container.querySelector('#device-program-select');
  const loadDeviceBtn = container.querySelector('#btn-load-device-program');
  const refreshDeviceBtn = container.querySelector('#btn-refresh-device-programs');

  function setStatus(text, isError) {
    statusEl.textContent = text || '';
    statusEl.classList.toggle('command-feedback--error', Boolean(isError));
  }

  async function refreshDeviceProgramList() {
    deviceSelect.innerHTML = '';
    try {
      const result = await fetchProgramFiles();
      const files = (result.files || []).filter((f) => f.valid);
      if (files.length === 0) {
        const opt = document.createElement('option');
        opt.value = '';
        opt.textContent = 'Keine gespeicherten Programme';
        deviceSelect.appendChild(opt);
        loadDeviceBtn.disabled = true;
        return;
      }
      loadDeviceBtn.disabled = false;
      for (const f of files) {
        const opt = document.createElement('option');
        opt.value = f.fileName;
        const labelName = f.programName ? `${f.programName} (${f.fileName})` : f.fileName;
        opt.textContent = `${labelName} — ${f.pointCount} Punkte`;
        deviceSelect.appendChild(opt);
      }
    } catch (err) {
      const opt = document.createElement('option');
      opt.value = '';
      opt.textContent = err.message || 'Liste nicht verfügbar';
      deviceSelect.appendChild(opt);
      loadDeviceBtn.disabled = true;
    }
  }

  async function onLoadDeviceProgram() {
    const fileName = String(deviceSelect.value || '').trim();
    if (!fileName) {
      setStatus('Bitte eine gespeicherte Datei wählen.', true);
      return;
    }
    summaryRoot.innerHTML = '';
    setStatus('Programm wird vom Gerät geladen…', false);
    try {
      const result = await loadProgramFile(fileName);
      if (!result.ok) {
        setStatus(result.error || 'Laden fehlgeschlagen', true);
        return;
      }
      setStatus(`Programm „${fileName}“ geladen.`, false);
      summaryRoot.innerHTML = renderProgramSummary(result.summary);
      await refreshDeleteButton();
    } catch (err) {
      setStatus(err.message || String(err), true);
    }
  }

  async function refreshDeleteButton() {
    const [programResult, procedureResult] = await Promise.all([
      fetchCurrentProgram(),
      fetchProcedureStatus(),
    ]);
    const hasProgram = programResult.ok && programResult.current;
    const procState = procedureResult.status?.state;
    const procActive =
      procState === 'running' || procState === 'paused' || procState === 'unavailable';
    deleteBtn.disabled = !hasProgram || procActive;
  }

  async function onDeleteProgram() {
    setStatus('Prüfstellen werden gelöscht…', false);
    try {
      const result = await deleteCurrentProgram();
      if (!result.ok) {
        setStatus(result.error || 'Löschen fehlgeschlagen', true);
        await refreshDeleteButton();
        return;
      }
      summaryRoot.innerHTML = '';
      fileEl.value = '';
      setStatus('Geladenes Programm und Auswahl wurden gelöscht.', false);
      await refreshDeleteButton();
    } catch (err) {
      setStatus(err.message || String(err), true);
      await refreshDeleteButton();
    }
  }

  async function onUpload() {
    summaryRoot.innerHTML = '';

    const file = fileEl.files && fileEl.files[0];
    if (!file) {
      setStatus('Bitte eine .json Datei auswählen.', true);
      return;
    }

    setStatus('Datei wird geladen…', false);
    const text = await file.text();

    let parsed;
    try {
      parsed = JSON.parse(text);
    } catch {
      setStatus('Ungültiges JSON: Parsing fehlgeschlagen.', true);
      return;
    }

    setStatus('Upload wird validiert…', false);
    const result = await uploadProgram(parsed);

    if (!result.ok) {
      setStatus('Upload abgelehnt (siehe Fehlerliste).', true);
      summaryRoot.innerHTML = renderErrorList(result.errors);
      return;
    }

    setStatus('Programm erfolgreich geladen.', false);
    summaryRoot.innerHTML = renderProgramSummary(result.summary);
    await refreshDeleteButton();
  }

  async function onLoadSample() {
    summaryRoot.innerHTML = '';
    setStatus('Beispiel-Job wird geladen…', false);
    try {
      const response = await fetch('/samples/Messpunkte_Friktionskraft.json');
      if (!response.ok) {
        setStatus('Beispiel-Job nicht gefunden.', true);
        return;
      }
      const parsed = await response.json();
      const result = await uploadProgram(parsed);
      if (!result.ok) {
        setStatus('Beispiel-Job abgelehnt.', true);
        summaryRoot.innerHTML = renderErrorList(result.errors);
        return;
      }
      setStatus('Beispiel-Job geladen. Als Nächstes unter „Prüfstellen auswählen“ die Reihenfolge festlegen.', false);
      summaryRoot.innerHTML = renderProgramSummary(result.summary);
      await refreshDeleteButton();
    } catch (err) {
      setStatus(err.message || String(err), true);
    }
  }

  uploadBtn.addEventListener('click', () => {
    onUpload().catch((err) => setStatus(err.message || String(err), true));
  });

  sampleBtn.addEventListener('click', () => {
    onLoadSample().catch((err) => setStatus(err.message || String(err), true));
  });

  deleteBtn.addEventListener('click', () => {
    onDeleteProgram().catch((err) => setStatus(err.message || String(err), true));
  });

  loadDeviceBtn.addEventListener('click', () => {
    onLoadDeviceProgram().catch((err) => setStatus(err.message || String(err), true));
  });

  refreshDeviceBtn.addEventListener('click', () => {
    refreshDeviceProgramList().catch((err) => setStatus(err.message || String(err), true));
  });

  refreshDeviceProgramList().catch(() => {});

  fetchCurrentProgram()
    .then((programResult) => {
      if (programResult.ok && programResult.current) {
        const points = programResult.current.testPoints || [];
        summaryRoot.innerHTML = renderProgramSummary({
          programName: programResult.current.meta?.programName,
          buildJobNumber: programResult.current.meta?.buildJobNumber,
          count: points.length,
          plates: programResult.current.plates,
          testPoints: points,
          xRange: {
            min: Math.min(...points.map((p) => p.x)),
            max: Math.max(...points.map((p) => p.x)),
          },
          yRange: {
            min: Math.min(...points.map((p) => p.y)),
            max: Math.max(...points.map((p) => p.y)),
          },
          zRange: {
            min: Math.min(...points.map((p) => p.z)),
            max: Math.max(...points.map((p) => p.z)),
          },
        });
      }
      return refreshDeleteButton();
    })
    .catch(() => refreshDeleteButton());

  return () => {
    uploadBtn.onclick = null;
    deleteBtn.onclick = null;
    loadDeviceBtn.onclick = null;
    refreshDeviceBtn.onclick = null;
  };
}
