'use strict';

/**
 * TCP byte-stream reassembly for M12 frames — mirrors simulation_bridge/FrameStreamBuffer.
 */

const { decodeFrame } = require('./m12Codec');
const {
  MAGIC0,
  MAGIC1,
  MAX_FRAME_SIZE,
  MAX_PAYLOAD_SIZE,
  DecodeResult,
} = require('./protocolTypes');

const MAX_BUFFER_SIZE = 4 * MAX_FRAME_SIZE;

class FrameStreamBuffer {
  constructor() {
    this._buffer = Buffer.alloc(0);
    this._overflowed = false;
  }

  append(data) {
    if (!data || data.length === 0) {
      return;
    }
    const chunk = Buffer.isBuffer(data) ? data : Buffer.from(data);
    if (this._buffer.length + chunk.length > MAX_BUFFER_SIZE) {
      this._buffer = Buffer.alloc(0);
      this._overflowed = true;
      return;
    }
    this._buffer = Buffer.concat([this._buffer, chunk]);
  }

  /**
   * @returns {{ ok: boolean, frame?: object, decodeResult?: string }}
   */
  tryPopFrame() {
    for (;;) {
      if (this._buffer.length === 0) {
        return { ok: false };
      }

      if (this._buffer[0] !== MAGIC0 || this._buffer[1] !== MAGIC1) {
        this._resyncFrom(1);
        continue;
      }

      const decoded = decodeFrame(this._buffer);

      if (decoded.result === DecodeResult.NeedMoreData) {
        return { ok: false };
      }

      if (
        decoded.result === DecodeResult.Ok ||
        decoded.result === DecodeResult.UnknownMessageId ||
        decoded.result === DecodeResult.InvalidPayload
      ) {
        this._discardFront(decoded.bytesConsumed);
        return {
          ok: true,
          frame: decoded.frame,
          decodeResult: decoded.result,
        };
      }

      if (
        decoded.result === DecodeResult.CrcError ||
        decoded.result === DecodeResult.UnsupportedVersion ||
        decoded.result === DecodeResult.InvalidLength
      ) {
        if (this._buffer.length >= 6) {
          const frameSize = this._frameSizeFromHeader();
          if (frameSize > 0 && frameSize <= this._buffer.length) {
            this._discardFront(frameSize);
            continue;
          }
        }
        this._discardFront(1);
        continue;
      }

      this._resyncFrom(1);
    }
  }

  clear() {
    this._buffer = Buffer.alloc(0);
    this._overflowed = false;
  }

  overflowed() {
    return this._overflowed;
  }

  size() {
    return this._buffer.length;
  }

  _discardFront(count) {
    if (count >= this._buffer.length) {
      this._buffer = Buffer.alloc(0);
      return;
    }
    this._buffer = this._buffer.subarray(count);
  }

  _resyncFrom(startIndex) {
    for (let i = startIndex; i + 1 < this._buffer.length; i += 1) {
      if (this._buffer[i] === MAGIC0 && this._buffer[i + 1] === MAGIC1) {
        this._discardFront(i);
        return;
      }
    }
    this._discardFront(1);
  }

  _frameSizeFromHeader() {
    if (this._buffer.length < 6) {
      return 0;
    }
    const payloadLength = this._buffer[4] | (this._buffer[5] << 8);
    if (payloadLength > MAX_PAYLOAD_SIZE) {
      return 0;
    }
    return 6 + payloadLength + 2;
  }
}

module.exports = {
  FrameStreamBuffer,
  MAX_BUFFER_SIZE,
};
