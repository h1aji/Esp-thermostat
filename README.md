# Esp-thermostat
**WiFi thermostat based on ESP8266 and DS18B20 temperature sensor.**

The project comes from this [article](https://iotprojectsideas.com/temperature-control-with-esp8266-asyncwebserver/)
and adjusted for the ESP12-F board with embedded relay from Aliexpress.

![Board](./doc/esp12f-relay-x1.jpg)


### Installation
 - Connect the DS18B20 Dallas temperature sensor to the ESP12-F GPIO2.
 - Add a 4.7k ohm resistor between GPIO2 and the 3.3V pin.
 - VDD and GND pins are connected to the 3.3V supply and the ground of the ESP12-F module.

![Pinout](./doc/ds18b20-pinout.png)

### Wiring

|  ESP-12F  |  RELAY  |  DS18B20  |
|-----------|---------|-----------|
|  GPIO04   |         |           |
|  GPIO05   |         |           |
|  GPIO00   |         |           |
|  GPIO02   |         |   DATA    |
|  GPIO14   |         |           |
|  GPIO12   |         |           |
|  GPIO13   |         |           |
|  GPIO16   |         |           |
|  GPIO15   |   PIN   |           |

### Firmware setup

**0. Install PlatformIO (Linux)**

This project is built with [PlatformIO](https://platformio.org/), not the Arduino IDE.

1. Install Python 3.6+ and pip, if not already present (e.g. on Debian/Ubuntu: `sudo apt install python3 python3-pip`).
2. Install PlatformIO Core: `pip install -U platformio`
3. If the `pio` command isn't found afterwards, add pip's user-install bin directory to your `PATH` (commonly `~/.local/bin`).
4. Grant serial port access without `sudo` by adding your user to the `dialout` group:
   ```
   sudo usermod -aG dialout $USER
   ```
   Log out and back in (or reboot) for the group change to take effect.
5. Verify the install: `pio --version`

With that in place, from the project root:
```
pio run              # build
pio run -t upload    # flash over USB
```

(Alternatively, install the official [PlatformIO IDE extension](https://platformio.org/platformio-ide) for VS Code instead of the bare CLI — it wraps the same `pio` tool in a GUI.)

**1. First flash (over USB)**

1. Connect a USB-UART dongle to the ESP8266/relay board.
2. Short GPIO0 to GND, then press the device's reset button to enter flashing mode.
3. Start the upload, keeping GPIO0 shorted to GND until it finishes.
4. Once the upload succeeds, release the GPIO0 short and reset the device.

**2. WiFi setup**

1. On first boot (or whenever it can't connect to a configured network), the device starts its own open WiFi access point, **Esp-thermostat**.
2. Connect to that access point and open `192.168.4.1` in a browser — it redirects to the Settings page.
3. Enter your home WiFi SSID and password and save. The device restarts and joins your network.
4. Once connected, the dashboard is reachable at `http://thermostat.local` or the device's new IP address.
   Default login: `esp` / `thermostat`.

**3. Subsequent updates (OTA)**

Once the device is on your network, firmware can be updated wirelessly instead of over USB: uncomment `upload_protocol = espota` in `platformio.ini` and set `upload_port` to the device's IP address or `thermostat.local`.

### Roadmap

 - Add MQTT support
 - Setup Github CI/CD
