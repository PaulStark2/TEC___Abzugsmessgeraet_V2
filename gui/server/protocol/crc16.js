'use strict';

/**
 * CRC-16/CCITT-FALSE — mirrors communication/Crc16.cpp.
 * Polynomial 0x1021, init 0xFFFF, no reflection.
 */

function crc16CcittFalse(data) {
  let crc = 0xffff;
  const bytes = Buffer.isBuffer(data) ? data : Buffer.from(data);
  for (let i = 0; i < bytes.length; i += 1) {
    crc ^= bytes[i] << 8;
    for (let bit = 0; bit < 8; bit += 1) {
      if ((crc & 0x8000) !== 0) {
        crc = ((crc << 1) ^ 0x1021) & 0xffff;
      } else {
        crc = (crc << 1) & 0xffff;
      }
    }
  }
  return crc & 0xffff;
}

module.exports = {
  crc16CcittFalse,
};
