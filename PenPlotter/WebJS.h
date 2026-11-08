#pragma once

#include <Arduino.h>

static const char WEB_JS[] PROGMEM = R"JS(
let cancelJob = false;
let jobRunning = false
let IDLE_WAIT_MS = 128
let consolePaused = false;
let pausedConsoleLines = [];
const MAX_PAUSED_LOG_LINES = 2000;

function showTab(id) {
  document.querySelectorAll('.tab').forEach(e => e.classList.remove('active'));
  document.getElementById(id).classList.add('active');
}



function log(message) {
  if (consolePaused) {
    pausedConsoleLines.push(message);

    // Bound memory if the console stays paused during a long job.
    if (pausedConsoleLines.length > MAX_PAUSED_LOG_LINES) {
      pausedConsoleLines.shift();
    }
    return;
  }

  const box = document.getElementById('console');
  box.textContent += (box.textContent ? '\n' : '') + message;
  box.scrollTop = box.scrollHeight;
}

async function control(url) {
  try {
    const response = await fetch(url);
    log(await response.text());
  } catch (error) {
    log('Request failed: ' + error);
  }
}

async function stopJob() {
  cancelJob = true;
  jobRunning = false;
  updateLineNumbers(0);
  await control('/api/stop');
}

function updateLineNumbers(activeLine = 0) {

  const program = document.getElementById('program');
  const gutter = document.getElementById('lineNumbers');
  const count = program.value.split(/\r?\n/).length;

  let html = '';
  for (let i = 1; i <= count; i++) {
    html += '<span class="' + (i === activeLine ? 'active' : '') +
            '">' + i + '</span>\n';
  }
  gutter.innerHTML = html;

  if (activeLine > 0) {
    const style = getComputedStyle(program);
    const lineHeight = parseFloat(style.lineHeight) || 18;
    const paddingTop = parseFloat(style.paddingTop) || 0;
    const lineTop = paddingTop + (activeLine - 1) * lineHeight;
    const lineBottom = lineTop + lineHeight;
    const viewTop = program.scrollTop;
    const viewBottom = viewTop + program.clientHeight;

    if (lineTop < viewTop || lineBottom > viewBottom) {
      program.scrollTop = Math.max(0, lineTop - 2 * lineHeight);
    }
  }

  gutter.scrollTop = program.scrollTop;
}

document.getElementById('program').addEventListener('input', () => {
  updateLineNumbers();
});

document.getElementById('program').addEventListener('scroll', event => {
  document.getElementById('lineNumbers').scrollTop = event.target.scrollTop;
});

async function loadFile(input) {
  if (input.files && input.files[0]) {
    document.getElementById('program').value =
      await input.files[0].text();
    updateLineNumbers();
  }
}

function saveFile() {
  const contents = document.getElementById('program').value;
  const filename = prompt('Save G-code as:', 'plotter.gcode');

  if (filename === null) return;

  let name = filename.trim();
  if (!name) name = 'plotter.gcode';
  if (!/\.[a-z0-9]+$/i.test(name)) name += '.gcode';

  const blob = new Blob([contents], {type: 'text/plain'});
  const link = document.createElement('a');
  link.href = URL.createObjectURL(blob);
  link.download = name;
  link.click();
  URL.revokeObjectURL(link.href);
}

async function command(gcode) {
  if (!gcode.trim()) return;

  try {
    const response = await fetch(
      '/api/cmd?g=' + encodeURIComponent(gcode)
    );
    log(await response.text());
  } catch (error) {
    log('Command failed: ' + error);
  }
}

async function jog(x, y) {
  const distance = Number(document.getElementById('dist').value);
  const response = await fetch('/api/jog?x=' + (x * distance) +
                               '&y=' + (y * distance));
  log(await response.text());
}
async function getStatus(manual = false) {
  
  try {
    if (!jobRunning && !manual) return;

    const response = await fetch('/api/status');
    const status = await response.json();

    document.getElementById('progress').textContent =
      status.state + ' | owner: ' + status.owner +
      ' | line: ' + status.line +
      ' | X: ' + status.x + ' Y: ' + status.y;
    if (status.debug) {
      status.debug.split('\n').filter(Boolean).forEach(line => log(line));
    }
    updateLineNumbers(Number(status.line));
    return status;
  } catch (error) {
    log('Status error: ' + error);
    return null;
  }
}

const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));

async function waitUntilIdle() {
  while (!cancelJob) {
    const status = await getStatus();

    if (cancelJob) return false;

    if (status) {
      log(`Waiting: busy=${status.busy}, paused=${status.paused}, ` +
          `state=${status.state}, line=${status.line}`);

      if (!status.busy && !status.paused) return true;
    }

    await sleep(IDLE_WAIT_MS);
  }
  return false;
}

function toggleConsolePause() {
  const button = document.getElementById('pauseConsoleButton');

  if (!consolePaused) {
    consolePaused = true;
    button.textContent = 'Resume console';
    return;
  }

  consolePaused = false;
  button.textContent = 'Pause console';

  if (pausedConsoleLines.length) {
    const box = document.getElementById('console');
    const buffered = pausedConsoleLines.join('\n');
    box.textContent += (box.textContent ? '\n' : '') + buffered;
    box.scrollTop = box.scrollHeight;
    pausedConsoleLines = [];
  }
}

async function queueJobLine(lineNumber, gcode) {
  let fullMessageLogged = false;

  while (!cancelJob) {
    const response = await fetch(
      '/api/line?n=' + lineNumber +
      '&g=' + encodeURIComponent(gcode)
    );

    if (cancelJob) return false;

    if (response.ok) {
      // A successful enqueue ends the current queue-full episode.
      fullMessageLogged = false;
      return true;
    }

    if (response.status === 429) {
      if (!fullMessageLogged) {
        log('Queue full; waiting to enqueue line ' + lineNumber);
        fullMessageLogged = true;
      }

      await sleep(IDLE_WAIT_MS);
      continue;
    }

    throw new Error(await response.text());
  }

  return false;
}

async function runJob() {
  if (jobRunning) {
    log('A job is already running');
    return;
  }

  cancelJob = false;

  const lines = document.getElementById('program').value.split(/\r?\n/);

  try {
    const startResponse = await fetch('/api/start');
    if (!startResponse.ok) {
      throw new Error(await startResponse.text());
    }

    jobRunning = true;
    log('Web G-code job started');

    let endCommandQueued = false;

    for (let i = 0; i < lines.length; i++) {
      if (cancelJob) break;

      const line = lines[i].trim();
      if (!line || line.startsWith(';')) continue;

      const accepted = await queueJobLine(i + 1, lines[i]);
      if (!accepted) break;

      log('Queued line ' + (i + 1) + ': ' + line);

      // M2/M30 ends the job. Don't send later lines.
      if (/^M(2|30)\b/i.test(line)) {
        endCommandQueued = true;
        break;
      }
    }

    if (cancelJob) {
      log('Job stopped');
      return;
    }

    if (!endCommandQueued) {
      const endLine = lines.length + 1;
      const accepted = await queueJobLine(endLine, 'M30');
      if (!accepted) {
        log('Job stopped');
        return;
      }
      log('Queued appended M30');
    }

    // Only wait for idle after the whole job has been queued.
    while (!cancelJob) {
      const status = await getStatus(true);
      if (status && !status.busy && !status.paused) {
        log('Job complete');
        return;
      }
      await sleep(250);
    }

    log('Job stopped');
  } catch (error) {
    log('Job failed: ' + error.message);
  } finally {
    jobRunning = false;
    updateLineNumbers(0);
  }
}

function escapeHtml(text) {
  return text.replace(/&/g, '&amp;')
             .replace(/</g, '&lt;')
             .replace(/>/g, '&gt;')
             .replace(/"/g, '&quot;')
             .replace(/'/g, '&#39;');
}

function renderMarkdown(markdown) {
  const lines = markdown.split(/\r?\n/);
  let html = '';
  let inList = false;

  for (const rawLine of lines) {
    const line = rawLine.trim();

    if (!line) {
      if (inList) {
        html += '</ul>';
        inList = false;
      }
      continue;
    }

    const heading = line.match(/^(#{1,3})\s+(.+)$/);
    if (heading) {
      if (inList) {
        html += '</ul>';
        inList = false;
      }
      const level = heading[1].length;
      html += '<h' + level + '>' +
              escapeHtml(heading[2]) +
              '</h' + level + '>';
      continue;
    }

    if (line.startsWith('- ')) {
      if (!inList) {
        html += '<ul>';
        inList = true;
      }
      html += '<li>' +
              escapeHtml(line.slice(2)).replace(
                /`([^`]+)`/g, '<code>$1</code>') +
              '</li>';
      continue;
    }

    if (inList) {
      html += '</ul>';
      inList = false;
    }

    html += '<p>' +
            escapeHtml(line).replace(
              /`([^`]+)`/g, '<code>$1</code>') +
            '</p>';
  }

  if (inList) html += '</ul>';
  return html;
}

async function refreshHelp() {
  const response = await fetch('/api/help');
  document.getElementById('helpText').innerHTML =
    renderMarkdown(await response.text());
}

async function setDebugLogging(enabled) {
  const checkbox = document.getElementById('debugLogging');

  try {
    const response = await fetch('/api/debug-logging', {
      method: 'POST',
      headers: {'Content-Type': 'application/x-www-form-urlencoded'},
      body: 'enabled=' + (enabled ? '1' : '0')
    });

    const result = await response.json();
    if (!response.ok) throw new Error(result.error || 'Request failed');

    checkbox.checked = result.enabled;
    log('Debug logging ' + (result.enabled ? 'enabled' : 'disabled'));
  } catch (error) {
    checkbox.checked = !enabled;
    log('Could not change debug logging: ' + error);
  }
}

function previewSvg(input) {
  const preview = document.getElementById('svgPreview');
  const message = document.getElementById('svgMessage');
  const generateButton = document.getElementById('generateSvgButton');

  if (preview.dataset.url) {
    URL.revokeObjectURL(preview.dataset.url);
    delete preview.dataset.url;
  }

  const file = input.files && input.files[0];

  if (!file) {
    preview.removeAttribute('src');
    preview.style.display = 'none';
    generateButton.disabled = true;
    message.textContent = '';
    return;
  }

  preview.dataset.url = URL.createObjectURL(file);
  preview.src = preview.dataset.url;
  preview.style.display = 'block';
  generateButton.disabled = false;
  message.textContent = 'SVG loaded. Ready to generate G-code.';
}


  
async function setOwnershipLock(enabled) {
  const checkbox = document.getElementById('ownershipLock');
  try {
    const response = await fetch('/api/ownership-lock', {
      method: 'POST',
      headers: {'Content-Type': 'application/x-www-form-urlencoded'},
      body: 'enabled=' + (enabled ? '1' : '0')
    });

    const result = await response.json();
    if (!response.ok) throw new Error(result.error || 'Request failed');

    checkbox.checked = result.enabled;
    log('USB/web ownership lock ' +
        (result.enabled ? 'enabled' : 'disabled'));
  } catch (error) {
    checkbox.checked = !enabled;
    log('Could not change ownership lock: ' + error);
  }
}

async function refreshSettings() {
  const response = await fetch('/api/settings');
  document.getElementById('settings').innerHTML =
    await response.text(); 
}

async function sendSettingAndWait(gcode) {
  const response = await fetch(
    '/api/cmd?g=' + encodeURIComponent(gcode)
  );
  const message = await response.text();
  log(message);

  if (!response.ok) {
    throw new Error(message);
  }

  for (let i = 0; i < 120; i++) {
    const status = await getStatus();
    if (status && !status.busy) return;
    await sleep(250);
  }

  throw new Error('Timed out waiting for the setting command.');
}

async function storeSteps(axis) {
  const key = String(axis).toUpperCase();
  if (key !== 'X' && key !== 'Y') return;

  const lower = key.toLowerCase();
  const value = document.getElementById(lower + 'NewSteps').textContent.trim();
  const button = document.getElementById('save' + key + 'Steps');

  if (!(Number(value) > 0)) return;

  button.disabled = true;

  try {
    await sendSettingAndWait((key === 'X' ? '$100=' : '$101=') + value);

    document.getElementById(lower + 'CurrentSteps').textContent = value;

    const settingCell = document.getElementById(lower + 'StepsSetting');
    if (settingCell) settingCell.textContent = value;

    log(key + ' steps/mm stored.');
  } catch (error) {
    button.disabled = false;
    log('Could not store ' + key + ' steps/mm: ' + error.message);
  }
}

function calculateSteps(axis) {
  const key = String(axis ?? '').toLowerCase();
  if (key !== 'x' && key !== 'y') {
    console.error(`calculateSteps: invalid axis "${axis}"`);
    return;
  }

  const getElement = (id) => {
    const element = document.getElementById(id);
    if (!element) console.error(`calculateSteps: missing #${id}`);
    return element;
  };

  const saveButton = getElement(`save${key.toUpperCase()}Steps`);
  const currentElement = getElement(`${key}CurrentSteps`);
  const commandedElement = getElement(`${key}CommandDistance`);
  const measuredElement = getElement(`${key}MeasuredDistance`);
  const resultElement = getElement(`${key}NewSteps`);

  // If required page elements are missing, stop safely.
  if (!saveButton || !currentElement || !commandedElement ||
      !measuredElement || !resultElement) {
    return;
  }

  saveButton.disabled = true;

  const current = Number(currentElement.textContent.trim());
  const commanded = Number(commandedElement.value);
  const measured = Number(measuredElement.value);

  if (![current, commanded, measured].every(
    value => Number.isFinite(value) && value > 0
  )) {
    resultElement.textContent = '—';
    return;
  }

  const result = current * commanded / measured;

  if (!Number.isFinite(result)) {
    resultElement.textContent = '—';
    return;
  }

  resultElement.textContent = result.toFixed(2);
  saveButton.disabled = false;
}

const DRAWING_FEED_KEY = 'drawingFeedRate';
const DEFAULT_DRAWING_FEED = 4000;
const MAX_DRAWING_FEED = 10000;

function loadDrawingFeedRate() {
  const input = document.getElementById('drawingFeedRate');
  if (!input) return;

  const saved = Number(localStorage.getItem(DRAWING_FEED_KEY));
  if (Number.isFinite(saved) && saved >= 1 && saved <= MAX_DRAWING_FEED) {
    input.value = String(saved);
  }
}

function saveDrawingFeedRate() {
  const input = document.getElementById('drawingFeedRate');
  const value = Number(input.value);

  if (!Number.isFinite(value) || value < 1 || value > MAX_DRAWING_FEED) {
    input.setCustomValidity( 'Enter a feed from 1 to' + MAX_DRAWING_FEED );
    input.reportValidity();
    return false;
  }

  input.setCustomValidity('');
  input.value = String(Math.round(value));
  localStorage.setItem(DRAWING_FEED_KEY, input.value);
  return true;
}


async function generateSvgGcode() {
  const input = document.getElementById('svgFile');
  const message = document.getElementById('svgMessage');
  const file = input.files && input.files[0];

  if (!file) {
    message.textContent = 'Choose an SVG file first.';
    return;
  }
  if (!saveDrawingFeedRate()) return;

  const drawingFeed = Number(
    document.getElementById('drawingFeedRate').value
  );

  const preview = document.getElementById('svgPreview');
  if (preview.dataset.url) URL.revokeObjectURL(preview.dataset.url);
  preview.dataset.url = URL.createObjectURL(file);
  preview.src = preview.dataset.url;
  preview.style.display = 'block';

  const editor = document.getElementById('program');
  if (editor.value.trim()) {
    const answer = confirm(
      'The G-code editor is not empty. Save/download its contents before replacing it?'
    );
    if (answer) saveFile();
  }

  message.textContent = 'Converting...';

  try {
    const result = await SvgConverter.convert(
      file, 225, 325,
      document.getElementById('svgMode').value,
      drawingFeed
    );

    editor.value = result.gcode;
    updateLineNumbers();
    showTab('file');

    message.textContent = result.warning || 'G-code generated.';
    log('Generated G-code from ' + file.name);
    if (result.warning) log(result.warning);
  } catch (error) {
    message.textContent = 'SVG conversion failed: ' + error.message;
    log('SVG conversion failed: ' + error.message);
  }
}

updateLineNumbers();
refreshHelp();
refreshSettings();
setInterval(getStatus, 1000);
)JS";
