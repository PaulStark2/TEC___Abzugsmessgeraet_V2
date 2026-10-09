'use strict';

/**
 * M12 transport factory: TCP (simulation) or UART (STM32 bring-up / Pi).
 * Returns a socket-like object: write, destroy, destroyed, writable, on(data|error|end|close).
 */

const net = require('net');

function createTcpConnection({ host, port }, onConnect) {
  return net.createConnection({ host, port }, onConnect);
}

function createUartConnection({ path, baudRate }, onConnect) {
  let SerialPort;
  try {
    ({ SerialPort } = require('serialport'));
  } catch (err) {
    throw new Error(
      'PROTOCOL_TRANSPORT=uart requires npm package "serialport". From gui/: npm install serialport'
    );
  }

  const port = new SerialPort({
    path,
    baudRate: baudRate || 115200,
    autoOpen: false,
  });

  const facade = {
    write(buf, cb) {
      return port.write(buf, cb);
    },
    destroy() {
      try {
        port.removeAllListeners('data');
        if (port.isOpen) {
          port.close(() => {
            try {
              port.removeAllListeners();
            } catch {
              // ignore
            }
          });
        } else {
          port.removeAllListeners();
        }
      } catch {
        // ignore
      }
    },
    get destroyed() {
      return !port.isOpen;
    },
    get writable() {
      return Boolean(port.isOpen);
    },
    on(ev, fn) {
      if (ev === 'data') {
        port.on('data', fn);
      } else if (ev === 'error') {
        port.on('error', fn);
      } else if (ev === 'close' || ev === 'end') {
        port.on('close', fn);
      }
      return facade;
    },
  };

  port.open((err) => {
    if (err) {
      // Defer so ProtocolMachineClient can attach handlers first.
      setImmediate(() => {
        if (typeof port.emit === 'function') {
          port.emit('error', err);
        }
      });
      return;
    }
    onConnect();
  });

  return facade;
}

/**
 * @param {{ transport?: string, host?: string, port?: number, serialPath?: string, baudRate?: number }} options
 * @param {() => void} onConnect
 */
function createProtocolConnection(options, onConnect) {
  const transport = String(
    options.transport || process.env.PROTOCOL_TRANSPORT || 'tcp'
  ).toLowerCase();

  if (transport === 'uart' || transport === 'serial') {
    const path = options.serialPath || process.env.PROTOCOL_SERIAL_PATH;
    if (!path) {
      throw new Error('PROTOCOL_SERIAL_PATH is required when PROTOCOL_TRANSPORT=uart (e.g. COM5)');
    }
    const baudRate = Number(options.baudRate || process.env.PROTOCOL_SERIAL_BAUD || 115200);
    return createUartConnection({ path, baudRate }, onConnect);
  }

  const host = options.host || process.env.PROTOCOL_HOST || '127.0.0.1';
  const port = Number(options.port || process.env.PROTOCOL_PORT || 8766);
  return createTcpConnection({ host, port }, onConnect);
}

module.exports = {
  createProtocolConnection,
};
