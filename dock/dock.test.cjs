const test = require('node:test');
const assert = require('node:assert/strict');
const { normalize, createAdapter } = require('./dock.js');
test('normalizes launch points and deduplicates app identities', () => {
  assert.deepEqual(normalize([{id:'a',title:'A'}, {appId:'a',title:'Duplicate'}, {appId:'b'}, {}]).map(x => x.id), ['a','b']);
});
test('catalog fallback and unavailable running state remain distinct', async () => {
  const adapter = createAdapter(async method => {
    if (method === 'listApps') return {apps:[{id:'b',title:'B'}]};
    if (method === 'getForegroundAppInfo') return {appId:'b'};
    throw Error('denied');
  });
  const data = await adapter.load();
  assert.equal(data.apps[0].id, 'b'); assert.equal(data.foreground, 'b'); assert.equal(data.running, null); assert.equal(data.catalogAvailable, true);
});
test('running apps and launch use explicit service payloads', async () => {
  let launch;
  const adapter = createAdapter(async (method, params) => {
    if (method === 'listLaunchPoints') return {launchPoints:[{id:'a',title:'A'}]};
    if (method === 'dev/running') return {running:[{appId:'a'}]};
    if (method === 'launch') { launch = params; return {returnValue:true}; }
    throw Error('denied');
  });
  assert.deepEqual((await adapter.load()).running, ['a']);
  await adapter.launch('a'); assert.deepEqual(launch, {id:'a'});
});
