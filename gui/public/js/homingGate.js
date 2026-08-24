/**

 * Central GUI gate: offer Homing when an action requires isHomed.

 * Processes that themselves run Homing should not call this.

 *

 * Pure helpers are exported for unit tests (no DOM).

 */



const STARTUP_TITLE = 'Maschine ist noch nicht gehomed';

const STARTUP_MESSAGE =

  'Für die weitere Bedienung muss die Maschine zunächst gehomed werden.\nMöchten Sie ein Homing durchführen?';



const ACTION_TITLE = 'Maschine ist noch nicht gehomed';

const ACTION_MESSAGE =

  'Für diesen Prozess muss zunächst ein Homing durchgeführt werden.\nMöchten Sie jetzt ein Homing durchführen?';



function sleep(ms) {

  return new Promise((resolve) => setTimeout(resolve, ms));

}



/**

 * First usable Core status: live TCP session, not a pre-connect default.

 * @param {object|null|undefined} status

 */

export function isValidLiveStatus(status) {

  return Boolean(status && status.connected === true);

}



/**

 * @param {object|null|undefined} status

 */

export function needsHoming(status) {

  return isValidLiveStatus(status) && status.isHomed !== true;

}



/**

 * Classify wait loop while Homing runs.

 * @param {object|null|undefined} status

 * @returns {'waiting'|'homed'|'failed'}

 */

export function classifyHomingOutcome(status) {

  if (!status) {

    return 'waiting';

  }

  if (status.isHomed === true) {

    return 'homed';

  }

  if (

    status.homingState === 'HomingError' ||

    status.machineState === 'Fault' ||

    status.eStop === true

  ) {

    return 'failed';

  }

  return 'waiting';

}



/**

 * Decide whether the GUI-start prompt should open for this status sample.

 * @param {object|null|undefined} status

 * @param {{ alreadyHandled?: boolean }} [state]

 * @returns {'skip'|'prompt'|'markHomed'}

 */

export function evaluateStartupHoming(status, state = {}) {

  if (state.alreadyHandled) {

    return 'skip';

  }

  if (!isValidLiveStatus(status)) {

    return 'skip';

  }

  if (status.isHomed === true) {

    return 'markHomed';

  }

  return 'prompt';

}



/**

 * Poll until Homed, HomingError/Fault, or timeout.

 * @param {{ getStatus: () => (object|Promise<object>), intervalMs?: number, timeoutMs?: number }} options

 */

export async function waitUntilHomed(options) {

  const getStatus = options.getStatus;

  const intervalMs = options.intervalMs ?? 200;

  const timeoutMs = options.timeoutMs ?? 120000;

  const started = Date.now();



  while (Date.now() - started < timeoutMs) {

    const status = await getStatus();

    const outcome = classifyHomingOutcome(status);

    if (outcome === 'homed') {

      return { ok: true, reason: 'homed', status };

    }

    if (outcome === 'failed') {

      return { ok: false, reason: 'failed', status };

    }

    await sleep(intervalMs);

  }

  return { ok: false, reason: 'timeout', status: null };

}



/**

 * Prompt if needed, run Homing, wait for Homed, then run the original action.

 * Does not start the action before Homing succeeds.

 *

 * @param {{

 *   status: object,

 *   prompt?: (status: object, options?: object) => Promise<'home'|'cancel'|'already'>,

 *   startHoming: () => Promise<unknown>,

 *   getStatus: () => (object|Promise<object>),

 *   action: () => Promise<unknown>,

 *   onHomingStarted?: () => (void|Promise<void>),

 *   promptOptions?: object,

 *   intervalMs?: number,

 *   timeoutMs?: number,

 * }} options

 */

export async function runWithHomingGate(options) {

  const {

    status,

    prompt = ensureHomedOrPrompt,

    startHoming,

    getStatus,

    action,

    onHomingStarted,

    promptOptions,

    intervalMs,

    timeoutMs,

  } = options;



  if (status && status.isHomed === true) {

    return { result: 'ran', actionResult: await action() };

  }



  const choice = await prompt(status || { isHomed: false }, promptOptions || {

    title: ACTION_TITLE,

    message: ACTION_MESSAGE,

  });



  if (choice === 'cancel') {

    return { result: 'cancelled' };

  }



  if (choice === 'already') {

    return { result: 'ran', actionResult: await action() };

  }



  await startHoming();

  if (typeof onHomingStarted === 'function') {

    await onHomingStarted();

  }

  const waited = await waitUntilHomed({ getStatus, intervalMs, timeoutMs });

  if (!waited.ok) {

    return { result: waited.reason, status: waited.status };

  }

  return { result: 'ran', actionResult: await action() };

}



/**

 * One-shot GUI-start watcher: prompt after first valid live status if not Homed.

 * @param {{

 *   prompt?: (status: object, options?: object) => Promise<'home'|'cancel'|'already'>,

 *   startHoming: () => Promise<unknown>,

 * }} deps

 */

export function createStartupHomingController(deps) {

  const prompt = deps.prompt || ensureHomedOrPrompt;

  const startHoming = deps.startHoming;

  let alreadyHandled = false;

  let busy = false;



  return {

    /**

     * Feed each live status sample. Safe to call on every WebSocket tick.

     * @param {object} status

     */

    async onStatus(status) {

      if (busy) {

        return;

      }

      const decision = evaluateStartupHoming(status, { alreadyHandled });

      if (decision === 'skip') {

        return;

      }

      if (decision === 'markHomed') {

        alreadyHandled = true;

        return;

      }



      alreadyHandled = true;

      busy = true;

      try {

        const choice = await prompt(status, {

          title: STARTUP_TITLE,

          message: STARTUP_MESSAGE,

        });

        if (choice === 'home') {

          await startHoming();

        }

      } finally {

        busy = false;

      }

    },



    /** @returns {{ alreadyHandled: boolean, busy: boolean }} */

    getState() {

      return { alreadyHandled, busy };

    },

  };

}



/**

 * @param {{ isHomed?: boolean }} status

 * @param {{ title?: string, message?: string }} [options]

 * @returns {Promise<'home'|'cancel'|'already'>}

 */

export function ensureHomedOrPrompt(status, options = {}) {

  if (status && status.isHomed === true) {

    return Promise.resolve('already');

  }



  const title = options.title || ACTION_TITLE;

  const message = options.message || ACTION_MESSAGE;



  return new Promise((resolve) => {

    if (typeof document === 'undefined') {

      resolve('cancel');

      return;

    }



    const existing = document.querySelector('.homing-gate-overlay');

    if (existing) {

      resolve('cancel');

      return;

    }



    const overlay = document.createElement('div');

    overlay.className = 'homing-gate-overlay';

    overlay.setAttribute('role', 'dialog');

    overlay.setAttribute('aria-modal', 'true');

    overlay.innerHTML = `

      <div class="homing-gate-dialog">

        <h3>${escapeHtml(title)}</h3>

        <p>${escapeHtml(message).replaceAll('\n', '<br/>')}</p>

        <div class="homing-gate-actions">

          <button type="button" class="homing-gate-yes">JA – Homing starten</button>

          <button type="button" class="homing-gate-no">NEIN – zurück</button>

        </div>

      </div>

    `;

    const cleanup = (result) => {

      overlay.remove();

      resolve(result);

    };

    overlay.querySelector('.homing-gate-yes').addEventListener('click', () => cleanup('home'));

    overlay.querySelector('.homing-gate-no').addEventListener('click', () => cleanup('cancel'));

    document.body.appendChild(overlay);

  });

}



function escapeHtml(str) {

  return String(str)

    .replaceAll('&', '&amp;')

    .replaceAll('<', '&lt;')

    .replaceAll('>', '&gt;')

    .replaceAll('"', '&quot;');

}


