# Smart Dual-Source Irrigation Controller

A robust, distributed Arduino-based irrigation management system that uses **CAN Bus communication** to manage dual-source water supplies (Rainwater vs. Well water). This project demonstrates advanced embedded systems concepts, including real-time monitoring, hardware fail-safes, and inter-processor communication.

## 🚀 Technical Highlights
* **Distributed Architecture:** Utilizes a Master/Remote node setup over CAN Bus (500kbps) to separate sensor acquisition from control logic.
* **Fail-Safe Engineering:** Implements "Delta-P" monitoring (pressure change detection), prime failure detection, and overrun timers to prevent pump burnout.
* **Non-Blocking Logic:** Features a custom object-oriented `Timer` class to handle concurrent processes (filter flushing, rest periods, display cycling) without using `delay()`.
* **Priority-Based Resource Management:** Smartly prioritizes rainwater over well water based on cistern levels to optimize sustainable resource usage.

---

## 🏗 System Logic & Flow

The system operates as a state machine that balances water demand with hardware longevity.



### 1. Decision Engine (Pump Selection)
* **Demand:** Triggered when System Pressure drops to **20 PSI** or lower.
* **Rain Priority:** If Tank Level is above **50%** (and not in a rest period), the **Rain Pump** is activated.
* **Well Backup:** If rainwater is insufficient (below **15%**) or the rain pump fails to prime, the system switches to the **Well Pump**.
* **Target:** All pumps deactivate once pressure reaches **40 PSI**.

### 2. Safety Monitor (Continuous Protection)
While a pump is active, the system monitors three critical "kill switches":
* **Prime Protection:** Shuts down if pressure doesn't rise above 20 PSI within **80 seconds**.
* **Delta-P Monitoring:** Shuts down if pressure remains stagnant or drops for **100 seconds** (indicates pipe burst or pump failure).
* **Overrun Protection:** If the pump runs for longer than **30 minutes**, a mandatory **12-hour rest** period is enforced.

---

## 🛠 Hardware & Protocols
* **Microcontrollers:** Arduino
* **Communication:** CAN Bus (Controller Area Network) for high-reliability data exchange.
* **Sensors:** Ultrasonic Distance Sensor (Water Level), Analog Pressure Transducer.
* **Peripherals:** I2C 16x2 LCD Display, Dual-Channel Relay Modules.

---

## 📂 Project Structure

### 1. Control Master (`master_irrigation.ino`)
The primary controller that processes CAN data and executes the state machine.
* Manages a **Dynamic LCD UI**, cycling through system health metrics and active timers.
* Executes the **Filter Flush** maintenance cycle (5 seconds every 24 hours of pump activity).
* Handles error logging for prime and pressure failures.

### 2. Remote Sensor Node (`remote_node.ino`)
Handles raw hardware abstraction and sends data to the Master.
* **Noise Filtering:** Performs 20x analog oversampling for the pressure transducer to ensure stable readings.
* **Distance Calculation:** Uses the `NewPing` library for precise ultrasonic ranging in the water tank.
* **Remote Actuation:** Listens for CAN commands to trigger the Filter Relay.

### 3. Custom Timer Class
A key software feature that ensures the system remains responsive.
```cpp
// Example of non-blocking event-driven timer used in this project
overrunTimer.attachOnEnd(pumpOverran);
primeTimer.attachOnEnd(primeFailed);
deltaPTimer.attachOnEnd(lowDeltaP);
```
## 📡 CAN Bus Messaging Protocol

The system uses a lightweight CAN messaging structure optimized for low-bandwidth, real-time sensor reporting and remote actuation.

### 📨 Message Definitions

#### `0x02` — System Pressure Broadcast
- **Sender:** Remote Node  
- **Data Type:** Byte (0–100)  
- **Unit:** PSI  
- **Description:** Transmits the current system water pressure reading to the Master controller.

#### `0x03` — Rainwater Tank Level Broadcast
- **Sender:** Remote Node  
- **Data Type:** Byte (0–100)  
- **Unit:** Percentage (%)  
- **Description:** Sends the calculated cistern fill level to the Master for pump selection logic.

#### `0x07` — Filter Flush Command
- **Sender:** Master Node  
- **Data Type:** Boolean (0 / 1)  
- **Description:**  
  - `1` → Activate filter relay  
  - `0` → Deactivate filter relay  

---

### 📊 Compact Reference Table

| Message ID | Sender        | Data Type      | Purpose                         |
|------------|--------------|---------------|---------------------------------|
| `0x02`     | Remote Node  | Byte (0–100)  | System Water Pressure (PSI)     |
| `0x03`     | Remote Node  | Byte (0–100)  | Rainwater Tank Level (%)        |
| `0x07`     | Master Node  | Boolean (0/1) | Filter Flush Relay Command      |

---

## 🔧 Installation

### 📚 Required Libraries

- `CAN.h`
- `LiquidCrystal_PCF8574.h`
- `NewPing.h`

---

### 🔌 Wiring — Master Node

- **CAN Transceiver** → SPI interface  
- **LCD Display** → I2C  
- **Relay Outputs** → D4, D9  

---

### 🔌 Wiring — Remote Node

- **Pressure Sensor** → A7  
- **Ultrasonic Sensor** → D3 (Trig), D4 (Echo)  
- **Filter Relay** → D6  

---

## 👤 Author

**[Your Name]**

**Focus Areas:** Embedded Systems · Industrial Automation · IoT
