'use strict';

import { renderPlaceholder } from './placeholder.js';

export function renderResults(container) {
  renderPlaceholder(container, {
    title: 'Prüfergebnisse',
    description: 'Platzhalter — Ergebnisanzeige und Export folgen in G9.',
    milestone: 'G9',
  });
}
