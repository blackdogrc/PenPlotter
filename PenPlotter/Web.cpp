#include "Web.h"
#include "Config.h"

#include <WiFi.h>
#include <WebServer.h>
#include "SvgConverter.h"
#include "WebJS.h"
#include "Debugger.h"

static WebServer server(80);
static Gcode* parser = nullptr;
static void sendApiResponse(int code, const char* contentType,
                            const String& body) {
  if (code >= 400) {
    Debugger::log(String("API response error ") + String(code) +
                  ": " + body);
  } else {
    Debugger::log(String("API response ") + String(code) +
                  ": " + body);
  }

  server.send(code, contentType, body);
}

static void sendError(int code, const char* message) {
  sendApiResponse(code, "text/plain", String(message));
}

static String jsonEscape(const String& input) {
  String output;
  output.reserve(input.length() + 8);

  for (size_t i = 0; i < input.length(); ++i) {
    const char c = input[i];

    switch (c) {
      case '\"': output += "\\\""; break;
      case '\\': output += "\\\\"; break;
      case '\n': output += "\\n"; break;
      case '\r': output += "\\r"; break;
      case '\t': output += "\\t"; break;
      default:
        if (static_cast<uint8_t>(c) >= 0x20) output += c;
        break;
    }
  }

  return output;
}
static const char PAGE[] PROGMEM = R"HTML(
<!doctype html>
<html>
<head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>FergoPlotter</title>
<style>
body{font:16px system-ui;max-width:900px;margin:18px auto;padding:0 12px}
nav button,button{font-size:16px;padding:9px 12px;margin:3px}
nav button{min-width:90px}
#lineNumbers {
  max-height:none;
  height:100%;
  overflow:hidden;
  line-height:18px;
}
.editor textarea{
  font:14px monospace;
  line-height:18px;
}
.tab{display:none}
.tab.active{display:block}
input,select{font-size:16px;padding:7px}
pre{white-space:pre-wrap;background:#eee;padding:10px;max-height:180px;overflow:auto}
.row{text-align:center;margin:8px}
#progress{font-weight:bold;margin:10px 0}
.help h1,.help h2,.help h3{margin-bottom:.3em}
.help ul{padding-left:24px}
.help code{background:#eee;padding:2px 5px}
.settings table{border-collapse:collapse;width:100%;font-size:14px}
.settings th,.settings td{border:1px solid #bbb;padding:8px;text-align:left}
.settings th{background:#eee}
.editor{
  display:flex;
  width:100%;
  height:52vh;
  border:1px solid #aaa;
  box-sizing:border-box;
}
#lineNumbers{
  box-sizing:border-box;
  width:48px;
  flex:none;
  margin:0;
  padding:8px 6px;
  overflow:hidden;
  text-align:right;
  background:#eee;
  color:#777;
  font:14px monospace;
  line-height:normal;
  white-space:pre;
}
#lineNumbers .active{
  background:#ffe58a;
  color:#111;
  font-weight:bold;
}
.editor textarea{
  box-sizing:border-box;
  flex:1;
  width:0;
  height:100%;
  min-width:0;
  border:0;
  margin:0;
  padding:8px;
  resize:none;
  font:14px monospace;
  white-space:pre;
  overflow:auto;
}
@media(max-width:600px){
  .settings table{font-size:12px}
  .settings th,.settings td{padding:5px}
}
</style>
</head>
<body>

<h2>FergoPlotter v1.1</h2>

<nav>
  <button onclick="showTab('jog')">Jog</button>
  <button onclick="showTab('file')">G-code</button>
  <button onclick="showTab('svg')">SVG</button>
  <button onclick="showTab('help')">Instructions</button>
  <button onclick="showTab('config')">Config</button>
</nav>

<section id="jog" class="tab active">
  <h3>Jog</h3>

  <label>
    Distance
    <select id="dist">
      <option value="1">1 mm</option>
      <option value="10" selected>10 mm</option>
      <option value="50">50 mm</option>
    </select>
  </label>

  <div class="row">
    <button onclick="jog(0,-1)">Y-</button>
  </div>
  <div class="row">
    <button onclick="jog(-1,0)">X-</button>
    <button onclick="jog(1,0)">X+</button>
  </div>
  <div class="row">
    <button onclick="jog(0,1)">Y+</button>
  </div>

  <div class="row">
    <button onclick="command('M5')">Pen up</button>
    <button onclick="command('M3')">Pen down</button>
  </div>

  <p>
    <input id="single" placeholder="G1 X20 Y10 F600">
    <button onclick="command(document.getElementById('single').value)">Send</button>
  </p>
</section>

<section id="file" class="tab">
  <h3>G-code</h3>

  <p>
    <input type="file" accept=".gcode,.nc,.txt" onchange="loadFile(this)">
    <button onclick="saveFile()">Save / download</button>
  </p>

  <div class="editor">
    <pre id="lineNumbers" aria-hidden="true">1</pre>
    <textarea id="program" spellcheck="false" wrap="off"
      placeholder="Paste G-code here or load a file"></textarea>
  </div>

  <p>
    <button onclick="runJob()">Run</button>
    <button onclick="control('/api/pause')">Pause</button>
    <button onclick="control('/api/resume')">Resume</button>
    <button onclick="stopJob()">STOP</button>
  </p>

  <div id="progress">Idle</div>
</section>

<section id="svg" class="tab">
  <h3>SVG to G-code</h3>

  <p><input id="svgFile" type="file" accept=".svg,image/svg+xml"></p>

  <p>
    Work area: 225 mm by 325 mm.
    <label>
      Oversize handling
      <select id="svgMode">
        <option value="fit">Fit drawing to area</option>
        <option value="crop">Keep size and crop outside area</option>
      </select>
    </label>
  </p>

  <p>
    <img id="svgPreview" alt="SVG preview"
      style="display:none;max-width:100%;max-height:45vh;border:1px solid #aaa">
  </p>

  <button onclick="generateSvgGcode()">Generate G-code</button>
  <span id="svgMessage"></span>
</section>

<section id="help" class="tab">
  <h3>Instructions</h3>
  <div id="helpText" class="help">Loading instructions…</div>
</section>

<section id="config" class="tab">
  <h3>Config and command reference</h3>
  <div id="settings" class="settings">Loading settings…</div>
</section>

<h3>Console</h3>
<pre id="console">Ready</pre>

<script src="/svg-converter.js"></script>
<script src="/app.js"></script>
</body>
</html>
)HTML";

static const char HELP_TEXT[] =
  "# FergoPlotter v1.1\n"
  "## Movement\n"
  "- `G0` / `G1`: XY movement\n"
  "- `G20`: select inches\n"
  "- `G21`: select millimeters\n"
  "- `G90`: absolute positioning\n"
  "- `G91`: relative positioning\n"
  "- `G92`: set the current XY coordinates\n"
  "- `G28`: home\n"
  "## Pen\n"
  "- `M3` / `M4`: pen down\n"
  "- `M5` / `M9`: pen up\n"
  "- `Z > 0`: pen up; `Z = 0`: pen down\n"
  "## Jobs and controls\n"
  "- `M2` / `M30`: end the job and release control\n"
  "- USB `?`: status; `!`: pause; `~`: resume; Ctrl-X: stop\n"
  "- Pause stops the current move; Resume continues it.\n"
  "- STOP clears queued motion and raises the pen.\n"
  "- USB/UGS and web motion control cannot control the plotter simultaneously.\n";



void webBegin(Gcode& gcode) {
  parser = &gcode;

  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  WiFi.softAP(WIFI_SSID, WIFI_PASSWORD, 6, false, 4);

  server.on("/", HTTP_GET, []() {
    server.send_P(200, "text/html", PAGE);
  });

  server.on("/app.js", HTTP_GET, []() {
    server.send_P(200, "application/javascript", WEB_JS);
  });

server.on("/api/status", HTTP_GET, []() {
  String status = parser->webStatus();
  const String debug = Debugger::takePendingLog();

  if (status.endsWith("}")) {
    status.remove(status.length() - 1);
    status += ",\"debug\":\"";
    status += jsonEscape(debug);
    status += "\"}";
  }

  server.send(200, "application/json", status);
});

  server.on("/api/help", HTTP_GET, []() {
    server.send(200, "text/markdown", HELP_TEXT);
  });

  server.on("/svg-converter.js", HTTP_GET, []() {
    server.send_P(200, "application/javascript", SVG_CONVERTER_JS);
  });

  server.on("/api/settings", HTTP_GET, []() {
    String html;
    html += "<table><tr><th>Setting / function</th>";
    html += "<th>Current value</th>";
    html += "<th>G-code command or how to change</th></tr>";

    html += "<tr><td>X steps/mm</td><td id='xStepsSetting'>";
    html += String(parser->xStepsPerMm(), 2);
    html += "</td><td>Set with <code>$100=value</code>.</td></tr>";

    html += "<tr><td>Y steps/mm</td><td id='yStepsSetting'>";
    html += String(parser->yStepsPerMm(), 2);
    html += "</td><td>Set with <code>$101=value</code>.</td></tr>";

    html += "<tr><td>Maximum feed limit</td><td>";
    html += String(MAX_FEED_MM_MIN, 0);
    html += " mm/min</td><td>Edit <code>Config.h</code> and rebuild. ";
    html += "Use <code>F600</code>, for example, to set a move's feed.</td></tr>";

    html += "<tr><td>Homing feed</td><td>";
    html += String(HOMING_FEED_MM_MIN, 0);
    html += " mm/min</td><td>Edit <code>Config.h</code>; ";
    html += "run homing with <code>G28</code>.</td></tr>";

    html += "<tr><td>X maximum rate ($110)</td><td>";
    html += String(parser->xMaxRate(), 2);
    html += " mm/min</td><td>Set with <code>$110=value</code>.</td></tr>";

    html += "<tr><td>Y maximum rate ($111)</td><td>";
    html += String(parser->yMaxRate(), 2);
    html += " mm/min</td><td>Set with <code>$111=value</code>.</td></tr>";

    html += "<tr><td>Homing seek rate ($25)</td><td>";
    html += String(parser->homingSeekRate(), 2);
    html += " mm/min</td><td>Set with <code>$25=value</code>.</td></tr>";

    html += "<tr><td>Pen up / down pulse</td><td>";
    html += String(PEN_UP_US);
    html += " / ";
    html += String(PEN_DOWN_US);
    html += " us</td><td>Edit <code>Config.h</code> and rebuild; ";
    html += "there is no runtime G-code setter.</td></tr>";

    html += "<tr><td>Units</td><td>Selected by the job</td>";
    html += "<td><code>G21</code> millimeters; ";
    html += "<code>G20</code> inches.</td></tr>";

    html += "<tr><td>Positioning mode</td><td>Selected by the job</td>";
    html += "<td><code>G90</code> absolute; <code>G91</code> relative.</td></tr>";

    html += "<tr><td>Move feed</td><td>Per command</td>";
    html += "<td>Example: <code>G1 X20 Y10 F600</code>.</td></tr>";

    html += "<tr><td>Pen commands</td><td>As commanded</td>";
    html += "<td><code>M3</code>/<code>M4</code> down; ";
    html += "<code>M5</code>/<code>M9</code> up.</td></tr>";
    html += "<tr><td>USB / web ownership lock</td><td>";
    html += "<label><input id='ownershipLock' type='checkbox' ";
    if (parser->ownershipLockEnabled()) html += "checked ";
    html += "onchange='setOwnershipLock(this.checked)'> Enabled</label>";
    html += "</td><td>Prevents USB and web commands from controlling the plotter "
            "at the same time. Resets to enabled after reboot.</td></tr>";
    html += "<tr><td>Debug logging</td><td>";
    html += "<label><input id='debugLogging' type='checkbox' ";
    if (Debugger::isEnabled()) html += "checked ";
    html += "onchange='setDebugLogging(this.checked)'> Enabled</label>";
    html += "</td><td>Writes additional motion and queue diagnostics.</td></tr>";

    html += "</table>";

    html += "<h3>Steps/mm calibration</h3>";
    html += "<p>Enter the commanded distance and the distance actually measured. ";
    html += "Calculate each axis separately.</p>";

    html += "<table>";
    html += "<tr><th>Axis</th><th>Current steps/mm</th>";
    html += "<th>Commanded distance (mm)</th>";
    html += "<th>Measured distance (mm)</th><th>Calculate</th>";
    html += "<th>New steps/mm</th><th>Save</th></tr>";

    html += "<tr><td>X</td><td id='xCurrentSteps'>";
    html += String(parser->xStepsPerMm(), 2);
    html += "</td><td><input id='xCommandDistance' type='number' min='0.001' step='any'></td>";
    html += "<td><input id='xMeasuredDistance' type='number' min='0.001' step='any'></td>";
    html += "<td><button type='button' onclick=\"calculateSteps('x')\">Calculate</button></td>";
    html += "<td id='xNewSteps'>—</td>";
    html += "<td><button id='saveXSteps' type='button' disabled onclick=\"storeSteps('X')\">Save X</button></td></tr>";

    html += "<tr><td>Y</td><td id='yCurrentSteps'>";
    html += String(parser->yStepsPerMm(), 2);
    html += "</td><td><input id='yCommandDistance' type='number' min='0.001' step='any'></td>";
    html += "<td><input id='yMeasuredDistance' type='number' min='0.001' step='any'></td>";
    html += "<td><button type='button' onclick=\"calculateSteps('y')\">Calculate</button></td>";
    html += "<td id='yNewSteps'>—</td>";
    html += "<td><button id='saveYSteps' type='button' disabled onclick=\"storeSteps('Y')\">Save Y</button></td></tr>";
    html += "</table>";

    server.send(200, "text/html", html);
  });
  server.on("/api/ownership-lock", HTTP_POST, []() {
    if (!server.hasArg("enabled")) {
      sendError(400, "Missing enabled value");
      return;
    }

    const String value = server.arg("enabled");
    if (value != "0" && value != "1") {
      sendError(400, "enabled must be 0 or 1");
      return;
    }

    parser->setOwnershipLockEnabled(value == "1");
    server.send(200, "application/json",
                parser->ownershipLockEnabled()
                  ? "{\"enabled\":true}"
                  : "{\"enabled\":false}");
  });
server.on("/api/debug-logging", HTTP_POST, []() {
  if (!server.hasArg("enabled")) {
    sendError(400, "Missing enabled value");
    return;
  }

  const String value = server.arg("enabled");
  if (value != "0" && value != "1") {
    sendError(400, "enabled must be 0 or 1");
    return;
  }

  const bool enabled = (value == "1");
  Debugger::setEnabled(enabled);

  server.send(
    200,
    "application/json",
    enabled ? "{\"enabled\":true}" : "{\"enabled\":false}"
  );
});

  server.on("/api/start", HTTP_GET, []() {
    if (!parser->claimWeb()) {
      sendError(409, "Controller is owned by USB/UGS");
      return;
    }
    server.send(200, "text/plain", "Web job owns motion");
  });

  server.on("/api/line", HTTP_GET, []() {
    if (!server.hasArg("g")) {
      sendError(400, "Missing G-code line");
      return;
    }

    const uint16_t lineNumber = server.arg("n").toInt();
    if (!parser->enqueue(server.arg("g"), Gcode::WEB, lineNumber)) {
      if (parser->owner() != Gcode::WEB)
        sendError(409, "Controller is owned by USB/UGS");
      else
        sendError(429, "Queue full; retry");
      return;
    }

    server.send(200, "text/plain", "ok");
  });

  server.on("/api/cmd", HTTP_GET, []() {
    if (!server.hasArg("g")) {
      sendError(400, "Missing command");
      return;
    }

    String command = server.arg("g");
    command.trim();

    if (command == "$" || command == "$$") {
      String response = parser->settings();
      response += "\nok";
      server.send(200, "text/plain", response);
      return;
    }

    if (parser->busy()) {
      sendError(409, "Wait for the current job to finish");
      return;
    }

    if (!parser->claimWeb()) {
      sendError(409, "Controller is owned by USB/UGS");
      return;
    }

    if (!parser->enqueue(command, Gcode::WEB) || !parser->enqueue("M30", Gcode::WEB)) {
      sendError(429, "Queue full or controller busy");
      return;
    }

    server.send(200, "text/plain", "Command queued");
  });

  server.on("/api/jog", HTTP_GET, []() {
    if (parser->busy()) {
      sendError(409, "Wait for the current job to finish");
      return;
    }

    if (!parser->claimWeb()) {
      sendError(409, "Controller is owned by USB/UGS");
      return;
    }

    const float dx = server.arg("x").toFloat();
    const float dy = server.arg("y").toFloat();

    String move = "G91 G0 X" + String(dx, 3) + " Y" + String(dy, 3) + " F600";

    if (!parser->enqueue(move, Gcode::WEB) || !parser->enqueue("G90", Gcode::WEB) || !parser->enqueue("M30", Gcode::WEB)) {
      sendError(429, "Queue full or controller busy");
      return;
    }

    server.send(200, "text/plain", "Jog queued");
  });

  server.on("/api/pause", HTTP_GET, []() {
    if (parser->pause()) {
      server.send(200, "text/plain", "Paused");
    } else {
      sendError(409, "No active job");
    }
  });

  server.on("/api/resume", HTTP_GET, []() {
    parser->resume();
    server.send(200, "text/plain", "Resumed");
  });

  server.on("/api/stop", HTTP_GET, []() {
    parser->stop();
    server.send(200, "text/plain", "Stopped; queue cleared; pen up");
  });

  server.begin();
}

void webService() {
  server.handleClient();
}



