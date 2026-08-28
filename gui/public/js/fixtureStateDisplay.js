'use strict';

/**
 * Shared Offen/Geschlossen display for Spannsystem (HMI + Sim-UI).
 * Open is an alarm (red); closed uses the default text style.
 *
 * @param {HTMLElement | null} el
 * @param {boolean | undefined | null} isOpen
 */
export function applyFixtureStateDisplay(el, isOpen) {
  if (!el) {
    return;
  }
  if (isOpen === true) {
    el.textContent = 'Offen';
    el.classList.add('status-value--alarm');
  } else if (isOpen === false) {
    el.textContent = 'Geschlossen';
    el.classList.remove('status-value--alarm');
  } else {
    el.textContent = '—';
    el.classList.remove('status-value--alarm');
  }
}
