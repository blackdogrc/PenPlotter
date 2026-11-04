#pragma once

#include <Arduino.h>

static const char WEB_JS[] PROGMEM = R"JS(
let cancelJob = false;

function showTab(id) {
  document.querySelectorAll('.tab').forEach(e => e.classList.remove('active'));
  document.getElementById(id).classList.add('active');
}

function log(message) {
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

async function getStatus() {
  try {
    const response = await fetch('/api/status');
    const status = await response.json();

    document.getElementById('progress').textContent =
      status.state + ' | owner: ' + status.owner +
      ' | line: ' + status.line +
      ' | X: ' + status.x + ' Y: ' + status.y;

    updateLineNumbers(Number(status.line));
    return status;
  } catch (error) {
    return null;
  }
}

const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));

async function waitUntilIdle() {
  while (!cancelJob) {
    const status = await getStatus();

    if (cancelJob) return false;
    if (status && !status.busy && !status.paused) return true;

    await sleep(250);
  }
  return false;
}

async function runJob() {
  cancelJob = false;

  const text = document.getElementById('program').value;
  const lines = text.split(/\r?\n/);

  let response = await fetch('/api/start');
  if (!response.ok) {
    log(await response.text());
    return;
  }

  log('Web G-code job started');

  for (let i = 0; i < lines.length; i++) {
    if (cancelJob) {
      log('Job stopped');
      return;
    }

    const line = lines[i].trim();
    if (!line || line.startsWith(';')) continue;

    while (true) {
      if (cancelJob) {
        log('Job stopped');
        return;
      }

      response = await fetch('/api/line?n=' + (i + 1) +
                             '&g=' + encodeURIComponent(lines[i]));

      if (cancelJob) {
        log('Job stopped');
        return;
      }

      if (response.ok) break;

      if (response.status === 429) {
        await sleep(250);
        continue;
      }

      log(await response.text());
      return;
    }

    log('Queued line ' + (i + 1) + ': ' + line);

    const idle = await waitUntilIdle();
    if (!idle || cancelJob) {
      log('Job stopped');
      return;
    }

    if (/^M(2|30)\b/i.test(line)) {
      log('Job complete');
      return;
    }
  }

  if (cancelJob) {
    log('Job stopped');
    return;
  }

  response = await fetch('/api/line?n=' + (lines.length + 1) +
                         '&g=M30');

  if (cancelJob) {
    log('Job stopped');
    return;
  }

  if (response.ok) {
    const idle = await waitUntilIdle();
    if (idle && !cancelJob) log('Job complete (M30 appended)');
    else log('Job stopped');
  } else {
    log(await response.text());
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
  const key = String(axis).toLowerCase();
  if (key !== 'x' && key !== 'y') return;

  const saveButton =
    document.getElementById('save' + key.toUpperCase() + 'Steps');
  saveButton.disabled = true;

  const current = Number(
    document.getElementById(key + 'CurrentSteps').textContent
  );
  const commanded = Number(
    document.getElementById(key + 'CommandDistance').value
  );
  const measured = Number(
    document.getElementById(key + 'MeasuredDistance').value
  );

  if (!(current > 0) || !(commanded > 0) || !(measured > 0) ||
      !Number.isFinite(current) || !Number.isFinite(commanded) ||
      !Number.isFinite(measured)) {
    alert('Enter positive numbers for both distances.');
    return;
  }

  const result = current * commanded / measured;
  document.getElementById(key + 'NewSteps').textContent =
    result.toFixed(2);
  saveButton.disabled = false;
}
const id = axis + suffix;
const element = document.getElementById(id);


for (const axis of ['x', 'y']) {
  for (const suffix of ['CommandDistance', 'MeasuredDistance']) {
    const id = axis + suffix;
    const element = document.getElementById(id);

    if (!element) {
      console.error(`Missing element: #${id}`);
      continue;
    }

    element.addEventListener('input', () => {
      document.getElementById(
        'save' + axis.toUpperCase() + 'Steps'
      ).disabled = true;
      document.getElementById(axis + 'NewSteps').textContent = '—';
    });
  }
}



async function generateSvgGcode() {
  const input = document.getElementById('svgFile');
  const message = document.getElementById('svgMessage');
  const file = input.files && input.files[0];

  if (!file) {
    message.textContent = 'Choose an SVG file first.';
    return;
  }

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

  message.textContent = 'Converting…';

  try {
    const result = await SvgConverter.convert(
      file, 225, 325,
      document.getElementById('svgMode').value
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

