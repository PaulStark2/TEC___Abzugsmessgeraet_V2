'use strict';

/**
 * One GUI account. The session lives in this process and starts locked.
 * Development defaults are overridden on the device via GUI_USERNAME / GUI_PASSWORD.
 */

const DEV_USERNAME = 'bediener';
const DEV_PASSWORD = 'tec-admin';

function createGuiSession(env = process.env) {
  const username = String(env.GUI_USERNAME || DEV_USERNAME);
  const password = String(env.GUI_PASSWORD || DEV_PASSWORD);
  let authenticated = false;

  return {
    isAuthenticated() {
      return authenticated;
    },
    login(user, pass) {
      const userOk = String(user || '') === username;
      const passOk = String(pass || '') === password;
      if (!userOk || !passOk) {
        return false;
      }
      authenticated = true;
      return true;
    },
    logout() {
      authenticated = false;
    },
  };
}

/**
 * Mutations that stay available without a GUI session.
 * Simulation routes stay open so the development page does not need this login.
 * @param {string} method
 * @param {string} urlPath
 */
function isSessionExemptMutation(method, urlPath) {
  if (method === 'GET' || method === 'HEAD' || method === 'OPTIONS') {
    return true;
  }
  if (!urlPath.startsWith('/api/')) {
    return true;
  }
  if (method === 'POST' && urlPath === '/api/session/login') {
    return true;
  }
  if (method === 'POST' && urlPath === '/api/system/shutdown') {
    return true;
  }
  if (urlPath.startsWith('/api/simulation/')) {
    return true;
  }
  return false;
}

module.exports = {
  DEV_USERNAME,
  DEV_PASSWORD,
  createGuiSession,
  isSessionExemptMutation,
};
