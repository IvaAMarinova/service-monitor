// USB (Web Serial) connection to the board, shared by the setup pages.
//
// The page provides: #unsupported, #connect, #connect-msg, #log, and passes
// the ids of the sections to enable once connected.
//
//   Board.init({ sections: ['s-wifi'], onConnected: (status) => { ... } });
//   const reply = await Board.command('status');   // { ok, text }

const Board = (() => {
  const $ = (id) => document.getElementById(id);
  const ESPRESSIF_VID = 0x303a;

  let port = null, reader = null, writer = null;
  let buffer = '';
  let linesSeen = 0;
  let closing = false;
  const waiters = new Set();
  let opts = { sections: [], onConnected: () => {} };

  function setMsg(id, text, kind) {
    const el = $(id);
    el.textContent = text;
    el.className = 'msg ' + (kind || '');
  }

  function log(line) {
    const el = $('log');
    el.textContent += line + '\n';
    if (el.textContent.length > 20000) el.textContent = el.textContent.slice(-15000);
    el.scrollTop = el.scrollHeight;
  }

  function onLine(line) {
    log(line);
    for (const w of [...waiters]) {
      try { w(line); } catch (e) { /* a waiter handles its own errors */ }
    }
  }

  // Resolves with whatever `match` returns (non-undefined) for an incoming line.
  function waitFor(match, timeoutMs) {
    return new Promise((resolve, reject) => {
      const w = (line) => {
        const r = match(line);
        if (r !== undefined) { cleanup(); resolve(r); }
      };
      const timer = setTimeout(() => { cleanup(); reject(new Error('timeout')); }, timeoutMs);
      function cleanup() { clearTimeout(timer); waiters.delete(w); }
      waiters.add(w);
    });
  }

  async function send(line) {
    if (!writer) throw new Error('not connected');
    await writer.write(new TextEncoder().encode(line + '\n'));
  }

  // Sends a command and waits for "[setup] ok ..." or "[setup] error ...".
  async function command(line, timeoutMs = 3000) {
    const reply = waitFor((l) => {
      if (l.startsWith('[setup] ok')) return { ok: true, text: l.slice(10).trim() };
      if (l.startsWith('[setup] error')) return { ok: false, text: l.slice(13).trim() };
    }, timeoutMs);
    await send(line);
    return reply;
  }

  async function readLoop(r) {
    const decoder = new TextDecoder();
    try {
      while (true) {
        const { value, done } = await r.read();
        if (done) break;
        buffer += decoder.decode(value, { stream: true });
        let i;
        while ((i = buffer.search(/\r?\n/)) >= 0) {
          const line = buffer.slice(0, i);
          buffer = buffer.slice(buffer[i] === '\r' ? i + 2 : i + 1);
          if (line.length) { linesSeen++; onLine(line); }
        }
      }
    } catch (e) {
      log('[page] read error: ' + e.message);
    }
    if (!closing) {
      await closePort();
      setMsg('connect-msg', 'The board disconnected (unplugged or restarted). Click Connect again.', 'err');
    }
  }

  function setConnected(on) {
    for (const id of opts.sections) {
      $(id).classList.toggle('disabled', !on);
      $(id).classList.toggle('active', on);
    }
    $('s-connect').classList.toggle('active', !on);
    $('connect').hidden = on;
  }

  // Releases the reader/writer and closes the port so the browser lets go of it.
  async function closePort() {
    closing = true;
    const p = port, r = reader, w = writer;
    port = null; reader = null; writer = null;
    try { if (r) { await r.cancel(); r.releaseLock(); } } catch (e) {}
    try { if (w) { await w.close().catch(() => {}); w.releaseLock(); } } catch (e) {}
    try { if (p) await p.close(); } catch (e) { log('[page] close: ' + e.message); }
    waiters.clear();
    buffer = '';
    closing = false;
    setConnected(false);
  }

  async function connect() {
    let picked;
    try {
      picked = await navigator.serial.requestPort({ filters: [{ usbVendorId: ESPRESSIF_VID }] });
    } catch (e) {
      return; // picker closed
    }
    setMsg('connect-msg', 'Connecting…', 'busy');
    $('connect').disabled = true;
    try {
      // Opening fails if a previous attempt left the port open; close and retry once.
      try { await picked.open({ baudRate: 115200 }); }
      catch (e) {
        try { await picked.close(); } catch (e2) {}
        await picked.open({ baudRate: 115200 });
      }
    } catch (e) {
      setMsg('connect-msg', 'Could not open the board. Close other programs or tabs using it (PlatformIO monitor, Arduino IDE, the other setup page), then unplug it, plug it back in and reload this page.\n(' + e.message + ')', 'err');
      $('connect').disabled = false;
      return;
    }
    port = picked;
    reader = port.readable.getReader();
    writer = port.writable.getWriter();
    linesSeen = 0;
    readLoop(reader);

    // The board may reboot when the port opens; keep asking until it answers.
    let status = null;
    for (let i = 0; i < 8 && !status && port; i++) {
      try {
        const r = await command('status', 1500);
        if (r.ok) status = r.text;
      } catch (e) { /* retry */ }
    }
    $('connect').disabled = false;
    if (!port) return; // disconnected meanwhile; readLoop already said so
    if (!status) {
      const why = linesSeen > 0
        ? 'The board is sending its log but doesn\'t understand setup commands. It is probably running older firmware: flash the new version and try again.'
        : 'The board is not sending anything. Unplug it, plug it back in, reload this page and try again. If that doesn\'t help, press the RST button on its side.';
      await closePort();
      setMsg('connect-msg', why, 'err');
      $('log').parentElement.open = true;
      return;
    }

    setConnected(true);
    const wifi = /wifi="([^"]*)"/.exec(status);
    const net = /network="([^"]*)"/.exec(status);
    const ip = /ip=(\S+)/.exec(status);
    let text = 'Connected to the board.';
    if (wifi) text += ' Wi-Fi: ' + wifi[1] + (net && net[1] ? ' (' + net[1] + ')' : '') + (ip && ip[1] !== '-' ? ', ip ' + ip[1] : '');
    setMsg('connect-msg', text, 'ok');
    opts.onConnected({ network: net ? net[1] : '', wifi: wifi ? wifi[1] : '', ip: ip ? ip[1] : '-' });
  }

  function init(options) {
    opts = Object.assign(opts, options);
    if (!('serial' in navigator)) {
      $('unsupported').style.display = 'block';
      $('connect').disabled = true;
      return;
    }
    $('connect').onclick = connect;
    navigator.serial.addEventListener('disconnect', (e) => {
      if (port && e.target === port) {
        closePort().then(() => setMsg('connect-msg', 'The board was unplugged. Plug it in and click Connect.', 'err'));
      }
    });
    window.addEventListener('beforeunload', () => { if (port) closePort(); });
  }

  return { init, command, send, waitFor, setMsg };
})();
