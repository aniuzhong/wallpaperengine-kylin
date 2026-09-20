'use strict';

// The whole frontend: no framework, no build step. It talks to the service
// layer through the same JSON projections the CLI publishes, so the two
// frontends cannot drift apart.

const state = {
  library: [],
  status: null,
  screens: [],
  config: null,
  integration: null,
  selected: null,
  properties: null,
  filter: '',
  sort: 'name',
};

const $ = (id) => document.getElementById(id);

// ---- plumbing -------------------------------------------------------------

async function api(path, options) {
  const response = await fetch(path, Object.assign({ headers: {} }, options || {}));
  const text = await response.text();
  let body = null;
  try { body = text ? JSON.parse(text) : null; } catch (error) { body = null; }

  if (!response.ok) {
    const detail = body && body.error ? body.error.message || body.error.kind : 'HTTP ' + response.status;
    throw new Error(detail);
  }
  return body;
}

let toastTimer = null;
function toast(message, kind) {
  const node = $('toast');
  node.textContent = message;
  node.className = 'toast' + (kind ? ' ' + kind : '');
  node.hidden = false;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { node.hidden = true; }, kind === 'error' ? 6000 : 3000);
}

// The engine's own names for a wallpaper's type (Scene/Video/Web), plus the
// one the workshop uses for wallpaper applications.
const TYPE_LABELS = { scene: 'Scene', video: 'Video', web: 'Web', application: 'Application' };
const typeLabel = (type) => TYPE_LABELS[type] || type || 'Unknown';

function formatSize(bytes) {
  if (!bytes) return 'Unknown size';
  const mb = bytes / (1024 * 1024);
  if (mb < 1) return (bytes / 1024).toFixed(0) + ' KB';
  if (mb < 1024) return mb.toFixed(0) + ' MB';
  return (mb / 1024).toFixed(2) + ' GB';
}

// A wallpaper is "current" when some screen is actually running it — read
// from the unit file, not from what the UI last asked for.
function currentScreens(id) {
  if (!state.status || !state.status.screens) return [];
  return Object.keys(state.status.screens).filter((screen) => state.status.screens[screen] === id);
}

function entryById(id) {
  return state.library.find((entry) => entry.id === id) || null;
}

// ---- rendering ------------------------------------------------------------

function visibleEntries() {
  const needle = state.filter.trim().toLowerCase();
  const entries = state.library.filter((entry) =>
    !needle || entry.title.toLowerCase().includes(needle) || entry.id.includes(needle));

  // titles come from the workshop in whatever language their author used, so
  // they sort under the browser's locale, not a fixed one
  const byName = (a, b) => a.title.localeCompare(b.title, undefined, { sensitivity: 'base' });
  if (state.sort === 'size') entries.sort((a, b) => b.sizeBytes - a.sizeBytes || byName(a, b));
  else if (state.sort === 'type') entries.sort((a, b) => a.type.localeCompare(b.type) || byName(a, b));
  else entries.sort(byName);
  return entries;
}

function renderGrid() {
  const grid = $('grid');
  const entries = visibleEntries();
  grid.textContent = '';

  const empty = $('grid-empty');
  if (!state.library.length) {
    empty.textContent = state.config
      ? 'No wallpapers in the workshop directory: ' + state.config.workshopDir
      : 'No wallpapers found.';
    empty.hidden = false;
  } else if (!entries.length) {
    empty.textContent = 'Nothing matches "' + state.filter + '".';
    empty.hidden = false;
  } else {
    empty.hidden = true;
  }

  const fragment = document.createDocumentFragment();
  for (const entry of entries) {
    const card = document.createElement('div');
    card.className = 'card' + (entry.id === state.selected ? ' selected' : '');
    card.title = entry.title;

    const thumb = document.createElement('div');
    thumb.className = 'thumb';
    if (entry.hasPreview) {
      const image = document.createElement('img');
      image.loading = 'lazy';          // hundreds of previews: only fetch what is looked at
      image.decoding = 'async';
      image.src = '/api/preview/' + encodeURIComponent(entry.id);
      image.alt = '';
      thumb.appendChild(image);
    }

    const badges = document.createElement('div');
    badges.className = 'badges';
    const type = document.createElement('span');
    type.className = 'badge';
    type.textContent = typeLabel(entry.type);
    badges.appendChild(type);
    if (currentScreens(entry.id).length) {
      const current = document.createElement('span');
      current.className = 'badge current';
      current.textContent = 'Active';
      badges.appendChild(current);
    }
    thumb.appendChild(badges);

    const label = document.createElement('div');
    label.className = 'label';
    label.textContent = entry.title;

    card.appendChild(thumb);
    card.appendChild(label);
    card.addEventListener('click', () => select(entry.id));
    fragment.appendChild(card);
  }
  grid.appendChild(fragment);
}

function renderDetail() {
  const entry = state.selected ? entryById(state.selected) : null;
  const image = $('preview-img');
  const video = $('preview-video');

  image.hidden = true;
  video.hidden = true;
  video.pause();

  if (!entry) {
    $('preview-empty').hidden = false;
    $('detail-title').textContent = 'Nothing selected';
    $('detail-meta').textContent = '';
    $('detail-tags').textContent = '';
    $('btn-apply').hidden = true;
    $('btn-apply-2').disabled = true;
    $('btn-properties').disabled = true;
    $('properties').textContent = '';
    $('properties').appendChild(propertiesButton());
    return;
  }

  $('preview-empty').hidden = entry.hasPreview;
  $('detail-title').textContent = entry.title;
  $('detail-meta').textContent = '#' + entry.id;
  $('btn-apply').hidden = false;
  $('btn-apply-2').disabled = false;
  $('btn-properties').disabled = false;

  if (entry.hasPreview) {
    const source = '/api/preview/' + encodeURIComponent(entry.id);
    if (entry.previewKind === 'video') {
      video.src = source;
      video.hidden = false;
      video.play().catch(() => {});
    } else {
      image.src = source;
      image.hidden = false;
    }
  }

  const tags = $('detail-tags');
  tags.textContent = '';
  for (const text of [typeLabel(entry.type), formatSize(entry.sizeBytes)].concat(currentScreens(entry.id))) {
    const tag = document.createElement('span');
    tag.className = 'tag';
    tag.textContent = text;
    tags.appendChild(tag);
  }
}

function propertiesButton() {
  const button = document.createElement('button');
  button.className = 'btn block';
  button.id = 'btn-properties';
  button.textContent = 'Load engine properties';
  button.disabled = !state.selected;
  button.addEventListener('click', loadProperties);
  return button;
}

function renderProperties() {
  const box = $('properties');
  box.textContent = '';
  if (!state.properties) {
    box.appendChild(propertiesButton());
    return;
  }
  if (state.properties.pending) {
    const waiting = document.createElement('div');
    waiting.className = 'muted';
    waiting.textContent = 'Asking the engine…';
    box.appendChild(waiting);
    return;
  }
  const pre = document.createElement('pre');
  pre.textContent = state.properties.output || '(the engine printed nothing)';
  box.appendChild(pre);
}

function renderStatus() {
  const status = state.status;
  const dot = $('status-dot');
  const text = $('status-text');

  if (!status) {
    dot.className = 'dot';
    text.textContent = 'Connecting…';
    return;
  }
  const engineState = status.state;
  dot.className = 'dot' + (engineState === 'active' ? ' active' : engineState === 'failed' ? ' failed' : '');
  text.textContent = stateName(engineState) + (status.busError ? ' · ' + status.busError : '');
}

// systemd's ActiveState values, in the words the rest of the desktop uses.
const STATE_NAMES = { active: 'Running', inactive: 'Stopped', failed: 'Failed', activating: 'Starting', unknown: 'Unknown' };
const stateName = (name) => STATE_NAMES[name] || name;

function renderScreens() {
  const box = $('screens');
  box.textContent = '';
  for (const screen of state.screens) {
    const row = document.createElement('div');
    row.className = 'row';

    const name = document.createElement('span');
    name.className = 'name';
    name.textContent = screen;

    const value = document.createElement('span');
    value.className = 'value';
    const id = state.status && state.status.screens ? state.status.screens[screen] : null;
    const entry = id ? entryById(id) : null;
    value.textContent = entry ? entry.title : id ? '#' + id : 'Not set';

    const button = document.createElement('button');
    button.className = 'btn';
    button.textContent = 'Apply to this screen';
    button.disabled = !state.selected;
    button.addEventListener('click', () => applyTo(screen));

    row.append(name, value, button);
    box.appendChild(row);
  }
}

function renderDisplays() {
  const box = $('display-rows');
  box.textContent = '';
  for (const screen of state.screens) {
    const row = document.createElement('div');
    row.className = 'row';

    const name = document.createElement('span');
    name.className = 'name';
    name.textContent = screen;

    const value = document.createElement('span');
    value.className = 'value';
    const id = state.status && state.status.screens ? state.status.screens[screen] : null;
    const entry = id ? entryById(id) : null;
    value.textContent = entry ? entry.title + '  ·  #' + entry.id : id ? '#' + id : 'Not set';

    const button = document.createElement('button');
    button.className = 'btn';
    button.textContent = 'Replace with selection';
    button.disabled = !state.selected;
    button.addEventListener('click', () => applyTo(screen));

    row.append(name, value, button);
    box.appendChild(row);
  }
}

function renderIntegration() {
  const box = $('integration');
  const status = state.integration;
  box.textContent = '';
  if (!status) {
    box.textContent = 'Checking…';
    return;
  }

  const line = document.createElement('div');
  if (status.configured) {
    line.className = 'ok';
    line.textContent = 'Desktop taken over (peony pid ' + status.peonyPid + ', shim injected)';
  } else if (status.peonyPid) {
    line.className = 'bad';
    line.textContent = 'peony is running without the shim — desktop icons will cover the wallpaper';
  } else {
    line.className = 'bad';
    line.textContent = 'No peony desktop process found';
  }
  box.appendChild(line);

  // removing has to be as easy to find as setting up: the injected peony
  // runs under a transient unit the user has no reason to know about
  if (status.configured) {
    const remove = document.createElement('button');
    remove.className = 'btn block';
    remove.textContent = 'Remove desktop integration';
    remove.addEventListener('click', removeIntegration);
    box.appendChild(remove);
  }
}

function renderFooter() {
  if (!state.config) return;
  const count = state.library.length;
  $('footer-info').textContent =
    count + (count === 1 ? ' wallpaper  ·  ' : ' wallpapers  ·  ') + state.config.enginePath;
}

// ---- actions --------------------------------------------------------------

function select(id) {
  state.selected = id;
  state.properties = null;
  renderGrid();
  renderDetail();
  renderProperties();
  renderScreens();
  renderDisplays();
}

// Applying is a restart of the systemd unit: the call returns as soon as
// systemd accepts the job, which is not the same as the engine running. The
// status poll below is what turns "accepted" into "running" — or into
// "failed", which is how a broken engine path surfaces instead of silently
// doing nothing.
async function applyTo(screen) {
  if (!state.selected) return;
  const body = { id: state.selected };
  if (screen) body.screen = screen;
  try {
    await api('/api/switch', { method: 'POST', body: JSON.stringify(body) });
    toast('Applied "' + (entryById(state.selected) || {}).title + '" — waiting for the engine');
    setTimeout(refreshStatus, 600);
    setTimeout(refreshStatus, 2000);
    setTimeout(refreshStatus, 4000);
  } catch (error) {
    toast('Could not apply: ' + error.message, 'error');
  }
}

async function stopEngine() {
  try {
    await api('/api/stop', { method: 'POST' });
    toast('Stopped');
    refreshStatus();
  } catch (error) {
    toast('Could not stop: ' + error.message, 'error');
  }
}

async function loadProperties() {
  if (!state.selected) return;
  state.properties = { pending: true };
  renderProperties();
  try {
    const started = await api('/api/properties/' + encodeURIComponent(state.selected));
    const jobId = started.job;
    for (let attempt = 0; attempt < 90; attempt++) {
      await new Promise((done) => setTimeout(done, 700));
      const job = await api('/api/jobs/' + encodeURIComponent(jobId));
      if (job.done) {
        state.properties = { output: job.output };
        renderProperties();
        return;
      }
    }
    state.properties = { output: 'The engine did not answer in time.' };
    renderProperties();
  } catch (error) {
    state.properties = { output: 'Could not read properties: ' + error.message };
    renderProperties();
  }
}

async function setupIntegration() {
  toast('Taking over the desktop…');
  try {
    await api('/api/integration/setup', { method: 'POST' });
    toast('Desktop integration configured', 'ok');
  } catch (error) {
    toast('Desktop integration failed: ' + error.message, 'error');
  }
  await refreshIntegration();
}

async function removeIntegration() {
  toast('Restoring the desktop…');
  try {
    await api('/api/integration/remove', { method: 'POST' });
    toast('Removed — the desktop wallpaper is back', 'ok');
  } catch (error) {
    toast('Could not remove: ' + error.message, 'error');
  }
  await refreshIntegration();
}

async function saveSettings(event) {
  event.preventDefault();
  const form = new FormData(event.target);
  const patch = {};
  for (const [key, value] of form.entries()) patch[key] = value;
  for (const box of event.target.querySelectorAll('input[type=checkbox]')) {
    patch[box.name] = box.checked;
  }
  patch.fps = parseInt(patch.fps, 10) || 30;
  patch.volume = parseInt(patch.volume, 10) || 0;

  try {
    await api('/api/config', { method: 'POST', body: JSON.stringify(patch) });
    toast('Saved — the engine is restarting', 'ok');
    await refreshConfig();
    setTimeout(refreshStatus, 1500);
  } catch (error) {
    toast('Could not save: ' + error.message, 'error');
  }
}

async function quit() {
  try {
    await api('/api/quit', { method: 'POST' });
  } catch (error) {
    /* the server is going away; the fetch may not even complete */
  }
  document.body.innerHTML =
    '<p style="margin:40px;font:14px sans-serif;color:#bbb">The service has stopped. You can close this window.</p>';
}

// ---- data loading ---------------------------------------------------------

async function refreshLibrary() {
  const body = await api('/api/library');
  state.library = (body && body[0]) || [];
  // the footer counts wallpapers, so it belongs to this load, not to the
  // config load that happens alongside it
  renderFooter();
}

async function refreshStatus() {
  state.status = (await api('/api/status')).status;
  renderStatus();
  renderGrid();
  renderDetail();
  renderScreens();
  renderDisplays();
}

async function refreshScreens() {
  state.screens = (await api('/api/screens')).screens;
  renderScreens();
  renderDisplays();
}

async function refreshConfig() {
  state.config = (await api('/api/config')).config;
  const form = $('settings-form');
  for (const [key, value] of Object.entries(state.config)) {
    const field = form.elements[key];
    if (!field) continue;
    if (field.type === 'checkbox') field.checked = !!value;
    else field.value = value;
  }
  renderFooter();
}

async function refreshIntegration() {
  state.integration = (await api('/api/integration')).integration;
  renderIntegration();
}

async function heartbeat() {
  try {
    await api('/api/heartbeat', { method: 'POST' });
  } catch (error) {
    /* the server decides when to go away; a failed heartbeat is not an error
       the user needs to see */
  }
}

// ---- wiring ---------------------------------------------------------------

function showTab(name) {
  for (const button of document.querySelectorAll('.tab')) {
    button.classList.toggle('active', button.dataset.tab === name);
  }
  for (const panel of ['installed', 'displays', 'settings']) {
    $('panel-' + panel).hidden = panel !== name;
  }
  $('detail').hidden = name === 'settings';
  document.querySelector('.filterbar').hidden = name !== 'installed';
}

function wire() {
  for (const button of document.querySelectorAll('.tab')) {
    button.addEventListener('click', () => showTab(button.dataset.tab));
  }
  $('search').addEventListener('input', (event) => {
    state.filter = event.target.value;
    renderGrid();
  });
  $('sort').addEventListener('change', (event) => {
    state.sort = event.target.value;
    renderGrid();
  });
  $('btn-random').addEventListener('click', () => {
    if (!state.library.length) return;
    select(state.library[Math.floor(Math.random() * state.library.length)].id);
    applyTo(null);
  });
  $('btn-apply').addEventListener('click', () => applyTo(null));
  $('btn-apply-2').addEventListener('click', () => applyTo(null));
  $('btn-stop').addEventListener('click', stopEngine);
  $('btn-integration').addEventListener('click', setupIntegration);
  $('btn-quit').addEventListener('click', quit);
  $('settings-form').addEventListener('submit', saveSettings);
  document.addEventListener('visibilitychange', () => {
    if (!document.hidden) refreshStatus().catch(() => {});
  });
}

async function start() {
  wire();
  try {
    await Promise.all([refreshConfig(), refreshScreens(), refreshIntegration(), refreshLibrary()]);
    await refreshStatus();
    if (!state.selected && state.library.length) select(state.library[0].id);
  } catch (error) {
    toast('Could not start: ' + error.message, 'error');
  }
  setInterval(heartbeat, 10000);
  setInterval(() => refreshStatus().catch(() => {}), 3000);
}

start();
