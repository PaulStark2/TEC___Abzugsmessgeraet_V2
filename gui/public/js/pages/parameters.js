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

export function renderParameters(container) {
  container.innerHTML = `
    <section class="panel page-panel">
      <h2>Parametereinstellungen</h2>
      <p class="placeholder-note">
        Geschütztes Menü. Ungültige Werte werden vom Controller mit NACK abgelehnt;
        der bisherige gültige Wert bleibt erhalten (kein stilles Clamping).
        SafetyLimits sind nicht editierbar.
      </p>
      <p id="parameters-feedback" class="command-feedback" aria-live="polite"></p>
      <div id="parameters-content"></div>
    </section>
  `;

  const feedbackEl = container.querySelector('#parameters-feedback');
  const contentEl = container.querySelector('#parameters-content');

  function setFeedback(text, isError) {
    feedbackEl.textContent = text || '';
    feedbackEl.classList.toggle('command-feedback--error', Boolean(isError));
  }

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

  function renderEditor(groups, config) {
    const sections = groups
      .map(
        (group) => `
      <fieldset class="param-group">
        <legend>${escapeHtml(group.title)}</legend>
        ${group.fields
          .map(
            (field) => `
          <label class="param-field">
            <span class="param-field__label">${escapeHtml(field.label)}</span>
            <input
              data-key="${escapeHtml(field.key)}"
              type="number"
              step="${field.integer ? '1' : 'any'}"
              ${field.min !== undefined ? `min="${field.min}"` : ''}
              ${field.max !== undefined ? `max="${field.max}"` : ''}
              value="${escapeHtml(config[field.key] ?? '')}"
            />
            <span class="param-field__unit">${escapeHtml(field.unit || '')}</span>
            ${field.future ? '<span class="param-field__future">zukünftig</span>' : ''}
            <details class="param-info">
              <summary>Info</summary>
              <p>${escapeHtml(field.info || '')}</p>
              <p>Einheit: ${escapeHtml(field.unit || '—')}</p>
              <p>Zulässiger Bereich: ${escapeHtml(
                field.minExclusive !== undefined
                  ? `> ${field.minExclusive}${field.max !== undefined ? ` … ${field.max}` : ''}`
                  : field.min !== undefined || field.max !== undefined
                    ? `${field.min !== undefined ? field.min : '—'} … ${field.max !== undefined ? field.max : '—'}`
                    : 'endlich (Core-Validierung)'
              )}</p>
              <p>Wirkung: ${field.future ? 'noch nicht im Core ausgewertet' : 'wirkt auf Core MachineConfig'}</p>
            </details>
          </label>`
          )
          .join('')}
      </fieldset>`
      )
      .join('');

    contentEl.innerHTML = `
      <form id="param-form" class="control-form">
        ${sections}
        <div class="control-actions page-actions">
          <button type="submit">Speichern und an Controller senden</button>
          <button type="button" class="btn-back" data-route="menu">Zurück</button>
        </div>
      </form>
    `;
    contentEl.querySelector('.btn-back').addEventListener('click', () => navigate('menu'));
    contentEl.querySelector('#param-form').addEventListener('submit', async (ev) => {
      ev.preventDefault();
      const next = { ...config };
      contentEl.querySelectorAll('input[data-key]').forEach((input) => {
        const key = input.getAttribute('data-key');
        next[key] = Number(input.value);
      });
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
  return () => {};
}
