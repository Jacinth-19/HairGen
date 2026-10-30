// studio.js - input forwarding for the ImGui panel.
//
// The renderer composites the panel into the frames it streams, so the browser
// only has to
//   1. map pointer coordinates from the letterboxed <img> back to frame pixels,
//   2. forward mouse / wheel / key / text events to /ui/input,
//   3. keep the MJPEG stream alive and report the state in the bottom bar.
//
// Everything is fire-and-forget (sendBeacon where available): the panel is
// immediate-mode, a dropped mouse-move is invisible, and this keeps a slow
// software renderer from building a queue of stale requests.

(() => {
  const img = document.getElementById('view');
  const boot = document.getElementById('boot');
  const statusEl = document.getElementById('status');
  const toggleBtn = document.getElementById('toggle-panel');
  const fsBtn = document.getElementById('fullscreen');

  let frameW = 768;
  let frameH = 768;
  let panelVisible = true;

  // ---- transport ---------------------------------------------------------
  function send(params) {
    const q = Object.entries(params)
      .filter(([, v]) => v !== undefined && v !== null)
      .map(([k, v]) => `${k}=${encodeURIComponent(v)}`)
      .join('&');
    if (!q) return;
    const url = `/ui/input?${q}`;
    if (navigator.sendBeacon) {
      navigator.sendBeacon(url);
    } else {
      fetch(url, { method: 'POST', keepalive: true }).catch(() => {});
    }
  }

  // ---- coordinate mapping ------------------------------------------------
  // The image is drawn with object-fit: contain, so the picture inside the
  // element is centred and may be letterboxed. Map through that box.
  function toFrame(ev) {
    const r = img.getBoundingClientRect();
    const scale = Math.min(r.width / frameW, r.height / frameH);
    const dw = frameW * scale;
    const dh = frameH * scale;
    const ox = r.left + (r.width - dw) * 0.5;
    const oy = r.top + (r.height - dh) * 0.5;
    return {
      x: Math.round((ev.clientX - ox) / scale),
      y: Math.round((ev.clientY - oy) / scale),
    };
  }

  // Mouse motion is coalesced: at most one packet per animation frame, and no
  // more than one every 30 ms. The panel is immediate-mode, so a dropped move is
  // invisible, and this keeps a 3 fps software render from queueing thousands of
  // stale requests.
  let pendingMove = null;
  let lastSend = 0;
  function flushMove() {
    const now = performance.now();
    if (pendingMove && now - lastSend > 30) {
      const p = pendingMove;
      pendingMove = null;
      lastSend = now;
      send({ x: p.x, y: p.y });
    }
    requestAnimationFrame(flushMove);
  }
  requestAnimationFrame(flushMove);

  img.addEventListener('mousemove', (ev) => {
    pendingMove = toFrame(ev);
  });

  const buttons = new Set();
  img.addEventListener('mousedown', (ev) => {
    ev.preventDefault();
    const p = toFrame(ev);
    send({ x: p.x, y: p.y, b: ev.button, d: 1 });
    buttons.add(ev.button);
    img.setPointerCapture?.(ev.pointerId ?? 0);
  });
  window.addEventListener('mouseup', (ev) => {
    if (!buttons.has(ev.button) && ev.button !== 0) return;
    buttons.delete(ev.button);
    const p = toFrame(ev);
    send({ x: p.x, y: p.y, b: ev.button, d: 0 });
  });
  img.addEventListener('contextmenu', (ev) => ev.preventDefault());

  img.addEventListener('wheel', (ev) => {
    ev.preventDefault();
    send({ w: Math.max(-1, Math.min(1, -ev.deltaY / 100)) });
  }, { passive: false });

  // ---- keyboard ----------------------------------------------------------
  // The panel wants plain keyCodes: ImGui maps them itself, and using them
  // keeps the browser's layout handling out of the way.
  window.addEventListener('keydown', (ev) => {
    // Keep browser shortcuts (reload, devtools, tab-away) working.
    if (ev.ctrlKey && ['r', 'R', 'w', 'W', 't', 'T'].includes(ev.key)) return;
    send({ k: ev.keyCode, kd: 1 });
    if (['Tab', ' ', 'ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight', 'Backspace'].includes(ev.key)) {
      ev.preventDefault();
    }
  });
  window.addEventListener('keyup', (ev) => {
    send({ k: ev.keyCode, kd: 0 });
  });
  window.addEventListener('keypress', (ev) => {
    if (ev.key && ev.key.length >= 1 && !ev.ctrlKey && !ev.metaKey) {
      send({ t: ev.key });
    }
  });

  // ---- stream health -----------------------------------------------------
  function poll() {
    fetch('/ui', { cache: 'no-store' })
      .then((r) => r.json())
      .then((info) => {
        if (info.width) frameW = info.width;
        if (info.height) frameH = info.height;
        panelVisible = info.visible;
        statusEl.textContent = `live · ${info.width}×${info.height} · ${info.fps.toFixed(1)} fps`;
        statusEl.className = 'pill live';
        toggleBtn.classList.toggle('on', panelVisible);
        boot.classList.add('hidden');
      })
      .catch(() => {
        statusEl.textContent = 'renderer offline';
        statusEl.className = 'pill off';
      })
      .finally(() => setTimeout(poll, 2000));
  }
  poll();

  img.addEventListener('load', () => boot.classList.add('hidden'));
  img.addEventListener('error', () => {
    // MJPEG streams drop when the renderer restarts; re-attach after a moment.
    statusEl.textContent = 'reconnecting';
    statusEl.className = 'pill off';
    setTimeout(() => {
      img.src = `/stream?t=${Date.now()}`;
    }, 1200);
  });

  toggleBtn.addEventListener('click', () => {
    fetch('/ui/toggle', { method: 'POST' })
      .then((r) => r.json())
      .then((info) => {
        panelVisible = info.visible;
        toggleBtn.classList.toggle('on', panelVisible);
      })
      .catch(() => {});
  });

  fsBtn.addEventListener('click', () => {
    if (document.fullscreenElement) document.exitFullscreen();
    else document.documentElement.requestFullscreen?.();
  });
})();
