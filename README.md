# J.A.R.V.I.S-Cyberdeck
An Esp32 Based Cyberdeck using 1.8 inch TFT and four buttons (And of course 30 pin ESP-32).


The "Atlantis V2" project is a Tier 3 custom-built, portable ESP32-based cyberdeck and smart device system, featuring a firmware environment dubbed "J.A.R.V.I.S. Core V.01" or "Retro-OS". Designed by pilot/operator Sloke Bhattacharyya, the project functions as a multi-module tactical terminal integrated with cellular and network diagnostics, arcade games, and productivity tools.

**Hardware Specifications**

* **Microcontroller:** The system is powered by an ESP32 Development Board, specifically a NodeMCU-32S or ESP32-WROOM-32.
* **Display:** It utilizes a 1.8-inch SPI TFT LCD Display driven by an ST7735 controller with a 128x160 pixel resolution.
* **Input Interface:** Navigation is handled by a physical matrix of four tactile push buttons mapped to Nav, Prev, Select, and Back actions.
* **Connectivity Hardware:** Cellular capabilities are provided by an A7670C 4G LTE Module using a UART2 interface.
* **Power Architecture:** The device runs on a single 18650 Lithium-ion battery cell paired with a TP4056/BMS protection board.

**User Interface and Display Design**

* **Aesthetic Paradigm:** The interface is modeled after a simplistic, monochromatic Linux terminal.
* **Menu System:** Navigation relies on a horizontal camera-roll carousel featuring an animated viewfinder box to highlight active selections.
* **Display Orientation:** The software enforces a 180-degree display rotation (`tft.setRotation(2)`) to accommodate the physical mounting orientation of the screen.
* **Theme Engine:** The UI supports custom color palettes, including "OBSIDIAN" (black with magenta highlights), "MATRIX" (black with emerald green), and "LAPIS" (obsidian base with cyan-dominant telemetry).

**Software Modules and Applications**

* **Boot Sequence:** The system features a multi-stage fake Linux boot sequence, complete with rootfs mounting, SPI initialization, a CRT beam raster expansion effect, and an encrypted "HELLO SLOKE" operator greeting.
* **E.D.I.T.H. Tactical Diagnostics:** A dedicated network module capable of WiFi access point scanning, subnet discovery (pinging local IPs to map active hosts), and RSSI signal mapping.
* **F.R.I.D.A.Y. Web Portal:** The ESP32 hosts a local Access Point and web server that allows users to remotely configure WiFi credentials, manage a tactical "To-Do" directive list, and upload text files (TXT, JSON, CSV) directly to the device's LittleFS flash storage.
* **Productivity Tools:** The firmware includes a built-in text reader for displaying uploaded files, a virtual on-screen keyboard for standalone text entry, and a clock application featuring NTP network time synchronization, a countdown timer, and a stopwatch.
* **Arcade Games Suite:** The device includes a fully persistent, high-score tracking game engine containing six titles:
* *Space Impact:* A vertical space shooter featuring progressive difficulty, drone and interceptor enemy classes, and a dynamic spawn rate.
* *Cyber Snake:* A classic grid-based snake game.
* *Cyber Bird:* A physics-based Flappy Bird clone.
* *Firewall Breach:* A Breakout-style block-busting game.
* *Neon Overdrive:* A three-lane vertical racing/dodging game.
* *Lunar Descent:* A physics-based lunar lander simulation requiring precise thrust and angle management.


[BOM.csv](https://github.com/user-attachments/files/33223327/BOM.csv)
<img width="743" height="516" alt="Screenshot 2026-10-09 011607" src="https://github.com/user-attachments/assets/42a64321-f154-4d5b-936c-ccb9d8106e96" />
<img width="381" height="591" alt="Screenshot 2026-10-09 012755" src="https://github.com/user-attachments/assets/b81cab99-58c6-40fa-9784-f0d72fdc4e2c" />
<img width="148" height="226" alt="Screenshot 2026-10-09 012021" src="https://github.com/user-attachments/assets/2bc8033a-6c56-4c24-ae17-03820cc8b1c0" />
[Atlantis V2.pdf](https://github.com/user-attachments/files/33223259/Atlantis.V2.pdf)
Item No.,Part Name,Quantity
1,ESP32 30-pin,1
2,4-pin Tactile Button,4
3,1.8 in Touchless TFT,1
