'use strict';

import { sendCommand } from '../api.js';

/**
 * G3 movement controls for #manual. STOP is global in status bar.
 * @param {HTMLElement} container
 * @returns {() => void} cleanup
 */
export function mountControlPanel(container) {
  container.innerHTML = `
    <p class="placeholder-note">
      Bewegungssteuerung (M5). Soft-STOP ist in der Statusleiste (kein Hardware-E-Stop).
      Bei MACHINE_CLIENT=sim steuert die HMI den C++ Core.
    </p>
    <form id="move-form" class="control-form">
      <label>
        Ziel X
        <input id="target-x" name="x" type="number" step="any" value="10" required />
      </label>
      <label>
        Ziel Y
        <input id="target-y" name="y" type="number" step="any" value="0" required />
      </label>
      <label>
        Ziel Z
        <input id="target-z" name="z" type="number" step="any" value="0" required />
      </label>
      <button type="submit" id="btn-moveto">Move To</button>
    </form>
    <div class="control-actions">
      <button type="button" id="btn-pause">Pause</button>
      <button type="button" id="btn-resume">Resume</button>
    </div>
    <p id="command-feedback" class="command-feedback" aria-live="polite"></p>
  `;

  const form = container.querySelector('#move-form');
  const pauseBtn = container.querySelector('#btn-pause');
  const resumeBtn = container.querySelector('#btn-resume');
  const feedbackEl = container.querySelector('#command-feedback');

  function setFeedback(text, isError) {
    if (!feedbackEl) return;
    feedbackEl.textContent = text || '';
    feedbackEl.classList.toggle('command-feedback--error', Boolean(isError));
  }

  const onSubmit = async (event) => {
    event.preventDefault();
    const x = Number(container.querySelector('#target-x').value);
    const y = Number(container.querySelector('#target-y').value);
    const z = Number(container.querySelector('#target-z').value);
    try {
      await sendCommand({ command: 'moveTo', x, y, z });
      setFeedback(`moveTo(${x}, ${y}, ${z}) gesendet`, false);
    } catch (err) {
      setFeedback(err.message || String(err), true);
    }
  };

  const onPause = async () => {
    try {
      await sendCommand({ command: 'pause' });
      setFeedback('pause gesendet', false);
    } catch (err) {
      setFeedback(err.message || String(err), true);
    }
  };

  const onResume = async () => {
    try {
      await sendCommand({ command: 'resume' });
      setFeedback('resume gesendet', false);
    } catch (err) {
      setFeedback(err.message || String(err), true);
    }
  };

  form.addEventListener('submit', onSubmit);
  pauseBtn.addEventListener('click', onPause);
  resumeBtn.addEventListener('click', onResume);

  return () => {
    form.removeEventListener('submit', onSubmit);
    pauseBtn.removeEventListener('click', onPause);
    resumeBtn.removeEventListener('click', onResume);
  };
}
