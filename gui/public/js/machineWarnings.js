'use strict';

export const LIMIT_MESSAGES = Object.freeze([
  'X-Min Endanschlag aktiv',
  'X-Max Endanschlag aktiv',
  'Y-Min Endanschlag aktiv',
  'Y-Max Endanschlag aktiv',
  'Z-Min Endanschlag aktiv',
  'Z-Max Endanschlag aktiv',
]);

/**
 * @param {number} limitsBitmask
 * @returns {string[]}
 */
export function formatLimitWarnings(limitsBitmask) {
  if (typeof limitsBitmask !== 'number' || !Number.isFinite(limitsBitmask) || limitsBitmask === 0) {
    return [];
  }
  const messages = [];
  for (let bit = 0; bit < LIMIT_MESSAGES.length; bit += 1) {
    if ((limitsBitmask & (1 << bit)) !== 0) {
      messages.push(LIMIT_MESSAGES[bit]);
    }
  }
  return messages;
}
