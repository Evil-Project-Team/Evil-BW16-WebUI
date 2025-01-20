// Global variables
let uartConnection;
let isConnected = false;
let networks = [];
let isCollectingNetworks = false;
let currentAttack = null;
let currentChannels = [];
let currentChannelIndex = 0;
let currentChannel = null;

// Global state objects
let systemData = {
    cpuClock: null,
    freeHeap: null,
    minFreeHeap: null,
    uartConnected: false
};

let wifiData = {
    macAddress: null,
    channel: null,
    ipAddress: null,
    signalStrength: null
};

// Local time reference
const localTime = new Date('2025-01-05T06:00:00-05:00');

// Global helper functions
function appendToTerminal(data) {
    const terminal = document.getElementById('terminal');
    if (terminal) {
        const line = document.createElement('div');
        line.className = 'terminal-line';
        line.textContent = data;
        terminal.appendChild(line);
        terminal.scrollTop = terminal.scrollHeight;

        // Show notification dot on terminal toggle
        const terminalToggle = document.getElementById('terminal-toggle');
        if (terminalToggle && !terminalToggle.classList.contains('active')) {
            terminalToggle.classList.add('has-update');
        }
    }
}

function updateConnectionStatus(connected) {
    isConnected = connected;
    
    if (!connected) {
        updateAttackStatus(null);
    }
    
    // Update connection status badge
    const statusBadge = document.getElementById('connection-status');
    if (statusBadge) {
        statusBadge.textContent = connected ? 'Connected' : 'Disconnected';
        statusBadge.className = connected ? 'badge bg-success' : 'badge bg-danger';
    }
}

function sendUARTCommand(command) {
    if (!command) {
        appendToTerminal('[ERROR] Cannot send empty command via UART');
        return;
    }

    // Create command object
    const commandObj = {
        action: "send_command",
        command: command.trim()
    };

    // Convert to JSON string
    const commandJSON = JSON.stringify(commandObj);

    // Log the command being sent
    //console.log("[DEBUG] Sending UART Command: " + commandJSON);

    if (isConnected && uartConnection) {
        uartConnection.send(commandJSON);
        appendToTerminal(`[UART SENT] ${commandJSON}`);
    } else {
        appendToTerminal('[ERROR] UART not connected');
    }

    // Additional debug log to track unexpected actions
    //console.log("[DEBUG] Command sent, awaiting response...");
}

function toggleConnection() {
    isConnected = !isConnected;
    updateConnectionStatus(isConnected);
    if (isConnected) {
        appendToTerminal('[INFO] UART connection established');
    } else {
        appendToTerminal('[INFO] UART connection closed');
    }
}

// Initialize the UI
updateConnectionStatus(false);
appendToTerminal(`[INFO] Local time set to: ${localTime.toLocaleString()}`);

// Command functions
function sendCommand(command) {
    if (!isConnected) {
        console.warn('Not connected to UART');
        appendToTerminal('Error: Not connected to UART');
        return;
    }
    console.log('Sending command:', command);
    sendUARTCommand(command);
    appendToTerminal(`> ${command}`);
}

function refreshPorts() {
    console.log('Refreshing ports...');
    if (uartConnection) {
        sendUARTCommand('refresh_ports');
    }
}

function updatePortsList(ports) {
    console.log('Updating ports list:', ports);
    const portSelect = document.getElementById('portSelect');
    if (portSelect) {
        portSelect.innerHTML = '';
        if (ports && ports.length > 0) {
            // Add default option
            const defaultOption = document.createElement('option');
            defaultOption.value = '';
            defaultOption.textContent = 'Select Port';
            portSelect.appendChild(defaultOption);
            
            // Add available ports
            ports.forEach(port => {
                const option = document.createElement('option');
                option.value = port;
                option.textContent = port;
                portSelect.appendChild(option);
            });
        } else {
            const option = document.createElement('option');
            option.value = '';
            option.textContent = 'No ports available';
            portSelect.appendChild(option);
        }
    }
}

function updateAttackStatus(attackType = null) {
    currentAttack = attackType;
    
    // Show/hide attack visualization
    if (attackType) {
        showAttackVisualization();
    } else {
        hideAttackVisualization();
    }

    const terminalToggle = document.getElementById('terminal-toggle');
    if (!terminalToggle) return;

    if (attackType) {
        terminalToggle.classList.add('attacking');
        let icon, text;
        switch(attackType) {
            case 'deauth':
                icon = 'bi-play-fill';
                text = 'Deauth';
                break;
            case 'disassoc':
                icon = 'bi-x-circle';
                text = 'Disassoc';
                break;
            case 'random':
                icon = 'bi-shuffle';
                text = 'Random';
                break;
            default:
                icon = 'bi-play-fill';
                text = attackType;
        }
        terminalToggle.innerHTML = `
            <span class="attack-type">
                <i class="bi ${icon}"></i>
                ${text}
            </span>
            <i class="bi bi-terminal fs-5"></i>
        `;
    } else {
        terminalToggle.classList.remove('attacking');
        terminalToggle.innerHTML = '<i class="bi bi-terminal fs-5"></i>';
    }
}

function showConfirmationModal(attackType) {
    const modal = document.getElementById('confirmationModal');
    if (modal) {
        // Set the attack type and title
        let title, command;
        switch(attackType) {
            case 'deauth':
                title = 'Start Deauth Attack';
                command = 'start deauther';
                break;
            case 'disassoc':
                title = 'Start Disassoc Attack';
                command = 'disassoc';
                break;
            case 'random':
                title = 'Start Random Attack';
                command = 'random_attack';
                break;
        }
        
        modal.setAttribute('data-attack-type', attackType);
        modal.setAttribute('data-command', command);
        
        // Update modal content
        modal.querySelector('.modal-title').textContent = title;
        modal.querySelector('.modal-body').textContent = `Are you sure you want to start a ${attackType} attack?`;
        
        modal.classList.add('show');
    }
}

function confirmAttack() {
    const modal = document.getElementById('confirmationModal');
    if (!modal) return;

    const attackType = modal.getAttribute('data-attack-type');
    const command = modal.getAttribute('data-command');
    
    closeConfirmationModal();
    appendToTerminal(`> Starting ${attackType} attack...`);
    sendCommand(command);
    updateAttackStatus(attackType);
}

function handleQuickAction(action) {
    if (!uartConnection) {
        appendToTerminal('Error: No UART connection');
        return;
    }

    if (!isConnected) {
        appendToTerminal('Error: Not connected to UART');
        return;
    }

    const actionBtn = document.getElementById(`${action}Btn`);
    if (actionBtn) {
        actionBtn.disabled = true;
    }

    switch (action) {
        case 'scan':
            showScanningIndicator();
            sendCommand('scan');
            break;
        case 'start':
            showConfirmationModal('deauth');
            break;
        case 'disassoc':
            showConfirmationModal('disassoc');
            break;
        case 'random':
            showConfirmationModal('random');
            break;
        case 'stop':
            appendToTerminal('> Stopping attack...');
            sendCommand('stop deauther');
            setTimeout(() => updateAttackStatus(null), 100); // Delay the status update
            break;
        case 'help':
            appendToTerminal('> Requesting help...');
            sendCommand('help');
            break;
        case 'results':
            appendToTerminal('> Displaying results...');
            sendCommand('results');
            break;
    }

    // Re-enable button after a short delay
    setTimeout(() => {
        if (actionBtn) {
            actionBtn.disabled = !isConnected;
        }
    }, 1000);
}

// Content loading functions
function loadContent(target) {
    const contentDiv = document.getElementById('dashboard-content');
    if (!contentDiv) return;

    // Store current connection state
    const wasConnected = isConnected;
    const currentPort = document.getElementById('portSelect')?.value;
    const currentBaud = document.getElementById('baudSelect')?.value;

    // Clear existing content
    contentDiv.innerHTML = '';

    switch(target) {
        case 'dashboard':
            loadDashboard(contentDiv);
            break;
        case 'logs':
            loadLogs(contentDiv);
            break;
        case 'sniffmaster':
            loadSniffMaster(contentDiv);
            break;
        default:
            loadDashboard(contentDiv);
    }

    // If we were connected, restore the connection state
    if (wasConnected && target === 'dashboard') {
        // Wait for elements to be created
        setTimeout(() => {
            const portSelect = document.getElementById('portSelect');
            const baudSelect = document.getElementById('baudSelect');
            if (portSelect && baudSelect) {
                portSelect.value = currentPort;
                baudSelect.value = currentBaud;
                portSelect.disabled = true;
                baudSelect.disabled = true;
            }
            updateConnectionStatus(true);
        }, 100);
    }
}

// Create terminal overlay
const terminalOverlay = document.createElement('div');
terminalOverlay.className = 'terminal-overlay';
terminalOverlay.innerHTML = `
    <div class="terminal-header">
        <div class="d-flex align-items-center">
            <i class="bi bi-terminal me-2"></i>
            UART Shell
        </div>
        <button class="btn btn-link text-light p-0" onclick="toggleTerminal()">
            <i class="bi bi-x-lg"></i>
        </button>
    </div>
    <div class="terminal-content" id="terminal">
        <div class="terminal-line">Welcome to UART Interface</div>
        <div class="terminal-line">Type 'help' to see available commands</div>
    </div>
    <div class="terminal-input-area">
        <span class="terminal-prompt">></span>
        <input type="text" class="terminal-input" id="terminalInput" placeholder="Type a command..." autocomplete="off">
    </div>
`;
document.body.appendChild(terminalOverlay);

// Add event listener for terminal input
document.addEventListener('DOMContentLoaded', function() {
    // Initialize terminal input
    const terminalInput = document.getElementById('terminalInput');
    if (terminalInput) {
        terminalInput.addEventListener('keypress', function(e) {
            if (e.key === 'Enter' && this.value.trim()) {
                const command = this.value.trim();
                // Show the command in terminal
                appendToTerminal(`> ${command}`);
                // Send command to UART
                sendCommand(command);
                // Clear input
                this.value = '';
            }
        });
    }

    // Initialize WebSocket connection
    uartConnection = new WebSocket(`ws://${window.location.hostname}/ws`);

    // Setup WebSocket event handlers
    uartConnection.onopen = () => {
        console.log('WebSocket connected');
        isConnected = true;
        updateConnectionStatus(isConnected);
        appendToTerminal('Connected to device.');
        loadWelcomeMessage();
    };

    uartConnection.onmessage = (event) => {
        try {
            const cleanData = event.data.replace(/[\u0000-\u001F\u007F-\u009F]/g, '');
            const data = JSON.parse(cleanData);
            processMessage(data);
        } catch (error) {
            console.error('Message parsing error:', error);
            appendToTerminal('Error: Invalid data received');
        }
    };

    uartConnection.onclose = () => {
        console.log('WebSocket disconnected');
        isConnected = false;
        updateConnectionStatus(isConnected);
        appendToTerminal('Disconnected from device.');
    };

    uartConnection.onerror = (error) => {
        console.error('WebSocket error:', error);
        appendToTerminal('Connection error occurred.');
    };

    function processMessage(data) {
        console.log('Received data:', data);
        switch (data.type) {
            case 'serial_data':
                appendToTerminal(data.message);
                break;
            case 'network_scan':
                displayNetworks(data.networks);
                break;
            case 'command_status':
                handleCommandStatus(data);
                break;
            default:
                console.warn('Unknown message type:', data.type);
                appendToTerminal(`[ERROR] Unknown message type: ${data.type}`);
        }
    }
});

document.addEventListener('DOMContentLoaded', function() {
    // Open the web shell when the page loads
    toggleTerminal();
});

function fetchDeviceInfo() {
    if (!isConnected) {
        console.log('Not connected');
        return;
    }
    
    // Send both commands with a small delay
    sendCommand('sys');
    setTimeout(() => {
        sendCommand('wifi');
    }, 500);
}

function processUARTData(data) {
    // Remove [INFO] prefix if present
    if (data.startsWith('[INFO]')) {
        data = data.substring(6).trim();
    }

    // Append incoming data to the terminal
    appendToTerminal(data);

    // Parse and store data for sniff table
    const parsedData = parseUARTData(data);
    if (parsedData) {
        window.sniffData.push(parsedData);
        updateSniffTable();
    }
}

function updateSysInfoDisplay() {
    const sysInfo = document.getElementById('sysInfo');
    if (!sysInfo) return;
    
    let html = '<div class="info-grid">';
    if (systemData.cpuClock) {
        html += `<div class="info-item">
            <span class="info-label"><i class="bi bi-speedometer2 me-2"></i>CPU Clock:</span>
            <span class="info-value">${systemData.cpuClock}</span>
        </div>`;
    }
    if (systemData.freeHeap) {
        html += `<div class="info-item">
            <span class="info-label"><i class="bi bi-memory me-2"></i>Free Heap:</span>
            <span class="info-value">${systemData.freeHeap}</span>
        </div>`;
    }
    if (systemData.minFreeHeap) {
        html += `<div class="info-item">
            <span class="info-label"><i class="bi bi-graph-down me-2"></i>Min Free Heap:</span>
            <span class="info-value">${systemData.minFreeHeap}</span>
        </div>`;
    }
    html += '</div>';
    sysInfo.innerHTML = html;
}

function updateWifiInfoDisplay() {
    const wifiInfo = document.getElementById('wifiInfo');
    if (!wifiInfo) return;
    
    let html = '<div class="info-grid">';
    if (wifiData.macAddress) {
        html += `<div class="info-item">
            <span class="info-label"><i class="bi bi-upc-scan me-2"></i>MAC Address:</span>
            <span class="info-value">${wifiData.macAddress}</span>
        </div>`;
    }
    if (wifiData.channel) {
        html += `<div class="info-item">
            <span class="info-label"><i class="bi bi-broadcast me-2"></i>Channel:</span>
            <span class="info-value">${wifiData.channel}</span>
        </div>`;
    }
    if (wifiData.ipAddress) {
        html += `<div class="info-item">
            <span class="info-label"><i class="bi bi-globe me-2"></i>IP Address:</span>
            <span class="info-value">${wifiData.ipAddress}</span>
        </div>`;
    }
    if (wifiData.signalStrength) {
        html += `<div class="info-item">
            <span class="info-label"><i class="bi bi-reception-4 me-2"></i>Signal Strength:</span>
            <span class="info-value">${wifiData.signalStrength}</span>
        </div>`;
    }
    html += '</div>';
    wifiInfo.innerHTML = html;
}

function clearDeviceInfo() {
    systemData = {
        cpuClock: null,
        freeHeap: null,
        minFreeHeap: null
    };
    
    wifiData = {
        macAddress: null,
        channel: null,
        ipAddress: null,
        signalStrength: null
    };
    
    // Update displays with empty data
    updateSysInfoDisplay();
    updateWifiInfoDisplay();
}

// Initial UI update
updateConnectionStatus(false);

// Add navigation handling to DOMContentLoaded
const navLinks = document.querySelectorAll('#sidebar .nav-link');
navLinks.forEach(link => {
    link.addEventListener('click', (e) => {
        e.preventDefault();
        // Remove active class from all links
        navLinks.forEach(l => l.classList.remove('active'));
        // Add active class to clicked link
        link.classList.add('active');
        
        // Load content based on href
        const target = link.getAttribute('href').substring(1);
        loadContent(target);
    });
});

// Load dashboard by default
loadContent('dashboard');

function toggleTerminal() {
    const terminal = document.querySelector('.terminal-overlay');
    if (terminal) {
        terminal.classList.toggle('show');
        // Focus input when terminal is shown
        if (terminal.classList.contains('show')) {
            const input = terminal.querySelector('.terminal-input');
            if (input) {
                input.focus();
            }
        }
    }
}

// Update appendToTerminal to auto-scroll
function appendToTerminal(data) {
    const terminal = document.getElementById('terminal');
    if (terminal) {
        const line = document.createElement('div');
        line.className = 'terminal-line';
        line.textContent = data;
        terminal.appendChild(line);
        // Auto-scroll to bottom
        terminal.scrollTop = terminal.scrollHeight;
    }
}

function loadDashboard(container) {
    container.innerHTML = `
        <div class="row g-4">
            <!-- Quick Actions -->
            <div class="col-12">
                <div class="card">
                    <div class="card-body">
                            <h5 class="card-title">Quick Actions</h5>
                                <div class="quick-actions">
                                    <div class="row g-3">
                                        <div class="col">
                                            <button class="btn btn-dark w-100 text-start px-2 py-2" onclick="handleQuickAction('scan')" id="scanBtn">
                                                <i class="bi bi-wifi me-2"></i>
                                                    Scan
                                                </button>
                                            </div>
                                            <div class="col">
                                                <button class="btn btn-dark w-100 text-start px-2 py-2" onclick="handleQuickAction('start')" id="startBtn">
                                                    <i class="bi bi-play-fill me-2"></i>
                                                    Deauth
                                                </button>
                                            </div>
                                            <div class="col">
                                                <button class="btn btn-dark w-100 text-start px-2 py-2" onclick="handleQuickAction('disassoc')" id="disassocBtn">
                                                    <i class="bi bi-x-circle me-2"></i>
                                                    Disassoc
                                                </button>
                                            </div>
                                            <div class="col">
                                                <button class="btn btn-dark w-100 text-start px-2 py-2" onclick="handleQuickAction('random')" id="randomBtn">
                                                    <i class="bi bi-shuffle me-2"></i>
                                                    Random
                                                </button>
                                            </div>
                                            <div class="col">
                                                <button class="btn btn-dark w-100 text-start px-2 py-2" onclick="handleQuickAction('stop')" id="stopBtn">
                                                    <i class="bi bi-stop-fill me-2"></i>
                                                    Stop
                                                </button>
                                            </div>
                                            <div class="col">
                                                <button class="btn btn-dark w-100 text-start px-2 py-2" onclick="handleQuickAction('help')" id="helpBtn">
                                                    <i class="bi bi-question-circle me-2"></i>
                                                    Help
                                                </button>
                                            </div>
                                            <div class="col">
                                                <button class="btn btn-light w-100 text-start px-2 py-2" onclick="handleQuickAction('results')" id="resultsBtn">
                                                    <i class="bi bi-clipboard-data me-2"></i>
                                                    Results
                                                </button>
                                            </div>
                                            <div class="col">
                                                <button id="randomAttackBtn" class="btn btn-dark w-100 text-start px-2 py-2">
                                                    <i class="bi bi-lightning-fill me-2"></i>
                                                    Random Attack
                                                </button>
                                            </div>
                                            <div class="col">
                                                <input type="number" id="attackTimeInput" class="form-control bg-dark text-light" placeholder="Attack Time (ms)">
                                                Attack Time (ms)
                                            </div>
                                            <div class="col">
                                                <input type="text" id="targetInput" class="form-control bg-dark text-light" placeholder="Set Target">
                                                Set Target(s)
                                            </div>
                                            <div class="col">
                                                <button id="setTargetBtn" class="btn btn-dark w-100 text-start px-2 py-2">
                                                    <i class="bi bi-crosshair me-2"></i>
                                                    Set Target
                                                </button>
                                            </div>
                                            <div class="col">
                                                <button id="executeAttackBtn" class="btn btn-dark w-100 text-start px-2 py-2">
                                                    <i class="bi bi-exclamation-triangle-fill me-2"></i>
                                                    Execute Attack
                                                </button>
                                            </div>
                                        </div>
                                    </div>
                                </div>
                    </div>
                </div>
            </div>
        </div>

        <!-- Scan Results will be appended here -->
        <div id="scanResults" class="mt-4"></div>
    `;
       
    // Update UI based on current connection status
    updateConnectionStatus(isConnected);
}

function loadNetworks(container) {
    container.innerHTML = `
        <div class="card">
            <div class="card-body">
                <h5 class="card-title">Network Scanner</h5>
                <div class="mb-4">
                    <button class="btn btn-primary" onclick="handleQuickAction('scan')" id="scanBtn">
                        <i class="bi bi-wifi me-2"></i>Scan Networks
                    </button>
                </div>
                <div id="scanResults">
                    <div class="alert alert-info">
                        Click "Scan Networks" to start scanning for available networks.
                    </div>
                </div>
            </div>
        </div>
    `;
    updateConnectionStatus(isConnected);
}

function loadControls(container) {
    container.innerHTML = `
        <div class="card">
            <div class="card-body">
                <h5 class="card-title">UART Controls</h5>
                <div class="mb-4">
                    <div class="input-group">
                        <input type="text" id="commandInput" class="form-control" placeholder="Enter custom command...">
                        <button class="btn btn-primary" id="sendCommandBtn" onclick="sendCustomCommand()">
                            <i class="bi bi-send me-2"></i>Send
                        </button>
                    </div>
                </div>
                <div class="terminal-container">
                    <div id="terminal" class="bg-black p-3 rounded">
                        <pre class="terminal-welcome">Enter a command above to interact with the UART.</pre>
                    </div>
                </div>
            </div>
        </div>
    `;
    updateConnectionStatus(isConnected);
}

function loadSettings(container) {
    container.innerHTML = `
        <div class="card">
            <div class="card-body">
                <h5 class="card-title">Settings</h5>
                <form>
                    <div class="mb-3">
                        <label for="cycleDelay" class="form-label">Attack Cycle Delay (ms)</label>
                        <input type="number" class="form-control" id="cycleDelay" value="2000">
                    </div>
                    <div class="mb-3">
                        <label for="scanTime" class="form-label">Scan Duration (ms)</label>
                        <input type="number" class="form-control" id="scanTime" value="5000">
                    </div>
                    <div class="mb-3">
                        <label for="numFrames" class="form-label">Number of Frames per Burst</label>
                        <input type="number" class="form-control" id="numFrames" value="3">
                    </div>
                    <button type="button" class="btn btn-primary" onclick="saveSettings()">
                        <i class="bi bi-save me-2"></i>Save Settings
                    </button>
                </form>
            </div>
        </div>
    `;
}

function loadLogs(container) {
    container.innerHTML = `
        <div class="row g-4">
            <!-- About and Usage Instructions -->
            <div class="col-12">
                <div class="card">
                    <div class="card-body">
                        <h5 class="about-usage-title mb-0">
                            <i class="bi bi-info-circle me-2"></i>
                            About and Usage Instructions
                        </h5>
                        <p>This section provides detailed information on how to use the Evil BW16 Web Interface effectively.</p>
                        <ul>
                            <li><strong>Dashboard:</strong>
                                <ul>
                                    <li>Quick action buttons for tasks like network scanning and attack initiation.</li>
                                </ul>
                            </li>
                            </li>
                            <li><strong>SniffMaster:</strong>
                                <ul>
                                    <li>Analyze network traffic and manage sniffing operations.</li>
                                    <li>Configure parameters such as channel and frequency for data capture.</li>
                                </ul>
                            </li>
                            <li><strong>Set Target:</strong>
                                <ul>
                                    <li>Specify a target for operations using MAC or IP addresses.</li>
                                </ul>
                            </li>
                            <li><strong>Execute Attack:</strong>
                                <ul>
                                    <li>Initiate network attacks with various types, including deauthentication and disassociation.</li>
                                    <li>Monitor attack progress and results in the command output section.</li>
                                </ul>
                            </li>
                            <li><strong>Command Reference:</strong>
                                <ul>
                                    <li><strong>start deauther</strong>: Begin the deauth attack cycle.</li>
                                    <li><strong>stop deauther</strong>: Stop all attack cycles.</li>
                                    <li><strong>scan</strong>: Perform a WiFi scan and display results.</li>
                                    <li><strong>results</strong>: Show last scan results.</li>
                                    <li><strong>disassoc</strong>: Begin continuous disassociation attacks.</li>
                                    <li><strong>random_attack</strong>: Deauth a randomly chosen AP from the scan list.</li>
                                    <li><strong>attack_time <ms></strong>: Start a timed attack for the specified duration.</li>
                                    <li><strong>start sniff</strong>: Enable the sniffer with ALL mode.</li>
                                    <li><strong>sniff beacon/probe/deauth/eapol/pwnagotchi/all</strong>: Enable/Disable specific frame captures.</li>
                                    <li><strong>stop sniff</strong>: Stop sniffing.</li>
                                    <li><strong>hop on/off</strong>: Enable/Disable channel hopping.</li>
                                    <li><strong>set <key> <value></strong>: Update configuration values (e.g., channels, target APs, cycle delay).</li>
                                </ul>
                            </li>
                        </ul>
                        <p><strong>Responsible Use:</strong>
                            <ul>
                                <li>Ensure compliance with laws and regulations.</li>
                                <li>Use tools responsibly and ethically, considering network security and privacy impacts.</li>
                            </ul>
                        </p>
                    </div>
                </div>
            </div>
        </div>
    `;
}

function loadSniffMaster(container) {
    container.innerHTML = `
        <div class="container-fluid py-4">
            <div class="card bg-darker mb-4">
                <div class="card-body">
                    <h6 class="card-subtitle mb-3">Quick Actions</h6>
                    <div class="row g-3">
                        <div class="col-md-2">
                            <button class="btn btn-darker w-100 text-start px-3 py-2" onclick="handleSniffAction('all')">
                                <i class="bi bi-reception-4 me-2"></i>
                                Sniff All
                            </button>
                        </div>
                        <div class="col-md-2">
                            <button class="btn btn-darker w-100 text-start px-3 py-2" onclick="handleSniffAction('beacon')">
                                <i class="bi bi-broadcast me-2"></i>
                                Beacon
                            </button>
                        </div>
                        <div class="col-md-2">
                            <button class="btn btn-darker w-100 text-start px-3 py-2" onclick="handleSniffAction('probe')">
                                <i class="bi bi-search me-2"></i>
                                Probe
                            </button>
                        </div>
                        <div class="col-md-2">
                            <button class="btn btn-darker w-100 text-start px-3 py-2" onclick="handleSniffAction('deauth')">
                                <i class="bi bi-x-circle me-2"></i>
                                Deauth
                            </button>
                        </div>
                        <div class="col-md-2">
                            <button class="btn btn-darker w-100 text-start px-3 py-2" onclick="handleSniffAction('eapol')">
                                <i class="bi bi-shield-lock me-2"></i>
                                EAPOL
                            </button>
                        </div>
                        <div class="col-md-2">
                            <button class="btn btn-darker w-100 text-start px-3 py-2" onclick="handleSniffAction('pwnagotchi')">
                                <i class="bi bi-robot me-2"></i>
                                Pwnagotchi
                            </button>
                        </div>
                        <div class="col-md-2">
                            <button class="btn btn-darker w-100 text-start px-3 py-2" onclick="handleSniffAction('stop')" id="stopSniffButton">
                                <i class="bi bi-stop-circle me-2"></i>
                                STOP
                            </button>
                        </div>
                        <div class="col-md-2">
                            <select class="form-select btn-darker text-light border-secondary py-2 h-100" id="hopSelect" style="height: 42px !important; border-color: rgba(255,255,255,0.1) !important; background-color: #1a1a1a !important;">
                                <option value="off">Hop: Off</option>
                                <option value="on">Hop: On</option>
                            </select>
                        </div>
                        <div class="col-md-3">
                            <div class="input-group" style="height: 42px;">
                                <span class="input-group-text btn-darker text-light border-secondary" style="border-color: rgba(255,255,255,0.1) !important; background-color: #1a1a1a !important;">Ch:</span>
                                <input type="text" class="form-control btn-darker text-light" id="channelInput" placeholder="e.g., 1,6,11" style="background-color: #1a1a1a !important; border-color: rgba(255,255,255,0.1) !important;">
                            </div>
                        </div>
                        <div class="col-md-1">
                            <button class="btn btn-darker w-100 text-start px-3 py-2" onclick="saveHopSettings()" id="saveSettingsButton">
                                <i class="bi bi-save me-2"></i>
                                Save
                            </button>
                        </div>
                    </div>
                </div>
            </div>
    `;
    
    // Initialize the sniff data array
    window.sniffData = window.sniffData || [];

    // Initialize UART event listeners
    document.addEventListener('DOMContentLoaded', function() {
        // UART data received
        appendToTerminal('[INFO] UART data received');
        // Process UART data
        processUARTData();
    });
}

// Function to handle incoming UART data
function processUARTData(data) {
    console.log('Received UART data:', data);

    if (!data) {
        console.warn('No data received');
        return;
    }

    // If data is a string, parse it
    const dataStr = typeof data === 'object' && data.data ? data.data : data;
    
    const parsedData = parseUARTData(dataStr);
    if (parsedData) {
        console.log('Parsed packet:', parsedData);
        
        // Skip CMD type packets
        if (parsedData.type === 'CMD') {
            return;
        }
        
        // Add to data array
        window.sniffData.unshift(parsedData);

        // Limit array size to prevent memory issues
        if (window.sniffData.length > 1000) {
            window.sniffData = window.sniffData.slice(0, 1000);
        }

        // Update table
        updateSniffTable();
    }
}

// Function to parse UART data from serial output
function parseUARTData(data) {
    console.log('Parsing UART data:', data);
    
    if (!data || typeof data !== 'string') {
        console.warn('Invalid data received:', data);
        return null;
    }

    // Skip any non-packet data
    if (!data.match(/^\[(MGMT|DATA|CTRL|EXT)\]/)) {
        return null;
    }

    // Skip status messages and commands
    if (data.includes('Connected to UART') ||
        data.includes('#calibration_ok') ||
        data.includes('[INFO]') ||
        data.includes('[CMD]') ||
        data.includes('Initializing') ||
        data.includes('initialized') ||
        data.includes('Starting') ||
        data.includes('Stopping') ||
        data.includes('> ')) {
        return null;
    }

    let parsed = {
        frame_type: '',
        frame_subtype: '',
        source_mac: '',
        dest_mac: '',
        ssid: '',
        reason_code: '',
        is_pwnagotchi: false
    };

    try {
        // Extract frame type from the prefix [MGMT], [DATA], etc.
        const frameTypeMatch = data.match(/^\[(\w+)\]/);
        if (frameTypeMatch) {
            switch (frameTypeMatch[1]) {
                case 'MGMT': parsed.frame_type = 'Management'; break;
                case 'DATA': parsed.frame_type = 'Data'; break;
                case 'CTRL': parsed.frame_type = 'Control'; break;
                case 'EXT': parsed.frame_type = 'Extension'; break;
                default: return null;
            }
        } else {
            return null;
        }

        // Extract frame subtype directly from the data
        if (data.includes('Beacon')) {
            parsed.frame_subtype = 'Beacon';
        } else if (data.includes('Probe Request')) {
            parsed.frame_subtype = 'Probe Request';
        } else if (data.includes('Probe Response')) {
            parsed.frame_subtype = 'Probe Response';
        } else if (data.includes('Deauth')) {
            parsed.frame_subtype = 'Deauthentication';
        } else if (data.includes('Disassoc')) {
            parsed.frame_subtype = 'Disassociation';
        } else if (data.includes('EAPOL')) {
            parsed.frame_subtype = 'EAPOL';
        } else if (data.includes('Authentication')) {
            parsed.frame_subtype = 'Authentication';
        } else if (data.includes('Association Request')) {
            parsed.frame_subtype = 'Association Request';
        } else if (data.includes('Association Response')) {
            parsed.frame_subtype = 'Association Response';
        }

        // Extract MAC addresses
        const macAddresses = data.match(/([0-9A-Fa-f]{2}[:-]){5}([0-9A-Fa-f]{2})/g);
        if (macAddresses) {
            if (macAddresses.length >= 1) parsed.source_mac = macAddresses[0];
            if (macAddresses.length >= 2) parsed.dest_mac = macAddresses[1];
        }

        // Extract SSID if present
        const ssidMatch = data.match(/SSID:\s*([^\n]+)/);
        if (ssidMatch) {
            parsed.ssid = ssidMatch[1].trim();
        }

        // Extract reason code for deauth frames
        const reasonMatch = data.match(/Reason:\s*(\d+)/);
        if (reasonMatch) {
            parsed.reason_code = reasonMatch[1];
        }

        // Only return if we have at least some valid data
        if (parsed.frame_type && (parsed.source_mac || parsed.dest_mac || parsed.ssid)) {
            return parsed;
        }

        return null;
    } catch (error) {
        console.error('Error parsing UART data:', error);
        return null;
    }
}

// Function to update the sniff table
function updateSniffTable() {
    const tableBody = document.getElementById('sniffTableBody');
    tableBody.innerHTML = ''; // Clear existing table data

    window.sniffData.forEach(entry => {
        const row = document.createElement('tr');
        row.innerHTML = `
            <td>${entry.frame_type}</td>
            <td>${entry.frame_subtype}</td>
            <td>${entry.source_mac}</td>
            <td>${entry.dest_mac}</td>
            <td>${entry.ssid}</td>
            <td>${entry.reason_code}</td>
            <td>${entry.is_pwnagotchi}</td>
        `;
        tableBody.appendChild(row);
    });
}

// Function to sort the sniff table
function sortSniffTable(columnIndex) {
    const thead = document.querySelector('#sniffTable thead');
    const headers = thead.querySelectorAll('th');
    const currentHeader = headers[columnIndex];
    const isAscending = currentHeader.getAttribute('data-sort') === 'asc';

    // Remove sort indicators from all headers
    headers.forEach(header => header.removeAttribute('data-sort'));
    
    // Add sort indicator to current header
    currentHeader.setAttribute('data-sort', isAscending ? 'desc' : 'asc');

    // Get the property name based on column index
    const properties = ['frame_type', 'frame_subtype', 'source_mac', 'dest_mac', 'ssid', 'reason_code', 'is_pwnagotchi'];
    const property = properties[columnIndex];

    // Sort the data
    window.sniffData.sort((a, b) => {
        let valueA = a[property];
        let valueB = b[property];

        if (isAscending) {
            return valueA > valueB ? -1 : valueA < valueB ? 1 : 0;
        } else {
            return valueA < valueB ? -1 : valueA > valueB ? 1 : 0;
        }
    });

    // Update the table
    updateSniffTable();
}

function saveHopSettings() {
    const hopSelect = document.getElementById('hopSelect');
    const channelInput = document.getElementById('channelInput');
    const hopValue = hopSelect.value;
    const channels = channelInput.value.trim();

    // Send hop command
    sendUARTCommand(`hop ${hopValue}`);
    
    // If channels are specified and not empty, send channel command
    if (channels) {
        sendUARTCommand(`ch ${channels}`);
    }
    
    // Show feedback in terminal
    appendToTerminal(`> Set hop ${hopValue}${channels ? ` and channels: ${channels}` : ''}`);
}

function handleSniffAction(action) {
    let command = '';
    window.currentSniffMode = action;

    if (action === 'stop') {
        hideSniffingIndicator();
        command = 'stop sniff';  
    } else if (action === 'all') {
        command = 'start sniff';  
        showSniffingIndicator(action);
    } else {
        command = `sniff ${action}`;
        showSniffingIndicator(action);
    }

    function showSniffingIndicator(mode) {
        const indicator = document.getElementById('sniffingIndicator');
        const textElement = document.getElementById('sniffModeText');
        
        if (!indicator || !textElement) {
            console.error('Sniffing indicator elements not found');
            return;
        }
        
        // Set the appropriate text based on mode
        let displayText = '';
        switch (mode) {
            case 'all':
                displayText = 'Sniffing All Packets';
                break;
            case 'beacon':
                displayText = 'Sniffing Beacon Frames';
                break;
            case 'probe':
                displayText = 'Sniffing Probe Requests';
                break;
            case 'deauth':
                displayText = 'Sniffing Deauth Frames';
                break;
            case 'eapol':
                displayText = 'Sniffing EAPOL Frames';
                break;
            case 'pwnagotchi':
                displayText = 'Sniffing Pwnagotchi Beacons';
                break;
            default:
                displayText = 'Sniffing in Progress';
        }
        
        // Update text and show indicator
        textElement.textContent = displayText;
        indicator.style.display = 'flex';
        indicator.classList.remove('d-none');
        
        // Force a reflow
        void indicator.offsetWidth;
        
        // Add show class
        indicator.classList.add('show');
        
        console.log('Showing sniffing indicator:', displayText); 
    }
    
    function hideSniffingIndicator() {
        const indicator = document.getElementById('sniffingIndicator');
        
        if (!indicator) {
            console.error('Sniffing indicator element not found');
            return;
        }
        
        // Remove show class
        indicator.classList.remove('show');
        
        // Wait for animation to complete before hiding
        setTimeout(() => {
            indicator.classList.add('d-none');
            indicator.style.display = 'none';
        }, 300);
        
        console.log('Hiding sniffing indicator'); 
    }

    console.log('Queueing command:', command); 
    commandQueue.push(command);
}

// Command queue
let commandQueue = [];
let dataQueue = [];

function processCommandQueue() {
    if (isConnected) {
        // Continuously prioritize command queue
        while (commandQueue.length > 0) {
            if (uartConnection.bufferedAmount < 1024) {
                const command = commandQueue.shift();
                sendUARTCommand(command);
                console.log('[INFO] Command sent from queue:', command);
            } else {
                console.warn('[WARNING] UART buffer full, retrying command');
                break; // Exit loop to retry later
            }
        }
        // Process data queue only if command queue is empty
        if (commandQueue.length === 0) {
            while (dataQueue.length > 0) {
                if (uartConnection.bufferedAmount < 1024) {
                    const data = dataQueue.shift();
                    sendUARTData(data);
                    console.log('[INFO] Data sent from queue:', data);
                } else {
                    console.warn('[WARNING] UART buffer full, retrying data');
                    break; // Exit loop to retry later
                }
            }
        }
    }
}

function sendUARTData(data) {
    if (isConnected && uartConnection) {
        uartConnection.send(data);
        appendToTerminal(`[UART DATA SENT] ${data}`);
    } else {
        appendToTerminal('[ERROR] UART not connected for data');
    }
}

setInterval(processCommandQueue, 50); // Check the queue every 50ms for faster processing

function displayNetworks(networks) {
    // Hide scanning visualization
    const scanViz = document.querySelector('.scan-visualization');
    if (scanViz) {
        scanViz.style.display = 'none';
    }

    console.log('Displaying networks:', networks);
    const scanResults = document.getElementById('scanResults');
    if (!scanResults) return;

    // Sort networks by signal strength (RSSI) in descending order
    networks.sort((a, b) => b.rssi - a.rssi);

    // Calculate statistics
    const totalNetworks = networks.length;
    const networks24G = networks.filter(n => n.type === '2.4GHz').length;
    const networks5G = networks.filter(n => n.type === '5GHz').length;
    
    // Calculate channel distribution
    const channelCounts = {};
    networks.forEach(network => {
        channelCounts[network.channel] = (channelCounts[network.channel] || 0) + 1;
    });

    // Create channel graph data
    const maxChannelCount = Math.max(...Object.values(channelCounts));
    const channelBars = Object.entries(channelCounts)
        .map(([channel, count]) => {
            const height = (count / maxChannelCount) * 100;
            const channelNum = parseInt(channel);
            const channelType = channelNum > 14 ? 'channel-5ghz' : 'channel-2ghz';
            return `<div class="channel-bar ${channelType}" style="height: ${height}%" title="Channel ${channel}: ${count} networks"></div>`;
        })
        .join('');

    scanResults.innerHTML = `
        <div class="card mb-3">
            <div class="card-body d-flex justify-content-end">
                <button class="btn btn-light me-2" onclick="exportToCSV()">
                    <i class="bi bi-file-earmark-spreadsheet me-2"></i>
                    Export CSV
                </button>
                <button class="btn btn-light" onclick="attackSelectedTargets()" id="attackSelectedBtn" disabled>
                    <i class="bi bi-lightning me-2"></i>
                    Attack Selected
                </button>
            </div>
        </div>

        <div class="network-stats mb-3">
            <div class="row g-3">
                <div class="col-md-3">
                    <div class="stat-card">
                        <div class="stat-icon">
                            <i class="bi bi-wifi"></i>
                        </div>
                        <div class="stat-info">
                            <div class="stat-value">${totalNetworks}</div>
                            <div class="stat-label">Total Networks</div>
                        </div>
                    </div>
                </div>
                <div class="col-md-3">
                    <div class="stat-card">
                        <div class="stat-icon">
                            <i class="bi bi-broadcast"></i>
                        </div>
                        <div class="stat-info">
                            <div class="stat-value">${networks24G}</div>
                            <div class="stat-label">2.4GHz Networks</div>
                        </div>
                    </div>
                </div>
                <div class="col-md-3">
                    <div class="stat-card">
                        <div class="stat-icon">
                            <i class="bi bi-broadcast-pin"></i>
                        </div>
                        <div class="stat-info">
                            <div class="stat-value">${networks5G}</div>
                            <div class="stat-label">5GHz Networks</div>
                        </div>
                    </div>
                </div>
                <div class="col-md-3">
                    <div class="stat-card">
                        <div class="stat-icon">
                            <i class="bi bi-bar-chart"></i>
                        </div>
                        <div class="stat-info">
                            <div class="channel-graph">
                                ${channelBars}
                            </div>
                            <div class="stat-label">Channel Distribution</div>
                        </div>
                    </div>
                </div>
            </div>
        </div>

        <div class="card">
            <div class="card-body p-0">
                <div class="table-container">
                    <table class="table table-hover align-middle mb-0" id="networksTable">
                        <thead>
                            <tr>
                                <th><input type="checkbox" id="selectAll" onclick="toggleSelectAll()"></th>
                                <th class="sortable" onclick="sortTable(1)">ID <i class="bi bi-arrow-down-up"></i></th>
                                <th class="sortable" onclick="sortTable(2)">SSID <i class="bi bi-arrow-down-up"></i></th>
                                <th class="sortable" onclick="sortTable(3)">Channel <i class="bi bi-arrow-down-up"></i></th>
                                <th class="sortable" onclick="sortTable(4)">Signal <i class="bi bi-arrow-down-up"></i></th>
                                <th class="sortable" onclick="sortTable(5)">Type <i class="bi bi-arrow-down-up"></i></th>
                                <th>Action</th>
                            </tr>
                        </thead>
                        <tbody>
                            ${networks.map(network => {
                                const rssiPercentage = Math.min(100, Math.max(0, (network.rssi + 100) * 2));
                                const signalClass = getSignalClass(network.rssi);
                                return `
                                    <tr>
                                        <td><input type="checkbox" class="network-select" value="${network.id}" onchange="updateAttackSelectedButton()"></td>
                                        <td>${network.id}</td>
                                        <td>
                                            <div class="d-flex align-items-center">
                                                <i class="bi bi-wifi me-2"></i>
                                                ${network.ssid}
                                            </div>
                                        </td>
                                        <td>${network.channel}</td>
                                        <td class="${signalClass} rounded px-2">
                                            <div class="d-flex align-items-center">
                                                <div class="signal-strength" style="width: ${rssiPercentage}%"></div>
                                                <small class="ms-2">${network.rssi} dBm</small>
                                            </div>
                                        </td>
                                        <td>${network.type}</td>
                                        <td>
                                            <button class="btn btn-light w-100 text-start px-3 py-2" onclick="setTarget(${network.id})">
                                                <i class="bi bi-crosshair me-2"></i>
                                                Select Target
                                            </button>
                                        </td>
                                    </tr>
                                `;
                            }).join('')}
                        </tbody>
                    </table>
                </div>
            </div>
        </div>
    `;
    
    // Update attack selected button state
    updateAttackSelectedButton();
}

function toggleSelectAll() {
    const selectAll = document.getElementById('selectAll');
    const checkboxes = document.querySelectorAll('.network-select');
    checkboxes.forEach(checkbox => {
        checkbox.checked = selectAll.checked;
    });
    updateAttackSelectedButton();
}

function updateAttackSelectedButton() {
    const attackBtn = document.getElementById('attackSelectedBtn');
    const selectedCount = document.querySelectorAll('.network-select:checked').length;
    attackBtn.disabled = selectedCount === 0;
}

function attackSelectedTargets() {
    const selectedTargets = Array.from(document.querySelectorAll('.network-select:checked'))
        .map(checkbox => checkbox.value);

    if (selectedTargets.length > 0 && isConnected) {
        sendUARTCommand(`set target ${selectedTargets.join(', ')}`); 
        appendToTerminal(`> Set target ${selectedTargets.join(', ')}`); 
    } else {
        appendToTerminal('No targets selected or not connected to UART');
    }
}

function sortTable(columnIndex) {
    const table = document.getElementById('networksTable');
    const tbody = table.getElementsByTagName('tbody')[0];
    const rows = Array.from(tbody.getElementsByTagName('tr'));
    const isNumeric = columnIndex === 1 || columnIndex === 3 || columnIndex === 4; // ID, Channel, Signal

    // Get current sort direction
    const th = table.querySelector(`th:nth-child(${columnIndex + 1})`);
    const currentDir = th.getAttribute('data-sort') === 'asc' ? 'desc' : 'asc';
    
    // Update sort direction indicators
    table.querySelectorAll('th').forEach(header => header.removeAttribute('data-sort'));
    
    // Add sort indicator to current header
    th.setAttribute('data-sort', currentDir);

    // Get the property name based on column index
    const properties = ['id', 'ssid', 'channel', 'rssi', 'type'];
    const property = properties[columnIndex - 1];

    // Sort the data
    rows.sort((a, b) => {
        let aValue = a.cells[columnIndex].textContent.trim();
        let bValue = b.cells[columnIndex].textContent.trim();

        if (columnIndex === 4) { // Signal strength column
            aValue = parseInt(aValue.match(/-\d+/)[0]);
            bValue = parseInt(bValue.match(/-\d+/)[0]);
        } else if (isNumeric) {
            aValue = parseInt(aValue);
            bValue = parseInt(bValue);
        }

        if (currentDir === 'asc') {
            return isNumeric ? aValue - bValue : aValue.localeCompare(bValue);
        } else {
            return isNumeric ? bValue - aValue : bValue.localeCompare(aValue);
        }
    });

    rows.forEach(row => tbody.appendChild(row));
}

function exportToCSV() {
    const table = document.getElementById('networksTable');
    const rows = Array.from(table.querySelectorAll('tr'));
    
    const csvContent = rows.map(row => {
        return Array.from(row.querySelectorAll('th:not(:first-child), td:not(:first-child):not(:last-child)'))
            .map(cell => {
                let text = cell.textContent.trim();
                // Handle signal strength column
                if (text.includes('dBm')) {
                    text = text.match(/-\d+/)[0];
                }
                // Escape quotes and wrap in quotes if contains comma
                text = text.replace(/"/g, '""');
                return text.includes(',') ? `"${text}"` : text;
            })
            .join(',');
    }).join('\n');

    const blob = new Blob([csvContent], { type: 'text/csv;charset=utf-8;' });
    const link = document.createElement('a');
    link.href = URL.createObjectURL(blob);
    link.download = 'networks.csv';
    link.click();
}

function setTarget(id) {
    if (isConnected) {
        sendUARTCommand(`set target ${id}`); 
        appendToTerminal(`> Set target ${id}`); 
    } else {
        appendToTerminal('Not connected to UART');
    }
}

function createScanVisualization() {
    const visualization = document.createElement('div');
    visualization.className = 'scan-visualization';
    visualization.innerHTML = `
        <div class="scan-container">
            <div class="wifi-icon">
                <i class="bi bi-wifi"></i>
                <div class="scan-wave"></div>
                <div class="scan-wave"></div>
                <div class="scan-wave"></div>
            </div>
            <div class="scan-text">Scanning for networks...</div>
        </div>
    `;
    document.body.appendChild(visualization);
}

function showScanningIndicator() {
    const scanViz = document.querySelector('.scan-visualization');
    if (scanViz) {
        scanViz.style.display = 'block';
    }

    setTimeout(() => {
        if (scanViz) {
            scanViz.style.display = 'none';
        }
    }, 30000); // Hide after 30 seconds if not hidden by scan completion
}

function closeConfirmationModal() {
    const modal = document.getElementById('confirmationModal');
    if (modal) {
        modal.classList.remove('show');
    }
}

function toggleSettings() {
    const drawer = document.getElementById('settingsDrawer');
    if (drawer) {
        drawer.classList.toggle('show');
    }
}

function saveParameters() {
    if (!isConnected) {
        appendToTerminal('Error: Not connected to UART');
        return;
    }

    const form = document.getElementById('settingsForm');
    const formData = new FormData(form);
    const parameters = Object.fromEntries(formData.entries());

    // Send each parameter as a separate command
    Object.entries(parameters).forEach(([key, value]) => {
        const command = `set ${key} ${value}`;
        appendToTerminal(`> ${command}`);
        sendUARTCommand(command);
    });

    // Close the drawer after saving
    toggleSettings();
}

// Theme handling
function initializeTheme() {
    const savedTheme = localStorage.getItem('theme');
    const themeToggle = document.querySelector('.theme-toggle i');
    if (savedTheme === 'dark') {
        document.body.setAttribute('data-theme', 'dark');
        themeToggle.className = 'bi bi-sun';
    } else {
        document.body.setAttribute('data-theme', 'light');
        themeToggle.className = 'bi bi-moon-stars';
    }
    console.log('Theme initialized to:', savedTheme);
}

function toggleTheme() {
    const body = document.body;
    const themeToggle = document.querySelector('.theme-toggle i');
    const currentTheme = body.getAttribute('data-theme');
    console.log('Current theme:', currentTheme); // Debugging line
    
    if (currentTheme === 'dark') {
        body.setAttribute('data-theme', 'light');
        themeToggle.className = 'bi bi-moon-stars';
        localStorage.setItem('theme', 'light');
        console.log('Switched to light theme'); // Debugging line
    } else {
        body.setAttribute('data-theme', 'dark');
        themeToggle.className = 'bi bi-sun';
        localStorage.setItem('theme', 'dark');
        console.log('Switched to dark theme'); // Debugging line
    }
}

// Initialize theme from localStorage
document.addEventListener('DOMContentLoaded', function() {
    initializeTheme();
    const themeToggleBtn = document.querySelector('.theme-toggle');
    if (themeToggleBtn) {
        themeToggleBtn.addEventListener('click', toggleTheme);
    }
});

document.addEventListener('DOMContentLoaded', function() {
    const navLinks = document.querySelectorAll('#sidebar .nav-link');
    navLinks.forEach(link => {
        link.addEventListener('click', (e) => {
            e.preventDefault();
            // Remove active class from all links
            navLinks.forEach(l => l.classList.remove('active'));
            // Add active class to clicked link
            link.classList.add('active');
            
            // Load content based on href
            const target = link.getAttribute('href').substring(1);
            loadContent(target);
        });
    });
});

function showAttackVisualization() {
    const visualization = document.querySelector('.attack-visualization');
    if (visualization) {
        visualization.style.display = 'block';
        // Add animated class to packets
        const packets = visualization.querySelectorAll('.packet');
        packets.forEach(packet => {
            packet.classList.add('animated');
        });
    }
}

function hideAttackVisualization() {
    const visualization = document.querySelector('.attack-visualization');
    if (visualization) {
        visualization.style.display = 'none';
        // Remove animated class from packets
        const packets = visualization.querySelectorAll('.packet');
        packets.forEach(packet => {
            packet.classList.remove('animated');
        });
    }
}

function createAttackVisualization() {
    const visualization = document.createElement('div');
    visualization.className = 'attack-visualization';
    visualization.innerHTML = `
        <div class="device-router">
            <div class="device">
                <i class="bi bi-cpu"></i>
            </div>
            <div class="packets">
                <div class="packet"></div>
                <div class="packet"></div>
                <div class="packet"></div>
            </div>
            <div class="router">
                <i class="bi bi-router"></i>
            </div>
        </div>
    `;
    document.body.appendChild(visualization);
}

// Initialize WebSocket connection
function initUART() {
    // Initialize WebSocket connection
    uartConnection = new WebSocket(`ws://${window.location.hostname}/ws`);

    // Setup WebSocket event handlers
    uartConnection.onopen = () => {
        console.log('WebSocket connected');
        isConnected = true;
        updateConnectionStatus(isConnected);
        appendToTerminal('Connected to device.');
        loadWelcomeMessage();
    };

    uartConnection.onmessage = (event) => {
        try {
            const cleanData = event.data.replace(/[\u0000-\u001F\u007F-\u009F]/g, '');
            const data = JSON.parse(cleanData);
            processMessage(data);
        } catch (error) {
            console.error('Message parsing error:', error);
            appendToTerminal('Error: Invalid data received');
        }
    };

    uartConnection.onclose = () => {
        console.log('WebSocket disconnected');
        isConnected = false;
        updateConnectionStatus(isConnected);
        appendToTerminal('Disconnected from device.');
    };

    uartConnection.onerror = (error) => {
        console.error('WebSocket error:', error);
        appendToTerminal('Connection error occurred.');
    };

    function processMessage(data) {
        console.log('Received data:', data);
        switch (data.type) {
            case 'serial_data':
                appendToTerminal(data.message);
                break;
            case 'network_scan':
                displayNetworks(data.networks);
                break;
            case 'command_status':
                handleCommandStatus(data);
                break;
            default:
                console.warn('Unknown message type:', data.type);
                appendToTerminal(`[ERROR] Unknown message type: ${data.type}`);
        }
    }
}

function loadWelcomeMessage() {
    fetch('/static/welcome_msg.txt')
        .then(response => response.text())
        .then(data => {
            appendToTerminal(data);
        })
        .catch(error => {
            console.error('Error loading welcome message:', error);
        });
}

function sendUARTCommand(command) {
    if (!command) {
        appendToTerminal('[ERROR] Cannot send empty command via UART');
        return;
    }

    // Create command object
    const commandObj = {
        action: "send_command",
        command: command.trim()
    };

    // Convert to JSON string
    const commandJSON = JSON.stringify(commandObj);

    // Log the command being sent
    console.log("[DEBUG] Sending UART Command: " + commandJSON);

    if (isConnected && uartConnection) {
        uartConnection.send(commandJSON);
        appendToTerminal(`[UART SENT] ${commandJSON}`);
    } else {
        appendToTerminal('[ERROR] UART not connected');
    }

    // Additional debug log to track unexpected actions
    console.log("[DEBUG] Command sent, awaiting response...");
}

function updateConnectionStatus(connected) {
    isConnected = connected;
    const statusBadge = document.getElementById('connection-status');
    if (statusBadge) {
        statusBadge.textContent = connected ? 'Connected' : 'Disconnected';
        statusBadge.className = connected ? 'badge bg-success' : 'badge bg-danger';
    }
}

function processUARTData(data) {
    appendToTerminal(`[UART RECEIVED] ${data}`);
    // Add logic to process and display UART data
}

function enableInterface() {
    // Logic to enable UI elements
    const commandInput = document.getElementById('command-input');
    const sendButton = document.getElementById('send-button');
    if (commandInput && sendButton) {
        commandInput.disabled = false;
        sendButton.disabled = false;
    }
}

function handleCommandStatus(data) {
    // Logic to handle command status
    if (data.success) {
        appendToTerminal(`[INFO] ${data.message}`);
    } else {
        appendToTerminal(`[ERROR] ${data.message}`);
    }
}

function handleQuickActions() {
    const randomAttackBtn = document.getElementById('randomAttackBtn');
    const attackTimeInput = document.getElementById('attackTimeInput');
    const targetInput = document.getElementById('targetInput');
    const executeAttackBtn = document.getElementById('executeAttackBtn');

    if (randomAttackBtn) {
        randomAttackBtn.addEventListener('click', () => {
            sendUARTCommand('random_attack');
        });
    }

    if (executeAttackBtn) {
        executeAttackBtn.addEventListener('click', () => {
            const attackTime = attackTimeInput.value.trim();
            let command = '';

            if (attackTime) {
                command = `attack_time ${attackTime}`;
                sendUARTCommand(command);
                appendToTerminal(`> ${command}`);
            } else {
                appendToTerminal('No attack time specified');
            }
        });
    }
}

// Call the function to attach event listeners after DOM is fully loaded
document.addEventListener('DOMContentLoaded', function() {
    handleQuickActions();
});

// Ensure DOM is fully loaded before executing scripts
function initializeUI() {
    // Attach event listeners for quick actions
    handleQuickActions();

    // Initialize theme based on saved preference
    const savedTheme = localStorage.getItem('theme') || 'light';
    document.body.setAttribute('data-theme', savedTheme);

    // Attach event listener to theme toggle button
    const themeToggleBtn = document.querySelector('.theme-toggle');
    if (themeToggleBtn) {
        themeToggleBtn.addEventListener('click', toggleTheme);
    }
}

document.addEventListener('DOMContentLoaded', initializeUI);

// Function to handle setting a target
function setTargetAction() {
    const targetInput = document.getElementById('targetInput');
    const target = targetInput.value.trim();
    if (target) {
        sendUARTCommand(`set target ${target}`);
        appendToTerminal(`> Set target ${target}`);

        // Check if the target is 666 to trigger the modal
        if (target === '666') {
            openrbbtModal();
        }
    } else {
        appendToTerminal('No target specified');
    }
}

// Attach event listener to the Set Target button
document.addEventListener('DOMContentLoaded', function() {
    const setTargetBtn = document.getElementById('setTargetBtn');
    if (setTargetBtn) {
        setTargetBtn.addEventListener('click', setTargetAction);
    }
});

// Function to open the easter egg modal
function openrbbtModal() {
  var modal = document.getElementById('rbbtModal');
  modal.style.display = 'block';

  // Add event listener to close the modal
  var closeButton = document.getElementsByClassName('close')[0];
  closeButton.onclick = function() {
    modal.style.display = 'none';
  }
}
