(function (root) {
  'use strict';
  var SERVICE = 'luna://com.webos.service.applicationmanager/';
  function call(method, params) {
    return new Promise(function (resolve, reject) {
      if (typeof root.PalmServiceBridge !== 'function') { reject(new Error('TV service bridge unavailable')); return; }
      var bridge = new root.PalmServiceBridge();
      var timer = setTimeout(function () { reject(new Error(method + ' timed out')); }, 5000);
      bridge.onservicecallback = function (raw) {
        clearTimeout(timer);
        try { var data = JSON.parse(raw); if (data.returnValue === false) reject(new Error(data.errorText || method + ' denied')); else resolve(data); }
        catch (e) { reject(e); }
      };
      bridge.call(SERVICE + method, JSON.stringify(params || {}));
    });
  }
  function normalize(items) {
    var seen = Object.create(null);
    return (items || []).map(function (a) { return { id: a.id || a.appId, title: a.title || a.appTitle || a.id || a.appId, icon: a.icon || a.largeIcon || '' }; }).filter(function (a) { if (!a.id || seen[a.id]) return false; seen[a.id] = true; return true; });
  }
  function runningIds(data) { return (data && data.running || []).map(function (a) { return a.id || a.appId; }).filter(Boolean); }
  function createAdapter(request, preset) {
    return {
      load: function () {
        if (preset && Array.isArray(preset.apps)) return Promise.resolve(preset);
        return Promise.allSettled([request('listLaunchPoints', {}), request('listApps', {}), request('getForegroundAppInfo', {}), request('dev/running', {})]).then(function (r) {
          var launch = r[0].status === 'fulfilled' ? r[0].value.launchPoints : null;
          var apps = r[1].status === 'fulfilled' ? r[1].value.apps : null;
          var foreground = r[2].status === 'fulfilled' ? r[2].value.appId : '';
          var running = r[3].status === 'fulfilled' && Array.isArray(r[3].value.running) ? runningIds(r[3].value) : null;
          return { apps: normalize(launch && launch.length ? launch : apps), foreground: foreground, running: running, catalogAvailable: Array.isArray(launch) || Array.isArray(apps) };
        });
      },
      launch: function (id) { return request('launch', { id: id }); }
    };
  }
  if (typeof module !== 'undefined' && module.exports) module.exports = { normalize: normalize, runningIds: runningIds, createAdapter: createAdapter };
  if (!root.document) return;
  var demo = /(?:^|[?&])demo=1(?:&|$)/.test(root.location.search);
  function demoCall(method) {
    if (method === 'listLaunchPoints') return Promise.resolve({ launchPoints: [{id:'demo.video',title:'Video'}, {id:'demo.music',title:'Music'}, {id:'demo.browser',title:'Browser'}] });
    if (method === 'getForegroundAppInfo') return Promise.resolve({appId:'demo.video'});
    if (method === 'dev/running') return Promise.resolve({running:[{id:'demo.video'},{id:'demo.music'}]});
    if (method === 'launch') return Promise.resolve({returnValue:true});
    return Promise.reject(new Error('Unavailable in demo'));
  }
  var doc = root.document, preset = root.JUNCTION_DOCK_CATALOG && Array.isArray(root.JUNCTION_DOCK_CATALOG.apps) ? root.JUNCTION_DOCK_CATALOG : null, adapter, dock = doc.getElementById('dock'), list = doc.getElementById('apps'), status = doc.getElementById('status'), mode = doc.getElementById('mode'), timer;
  function makeAdapter() { return createAdapter(demo ? demoCall : call, preset); }
  adapter = makeAdapter();
  function reloadPreset() {
    return new Promise(function (resolve) {
      var script = doc.createElement('script');
      script.src = 'catalog.js?t=' + Date.now();
      script.onload = function () {
        preset = root.JUNCTION_DOCK_CATALOG && Array.isArray(root.JUNCTION_DOCK_CATALOG.apps) ? root.JUNCTION_DOCK_CATALOG : null;
        adapter = makeAdapter();
        resolve(true);
      };
      script.onerror = function () { resolve(false); };
      (doc.head || doc.documentElement).appendChild(script);
    });
  }
  function show() { clearTimeout(timer); dock.classList.remove('hidden'); }
  function hide() { if (dock.contains(doc.activeElement)) doc.activeElement.blur(); dock.classList.add('hidden'); }
  function schedule() { clearTimeout(timer); timer = setTimeout(function () { if (!dock.contains(doc.activeElement)) hide(); }, 3500); }
  function refresh() {
    status.textContent = 'Loading apps…';
    adapter.load().then(function (data) {
      list.textContent = '';
      var ordered = data.apps.slice().sort(function (a, b) { var ar = data.running && data.running.indexOf(a.id) >= 0, br = data.running && data.running.indexOf(b.id) >= 0; return Number(br) - Number(ar) || a.title.localeCompare(b.title); });
      mode.textContent = data.source === 'junction' ? 'Open apps first · select an app' : data.running ? 'Running apps marked · all apps' : 'All apps · running state unavailable';
      status.textContent = demo ? 'Demo data: no TV calls are made.' : data.source === 'junction' ? 'Open apps first. Select an app to launch or return.' : data.catalogAvailable ? (data.running ? 'Running state available; select an app to switch.' : 'Running state unavailable; select an app to launch or return.') : 'App list unavailable. Open this dock from Junction to load the catalog.';
      doc.getElementById('empty').hidden = ordered.length > 0;
      ordered.forEach(function (app) {
        var button = doc.createElement('button'); button.type = 'button'; button.className = 'app';
        var icon = doc.createElement(app.icon ? 'img' : 'span'); if (app.icon) { icon.src = app.icon; icon.alt = ''; icon.onerror = function () { icon.style.display = 'none'; }; } else { icon.className = 'fallback'; icon.textContent = app.title.charAt(0).toUpperCase(); } button.appendChild(icon);
        var title = doc.createElement('span'); title.className = 'title'; title.textContent = app.title; button.appendChild(title);
        var state = doc.createElement('small'); state.textContent = data.foreground === app.id ? 'Foreground' : data.running && data.running.indexOf(app.id) >= 0 ? 'Running' : 'Open'; button.appendChild(state);
        button.addEventListener('click', function () { status.textContent = 'Opening ' + app.title + '…'; adapter.launch(app.id).then(function () { status.textContent = 'Launch requested for ' + app.title + '.'; }, function (e) { status.textContent = 'Could not open ' + app.title + ': ' + e.message; }); }); list.appendChild(button);
      });
    });
  }
  doc.getElementById('edge').addEventListener('mouseenter', show); dock.addEventListener('mouseenter', show); dock.addEventListener('mouseleave', schedule); dock.addEventListener('focusin', show); dock.addEventListener('focusout', schedule); doc.getElementById('hide').addEventListener('click', hide); doc.getElementById('refresh').addEventListener('click', refresh);
  doc.addEventListener('keydown', function (e) { if (e.key === 'Escape') hide(); else if (e.key.toLowerCase() === 'd' && !/INPUT|TEXTAREA/.test(e.target.tagName)) { show(); var first = list.querySelector('button'); if (first) first.focus(); } });
  doc.addEventListener('visibilitychange', function () { if (!doc.hidden) { reloadPreset().then(function () { show(); refresh(); schedule(); }); } });
  refresh(); schedule();
}(typeof window === 'undefined' ? globalThis : window));
