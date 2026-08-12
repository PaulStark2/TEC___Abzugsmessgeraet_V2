'use strict';

/**
 * SimMachineClient — TCP bridge to C++ simulation_interactive (M8.5 / G3.5).
 *
 * Temporary pre-protocol transport (newline JSON). NOT the future M12/M13 protocol.
 * Forwards commands; caches status from the Core host. NO motion integration in JS.
 */

const net = require('net');

function createSimMachineClient(options = {}) {
  const host = options.host || process.env.SIM_HOST || '127.0.0.1';
  const port = Number(options.port || process.env.SIM_PORT || 8765);

  let status = {
    machineState: 'Idle',
    motionState: 'Idle',
    x: 0,
    y: 0,
    z: 0,
  };

  let socket = null;
  let buffer = '';
  let connected = false;
  let reconnectTimer = null;

  function getStatus() {
    return { ...status };
  }

  function applyStatusPayload(msg) {
    if (!msg || msg.type !== 'status') {
      return;
    }
    // Cache Core pose/state only — do not integrate motion here.
    status = {
      machineState: String(msg.machineState ?? status.machineState),
      motionState: String(msg.motionState ?? status.motionState),
      x: Number(msg.x),
      y: Number(msg.y),
      z: Number(msg.z),
    };
  }

  function sendLine(obj) {
    if (!socket || !connected) {
      return { ok: false, error: 'Not connected to simulation_interactive' };
    }
    try {
      socket.write(`${JSON.stringify(obj)}\n`);
      return { ok: true, status: getStatus() };
    } catch (err) {
      return { ok: false, error: err.message || String(err) };
    }
  }

  function scheduleReconnect() {
    if (reconnectTimer) {
      return;
    }
    reconnectTimer = setTimeout(() => {
      reconnectTimer = null;
      connect();
    }, 2000);
  }

  function connect() {
    if (socket) {
      socket.destroy();
      socket = null;
    }
    connected = false;
    buffer = '';

    const s = net.createConnection({ host, port }, () => {
      connected = true;
      console.log(`SimMachineClient connected to ${host}:${port}`);
    });

    socket = s;

    s.setEncoding('utf8');
    s.on('data', (chunk) => {
      buffer += chunk;
      for (;;) {
        const nl = buffer.indexOf('\n');
        if (nl < 0) {
          break;
        }
        const line = buffer.slice(0, nl).replace(/\r$/, '');
        buffer = buffer.slice(nl + 1);
        if (!line) {
          continue;
        }
        try {
          applyStatusPayload(JSON.parse(line));
        } catch {
          // ignore malformed lines
        }
      }
    });

    s.on('error', (err) => {
      console.error(`SimMachineClient error: ${err.message}`);
    });

    s.on('close', () => {
      connected = false;
      socket = null;
      console.log('SimMachineClient disconnected; reconnecting…');
      scheduleReconnect();
    });
  }

  function moveTo(x, y, z) {
    const nx = Number(x);
    const ny = Number(y);
    const nz = Number(z);
    if (![nx, ny, nz].every(Number.isFinite)) {
      return { ok: false, error: 'moveTo requires finite x, y, z' };
    }
    return sendLine({ type: 'command', command: 'moveTo', x: nx, y: ny, z: nz });
  }

  function pause() {
    return sendLine({ type: 'command', command: 'pause' });
  }

  function resume() {
    return sendLine({ type: 'command', command: 'resume' });
  }

  function stop() {
    return sendLine({ type: 'command', command: 'stop' });
  }

  // No tick(): Core host advances Machine; JS must not simulate pose.
  function close() {
    if (reconnectTimer) {
      clearTimeout(reconnectTimer);
      reconnectTimer = null;
    }
    if (socket) {
      socket.destroy();
      socket = null;
    }
    connected = false;
  }

  connect();

  return {
    getStatus,
    moveTo,
    pause,
    resume,
    stop,
    close,
  };
}

module.exports = {
  createSimMachineClient,
};
