'use strict';

/**
 * ProtocolMachineClient unit tests against a local M12 mock TCP server.
 */

const assert = require('assert');
const net = require('net');
const {
  createProtocolMachineClient,
  MACHINE_UNAVAILABLE,
} = require('./protocolMachineClient');
const {
  encodeFrame,
  encodeAckPayload,
  encodeNackPayload,
  encodeStatusReportPayload,
  decodeMoveToPayload,
  decodeBoolPayload,
} = require('./m12Codec');
const {
  MessageId,
  AckResultCode,
  NackErrorCode,
  DecodeResult,
  NO_CURRENT_POINT_INDEX,
} = require('./protocolTypes');
const { FrameStreamBuffer } = require('./frameStreamBuffer');

function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

function makeStatusPayload(overrides = {}) {
  return encodeStatusReportPayload({
    machineState: 0,
    motionState: 0,
    processState: 0,
    x: 0,
    y: 0,
    z: 0,
    fx: 0,
    fy: 0,
    fz: 0,
    limits: 0,
    eStop: 0,
    spreaderExtend: 0,
    spreaderExtended: 0,
    fixtureExtend: 0,
    fixtureExtended: 0,
    currentPointIndex: NO_CURRENT_POINT_INDEX,
    programPointCount: 0,
    ...overrides,
  });
}

function replyAck(socket, messageId, resultCode = AckResultCode.Ok) {
  const ack = encodeFrame(
    MessageId.RspAck,
    encodeAckPayload({ refMessageId: messageId, resultCode })
  );
  socket.write(ack.frame);
}

function startMockHost(handler) {
  return new Promise((resolve, reject) => {
    /** @type {import('net').Socket[]} */
    const sockets = [];
    const server = net.createServer((socket) => {
      sockets.push(socket);
      const stream = new FrameStreamBuffer();

      const hello = encodeFrame(MessageId.MsgStatusReport, makeStatusPayload({ x: 1, y: 2, z: 3 }));
      socket.write(hello.frame);

      socket.on('data', (chunk) => {
        stream.append(chunk);
        for (;;) {
          const popped = stream.tryPopFrame();
          if (!popped.ok) {
            break;
          }
          if (popped.decodeResult === DecodeResult.Ok) {
            handler(socket, popped.frame);
          }
        }
      });

      socket.on('close', () => {
        const idx = sockets.indexOf(socket);
        if (idx >= 0) {
          sockets.splice(idx, 1);
        }
      });
    });

    server.listen(0, '127.0.0.1', () => {
      const { port } = server.address();
      resolve({
        server,
        port,
        dropClients() {
          for (const s of [...sockets]) {
            s.destroy();
          }
        },
      });
    });
    server.on('error', reject);
  });
}

async function waitUntilConnected(client, timeoutMs = 2000) {
  const start = Date.now();
  while (Date.now() - start < timeoutMs) {
    const s = client.getStatus();
    if (s.connected) {
      return s;
    }
    await sleep(20);
  }
  throw new Error('Timeout waiting for protocol sessionReady');
}

async function waitUntilDisconnected(client, timeoutMs = 2000) {
  const start = Date.now();
  while (Date.now() - start < timeoutMs) {
    if (!client.getStatus().connected) {
      return;
    }
    await sleep(20);
  }
  throw new Error('Timeout waiting for disconnect');
}

async function withClient(port, fn, options = {}) {
  const client = createProtocolMachineClient({
    host: '127.0.0.1',
    port,
    ackTimeoutMs: options.ackTimeoutMs || 1000,
  });
  try {
    await waitUntilConnected(client);
    return await fn(client);
  } finally {
    client.close();
  }
}

async function testStatusHello() {
  const { server, port } = await startMockHost(() => {});
  try {
    await withClient(port, async (client) => {
      const status = client.getStatus();
      assert.ok(status.connected);
      assert.strictEqual(status.x, 1);
      assert.strictEqual(status.y, 2);
      assert.strictEqual(status.z, 3);
    });
  } finally {
    server.close();
  }
}

async function testMoveToAck() {
  const { server, port } = await startMockHost((socket, frame) => {
    assert.strictEqual(frame.messageId, MessageId.CmdMoveTo);
    const move = decodeMoveToPayload(frame.payload);
    assert.strictEqual(move.x, 10);
    replyAck(socket, MessageId.CmdMoveTo);
    const status = encodeFrame(
      MessageId.MsgStatusReport,
      makeStatusPayload({ machineState: 1, motionState: 1, x: 10, y: 0, z: 0 })
    );
    socket.write(status.frame);
  });

  try {
    await withClient(port, async (client) => {
      const result = await client.moveTo(10, 0, 0);
      assert.ok(result.ok, result.error);
      assert.strictEqual(client.getStatus().x, 10);
    });
  } finally {
    server.close();
  }
}

async function testStatusDuringAckWait() {
  const { server, port } = await startMockHost((socket, frame) => {
    if (frame.messageId !== MessageId.CmdPause) {
      return;
    }
    const mid = encodeFrame(
      MessageId.MsgStatusReport,
      makeStatusPayload({ machineState: 1, motionState: 1, x: 7 })
    );
    socket.write(mid.frame);
    setTimeout(() => replyAck(socket, MessageId.CmdPause), 50);
  });

  try {
    await withClient(port, async (client) => {
      const result = await client.pause();
      assert.ok(result.ok, result.error);
      assert.strictEqual(client.getStatus().x, 7);
    });
  } finally {
    server.close();
  }
}

async function testNack() {
  const { server, port } = await startMockHost((socket, frame) => {
    const nack = encodeFrame(
      MessageId.RspNack,
      encodeNackPayload({
        refMessageId: frame.messageId,
        errorCode: NackErrorCode.Unsupported,
        detail: 0,
      })
    );
    socket.write(nack.frame);
  });

  try {
    await withClient(port, async (client) => {
      const result = await client.stop();
      assert.strictEqual(result.ok, false);
      assert.strictEqual(result.error, 'Unsupported command');
    });
  } finally {
    server.close();
  }
}

async function testAckRejected() {
  const { server, port } = await startMockHost((socket, frame) => {
    replyAck(socket, frame.messageId, AckResultCode.FaultLatched);
  });

  try {
    await withClient(port, async (client) => {
      const result = await client.moveTo(1, 2, 3);
      assert.strictEqual(result.ok, false);
      assert.strictEqual(result.error, 'Machine in fault; clear fault first');
    });
  } finally {
    server.close();
  }
}

async function testAckTimeoutMarksUnavailable() {
  const { server, port } = await startMockHost(() => {
    // never reply
  });

  try {
    await withClient(
      port,
      async (client) => {
        const result = await client.pause();
        assert.strictEqual(result.ok, false);
        assert.strictEqual(result.error, MACHINE_UNAVAILABLE);
        assert.strictEqual(client.getStatus().connected, false);
      },
      { ackTimeoutMs: 200 }
    );
  } finally {
    server.close();
  }
}

async function testDisconnectedCommandRejected() {
  // A) Client never becomes ready (nothing listening) → moveTo fails.
  const client = createProtocolMachineClient({
    host: '127.0.0.1',
    port: 59999,
    ackTimeoutMs: 300,
  });
  await sleep(100);
  assert.strictEqual(client.getStatus().connected, false);
  const result = await client.moveTo(1, 2, 3);
  assert.strictEqual(result.ok, false);
  assert.strictEqual(result.error, MACHINE_UNAVAILABLE);
  client.close();
}

async function testDisconnectThenMoveRejected() {
  // B) Connect → status → drop socket → connected=false → moveTo fails
  const { server, port, dropClients } = await startMockHost((socket, frame) => {
    replyAck(socket, frame.messageId);
  });

  const client = createProtocolMachineClient({
    host: '127.0.0.1',
    port,
    ackTimeoutMs: 500,
  });

  try {
    await waitUntilConnected(client);
    const before = client.getStatus();
    assert.ok(before.connected);

    dropClients();
    await waitUntilDisconnected(client);
    assert.strictEqual(client.getStatus().connected, false);

    const frozenX = client.getStatus().x;
    const result = await client.moveTo(99, 0, 0);
    assert.strictEqual(result.ok, false);
    assert.strictEqual(result.error, MACHINE_UNAVAILABLE);
    // Pose must not change locally after disconnect.
    assert.strictEqual(client.getStatus().x, frozenX);
  } finally {
    client.close();
    server.close();
  }
}

async function testReconnectAllowsCommandsAgain() {
  // C) Drop → fail → host still listening → reconnect → status → command ok
  const { server, port, dropClients } = await startMockHost((socket, frame) => {
    replyAck(socket, frame.messageId);
    if (frame.messageId === MessageId.CmdMoveTo) {
      const move = decodeMoveToPayload(frame.payload);
      const status = encodeFrame(
        MessageId.MsgStatusReport,
        makeStatusPayload({ x: move.x, y: move.y, z: move.z, machineState: 1, motionState: 1 })
      );
      socket.write(status.frame);
    }
  });

  const client = createProtocolMachineClient({
    host: '127.0.0.1',
    port,
    ackTimeoutMs: 500,
  });

  try {
    await waitUntilConnected(client);
    dropClients();
    await waitUntilDisconnected(client);

    const failed = await client.moveTo(5, 0, 0);
    assert.strictEqual(failed.ok, false);
    assert.strictEqual(failed.error, MACHINE_UNAVAILABLE);

    // Server still listening — client reconnects automatically.
    await waitUntilConnected(client, 5000);
    assert.ok(client.getStatus().connected);

    const ok = await client.moveTo(12, 0, 0);
    assert.ok(ok.ok, ok.error);
  } finally {
    client.close();
    server.close();
  }
}

async function testPneumaticAndSetSpeed() {
  const { server, port } = await startMockHost((socket, frame) => {
    if (frame.messageId === MessageId.CmdSetSpreader) {
      const bool = decodeBoolPayload(frame.payload);
      assert.strictEqual(bool.value, 1);
      replyAck(socket, MessageId.CmdSetSpreader);
      const status = encodeFrame(
        MessageId.MsgStatusReport,
        makeStatusPayload({ spreaderExtend: 1, spreaderExtended: 1 })
      );
      socket.write(status.frame);
    }
  });

  try {
    await withClient(port, async (client) => {
      const speed = await client.setSpeed(20);
      assert.strictEqual(speed.ok, false);
      assert.strictEqual(speed.error, 'Not supported over protocol');

      const open = await client.pneumaticOpen();
      assert.ok(open.ok, open.error);
      assert.strictEqual(client.getStatus().pneumaticExtended, true);
    });
  } finally {
    server.close();
  }
}

async function testJogMapsToMoveTo() {
  const { server, port } = await startMockHost((socket, frame) => {
    assert.strictEqual(frame.messageId, MessageId.CmdMoveTo);
    const move = decodeMoveToPayload(frame.payload);
    assert.strictEqual(move.x, 2);
    replyAck(socket, MessageId.CmdMoveTo);
  });

  try {
    await withClient(port, async (client) => {
      const result = await client.jog('X', 1, 1);
      assert.ok(result.ok, result.error);
    });
  } finally {
    server.close();
  }
}

async function testCloseStopsReconnect() {
  const { server, port } = await startMockHost(() => {});
  const client = createProtocolMachineClient({ host: '127.0.0.1', port, ackTimeoutMs: 500 });
  await waitUntilConnected(client);
  client.close();
  await sleep(100);
  assert.strictEqual(client.getStatus().connected, false);
  server.close();
}

async function main() {
  await testStatusHello();
  await testMoveToAck();
  await testStatusDuringAckWait();
  await testNack();
  await testAckRejected();
  await testAckTimeoutMarksUnavailable();
  await testDisconnectedCommandRejected();
  await testDisconnectThenMoveRejected();
  await testReconnectAllowsCommandsAgain();
  await testPneumaticAndSetSpeed();
  await testJogMapsToMoveTo();
  await testCloseStopsReconnect();
  console.log('protocolMachineClient.test.js: PASS');
}

main().catch((err) => {
  console.error(err);
  process.exit(1);
});
