'use strict';

/**
 * Unified procedure progress display semantics (mock + protocol).
 *
 * Controller uses 0-based currentPointIndex = count of completed points while Running.
 * UI shows pointsDone / totalPoints where pointsDone = completed point count.
 */

function derivePointProgress({
  totalPoints,
  index0Based,
  processState,
  procedureState,
  iterationIndex = 0,
  totalIterations = 1,
  pointsPerIteration = null,
}) {
  const total = Math.max(0, Number(totalPoints) || 0);
  const iterations = Math.max(1, Number(totalIterations) || 1);
  const perIter = pointsPerIteration === null ? total : Math.max(0, Number(pointsPerIteration) || 0);

  if (total <= 0 && perIter <= 0) {
    return {
      pointsDone: 0,
      totalPoints: 0,
      percent: 0,
      isComplete: false,
      currentPointIndex: 0,
    };
  }

  const complete =
    processState === 'Completed' || procedureState === 'completed';

  if (complete) {
    return {
      pointsDone: total,
      totalPoints: total,
      percent: 100,
      isComplete: true,
      currentPointIndex: index0Based ?? total,
    };
  }

  const idx = Number.isFinite(Number(index0Based)) ? Math.max(0, Number(index0Based)) : 0;
  const pointsDone = Math.min(idx, total);

  let percent;
  if (iterations > 1 && perIter > 0) {
    const doneSteps = iterationIndex * perIter + pointsDone;
    const totalSteps = Math.max(1, iterations * perIter);
    percent = Math.min(99, Math.round((doneSteps / totalSteps) * 100));
  } else {
    percent = total > 0 ? Math.min(99, Math.round((pointsDone / total) * 100)) : 0;
  }

  return {
    pointsDone,
    totalPoints: total,
    percent,
    isComplete: false,
    currentPointIndex: idx,
  };
}

module.exports = {
  derivePointProgress,
};
