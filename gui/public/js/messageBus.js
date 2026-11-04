'use strict';

/**
 * Client helper: push HMI messages into the server store (and optionally local feedback).
 * See docs/HMI_Messages.md.
 */

/**
 * @param {{ severity?: string, code?: string, title?: string, detail?: string, sticky?: boolean }} msg
 */
export async function reportMessage(msg) {
  const body = {
    severity: msg.severity || 'error',
    code: msg.code || 'Gui.CommandRejected',
    title: msg.title || undefined,
    detail: msg.detail || msg.title || undefined,
    sticky: msg.sticky,
    source: 'gui',
  };
  try {
    const response = await fetch('/api/messages', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body),
    });
    return await response.json();
  } catch (err) {
    return { ok: false, error: err.message || String(err) };
  }
}

/**
 * Report a rejected command with German title.
 * @param {string} title
 * @param {string} [detail]
 */
export function reportCommandError(title, detail) {
  return reportMessage({
    severity: 'error',
    code: 'Gui.CommandRejected',
    title: title || 'Befehl abgelehnt',
    detail: detail || title || 'Befehl abgelehnt',
  });
}

export async function fetchMessages(opts = {}) {
  const q = new URLSearchParams();
  if (opts.active) q.set('active', '1');
  if (opts.limit) q.set('limit', String(opts.limit));
  const response = await fetch(`/api/messages?${q.toString()}`);
  return response.json();
}

export async function ackMessage(id) {
  const response = await fetch('/api/messages/ack', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ id }),
  });
  return response.json();
}

export async function ackAllMessages() {
  const response = await fetch('/api/messages/ack-all', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: '{}',
  });
  return response.json();
}
