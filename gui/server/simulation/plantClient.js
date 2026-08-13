'use strict';

/**
 * M15 client for the simulation plant channel (line-delimited JSON over TCP).
 *
 * This channel is simulation-only and separate from the M12 controller
 * protocol: it configures virtual sensors, never machine motion.
 */

const net = require('net');

const DEFAULT_HOST = '127.0.0.1';
const DEFAULT_PORT = 8767;
const DEFAULT_TIMEOUT_MS = 1000;

const OFFLINE_ERROR = 'Simulations-Plant-Kanal nicht erreichbar (Simulation gestoppt?)';

function createPlantClient(options = {}) {
  const host = options.host || process.env.PLANT_HOST || DEFAULT_HOST;
  const port = Number(options.port || process.env.PLANT_PORT || DEFAULT_PORT);
  const timeoutMs = Number(options.timeoutMs || DEFAULT_TIMEOUT_MS);

  /**
   * Send one command and await one response line.
   * Connection problems resolve as { ok: false, offline: true } so the UI can
   * stay open while the simulation host is down.
   * @param {object} command
   * @returns {Promise<object>}
   */
  function request(command) {
    return new Promise((resolve) => {
      const socket = net.createConnection({ host, port });
      let buffer = '';
      let settled = false;

      const finish = (result) => {
        if (settled) {
          return;
        }
        settled = true;
        socket.destroy();
        resolve(result);
      };

      socket.setTimeout(timeoutMs);
      socket.on('connect', () => {
        socket.write(`${JSON.stringify(command)}\n`);
      });

      socket.on('data', (chunk) => {
        buffer += chunk.toString('utf8');
        const newline = buffer.indexOf('\n');
        if (newline === -1) {
          return;
        }
        const line = buffer.slice(0, newline).trim();
        try {
          finish(JSON.parse(line));
        } catch {
          finish({ ok: false, error: 'Ungültige Antwort des Simulations-Plant-Kanals' });
        }
      });

      socket.on('timeout', () => {
        finish({ ok: false, offline: true, error: `${OFFLINE_ERROR} — Zeitüberschreitung` });
      });

      socket.on('error', () => {
        finish({ ok: false, offline: true, error: OFFLINE_ERROR });
      });

      socket.on('close', () => {
        finish({ ok: false, offline: true, error: OFFLINE_ERROR });
      });
    });
  }

  return {
    host,
    port,
    request,
    getState: () => request({ cmd: 'getState' }),
    setLimits: (limits) => request({ cmd: 'setLimits', ...limits }),
    triggerProbe: (pulseMs) =>
      request(pulseMs === undefined ? { cmd: 'triggerProbe' } : { cmd: 'triggerProbe', pulseMs }),
    setEStop: (active) => request({ cmd: 'setEStop', active: Boolean(active) }),
  };
}

module.exports = { createPlantClient, OFFLINE_ERROR };
