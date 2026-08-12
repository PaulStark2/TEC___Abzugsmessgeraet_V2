'use strict';

import { renderPlaceholder } from './placeholder.js';

export function renderPlate(container) {
  renderPlaceholder(container, {
    title: 'Bauplatte wechseln',
    description: 'Platzhalter — Spannsystem folgt in G10.',
    milestone: 'G10',
  });
}
