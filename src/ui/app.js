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
  job: null,
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

const TYPE_LABELS = { scene: '场景', video: '视频', web: '网页', application: '程序' };
const typeLabel = (type) => TYPE_LABELS[type] || type || '未知';

function formatSize(bytes) {
  if (!bytes) return '未知大小';
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

  const byName = (a, b) => a.title.localeCompare(b.title, 'zh-Hans-CN');
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
      ? '创意工坊目录里没有壁纸：' + state.config.workshopDir
      : '没有找到壁纸。';
    empty.hidden = false;
  } else if (!entries.length) {
    empty.textContent = '没有匹配「' + state.filter + '」的壁纸。';
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
      current.textContent = '使用中';
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
    $('detail-title').textContent = '未选择';
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
  button.textContent = '加载引擎属性';
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
    waiting.textContent = '正在向引擎查询…';
    box.appendChild(waiting);
    return;
  }
  const pre = document.createElement('pre');
  pre.textContent = state.properties.output || '（引擎没有输出）';
  box.appendChild(pre);
}

function renderStatus() {
  const status = state.status;
  const dot = $('status-dot');
  const text = $('status-text');

  if (!status) {
    dot.className = 'dot';
    text.textContent = '连接中…';
    return;
  }
  const engineState = status.state;
  dot.className = 'dot' + (engineState === 'active' ? ' active' : engineState === 'failed' ? ' failed' : '');
  text.textContent = stateName(engineState) + (status.busError ? ' · ' + status.busError : '');
}

const STATE_NAMES = { active: '运行中', inactive: '已停止', failed: '启动失败', activating: '启动中', unknown: '未知' };
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
    value.textContent = entry ? entry.title : id ? '#' + id : '未设置';

    const button = document.createElement('button');
    button.className = 'btn';
    button.textContent = '应用到此屏';
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
    value.textContent = entry ? entry.title + '  ·  #' + entry.id : id ? '#' + id : '未设置';

    const button = document.createElement('button');
    button.className = 'btn';
    button.textContent = '用当前选中项替换';
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
    box.textContent = '检测中…';
    return;
  }

  const line = document.createElement('div');
  if (status.configured) {
    line.className = 'ok';
    line.textContent = '桌面已接管（peony pid ' + status.peonyPid + '，shim 已注入）';
  } else if (status.peonyPid) {
    line.className = 'bad';
    line.textContent = 'peony 正在运行，但未注入 shim —— 桌面图标会盖住壁纸';
  } else {
    line.className = 'bad';
    line.textContent = '未检测到 peony 桌面进程';
  }
  box.appendChild(line);
}

function renderFooter() {
  if (!state.config) return;
  $('footer-info').textContent =
    state.library.length + ' 张壁纸  ·  ' + state.config.enginePath;
}

function renderAll() {
  renderGrid();
  renderDetail();
  renderStatus();
  renderScreens();
  renderDisplays();
  renderIntegration();
  renderFooter();
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
    toast('已应用「' + (entryById(state.selected) || {}).title + '」，等待引擎启动…');
    setTimeout(refreshStatus, 600);
    setTimeout(refreshStatus, 2000);
    setTimeout(refreshStatus, 4000);
  } catch (error) {
    toast('应用失败：' + error.message, 'error');
  }
}

async function stopEngine() {
  try {
    await api('/api/stop', { method: 'POST' });
    toast('已停止');
    refreshStatus();
  } catch (error) {
    toast('停止失败：' + error.message, 'error');
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
    state.properties = { output: '引擎没有在预期时间内返回。' };
    renderProperties();
  } catch (error) {
    state.properties = { output: '读取属性失败：' + error.message };
    renderProperties();
  }
}

async function setupIntegration() {
  toast('正在接管桌面…');
  try {
    await api('/api/integration/setup', { method: 'POST' });
    toast('桌面集成完成', 'ok');
  } catch (error) {
    toast('桌面集成失败：' + error.message, 'error');
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
    toast('已保存，引擎正在重启', 'ok');
    await refreshConfig();
    setTimeout(refreshStatus, 1500);
  } catch (error) {
    toast('保存失败：' + error.message, 'error');
  }
}

async function quit() {
  try {
    await api('/api/quit', { method: 'POST' });
  } catch (error) {
    /* the server is going away; the fetch may not even complete */
  }
  document.body.innerHTML =
    '<p style="margin:40px;font:14px sans-serif;color:#bbb">服务已停止，可以关闭此窗口了。</p>';
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
    toast('初始化失败：' + error.message, 'error');
  }
  setInterval(heartbeat, 10000);
  setInterval(() => refreshStatus().catch(() => {}), 3000);
}

start();
