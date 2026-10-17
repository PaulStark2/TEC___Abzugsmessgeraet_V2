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
  encodeMachineConfigPayload,
  decodeMoveToPayload,
  decodeBoolPayload,
} = require('./m12Codec');
const {
  MessageId,
  AckResultCode,
  NackErrorCode,
  DecodeResult,
  NO_CURRENT_POINT_INDEX,
  HomingState,
  StatusFlag,
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

function sampleMachineConfig(overrides = {}) {
  return {
    v_approach_testpoints: 10,
    v_fine_testpoints: 2,
    v_unclamp: 1,
    v_homing: 5,
    a_testpoints: 0,
    corner_testpoints: 0,
    a_homing: 0,
    force_filter: 0,
    xy_tolerance_N: 0.5,
    xy_correction_step: 0.05,
    xy_max_correction: 2,
    insertion_buffer: 1,
    collision_approach_N: 20,
    collision_retract_N: 20,
    collision_clamp_N: 20,
    collision_unclamp_N: 20,
    collision_contact_N: 40,
    collision_measure_N: 40,
    spreader_extend_time_s: 0.5,
    spreader_retract_time_s: 0.5,
    force_tolerance_N: 0.5,
    z_correction_gain: 0.1,
    z_correction_max: 1,
    minimum_correction: 0.01,
    max_optimization_iterations: 10,
    v_search: 2,
    probe_search_distance: 10,
    probe_backoff: 1,
    home_offset_x: 0,
    home_offset_y: 0,
    home_offset_z: 0,
    tool_offset_x: 0.25,
    tool_offset_y: -0.5,
    tool_offset_z: 1,
    safe_z_buffer: 30,
    sample_timeout_s: 0.5,
    heartbeat_timeout_s: 0.3,
    xy_correct_timeout_s: 2,
    head_diameter: 0,
    measuring_head_diameter: 8,
    measuring_head_thickness: 3,
    tool_diameter: 2.5,
    tool_cylindrical_height: 15,
    probe_xy_search_margin: 2,
    insert_search_distance: 10,
    search_depth: 5,
    approach_distance: 10,
    v_search_fine: 1,
    probe_offset_reference_x: 0,
    probe_offset_reference_y: 0,
    probe_offset_reference_z: 5,
    num_fine_searches: 3,
    phase1_v_extract: 1,
    phase1_v_insert: 1,
    phase1_a: 0,
    phase1_corner: 0,
    phase2_v_extract: 2,
    phase2_v_insert: 3,
    phase2_a: 0,
    phase2_corner: 0,
    phase3_v_extract: 2,
    phase3_v_insert: 2,
    phase3_a: 0,
    phase3_corner: 0,
    v_approach_probe: 10,
    v_transfer_probe: 10,
    a_probe_transfer: 0,
    corner_probe_transfer: 0,
    a_probe_search: 0,
    a_probe_brake: 500,
    probe_center_x: 0,
    probe_center_y: 0,
    probe_center_z: 0,
    ...overrides,
  };
}

function replyMachineConfig(socket, config) {
  const payload = encodeMachineConfigPayload(config);
  assert.ok(payload, 'encodeMachineConfigPayload failed');
  const frame = encodeFrame(MessageId.MsgMachineConfig, payload);
  socket.write(frame.frame);
}

function replyMachineConfigSameWrite(socket, config) {
  const ack = encodeFrame(
    MessageId.RspAck,
    encodeAckPayload({ refMessageId: MessageId.CmdGetMachineConfig, resultCode: AckResultCode.Ok })
  );
  const payload = encodeMachineConfigPayload(config);
  assert.ok(payload, 'encodeMachineConfigPayload failed');
  const cfg = encodeFrame(MessageId.MsgMachineConfig, payload);
  socket.write(Buffer.concat([ack.frame, cfg.frame]));
}

function startMockHost(handler, helloOverrides = {}) {
  return new Promise((resolve, reject) => {
    /** @type {import('net').Socket[]} */
    const sockets = [];
    const server = net.createServer((socket) => {
      sockets.push(socket);
      const stream = new FrameStreamBuffer();

      const hello = encodeFrame(
        MessageId.MsgStatusReport,
        makeStatusPayload({ x: 1, y: 2, z: 3, ...helloOverrides })
      );
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
        sockets,
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
    if (frame.messageId === MessageId.CmdSetManualSpeed) {
      assert.strictEqual(frame.payload.length, 4);
      assert.ok(Math.abs(frame.payload.readFloatLE(0) - 20) < 1e-6);
      replyAck(socket, MessageId.CmdSetManualSpeed);
      return;
    }
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
      assert.strictEqual(speed.ok, true);
      assert.strictEqual(speed.status.manualSpeed, 20);

      const open = await client.pneumaticOpen();
      assert.ok(open.ok, open.error);
      assert.strictEqual(client.getStatus().pneumaticExtended, true);
    });
  } finally {
    server.close();
  }
}

async function testFixtureOpen() {
  const { server, port } = await startMockHost((socket, frame) => {
    if (frame.messageId === MessageId.CmdSetFixture) {
      const bool = decodeBoolPayload(frame.payload);
      assert.strictEqual(bool.value, 1);
      replyAck(socket, MessageId.CmdSetFixture);
      const status = encodeFrame(
        MessageId.MsgStatusReport,
        makeStatusPayload({ fixtureExtend: 1, fixtureExtended: 1 })
      );
      socket.write(status.frame);
    }
  });

  try {
    await withClient(port, async (client) => {
      const open = await client.fixtureOpen();
      assert.ok(open.ok, open.error);
      assert.strictEqual(client.getStatus().fixtureExtended, true);
      assert.strictEqual(client.getStatus().pneumaticExtended, false);
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

async function testMeasuredHomeLatchFromHomingComplete() {
  const { server, port, sockets } = await startMockHost(() => {});
  try {
    await withClient(port, async (client) => {
      assert.strictEqual(client.getStatus().measuredHome, null);
      const socket = sockets[0];
      socket.write(
        encodeFrame(
          MessageId.MsgStatusReport,
          makeStatusPayload({
            homingState: HomingState.HomingY,
            x: 5,
            y: 5,
            z: 5,
          })
        ).frame
      );
      await sleep(50);
      socket.write(
        encodeFrame(
          MessageId.MsgStatusReport,
          makeStatusPayload({
            homingState: HomingState.Homed,
            flags: 1 << StatusFlag.IsHomed,
            x: 2.25,
            y: 2.5,
            z: 2.75,
          })
        ).frame
      );
      await sleep(50);
      const home = client.getStatus().measuredHome;
      assert.ok(home);
      assert.ok(Math.abs(home.x - 2.25) < 1e-4);
      assert.ok(Math.abs(home.y - 2.5) < 1e-4);
      assert.ok(Math.abs(home.z - 2.75) < 1e-4);

      socket.write(
        encodeFrame(
          MessageId.MsgStatusReport,
          makeStatusPayload({
            homingState: HomingState.Homed,
            flags: 1 << StatusFlag.IsHomed,
            x: 40,
            y: 1,
            z: 8,
          })
        ).frame
      );
      await sleep(50);
      const later = client.getStatus();
      assert.ok(Math.abs(later.x - 40) < 1e-4);
      assert.ok(Math.abs(later.measuredHome.x - 2.25) < 1e-4);
      assert.ok(Math.abs(later.measuredHome.y - 2.5) < 1e-4);
    });
  } finally {
    server.close();
  }
}

async function testAlreadyHomedDoesNotLatchLivePose() {
  const { server, port } = await startMockHost(
    () => {},
    {
      homingState: HomingState.Homed,
      flags: 1 << StatusFlag.IsHomed,
      x: 40,
      y: 0,
      z: 8,
    }
  );
  try {
    await withClient(port, async (client) => {
      assert.strictEqual(client.getStatus().measuredHome, null);
      assert.ok(Math.abs(client.getStatus().x - 40) < 1e-4);
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

async function testGetMachineConfigAckThenDelayedConfig() {
  const cfg = sampleMachineConfig({ tool_offset_x: 42 });
  const { server, port } = await startMockHost((socket, frame) => {
    if (frame.messageId !== MessageId.CmdGetMachineConfig) {
      return;
    }
    replyAck(socket, MessageId.CmdGetMachineConfig);
    setTimeout(() => replyMachineConfig(socket, cfg), 80);
  });

  try {
    await withClient(port, async (client) => {
      const pending = client.getMachineConfig();
      let settled = false;
      pending.then(() => {
        settled = true;
      });
      await sleep(30);
      assert.strictEqual(settled, false, 'must not resolve after Ack without MsgMachineConfig');
      const result = await pending;
      assert.ok(result.ok, result.error);
      assert.ok(result.config);
      assert.ok(Math.abs(result.config.tool_offset_x - 42) < 1e-4);
    });
  } finally {
    server.close();
  }
}

async function testGetMachineConfigAckAndConfigSameChunk() {
  const cfg = sampleMachineConfig({ safe_z_buffer: 55 });
  const { server, port } = await startMockHost((socket, frame) => {
    if (frame.messageId !== MessageId.CmdGetMachineConfig) {
      return;
    }
    replyMachineConfigSameWrite(socket, cfg);
  });

  try {
    await withClient(port, async (client) => {
      const result = await client.getMachineConfig();
      assert.ok(result.ok, result.error);
      assert.ok(Math.abs(result.config.safe_z_buffer - 55) < 1e-4);
    });
  } finally {
    server.close();
  }
}

async function testGetMachineConfigAckAndConfigSeparateWrites() {
  const cfg = sampleMachineConfig({ measuring_head_diameter: 12 });
  const { server, port } = await startMockHost((socket, frame) => {
    if (frame.messageId !== MessageId.CmdGetMachineConfig) {
      return;
    }
    replyAck(socket, MessageId.CmdGetMachineConfig);
    setImmediate(() => replyMachineConfig(socket, cfg));
  });

  try {
    await withClient(port, async (client) => {
      const result = await client.getMachineConfig();
      assert.ok(result.ok, result.error);
      assert.ok(Math.abs(result.config.measuring_head_diameter - 12) < 1e-4);
    });
  } finally {
    server.close();
  }
}

async function testGetMachineConfigAckRejected() {
  const { server, port } = await startMockHost((socket, frame) => {
    if (frame.messageId !== MessageId.CmdGetMachineConfig) {
      return;
    }
    replyAck(socket, MessageId.CmdGetMachineConfig, AckResultCode.Rejected);
  });

  try {
    await withClient(port, async (client) => {
      const result = await client.getMachineConfig();
      assert.strictEqual(result.ok, false);
      assert.strictEqual(result.error, 'Command rejected by Core');
      assert.strictEqual(result.config, undefined);
    });
  } finally {
    server.close();
  }
}

async function testGetMachineConfigTimeoutWithoutConfig() {
  const { server, port } = await startMockHost((socket, frame) => {
    if (frame.messageId !== MessageId.CmdGetMachineConfig) {
      return;
    }
    replyAck(socket, MessageId.CmdGetMachineConfig);
  });

  try {
    await withClient(
      port,
      async (client) => {
        const result = await client.getMachineConfig();
        assert.strictEqual(result.ok, false);
        assert.strictEqual(result.error, MACHINE_UNAVAILABLE);
        assert.strictEqual(result.config, undefined);
        assert.strictEqual(client.getStatus().connected, false);
      },
      { ackTimeoutMs: 200 }
    );
  } finally {
    server.close();
  }
}

async function testGetMachineConfigDoesNotReturnStaleConfig() {
  const stale = sampleMachineConfig({ tool_offset_x: 999 });
  const fresh = sampleMachineConfig({ tool_offset_x: 42 });
  const { server, port, sockets } = await startMockHost((socket, frame) => {
    if (frame.messageId !== MessageId.CmdGetMachineConfig) {
      return;
    }
    replyAck(socket, MessageId.CmdGetMachineConfig);
    setTimeout(() => replyMachineConfig(socket, fresh), 60);
  });

  try {
    await withClient(port, async (client) => {
      replyMachineConfig(sockets[0], stale);
      await sleep(20);

      const pending = client.getMachineConfig();
      let settled = false;
      pending.then((result) => {
        settled = true;
        if (result.ok) {
          assert.ok(Math.abs(result.config.tool_offset_x - 42) < 1e-4);
          assert.ok(Math.abs(result.config.tool_offset_x - 999) > 1e-4);
        }
      });
      await sleep(30);
      assert.strictEqual(settled, false, 'stale lastMachineConfig must not satisfy pending get');
      const result = await pending;
      assert.ok(result.ok, result.error);
      assert.ok(Math.abs(result.config.tool_offset_x - 42) < 1e-4);
    });
  } finally {
    server.close();
  }
}

async function testGetMachineConfigSequentialCalls() {
  const firstCfg = sampleMachineConfig({ tool_offset_x: 1 });
  const secondCfg = sampleMachineConfig({ tool_offset_x: 2 });
  let callCount = 0;
  const { server, port } = await startMockHost((socket, frame) => {
    if (frame.messageId !== MessageId.CmdGetMachineConfig) {
      return;
    }
    callCount += 1;
    replyAck(socket, MessageId.CmdGetMachineConfig);
    const cfg = callCount === 1 ? firstCfg : secondCfg;
    setTimeout(() => replyMachineConfig(socket, cfg), 40);
  });

  try {
    await withClient(port, async (client) => {
      const first = await client.getMachineConfig();
      assert.ok(first.ok, first.error);
      assert.ok(Math.abs(first.config.tool_offset_x - 1) < 1e-4);

      const pending = client.getMachineConfig();
      let settled = false;
      pending.then((result) => {
        settled = true;
        if (result.ok) {
          assert.ok(Math.abs(result.config.tool_offset_x - 2) < 1e-4);
          assert.ok(Math.abs(result.config.tool_offset_x - 1) > 1e-4);
        }
      });
      await sleep(20);
      assert.strictEqual(settled, false, 'second call must not return first config early');
      const second = await pending;
      assert.ok(second.ok, second.error);
      assert.ok(Math.abs(second.config.tool_offset_x - 2) < 1e-4);
    });
  } finally {
    server.close();
  }
}

async function testRawAdcSurvivesStatusReport() {
  const { server, port } = await startMockHost((socket, frame) => {
    if (frame.messageId !== MessageId.CmdMoveTo) {
      return;
    }
    replyAck(socket, frame.messageId);
    const rawPayload = Buffer.alloc(7);
    rawPayload.writeUInt16LE(10, 0);
    rawPayload.writeUInt16LE(20, 2);
    rawPayload.writeUInt16LE(1861, 4);
    rawPayload[6] = 1;
    const raw = encodeFrame(MessageId.MsgRawAdc, rawPayload);
    const status = encodeFrame(MessageId.MsgStatusReport, makeStatusPayload({ x: 4 }));
    socket.write(Buffer.concat([raw.frame, status.frame]));
  });

  try {
    await withClient(port, async (client) => {
      const moved = await client.moveTo(1, 0, 0);
      assert.ok(moved.ok, moved.error);
      const status = client.getStatus();
      assert.strictEqual(status.adcValid, true);
      assert.strictEqual(status.adcX, 10);
      assert.strictEqual(status.adcY, 20);
      assert.strictEqual(status.adcZ, 1861);
      assert.strictEqual(status.x, 4);
    });
  } finally {
    server.close();
  }
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
  await testFixtureOpen();
  await testJogMapsToMoveTo();
  await testMeasuredHomeLatchFromHomingComplete();
  await testAlreadyHomedDoesNotLatchLivePose();
  await testCloseStopsReconnect();
  await testGetMachineConfigAckThenDelayedConfig();
  await testGetMachineConfigAckAndConfigSameChunk();
  await testGetMachineConfigAckAndConfigSeparateWrites();
  await testGetMachineConfigAckRejected();
  await testGetMachineConfigTimeoutWithoutConfig();
  await testGetMachineConfigDoesNotReturnStaleConfig();
  await testGetMachineConfigSequentialCalls();
  await testRawAdcSurvivesStatusReport();
  console.log('protocolMachineClient.test.js: PASS');
}

main().catch((err) => {
  console.error(err);
  process.exit(1);
});
