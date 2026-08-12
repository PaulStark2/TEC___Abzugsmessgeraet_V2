'use strict';

import { renderPlaceholder } from './placeholder.js';

export function renderLoadPoints(container) {
  renderPlaceholder(container, {
    title: 'Prüftstellen laden',
    description: 'Platzhalter — Dateiauswahl und Validierung folgen in G5.',
    milestone: 'G5',
  });
}
