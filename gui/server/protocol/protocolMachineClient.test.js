'use strict';

/**
 * ProtocolMachineClient unit tests against a local M12 mock TCP server.
 */

const assert = require('assert');
const net = require('net');
const { createProtocolMachineClient } = require('./protocolMachineClient');
const {
  encodeFrame,
  encodeEmptyMessage,
  encodeMoveToMessage,
  encodeAckPayload,
  encodeNackPayload,
  encodeStatusReportPayload,
  decodeFrame,
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

function startMockHost(handler) {
  return new Promise((resolve, reject) => {
    const server = net.createServer((socket) => {
      const stream = new FrameStreamBuffer();

      // Hello status
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
    });

    server.listen(0, '127.0.0.1', () => {
      const { port } = server.address();
      resolve({ server, port });
    });
    server.on('error', reject);
  });
}

async function withClient(port, fn, options = {}) {
  const client = createProtocolMachineClient({
    host: '127.0.0.1',
    port,
    ackTimeoutMs: options.ackTimeoutMs || 1000,
  });
  try {
    // Wait for hello status
    for (let i = 0; i < 50; i += 1) {
      const s = client.getStatus();
      if (s.connected && (s.x !== 0 || s.y !== 0 || s.z !== 0)) {
        break;
      }
      await sleep(20);
    }
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
    const ack = encodeFrame(
      MessageId.RspAck,
      encodeAckPayload({ refMessageId: MessageId.CmdMoveTo, resultCode: AckResultCode.Ok })
    );
    const status = encodeFrame(
      MessageId.MsgStatusReport,
      makeStatusPayload({ machineState: 1, motionState: 1, x: 10, y: 0, z: 0 })
    );
    socket.write(Buffer.concat([ack.frame, status.frame]));
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
    // Push status before ACK
    const mid = encodeFrame(
      MessageId.MsgStatusReport,
      makeStatusPayload({ machineState: 1, motionState: 1, x: 7 })
    );
    socket.write(mid.frame);
    setTimeout(() => {
      const ack = encodeFrame(
        MessageId.RspAck,
        encodeAckPayload({ refMessageId: MessageId.CmdPause, resultCode: AckResultCode.Ok })
      );
      socket.write(ack.frame);
    }, 50);
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
    const ack = encodeFrame(
      MessageId.RspAck,
      encodeAckPayload({
        refMessageId: frame.messageId,
        resultCode: AckResultCode.FaultLatched,
      })
    );
    socket.write(ack.frame);
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

async function testAckTimeout() {
  const { server, port } = await startMockHost(() => {
    // never reply
  });

  try {
    await withClient(
      port,
      async (client) => {
        const result = await client.pause();
        assert.strictEqual(result.ok, false);
        assert.strictEqual(result.error, 'Protocol ACK timeout');
      },
      { ackTimeoutMs: 200 }
    );
  } finally {
    server.close();
  }
}

async function testPneumaticAndSetSpeed() {
  const { server, port } = await startMockHost((socket, frame) => {
    if (frame.messageId === MessageId.CmdSetSpreader) {
      const bool = decodeBoolPayload(frame.payload);
      assert.strictEqual(bool.value, 1);
      const ack = encodeFrame(
        MessageId.RspAck,
        encodeAckPayload({ refMessageId: MessageId.CmdSetSpreader, resultCode: AckResultCode.Ok })
      );
      const status = encodeFrame(
        MessageId.MsgStatusReport,
        makeStatusPayload({ spreaderExtend: 1, spreaderExtended: 1 })
      );
      socket.write(Buffer.concat([ack.frame, status.frame]));
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
    // hello status was x=1; jog +X step 1 => 2
    assert.strictEqual(move.x, 2);
    const ack = encodeFrame(
      MessageId.RspAck,
      encodeAckPayload({ refMessageId: MessageId.CmdMoveTo, resultCode: AckResultCode.Ok })
    );
    socket.write(ack.frame);
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
  await sleep(50);
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
  await testAckTimeout();
  await testPneumaticAndSetSpeed();
  await testJogMapsToMoveTo();
  await testCloseStopsReconnect();
  console.log('protocolMachineClient.test.js: PASS');
}

main().catch((err) => {
  console.error(err);
  process.exit(1);
});
