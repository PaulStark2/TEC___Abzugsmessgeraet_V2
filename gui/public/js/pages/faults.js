'use strict';

import { navigate } from '../router.js';
import { ackAllMessages, ackMessage, fetchMessages } from '../messageBus.js';

function escapeHtml(value) {
  return String(value ?? '')
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;');
}

function formatTs(iso) {
  try {
    const d = new Date(iso);
    if (Number.isNaN(d.getTime())) return iso || '—';
    return d.toLocaleString('de-DE');
  } catch {
    return iso || '—';
  }
}

function severityClass(sev) {
  return `msg-sev msg-sev--${sev || 'info'}`;
}

export function renderFaults(container) {
  const panel = document.createElement('section');
  panel.className = 'panel page-panel faults-page';
  panel.innerHTML = `
    <h2>Fehler &amp; Meldungen</h2>
    <p class="placeholder-note">
      Aktive und historische Meldungen (max. 7 Tage). Quittieren entfernt die Meldung aus der Statuszeile;
      die Historie bleibt erhalten. Fault Clear ist separat in der Fault-Leiste.
    </p>
    <div class="control-actions page-actions">
      <button type="button" class="btn-back" data-route="menu">Zurück zum Hauptmenü</button>
      <button type="button" id="btn-ack-all">Alle aktiven quittieren</button>
      <button type="button" id="btn-refresh-msgs">Aktualisieren</button>
    </div>
    <div class="faults-layout">
      <div class="faults-table-wrap">
        <table class="faults-table" aria-label="Meldungsliste">
          <thead>
            <tr>
              <th>Zeit</th>
              <th>Stufe</th>
              <th>Titel</th>
              <th>Status</th>
            </tr>
          </thead>
          <tbody id="faults-tbody"></tbody>
        </table>
      </div>
      <aside class="faults-detail panel" aria-live="polite">
        <h3>Detail</h3>
        <p id="faults-detail-empty" class="placeholder-note">Meldung in der Tabelle anwählen.</p>
        <div id="faults-detail-body" hidden>
          <p><strong id="faults-detail-title"></strong></p>
          <p id="faults-detail-meta" class="placeholder-note"></p>
          <pre id="faults-detail-text" class="faults-detail-text"></pre>
          <div class="control-actions">
            <button type="button" id="btn-ack-one">Quittieren</button>
          </div>
        </div>
      </aside>
    </div>
    <p id="faults-feedback" class="command-feedback" aria-live="polite"></p>
  `;

  container.appendChild(panel);

  const tbody = panel.querySelector('#faults-tbody');
  const detailEmpty = panel.querySelector('#faults-detail-empty');
  const detailBody = panel.querySelector('#faults-detail-body');
  const detailTitle = panel.querySelector('#faults-detail-title');
  const detailMeta = panel.querySelector('#faults-detail-meta');
  const detailText = panel.querySelector('#faults-detail-text');
  const feedback = panel.querySelector('#faults-feedback');
  let selectedId = null;
  let rows = [];

  function setFeedback(text, isError) {
    feedback.textContent = text || '';
    feedback.classList.toggle('command-feedback--error', Boolean(isError));
  }

  function showDetail(row) {
    selectedId = row ? row.id : null;
    for (const tr of tbody.querySelectorAll('tr')) {
      tr.classList.toggle('is-selected', tr.dataset.id === selectedId);
    }
    if (!row) {
      detailEmpty.hidden = false;
      detailBody.hidden = true;
      return;
    }
    detailEmpty.hidden = true;
    detailBody.hidden = false;
    detailTitle.textContent = row.title || row.code;
    detailMeta.textContent = `${formatTs(row.ts)} · ${row.severity} · ${row.code} · ${
      row.active ? 'aktiv' : 'quittiert/historisch'
    } · Quelle: ${row.source || '—'}`;
    const ctx = row.context ? `\n\nKontext:\n${JSON.stringify(row.context, null, 2)}` : '';
    detailText.textContent = `${row.detail || '—'}${ctx}`;
  }

  async function reload() {
    try {
      const result = await fetchMessages({ limit: 300 });
      if (!result.ok) {
        setFeedback(result.error || 'Laden fehlgeschlagen', true);
        return;
      }
      rows = result.messages || [];
      tbody.innerHTML = rows
        .map(
          (r) => `<tr data-id="${escapeHtml(r.id)}" class="${r.active ? 'is-active' : ''}">
            <td>${escapeHtml(formatTs(r.ts))}</td>
            <td><span class="${severityClass(r.severity)}">${escapeHtml(r.severity)}</span></td>
            <td>${escapeHtml(r.title)}</td>
            <td>${r.active ? 'aktiv' : 'Historie'}</td>
          </tr>`
        )
        .join('');
      const still = rows.find((r) => r.id === selectedId);
      showDetail(still || null);
      setFeedback(`${rows.length} Meldungen`, false);
    } catch (err) {
      setFeedback(err.message || String(err), true);
    }
  }

  tbody.addEventListener('click', (ev) => {
    const tr = ev.target.closest('tr[data-id]');
    if (!tr) return;
    const row = rows.find((r) => r.id === tr.dataset.id);
    showDetail(row || null);
  });

  panel.querySelector('.btn-back').addEventListener('click', () => navigate('menu'));
  panel.querySelector('#btn-refresh-msgs').addEventListener('click', () => reload());
  panel.querySelector('#btn-ack-all').addEventListener('click', async () => {
    try {
      const result = await ackAllMessages();
      if (!result.ok) {
        setFeedback(result.error || 'Quittieren fehlgeschlagen', true);
        return;
      }
      setFeedback(`${result.count || 0} Meldungen quittiert`, false);
      await reload();
    } catch (err) {
      setFeedback(err.message || String(err), true);
    }
  });
  panel.querySelector('#btn-ack-one').addEventListener('click', async () => {
    if (!selectedId) return;
    try {
      const result = await ackMessage(selectedId);
      if (!result.ok) {
        setFeedback(result.error || 'Quittieren fehlgeschlagen', true);
        return;
      }
      setFeedback('Meldung quittiert', false);
      await reload();
    } catch (err) {
      setFeedback(err.message || String(err), true);
    }
  });

  reload();
  return () => {};
}
