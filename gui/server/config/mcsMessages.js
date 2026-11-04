'use strict';

/** Shared UX copy when Reference Homing is requested without a valid MCS. */
const MCS_REQUIRED_MESSAGE =
  'Kein gültiges Maschinenkoordinatensystem vorhanden. ' +
  'Bitte zuerst „MCS und Bauplatte/Einmessbohrung definieren und kalibrieren“ durchführen.';

const GEOMETRY_INCOMPLETE_MESSAGE =
  'Maschinenkoordinatensystem ist definiert. ' +
  'Bauplatten- und Einmessbohrungskalibrierung sind noch erforderlich.';

module.exports = {
  MCS_REQUIRED_MESSAGE,
  GEOMETRY_INCOMPLETE_MESSAGE,
};
