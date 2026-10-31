/* Junction app shell — UI behaviour, polling and keyboard/mouse interaction.
   Classic script for direct packaging; no framework runtime. */
(function () {
  'use strict';

  var J = window.Junction;
  if (!J) return;

  var els = {};
  var state = {
    settings: J.loadSettings(),
    snapshot: null,
    snapshotError: null,
    snapshotBusy: false,
    probe: null,
    apps: [],
    appsKnown: false,
    appsRunningKnown: false,
    appsStatus: 'idle',
    appsError: '',
    currentView: 'home',
    followLog: true,
    busy: false,
    resetArmed: false,
    resetTimer: null,
    autoConnectTried: false,
    pendingFocus: false,
    loaded: false
  };

  function $(id) {
    return document.getElementById(id);
  }

  function all(selector) {
    return Array.prototype.slice.call(document.querySelectorAll(selector));
  }

  function cacheElements() {
    [
      'globalStatus', 'globalStatusText', 'clock', 'demoBadge', 'tvAddress',
      'rootState', 'versionLabel', 'homeStartLabel', 'boardMode', 'boardHost',
      'boardScreen', 'boardLink', 'boardInput', 'quickInputState', 'quickHomeState',
      'quickAutoState', 'appsHint', 'homeApps', 'linkStatusDot', 'linkStatusText',
      'host', 'port', 'screenName', 'width', 'height', 'dryRun', 'autoConnect',
      'connectionForm', 'formMessage', 'startBtn', 'logMeta', 'logFollow', 'logView',
      'factRoot', 'factPython', 'factUinput', 'factAddress', 'probeNote',
      'autostartBadge', 'autostartNote', 'appSearch', 'appsStatus', 'appGrid',
      'dockMessage', 'saveState', 'homeDelay', 'homeDelayOut', 'homeNote',
      'cornerEnabled', 'cornerOverride', 'switchDelay', 'switchDelayOut',
      'serverState', 'serverDelay', 'serverOverride', 'profilePreview',
      'aboutVersion', 'resetBtn', 'resetNote', 'toast', 'keyflowTarget', 'keyflowSub'
    ].forEach(function (id) {
      els[id] = $(id);
    });
  }

  /* ---------- formatting ---------- */

  function messageOf(error) {
    if (!error) return 'Unknown error.';
    return String(error.message || error);
  }

  function timeNow() {
    try {
      return new Date().toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
    } catch (error) {
      return '--:--';
    }
  }

  function toast(message, kind) {
    if (!els.toast) return;
    window.clearTimeout(toast.timer);
    els.toast.textContent = message;
    els.toast.className = 'toast' + (kind ? ' is-' + kind : '');
    els.toast.hidden = false;
    toast.timer = window.setTimeout(function () {
      els.toast.hidden = true;
    }, kind === 'error' ? 6200 : 4200);
  }

  function setFormMessage(text, kind) {
    if (!els.formMessage) return;
    els.formMessage.textContent = text || '';
    els.formMessage.className = 'form-message' + (kind ? ' is-' + kind : '');
  }

  function setBusy(busy) {
    state.busy = busy;
    document.body.classList.toggle('is-busy', busy);
  }

  /* ---------- link status ---------- */

  function linkInfo() {
    if (!J.hasBridge() && !J.DEMO) {
      return { kind: 'idle', label: 'Preview only', mode: 'Preview', detail: 'TV services unavailable in a browser preview.' };
    }
    if (state.snapshotError && !state.snapshot) {
      return { kind: 'error', label: 'Helper unavailable', mode: 'Error', detail: state.snapshotError };
    }
    if (!state.snapshot) {
      return { kind: 'idle', label: 'Checking link', mode: 'Checking', detail: 'Reading the client status.' };
    }
    var snap = state.snapshot;
    var data = snap.state || {};
    if (!snap.running) {
      return { kind: 'idle', label: 'Link stopped', mode: 'Idle', detail: 'No client process is running.' };
    }
    if (data.state === 'connected') {
      return {
        kind: 'live',
        label: 'Link live',
        mode: 'Live',
        detail: 'PID ' + (data.pid || '—') + (data.server ? ' · ' + data.server + ':' + data.port : '')
      };
    }
    if (data.state === 'connecting') {
      return { kind: 'busy', label: 'Connecting', mode: 'Connecting', detail: 'Waiting for the Synergy handshake.' };
    }
    if (data.state === 'disconnected') {
      return { kind: 'busy', label: 'Reconnecting', mode: 'Reconnecting', detail: 'The link ended; the client will retry.' };
    }
    return {
      kind: 'busy',
      label: 'Client running',
      mode: 'Running',
      detail: 'PID ' + (data.pid || '—')
    };
  }

  function setPill(kind, text) {
    if (!els.globalStatus) return;
    els.globalStatus.className = 'status-pill is-' + kind;
    if (els.globalStatusText) els.globalStatusText.textContent = text;
  }

  function probeInputLabel() {
    if (!state.probe) return 'Not checked';
    return state.probe.is_root && state.probe.uinput_access ? 'Root + uinput' : 'Blocked';
  }

  function renderStatus() {
    var info = linkInfo();
    var snap = state.snapshot;
    var data = (snap && snap.state) || {};

    setPill(info.kind, info.label);
    document.body.classList.toggle('is-live', info.kind === 'live');

    if (els.boardMode) els.boardMode.textContent = info.mode;
    if (els.boardHost) els.boardHost.textContent = state.settings.host || 'Not set';
    if (els.boardScreen) els.boardScreen.textContent = state.settings.screenName || 'webos-tv';
    if (els.boardLink) {
      els.boardLink.textContent = snap && snap.running
        ? (data.state === 'connected' ? 'Live · PID ' + (data.pid || '—') : info.label)
        : 'Not connected';
    }
    if (els.boardInput) els.boardInput.textContent = probeInputLabel();

    if (els.linkStatusText) els.linkStatusText.textContent = info.detail === 'TV services unavailable in a browser preview.'
      ? 'TV services unavailable'
      : (snap && snap.running ? info.label + (data.pid ? ' · PID ' + data.pid : '') : info.label);
    if (els.linkStatusDot) {
      els.linkStatusDot.className = 'status-dot' +
        (info.kind === 'live' ? ' is-live' : info.kind === 'error' ? ' is-bad' : '');
    }

    if (els.homeStartLabel) els.homeStartLabel.textContent = snap && snap.running ? 'Manage link' : 'Start link';
    if (els.quickInputState) {
      els.quickInputState.textContent = probeInputLabel();
      els.quickInputState.className = 'chip' +
        (state.probe && state.probe.is_root && state.probe.uinput_access ? ' is-good' : '');
    }
    if (els.quickHomeState) {
      var home = state.settings.homeTarget === 'off' ? 'Off' : 'System home';
      els.quickHomeState.textContent = home;
      els.quickHomeState.className = 'chip' + (state.settings.homeTarget === 'off' ? '' : ' is-good');
    }
    if (els.quickAutoState) {
      var known = snap || state.snapshotError;
      els.quickAutoState.textContent = snap ? (snap.autostart ? 'On' : 'Off') : (known ? 'Unknown' : '…');
      els.quickAutoState.className = 'chip' + (snap && snap.autostart ? ' is-good' : '');
    }

    if (els.logMeta) {
      if (snap && snap.running) {
        els.logMeta.textContent = 'PID ' + (data.pid || '—') + ' · ' +
          (state.settings.host || 'unknown host') + ':' + state.settings.port +
          ' · ' + state.settings.screenName;
      } else {
        els.logMeta.textContent = J.hasBridge() || J.DEMO ? 'No active client' : 'Root helper unavailable';
      }
    }

    if (els.rootState) {
      if (!state.probe) {
        els.rootState.textContent = J.hasBridge() || J.DEMO ? 'Not checked' : 'Preview only';
        els.rootState.className = 'is-muted';
      } else if (state.probe.is_root && state.probe.uinput_access) {
        els.rootState.textContent = 'Ready';
        els.rootState.className = 'is-good';
      } else {
        els.rootState.textContent = 'Blocked';
        els.rootState.className = 'is-bad';
      }
    }

    renderHomeNote();
  }

  function renderHomeNote() {
    if (!els.homeNote) return;
    var snap = state.snapshot;
    var active = snap && snap.config;
    if (snap && snap.running && active && (active.home_target || active.home_delay_ms)) {
      var target = active.home_target === 'off' ? 'Off' : 'System home';
      els.homeNote.textContent = 'Active link: ' + target + ' · ' + (active.home_delay_ms || 0) +
        ' ms hold. Saved changes apply on the next client start.';
      return;
    }
    els.homeNote.textContent = 'Saved values apply the next time the client starts.';
  }

  /* ---------- log ---------- */

  function isLogNearBottom() {
    if (!els.logView) return true;
    return els.logView.scrollTop + els.logView.clientHeight >= els.logView.scrollHeight - 28;
  }

  function renderLog(text, follow) {
    if (!els.logView) return;
    var shouldFollow = follow || isLogNearBottom();
    var previousTop = els.logView.scrollTop;
    var next = (text || '').trim() || 'No client log yet.';
    if (els.logView.textContent !== next) {
      els.logView.textContent = next;
      if (shouldFollow) {
        els.logView.scrollTop = els.logView.scrollHeight;
      } else {
        els.logView.scrollTop = previousTop;
      }
    }
    state.followLog = shouldFollow;
    if (els.logFollow) {
      els.logFollow.textContent = shouldFollow ? 'Following' : 'Paused';
      els.logFollow.className = 'follow-state' + (shouldFollow ? '' : ' is-paused');
    }
  }

  function renderSnapshot(snap) {
    state.snapshot = snap;
    state.snapshotError = null;
    renderStatus();

    if (els.autostartBadge) {
      els.autostartBadge.textContent = snap.autostart ? 'On' : 'Off';
      els.autostartBadge.className = 'chip' + (snap.autostart ? ' is-good' : '');
    }
    if (els.autostartNote) {
      els.autostartNote.textContent = snap.autostart
        ? 'Enabled through the Homebrew startup hook. The current link is untouched.'
        : 'Autostart is not enabled. Enable it to bring the link up at boot.';
    }

    renderLog(snap.log, state.followLog);

    if (!state.autoConnectTried && state.settings.autoConnect && state.settings.host) {
      state.autoConnectTried = true;
      if (!snap.running) startLink({ auto: true });
    }
  }

  function refreshSnapshot(options) {
    var opts = options || {};
    if (state.snapshotBusy) return Promise.resolve();
    state.snapshotBusy = true;
    return J.snapshot().then(function (snap) {
      state.loaded = true;
      renderSnapshot(snap);
      return snap;
    }).catch(function (error) {
      state.loaded = true;
      state.snapshot = null;
      state.snapshotError = messageOf(error);
      renderStatus();
      if (els.autostartBadge && !state.snapshot) {
        els.autostartBadge.textContent = 'Unknown';
        els.autostartBadge.className = 'chip';
      }
      if (els.autostartNote) {
        els.autostartNote.textContent = 'Status needs the TV root helper.';
      }
      if (!state.snapshot) {
        renderLog('TV root helper unavailable.\n' + state.snapshotError, true);
      }
      if (!opts.silent) {
        toast('Could not read the client status: ' + state.snapshotError, 'error');
      }
      return null;
    }).then(function (snap) {
      state.snapshotBusy = false;
      return snap;
    });
  }

  function startPolling() {
    window.clearInterval(startPolling.timer);
    startPolling.timer = window.setInterval(function () {
      if (document.hidden) return;
      refreshSnapshot({ silent: true });
    }, 4000);
  }

  /* ---------- connection form ---------- */

  function readForm() {
    return {
      host: els.host ? els.host.value : '',
      port: els.port ? els.port.value : '',
      screenName: els.screenName ? els.screenName.value : '',
      width: els.width ? els.width.value : '',
      height: els.height ? els.height.value : ''
    };
  }

  function hydrateConnectionForm() {
    var s = state.settings;
    if (els.host) els.host.value = s.host;
    if (els.port) els.port.value = s.port;
    if (els.screenName) els.screenName.value = s.screenName;
    if (els.width) els.width.value = s.width;
    if (els.height) els.height.value = s.height;
    if (els.dryRun) els.dryRun.checked = Boolean(s.dryRun);
    if (els.autoConnect) els.autoConnect.checked = Boolean(s.autoConnect);
    renderStatus();
  }

  function persistForm() {
    var values = readForm();
    state.settings.host = values.host.trim();
    state.settings.port = values.port.trim();
    state.settings.screenName = values.screenName.trim();
    state.settings.width = values.width.trim();
    state.settings.height = values.height.trim();
    if (els.dryRun) state.settings.dryRun = els.dryRun.checked;
    if (els.autoConnect) state.settings.autoConnect = els.autoConnect.checked;
    J.saveSettings(state.settings);
    renderStatus();
  }

  function markInvalid(fields) {
    ['host', 'port', 'screenName', 'width', 'height'].forEach(function (id) {
      if (!els[id]) return;
      var invalid = Boolean(fields && fields[id]);
      els[id].classList.toggle('is-invalid', invalid);
      if (invalid) els[id].setAttribute('aria-invalid', 'true');
      else els[id].removeAttribute('aria-invalid');
    });
    if (fields) {
      var first = ['host', 'port', 'screenName', 'width', 'height'].find(function (id) {
        return fields[id];
      });
      if (first && els[first]) {
        els[first].focus();
        els[first].select();
      }
    }
  }

  function markStarting() {
    var snap = state.snapshot || {};
    snap.running = true;
    snap.state = {
      state: 'connecting',
      pid: snap.state && snap.state.pid ? snap.state.pid : null,
      server: state.settings.host,
      port: Number(state.settings.port),
      screen: state.settings.screenName
    };
    state.snapshot = snap;
    state.snapshotError = null;
    renderStatus();
  }

  function startLink(options) {
    var opts = options || {};
    var values = readForm();
    var valid = J.validateConnection(values);
    if (!valid.ok) {
      var firstError = valid.errors[Object.keys(valid.errors)[0]];
      markInvalid(valid.errors);
      setFormMessage(firstError, 'error');
      if (!opts.auto) go('link');
      return Promise.resolve(false);
    }
    markInvalid(null);
    state.settings.host = valid.values.host;
    state.settings.port = valid.values.port;
    state.settings.screenName = valid.values.screenName;
    state.settings.width = valid.values.width;
    state.settings.height = valid.values.height;
    if (els.dryRun) state.settings.dryRun = els.dryRun.checked;
    if (els.autoConnect) state.settings.autoConnect = els.autoConnect.checked;
    J.saveSettings(state.settings);
    if (state.busy) return Promise.resolve(false);
    setBusy(true);
    setFormMessage(opts.auto ? 'Connecting with saved details…' : 'Starting the Junction client…', '');
    var wasRunning = Boolean(state.snapshot && state.snapshot.running);
    return J.startClient(state.settings, { restart: wasRunning }).then(function (result) {
      var output = String((result && result.output) || '');
      if (/already running/i.test(output)) {
        setFormMessage('A client is already running. Use Restart link in Settings to apply new details.', '');
        toast('Link is already running.');
        markStarting();
      } else {
        setFormMessage(wasRunning ? 'Link restarted with the saved details.' : 'Link started. The TV should connect in a moment.', 'good');
        toast(wasRunning ? 'Link restarted.' : 'Link starting…', 'good');
        markStarting();
      }
      setBusy(false);
      window.setTimeout(function () { refreshSnapshot({ silent: true }); }, 700);
      return true;
    }).catch(function (error) {
      setBusy(false);
      var message = messageOf(error);
      setFormMessage('Could not start the link: ' + message, 'error');
      toast('Start failed: ' + message, 'error');
      return false;
    });
  }

  function stopLink() {
    setBusy(true);
    return J.stopClient().then(function () {
      setBusy(false);
      setFormMessage('Client stopped.', '');
      toast('Link stopped.');
      if (state.snapshot) {
        state.snapshot.running = false;
        state.snapshot.state = { state: 'stopped' };
        renderStatus();
      }
      return refreshSnapshot({ silent: true });
    }).catch(function (error) {
      setBusy(false);
      var message = messageOf(error);
      setFormMessage('Could not stop the link: ' + message, 'error');
      toast('Stop failed: ' + message, 'error');
      return null;
    });
  }

  function restartLink() {
    if (state.busy) return;
    var snap = state.snapshot;
    if (!snap || !snap.running) {
      toast('No link is running. Start it from Link.', 'error');
      go('link');
      return;
    }
    var values = readForm();
    var valid = J.validateConnection(values);
    if (!valid.ok) {
      toast(valid.errors[Object.keys(valid.errors)[0]], 'error');
      go('link');
      return;
    }
    persistForm();
    setBusy(true);
    setFormMessage('Restarting the link with saved home settings…', '');
    J.startClient(state.settings, { restart: true }).then(function () {
      setBusy(false);
      setFormMessage('Link restarted with the saved home settings.', 'good');
      toast('Link restarted.');
      markStarting();
      return refreshSnapshot({ silent: true });
    }).catch(function (error) {
      setBusy(false);
      var message = messageOf(error);
      setFormMessage('Could not restart the link: ' + message, 'error');
      toast('Restart failed: ' + message, 'error');
    });
  }

  /* ---------- probe ---------- */

  function runProbe(options) {
    var opts = options || {};
    var lock = !opts.silent;
    if (state.busy && lock) return Promise.resolve();
    if (lock) setBusy(true);
    if (els.probeNote) els.probeNote.textContent = 'Probing root, Python and uinput…';
    return J.probe().then(function (result) {
      state.probe = result.data;
      if (lock) setBusy(false);
      var data = result.data;
      if (els.factRoot) els.factRoot.textContent = data.is_root ? 'Root · uid ' + data.euid : 'Not root · uid ' + data.euid;
      if (els.factPython) els.factPython.textContent = data.python || 'Missing';
      if (els.factUinput) els.factUinput.textContent = data.uinput_access ? 'Writable · ' + data.uinput_path : (data.uinput_path || 'Missing');
      if (els.factAddress) els.factAddress.textContent = els.tvAddress ? els.tvAddress.textContent : 'Unavailable';
      if (els.probeNote) {
        els.probeNote.textContent = data.is_root && data.uinput_access
          ? 'Root and a writable uinput device are available. Real input injection can be attempted.'
          : 'Real input injection is blocked. Check root access and /dev/uinput before starting a real link.';
      }
      renderStatus();
      if (!opts.silent) toast(data.is_root && data.uinput_access ? 'TV backend is ready.' : 'TV backend is blocked.', data.is_root && data.uinput_access ? 'good' : 'error');
      return data;
    }).catch(function (error) {
      if (lock) setBusy(false);
      var message = messageOf(error);
      state.probe = null;
      if (els.factRoot) els.factRoot.textContent = 'Unavailable';
      if (els.factPython) els.factPython.textContent = 'Unknown';
      if (els.factUinput) els.factUinput.textContent = 'Unknown';
      if (els.probeNote) els.probeNote.textContent = 'Probe failed: ' + message;
      renderStatus();
      if (!opts.silent) toast('Probe failed: ' + message, 'error');
      return null;
    });
  }

  /* ---------- settings ---------- */

  function hydrateSettings() {
    var s = state.settings;
    var radios = all('input[name="homeTarget"]');
    radios.forEach(function (radio) {
      radio.checked = radio.value === s.homeTarget;
      var card = radio.closest('.radio-card');
      if (card) card.classList.toggle('is-selected', radio.checked);
    });
    if (els.homeDelay) els.homeDelay.value = String(s.homeDelayMs);
    if (els.homeDelayOut) els.homeDelayOut.textContent = J.formatMs(s.homeDelayMs);
    if (els.cornerEnabled) els.cornerEnabled.checked = Boolean(s.handoffEnabled);
    if (els.cornerOverride) els.cornerOverride.checked = Boolean(s.handoffSuspend);
    if (els.switchDelay) els.switchDelay.value = String(s.switchDelayMs);
    if (els.switchDelayOut) els.switchDelayOut.textContent = J.formatMs(s.switchDelayMs);
    if (els.keyflowTarget) els.keyflowTarget.textContent = s.homeTarget === 'off' ? 'No action' : 'System home';
    if (els.keyflowSub) els.keyflowSub.textContent = s.homeTarget === 'off' ? 'Key is consumed' : 'TV home screen';
    if (els.versionLabel) els.versionLabel.textContent = J.APP.version;
    if (els.aboutVersion) els.aboutVersion.textContent = J.APP.version;
    renderProfile();
  }

  function renderProfile() {
    if (!els.profilePreview) return;
    els.profilePreview.textContent = JSON.stringify(J.buildProfile(state.settings), null, 2);
  }

  function updateSetting(key, value) {
    state.settings[key] = value;
    J.saveSettings(state.settings);
    renderProfile();
    renderStatus();
    if (els.saveState) {
      els.saveState.textContent = 'Saved locally';
      els.saveState.className = 'save-state is-good';
      window.clearTimeout(updateSetting.timer);
      updateSetting.timer = window.setTimeout(function () {
        if (els.saveState) els.saveState.textContent = 'Saved locally';
      }, 1400);
    }
  }

  function saveHomeSettings() {
    var target = all('input[name="homeTarget"]').filter(function (radio) { return radio.checked; })[0];
    updateSetting('homeTarget', target && target.value === 'off' ? 'off' : 'system');
    if (els.homeDelay) updateSetting('homeDelayMs', J.clampInt(els.homeDelay.value, 0, 5000, 0));
    hydrateSettings();
    toast('Home key settings saved. Restart the link to apply them.', 'good');
  }

  function handleHandoffChange() {
    if (els.cornerEnabled) updateSetting('handoffEnabled', els.cornerEnabled.checked);
    if (els.cornerOverride) updateSetting('handoffSuspend', els.cornerOverride.checked);
    if (els.switchDelay) updateSetting('switchDelayMs', J.clampInt(els.switchDelay.value, 0, 1000, 250));
    if (els.switchDelayOut) els.switchDelayOut.textContent = J.formatMs(state.settings.switchDelayMs);
    renderProfile();
  }

  function copyProfile() {
    var text = JSON.stringify(J.buildProfile(state.settings), null, 2);
    function fallback() {
      try {
        var area = document.createElement('textarea');
        area.value = text;
        area.style.position = 'fixed';
        area.style.opacity = '0';
        document.body.appendChild(area);
        area.select();
        var ok = document.execCommand('copy');
        document.body.removeChild(area);
        toast(ok ? 'Profile JSON copied.' : 'Copy unavailable. The profile is shown below.', ok ? 'good' : 'error');
      } catch (error) {
        toast('Copy unavailable. The profile is shown below.', 'error');
      }
    }
    if (navigator.clipboard && navigator.clipboard.writeText) {
      navigator.clipboard.writeText(text).then(function () {
        toast('Profile JSON copied.', 'good');
      }).catch(fallback);
    } else {
      fallback();
    }
  }

  function saveProfile() {
    if (state.busy) return;
    setBusy(true);
    J.profileSave(J.buildProfile(state.settings)).then(function (result) {
      setBusy(false);
      toast('Profile saved to the TV at /var/lib/webosbrew/junction/handoff-profile.json.', 'good');
      return result;
    }).catch(function (error) {
      setBusy(false);
      toast('Could not save the profile: ' + messageOf(error), 'error');
    });
  }

  function readServer() {
    if (state.busy) return;
    persistForm();
    var values = readForm();
    if (!values.host || !J.isHostValid(values.host)) {
      toast('Enter the desktop host in Link first.', 'error');
      go('link');
      return;
    }
    setBusy(true);
    if (els.serverState) els.serverState.textContent = 'Reading…';
    J.serverStatus(values.host, J.APP.serverApiPort).then(function (response) {
      setBusy(false);
      var data = response.data || response;
      if (els.serverState) els.serverState.textContent = 'Reachable' + (data.serviceVersion ? ' · ' + data.serviceVersion : '');
      if (els.serverDelay) {
        var delay = data.developers && data.developers.switchDelay;
        els.serverDelay.textContent = delay === undefined || delay === null ? 'Unknown' : delay + ' ms';
      }
      if (els.serverOverride) {
        var override = data.override && data.override.shouldOverride;
        els.serverOverride.textContent = override === undefined ? 'Unknown' : override ? 'Custom config on' : 'Off';
      }
      toast('Desktop settings read. No changes were made.', 'good');
    }).catch(function (error) {
      setBusy(false);
      if (els.serverState) els.serverState.textContent = 'Unreachable';
      if (els.serverDelay) els.serverDelay.textContent = '—';
      if (els.serverOverride) els.serverOverride.textContent = '—';
      toast('Could not read the desktop settings: ' + messageOf(error), 'error');
    });
  }

  function resetLocalSettings() {
    if (!state.resetArmed) {
      state.resetArmed = true;
      if (els.resetBtn) els.resetBtn.textContent = 'Click again to confirm reset';
      if (els.resetNote) els.resetNote.textContent = 'This clears the saved form, home-key and handoff values on this TV. The active client keeps running.';
      window.clearTimeout(state.resetTimer);
      state.resetTimer = window.setTimeout(function () {
        state.resetArmed = false;
        if (els.resetBtn) els.resetBtn.textContent = 'Reset local settings';
        if (els.resetNote) els.resetNote.textContent = 'Resetting removes this app’s saved form values from the TV. It does not stop the link or disable autostart.';
      }, 6000);
      return;
    }
    state.resetArmed = false;
    window.clearTimeout(state.resetTimer);
    state.settings = J.resetSettings();
    hydrateConnectionForm();
    hydrateSettings();
    if (els.resetBtn) els.resetBtn.textContent = 'Reset local settings';
    if (els.resetNote) els.resetNote.textContent = 'Local settings reset. The active client and autostart are unchanged.';
    toast('Local settings reset.', 'good');
    renderStatus();
  }

  /* ---------- apps ---------- */

  function letterFor(title) {
    var text = String(title || '?').trim();
    return text ? text.charAt(0).toUpperCase() : '?';
  }

  function makeAvatar(app, className) {
    var avatar = document.createElement('span');
    avatar.className = className || 'app-avatar';
    if (app.icon) {
      var image = document.createElement('img');
      image.alt = '';
      image.loading = 'lazy';
      image.src = app.icon;
      image.addEventListener('error', function () {
        avatar.textContent = letterFor(app.title);
      });
      avatar.appendChild(image);
    } else {
      avatar.textContent = letterFor(app.title);
    }
    return avatar;
  }

  function appStatusLabel(app) {
    if (app.id === J.APP.id) return 'This app';
    if (app.foreground) return 'Foreground';
    if (app.running) return 'Running';
    return state.appsRunningKnown ? 'Open' : 'TV app';
  }

  function openApp(app) {
    if (!app) return;
    if (app.id === J.APP.id) {
      go('home');
      return;
    }
    toast('Opening ' + app.title + '…');
    J.launchApp(app.id).then(function () {
      toast(app.title + ' launch requested.', 'good');
      window.setTimeout(function () { refreshSnapshot({ silent: true }); }, 1200);
    }).catch(function (error) {
      toast('Could not open ' + app.title + ': ' + messageOf(error), 'error');
    });
  }

  function makeAppCard(app) {
    var card = document.createElement('button');
    card.type = 'button';
    card.className = 'app-card' + (app.foreground ? ' is-foreground' : '');
    card.setAttribute('aria-label', 'Open ' + app.title + (app.foreground ? ' (foreground)' : ''));
    card.dataset.appId = app.id;
    if (app.foreground) {
      var badge = document.createElement('span');
      badge.className = 'app-badge foreground';
      badge.setAttribute('aria-hidden', 'true');
      card.appendChild(badge);
    } else if (app.running) {
      var running = document.createElement('span');
      running.className = 'app-badge';
      running.setAttribute('aria-hidden', 'true');
      card.appendChild(running);
    }
    card.appendChild(makeAvatar(app));
    var copy = document.createElement('span');
    var title = document.createElement('strong');
    title.textContent = app.title;
    var sub = document.createElement('small');
    sub.textContent = appStatusLabel(app);
    copy.appendChild(title);
    copy.appendChild(sub);
    card.appendChild(copy);
    card.addEventListener('click', function () { openApp(app); });
    return card;
  }

  function renderApps() {
    if (!els.appGrid) return;
    var query = (els.appSearch && els.appSearch.value || '').trim().toLowerCase();
    var apps = state.apps.filter(function (app) {
      return !query || app.title.toLowerCase().indexOf(query) >= 0;
    });
    els.appGrid.textContent = '';

    if (state.appsStatus === 'loading' && !state.apps.length) {
      var loading = document.createElement('p');
      loading.className = 'empty-note';
      loading.textContent = 'Loading the TV app catalog…';
      els.appGrid.appendChild(loading);
      return;
    }
    if (!apps.length) {
      var empty = document.createElement('p');
      empty.className = 'empty-note';
      empty.textContent = state.appsError
        ? state.appsError
        : query
          ? 'No apps match “' + query + '”.'
          : 'The TV did not return an app catalog.';
      els.appGrid.appendChild(empty);
      return;
    }
    var fragment = document.createDocumentFragment();
    apps.forEach(function (app) { fragment.appendChild(makeAppCard(app)); });
    els.appGrid.appendChild(fragment);
  }

  function renderHomeApps() {
    if (!els.homeApps) return;
    els.homeApps.textContent = '';
    if (!state.apps.length) {
      var empty = document.createElement('p');
      empty.className = 'empty-note';
      empty.textContent = state.appsError || (state.appsStatus === 'loading'
        ? 'Loading the TV app catalog…'
        : 'Open Apps to browse this TV’s catalog.');
      els.homeApps.appendChild(empty);
      return;
    }
    var fragment = document.createDocumentFragment();
    state.apps.slice(0, 6).forEach(function (app) {
      var button = document.createElement('button');
      button.type = 'button';
      button.className = 'mini-app';
      button.setAttribute('aria-label', 'Open ' + app.title);
      button.appendChild(makeAvatar(app, 'app-avatar'));
      var copy = document.createElement('span');
      var title = document.createElement('strong');
      title.textContent = app.title;
      var sub = document.createElement('small');
      sub.textContent = app.foreground ? 'Foreground' : app.running ? 'Running' : 'TV app';
      copy.appendChild(title);
      copy.appendChild(sub);
      button.appendChild(copy);
      button.addEventListener('click', function () { openApp(app); });
      fragment.appendChild(button);
    });
    els.homeApps.appendChild(fragment);
  }

  function updateAppsCopy() {
    var count = state.apps.length;
    var running = state.apps.filter(function (app) { return app.running || app.foreground; }).length;
    if (els.appsHint) {
      els.appsHint.textContent = state.appsError
        ? 'App catalog unavailable'
        : state.appsStatus === 'loading'
          ? 'Loading app catalog…'
          : count + ' apps' + (state.appsRunningKnown ? ' · ' + running + ' running' : '');
    }
    if (els.appsStatus) {
      els.appsStatus.textContent = state.appsError
        ? state.appsError
        : state.appsStatus === 'loading'
          ? 'Loading app catalog…'
          : count + ' apps available' +
            (state.appsRunningKnown ? ' · running state from the TV' : ' · running state unavailable');
    }
  }

  function loadApps(options) {
    var opts = options || {};
    if (state.appsStatus === 'loading') return Promise.resolve();
    state.appsStatus = 'loading';
    updateAppsCopy();
    renderApps();
    return J.getCatalog().then(function (result) {
      state.apps = result.apps || [];
      state.appsRunningKnown = Boolean(result.runningKnown);
      state.appsStatus = 'ready';
      state.appsError = '';
      updateAppsCopy();
      renderApps();
      renderHomeApps();
      if (!opts.silent) toast('App catalog refreshed.', 'good');
      return result;
    }).catch(function (error) {
      state.appsStatus = 'error';
      state.appsError = messageOf(error);
      updateAppsCopy();
      renderApps();
      renderHomeApps();
      if (!opts.silent) toast('Could not read apps: ' + state.appsError, 'error');
      return null;
    });
  }

  function launchDock() {
    if (els.dockMessage) els.dockMessage.textContent = 'Requesting the dock…';
    J.openDock().then(function () {
      if (els.dockMessage) els.dockMessage.textContent = 'Dock launch requested. If it does not appear, install the companion package.';
      toast('Dock launch requested.', 'good');
    }).catch(function (error) {
      if (els.dockMessage) els.dockMessage.textContent = 'Dock unavailable: ' + messageOf(error) + '. The built-in app grid still works.';
      toast('Dock unavailable: ' + messageOf(error), 'error');
    });
  }

  /* ---------- navigation ---------- */

  var VIEWS = { home: true, link: true, apps: true, settings: true };

  function currentHashView() {
    var hash = (window.location.hash || '').replace(/^#/, '');
    return VIEWS[hash] ? hash : 'home';
  }

  function go(view, focus) {
    if (!VIEWS[view]) return;
    if (view === state.currentView && window.location.hash === '#' + view) {
      if (focus) focusView(view);
      return;
    }
    if (window.location.hash !== '#' + view) {
      state.pendingFocus = Boolean(focus);
      window.location.hash = view;
      return;
    }
    renderView(view, focus);
  }

  function focusView(view) {
    var heading = document.querySelector('[data-panel="' + view + '"] h1');
    if (heading) heading.focus();
  }

  function renderView(view, focus) {
    state.currentView = view;
    all('[data-panel]').forEach(function (panel) {
      panel.hidden = panel.getAttribute('data-panel') !== view;
    });
    all('.nav-item').forEach(function (item) {
      var active = item.getAttribute('data-go') === view;
      if (active) item.setAttribute('aria-current', 'page');
      else item.removeAttribute('aria-current');
    });
    var main = $('main');
    if (main) main.scrollTop = 0;
    if (focus) focusView(view);
    if (view === 'apps' && !state.apps.length && state.appsStatus !== 'error') loadApps({ silent: true });
  }

  function onHashChange() {
    renderView(currentHashView(), state.pendingFocus);
    state.pendingFocus = false;
  }

  /* ---------- wiring ---------- */

  function goToAppsAndFocus() {
    go('apps', true);
    window.setTimeout(function () {
      if (els.appSearch) els.appSearch.focus();
    }, 80);
  }

  function wireEvents() {
    document.addEventListener('click', function (event) {
      var goEl = event.target.closest('[data-go]');
      if (goEl) {
        event.preventDefault();
        go(goEl.getAttribute('data-go'), true);
        return;
      }

      var actionEl = event.target.closest('[data-action]');
      if (!actionEl) return;
      var action = actionEl.getAttribute('data-action');

      if (action === 'home-start') {
        if (state.snapshot && state.snapshot.running) {
          go('link', true);
          return;
        }
        if (!state.settings.host && els.host) {
          go('link', true);
          window.setTimeout(function () { els.host.focus(); }, 80);
          toast('Enter the desktop host to start the link.');
          return;
        }
        startLink();
      } else if (action === 'stop') {
        stopLink();
      } else if (action === 'probe') {
        runProbe();
      } else if (action === 'refresh') {
        refreshSnapshot({ silent: false });
      } else if (action === 'clear-log') {
        if (els.logView) els.logView.textContent = 'Log view cleared. New client output will appear here.';
        state.followLog = true;
      } else if (action === 'autostart-on') {
        enableAutostart();
      } else if (action === 'autostart-off') {
        disableAutostart();
      } else if (action === 'save-home') {
        saveHomeSettings();
      } else if (action === 'restart-link') {
        restartLink();
      } else if (action === 'save-profile') {
        saveProfile();
      } else if (action === 'copy-profile') {
        copyProfile();
      } else if (action === 'read-server') {
        readServer();
      } else if (action === 'refresh-apps') {
        loadApps();
      } else if (action === 'open-dock') {
        launchDock();
      } else if (action === 'reset-settings') {
        resetLocalSettings();
      }
    });

    if (els.connectionForm) {
      els.connectionForm.addEventListener('submit', function (event) {
        event.preventDefault();
        startLink();
      });
      ['host', 'port', 'screenName', 'width', 'height'].forEach(function (id) {
        if (!els[id]) return;
        els[id].addEventListener('change', function () {
          persistForm();
          markInvalid(null);
        });
      });
      if (els.dryRun) els.dryRun.addEventListener('change', persistForm);
      if (els.autoConnect) els.autoConnect.addEventListener('change', persistForm);
    }

    all('input[name="homeTarget"]').forEach(function (radio) {
      radio.addEventListener('change', function () {
        all('.radio-card').forEach(function (card) {
          var input = card.querySelector('input');
          card.classList.toggle('is-selected', Boolean(input && input.checked));
        });
        if (els.keyflowTarget) {
          els.keyflowTarget.textContent = radio.value === 'off' && radio.checked ? 'No action' : 'System home';
        }
        if (els.keyflowSub) {
          els.keyflowSub.textContent = radio.value === 'off' && radio.checked ? 'Key is consumed' : 'TV home screen';
        }
      });
    });

    if (els.homeDelay) {
      els.homeDelay.addEventListener('input', function () {
        if (els.homeDelayOut) els.homeDelayOut.textContent = J.formatMs(els.homeDelay.value);
      });
      els.homeDelay.addEventListener('change', function () {
        updateSetting('homeDelayMs', J.clampInt(els.homeDelay.value, 0, 5000, 0));
      });
    }
    if (els.switchDelay) {
      els.switchDelay.addEventListener('input', function () {
        if (els.switchDelayOut) els.switchDelayOut.textContent = J.formatMs(els.switchDelay.value);
        renderProfile();
      });
      els.switchDelay.addEventListener('change', handleHandoffChange);
    }
    ['cornerEnabled', 'cornerOverride'].forEach(function (id) {
      if (els[id]) els[id].addEventListener('change', handleHandoffChange);
    });

    if (els.appSearch) {
      els.appSearch.addEventListener('input', renderApps);
      els.appSearch.addEventListener('keydown', function (event) {
        if (event.key === 'Escape') {
          els.appSearch.value = '';
          renderApps();
          event.stopPropagation();
        }
      });
    }

    if (els.logView) {
      els.logView.addEventListener('scroll', function () {
        var follow = isLogNearBottom();
        state.followLog = follow;
        if (els.logFollow) {
          els.logFollow.textContent = follow ? 'Following' : 'Paused';
          els.logFollow.className = 'follow-state' + (follow ? '' : ' is-paused');
        }
      });
    }

    document.addEventListener('keydown', function (event) {
      var target = event.target;
      var editable = target && (target.tagName === 'INPUT' || target.tagName === 'TEXTAREA' ||
        target.tagName === 'SELECT' || target.isContentEditable);

      if (event.key === 'Escape') {
        if (editable) {
          if (els.appSearch && target === els.appSearch && els.appSearch.value) {
            els.appSearch.value = '';
            renderApps();
          }
          target.blur();
        }
        return;
      }

      if (editable || event.ctrlKey || event.metaKey || event.altKey) return;

      if (/^[1-4]$/.test(event.key)) {
        var map = { '1': 'home', '2': 'link', '3': 'apps', '4': 'settings' };
        event.preventDefault();
        go(map[event.key], true);
        return;
      }

      if (event.key === '/') {
        event.preventDefault();
        goToAppsAndFocus();
        return;
      }

      if ((event.key === 'ArrowLeft' || event.key === 'ArrowRight' ||
        event.key === 'ArrowUp' || event.key === 'ArrowDown') && target &&
        target.classList && target.classList.contains('app-card')) {
        var cards = all('.app-card');
        var index = cards.indexOf(target);
        if (index < 0) return;
        var columns = 6;
        if (els.appGrid) {
          var template = window.getComputedStyle(els.appGrid).gridTemplateColumns;
          if (template) columns = Math.max(1, template.split(' ').length);
        }
        var next = index;
        if (event.key === 'ArrowRight') next = index + 1;
        else if (event.key === 'ArrowLeft') next = index - 1;
        else if (event.key === 'ArrowDown') next = index + columns;
        else if (event.key === 'ArrowUp') next = index - columns;
        if (next >= 0 && next < cards.length) {
          event.preventDefault();
          cards[next].focus();
        }
      }
    });

    window.addEventListener('hashchange', onHashChange);

    document.addEventListener('visibilitychange', function () {
      if (!document.hidden) {
        refreshSnapshot({ silent: true });
        if (state.currentView === 'apps') loadApps({ silent: true });
      }
    });
  }

  function setAutostartUi(enabled) {
    if (state.snapshot) state.snapshot.autostart = Boolean(enabled);
    if (els.autostartBadge) {
      els.autostartBadge.textContent = enabled ? 'On' : 'Off';
      els.autostartBadge.className = 'chip' + (enabled ? ' is-good' : '');
    }
    if (els.autostartNote) {
      els.autostartNote.textContent = enabled
        ? 'Enabled through the Homebrew startup hook. It will run at the next TV boot.'
        : 'Autostart is not enabled. Enable it to bring the link up at boot.';
    }
    renderStatus();
  }

  function enableAutostart() {
    var values = readForm();
    var valid = J.validateConnection(values);
    if (!valid.ok) {
      markInvalid(valid.errors);
      setFormMessage(valid.errors[Object.keys(valid.errors)[0]], 'error');
      go('link');
      return;
    }
    persistForm();
    if (state.busy) return;
    setBusy(true);
    J.autostartEnable(state.settings).then(function () {
      setBusy(false);
      setAutostartUi(true);
      toast('Autostart enabled. It will run at the next TV boot.', 'good');
      return refreshSnapshot({ silent: true });
    }).catch(function (error) {
      setBusy(false);
      toast('Could not enable autostart: ' + messageOf(error), 'error');
    });
  }

  function disableAutostart() {
    if (state.busy) return;
    setBusy(true);
    J.autostartDisable().then(function () {
      setBusy(false);
      setAutostartUi(false);
      toast('Autostart disabled.');
      return refreshSnapshot({ silent: true });
    }).catch(function (error) {
      setBusy(false);
      toast('Could not disable autostart: ' + messageOf(error), 'error');
    });
  }

  /* ---------- boot ---------- */

  function init() {
    cacheElements();

    if (els.versionLabel) els.versionLabel.textContent = J.APP.version;
    if (els.aboutVersion) els.aboutVersion.textContent = J.APP.version;
    if (els.demoBadge) els.demoBadge.hidden = !J.DEMO;
    if (els.clock) els.clock.textContent = timeNow();
    window.setInterval(function () {
      if (els.clock) els.clock.textContent = timeNow();
    }, 15000);

    hydrateConnectionForm();
    hydrateSettings();
    wireEvents();
    renderView(currentHashView(), false);
    renderStatus();

    if (!J.hasBridge() && !J.DEMO) {
      if (els.formMessage) setFormMessage('Browser preview: the TV root helper and app launcher are unavailable. The layout and settings still work.', '');
      if (els.appsStatus) els.appsStatus.textContent = 'TV services are unavailable in a browser preview.';
      if (els.appsHint) els.appsHint.textContent = 'TV services unavailable';
      renderApps();
      renderHomeApps();
      if (els.logView) els.logView.textContent = 'This is a browser preview. Open Junction on the TV to read the client status and log.';
      if (els.autostartBadge) els.autostartBadge.textContent = 'Unavailable';
      if (els.autostartNote) els.autostartNote.textContent = 'Autostart needs the TV root helper.';
      if (els.rootState) els.rootState.textContent = 'Preview only';
      if (els.probeNote) els.probeNote.textContent = 'Probing needs the TV root helper. In a browser preview, no TV calls are made.';
      setPill('idle', 'Preview only');
      return;
    }

    getTvAddress();
    refreshSnapshot({ silent: true });
    startPolling();
    window.setTimeout(function () { runProbe({ silent: true }); }, 650);
    loadApps({ silent: true });
  }

  function getTvAddress() {
    J.getTvAddress().then(function (address) {
      if (els.tvAddress) els.tvAddress.textContent = address;
      if (els.factAddress) els.factAddress.textContent = address;
    }).catch(function () {
      if (els.tvAddress) els.tvAddress.textContent = 'Unavailable';
      if (els.factAddress) els.factAddress.textContent = 'Unavailable';
    });
  }

  init();
})();
