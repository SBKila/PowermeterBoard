# PowermeterBoard

PowermeterBoard is an ESP8266-based device designed to read energy pulses from DDS238 power meters via GPIO and report cumulative and instant power data directly to Home Assistant using MQTT and the custom HALib library. 
It features a local web configuration interface and extremely resilient WiFi/MQTT connectivity.

## WiFi Manager Behavior

The WiFi Manager implements a robust state machine to handle network connectivity:

1. **Boot Sequence**:
   - If valid WiFi settings exist, the device starts in Station (STA) mode.
   - If no settings exist, it starts in Access Point (AP) mode for configuration.

2. **Connection Monitoring**:
   - The device continuously monitors the WiFi status.
   - **Zombie Detection**: Every 2 minutes, if connected, it attempts to open a TCP connection to the gateway (port 80). If this fails twice, the connection is marked as "Zombie" (connected but no internet/network access), and a reconnection cycle is forced.

3. **Recovery Strategy (Connection Lost)**:
   - When connection is lost, a recovery timer starts.
   - **Retry Interval**: The device attempts to reconnect to the saved SSID every 20 seconds.
   - **Phase 1 (First 15 minutes)**: The device forces **STA Only** mode. The Access Point is disabled to prioritize resources for reconnection.
   - **Phase 2 (After 15 minutes)**: If still disconnected, the device switches to **AP + STA** mode. This enables the internal Access Point (allowing user reconfiguration) while continuing to attempt reconnection in the background.

4. **Restoration**:
   - As soon as the connection is successfully restored (and verified), the device automatically switches back to **STA Only** mode, disabling the Access Point.

## LED Status (Blinker)

The onboard LED provides visual feedback on the device state using specific blink patterns:

- **Heartbeat (1 flash of 10ms every 30s)**:
  - System is fully operational.
  - WiFi is connected.
  - MQTT is connected.

- **1 Blink**:
  - WiFi is connected.
  - MQTT is **disconnected**.

- **2 Blinks**:
  - Device is in **Access Point (AP)** mode (or waiting for config).

- **3 Blinks**:
  - WiFi Station (STA) is **disconnected**.
  - Device is currently trying to connect to the WiFi network.

- **5 Blinks**:
  - **Zombie State** detected.
  - WiFi appears connected, but the gateway is unreachable.

## Web Interface & Data Dictionary (WebSocket SPA)

The web UI uses a Single Page Application (SPA) architecture communicating via WebSockets. It sends `req_vars` JSON requests with an array of keys to dynamically populate its DOM elements. 

The following variables are available throughout the system and resolved by the combined `stringProcessor` layers:

### System & Debug variables
* `RELEASE`: Firmware build version
* `EEPROM`: Total EEPROM space allocated
* `FS_STARTED`: Is filesystem started (YES/NO)
* `FS_USED`: Filesystem bytes used
* `FS_TOTAL`: Filesystem total capacity
* `BLINKER_STATE`: Current textual state of the LED blinker

### Wifi variables
* `SSIDNAME`: The currently configured WiFi SSID
* `WIFICONNECTIONSTATUS`: Textual connection state ("Connected" or "Not connected")
* `WIFINETWORKIP`: Current explicit IP Address or "Not connected"
* `WIFIRSSI`: WiFi signal strength in dBm

### MQTT & Powermeter variables
* `NODENAME`: Name of the device
* `MQTTDOMAIN`: MQTT Broker Hostname or IP
* `MQTTPORT`: MQTT Broker Port
* `MQTTLOGIN`: MQTT User
* `MQTTPWD`: MQTT Password
* `MQTTCONNECTIONSTATUS`: "Connected" or "Disconnected"
