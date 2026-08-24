'use strict';

/**
 * Client helper regression (button / labels) — mirrors coreProcessGuard semantics.
 */

const assert = require('assert');
const path = require('path');
const fs = require('fs');
const { pathToFileURL } = require('url');

async function loadClientModule() {
  const file = path.join(__dirname, '..', 'public', 'js', 'coreProcessStatus.js');
  assert.ok(fs.existsSync(file), 'coreProcessStatus.js missing');
  return import(pathToFileURL(file).href);
}

async function main() {
  const mod = await loadClientModule();

  assert.strictEqual(
    mod.isProcedureStartButtonDisabled({ procedureState: 'idle', coreProcessState: 'Running' }),
    true
  );
  assert.strictEqual(
    mod.isProcedureStartButtonDisabled({ procedureState: 'idle', coreProcessState: 'Completed' }),
    false
  );
  assert.strictEqual(
    mod.describeRunningCoreProcess('ProbeCalibrate').startBlockedMessage.includes('Messtaster'),
    true
  );
  assert.strictEqual(mod.describeRunningCoreProcess('PointApproach').startBlockedMessage, 'Die Prüfprozedur läuft bereits.');
  assert.strictEqual(
    mod.describeRunningCoreProcess('UnknownPhaseX').startBlockedMessage.includes('anderer Maschinenprozess'),
    true
  );
  assert.strictEqual(mod.formatProcedurePhaseLabel('ProbeCalibrate'), 'Messtaster kalibrieren');

  console.log('core_process_status_tests.js: PASS');
}

main().catch((err) => {
  console.error(err);
  process.exit(1);
});
