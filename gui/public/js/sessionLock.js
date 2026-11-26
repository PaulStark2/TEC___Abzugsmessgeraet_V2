'use strict';

import { fetchSession, loginSession, logoutSession, shutdownSystem } from './api.js';

/**
 * Full-screen sign-in. The browser stays open; logout only returns here.
 * @param {() => void} onReady called once, after a valid session
 */
export function installSessionLock(onReady) {
  const root = document.getElementById('session-lock');
  const form = document.getElementById('session-login-form');
  const userInput = document.getElementById('session-username');
  const passInput = document.getElementById('session-password');
  const errorEl = document.getElementById('session-error');
  const confirmBox = document.getElementById('session-shutdown-confirm');
  const shutdownBtn = document.getElementById('session-shutdown');
  const shutdownYes = document.getElementById('session-shutdown-yes');
  const shutdownNo = document.getElementById('session-shutdown-no');
  const logoutBtn = document.getElementById('btn-logout');
  let ready = false;

  function showError(message) {
    errorEl.textContent = message || '';
  }

  function enterApp() {
    if (ready) {
      return;
    }
    ready = true;
    root.hidden = true;
    onReady();
  }

  form.addEventListener('submit', async (event) => {
    event.preventDefault();
    showError('');
    try {
      const result = await loginSession(userInput.value, passInput.value);
      if (!result.ok) {
        showError(result.error || 'Anmeldung fehlgeschlagen');
        passInput.value = '';
        passInput.focus();
        return;
      }
      enterApp();
    } catch (err) {
      showError(err.message || 'Anmeldung fehlgeschlagen');
    }
  });

  shutdownBtn.addEventListener('click', () => {
    showError('');
    confirmBox.hidden = false;
  });

  shutdownNo.addEventListener('click', () => {
    confirmBox.hidden = true;
  });

  shutdownYes.addEventListener('click', async () => {
    shutdownYes.disabled = true;
    showError('');
    try {
      const result = await shutdownSystem();
      if (!result.ok) {
        showError(result.error || 'Herunterfahren konnte nicht gestartet werden.');
        shutdownYes.disabled = false;
        return;
      }
      showError('Das Gerät fährt herunter.');
    } catch (err) {
      showError(err.message || 'Herunterfahren konnte nicht gestartet werden.');
      shutdownYes.disabled = false;
    }
  });

  if (logoutBtn) {
    logoutBtn.addEventListener('click', async () => {
      try {
        await logoutSession();
      } catch {
        // reload still shows the lock if the session is already gone
      }
      window.location.reload();
    });
  }

  fetchSession()
    .then((session) => {
      if (session && session.authenticated) {
        enterApp();
        return;
      }
      root.hidden = false;
      userInput.focus();
    })
    .catch(() => {
      root.hidden = false;
      showError('Die Anmeldung ist gerade nicht erreichbar.');
    });
}
