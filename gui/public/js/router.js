'use strict';

import { renderMenu } from './pages/menu.js';
import { renderManual } from './pages/manual.js';
import { renderLoadPoints } from './pages/loadPoints.js';
import { renderSelectPoints } from './pages/selectPoints.js';
import { renderProcedure } from './pages/procedure.js';
import { renderResults } from './pages/results.js';
import { renderPlate } from './pages/plate.js';
import { renderCalibrate } from './pages/calibrate.js';
import { renderParameters } from './pages/parameters.js';

const ROUTES = {
  menu: renderMenu,
  'load-points': renderLoadPoints,
  'select-points': renderSelectPoints,
  procedure: renderProcedure,
  results: renderResults,
  manual: renderManual,
  plate: renderPlate,
  calibrate: renderCalibrate,
  parameters: renderParameters,
};

const DEFAULT_ROUTE = 'menu';

let activeCleanup = null;

function getRouteFromHash() {
  const raw = (window.location.hash || '').replace(/^#/, '').trim();
  if (!raw) {
    return DEFAULT_ROUTE;
  }
  return raw in ROUTES ? raw : DEFAULT_ROUTE;
}

export function navigate(route) {
  const target = route in ROUTES ? route : DEFAULT_ROUTE;
  if (window.location.hash !== `#${target}`) {
    window.location.hash = target;
    return;
  }
  renderRoute(target);
}

function renderRoute(route) {
  const root = document.getElementById('page-root');
  if (!root) return;

  if (activeCleanup) {
    activeCleanup();
    activeCleanup = null;
  }

  root.innerHTML = '';
  const cleanup = ROUTES[route](root);
  if (typeof cleanup === 'function') {
    activeCleanup = cleanup;
  }
}

export function startRouter() {
  window.addEventListener('hashchange', () => {
    renderRoute(getRouteFromHash());
  });

  if (!window.location.hash || window.location.hash === '#') {
    window.location.hash = DEFAULT_ROUTE;
  } else {
    renderRoute(getRouteFromHash());
  }
}
