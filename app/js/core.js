/* Junction core — TV service bridge, settings and pure helpers.
   Static classic script: no bundler and no Node.js runtime on the TV. */
(function (root) {
  'use strict';

  var APP = {
    id: 'dev.hurrspice.synergywebos',
    version: '0.3.2',
    name: 'SynergyWebOS',
    dockAppId: 'dev.hurrspice.tvdock',
    serverApiPort: 24803
  };

  var STORAGE_KEY = 'junction.settings.v1';
  var LEGACY_STORAGE_KEY = 'synergy-webos-settings';
  var PROFILE_SCHEMA = 'junction.handoff.v1';
  var APP_DIRS = [
    '/media/developer/apps/usr/palm/applications/' + APP.id,
    '/usr/palm/applications/' + APP.id,
    '/var/lib/webosbrew/apps/' + APP.id
  ];

  var DEFAULTS = {
    host: '',
    port: '24800',
    screenName: 'webos-tv',
    width: '1920',
    height: '1080',
    dryRun: false,
    autoConnect: false,
    homeTarget: 'system',
    homeDelayMs: 0,
    handoffEnabled: true,
    handoffSuspend: false,
    switchDelayMs: 250
  };

  var DEMO = false;
  try {
    DEMO = !!(root.__JUNCTION_DEMO__ ||
      (root.location && /(?:^|[?&])demo=1(?:&|$)/.test(root.location.search)));
  } catch (error) {
    DEMO = false;
  }

  var demo = {
    running: true,
    connected: true,
    autostart: true,
    homeTarget: 'system',
    homeDelayMs: 0,
    profileSaved: false
  };

  /* ---------- small utilities ---------- */

  function hasBridge() {
    return typeof root.PalmServiceBridge === 'function';
  }

  function shq(value) {
    return "'" + String(value).replace(/'/g, "'\\''") + "'";
  }

  function errorWithOutput(message, output) {
    var error = new Error(message);
    error.output = output || '';
    return error;
  }

  function parseJson(text) {
    if (!text) return null;
    try {
      return JSON.parse(text);
    } catch (error) {
      return null;
    }
  }

  function clampInt(value, min, max, fallback) {
    var number = Number(value);
    if (!isFinite(number)) return fallback;
    number = Math.round(number);
    if (number < min) return min;
    if (number > max) return max;
    return number;
  }

  function isHostValid(host) {
    return /^[A-Za-z0-9._:-]+$/.test(String(host || '').trim());
  }

  function isNameValid(name) {
    return /^[A-Za-z0-9._-]+$/.test(String(name || '').trim());
  }

  function isAppIdValid(id) {
    return /^[A-Za-z0-9._-]{3,180}$/.test(String(id || ''));
  }

  function formatMs(value) {
    var number = clampInt(value, 0, 60000, 0);
    return number + ' ms';
  }

  function ensureOk(result, label) {
    if (result && typeof result.code === 'number' && isFinite(result.code) && result.code !== 0) {
      throw errorWithOutput(
        label + (result.output ? ': ' + result.output : ' (exit ' + result.code + ')'),
        result.output
      );
    }
    return result;
  }

  /* ---------- settings ---------- */

  function normalizeSettings(raw) {
    var merged = {};
    var key;
    for (key in DEFAULTS) {
      if (Object.prototype.hasOwnProperty.call(DEFAULTS, key)) merged[key] = DEFAULTS[key];
    }
    if (raw && typeof raw === 'object') {
      for (key in DEFAULTS) {
        if (!Object.prototype.hasOwnProperty.call(DEFAULTS, key)) continue;
        if (!Object.prototype.hasOwnProperty.call(raw, key)) continue;
        if (typeof DEFAULTS[key] === 'boolean') {
          merged[key] = Boolean(raw[key]);
        } else if (typeof DEFAULTS[key] === 'number') {
          merged[key] = clampInt(raw[key], 0, key === 'homeDelayMs' ? 5000 : 1000, DEFAULTS[key]);
        } else if (key === 'homeTarget') {
          merged[key] = raw[key] === 'off' ? 'off' : 'system';
        } else {
          merged[key] = String(raw[key]);
        }
      }
    }
    merged.port = String(clampInt(merged.port, 1, 65535, 24800));
    merged.width = String(clampInt(merged.width, 1, 65535, 1920));
    merged.height = String(clampInt(merged.height, 1, 65535, 1080));
    merged.switchDelayMs = clampInt(merged.switchDelayMs, 0, 1000, 250);
    merged.homeDelayMs = clampInt(merged.homeDelayMs, 0, 5000, 0);
    return merged;
  }

  function loadSettings() {
    var stored = null;
    try {
      stored = parseJson(root.localStorage.getItem(STORAGE_KEY));
    } catch (error) {
      stored = null;
    }
    if (stored) {
      var existing = normalizeSettings(stored);
      if (DEMO && !existing.host) existing.host = '192.168.1.20';
      return existing;
    }

    // Migrate the small, non-device-specific subset of the previous app's
    // browser settings. Unknown/legacy fields are deliberately dropped.
    try {
      var legacy = parseJson(root.localStorage.getItem(LEGACY_STORAGE_KEY));
      if (legacy && typeof legacy === 'object') {
        var migrated = normalizeSettings({
          host: legacy.host,
          port: legacy.port,
          screenName: legacy.screenName,
          width: legacy.width,
          height: legacy.height,
          dryRun: legacy.dryRun,
          autoConnect: legacy.autoConnect
        });
        saveSettings(migrated);
        if (DEMO && !migrated.host) migrated.host = '192.168.1.20';
        return migrated;
      }
    } catch (error) {
      /* localStorage can be unavailable on locked-down webOS builds. */
    }
    var defaults = normalizeSettings(null);
    if (DEMO) defaults.host = '192.168.1.20';
    return defaults;
  }

  function saveSettings(settings) {
    var values = normalizeSettings(settings);
    try {
      root.localStorage.setItem(STORAGE_KEY, JSON.stringify(values));
      return true;
    } catch (error) {
      return false;
    }
  }

  function resetSettings() {
    try {
      root.localStorage.removeItem(STORAGE_KEY);
    } catch (error) {
      /* ignore */
    }
    return normalizeSettings(null);
  }

  function validateConnection(raw) {
    var errors = {};
    var host = String(raw.host || '').trim();
    var port = String(raw.port || '').trim();
    var screenName = String(raw.screenName || '').trim();
    var width = String(raw.width || '').trim();
    var height = String(raw.height || '').trim();

    if (!host) errors.host = 'Enter the desktop address.';
    else if (!isHostValid(host)) errors.host = 'Use letters, numbers, dot, colon, underscore or dash only.';
    if (!/^\d+$/.test(port) || Number(port) < 1 || Number(port) > 65535) {
      errors.port = 'Port must be 1–65535.';
    }
    if (!screenName) errors.screenName = 'Enter the Synergy screen name.';
    else if (!isNameValid(screenName)) errors.screenName = 'Use letters, numbers, dot, underscore or dash only.';
    if (!/^\d+$/.test(width) || Number(width) < 1 || Number(width) > 65535) {
      errors.width = 'Width must be 1–65535.';
    }
    if (!/^\d+$/.test(height) || Number(height) < 1 || Number(height) > 65535) {
      errors.height = 'Height must be 1–65535.';
    }
    if (Object.keys(errors).length) {
      return { ok: false, errors: errors };
    }
    return {
      ok: true,
      values: {
        host: host,
        port: port,
        screenName: screenName,
        width: width,
        height: height
      }
    };
  }

  /* ---------- profile ---------- */

  function buildProfile(settings) {
    var values = normalizeSettings(settings);
    return {
      schema: PROFILE_SCHEMA,
      generated_by: APP.name + ' ' + APP.version,
      screen: isNameValid(values.screenName) ? values.screenName : 'webos-tv',
      corner: {
        edge: 'top-left',
        width_px: 2,
        height_px: 2,
        enabled: Boolean(values.handoffEnabled)
      },
      switch_delay_ms: values.switchDelayMs,
      override: {
        suspend: Boolean(values.handoffSuspend)
      },
      notes: 'Synergy 3 custom config approximates a 2x2 corner with 2px edge intervals. Apply on the desktop with physical acceptance; this file does not change the server by itself.'
    };
  }

  /* ---------- TV bridge ---------- */

  function execRoot(command) {
    return new Promise(function (resolve, reject) {
      if (!hasBridge()) {
        reject(errorWithOutput('The TV root helper is unavailable in this preview.'));
        return;
      }
      var bridge = new root.PalmServiceBridge();
      var finished = false;
      var timer = root.setTimeout(function () {
        if (finished) return;
        finished = true;
        reject(errorWithOutput('The root helper timed out after 15 seconds.'));
      }, 15000);

      bridge.onservicecallback = function (message) {
        if (finished) return;
        finished = true;
        root.clearTimeout(timer);
        var response = parseJson(message);
        if (!response) {
          reject(errorWithOutput('The root helper returned an invalid response.'));
          return;
        }
        var stdout = response.stdoutString !== undefined ? response.stdoutString :
          response.stdout !== undefined ? response.stdout :
          response.output !== undefined ? response.output : '';
        var stderr = response.stderrString !== undefined ? response.stderrString :
          response.stderr !== undefined ? response.stderr : '';
        var output = [stdout, stderr].filter(Boolean).join('\n').trim();
        if (response.returnValue === false) {
          reject(errorWithOutput(response.errorText || output || 'The root helper denied the command.', output));
          return;
        }
        var code = response.code !== undefined ? response.code : response.exitCode;
        resolve({ output: output, code: code === undefined ? null : Number(code), raw: response });
      };

      try {
        bridge.call('luna://org.webosbrew.hbchannel.service/exec', JSON.stringify({ command: command }));
      } catch (error) {
        if (finished) return;
        finished = true;
        root.clearTimeout(timer);
        reject(errorWithOutput('Could not reach the root helper: ' + error.message));
      }
    });
  }

  function tvctlShell(args) {
    var dirs = APP_DIRS.map(function (dir) { return shq(dir); }).join(' ');
    return "BASE=''; TVCTL=''; for D in " + dirs + '; do ' +
      'if [ -f "$D/client/tvctl.sh" ]; then BASE="$D"; TVCTL="$D/client/tvctl.sh"; break; fi; ' +
      'if [ -f "$D/tvctl.sh" ]; then BASE="$D"; TVCTL="$D/tvctl.sh"; break; fi; ' +
      'done; ' +
      'if [ -z "$TVCTL" ]; then echo "SynergyWebOS client control script not found" >&2; exit 127; fi; ' +
      'exec sh "$TVCTL" ' + args;
  }

  function demoSnapshot() {
    var state = demo.running ? 'connected' : 'stopped';
    var host = '192.168.1.20';
    var port = '24800';
    var body = [
      '===STATE===',
      demo.running ? 'running pid=4812' : 'stopped',
      demo.running ? JSON.stringify({
        state: demo.connected ? 'connected' : 'connecting',
        pid: 4812,
        server: host,
        port: Number(port),
        screen: 'webos-tv'
      }) : '',
      '===AUTOSTART===',
      demo.autostart ? 'enabled' : 'disabled',
      '===CONFIG===',
      'home_target=' + demo.homeTarget,
      'home_delay_ms=' + demo.homeDelayMs,
      '===LOG===',
      '2026-10-31 03:14:02 INFO Starting Junction client 0.3.2',
      '2026-10-31 03:14:02 INFO Connecting to ' + host + ':' + port + ' as webos-tv',
      '2026-10-31 03:14:02 INFO TCP connected; waiting for Synergy handshake',
      '2026-10-31 03:14:02 INFO Server protocol Synergy 1.4',
      '2026-10-31 03:14:02 INFO Protocol handshake complete',
      '2026-10-31 03:14:02 INFO Using uinput device /dev/uinput',
      '2026-10-31 03:14:03 INFO Pointer entered TV screen',
      '2026-10-31 03:14:19 INFO HOME frame sent',
      '2026-10-31 03:14:27 INFO Pointer left TV screen'
    ].join('\n');
    return body;
  }

  function demoExecute(args) {
    var head = String(args || '').replace(/^'/, '').split(' ')[0];
    var output = '';
    if (args.indexOf('snapshot') === 0) output = demoSnapshot();
    else if (args.indexOf('probe') === 0) {
      output = JSON.stringify({
        app: APP.name,
        version: APP.version,
        is_root: true,
        euid: 0,
        python: '3.12.6',
        uinput_path: '/dev/uinput',
        uinput_access: true,
        uinput_error: null,
        machine: 'aarch64',
        webosbrew_root: true
      }, null, 2) + '\n\nPASS: root + writable uinput are available.';
    } else if (args.indexOf('status') === 0) {
      output = demo.running ? 'running pid=4812\n' + JSON.stringify({
        state: 'connected', pid: 4812, server: '192.168.1.20', port: 24800, screen: 'webos-tv'
      }) : 'stopped';
    } else if (args.indexOf('start') === 0) {
      demo.running = true;
      demo.connected = true;
      output = 'started pid=4812 mode=real log=/var/lib/webosbrew/junction.log';
    } else if (args.indexOf('restart') === 0) {
      output = 'restarted pid=4812 mode=real';
    } else if (args.indexOf('stop') === 0) {
      demo.running = false;
      demo.connected = false;
      output = 'stopped';
    } else if (args.indexOf('autostart-enable') === 0) {
      demo.autostart = true;
      output = 'autostart enabled';
    } else if (args.indexOf('autostart-disable') === 0) {
      demo.autostart = false;
      output = 'autostart disabled';
    } else if (args.indexOf('profile-save') === 0) {
      demo.profileSaved = true;
      output = 'profile saved: /var/lib/webosbrew/junction/handoff-profile.json';
    } else if (args.indexOf('server-status') === 0) {
      output = JSON.stringify({
        success: true,
        data: {
          serviceVersion: 'v3.0.79.1-rc3',
          developers: { switchDelay: 250 },
          override: { shouldOverride: true }
        }
      });
    } else if (args.indexOf('autostart-status') === 0) {
      output = demo.autostart ? 'enabled' : 'disabled';
    } else {
      output = 'demo: ' + args;
    }
    return new Promise(function (resolve) {
      root.setTimeout(function () {
        resolve({ output: output, code: 0, raw: { returnValue: true, demo: true } });
      }, 140);
    });
  }

  function tvctl(args) {
    if (DEMO) return demoExecute(String(args || ''));
    return execRoot(tvctlShell(String(args || '')));
  }

  function startClient(settings, overrides) {
    var values = normalizeSettings(settings);
    var opts = overrides || {};
    var mode = values.dryRun ? 'dry' : 'real';
    var command = opts.restart ? 'restart' : 'start';
    var args = [
      values.host,
      values.port,
      values.screenName,
      mode,
      values.width,
      values.height,
      values.homeTarget,
      String(values.homeDelayMs)
    ].map(shq).join(' ');
    return tvctl(command + ' ' + args).then(function (result) {
      return ensureOk(result, 'Could not start the client');
    });
  }

  function stopClient() {
    return tvctl('stop').then(function (result) {
      return ensureOk(result, 'Could not stop the client');
    });
  }

  function probe() {
    return tvctl('probe').then(function (result) {
      var text = result.output || '';
      var start = text.indexOf('{');
      var end = text.lastIndexOf('}');
      if (start < 0 || end <= start) {
        throw errorWithOutput('The probe did not return JSON.', text);
      }
      var data = parseJson(text.slice(start, end + 1));
      if (!data) throw errorWithOutput('The probe returned malformed JSON.', text);
      return { data: data, text: text, code: result.code };
    });
  }

  function snapshot() {
    return tvctl('snapshot').then(function (result) {
      var output = result.output || '';
      var stateAt = output.indexOf('===STATE===');
      var autoAt = output.indexOf('===AUTOSTART===');
      var configAt = output.indexOf('===CONFIG===');
      var logAt = output.indexOf('===LOG===');
      var stateText = stateAt >= 0 ? output.slice(stateAt + 11, autoAt >= 0 ? autoAt : output.length) : '';
      var autoText = autoAt >= 0 ? output.slice(autoAt + 15, configAt >= 0 ? configAt : (logAt >= 0 ? logAt : output.length)) : '';
      var configText = configAt >= 0 ? output.slice(configAt + 12, logAt >= 0 ? logAt : output.length) : '';
      var logText = logAt >= 0 ? output.slice(logAt + 9) : output;
      var state = null;
      var lines = stateText.split(/\r?\n/);
      for (var i = 0; i < lines.length; i += 1) {
        var line = lines[i].trim();
        if (line.charAt(0) === '{') {
          state = parseJson(line);
          if (state) break;
        }
      }
      var running = /running pid=\d+/.test(stateText) || (state && state.state && state.state !== 'stopped');
      var config = {};
      configText.split(/\r?\n/).forEach(function (line) {
        var match = /^([a-z_]+)=(.*)$/.exec(line.trim());
        if (match) config[match[1]] = match[2];
      });
      return {
        output: output,
        state: state,
        running: Boolean(running),
        autostart: /enabled/.test(autoText),
        autostartRaw: autoText.trim(),
        config: config,
        log: logText.replace(/^\s*\n/, ''),
        code: result.code
      };
    });
  }

  function autostartEnable(settings) {
    var values = normalizeSettings(settings);
    var args = [
      values.host,
      values.port,
      values.screenName,
      values.dryRun ? 'dry' : 'real',
      values.width,
      values.height,
      values.homeTarget,
      String(values.homeDelayMs)
    ].map(shq).join(' ');
    return tvctl('autostart-enable ' + args).then(function (result) {
      return ensureOk(result, 'Could not enable autostart');
    });
  }

  function autostartDisable() {
    return tvctl('autostart-disable').then(function (result) {
      return ensureOk(result, 'Could not disable autostart');
    });
  }

  function profileSave(profile) {
    return tvctl('profile-save ' + shq(JSON.stringify(profile, null, 2))).then(function (result) {
      return ensureOk(result, 'Could not save the profile');
    });
  }

  function serverStatus(host, port) {
    if (!isHostValid(host)) {
      return Promise.reject(new Error('Enter a valid desktop address first.'));
    }
    var args = 'server-status ' + shq(host) + ' ' + shq(String(port || APP.serverApiPort));
    return tvctl(args).then(function (result) {
      ensureOk(result, 'Desktop settings request failed');
      var data = parseJson(result.output);
      if (!data) throw errorWithOutput('The desktop did not return JSON.', result.output);
      return data;
    });
  }

  function luna(uri, params) {
    return new Promise(function (resolve, reject) {
      if (!hasBridge()) {
        reject(new Error('TV services are unavailable in this preview.'));
        return;
      }
      var bridge = new root.PalmServiceBridge();
      var finished = false;
      var timer = root.setTimeout(function () {
        if (finished) return;
        finished = true;
        reject(new Error('TV service timed out.'));
      }, 8000);
      bridge.onservicecallback = function (message) {
        if (finished) return;
        finished = true;
        root.clearTimeout(timer);
        var data = parseJson(message);
        if (!data) {
          reject(new Error('TV service returned an invalid response.'));
          return;
        }
        if (data.returnValue === false) {
          reject(new Error(data.errorText || 'TV service denied the request.'));
          return;
        }
        resolve(data);
      };
      try {
        bridge.call(uri, JSON.stringify(params || {}));
      } catch (error) {
        if (finished) return;
        finished = true;
        root.clearTimeout(timer);
        reject(error);
      }
    });
  }

  var DEMO_APPS = [
    { id: 'demo.youtube', title: 'YouTube', running: true, foreground: true },
    { id: 'demo.twitch', title: 'Twitch', running: false, foreground: false },
    { id: 'demo.browser', title: 'Browser', running: true, foreground: false },
    { id: 'demo.media', title: 'Media Player', running: false, foreground: false },
    { id: 'demo.photos', title: 'Photos', running: false, foreground: false },
    { id: 'demo.games', title: 'Games', running: false, foreground: false },
    { id: 'demo.music', title: 'Music', running: false, foreground: false },
    { id: 'demo.live', title: 'Live TV', running: false, foreground: false }
  ];

  function normalizeApps(items) {
    var seen = Object.create(null);
    var output = [];
    (items || []).forEach(function (item) {
      if (!item) return;
      var id = item.id || item.appId;
      if (!isAppIdValid(id) || seen[id]) return;
      seen[id] = true;
      output.push({
        id: id,
        title: String(item.title || item.appTitle || item.id || 'Untitled app'),
        icon: String(item.icon || item.largeIcon || ''),
        running: Boolean(item.running),
        foreground: Boolean(item.foreground)
      });
    });
    return output;
  }

  function settle(promise) {
    return promise.then(function (value) {
      return { ok: true, value: value };
    }, function (error) {
      return { ok: false, error: error };
    });
  }

  function appIconUrl(app) {
    var icon = String(app.icon || app.largeIcon || '');
    if (!icon) return '';
    if (/^(file|https?):/i.test(icon)) return icon;
    if (icon.charAt(0) === '/') return 'file://' + icon;
    var folder = String(app.folderPath || app.activeFolderPath || '').replace(/\/+$/, '');
    if (!folder) return icon;
    return 'file://' + folder + '/' + icon.replace(/^\/+/, '');
  }

  // Third-party apps are denied listApps/getForegroundAppInfo by the TV's
  // application-manager ACL on this firmware. The Homebrew root helper is
  // already used for tvctl, so ask it for the catalog instead of fabricating
  // an app list or requesting broad permissions.
  function getCatalogRoot() {
    if (!hasBridge()) {
      return Promise.reject(new Error('The TV root helper is unavailable in this preview.'));
    }
    var command = "luna-send -n 1 -f luna://com.webos.applicationManager/listApps '{}'; " +
      "echo; echo '___FG___'; " +
      "luna-send -n 1 -f luna://com.webos.applicationManager/getForegroundAppInfo '{}'; " +
      "echo; echo '___RUN___'; " +
      "luna-send -n 1 -f luna://com.webos.applicationManager/dev/running '{}'";
    return execRoot(command).then(function (result) {
      ensureOk(result, 'The root catalog request failed');
      var parts = String(result.output || '').split('___FG___');
      var runParts = String(parts[1] || '').split('___RUN___');
      var appsData = parseJson(String(parts[0] || '').trim());
      var fgData = parseJson(String(runParts[0] || '').trim());
      var runData = parseJson(String(runParts[1] || '').trim());
      var source = appsData && Array.isArray(appsData.apps) ? appsData.apps : [];
      var foreground = fgData && (fgData.appId || fgData.appID) || '';
      var runningKnown = runData && Array.isArray(runData.running);
      var running = runningKnown ? runData.running.map(function (app) {
        return app.id || app.appId;
      }).filter(Boolean) : [];
      var list = normalizeApps(source.filter(function (app) {
        return app && (app.visible !== false || app.id === 'com.webos.app.home');
      }).map(function (app) {
        var id = app.id || app.appId;
        return {
          id: id,
          title: app.title || app.appTitle || id,
          icon: appIconUrl(app),
          running: running.indexOf(id) >= 0,
          foreground: id === foreground
        };
      }));
      list.sort(function (a, b) {
        return Number(b.foreground) - Number(a.foreground) ||
          Number(b.running) - Number(a.running) ||
          a.title.localeCompare(b.title);
      });
      return {
        apps: list,
        foreground: foreground,
        runningKnown: runningKnown,
        running: running,
        catalogAvailable: source.length > 0,
        source: 'root'
      };
    });
  }

  function getCatalogBridge() {
    var appService = 'luna://com.webos.service.applicationmanager/';
    return Promise.all([
      settle(luna(appService + 'listLaunchPoints', {})),
      settle(luna(appService + 'listApps', {})),
      settle(luna(appService + 'getForegroundAppInfo', {})),
      settle(luna(appService + 'dev/running', {}))
    ]).then(function (results) {
      var launch = results[0].ok ? results[0].value.launchPoints : null;
      var apps = results[1].ok ? results[1].value.apps : null;
      var foreground = results[2].ok ? (results[2].value.appId || results[2].value.appID || '') : '';
      var runningKnown = results[3].ok && Array.isArray(results[3].value.running);
      var running = runningKnown ? results[3].value.running.map(function (app) {
        return app.id || app.appId;
      }).filter(Boolean) : [];
      var source = Array.isArray(launch) && launch.length ? launch : (Array.isArray(apps) ? apps : []);
      var list = normalizeApps(source.map(function (app) {
        return {
          id: app.id || app.appId,
          title: app.title || app.appTitle,
          icon: app.icon || app.largeIcon,
          running: running.indexOf(app.id || app.appId) >= 0,
          foreground: (app.id || app.appId) === foreground
        };
      }));
      list.sort(function (a, b) {
        return Number(b.foreground) - Number(a.foreground) ||
          Number(b.running) - Number(a.running) ||
          a.title.localeCompare(b.title);
      });
      return {
        apps: list,
        foreground: foreground,
        runningKnown: runningKnown,
        catalogAvailable: Array.isArray(launch) || Array.isArray(apps),
        source: 'bridge'
      };
    });
  }

  function getCatalog() {
    if (DEMO) {
      return new Promise(function (resolve) {
        root.setTimeout(function () {
          resolve({
            apps: DEMO_APPS.map(function (app) { return Object.assign({}, app); }),
            foreground: 'demo.youtube',
            runningKnown: true,
            demo: true
          });
        }, 180);
      });
    }
    return getCatalogRoot().then(function (catalog) {
      if (catalog.catalogAvailable && catalog.apps.length) return catalog;
      return getCatalogBridge();
    }, function () {
      return getCatalogBridge();
    });
  }

  function prepareDockCatalog(catalog) {
    var dockDir = '/media/developer/apps/usr/palm/applications/' + APP.dockAppId;
    var payload = 'window.JUNCTION_DOCK_CATALOG = ' + JSON.stringify({
      apps: catalog.apps || [],
      foreground: catalog.foreground || '',
      running: Array.isArray(catalog.running) ? catalog.running : null,
      runningKnown: Boolean(catalog.runningKnown),
      catalogAvailable: true,
      source: 'junction',
      generatedAt: new Date().toISOString()
    }) + ';\n';
    var base64;
    try {
      base64 = root.btoa(root.unescape(root.encodeURIComponent(payload)));
    } catch (error) {
      return Promise.reject(new Error('Could not encode the dock catalog.'));
    }
    var command = "test -d " + shq(dockDir) + " && printf '%s' '" + base64 + "' | base64 -d > " +
      shq(dockDir + '/catalog.js');
    return execRoot(command).then(function (result) {
      ensureOk(result, 'Could not prepare the dock catalog');
      return catalog;
    });
  }

  function launchApp(id) {
    if (!isAppIdValid(id)) return Promise.reject(new Error('Invalid app ID.'));
    if (DEMO) {
      return new Promise(function (resolve) {
        root.setTimeout(function () { resolve({ returnValue: true }); }, 120);
      });
    }
    return luna('luna://com.webos.service.applicationmanager/launch', { id: id });
  }

  function openDock() {
    return getCatalog().then(function (catalog) {
      return prepareDockCatalog(catalog).catch(function () {
        // A stale catalog is still better than refusing to open the dock.
        return catalog;
      });
    }).then(function () {
      return launchApp(APP.dockAppId);
    });
  }

  function getTvAddress() {
    if (DEMO) {
      return new Promise(function (resolve) {
        root.setTimeout(function () { resolve('192.168.1.20'); }, 80);
      });
    }
    return luna('luna://com.palm.connectionmanager/getStatus', {}).then(function (data) {
      var interfaces = [data.wired, data.wifi];
      for (var i = 0; i < interfaces.length; i += 1) {
        var iface = interfaces[i];
        if (iface && iface.state === 'connected' && iface.ipAddress) return iface.ipAddress;
      }
      return 'Unavailable';
    });
  }

  var Junction = {
    APP: APP,
    DEFAULTS: DEFAULTS,
    STORAGE_KEY: STORAGE_KEY,
    PROFILE_SCHEMA: PROFILE_SCHEMA,
    DEMO_APPS: DEMO_APPS,
    DEMO: DEMO,
    hasBridge: hasBridge,
    shq: shq,
    parseJson: parseJson,
    formatMs: formatMs,
    clampInt: clampInt,
    isHostValid: isHostValid,
    isNameValid: isNameValid,
    isAppIdValid: isAppIdValid,
    normalizeSettings: normalizeSettings,
    loadSettings: loadSettings,
    saveSettings: saveSettings,
    resetSettings: resetSettings,
    validateConnection: validateConnection,
    buildProfile: buildProfile,
    startClient: startClient,
    stopClient: stopClient,
    probe: probe,
    snapshot: snapshot,
    autostartEnable: autostartEnable,
    autostartDisable: autostartDisable,
    profileSave: profileSave,
    serverStatus: serverStatus,
    getCatalog: getCatalog,
    getCatalogRoot: getCatalogRoot,
    prepareDockCatalog: prepareDockCatalog,
    launchApp: launchApp,
    openDock: openDock,
    getTvAddress: getTvAddress,
    normalizeApps: normalizeApps
  };

  root.Junction = Junction;
  if (typeof module !== 'undefined' && module.exports) module.exports = Junction;
})(typeof window === 'undefined' ? globalThis : window);
