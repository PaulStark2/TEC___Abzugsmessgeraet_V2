'use strict';

const { spawn } = require('child_process');

const SHUTDOWN_BLOCKED_MESSAGE = 'Zuerst die Prüfung beenden.';
const SHUTDOWN_UNAVAILABLE_MESSAGE = 'Herunterfahren ist an diesem Gerät nicht eingerichtet.';
const SHUTDOWN_FAILED_MESSAGE = 'Herunterfahren konnte nicht gestartet werden.';

/**
 * @param {{ allowSystemPower?: boolean, procedureState?: string|null }} input
 * @returns {{ ok: true } | { ok: false, status: number, error: string }}
 */
function evaluateShutdown(input) {
  const procedureState = input && input.procedureState;
  if (procedureState === 'running' || procedureState === 'paused') {
    return { ok: false, status: 409, error: SHUTDOWN_BLOCKED_MESSAGE };
  }
  if (!input || input.allowSystemPower !== true) {
    return { ok: false, status: 403, error: SHUTDOWN_UNAVAILABLE_MESSAGE };
  }
  return { ok: true };
}

function requestShutdown() {
  return new Promise((resolve, reject) => {
    const child = spawn('sudo', ['-n', '/sbin/shutdown', '-h', 'now'], {
      stdio: 'ignore',
    });
    child.on('error', reject);
    child.on('close', (code) => {
      if (code === 0) {
        resolve();
        return;
      }
      reject(new Error('shutdown failed'));
    });
  });
}

module.exports = {
  SHUTDOWN_BLOCKED_MESSAGE,
  SHUTDOWN_UNAVAILABLE_MESSAGE,
  SHUTDOWN_FAILED_MESSAGE,
  evaluateShutdown,
  requestShutdown,
};
