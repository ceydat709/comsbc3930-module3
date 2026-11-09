# Freeze Frame: Collaborative Button Mash Game

**Freeze Frame** is a two-player collaborative button-mashing game built using LilyGo T-Display ESP32 devices. Rather than competing against each other, both players work toward a shared goal, contributing to the same progress bar before time runs out.

The game introduces unpredictability through a random freeze mechanism. Only one player can press buttons at a time, while the other is temporarily frozen. Every 2-5 seconds, the roles switch automatically. Each player's contributions appear in a distinct color, while visual effects such as a color-changing bar outline, screen dimming, and sparks reinforce the collaborative experience.

Both devices communicate wirelessly through ESP-NOW, synchronizing button presses, freeze states, and game progress.

## Project Demo

VIDEO COMES HERE

## Recreation

### What You Need

- 2 x LilyGo T-Display ESP32 devices
- 2 x USB-C cables
- Computer with the [Arduino IDE](https://www.arduino.cc/en/software)
- [`TFT_eSPI`](https://github.com/Bodmer/TFT_eSPI) library by Bodmer
- ESP32 board support, including `WiFi` and `esp_now`

### Setup

1. Download and install the [Arduino IDE](https://www.arduino.cc/en/software).
2. Add ESP32 support by following the [Arduino-ESP32 installation guide](https://docs.espressif.com/projects/arduino-esp32/en/latest/installing.html).
3. Install the `TFT_eSPI` library:
   - Open **Tools > Manage Libraries** in the Arduino IDE.
   - Search for `TFT_eSPI`.
   - Install `TFT_eSPI` by Bodmer.
4. Configure `TFT_eSPI` for the LilyGo T-Display:
   - Locate `User_Setup_Select.h` in the `TFT_eSPI` library folder.
   - Ensure the appropriate LilyGo T-Display setup (`Setup25_TTGO_T_Display.h`) is enabled.
5. Connect the LilyGo T-Display ESP32 devices to your computer using USB-C cables.
6. Select the appropriate ESP32 board and serial port under **Tools**.

### Running the Project

1. Clone or download this repository.
2. Open the project's `module3.ino` sketch in the Arduino IDE.
3. Configure the player identifiers for each device:
   - **Device 1:** `#define PLAYER_ID 0`
   - **Device 2:** `#define PLAYER_ID 1`
   - Both devices must use the same `TEAM_ID`.
   - Keep `MAX_PLAYERS` set to `2`.
5. Verify and upload the sketch to each ESP32 device separately, changing `PLAYER_ID` before uploading to the second device.
6. Power on both devices and keep them within wireless communication range.
7. Once the devices connect, the game begins automatically.
8. Press either button during your active turn to contribute to the shared progress bar. Button presses are ignored while frozen.
9. Reach the shared target before time runs out to win. After the round ends, press either button to initiate a rematch.

### Gameplay

Each round randomly selects one of three difficulty configurations:

| Difficulty | Target | Time Limit |
|---|---|---|
| Easier | 65 presses | 30 seconds |
| Medium | 70 presses | 27 seconds |
| Harder | 75 presses | 25 seconds |

At the beginning of each round, one player is randomly selected to be frozen. The active player can contribute button presses toward the shared target, while the frozen player must wait.

## Project Blogs
For more about the artistic vision, design decisions, technical implementation, and development process behind **Freeze Frame**, visit our project blogs:

- [Ho Jin's Project Blog](https://app.notion.com/p/Module-3-Distributed-Communication-ESP-Now-3dc28368cff9805f8047dc67484713cc)
- [Ceyda's Project Blog](https://ceydat709.github.io/coms3930-design-documentation/modules/module-3.html)
