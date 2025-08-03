#pragma once

const char LOGS_PAGE_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>Tiger IO - Serial Console</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { 
            font-family: monospace; 
            margin: 0; 
            padding: 20px; 
            background: #1e1e1e; 
            color: #d4d4d4; 
        }
        .container { 
            max-width: 1200px; 
            margin: 0 auto; 
        }
        h1 { 
            color: #569cd6; 
            text-align: center; 
            margin-bottom: 20px; 
        }
        .controls { 
            margin-bottom: 20px; 
            text-align: center; 
        }
        button { 
            background: #0078d4; 
            color: white; 
            border: none; 
            padding: 10px 20px; 
            margin: 0 10px; 
            border-radius: 4px; 
            cursor: pointer; 
            font-family: monospace; 
        }
        button:hover { 
            background: #106ebe; 
        }
        button:disabled { 
            background: #666; 
            cursor: not-allowed; 
        }
        #console { 
            background: #000; 
            border: 1px solid #444; 
            padding: 10px; 
            height: 500px; 
            overflow-y: auto; 
            font-size: 14px; 
            line-height: 1.4; 
            white-space: pre-wrap; 
            word-wrap: break-word; 
        }
        .status { 
            margin-top: 10px; 
            padding: 10px; 
            text-align: center; 
            border-radius: 4px; 
        }
        .connected { 
            background: #164e24; 
            color: #4fc3f7; 
        }
        .disconnected { 
            background: #4e1616; 
            color: #f44336; 
        }
        .timestamp { 
            color: #999; 
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>Tiger IO - Serial Console</h1>
        <div class="controls">
            <button onclick="clearConsole()">Clear</button>
            <button onclick="saveConsole()">Save Log</button>
            <button onclick="scrollToBottom()">Scroll to Bottom</button>
            <button onclick="toggleAutoScroll()" id="autoScrollBtn">Auto-scroll: ON</button>
        </div>
        <div id="console"></div>
        <div id="status" class="status disconnected">Disconnected</div>
    </div>

    <script>
        let ws;
        let autoScroll = true;
        let logBuffer = [];
        const console = document.getElementById('console');
        const status = document.getElementById('status');
        const autoScrollBtn = document.getElementById('autoScrollBtn');

        function connect() {
            const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
            const wsUrl = protocol + '//' + window.location.hostname + ':' + window.location.port + '/webserial';
            
            ws = new WebSocket(wsUrl);
            
            ws.onopen = function() {
                status.textContent = 'Connected';
                status.className = 'status connected';
                addToConsole('=== Connected to Tiger IO Serial Console ===\n');
            };
            
            ws.onmessage = function(event) {
                addToConsole(event.data);
            };
            
            ws.onclose = function() {
                status.textContent = 'Disconnected - Reconnecting...';
                status.className = 'status disconnected';
                setTimeout(connect, 2000);
            };
            
            ws.onerror = function() {
                status.textContent = 'Connection Error';
                status.className = 'status disconnected';
            };
        }

        function addToConsole(data) {
            const timestamp = new Date().toLocaleTimeString();
            const timestampedData = '[' + timestamp + '] ' + data;
            logBuffer.push(timestampedData);
            
            console.textContent += timestampedData;
            
            if (autoScroll) {
                console.scrollTop = console.scrollHeight;
            }
        }

        function clearConsole() {
            console.textContent = '';
            logBuffer = [];
        }

        function saveConsole() {
            const blob = new Blob([logBuffer.join('')], { type: 'text/plain' });
            const url = window.URL.createObjectURL(blob);
            const a = document.createElement('a');
            a.href = url;
            a.download = 'tiger-io-console-' + new Date().toISOString().replace(/[:.]/g, '-') + '.txt';
            document.body.appendChild(a);
            a.click();
            document.body.removeChild(a);
            window.URL.revokeObjectURL(url);
        }

        function scrollToBottom() {
            console.scrollTop = console.scrollHeight;
        }

        function toggleAutoScroll() {
            autoScroll = !autoScroll;
            autoScrollBtn.textContent = 'Auto-scroll: ' + (autoScroll ? 'ON' : 'OFF');
        }

        // Auto-connect on load
        connect();
    </script>
</body>
</html>)rawliteral";
