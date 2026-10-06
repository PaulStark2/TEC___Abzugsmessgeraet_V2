'use strict';

import { sendCommand, fetchCalibration, fetchMachineConfig, fetchMachineStatus, submitForceCalibration, selectForceCurve, setCollisionGuard } from '../api.js';
import { runWithHomingGate } from '../homingGate.js';
import { mountJogPanel } from '../components/jogPanel.js';

/** Defaults. The parameter page overrides count, step and measuring range. */
export const FORCE_CALIB_SCHEDULE = {
  pointsPerDirection: 5,
  stepG: 500,
  windowG: 100,
  zClearanceMm: 5,
  rangeN: 30,
};

export function scheduleFromConfig(cfg) {
  const points = Number(cfg && cfg.force_calib_points);
  const step = Number(cfg && cfg.force_calib_step_g);
  const range = Number(cfg && cfg.force_range_N);
  return {
    pointsPerDirection: Number.isInteger(points) && points >= 1 && points <= 16 ? points : FORCE_CALIB_SCHEDULE.pointsPerDirection,
    stepG: Number.isFinite(step) && step > 200 ? step : FORCE_CALIB_SCHEDULE.stepG,
    windowG: FORCE_CALIB_SCHEDULE.windowG,
    zClearanceMm: FORCE_CALIB_SCHEDULE.zClearanceMm,
    rangeN: Number.isFinite(range) && range > 0 ? range : FORCE_CALIB_SCHEDULE.rangeN,
  };
}

const G0 = 9.80665;
const SOURCE = { auto: 0, linear: 1, quadratic: 2, external: 3 };
const DIR = { tension: 0, compression: 1, zero: 2 };

const REASON = {
  0: 'Die Kurve ist übernommen.',
  1: 'Zu wenige Messpunkte.',
  2: 'Eine Masse liegt außerhalb des Fensters von ±100 g um die Vorgabe.',
  3: 'Die Kurve kehrt die Kraftrichtung im ADC-Bereich um und wird nicht verwendet.',
  4: 'Der größte Restfehler liegt über 1 % der Messspanne.',
  5: 'Es ist noch kein Punktesatz aus dem Wizard gespeichert.',
  6: 'Die Koeffizienten sind ungültig.',
  7: 'Die Punkte liegen so, dass keine Ausgleichsrechnung möglich ist.',
  8: 'Ein Messpunkt ist ungültig.',
};

function directionName(code) {
  if (code === DIR.tension || code === 'tension') return 'tension';
  if (code === DIR.compression || code === 'compression') return 'compression';
  return 'zero';
}

function evalCurve(beta0, beta1, beta2, adc) {
  return beta0 + beta1 * adc + beta2 * adc * adc;
}

function sensorGrams(newton) {
  return (newton / G0) * 1000;
}

function loadSteps(schedule) {
  const plan = schedule || FORCE_CALIB_SCHEDULE;
  const steps = [];
  for (let i = 1; i <= plan.pointsPerDirection; i += 1) {
    steps.push({ direction: 'tension', nominalG: i * plan.stepG });
  }
  for (let i = 1; i <= plan.pointsPerDirection; i += 1) {
    steps.push({ direction: 'compression', nominalG: i * plan.stepG });
  }
  return steps;
}

function fmtNum(value, digits) {
  const n = Number(value);
  if (!Number.isFinite(n)) return '—';
  const abs = Math.abs(n);
  if (abs !== 0 && (abs < 0.0001 || abs >= 10000)) return n.toExponential(3);
  return n.toFixed(digits == null ? 4 : digits);
}

function pointCaption(point) {
  const adc = Number(point.adcNet);
  const force = Number(point.referenceForceN);
  return `x ${Number.isFinite(adc) ? Math.round(adc) : '—'}, y ${fmtNum(force, 2)} N`;
}

function chartSvg(active, alternate, points, showLabels) {
  const usable = (points || []).filter((point) => Number.isFinite(Number(point.referenceForceN)) && Number.isFinite(Number(point.adcNet)));
  const xs = usable.map((point) => Number(point.adcNet));
  const samples = [];
  const lo = xs.length ? Math.min(...xs) - 50 : 0;
  const hi = xs.length ? Math.max(...xs) + 50 : 4095;
  if (active) {
    for (let i = 0; i <= 40; i += 1) {
      const adc = lo + ((hi - lo) * i) / 40;
      samples.push(evalCurve(active.beta0, active.beta1, active.beta2 || 0, adc));
    }
  }
  const ys = samples.concat(usable.map((point) => Number(point.referenceForceN)));
  if (ys.length < 2) {
    return '<p class="placeholder-note">Noch keine Kennlinie.</p>';
  }
  const dataMin = Math.min(...ys);
  const dataMax = Math.max(...ys);
  const yPad = Math.max((Math.max(dataMax, 0) - Math.min(dataMin, 0)) * 0.08, 0.5);
  const yLo = Math.min(dataMin, 0) - yPad;
  const yHi = Math.max(dataMax, 0) + yPad;
  const ySpan = yHi - yLo || 1;
  const xSpan = hi - lo || 1;
  const w = 520;
  const h = 320;
  const left = 72;
  const right = 18;
  const top = 28;
  const bottom = 36;
  const mapX = (adc) => left + ((adc - lo) / xSpan) * (w - left - right);
  const mapY = (force) => top + (1 - (force - yLo) / ySpan) * (h - top - bottom);
  const axisY = mapY(0);
  const xTicks = [0, 0.5, 1].map((t) => {
    const adc = lo + t * xSpan;
    const x = mapX(adc);
    const labelY = axisY + 14 > h - 6 ? axisY - 6 : axisY + 14;
    return `<line x1="${x.toFixed(1)}" y1="${axisY.toFixed(1)}" x2="${x.toFixed(1)}" y2="${(axisY + 4).toFixed(1)}" stroke="#888" />
      <text x="${x.toFixed(1)}" y="${labelY.toFixed(1)}" text-anchor="middle" font-size="10" fill="#bbb">${Math.round(adc)}</text>`;
  }).join('');
  const yTickValues = [];
  [Math.min(dataMin, 0), 0, Math.max(dataMax, 0)].forEach((force) => {
    if (!yTickValues.some((value) => Math.abs(value - force) < 1e-6)) yTickValues.push(force);
  });
  const yTicks = yTickValues.map((force) => {
    const y = mapY(force);
    return `<line x1="${left - 4}" y1="${y.toFixed(1)}" x2="${left}" y2="${y.toFixed(1)}" stroke="#888" />
      <text x="${left - 6}" y="${(y + 3).toFixed(1)}" text-anchor="end" font-size="10" fill="#bbb">${fmtNum(force, 2)}</text>`;
  }).join('');
  const line = (model, dashed) => {
    if (!model) return '';
    const pts = [];
    for (let i = 0; i <= 40; i += 1) {
      const adc = lo + ((hi - lo) * i) / 40;
      pts.push(`${mapX(adc).toFixed(1)},${mapY(evalCurve(model.beta0, model.beta1, model.beta2 || 0, adc)).toFixed(1)}`);
    }
    return `<polyline fill="none" stroke="${dashed ? '#888' : '#0b6'}" stroke-width="2" ${dashed ? 'stroke-dasharray="5 4"' : ''} points="${pts.join(' ')}" />`;
  };
  const dots = usable.map((point) => {
    const tension = directionName(point.direction) === 'tension';
    const x = mapX(Number(point.adcNet));
    const y = mapY(Number(point.referenceForceN));
    const label = showLabels
      ? `<text x="${x.toFixed(1)}" y="${(y - 8).toFixed(1)}" text-anchor="middle" font-size="9" fill="#ddd">${pointCaption(point)}</text>`
      : '';
    return `<circle cx="${x.toFixed(1)}" cy="${y.toFixed(1)}" r="4" fill="${tension ? '#c45' : directionName(point.direction) === 'zero' ? '#888' : '#246'}" />${label}`;
  }).join('');
  const beta0 = active ? Number(active.beta0) : 0;
  const beta1 = active ? Number(active.beta1) : 0;
  const beta2 = active ? Number(active.beta2) || 0 : 0;
  return `<svg class="force-chart" viewBox="0 0 ${w} ${h}" width="100%" role="img" aria-label="Kraftkennlinie. x ist der ADC-Zählwert, y ist die Sensorkraft in Newton. Die waagerechte Achse liegt bei y = 0."
    data-lo="${lo}" data-hi="${hi}" data-ylo="${yLo}" data-yhi="${yHi}"
    data-left="${left}" data-right="${right}" data-top="${top}" data-bottom="${bottom}"
    data-w="${w}" data-h="${h}" data-b0="${beta0}" data-b1="${beta1}" data-b2="${beta2}" data-active="${active ? '1' : '0'}">
    <line x1="${left}" y1="${top}" x2="${left}" y2="${h - bottom}" stroke="#666" />
    <line x1="${left}" y1="${axisY.toFixed(1)}" x2="${w - right}" y2="${axisY.toFixed(1)}" stroke="#ddd" />
    ${xTicks}
    ${yTicks}
    ${line(alternate, true)}
    ${line(active, false)}
    ${dots}
    <line id="force-cross-v" x1="0" y1="0" x2="0" y2="0" stroke="#9cf" stroke-dasharray="4 3" visibility="hidden" pointer-events="none" />
    <line id="force-cross-h" x1="0" y1="0" x2="0" y2="0" stroke="#9cf" stroke-dasharray="4 3" visibility="hidden" pointer-events="none" />
    <text id="force-cross-label" visibility="hidden" font-size="11" fill="#fff" pointer-events="none"></text>
    <rect id="force-chart-hit" x="${left}" y="${top}" width="${w - left - right}" height="${h - top - bottom}" fill="transparent" />
    <text x="${(left + (w - right)) / 2}" y="${h - 8}" text-anchor="middle" font-size="12" fill="#ddd">x: ADC-Zählwert</text>
    <text transform="translate(16 ${(top + (h - bottom)) / 2}) rotate(-90)" text-anchor="middle" font-size="12" fill="#ddd">y: F Sensor [N]</text>
  </svg>`;
}

function bindForceCharts(root) {
  if (!root) return;
  root.querySelectorAll('svg.force-chart').forEach((svg) => {
    if (svg.dataset.bound === '1' || svg.dataset.active !== '1') return;
    svg.dataset.bound = '1';
    const hit = svg.querySelector('#force-chart-hit');
    const vLine = svg.querySelector('#force-cross-v');
    const hLine = svg.querySelector('#force-cross-h');
    const label = svg.querySelector('#force-cross-label');
    if (!hit || !vLine || !hLine || !label) return;
    const lo = Number(svg.dataset.lo);
    const hi = Number(svg.dataset.hi);
    const yLo = Number(svg.dataset.ylo);
    const yHi = Number(svg.dataset.yhi);
    const left = Number(svg.dataset.left);
    const right = Number(svg.dataset.right);
    const top = Number(svg.dataset.top);
    const bottom = Number(svg.dataset.bottom);
    const w = Number(svg.dataset.w);
    const h = Number(svg.dataset.h);
    const beta0 = Number(svg.dataset.b0);
    const beta1 = Number(svg.dataset.b1);
    const beta2 = Number(svg.dataset.b2);
    const plotW = w - left - right;
    const plotH = h - top - bottom;
    const mapX = (adc) => left + ((adc - lo) / (hi - lo || 1)) * plotW;
    const mapY = (force) => top + (1 - (force - yLo) / (yHi - yLo || 1)) * plotH;
    const hide = () => {
      vLine.setAttribute('visibility', 'hidden');
      hLine.setAttribute('visibility', 'hidden');
      label.setAttribute('visibility', 'hidden');
    };
    hit.addEventListener('pointermove', (event) => {
      const rect = svg.getBoundingClientRect();
      if (!rect.width || !rect.height) return;
      const px = ((event.clientX - rect.left) / rect.width) * w;
      const adc = lo + ((px - left) / plotW) * (hi - lo);
      const clamped = Math.min(hi, Math.max(lo, adc));
      const force = evalCurve(beta0, beta1, beta2, clamped);
      const x = mapX(clamped);
      const y = mapY(force);
      const axisY = mapY(0);
      vLine.setAttribute('x1', x.toFixed(1));
      vLine.setAttribute('x2', x.toFixed(1));
      vLine.setAttribute('y1', y.toFixed(1));
      vLine.setAttribute('y2', axisY.toFixed(1));
      hLine.setAttribute('x1', String(left));
      hLine.setAttribute('x2', x.toFixed(1));
      hLine.setAttribute('y1', y.toFixed(1));
      hLine.setAttribute('y2', y.toFixed(1));
      vLine.setAttribute('visibility', 'visible');
      hLine.setAttribute('visibility', 'visible');
      const text = `x ${Math.round(clamped)}, y ${fmtNum(force, 2)} N`;
      label.textContent = text;
      const labelX = Math.min(x + 8, w - right - 120);
      const labelY = Math.max(top + 12, y - 8);
      label.setAttribute('x', labelX.toFixed(1));
      label.setAttribute('y', labelY.toFixed(1));
      label.setAttribute('visibility', 'visible');
    });
    hit.addEventListener('pointerleave', hide);
  });
}

function equationText(model) {
  if (!model || !Number.isFinite(Number(model.beta0))) return 'Keine Gleichung gespeichert.';
  const beta2 = Number(model.beta2) || 0;
  let text = `F_sensor [N] = ${fmtNum(model.beta0, 6)} + ${fmtNum(model.beta1, 6)} · adc`;
  if (beta2 !== 0) text += ` + ${fmtNum(beta2, 6)} · adc²`;
  return text;
}

function controlsMarkup(jogEnabled) {
  if (!jogEnabled) {
    return `<p id="force-live" class="force-wizard__live"></p>`;
  }
  return `
    <p id="force-live" class="force-wizard__live"></p>
    <div class="force-wizard__dock">
      <div id="force-jog"></div>
      <div class="force-wizard__tools">
        <button type="button" id="btn-spreader-open">Spreizer auf</button>
        <button type="button" id="btn-spreader-close">Spreizer zu</button>
        <button type="button" id="btn-force-stop" class="force-wizard__stop">STOP</button>
      </div>
    </div>
  `;
}

function sourceLabel(source) {
  if (source === 'linear' || source === SOURCE.linear) return 'Gerade fest';
  if (source === 'quadratic' || source === SOURCE.quadratic) return 'Polynom Grad 2 fest';
  if (source === 'external' || source === SOURCE.external) return 'Externe Gleichung';
  return 'Automatisch';
}

function modelRow(title, model) {
  if (!model) return '';
  const valid = model.valid === true || model.valid === 1;
  return `<tr><th>${title}</th><td>${valid ? 'gültig' : 'nicht verwendet'}, R² ${fmtNum(model.rSquared, 4)}, größter Restfehler ${fmtNum(model.maxAbsResidualN, 4)} N</td></tr>`;
}

function kennwerteMarkup(forceZ) {
  const active = (forceZ && forceZ.active) || null;
  if (!active) {
    return '<p>Es ist noch keine Messfunktion gespeichert.</p>';
  }
  const points = (forceZ && forceZ.points) || [];
  const degree = Number(active.degree) === 2 ? 'Polynom Grad 2' : 'Gerade';
  const practical = (0.003 * 60).toFixed(2);
  const reject = (0.01 * 60).toFixed(2);
  return `
    <h4>Kurvengleichung</h4>
    <p class="force-wizard__equation">${equationText(active)}</p>
    <p>Quelle: ${sourceLabel(forceZ.source)}. Verwendetes Modell: ${degree}. Zug ist negativ, Druck ist positiv. Dieselbe Kennlinie gilt für Fx, Fy und Fz.</p>
    <h4>Berechnungsgrundlage</h4>
    <p>Kleinste-Quadrate-Regression der Sensorkraft auf den ADC-Zählwert. Die Masse wird mit g0 = 9,80665 m/s² in Newton umgerechnet. Der Nullpunkt und das Adaptergewicht gehen als Netto-Zählwert ein, die Sollstufe nur in das Fenster von ±100 g.</p>
    <h4>Bedingungen</h4>
    <table class="force-wizard__spec">
      <tbody>
        <tr><th>Stützpunkte</th><td>${points.length}</td></tr>
        <tr><th>Pearson r</th><td>${fmtNum(active.pearsonR, 4)}</td></tr>
        <tr><th>R²</th><td>${fmtNum(active.rSquared, 4)}</td></tr>
        <tr><th>Größter Restfehler</th><td>${fmtNum(active.maxAbsResidualN, 4)} N</td></tr>
        <tr><th>Mittlerer Rest Zug</th><td>${fmtNum(active.meanResidualTensionN, 4)} N</td></tr>
        <tr><th>Mittlerer Rest Druck</th><td>${fmtNum(active.meanResidualCompressionN, 4)} N</td></tr>
        ${modelRow('Gerade', active.linear)}
        ${modelRow('Parabel', active.quadratic)}
        <tr><th>Parabel statt Gerade</th><td>Nur automatisch, und nur wenn mindestens 6 Punkte vorliegen, der größte Restfehler der Geraden über ${practical} N liegt (0,3 % von 60 N), der partielle F-Test bei α = 0,05 besteht und die Parabel im ADC-Intervall streng steigt.</td></tr>
        <tr><th>Ablehnung der Automatik</th><td>Größter Restfehler über ${reject} N (1 % von 60 N). Eine fest gewählte Gerade oder Parabel darf das nach einer zweiten Bestätigung überschreiten.</td></tr>
      </tbody>
    </table>
  `;
}

function curveName(report) {
  if (!report) return 'keine Kurve';
  if (report.resolvedSource === SOURCE.quadratic || report.degree === 2) return 'ein Polynom Grad 2';
  return 'eine Gerade';
}

function outcomeOf(report) {
  if (!report) {
    return {
      title: 'Die Kennlinie wurde nicht berechnet.',
      detail: 'Der Controller hat keine Ausgleichsrechnung geliefert.',
      canSave: false,
      confirm: false,
    };
  }
  if (report.reason === 0) {
    return {
      title: 'Die Kennlinie ist berechnet.',
      detail: `Vorgeschlagen ist ${curveName(report)}. Speichern übernimmt sie als Messfunktion. Zug ist negativ, Druck ist positiv.`,
      canSave: true,
      confirm: false,
    };
  }
  if (report.reason === 4 && report.needsConfirmation) {
    return {
      title: 'Der Restfehler ist groß.',
      detail: 'Er liegt über 1 % der Messspanne. Die Automatik übernimmt diese Kurve nicht. Sie können sie nach einer Bestätigung trotzdem speichern.',
      canSave: true,
      confirm: true,
    };
  }
  return {
    title: 'Die Kennlinie wird nicht übernommen.',
    detail: REASON[report.reason] || 'Die Ausgleichsrechnung ist fehlgeschlagen.',
    canSave: false,
    confirm: false,
  };
}

function statsLine(report) {
  if (!report || !Number.isFinite(Number(report.pearsonR))) return '';
  return `<p class="force-wizard__stats">Pearson r = ${Number(report.pearsonR).toFixed(4)}, R² = ${Number(report.rSquared).toFixed(4)}, größter Restfehler = ${Number(report.maxAbsResidualN).toFixed(3)} N. Mittlerer Rest Zug ${Number(report.meanResidualTensionN).toFixed(3)} N, Druck ${Number(report.meanResidualCompressionN).toFixed(3)} N.</p>`;
}

/**
 * @param {HTMLElement} contentEl
 * @param {{ onBack: () => void, viewOnly?: boolean }} opts
 */
export function mountForceCalibration(contentEl, opts) {
  const onBack = opts.onBack;
  let disposed = false;
  let jogCleanup = null;
  let pollTimer = 0;
  let status = {};
  const captured = [];
  let adcFree = null;
  let adcAdapter = null;
  let phase = opts.viewOnly ? 'view' : 'approach';
  let justSaved = false;
  let showPointLabels = false;
  let loadIndex = 0;
  let schedule = { ...FORCE_CALIB_SCHEDULE };
  let steps = loadSteps(schedule);
  let latestReport = null;
  let sampleToken = 0;

  function liveText() {
    const fx = Number(status.fx);
    const fy = Number(status.fy);
    const adc = status.adcValid ? status.adcZ : null;
    const sensorN = adc != null && latestReport ? evalCurve(latestReport.beta0, latestReport.beta1, latestReport.beta2 || 0, adc) : null;
    const fz = Number(status.fz);
    const parts = [
      `Fx ${Number.isFinite(fx) ? fx.toFixed(2) : '—'} N`,
      `Fy ${Number.isFinite(fy) ? fy.toFixed(2) : '—'} N`,
      `Fz ${Number.isFinite(fz) ? fz.toFixed(3) : '—'} N`,
      `ADC Z ${adc == null ? '—' : adc}`,
    ];
    if (sensorN != null) {
      parts.push(`Sensorkraft ${sensorGrams(sensorN).toFixed(0)} g (${sensorN.toFixed(3)} N)`);
    }
    return parts.join(' · ');
  }

  async function refreshStatus() {
    try {
      status = await fetchMachineStatus();
    } catch {
      status = status || {};
    }
    const live = contentEl.querySelector('#force-live');
    if (live) live.textContent = liveText();
  }

  function bindCommon(jogEnabled) {
    if (jogCleanup) {
      jogCleanup();
      jogCleanup = null;
    }
    const jogHost = contentEl.querySelector('#force-jog');
    if (jogHost && jogEnabled) {
      jogCleanup = mountJogPanel(jogHost, { getStatus: () => status, compact: true });
    }
    const open = contentEl.querySelector('#btn-spreader-open');
    const close = contentEl.querySelector('#btn-spreader-close');
    const stop = contentEl.querySelector('#btn-force-stop');
    if (open) open.addEventListener('click', () => sendCommand({ command: 'pneumaticOpen' }));
    if (close) close.addEventListener('click', () => sendCommand({ command: 'pneumaticClose' }));
    if (stop) {
      stop.addEventListener('click', async () => {
        sampleToken += 1;
        await sendCommand({ command: 'stop' });
        phase = 'aborted';
        render();
      });
    }
  }

  function progressLabel() {
    if (phase === 'approach') return 'Schritt 1 · Anfahrt';
    if (phase === 'free') return 'Schritt 2 · Ohne Kraft';
    if (phase === 'adapter') return 'Schritt 3 · Adapter';
    if (phase === 'tare') return 'Schritt 4 · Adaptergewicht';
    if (phase === 'load') return `Lastpunkt ${loadIndex + 1} von ${steps.length}`;
    if (phase === 'preview') return 'Ergebnis';
    if (phase === 'view') return 'Kalibrierkennwerte';
    return '';
  }

  function shell(title, body, jogEnabled, progress) {
    contentEl.innerHTML = `
      <div class="success-card force-wizard">
        <p class="force-wizard__progress">${progress || ''}</p>
        <h3 id="force-step-title">${title}</h3>
        <div id="force-step-body">${body}</div>
        <p id="force-feedback" class="command-feedback" aria-live="polite"></p>
        ${controlsMarkup(jogEnabled)}
      </div>
      <div class="control-actions page-actions">
        <button type="button" id="btn-force-back" class="btn-back">Zurück</button>
      </div>
    `;
    contentEl.querySelector('#btn-force-back').addEventListener('click', () => {
      sampleToken += 1;
      onBack();
    });
    bindCommon(jogEnabled);
    if (progress === undefined) {
      const label = contentEl.querySelector('.force-wizard__progress');
      if (label) label.textContent = progressLabel();
    }
    const live = contentEl.querySelector('#force-live');
    if (live) live.textContent = liveText();
  }

  function setFeedback(text, isError) {
    const el = contentEl.querySelector('#force-feedback');
    if (!el) return;
    el.textContent = text || '';
    el.classList.toggle('command-feedback--error', Boolean(isError));
  }

  async function runApproachMove(store) {
    const moved = await sendCommand({
      command: 'moveTo',
      x: store.probeCenter.x,
      y: store.probeCenter.y,
      z: -FORCE_CALIB_SCHEDULE.zClearanceMm,
    });
    if (!moved.ok) {
      setFeedback(moved.error || 'Anfahrt abgelehnt', true);
      return;
    }
    const started = Date.now();
    while (!disposed && Date.now() - started < 60000) {
      await refreshStatus();
      if (status.machineState === 'Fault' || status.machineState === 'Stopped') {
        setFeedback('Anfahrt abgebrochen.', true);
        return;
      }
        if (status.motionState && status.motionState !== 'Moving') {
          if (disposed) return;
          const guard = await setCollisionGuard(false);
          if (!guard.ok) {
            setFeedback(guard.error || 'Die Phasengrenze konnte nicht ausgeschaltet werden.', true);
            return;
          }
          phase = 'free';
          render();
          return;
        }
      await new Promise((resolve) => setTimeout(resolve, 250));
    }
    setFeedback('Anfahrt nicht beendet.', true);
  }

  async function renderApproach() {
    shell(
      'Anfahrt',
      `<p>Die Maschine fährt auf die Mitte des Messtasters, ${FORCE_CALIB_SCHEDULE.zClearanceMm} mm unter den oberen Anschlag.</p>
       <button type="button" id="btn-approach" class="force-wizard__primary">Anfahrt starten</button>`,
      false
    );
    contentEl.querySelector('#btn-approach').addEventListener('click', async () => {
      const calib = await fetchCalibration();
      const store = (calib && calib.calibration) || {};
      if (!store.machineCoordinateSystem || !store.machineCoordinateSystem.valid || !store.probeCenterValid || !store.probeCenter) {
        setFeedback('Zuerst MCS und Messtaster kalibrieren.', true);
        return;
      }
      await refreshStatus();
      const gate = await runWithHomingGate({
        status: {
          connected: status.connected !== false,
          isHomed: status.isHomed === true,
          mcsValid: status.mcsValid === true,
        },
        startHoming: () => sendCommand({ command: 'home' }),
        getStatus: async () => {
          await refreshStatus();
          return {
            connected: status.connected !== false,
            isHomed: status.isHomed === true,
            mcsValid: status.mcsValid === true,
            homingState: status.homingState,
            machineState: status.machineState,
            eStop: status.eStop === true,
          };
        },
        onHomingStarted: () => {
          setFeedback('Homing gestartet — warte auf erfolgreichen Abschluss…', false);
        },
        action: () => runApproachMove(store),
      });
      if (gate.result === 'cancelled') {
        setFeedback('Anfahrt abgebrochen — Homing erforderlich.', false);
      } else if (gate.result === 'failed') {
        setFeedback('Homing fehlgeschlagen — Anfahrt nicht gestartet.', true);
      } else if (gate.result === 'timeout') {
        setFeedback('Homing-Timeout — Anfahrt nicht gestartet.', true);
      } else if (gate.result === 'mcs_required') {
        setFeedback('Zuerst MCS und Messtaster kalibrieren.', true);
      }
    });
  }

  const SAMPLE_COUNT = 20;
  const SAMPLE_GAP_MS = 100;

  function delay(ms) {
    return new Promise((resolve) => setTimeout(resolve, ms));
  }

  function roundedMean(samples) {
    const valid = samples.filter((value) => value != null);
    if (!valid.length) return null;
    return Math.round(valid.reduce((sum, value) => sum + value, 0) / valid.length);
  }

  function lockWizard(locked) {
    const nodes = [
      ...contentEl.querySelectorAll('button'),
      ...contentEl.querySelectorAll('#force-mass'),
    ];
    nodes.forEach((el) => {
      if (el.id === 'btn-force-stop') return;
      if (locked) {
        if (!el.disabled) el.dataset.lockedByBurst = '1';
        el.disabled = true;
      } else if (el.dataset.lockedByBurst === '1') {
        el.disabled = false;
        delete el.dataset.lockedByBurst;
      }
    });
  }

  async function readBurst() {
    const token = ++sampleToken;
    lockWizard(true);
    const progress = contentEl.querySelector('#force-sample-progress');
    const samples = [];
    const started = Date.now();
    for (let i = 0; i < SAMPLE_COUNT; i += 1) {
      const wait = started + i * SAMPLE_GAP_MS - Date.now();
      if (wait > 0) await delay(wait);
      if (disposed || token !== sampleToken) return null;
      await refreshStatus();
      if (disposed || token !== sampleToken) return null;
      const value = status.adcValid ? Number(status.adcZ) : null;
      samples.push(Number.isFinite(value) ? Math.round(value) : null);
      if (progress) progress.textContent = `Messung ${i + 1} von ${SAMPLE_COUNT}`;
    }
    lockWizard(false);
    return samples;
  }

  function sampleTable(samples) {
    const rows = [];
    for (let i = 0; i < samples.length; i += 5) {
      const cells = samples.slice(i, i + 5).map((value, offset) => {
        const text = value == null ? '—' : String(value);
        return `<th>${i + offset + 1}</th><td>${text}</td>`;
      }).join('');
      rows.push(`<tr>${cells}</tr>`);
    }
    return `<table class="force-wizard__samples"><tbody>${rows.join('')}</tbody></table>`;
  }

  function showDecision(samples, mean, acceptValue, onAccept, onRepeat) {
    const host = contentEl.querySelector('#force-sample-host');
    if (!host) return;
    const validCount = samples.filter((value) => value != null).length;
    const missing = samples.length - validCount;
    const corrected = acceptValue != null && acceptValue !== mean
      ? `<p class="force-wizard__sample-progress">Gespeichert wird der korrigierte Wert ${acceptValue}.</p>`
      : '';
    host.innerHTML = `
      <p class="force-wizard__mean">${mean == null ? '—' : mean}</p>
      <p class="force-wizard__sample-progress">Mittel aus ${validCount} von ${SAMPLE_COUNT} Messungen</p>
      ${corrected}
      ${missing ? '<p class="force-wizard__sample-progress">Einzelne Messungen sind ausgefallen. Sie können den Mittelwert übernehmen oder den Punkt wiederholen.</p>' : ''}
      ${sampleTable(samples)}
      <button type="button" id="btn-accept-sample" class="force-wizard__primary" ${acceptValue == null ? 'disabled' : ''}>Wert übernehmen und weiter</button>
      <button type="button" id="btn-repeat-sample">Kalibrierpunkt wiederholen</button>
    `;
    host.querySelector('#btn-accept-sample').addEventListener('click', () => onAccept(acceptValue));
    host.querySelector('#btn-repeat-sample').addEventListener('click', () => {
      host.innerHTML = '';
      const progress = contentEl.querySelector('#force-sample-progress');
      if (progress) progress.textContent = '';
      onRepeat();
    });
  }

  function renderFree() {
    shell(
      'Kraftfreie Messung',
      `<p>Es wird eine kraftfreie Messung durchgeführt. Bitte keine Kraft auf den Kraftsensor aufbringen.</p>
       <p>An jedem Kalibrierpunkt werden 20 Messungen in 2 Sekunden gemittelt. Gespeichert und verwendet wird dieser Mittelwert.</p>
       <button type="button" id="btn-capture-free" class="force-wizard__primary">Messpunkt aufnehmen</button>
       <p id="force-sample-progress" class="force-wizard__sample-progress"></p>
       <div id="force-sample-host"></div>`,
      true
    );
    const start = contentEl.querySelector('#btn-capture-free');
    async function run() {
      start.disabled = true;
      const samples = await readBurst();
      if (!samples) return;
      const mean = roundedMean(samples);
      showDecision(samples, mean, mean, (value) => {
        adcFree = value;
        captured.push({ adcNet: value, massG: 0, nominalG: 0, direction: DIR.zero });
        phase = 'adapter';
        render();
      }, run);
    }
    start.addEventListener('click', () => { void run(); });
  }

  function renderAdapter() {
    shell(
      'Kalibrieradapter',
      `<p>Bitte den Kalibrieradapter in den Spreizer einspannen. Öffnen und Schließen liegen unten in der Leiste.</p>
       <button type="button" id="btn-adapter-done" class="force-wizard__primary">Adapter ist eingespannt</button>`,
      true
    );
    contentEl.querySelector('#btn-adapter-done').addEventListener('click', () => {
      phase = 'tare';
      render();
    });
  }

  function renderTare() {
    shell(
      'Adapter ohne Zusatzlast',
      `<p>Keine zusätzliche Last. Diese Messung zieht das Gewicht des Adapters von den folgenden Punkten ab.</p>
       <button type="button" id="btn-capture-tare" class="force-wizard__primary">Messung aufnehmen</button>
       <p id="force-sample-progress" class="force-wizard__sample-progress"></p>
       <div id="force-sample-host"></div>`,
      true
    );
    const start = contentEl.querySelector('#btn-capture-tare');
    async function run() {
      start.disabled = true;
      const samples = await readBurst();
      if (!samples) return;
      const mean = roundedMean(samples);
      showDecision(samples, mean, mean, (value) => {
        adcAdapter = value;
        phase = 'load';
        loadIndex = 0;
        render();
      }, run);
    }
    start.addEventListener('click', () => { void run(); });
  }

  function renderLoad() {
    const step = steps[loadIndex];
    const tension = step.direction === 'tension';
    const text = tension
      ? `Stellen Sie etwa ${step.nominalG} g Zug ein und tragen Sie den genauen Wert in Gramm ein.`
      : `Nullen Sie eine Waage und fahren Sie mit den Tasten unten darauf. Tragen Sie den abgelesenen Wert in Gramm ein. Sollwert etwa ${step.nominalG} g.`;
    shell(
      tension ? `Zug, ${step.nominalG} g` : `Druck, ${step.nominalG} g`,
      `<p>${text}</p>
       <label class="force-wizard__mass">Abgelesene Masse in g
         <input id="force-mass" type="number" min="0" step="1" value="${step.nominalG}" />
       </label>
       <button type="button" id="btn-capture-load" class="force-wizard__primary">Messpunkt aufnehmen</button>
       <p id="force-sample-progress" class="force-wizard__sample-progress"></p>
       <div id="force-sample-host"></div>`,
      true
    );
    const start = contentEl.querySelector('#btn-capture-load');
    async function run() {
      const mass = Number(contentEl.querySelector('#force-mass').value);
      const windowG = schedule.windowG;
      if (!Number.isFinite(mass) || Math.abs(mass - step.nominalG) > windowG) {
        setFeedback(
          `Der Wert muss im Fenster ${step.nominalG - windowG} g bis ${step.nominalG + windowG} g liegen. Bitte die Kalibrierkraft ändern.`,
          true
        );
        return;
      }
      const massN = (mass / 1000) * G0;
      if (massN > schedule.rangeN) {
        setFeedback(`Die Masse entspricht ${massN.toFixed(2)} N und liegt über dem Messbereich ${schedule.rangeN} N.`, true);
        return;
      }
      if (adcFree == null || adcAdapter == null) {
        setFeedback('Kein gültiger ADC-Wert.', true);
        return;
      }
      start.disabled = true;
      const samples = await readBurst();
      if (!samples) return;
      const mean = roundedMean(samples);
      const net = mean == null ? null : mean - (adcAdapter - adcFree);
      const stored = net != null && net >= 0 && net <= 65535 ? Math.round(net) : null;
      if (mean != null && stored == null) {
        setFeedback('Der korrigierte ADC-Wert ist ungültig. Bitte die Messung wiederholen.', true);
      }
      showDecision(samples, mean, stored, (value) => {
        if (value == null || value < 0 || value > 65535) return;
        captured.push({
          adcNet: value,
          massG: mass,
          nominalG: step.nominalG,
          direction: tension ? DIR.tension : DIR.compression,
        });
        loadIndex += 1;
        phase = loadIndex >= steps.length ? 'preview' : 'load';
        render();
      }, run);
    }
    start.addEventListener('click', () => { void run(); });
  }

  function showFitResult(report, errorText) {
    const title = contentEl.querySelector('#force-step-title');
    const body = contentEl.querySelector('#force-step-body');
    if (!body) return;
    const outcome = outcomeOf(report);
    if (title) title.textContent = outcome.title;
    const alternate = report && report.resolvedSource === SOURCE.quadratic && report.linearValid === 1
      ? { beta0: report.linearBeta0, beta1: report.linearBeta1, beta2: report.linearBeta2 }
      : report && report.quadraticValid === 1
        ? { beta0: report.quadraticBeta0, beta1: report.quadraticBeta1, beta2: report.quadraticBeta2 }
        : null;
    const marked = report
      ? captured.map((point, index) => ({
          ...point,
          referenceForceN: report.referenceForceN ? report.referenceForceN[index] : null,
        }))
      : [];
    const chart = report
      ? chartSvg({ beta0: report.beta0, beta1: report.beta1, beta2: report.beta2 }, alternate, marked)
      : '';
    body.innerHTML = `
      ${chart}
      <p>${outcome.detail}</p>
      ${statsLine(report)}
      ${errorText ? `<p class="command-feedback command-feedback--error">${errorText}</p>` : ''}
      ${outcome.canSave ? `<button type="button" id="btn-save-curve" class="force-wizard__primary">${outcome.confirm ? 'Trotzdem speichern' : 'Als Messfunktion speichern'}</button>` : ''}
    `;
    bindForceCharts(body);
    const save = contentEl.querySelector('#btn-save-curve');
    if (save) {
      save.addEventListener('click', async () => {
        save.disabled = true;
        try {
          const saved = await submitForceCalibration({
            source: SOURCE.auto,
            confirm: outcome.confirm,
            commit: true,
            points: captured,
          });
          latestReport = saved.report || latestReport;
          if (!saved.ok || !(saved.report && saved.report.committed)) {
            save.disabled = false;
            setFeedback(REASON[(saved.report && saved.report.reason) || 0] || saved.error || 'Nicht gespeichert', true);
            return;
          }
          justSaved = true;
          phase = 'view';
          render();
        } catch (err) {
          save.disabled = false;
          setFeedback(err.message || String(err), true);
        }
      });
    }
  }

  async function renderPreview() {
    shell('Ausgleichsrechnung', '<p>Die Messpunkte werden ausgeglichen. Das Ergebnis erscheint hier.</p>', false);
    let result;
    try {
      result = await submitForceCalibration({ source: SOURCE.auto, confirm: false, commit: false, points: captured });
    } catch (err) {
      if (disposed || phase !== 'preview') return;
      showFitResult(null, err.message || String(err));
      return;
    }
    if (disposed || phase !== 'preview') return;
    latestReport = result.report || null;
    if (!latestReport) {
      showFitResult(null, result.error || 'Keine Antwort vom Controller');
      return;
    }
    showFitResult(latestReport, '');
  }

  async function renderView() {
    const calib = await fetchCalibration();
    const forceZ = (calib && calib.calibration && calib.calibration.forceZ) || {};
    const active = (latestReport && {
      beta0: latestReport.beta0,
      beta1: latestReport.beta1,
      beta2: latestReport.beta2,
    }) || (forceZ.active && {
      beta0: forceZ.active.beta0,
      beta1: forceZ.active.beta1,
      beta2: forceZ.active.beta2,
    });
    const points = forceZ.points || [];
    const hasPoints = points.length >= 2;
    const headline = justSaved
      ? 'Die Kennlinie ist gespeichert.'
      : active
        ? 'Kalibrierkennwerte'
        : 'Noch keine Kennlinie';
    const specModel = forceZ.active || (latestReport && {
      degree: latestReport.degree,
      beta0: latestReport.beta0,
      beta1: latestReport.beta1,
      beta2: latestReport.beta2,
      pearsonR: latestReport.pearsonR,
      rSquared: latestReport.rSquared,
      maxAbsResidualN: latestReport.maxAbsResidualN,
      meanResidualTensionN: latestReport.meanResidualTensionN,
      meanResidualCompressionN: latestReport.meanResidualCompressionN,
      linear: {
        valid: latestReport.linearValid === 1,
        rSquared: latestReport.linearR2,
        maxAbsResidualN: latestReport.linearMaxAbsN,
      },
      quadratic: {
        valid: latestReport.quadraticValid === 1,
        rSquared: latestReport.quadraticR2,
        maxAbsResidualN: latestReport.quadraticMaxAbsN,
      },
    });
    shell(
      headline,
      `<label class="force-wizard__toggle"><input type="checkbox" id="toggle-point-labels" ${showPointLabels ? 'checked' : ''} /> Beschriftung der Kalibrierpunkte</label>
       <div id="force-chart">${chartSvg(active, null, points, showPointLabels)}</div>
       ${kennwerteMarkup({ ...forceZ, active: specModel, points })}
       <details class="force-wizard__advanced">
         <summary>Andere Kurve wählen</summary>
         <fieldset>
           <legend>Wirksame Kurve</legend>
           <label><input type="radio" name="force-source" value="auto" checked /> Automatisch</label>
           <label><input type="radio" name="force-source" value="linear" ${hasPoints ? '' : 'disabled'} /> Gerade fest</label>
           <label><input type="radio" name="force-source" value="quadratic" ${hasPoints ? '' : 'disabled'} /> Polynom Grad 2 fest</label>
           <label><input type="radio" name="force-source" value="external" /> Externe Gleichung</label>
         </fieldset>
         <div id="external-fields">
           <p>F_sensor [N] = β0 + β1·x + β2·x². β2 darf 0 sein.</p>
           <label>β0 <input id="ext-b0" type="number" step="any" value="0" /></label>
           <label>β1 <input id="ext-b1" type="number" step="any" value="0" /></label>
           <label>β2 <input id="ext-b2" type="number" step="any" value="0" /></label>
           <label>x
             <select id="ext-unit">
               <option value="adc">ADC-Counts</option>
               <option value="volt">Spannung in Volt</option>
             </select>
           </label>
         </div>
         <button type="button" id="btn-apply-source">Auswahl übernehmen</button>
       </details>`,
      false
    );
    bindForceCharts(contentEl);
    const toggle = contentEl.querySelector('#toggle-point-labels');
    if (toggle) {
      toggle.addEventListener('change', () => {
        showPointLabels = toggle.checked;
        const host = contentEl.querySelector('#force-chart');
        if (host) {
          host.innerHTML = chartSvg(active, null, points, showPointLabels);
          bindForceCharts(host);
        }
      });
    }
    const checked = contentEl.querySelector(`input[value="${forceZ.source || 'auto'}"]`);
    if (checked && !checked.disabled) checked.checked = true;
    contentEl.querySelector('#btn-apply-source').addEventListener('click', () => applySource(false));
  }

  async function applySource(confirm) {
    const selected = contentEl.querySelector('input[name="force-source"]:checked');
    if (!selected) return;
    const source = SOURCE[selected.value];
    let result;
    if (source === SOURCE.external) {
      result = await selectForceCurve({
        source,
        confirm,
        commit: true,
        unit: contentEl.querySelector('#ext-unit').value,
        beta0: Number(contentEl.querySelector('#ext-b0').value),
        beta1: Number(contentEl.querySelector('#ext-b1').value),
        beta2: Number(contentEl.querySelector('#ext-b2').value),
      });
    } else {
      result = await selectForceCurve({ source, confirm, commit: true, beta0: 0, beta1: 0, beta2: 0, unit: 'adc' });
    }
    latestReport = result.report || latestReport;
    if (result.report && result.report.needsConfirmation && !confirm) {
      const accepted = window.confirm(
        'Der Restfehler liegt über 1 % der Messspanne. Diese Kurve trotzdem als Messfunktion übernehmen?'
      );
      if (accepted) {
        await applySource(true);
      }
      return;
    }
    if (!result.ok) {
      setFeedback(REASON[(result.report && result.report.reason) || 0] || result.error || 'Nicht übernommen', true);
      return;
    }
    setFeedback('Die Auswahl ist die wirksame Messfunktion.', false);
    phase = 'view';
    render();
  }

  function render() {
    if (disposed) return;
    if (phase === 'approach') renderApproach();
    else if (phase === 'free') renderFree();
    else if (phase === 'adapter') renderAdapter();
    else if (phase === 'tare') renderTare();
    else if (phase === 'load') renderLoad();
    else if (phase === 'preview') void renderPreview();
    else if (phase === 'view') void renderView();
    else {
      shell('Abgebrochen', '<p>Der Vorgang wurde mit STOP beendet. Gespeicherte Kennlinien bleiben unverändert.</p>', false);
    }
  }

  async function refreshSchedule() {
    try {
      const result = await fetchMachineConfig();
      schedule = scheduleFromConfig(result && result.config);
      steps = loadSteps(schedule);
    } catch {
      schedule = { ...FORCE_CALIB_SCHEDULE };
      steps = loadSteps(schedule);
    }
  }

  pollTimer = window.setInterval(() => {
    void refreshStatus();
  }, 400);
  void Promise.all([refreshStatus(), refreshSchedule()]).then(render);

  return () => {
    disposed = true;
    window.clearInterval(pollTimer);
    if (jogCleanup) jogCleanup();
    void setCollisionGuard(true);
  };
}
