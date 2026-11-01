/*
 * Web app for the SSS10 PIR switch running OpenBeken: Logs, Filesystem and
 * SSS10 (switch settings and schedules) tabs. Plain JavaScript with no
 * dependencies, so it fits on the device's LittleFS; tools/webapp.py
 * uploads it.
 */
(function () {
  'use strict';

  // Set by OpenBeken's /app page ('http://' + location.host)
  const dev = window.device || '';

  /* ---------- helpers ---------- */

  // h('tag', {props, onevent: fn}, children...)
  function h(tag, props, ...kids) {
    const e = document.createElement(tag);
    for (const k in props || {}) {
      const v = props[k];
      if (v === undefined || v === null) continue;
      if (k.startsWith('on')) e.addEventListener(k.slice(2), v);
      else if (k === 'class') e.className = v;
      else if (k in e) e[k] = v;
      else e.setAttribute(k, v);
    }
    for (const c of kids.flat(Infinity)) {
      if (c === null || c === undefined || c === false) continue;
      e.append(c instanceof Node ? c : String(c));
    }
    return e;
  }

  function cmd(c) {
    return fetch(dev + '/api/cmnd', { method: 'POST', body: c })
      .then(r => r.json())
      .then(r => {
        if (r.success != 200) throw new Error(c + ': ' + (r.msg || 'failed'));
        return r;
      });
  }

  const wait = ms => new Promise(resolve => setTimeout(resolve, ms));

  /* ---------- Logs ---------- */

  function logsTab() {
    const MAX_LINES = 400;
    let lines = [];
    let paused = false;

    const text = h('textarea', { class: 'logs', readOnly: true });
    const pause = h('button', { onclick: () => { paused = !paused; pause.textContent = paused ? 'Resume' : 'Pause'; } }, 'Pause');
    const level = h('select', { onchange: () => cmd('loglevel ' + level.value).catch(err => reply.textContent = err.message) });
    const features = h('div', { class: 'features' });
    const input = h('input', { type: 'text', class: 'cmdline', autocomplete: 'on', placeholder: 'Console command' });
    const reply = h('span', { class: 'dim' });

    function setFeature(box, on) {
      box.checked = on;
      return cmd('logfeature ' + box.value + ' ' + (on ? 1 : 0)).catch(err => reply.textContent = err.message);
    }

    // One command at a time; the device handles one request at once
    async function setAll(on) {
      for (const box of features.querySelectorAll('input'))
        if (box.checked != on) await setFeature(box, on);
    }

    fetch(dev + '/api/logconfig')
      .then(r => r.json())
      .then(cfg => {
        cfg.levelnames.forEach((n, i) => level.append(h('option', { value: i }, n)));
        level.value = cfg.level;
        cfg.featurenames
          .map((n, i) => ({ name: n.replace(/:$/, ''), i: i }))
          .sort((a, b) => a.name.localeCompare(b.name))
          .forEach(f => features.append(h('label', null,
            h('input', { type: 'checkbox', value: f.i, checked: !!((cfg.features >> f.i) & 1),
                         onchange: ev => setFeature(ev.target, ev.target.checked) }),
            f.name)));
      })
      .catch(err => reply.textContent = 'Log config: ' + err.message);

    function poll() {
      (paused ? Promise.resolve() : fetch(dev + '/lograw').then(r => r.text()).then(data => {
        if (!data) return;
        const atBottom = text.scrollTop + text.clientHeight >= text.scrollHeight - 10;
        lines = lines.concat(data.replace(/\r/g, '').split('\n').filter(l => l));
        if (lines.length > MAX_LINES) lines = lines.slice(-MAX_LINES);
        text.value = lines.join('\n');
        if (atBottom) text.scrollTop = text.scrollHeight;
      })).catch(() => {}).then(() => setTimeout(poll, 500));
    }
    poll();

    const el = h('div', { class: 'logtab' },
      text,
      h('div', { class: 'row' },
        pause,
        h('button', { onclick: () => { lines = []; text.value = ''; } }, 'Clear'),
        ' Level ', level,
        h('button', { onclick: () => setAll(true) }, 'All features'),
        h('button', { onclick: () => setAll(false) }, 'No features')),
      features,
      h('form', { class: 'row', onsubmit: ev => {
          ev.preventDefault();
          const c = input.value;
          cmd(c).then(r => { reply.textContent = c + ': ' + r.msg; input.value = ''; })
                .catch(err => reply.textContent = err.message);
        } },
        input, h('button', { type: 'submit' }, 'Send'), ' ', reply),
      h('p', { class: 'dim' }, 'Some commands only exist once their driver has been started.'));
    return { el: el };
  }

  /* ---------- Filesystem ---------- */

  function fsTab() {
    let editing = null;
    const list = h('div', { class: 'files' });
    const total = h('div', { class: 'dim' });
    const status = h('div', { class: 'dim' });
    const gzip = h('input', { type: 'checkbox' });
    const label = h('b', null, 'Select a file to edit it.');
    const editor = h('textarea', { class: 'editor', spellcheck: false });
    const editBar = h('div', { class: 'row', hidden: true },
      h('button', { onclick: () => save() }, 'Save'),
      h('button', { onclick: () => save('startScript ' + editing), title: 'Save, then run the file as a script thread' }, 'Save and run'),
      h('button', { onclick: () => save('backlog resetSVM; startScript ' + editing), title: 'Save, stop all scripts, then run the file' }, 'Save, reset scripts and run'),
      h('button', { onclick: () => window.open(dev + '/api/lfs' + editing, '_blank') }, 'Open in browser'),
      h('button', { onclick: () => remove() }, 'Delete'));

    const isBinary = name => /\.(gz|bin|tar|png|jpg|ico)$/i.test(name);

    async function listFolder(path, out) {
      const folder = await fetch(dev + '/api/lfs' + path).then(r => r.json());
      for (const f of folder.content || []) {
        if (f.name.startsWith('.')) continue;
        const full = (path == '/' ? '' : path) + '/' + f.name;
        if (f.type == 2) await listFolder(full, out);
        else out.push({ name: full, size: f.size });
      }
      return out;
    }

    function refresh() {
      status.textContent = 'Listing...';
      return listFolder('/', []).then(files => {
        list.replaceChildren(...files.map(f => h('div', null,
          h('a', { href: '#', onclick: ev => { ev.preventDefault(); open(f.name); } }, f.name.slice(1)),
          h('span', { class: 'dim' }, ' ' + f.size))));
        if (!files.length) list.append(h('div', { class: 'dim' }, 'No files.'));
        total.textContent = 'Total ' + files.reduce((n, f) => n + f.size, 0) + ' bytes';
        status.textContent = '';
      }).catch(err => status.textContent = err.message);
    }

    function open(name) {
      editing = name;
      editBar.hidden = false;
      if (isBinary(name)) {
        label.textContent = name.slice(1) + ' (binary, not editable)';
        editor.value = '';
        editor.disabled = true;
        return;
      }
      label.textContent = 'Editing ' + name.slice(1);
      editor.disabled = false;
      fetch(dev + '/api/lfs' + name).then(r => r.text()).then(t => editor.value = t)
        .catch(err => status.textContent = err.message);
    }

    function write(name, data) {
      return fetch(dev + '/api/lfs/' + name.replace(/^\//, ''), { method: 'POST', body: data }).then(r => {
        if (!r.ok) throw new Error('Saving ' + name + ': HTTP ' + r.status);
      });
    }

    function save(then) {
      if (!editing || editor.disabled) return;
      status.textContent = 'Saving...';
      write(editing, editor.value)
        .then(() => then ? cmd(then) : null)
        .then(() => { status.textContent = 'Saved ' + editing.slice(1) + (then ? ' and started it' : ''); return refresh(); })
        .catch(err => status.textContent = err.message);
    }

    function remove() {
      if (!editing) return;
      const core = /^\/(app\.js\.gz|startup\.js)$/.test(editing) ? '\n\nThis web app needs it!' : '';
      if (!confirm('Delete ' + editing.slice(1) + '?' + core)) return;
      const name = editing;
      // OpenBeken answers /api/del/ without an HTTP status line, which
      // browsers may treat as a failure; the list shows whether it worked
      fetch(dev + '/api/del' + name).catch(() => {}).then(() => listFolder('/', [])).then(files => {
        if (files.some(f => f.name == name)) throw new Error('Could not delete ' + name.slice(1));
        status.textContent = 'Deleted ' + name.slice(1);
        editing = null;
        editBar.hidden = true;
        label.textContent = 'Select a file to edit it.';
        editor.value = '';
        return refresh();
      }).catch(err => status.textContent = err.message);
    }

    function create() {
      const name = prompt('New file name:', 'autoexec.bat');
      if (!name) return;
      write(name, '').then(refresh).then(() => open('/' + name.replace(/^\//, '')))
        .catch(err => status.textContent = err.message);
    }

    async function upload(files) {
      try {
        for (const file of files) {
          let data = file, name = file.name;
          if (gzip.checked) {
            data = await new Response(file.stream().pipeThrough(new CompressionStream('gzip'))).arrayBuffer();
            name += '.gz';
          }
          status.textContent = 'Uploading ' + name + '...';
          await write(name, data);
        }
        status.textContent = 'Uploaded ' + files.length + ' file(s)';
      } catch (err) {
        status.textContent = err.message;
      }
      refresh();
    }

    function pick() {
      const input = h('input', { type: 'file', multiple: true, onchange: () => upload(Array.from(input.files)) });
      input.click();
    }

    const el = h('div', { class: 'fstab',
        ondragover: ev => ev.preventDefault(),
        ondrop: ev => { ev.preventDefault(); upload(Array.from(ev.dataTransfer.files)); } },
      h('div', { class: 'row' },
        h('button', { onclick: refresh }, 'Refresh'),
        h('button', { onclick: create }, 'Create file'),
        h('button', { onclick: pick }, 'Upload'),
        h('label', { title: 'Compress uploads; OpenBeken serves .gz files with Content-Encoding: gzip' }, gzip, 'as gzip'),
        h('button', { onclick: () => cmd('backlog resetSVM; clearAllHandlers').then(() => status.textContent = 'Scripts stopped')
                                         .catch(err => status.textContent = err.message),
                      title: 'Stop all running script threads and event handlers' }, 'Reset scripts')),
      h('p', { class: 'dim' }, 'Drop files anywhere here to upload them. Files are served at ' + (dev || location.origin) + '/api/lfs/<name>.'),
      status,
      h('div', { class: 'fsbody' },
        h('div', { class: 'fslist' }, list, total),
        h('div', { class: 'fsedit' }, label, editBar, editor)));

    let loaded = false;
    return { el: el, show: () => { if (!loaded) { loaded = true; refresh(); } } };
  }

  /* ---------- SSS10 ---------- */

  function sssTab() {
    const DP_FIRST = 116, SLOTS = 7;
    const PRESET_DPS = [107, 123, 124];
    const TEST_DPS = [115, 125, 126];
    const DAYS = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'];
    const FADES = [[0, 'Instant'], [1, 'Fast (~0.7 s)'], [2, 'Medium (~5.6 s)'], [3, 'Slow (~17 s)']];

    // type: the tuyaMcu_sendState type; options: [value, label, not selectable]
    const S = (dp, type, label, help, extra) => Object.assign({ dp, type, label, help }, extra);
    const GROUPS = [
      { title: 'Lamp', items: [
        S(1, 'bool', 'Lamp', 'The load. Always off after a power cut.'),
        S(103, 'enum', 'Fade in', 'How long the lamp takes to reach full brightness when it switches on. ' +
          'Times are for 60 Hz mains; 20% longer at 50 Hz.', { options: FADES }),
        S(104, 'enum', 'Fade out', 'How long the lamp takes to go off.', { options: FADES }),
        S(7, 'val', 'Countdown', 'Seconds until the lamp is set to the state below; 0 = no countdown. ' +
          'The MCU only reports the remaining time once a minute. Not saved across power cuts.', { min: 0, max: 86400, unit: 's' }),
        S(110, 'bool', 'State after countdown', 'On: the countdown switches the lamp on; off: it switches it off.'),
      ]},
      { title: 'Motion and sound', items: [
        S(48, 'enum', 'PIR sensitivity', 'How strong a movement must be to count as motion. Motion only switches the lamp on ' +
          'during a PIR-controlled schedule entry when the room is dark enough.',
          { options: [[0, 'Low'], [1, 'Medium'], [2, 'High (default)']] }),
        S(50, 'val', 'Hold-off after manual off', 'After you switch the lamp off by hand (button or app), motion is ignored ' +
          'for this long so it does not come straight back on; the indicator shows cyan meanwhile. Default 10.',
          { min: 0, max: 65535, unit: 's' }),
        S(106, 'val', 'Sound threshold', 'Sound level (20-100) that counts as activity: while the lamp is on with auto-off, ' +
          'sound restarts the auto-off timer just like motion. Lower is more sensitive; very loud peaks always count; ' +
          '100 turns sound off. Default 65.', { min: 20, max: 100 }),
      ]},
      { title: 'Light presets', blurb: 'Each schedule entry picks one of these. Its PIR control and colour changes only act ' +
          'when the light level (0-100, higher is brighter) is at or below the threshold; 100 means always dark enough. ' +
          'Use the light meter test below to see the current level.', items: [
        S(107, 'val', 'Preset 0 dark threshold', 'Default 10 (only quite dark rooms).', { min: 0, max: 100 }),
        S(123, 'val', 'Preset 1 dark threshold', 'Default 20.', { min: 0, max: 100 }),
        S(124, 'val', 'Preset 2 dark threshold', 'Default 100 (always). Used by the built-in schedule.', { min: 0, max: 100 }),
      ]},
      { title: 'Indicator LEDs', blurb: 'The front LEDs show white when motion will switch the lamp on (or it is on with ' +
          'auto-off) and cyan otherwise. Changing a level shows that colour for 5 s as a preview.', items: [
        S(101, 'val', 'White level', 'Brightness 0-100 of the white indicator.', { min: 0, max: 100, unit: '%' }),
        S(102, 'val', 'Cyan level', 'Brightness 0-100 of the cyan indicator.', { min: 0, max: 100, unit: '%' }),
        S(105, 'val', 'Status level', 'Brightness 0-100 of the yellow breathing shown when the Wi-Fi connection is lost.',
          { min: 0, max: 100, unit: '%' }),
        S(127, 'bool', 'Follow room light', 'On: the LEDs dim in a dark room (15-100% depending on the light level) ' +
          'instead of using the levels above.'),
      ]},
      { title: 'Sensor test', blurb: 'Test modes for checking the sensors. A test ends by itself after 15 minutes; ' +
          'the readings below update every second while one runs.', items: [
        S(115, 'enum', 'Test mode', 'PIR test: the lamp goes off and the green LED breathes until motion is seen, then the ' +
          'lamp switches on (any schedule). Sound test: the same with the sound trigger. Light meter: reports the light level every second.',
          { options: [[0, 'Off'], [2, 'PIR test'], [1, 'PIR test: motion detected', true], [4, 'Sound test'],
                      [3, 'Sound test: sound detected', true], [5, 'Light meter']] }),
        S(125, 'val', 'Light level', 'Current light level 0-100 (higher is brighter); light meter mode only.', { readonly: true }),
        S(126, 'val', 'Sound peak', 'Loudest sound level in the last second; sound test only.', { readonly: true }),
      ]},
      { title: 'Device', items: [
        S(113, 'str', 'MCU firmware', 'Firmware version reported by the MCU.', { readonly: true }),
        S(109, 'str', 'Model tag', 'Hardware tag stored in the MCU\'s flash.', { readonly: true }),
      ]},
    ];
    const byDp = {};
    GROUPS.forEach(g => g.items.forEach(s => byDp[s.dp] = s));

    let slots = [];
    let busy = false;
    let liveTimer = null;
    const status = h('span', { class: 'status' });
    const warn = h('div', { class: 'warn', hidden: true },
      'Nothing came back from the MCU. Reading settings needs OpenBeken to keep the DP values the MCU reports ' +
      '(flags 46 and 47: store raw TuyaMCU data, for all DPs). ',
      h('button', { onclick: enableFlags }, 'Enable'));
    const slotBox = h('div');

    const say = t => status.textContent = t;
    const fail = err => { say(err.message); return false; };

    function setBusy(b) {
      busy = b;
      el.querySelectorAll('.whenidle').forEach(b2 => b2.disabled = b);
    }

    /* settings */

    function control(s) {
      const known = () => s.value !== null && s.value !== undefined;
      if (s.readonly) {
        const span = h('span');
        s.show = () => span.textContent = known() ? s.value + (s.unit ? ' ' + s.unit : '') : '-';
        return span;
      }
      if (s.type == 'bool') {
        const box = h('input', { type: 'checkbox', onchange: () => send(s, box.checked ? 1 : 0) });
        const txt = h('span');
        s.show = () => { box.disabled = !known(); box.checked = !!s.value; txt.textContent = known() ? (s.value ? ' On' : ' Off') : ''; };
        return h('label', null, box, txt);
      }
      if (s.options) {
        const sel = h('select', { onchange: () => send(s, +sel.value) });
        s.show = () => {
          const opts = s.options.slice();
          if (known() && !opts.some(o => o[0] == s.value)) opts.push([s.value, String(s.value)]);
          sel.replaceChildren(...(known() ? [] : [h('option', { value: '' })]),
            ...opts.map(o => h('option', { value: o[0], disabled: !!o[2] }, o[1])));
          sel.value = known() ? s.value : '';
          sel.disabled = !known();
        };
        return sel;
      }
      const num = h('input', { type: 'number', class: 'num', min: s.min, max: s.max,
                               onkeyup: ev => { if (ev.key == 'Enter') send(s, num.valueAsNumber); } });
      s.show = () => { num.value = known() ? s.value : ''; };
      return h('span', null, num, h('button', { onclick: () => send(s, num.valueAsNumber) }, 'Set'),
               s.unit ? h('span', { class: 'dim' }, ' ' + s.unit) : null);
    }

    function send(s, v) {
      if (v === '' || v === null || isNaN(v)) return;
      if (s.min !== undefined) v = Math.max(s.min, Math.min(s.max, Math.round(v)));
      say('Setting ' + s.label + '...');
      cmd('tuyaMcu_sendState ' + s.dp + ' ' + s.type + ' ' + v)
        .then(() => { setValue(s, v); say('Set ' + s.label + ' to ' + v); })
        .catch(err => { s.show(); fail(err); });
    }

    function setValue(s, v) {
      s.value = v;
      s.show();
      if (PRESET_DPS.includes(s.dp)) renderSlots();
      if (s.dp == 115) {
        if (v && !liveTimer) liveTimer = setInterval(pollLive, 1000);
        if (!v && liveTimer) { clearInterval(liveTimer); liveTimer = null; }
      }
    }

    /* schedules */

    function minutesToTime(m) {
      // 24:00 (end of day) has no <input type=time> form; 00:00 as an end
      // time also runs to midnight
      m %= 1440;
      return String(Math.floor(m / 60)).padStart(2, '0') + ':' + String(m % 60).padStart(2, '0');
    }

    function timeToMinutes(t) {
      const p = (t || '').split(':');
      return p.length < 2 ? NaN : parseInt(p[0], 10) * 60 + parseInt(p[1], 10);
    }

    const emptySlot = id => ({ id: id, enabled: true, mask: 0x7F, entries: [], dirty: false, loaded: false });

    function decodeSlot(i, text) {
      const s = emptySlot(i);
      let bin;
      try { bin = atob(text); } catch (e) { return null; }
      s.loaded = true;
      if (bin.length < 4) { s.enabled = false; return s; }
      const b = k => bin.charCodeAt(k);
      s.id = b(0);
      s.enabled = b(2) != 0;
      s.mask = b(3);
      const n = Math.min(Math.floor((bin.length - 4) / 9), 4);
      for (let k = 0; k < n; k++) {
        const o = 4 + 9 * k;
        s.entries.push({
          start: minutesToTime((b(o) << 8) | b(o + 1)),
          end: minutesToTime((b(o + 2) << 8) | b(o + 3)),
          mode: b(o + 4), off: b(o + 5), preset: b(o + 6),
          autooff: (b(o + 7) << 8) | b(o + 8),
        });
      }
      return s;
    }

    // Same layout as tools/sched.py and the MCU's sched_decode()
    function encodeSlot(s) {
      if (!s.entries.length) return 'AA==';   // shorter than 4 bytes: slot disabled
      const bytes = [s.id & 0xFF, 4 + 9 * s.entries.length, s.enabled ? 1 : 0, s.mask & 0x7F];
      for (const e of s.entries) {
        const start = timeToMinutes(e.start), end = timeToMinutes(e.end);
        const autooff = Math.max(0, Math.min(0xFFFF, e.autooff | 0));
        bytes.push(start >> 8, start & 0xFF, end >> 8, end & 0xFF,
                   e.mode, e.off, e.preset, autooff >> 8, autooff & 0xFF);
      }
      return btoa(String.fromCharCode.apply(null, bytes));
    }

    function slotError(s) {
      for (const e of s.entries) {
        const start = timeToMinutes(e.start), end = timeToMinutes(e.end);
        if (isNaN(start) || isNaN(end)) return 'Enter both times';
        if (start == end) return 'An entry\'s start equals its end: it never matches';
      }
      if (s.enabled && s.entries.length && !s.mask) return 'No days selected';
      return '';
    }

    function presetLabel(p) {
      const v = byDp[PRESET_DPS[p]].value;
      return p + (v !== null && v !== undefined ? ' (dark ≤ ' + v + ')' : ' (DP ' + PRESET_DPS[p] + ')');
    }

    function select(options, value, onchange) {
      const sel = h('select', { onchange: () => onchange(+sel.value) },
        options.map(o => h('option', { value: o[0] }, o[1])));
      if (!options.some(o => o[0] == value)) sel.append(h('option', { value: value }, String(value)));
      sel.value = value;
      return sel;
    }

    function renderSlot(i) {
      const s = slots[i];
      const state = h('span', { class: 'dim' });
      const err = h('span', { class: 'error' });
      const code = h('code', { class: 'dim', title: 'The DP string sent to the MCU (base64)' });
      const update = () => {
        state.textContent = s.dirty ? 'modified' : (s.loaded ? 'as on MCU' : 'not read');
        err.textContent = slotError(s);
        code.textContent = encodeSlot(s);
        box.classList.toggle('disabled', !s.enabled || !s.entries.length);
      };
      const touch = () => { s.dirty = true; update(); };
      const rebuild = () => { s.dirty = true; renderSlot(i); };

      const rows = s.entries.map((e, ei) => {
        const autooff = h('input', { type: 'number', class: 'num', min: 0, max: 65535, value: e.autooff,
                                     disabled: e.off != 2, oninput: () => { e.autooff = autooff.valueAsNumber || 0; touch(); } });
        return h('tr', null,
          h('td', null, h('input', { type: 'time', value: e.start, oninput: ev => { e.start = ev.target.value; touch(); } })),
          h('td', null, h('input', { type: 'time', value: e.end, oninput: ev => { e.end = ev.target.value; touch(); } })),
          h('td', null, select([[1, 'On at start'], [2, 'PIR controlled']], e.mode, v => { e.mode = v; touch(); })),
          h('td', null, select([[0, 'None'], [1, 'Off at end'], [2, 'Auto-off']], e.off,
                               v => { e.off = v; autooff.disabled = v != 2; touch(); })),
          h('td', null, autooff),
          h('td', null, select([0, 1, 2].map(p => [p, presetLabel(p)]), e.preset, v => { e.preset = v; touch(); })),
          h('td', null, h('button', { onclick: () => { s.entries.splice(ei, 1); rebuild(); } }, 'Remove')));
      });

      const box = h('div', { class: 'slot' },
        h('div', null,
          h('b', null, 'Slot ' + i), h('span', { class: 'dim' }, ' (DP ' + (DP_FIRST + i) + ') '),
          h('label', { title: 'A disabled slot keeps its entries but never matches' },
            h('input', { type: 'checkbox', checked: s.enabled, onchange: ev => { s.enabled = ev.target.checked; touch(); } }), 'enabled'),
          h('span', { class: 'days' }, DAYS.map((d, di) => h('label', null,
            h('input', { type: 'checkbox', checked: !!(s.mask & (1 << di)), onchange: () => { s.mask ^= 1 << di; touch(); } }), d))),
          ' ', state),
        rows.length
          ? h('table', null,
              h('tr', null,
                h('th', null, 'Start'),
                h('th', { title: 'Exclusive; earlier than the start runs overnight, 00:00 runs to midnight' }, 'End'),
                h('th', null, 'Mode'), h('th', null, 'Off'),
                h('th', { title: 'Seconds without motion or sound before Auto-off switches the lamp off' }, 'Auto-off (s)'),
                h('th', { title: 'Dark threshold the light level must be at or below' }, 'Light preset'), h('th')),
              rows)
          : h('p', { class: 'dim' }, 'No entries: the slot is disabled.'),
        h('div', { class: 'row' },
          h('button', { disabled: s.entries.length >= 4, onclick: () => {
              s.entries.push({ start: '22:00', end: '06:00', mode: 2, off: 2, autooff: 60, preset: 2 });
              rebuild(); } }, 'Add entry'),
          h('button', { disabled: !s.entries.length, onclick: () => { s.entries = []; rebuild(); } }, 'Clear'),
          h('button', { class: 'whenidle', disabled: busy, onclick: () => sendSlot(i) }, 'Send'),
          err, ' ', code));
      s.update = update;
      update();
      if (slotBox.children[i]) slotBox.replaceChild(box, slotBox.children[i]);
      else slotBox.append(box);
    }

    function renderSlots() {
      slots.forEach((s, i) => renderSlot(i));
    }

    function sendSlot(i) {
      const s = slots[i];
      const err = slotError(s);
      if (err) return Promise.resolve(fail(new Error('Slot ' + i + ': ' + err)));
      say('Sending slot ' + i + '...');
      return cmd('tuyaMcu_sendState ' + (DP_FIRST + i) + ' str ' + encodeSlot(s))
        .then(() => {
          s.dirty = false;
          s.loaded = true;
          s.update();
          say('Sent slot ' + i + ' (the MCU saves it after 3 s)');
          return true;
        })
        .catch(fail);
    }

    async function sendAll() {
      setBusy(true);
      let ok = true;
      // One at a time, spaced so the TuyaMCU link isn't flooded
      for (let i = 0; i < SLOTS && ok; i++) {
        ok = await sendSlot(i);
        await wait(300);
      }
      if (ok) say('Sent all slots (the MCU saves them after 3 s)');
      setBusy(false);
    }

    /* reading */

    const readDps = () => fetch(dev + '/cm?cmnd=Dp').then(r => r.json());

    // Without stored raw data OpenBeken lists mapped DPs with data 0, so
    // values are only taken when the schedule strings show it stores them
    function applyDps(dps, only) {
      const isSlot = dp => dp.id >= DP_FIRST && dp.id < DP_FIRST + SLOTS && typeof dp.data === 'string';
      const found = dps.filter(isSlot).length;
      if (!found) return 0;
      for (const dp of dps) {
        if (only && !only.includes(dp.id)) continue;
        if (isSlot(dp)) {
          const s = decodeSlot(dp.id - DP_FIRST, dp.data);
          if (s) slots[dp.id - DP_FIRST] = s;
        } else if (byDp[dp.id]) {
          const s = byDp[dp.id];
          if (s.type == 'str' ? typeof dp.data === 'string' : typeof dp.data === 'number') setValue(s, dp.data);
        }
      }
      if (!only) renderSlots();
      return found;
    }

    // Ask the MCU to report every DP (it sends one per frame, which takes a
    // few seconds) and poll OpenBeken's cache until the slots arrive.
    // Discards whatever was entered on the page.
    async function refresh() {
      slots = [];
      for (let i = 0; i < SLOTS; i++) slots.push(emptySlot(i));
      for (const dp in byDp) setValue(byDp[dp], null);
      renderSlots();
      setBusy(true);
      say('Asking the MCU for its settings...');
      try {
        await cmd('tuyaMcu_sendQueryState');
        let found = 0;
        for (let tries = 1; tries <= 12; tries++) {
          await wait(1000);
          found = applyDps(await readDps());
          if (found >= SLOTS && tries >= 5) break;
          say('Waiting for the MCU, ' + found + '/' + SLOTS + ' slots');
        }
        warn.hidden = found > 0;
        say(found >= SLOTS ? 'Read all settings' : found ? 'Only ' + found + '/' + SLOTS + ' slots reported' : 'No reply');
      } catch (err) {
        fail(err);
      }
      setBusy(false);
    }

    function pollLive() {
      if (!busy) readDps().then(dps => applyDps(dps, TEST_DPS)).catch(() => {});
    }

    function enableFlags() {
      cmd('SetFlag 46 1').then(() => cmd('SetFlag 47 1'))
        .then(() => { warn.hidden = true; refresh(); })
        .catch(fail);
    }

    /* layout */

    const el = h('div', { class: 'sss' },
      h('p', null, 'Settings of the SSS10 PIR switch, sent to its MCU as TuyaMCU data points (DPs). Hover over a ' +
        'setting\'s name for its DP number. The MCU saves settings to flash about 3 s after the last change.'),
      warn,
      h('div', { class: 'row' },
        h('button', { class: 'whenidle', onclick: refresh,
                      title: 'Ask the MCU for every value; discards anything entered on this page' }, 'Read from MCU'),
        status),
      GROUPS.map(g => h('section', null,
        h('h4', null, g.title),
        g.blurb ? h('p', { class: 'blurb' }, g.blurb) : null,
        h('table', { class: 'settings' }, g.items.map(s => h('tr', null,
          h('td', { class: 'name', title: 'DP ' + s.dp }, s.label),
          h('td', { class: 'control' }, control(s)),
          h('td', { class: 'help' }, s.help)))))),
      h('section', null,
        h('h4', null, 'Schedules'),
        h('p', { class: 'blurb' }, 'Seven slots (DPs 116-122) of up to four time entries each. The first enabled slot and ' +
          'entry that match the current day and time are used, so earlier slots win over later ones. An end time before ' +
          'the start time runs overnight. Outside every entry the PIR does nothing and the indicator is cyan. Schedules ' +
          'only run once the MCU has the local time (OpenBeken\'s NTP driver), and while the switch was never paired the ' +
          'built-in schedule (slot 0\'s default: PIR all day) is used instead.'),
        h('ul', { class: 'blurb' },
          h('li', null, h('b', null, 'Mode'), ': ', h('i', null, 'On at start'), ' switches the lamp on at the start time; ',
            h('i', null, 'PIR controlled'), ' lets motion switch it on while the entry is active and the room is dark enough.'),
          h('li', null, h('b', null, 'Off'), ': ', h('i', null, 'Off at end'), ' switches the lamp off at the end time; ',
            h('i', null, 'Auto-off'), ' switches it off after the auto-off time without motion or sound.'),
          h('li', null, h('b', null, 'Light preset'), ': the entry only acts when the light level is at or below that ' +
            'preset\'s dark threshold (see Light presets above).')),
        h('div', { class: 'row' }, h('button', { class: 'whenidle', onclick: sendAll }, 'Send all slots')),
        slotBox));

    for (let i = 0; i < SLOTS; i++) slots.push(emptySlot(i));
    for (const dp in byDp) setValue(byDp[dp], null);
    renderSlots();

    let loaded = false;
    return { el: el, show: () => { if (!loaded) { loaded = true; refresh(); } } };
  }

  /* ---------- page ---------- */

  const CSS = `
    body { margin: 0; font: 14px 'Segoe UI', Arial, sans-serif; }
    .tabs { display: flex; background: #f1f1f1; border-bottom: 1px solid #ccc; position: sticky; top: 0; z-index: 1; }
    .tabs button { border: 0; background: none; padding: 12px 16px; cursor: pointer; font: inherit; }
    .tabs button:hover { background: #ddd; }
    .tabs button.active { background: #ccc; }
    .tabs .title { margin-left: auto; padding: 12px 16px; color: #777; }
    .page { padding: 8px 12px; }
    .row { margin: 6px 0; }
    .row button, .row label { margin-right: 6px; }
    .dim { color: #777; }
    .error { color: #b00; margin-left: 6px; }
    .warn { background: #fff3cd; border: 1px solid #e0c060; padding: 6px 10px; margin: 8px 0; }
    .status { margin-left: 6px; }
    .num { width: 70px; }
    .logs { width: 100%; height: 60vh; box-sizing: border-box; font: 12px monospace; }
    .features label { display: inline-block; width: 140px; }
    .cmdline { width: 50%; }
    .fsbody { display: flex; gap: 16px; }
    .fslist { min-width: 220px; }
    .fslist a { text-decoration: none; }
    .fsedit { flex: 1; }
    .editor { width: 100%; height: 65vh; box-sizing: border-box; font: 12px monospace; margin-top: 6px; }
    .sss section { margin-bottom: 18px; }
    .blurb { color: #444; max-width: 900px; }
    .settings { border-collapse: collapse; }
    .settings td { padding: 4px 12px 4px 0; vertical-align: top; }
    .settings td.name { width: 190px; font-weight: 600; cursor: help; }
    .settings td.control { width: 230px; white-space: nowrap; }
    .settings td.help { color: #555; font-size: 0.9em; max-width: 600px; }
    .slot { border: 1px solid #ccc; padding: 6px 10px; margin: 8px 0; }
    .slot.disabled { background: #f6f6f6; }
    .slot label, .days { margin-left: 8px; }
    .slot th { text-align: left; font-weight: normal; color: #555; padding-right: 8px; }
    .slot th[title] { cursor: help; }
    .slot code { margin-left: 8px; }
  `;

  function start() {
    document.head.append(h('style', null, CSS));
    const tabs = [['Logs', logsTab()], ['Filesystem', fsTab()], ['SSS10', sssTab()]];
    const bar = h('div', { class: 'tabs' });
    const page = h('div', { class: 'page' });

    function select(name) {
      for (const [n, t] of tabs) {
        const on = n == name;
        t.el.hidden = !on;
        t.button.classList.toggle('active', on);
        if (on && t.show) t.show();
      }
      try { localStorage.setItem('sss10tab', name); } catch (e) {}
    }

    for (const [name, t] of tabs) {
      t.button = h('button', { onclick: () => select(name) }, name);
      bar.append(t.button);
      page.append(t.el);
    }
    bar.append(h('a', { class: 'title', href: dev + '/', target: '_blank', title: 'OpenBeken\'s own pages' }, 'Device UI'));
    document.body.append(bar, page);

    let first = 'SSS10';
    try { first = localStorage.getItem('sss10tab') || first; } catch (e) {}
    select(tabs.some(t => t[0] == first) ? first : 'SSS10');
  }

  if (document.readyState == 'loading') document.addEventListener('DOMContentLoaded', start);
  else start();
})();
