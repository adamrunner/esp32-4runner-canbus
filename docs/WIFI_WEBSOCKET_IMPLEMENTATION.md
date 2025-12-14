# WiFi WebSocket CAN Data Streaming Implementation

This document describes the WiFi access point and WebSocket streaming functionality added to the ESP32 4Runner CAN Bus project.

## Overview

The ESP32 broadcasts a WiFi network and serves a web interface that streams CAN data via WebSocket. Users can switch between data modes (TPMS-focused or Debug/all CAN frames) at runtime without reflashing firmware.

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                         ESP32                                   │
├─────────────────────────────────────────────────────────────────┤
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐      │
│  │ CAN Receive  │───▶│  Data Router │───▶│  WebSocket   │      │
│  │    Task      │    │   (Queue)    │    │   Broadcast  │      │
│  └──────────────┘    └──────────────┘    └──────────────┘      │
│                             │                    │              │
│                             ▼                    ▼              │
│                      ┌──────────────┐    ┌──────────────┐      │
│                      │ Serial Log   │    │  HTTP Server │      │
│                      │  (disabled   │    │  (Web UI)    │      │
│                      │  when WS     │    └──────────────┘      │
│                      │  connected)  │                          │
│                      └──────────────┘                          │
│                                                                 │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │                    WiFi AP Mode                          │   │
│  │               SSID: "ESP32-CAN" (configurable)           │   │
│  │               IP: 192.168.4.1                            │   │
│  └─────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
```

## Configuration

### Default Settings

| Setting | Default Value | Kconfig Option |
|---------|---------------|----------------|
| WiFi SSID | `ESP32-CAN` | `CONFIG_WIFI_AP_SSID` |
| WiFi Password | `esp32canbuswifi` | `CONFIG_WIFI_AP_PASSWORD` |
| WiFi Channel | 1 | `CONFIG_WIFI_AP_CHANNEL` |
| Max Connections | 2 | `CONFIG_WIFI_AP_MAX_CONNECTIONS` |
| mDNS Hostname | `esp32-can` | `CONFIG_MDNS_HOSTNAME` |

### Changing Configuration

Use `idf.py menuconfig` and navigate to "WiFi AP Configuration" to change these settings.

## File Structure

### Source Files

```
main/
├── 4runner_canbus_main.c   # Main application (modified)
├── wifi_ap.c               # WiFi AP initialization
├── wifi_ap.h
├── web_server.c            # HTTP server + WebSocket handler
├── web_server.h
├── can_data_router.c       # Mode management, JSON formatting
├── can_data_router.h
├── CMakeLists.txt          # Build config with dependencies
├── Kconfig.projbuild       # Configuration options
└── idf_component.yml       # Component manager (mdns dependency)
```

### Web UI Files (SPIFFS)

```
spiffs/
├── index.html              # Main page structure
├── style.css               # Dark theme styling
└── app.js                  # WebSocket client, UI logic
```

### Build Configuration

```
partitions.csv              # Custom partition table with SPIFFS
CMakeLists.txt              # SPIFFS image generation
sdkconfig.defaults          # Partition table + WebSocket settings
```

## Data Modes

### TPMS Mode (Default)

Only sends decoded tire pressure data at 1Hz intervals:

```json
{
  "type": "tpms",
  "fl": 32.7,
  "fr": 32.5,
  "rl": 33.0,
  "rr": 32.8,
  "ts": 123456789
}
```

### Debug Mode

Sends all raw CAN frames as they are received:

```json
{
  "type": "can_frame",
  "id": "0x0AA",
  "dlc": 8,
  "data": "1A 6F 1A 6F 1A 6F 1A 6F"
}
```

## WebSocket Protocol

### Endpoint

`ws://<ip>/ws` or `ws://esp32-can.local/ws`

### Server → Client Messages

**CAN Frame (Debug mode):**
```json
{"type":"can_frame","id":"0x0AA","dlc":8,"data":"01 02 03 04 05 06 07 08"}
```

**TPMS Data (TPMS mode):**
```json
{"type":"tpms","fl":32.7,"fr":32.5,"rl":33.0,"rr":32.8,"ts":123456789}
```

**Status Update:**
```json
{"type":"status","mode":"tpms","connected":true}
```

### Client → Server Messages

**Change Mode:**
```json
{"cmd":"set_mode","mode":"debug"}
```

```json
{"cmd":"set_mode","mode":"tpms"}
```

## Web UI Features

- **Mode Selector**: Dropdown to switch between TPMS and Debug modes
- **TPMS Panel**: Visual display of tire pressures with low-pressure warnings
- **Console**: Scrolling log of received CAN data
- **Pause/Resume**: Temporarily stop updating the console
- **Clear**: Clear the console contents
- **Auto-scroll**: Toggle automatic scrolling to latest data
- **Connection Status**: Shows connected/disconnected state

## Memory Usage

| Component | Estimated RAM |
|-----------|---------------|
| WiFi AP | ~40 KB |
| HTTP Server | ~4 KB |
| WebSocket buffer | ~2 KB |
| CAN message queue | ~2 KB |
| **Total Additional** | ~48 KB |

## Troubleshooting

### Cannot Connect to WiFi

1. Verify SSID and password match configuration
2. Check that ESP32 has powered up completely (wait 5-10 seconds)
3. Try forgetting the network on your device and reconnecting
4. Check serial monitor for WiFi initialization errors

### Web Page Not Loading

1. Verify you're connected to the ESP32's WiFi network
2. Try IP address directly: `http://192.168.4.1`
3. If using mDNS (`esp32-can.local`), ensure your device supports mDNS
4. Check serial monitor for SPIFFS mount errors

### WebSocket Not Connecting

1. Check browser console for WebSocket errors
2. Verify the ESP32 is receiving CAN data (check serial output)
3. Try refreshing the page
4. Check that only one client is connected (single client limit)

### No CAN Data Displayed

1. Verify CAN bus connections (TX/RX GPIO pins)
2. Check that vehicle ignition is on
3. In Debug mode, you should see frames immediately
4. In TPMS mode, data updates every 1 second

### Serial Output Not Showing

Serial CAN logging is intentionally disabled when a WebSocket client is connected. Disconnect from the web interface to see serial output again.

## Building and Flashing

```bash
# Set up ESP-IDF environment
. /path/to/esp-idf/export.sh

# Build
idf.py build

# Flash (includes SPIFFS image)
idf.py flash

# Monitor serial output
idf.py monitor

# Combined
idf.py build flash monitor
```

## Updating Web UI Only

The web UI files are stored in SPIFFS and can be updated independently:

```bash
# Rebuild and flash only the SPIFFS partition
idf.py build
idf.py -p PORT write_flash 0x110000 build/storage.bin
```

Note: The SPIFFS offset (0x110000) comes from the partition table. Verify with:
```bash
idf.py partition-table
```

## Dependencies

- ESP-IDF v5.5+
- espressif/mdns component (managed via idf_component.yml)

## Future Enhancements

Planned features for future development:

1. **Additional Data Modes**: Custom CAN ID filters, wheel speed, steering angle
2. **Data Logging**: Record CAN data to SD card or download from web UI
3. **OTA Updates**: Update firmware over WiFi
4. **Configuration UI**: Change WiFi settings from web interface
5. **Multiple Clients**: Support more than one simultaneous WebSocket connection
6. **Alerts**: Configurable thresholds for TPMS warnings
