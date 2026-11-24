'use strict';

import { navigate } from '../router.js';
import {
  fetchCurrentProgram,
  fetchProgramSelection,
  updateProgramSelection,
  confirmProgramSelection,
  clearProgramSelection,
  deleteCurrentProgram,
  fetchProcedureStatus,
  fetchMachineConfig,
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

function laufLabel(numbers) {
  if (!numbers.length) {
    return '';
  }
  const sorted = [...numbers].sort((a, b) => a - b);
  if (sorted.length === 1) {
    return `Lauf ${sorted[0]}`;
  }
  const contiguous = sorted.every((value, index) => index === 0 || value === sorted[index - 1] + 1);
  if (contiguous) {
    return `Lauf ${sorted[0]}–${sorted[sorted.length - 1]}`;
  }
  return `Lauf ${sorted.join(', ')}`;
}

function renderConfirmedSummary(selection, clearance) {
  const points = selection.points || [];
  return `
    <div class="success-card selection-summary">
      <h3>Auswahl bestätigt</h3>
      <p class="placeholder-note">
        Baujob <b>${escapeHtml(selection.meta?.buildJobNumber ?? '—')}</b><br/>
        ${points.length} Prüfstelle(n).
        ${selection.confirmedAt ? `<br/>Bestätigt: ${escapeHtml(selection.confirmedAt)}` : ''}
      </p>
      ${renderPreviewTable(points, clearance)}
    </div>
  `;
}

function renderPreviewTable(points, clearance, options = {}) {
  if (!points.length) {
    return '<p class="placeholder-note">Noch keine Prüfstelle angehakt.</p>';
  }
  const draggable = Boolean(options.draggable);
  return `
    <div class="program-table-scroll">
      <table class="program-table">
        <thead>
          <tr>
            ${draggable ? '<th></th>' : ''}
            <th>Lauf</th><th>Platte</th><th>Auftrag</th><th>Zahn</th>
            <th>X</th><th>Y</th><th>Z Boden</th><th>Z Anfahrt</th>
          </tr>
        </thead>
        <tbody>
          ${points
            .map((p) => {
              const approach = Number(p.z) + clearance;
              return `
              <tr class="job-preview-row" ${draggable ? `draggable="true" data-id="${escapeHtml(p.id)}"` : ''}>
                ${draggable ? '<td class="job-drag-handle" title="Ziehen zum Sortieren">↕</td>' : ''}
                <td>${p.sequenceNumber}</td>
                <td>${escapeHtml(p.plate)}</td>
                <td>${escapeHtml(p.orderUid)}</td>
                <td>${escapeHtml(p.toothNumber)}</td>
                <td>${formatNumber(p.x)}</td>
                <td>${formatNumber(p.y)}</td>
                <td>${formatNumber(p.z)}</td>
                <td>${formatNumber(approach)}</td>
              </tr>`;
            })
            .join('')}
        </tbody>
      </table>
    </div>
  `;
}

export function renderSelectPoints(container) {
  container.innerHTML = `
    <section class="panel page-panel">
      <h2>Prüfstellen auswählen</h2>
      <p id="select-status" class="command-feedback" aria-live="polite"></p>
      <div id="select-content"></div>
    </section>
  `;

  const statusEl = container.querySelector('#select-status');
  const contentEl = container.querySelector('#select-content');
  let clearance = 2;

  function setStatus(text, isError) {
    statusEl.textContent = text || '';
    statusEl.classList.toggle('command-feedback--error', Boolean(isError));
  }

  async function wireDeleteProgram(button) {
    if (!button) {
      return;
    }
    const procedure = await fetchProcedureStatus();
    const procActive =
      procedure.status?.state === 'running' ||
      procedure.status?.state === 'paused' ||
      procedure.status?.state === 'unavailable';
    button.disabled = procActive;
    button.addEventListener('click', async () => {
      try {
        const result = await deleteCurrentProgram();
        if (!result.ok) {
          setStatus(result.error || 'Löschen fehlgeschlagen', true);
          return;
        }
        setStatus('Geladenes Programm und Auswahl wurden gelöscht.', false);
        navigate('load-points');
      } catch (err) {
        setStatus(err.message || String(err), true);
      }
    });
  }

  async function applyDraft(body, okMessage) {
    const result = await updateProgramSelection(body);
    if (!result.ok) {
      setStatus(result.error || 'Auswahl abgelehnt', true);
      return;
    }
    await refresh();
    if (okMessage) {
      setStatus(okMessage, false);
    }
  }

  async function refresh() {
    setStatus('Programm wird geladen…', false);
    try {
      const cfg = await fetchMachineConfig();
      const value = Number(cfg && cfg.config && cfg.config.point_z_clearance_mm);
      if (Number.isFinite(value) && value >= 0) {
        clearance = value;
      }
    } catch {
      clearance = 2;
    }

    const programResult = await fetchCurrentProgram();
    if (!programResult.ok) {
      contentEl.innerHTML = `
        <div class="error-card">
          <h3>Kein Programm geladen</h3>
          <p class="placeholder-note">
            Bitte zuerst einen Job laden, bevor Prüfstellen ausgewählt werden können.
          </p>
          <div class="control-actions page-actions">
            <button type="button" id="btn-go-load">Zu „Prüfstellen laden“</button>
            <button type="button" class="btn-back" data-route="menu">Zurück zum Hauptmenü</button>
          </div>
        </div>
      `;
      contentEl.querySelector('#btn-go-load').addEventListener('click', () => navigate('load-points'));
      contentEl.querySelector('.btn-back').addEventListener('click', () => navigate('menu'));
      setStatus('', false);
      return;
    }

    const selectionResult = await fetchProgramSelection();
    const selection = selectionResult.selection || {
      confirmed: false,
      selectedIds: [],
      preview: [],
      points: [],
      toothRank: [],
      meta: { totalPoints: 0, selectedCount: 0 },
    };

    if (selection.confirmed) {
      contentEl.innerHTML = `
        ${renderConfirmedSummary(selection, clearance)}
        <div class="control-actions page-actions">
          <button type="button" id="btn-edit-selection">Auswahl ändern</button>
          <button type="button" id="btn-delete-program">Prüfstellen löschen</button>
          <button type="button" class="btn-back" data-route="menu">Zurück zum Hauptmenü</button>
        </div>
      `;
      contentEl.querySelector('#btn-edit-selection').addEventListener('click', async () => {
        try {
          await clearProgramSelection();
          await refresh();
        } catch (err) {
          setStatus(err.message || String(err), true);
        }
      });
      contentEl.querySelector('.btn-back').addEventListener('click', () => navigate('menu'));
      await wireDeleteProgram(contentEl.querySelector('#btn-delete-program'));
      setStatus('Bestätigte Auswahl ist für die Prozedur verfügbar.', false);
      return;
    }

    const meta = programResult.current.meta || {};
    const plates = programResult.current.plates || [];
    const preview = selection.preview || [];
    const laufById = new Map(preview.map((point) => [point.id, point.sequenceNumber]));
    const totalPoints = Number(selection.meta?.totalPoints ?? (programResult.current.testPoints || []).length);
    const allSelected = totalPoints > 0 && preview.length === totalPoints;

    contentEl.innerHTML = `
      <p class="placeholder-note">
        Baujob: <b>${escapeHtml(meta.buildJobNumber ?? meta.programName ?? '—')}</b>
        · ${escapeHtml(totalPoints)} Zahn/Zähne
        · Z-Abstand Anfahrt: <b>${formatNumber(clearance)} mm</b>
      </p>
      <p class="placeholder-note">
        Z in der Datei ist die maximale Tiefe. Angefahren wird Z + Abstand.
        Ein Haken am Auftrag nimmt alle seine Zähne auf, einzelne Zähne bleiben frei wählbar.
        Neu angehakte Punkte werden hinten an die Liste gesetzt. Die bestehende Reihenfolge bleibt
        und lässt sich unten durch Ziehen frei ändern.
      </p>

      <div class="control-actions selection-toolbar">
        <button type="button" id="btn-select-all">${allSelected ? 'Alles abwählen' : 'Alles auswählen'}</button>
        <button type="button" id="btn-clear-selection">Auswahl löschen</button>
        <button type="button" id="btn-delete-program">Prüfstellen löschen</button>
      </div>

      <div class="job-clusters">
        ${plates
          .map((plate) => {
            const orders = plate.orders || [];
            return `
              <section class="job-plate">
                <h3>Platte ${escapeHtml(plate.plate)}</h3>
                ${orders
                  .map((order) => {
                    const teeth = [...(order.teeth || [])].sort((a, b) => a.toothNumber - b.toothNumber);
                    const selectedCount = teeth.filter((tooth) => laufById.has(tooth.id)).length;
                    const state = selectedCount === 0 ? 'none' : selectedCount === teeth.length ? 'all' : 'some';
                    const laufs = teeth
                      .map((tooth) => laufById.get(tooth.id))
                      .filter((value) => value != null);
                    return `
                      <article class="job-order">
                        <header class="job-order-head">
                          <label class="job-order-check">
                            <input type="checkbox" class="order-checkbox" data-plate="${escapeHtml(plate.plate)}" data-uid="${escapeHtml(order.orderUid)}" data-state="${state}" ${state === 'all' ? 'checked' : ''}/>
                            <span>Auftrag ${escapeHtml(order.orderUid)}</span>
                          </label>
                          <span class="job-lauf">${escapeHtml(laufLabel(laufs))}</span>
                        </header>
                        <table class="program-table">
                          <thead>
                            <tr><th></th><th>Lauf</th><th>Zahn</th><th>X</th><th>Y</th><th>Z Boden</th><th>Z Anfahrt</th></tr>
                          </thead>
                          <tbody>
                            ${teeth
                              .map((tooth) => {
                                const id = tooth.id;
                                const lauf = laufById.get(id);
                                return `
                                <tr>
                                  <td><input type="checkbox" class="point-checkbox" data-id="${escapeHtml(id)}" ${lauf != null ? 'checked' : ''}/></td>
                                  <td>${lauf != null ? lauf : '—'}</td>
                                  <td>${escapeHtml(tooth.toothNumber)}</td>
                                  <td>${formatNumber(tooth.x)}</td>
                                  <td>${formatNumber(tooth.y)}</td>
                                  <td>${formatNumber(tooth.z)}</td>
                                  <td>${formatNumber(Number(tooth.z) + clearance)}</td>
                                </tr>`;
                              })
                              .join('')}
                          </tbody>
                        </table>
                      </article>`;
                  })
                  .join('')}
              </section>`;
          })
          .join('')}
      </div>

      <section class="job-preview">
        <h3>Prüfreihenfolge vor dem Bestätigen (${preview.length})</h3>
        <p class="placeholder-note">Zeilen an der Markierung ziehen. Die Reihenfolge darf danach jede Folge sein.</p>
        ${renderPreviewTable(preview, clearance, { draggable: true })}
      </section>

      <div class="control-actions page-actions">
        <button type="button" id="btn-confirm-selection">Auswahl bestätigen</button>
        <button type="button" class="btn-back" data-route="menu">Zurück</button>
      </div>
    `;

    contentEl.querySelectorAll('.order-checkbox').forEach((box) => {
      if (box.dataset.state === 'some') {
        box.indeterminate = true;
      }
      box.addEventListener('change', () => {
        const selected = box.dataset.state !== 'all';
        applyDraft(
          {
            orderSelection: {
              plate: box.dataset.plate,
              orderUid: box.dataset.uid,
              selected,
            },
          },
          selected ? 'Auftrag in die Reihenfolge aufgenommen.' : 'Auftrag aus der Reihenfolge genommen.'
        ).catch((err) => setStatus(err.message || String(err), true));
      });
    });

    contentEl.querySelectorAll('.point-checkbox').forEach((box) => {
      box.addEventListener('change', () => {
        const ids = preview.map((point) => point.id);
        const id = box.dataset.id;
        const next = box.checked ? (ids.includes(id) ? ids : [...ids, id]) : ids.filter((item) => item !== id);
        applyDraft({ toothRank: next }).catch((err) => setStatus(err.message || String(err), true));
      });
    });

    let dragId = '';
    contentEl.querySelectorAll('.job-preview-row').forEach((row) => {
      row.addEventListener('dragstart', (event) => {
        dragId = row.dataset.id || '';
        row.classList.add('job-preview-row--dragging');
        if (event.dataTransfer) {
          event.dataTransfer.effectAllowed = 'move';
          event.dataTransfer.setData('text/plain', dragId);
        }
      });
      row.addEventListener('dragend', () => {
        row.classList.remove('job-preview-row--dragging');
        dragId = '';
      });
      row.addEventListener('dragover', (event) => {
        event.preventDefault();
        row.classList.add('job-preview-row--over');
        if (event.dataTransfer) {
          event.dataTransfer.dropEffect = 'move';
        }
      });
      row.addEventListener('dragleave', () => {
        row.classList.remove('job-preview-row--over');
      });
      row.addEventListener('drop', (event) => {
        event.preventDefault();
        row.classList.remove('job-preview-row--over');
        const from = (event.dataTransfer && event.dataTransfer.getData('text/plain')) || dragId;
        const to = row.dataset.id;
        if (!from || !to || from === to) {
          return;
        }
        const ids = preview.map((point) => point.id).filter((id) => id !== from);
        const index = ids.indexOf(to);
        if (index < 0) {
          return;
        }
        ids.splice(index, 0, from);
        applyDraft({ toothRank: ids }, 'Reihenfolge geändert.').catch((err) =>
          setStatus(err.message || String(err), true)
        );
      });
    });

    contentEl.querySelector('#btn-select-all').addEventListener('click', () => {
      applyDraft(
        { selectAll: true },
        allSelected ? 'Auswahl geleert.' : 'Offene Zähne an die Reihenfolge angehängt.'
      ).catch((err) => setStatus(err.message || String(err), true));
    });

    contentEl.querySelector('#btn-clear-selection').addEventListener('click', async () => {
      try {
        await clearProgramSelection();
        await refresh();
        setStatus('Auswahl gelöscht.', false);
      } catch (err) {
        setStatus(err.message || String(err), true);
      }
    });

    contentEl.querySelector('#btn-confirm-selection').addEventListener('click', async () => {
      try {
        const result = await confirmProgramSelection();
        if (!result.ok) {
          setStatus(result.error || 'Bestätigen fehlgeschlagen', true);
          return;
        }
        await refresh();
        setStatus('Auswahl bestätigt.', false);
      } catch (err) {
        setStatus(err.message || String(err), true);
      }
    });

    contentEl.querySelector('.btn-back').addEventListener('click', () => navigate('menu'));
    await wireDeleteProgram(contentEl.querySelector('#btn-delete-program'));
    setStatus('', false);
  }

  refresh().catch((err) => setStatus(err.message || String(err), true));
  return () => {};
}
