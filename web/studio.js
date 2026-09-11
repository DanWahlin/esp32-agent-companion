const $ = (id) => document.getElementById(id);
const names = ['Right', 'Left', 'Up', 'Down', 'Up right', 'Up left', 'Down right', 'Down left'];
const session = crypto.randomUUID();
const canvas = $('screen'), context = canvas.getContext('2d', {alpha: false});
const trace = $('trace'), traceContext = trace.getContext('2d');
const image = context.createImageData(400, 352);
context.fillStyle = '#000'; context.fillRect(0, 0, 466, 466);
let playing = !matchMedia('(prefers-reduced-motion: reduce)').matches;
let simulation = 0, speed = 1, busy = false, lastRequest = 0, lastAdvance = 0;
let mode = 'random', inspecting = false, commands = [], history = [], largestStep = 0;
let previousPose = null, latest = null, renderedCount = 0, rateStart = performance.now();
let cycleIndex = 0, cycleVisited = false;
const cycleDirections = [1, 0, 2, 3, 5, 4, 7, 6];

function seed() {
  const value = Number($('seed').value);
  if (!Number.isInteger(value) || value < 0 || value > 4294967295) throw new Error('Seed must be between 0 and 4294967295.');
  return value;
}
function error(message) {
  playing = false; commands = []; $('error').hidden = false; $('error').textContent = message; updatePlay();
}
function updatePlay() {
  $('play').textContent = playing ? 'Pause' : 'Play';
  $('play').setAttribute('aria-pressed', String(playing));
}
function queue(command = 0, extras = {}) { commands.push({command, ...extras}); }
function setPlaying(value) {
  playing = value; lastAdvance = performance.now();
  if (value && inspecting) {
    inspecting = false;
    $('hint').textContent = 'Resuming the live timeline. The pose inspector is not part of playback.';
    $('mode-label').textContent = mode === 'random' ? 'Random looking' : 'Directed looking';
  }
  updatePlay();
}
function restart(nextMode = 'random') {
  simulation = 0; commands = []; history = []; previousPose = null; largestStep = 0;
  inspecting = false; mode = nextMode; cycleIndex = 0; cycleVisited = false;
  $('error').hidden = true; queue(3);
  if (mode === 'cycle') queue(1, {direction: cycleDirections[0]});
  $('mode-label').textContent = mode === 'cycle' ? '8-direction round-trip test' : 'Random looking';
  $('hint').textContent = 'Same seed, same moves. Every turn returns smoothly through center before the next begins.';
  setPlaying(true);
}
function look(direction) {
  inspecting = false; mode = 'directed'; queue(1, {direction});
  $('mode-label').textContent = `Queued: ${names[direction]}`;
  $('hint').textContent = `Requested ${names[direction].toLowerCase()}. The current gesture will finish and return through center first.`;
  setPlaying(true);
}
function inspect() {
  setPlaying(false); inspecting = true; commands = [];
  const direction = Number($('inspect-direction').value);
  const turn = Number($('inspect-depth').value) / 1000;
  const openness = Number($('inspect-blink').value) / 100;
  $('inspect-depth-value').textContent = `${(turn * 100).toFixed(1)}%`;
  $('inspect-blink-value').textContent = `${Math.round(openness * 100)}%`;
  $('mode-label').textContent = 'Paused pose inspector';
  $('hint').textContent = 'Manual pose inspection, not an animation transition. Press Play to return to the live timeline.';
  queue(4, {direction, turn, openness});
}
function drawTrace() {
  const w = trace.width, h = trace.height;
  traceContext.clearRect(0, 0, w, h);
  traceContext.strokeStyle = '#20283a'; traceContext.lineWidth = 1;
  for (const y of [10, 36, 62]) {
    traceContext.beginPath(); traceContext.moveTo(0, y + .5); traceContext.lineTo(w, y + .5); traceContext.stroke();
  }
  traceContext.strokeStyle = '#939cff'; traceContext.lineWidth = 2; traceContext.beginPath();
  history.forEach((point, index) => {
    const x = index / 239 * w, y = h - 10 - point.turn * (h - 20);
    if (index === 0) traceContext.moveTo(x, y); else traceContext.lineTo(x, y);
  });
  traceContext.stroke();
}
async function render(request) {
  busy = true;
  try {
    const requestedTime = request.time ?? simulation;
    const response = await fetch('/api/frame', {
      method: 'POST', headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({session, time: requestedTime, seed: seed(), ...request}),
    });
    if (!response.ok) {
      const body = await response.text();
      const document = new DOMParser().parseFromString(body, 'text/html');
      throw new Error(document.body.textContent.trim().replace(/\s+/g, ' '));
    }
    const buffer = new Uint8Array(await response.arrayBuffer());
    if (buffer.length !== 281600) throw new Error('Incomplete native framebuffer.');
    for (let pixel = 0, byte = 0; byte < buffer.length; pixel += 4, byte += 2) {
      const value = (buffer[byte] << 8) | buffer[byte + 1];
      image.data[pixel] = Math.round((value >> 11) * 255 / 31);
      image.data[pixel + 1] = Math.round(((value >> 5) & 63) * 255 / 63);
      image.data[pixel + 2] = Math.round((value & 31) * 255 / 31);
      image.data[pixel + 3] = 255;
    }
    context.putImageData(image, 33, 57);
    latest = {
      time: requestedTime, direction: Number(response.headers.get('X-Copilot-direction')),
      turn: Number(response.headers.get('X-Copilot-turn')),
      openness: Number(response.headers.get('X-Copilot-openness')),
      renderMs: Number(response.headers.get('X-Copilot-renderMs')),
    };
    $('time').textContent = `${requestedTime.toFixed(2)} s`;
    $('direction').textContent = latest.turn < .001 ? 'Center' : names[latest.direction];
    $('depth').textContent = `${(latest.turn * 100).toFixed(1)}%`;
    document.querySelectorAll('[data-direction]').forEach(button => {
      button.classList.toggle('active', latest.turn > .01 && Number(button.dataset.direction) === latest.direction);
    });
    if (request.command !== 4 && !inspecting) {
      if (previousPose && requestedTime > previousPose.time) {
        const step = Math.abs(latest.turn - previousPose.turn);
        largestStep = Math.max(largestStep, step);
        const badHandoff = latest.direction !== previousPose.direction && Math.max(latest.turn, previousPose.turn) > .01;
        $('step-metric').classList.toggle('warning', largestStep > .085 || badHandoff);
      }
      previousPose = latest; history.push(latest); if (history.length > 240) history.shift();
      $('step-metric').textContent = `Largest frame step: ${(largestStep * 100).toFixed(1)}%`;
      drawTrace();
      if (mode === 'cycle') {
        if (latest.turn > .7) cycleVisited = true;
        if (cycleVisited && latest.turn < .00001) {
          cycleVisited = false; cycleIndex++;
          if (cycleIndex < cycleDirections.length) queue(1, {direction: cycleDirections[cycleIndex]});
          else { setPlaying(false); $('mode-label').textContent = 'All 8 round trips complete'; }
        }
      }
    }
    renderedCount++;
    const elapsed = performance.now() - rateStart;
    if (elapsed >= 1000) {
      $('fps').textContent = `${(renderedCount * 1000 / elapsed).toFixed(1)} fps`;
      renderedCount = 0; rateStart = performance.now();
    }
  } catch (cause) {
    error(`Rendering stopped: ${cause.message}`);
  } finally { busy = false; }
}
function tick(now) {
  if (!busy && commands.length) {
    lastRequest = now; lastAdvance = now; render(commands.shift());
  } else if (!busy && playing && now - lastRequest >= 1000 / 30 - 1) {
    const delta = Math.min(Math.max(0, now - lastAdvance) / 1000, 1 / 30);
    simulation += delta * speed; lastAdvance = now; lastRequest = now; render({command: 0});
  }
  requestAnimationFrame(tick);
}

$('play').addEventListener('click', () => setPlaying(!playing));
$('step').addEventListener('click', () => {
  setPlaying(false); inspecting = false; simulation += 1 / 30; queue(0, {time: simulation});
});
$('restart').addEventListener('click', () => restart());
$('replay').addEventListener('click', () => restart());
$('random').addEventListener('click', () => {
  inspecting = false; mode = 'random'; queue(2, {direction: 1}); $('mode-label').textContent = 'Random looking'; setPlaying(true);
});
$('cycle').addEventListener('click', () => restart('cycle'));
$('center').addEventListener('click', () => {
  inspecting = false; mode = 'directed'; queue(2, {direction: 0});
  $('mode-label').textContent = 'Returning to center'; $('hint').textContent = 'The active gesture finishes, then returns gently to center.';
  setPlaying(true);
});
document.querySelectorAll('[data-direction]').forEach(button => button.addEventListener('click', () => look(Number(button.dataset.direction))));
document.querySelectorAll('[data-speed]').forEach(button => button.addEventListener('click', () => {
  speed = Number(button.dataset.speed);
  document.querySelectorAll('[data-speed]').forEach(other => other.setAttribute('aria-pressed', String(other === button)));
}));
['inspect-depth', 'inspect-blink', 'inspect-direction'].forEach(id => $(id).addEventListener('input', inspect));
$('snapshot').addEventListener('click', () => {
  canvas.toBlob(blob => {
    if (!blob) return error('Unable to create screenshot.');
    const url = URL.createObjectURL(blob), anchor = document.createElement('a');
    anchor.href = url; anchor.download = `copilot-${seed()}-${simulation.toFixed(2)}s.png`; anchor.click();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
  });
});
$('copy').addEventListener('click', async () => {
  const report = JSON.stringify({seed: seed(), mode, speed, ...latest, largestPoseStep: largestStep, notes: $('notes').value}, null, 2);
  try { await navigator.clipboard.writeText(report); $('feedback-status').textContent = 'Copied: note, seed, time, pose, and playback settings.'; }
  catch { $('feedback-status').textContent = 'Clipboard unavailable. Select and copy the report below.'; $('notes').value = report; $('notes').select(); }
});
document.addEventListener('keydown', event => {
  if (['INPUT', 'TEXTAREA', 'SELECT', 'BUTTON'].includes(event.target.tagName)) return;
  if (event.code === 'Space') { event.preventDefault(); setPlaying(!playing); }
  if (event.code === 'ArrowRight') { event.preventDefault(); $('step').click(); }
});
document.addEventListener('visibilitychange', () => { if (document.hidden) setPlaying(false); });
window.addEventListener('pagehide', () => navigator.sendBeacon(
  '/api/close', new Blob([JSON.stringify({session})], {type: 'application/json'})
));
updatePlay(); queue(3); requestAnimationFrame(tick);
