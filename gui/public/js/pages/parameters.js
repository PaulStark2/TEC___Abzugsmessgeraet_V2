'use strict';

import { navigate } from '../router.js';
import {
  fetchParameterCatalog,
  fetchMachineConfig,
  unlockParameters,
  saveMachineConfig,
} from '../api.js';

function escapeHtml(str) {
  return String(str)
    .replaceAll('&', '&amp;')
    .replaceAll('<', '&lt;')
    .replaceAll('>', '&gt;')
    .replaceAll('"', '&quot;');
}

function rangeText(field) {
  if (field.minExclusive !== undefined) {
    return `> ${field.minExclusive}${field.max !== undefined ? ` … ${field.max}` : ''}`;
  }
  if (field.min !== undefined || field.max !== undefined) {
    return `${field.min !== undefined ? field.min : '—'} … ${
      field.max !== undefined ? field.max : '—'
    }`;
  }
  return 'endlich (Core-Validierung)';
}

/** Mirrors server parameterCatalog.evaluateCornerConsistencyHints (uses field.cornerWith). */
function evaluateCornerConsistencyHints(cfg, groups) {
  const hints = [];
  if (!cfg || typeof cfg !== 'object' || !Array.isArray(groups)) {
    return hints;
  }
  for (const group of groups) {
    for (const field of group.fields || []) {
      if (!field.cornerWith) {
        continue;
      }
      const corner = Number(cfg[field.key]);
      if (!(corner > 0)) {
        continue;
      }
      const a = Number(cfg[field.cornerWith.accelKey]);
      const speeds = (field.cornerWith.speedKeys || [])
        .map((key) => Number(cfg[key]))
        .filter((v) => Number.isFinite(v) && v > 0);
      const vmax = speeds.length ? Math.max(...speeds) : 0;
      if (!(a > 0)) {
        const minA = corner > 0 && vmax > 0 ? (vmax * vmax) / (2 * corner) : null;
        hints.push(
          minA !== null
            ? `${field.label}: Bei Corner > 0 muss a > 0 sein. Empfohlen: a ≥ ${minA.toFixed(3)} mm/s² oder R erhöhen / v senken.`
            : `${field.label}: Bei Corner > 0 muss a > 0 sein (sonst kein Blend).`
        );
        continue;
      }
      if (!(vmax > 0)) {
        continue;
      }
      const minR = (vmax * vmax) / (2 * a);
      if (corner + 1e-9 < minR) {
        const minA = (vmax * vmax) / (2 * corner);
        const maxV = Math.sqrt(2 * a * corner);
        hints.push(
          `${field.label}: R=${corner} < v²/(2a)=${minR.toFixed(3)} mm. ` +
            `Empfehlung: R ≥ ${minR.toFixed(3)} mm, oder a ≥ ${minA.toFixed(3)} mm/s², oder v ≤ ${maxV.toFixed(3)} mm/s.`
        );
      }
    }
  }
  return hints;
}

export function renderParameters(container) {
  container.innerHTML = `
    <section class="panel page-panel">
      <h2>Parametereinstellungen</h2>
      <p class="placeholder-note">
        Geschütztes Menü. Ungültige Werte werden vom Controller mit NACK abgelehnt;
        der bisherige gültige Wert bleibt erhalten (kein stilles Clamping).
        SafetyLimits sind nicht editierbar. toolOffset nur über Messtaster-Einmessung.
      </p>
      <p id="parameters-feedback" class="command-feedback" aria-live="polite"></p>
      <div id="parameters-content"></div>
      <div id="param-info-modal" class="param-info-overlay" hidden>
        <div class="param-info-dialog" role="dialog" aria-modal="true" aria-labelledby="param-info-title">
          <h3 id="param-info-title"></h3>
          <div id="param-info-body" class="param-info-body"></div>
          <div class="param-info-actions">
            <button type="button" id="param-info-close">Schließen</button>
          </div>
        </div>
      </div>
    </section>
  `;

  const feedbackEl = container.querySelector('#parameters-feedback');
  const contentEl = container.querySelector('#parameters-content');
  const modalEl = container.querySelector('#param-info-modal');
  const modalTitleEl = container.querySelector('#param-info-title');
  const modalBodyEl = container.querySelector('#param-info-body');

  function setFeedback(text, isError) {
    feedbackEl.textContent = text || '';
    feedbackEl.classList.toggle('command-feedback--error', Boolean(isError));
  }

  function closeInfoModal() {
    modalEl.hidden = true;
    modalTitleEl.textContent = '';
    modalBodyEl.innerHTML = '';
  }

  function openInfoModal(field) {
    modalTitleEl.textContent = field.label || field.key;
    modalBodyEl.innerHTML = `
      <p>${escapeHtml(field.info || '')}</p>
      <p><strong>Einheit:</strong> ${escapeHtml(field.unit || '—')}</p>
      <p><strong>Zulässiger Bereich:</strong> ${escapeHtml(rangeText(field))}</p>
      <p><strong>Wirkung:</strong> ${
        field.readOnly
          ? 'nur Anzeige — Wert wird durch Messtaster-Einmessung gesetzt'
          : field.future
            ? 'noch nicht im Core ausgewertet'
            : 'wirkt auf Core MachineConfig'
      }</p>
    `;
    modalEl.hidden = false;
  }

  container.querySelector('#param-info-close').addEventListener('click', closeInfoModal);
  modalEl.addEventListener('click', (ev) => {
    if (ev.target === modalEl) {
      closeInfoModal();
    }
  });

  function renderLocked() {
    contentEl.innerHTML = `
      <form id="param-unlock" class="control-form">
        <label>
          Passwort
          <input id="param-password" type="password" autocomplete="current-password" />
        </label>
        <div class="control-actions page-actions">
          <button type="submit">Entsperren</button>
          <button type="button" class="btn-back" data-route="menu">Zurück</button>
        </div>
      </form>
    `;
    contentEl.querySelector('.btn-back').addEventListener('click', () => navigate('menu'));
    contentEl.querySelector('#param-unlock').addEventListener('submit', async (ev) => {
      ev.preventDefault();
      try {
        await unlockParameters(contentEl.querySelector('#param-password').value);
        setFeedback('Entsperrt.', false);
        await refresh();
      } catch (err) {
        setFeedback(err.message || String(err), true);
      }
    });
  }

  function readFormValues(baseConfig) {
    const next = { ...baseConfig };
    contentEl.querySelectorAll('input[data-key]').forEach((input) => {
      if (input.readOnly || input.disabled) {
        return;
      }
      const key = input.getAttribute('data-key');
      next[key] = Number(input.value);
    });
    return next;
  }

  function updateCornerHints(groups, baseConfig) {
    const hintEl = contentEl.querySelector('#param-corner-hints');
    if (!hintEl) {
      return;
    }
    const hints = evaluateCornerConsistencyHints(readFormValues(baseConfig), groups);
    if (!hints.length) {
      hintEl.hidden = true;
      hintEl.innerHTML = '';
      return;
    }
    hintEl.hidden = false;
    hintEl.innerHTML = `<strong>Corner-Konsistenz:</strong><ul>${hints
      .map((h) => `<li>${escapeHtml(h)}</li>`)
      .join('')}</ul>`;
  }

  function renderEditor(groups, config) {
    const fieldByKey = new Map();
    for (const group of groups) {
      for (const field of group.fields || []) {
        fieldByKey.set(field.key, field);
      }
    }

    const tabs = groups
      .map(
        (group, idx) => `
      <button type="button" class="param-tab ${idx === 0 ? 'param-tab--active' : ''}" data-group="${escapeHtml(
          group.id || group.title
        )}">${escapeHtml(group.title)}</button>`
      )
      .join('');

    const sections = groups
      .map(
        (group, idx) => `
      <fieldset class="param-group" data-group-panel="${escapeHtml(group.id || group.title)}" ${
          idx === 0 ? '' : 'hidden'
        }>
        <legend>${escapeHtml(group.title)}</legend>
        <div class="param-table-wrap">
          <table class="param-table">
            <thead>
              <tr>
                <th scope="col">Parameter</th>
                <th scope="col">Wert</th>
                <th scope="col">Einheit</th>
                <th scope="col">Info</th>
              </tr>
            </thead>
            <tbody>
              ${(group.fields || [])
                .map((field) => {
                  const readOnly = Boolean(field.readOnly);
                  return `
                <tr class="${readOnly ? 'param-row--readonly' : ''}${
                    field.future ? ' param-row--future' : ''
                  }">
                  <td class="param-table__name">
                    ${escapeHtml(field.label)}
                    ${field.future ? '<span class="param-field__future">zukünftig</span>' : ''}
                    ${readOnly ? '<span class="param-field__readonly">nur Anzeige</span>' : ''}
                  </td>
                  <td class="param-table__value">
                    <input
                      data-key="${escapeHtml(field.key)}"
                      type="number"
                      step="${field.integer ? '1' : 'any'}"
                      ${field.min !== undefined ? `min="${field.min}"` : ''}
                      ${field.max !== undefined ? `max="${field.max}"` : ''}
                      value="${escapeHtml(config[field.key] ?? '')}"
                      ${readOnly ? 'readonly disabled' : ''}
                    />
                  </td>
                  <td class="param-table__unit">${escapeHtml(field.unit || '')}</td>
                  <td class="param-table__info">
                    <button type="button" class="param-info-btn" data-info-key="${escapeHtml(
                      field.key
                    )}" aria-label="Info zu ${escapeHtml(field.label)}">Info</button>
                  </td>
                </tr>`;
                })
                .join('')}
            </tbody>
          </table>
        </div>
      </fieldset>`
      )
      .join('');

    contentEl.innerHTML = `
      <div class="param-editor-layout">
        <div class="param-tabs" role="tablist">${tabs}</div>
        <form id="param-form" class="control-form">
          <div id="param-corner-hints" class="param-corner-hints" hidden></div>
          ${sections}
          <div class="control-actions page-actions">
            <button type="submit">Speichern und an Controller senden</button>
            <button type="button" class="btn-back" data-route="menu">Zurück</button>
          </div>
        </form>
      </div>
    `;

    contentEl.querySelectorAll('.param-tab').forEach((btn) => {
      btn.addEventListener('click', () => {
        const activeId = btn.getAttribute('data-group');
        contentEl.querySelectorAll('.param-tab').forEach((b) => {
          b.classList.toggle('param-tab--active', b === btn);
        });
        contentEl.querySelectorAll('[data-group-panel]').forEach((panel) => {
          panel.hidden = panel.getAttribute('data-group-panel') !== activeId;
        });
      });
    });

    contentEl.querySelectorAll('.param-info-btn').forEach((btn) => {
      btn.addEventListener('click', () => {
        const field = fieldByKey.get(btn.getAttribute('data-info-key'));
        if (field) {
          openInfoModal(field);
        }
      });
    });

    contentEl.querySelectorAll('input[data-key]:not([readonly])').forEach((input) => {
      input.addEventListener('input', () => updateCornerHints(groups, config));
    });
    updateCornerHints(groups, config);

    contentEl.querySelector('.btn-back').addEventListener('click', () => navigate('menu'));
    contentEl.querySelector('#param-form').addEventListener('submit', async (ev) => {
      ev.preventDefault();
      const next = readFormValues(config);
      try {
        await saveMachineConfig(next);
        setFeedback('Konfiguration gespeichert.', false);
      } catch (err) {
        setFeedback(err.message || String(err), true);
      }
    });
  }

  async function refresh() {
    const catalog = await fetchParameterCatalog();
    const cfg = await fetchMachineConfig();
    if (!catalog.unlocked) {
      renderLocked();
      return;
    }
    renderEditor(catalog.groups || [], cfg.config || {});
  }

  refresh().catch((err) => setFeedback(err.message || String(err), true));
  return () => {
    closeInfoModal();
  };
}
