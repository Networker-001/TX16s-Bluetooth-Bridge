# 🛰️ Technische Details & Installation: Bluetooth Bridge

Diese Dokumentation enthält die vollständigen technischen Spezifikationen, die
Anschlussbelegungen und die Schritt-für-Schritt-Installationsanleitung für das
**Bluetooth Bridge** Projekt.

---

## 📊 Unterstützte Telemetrie-Protokolle

Das Modul arbeitet als Echtzeit-Übersetzer im Hintergrund:

1. **Natives FrSky / Universal-Telemetrie (1:1 Spiegelung):** Jeder reguläre
   Standard-SmartPort-Telemetriewert wird unverändert und fehlerfrei mit dem
   erforderlichen Startzeichen (`0x7E`) gefunkt (gilt für alle iNAV- und
   Standard-Sensoren).
2. **ArduPilot (Yaapu-Konvertierung):** Hochkomprimierte Passthrough-Daten
   (Yaapu) werden im RAM automatisch in das iNAV-Standardformat übersetzt, damit
   das Teleview-Display Horizont und Akku korrekt darstellt.
3. **Erweiterbar via LUA-Skripte:** Über Zusatzskripte können auch andere
   Protokolle übertragen werden.

### Die HoTT-Protokollerweiterung via `hottx.lua`:
Dieses Hintergrund-Skript (V17 S.Port Mirror) wird benötigt, wenn ein Modell mit
Graupner HoTT-Empfänger geflogen wird, um die fahrzeugseitigen Telemetriedaten
korrekt an die Bluetooth Bridge zu übergeben.

* **Verzeichnis auf der SD-Karte:** Kopieren Sie die Datei in den Ordner:
  ➔ **`/SCRIPTS/FUNCTIONS/`**
* **Aktivierung über die Sonderfunktionen (Special Functions):**
  * Gehen Sie im EdgeTX-Modellmenü auf den Reiter **"Sonderfunktionen"**.
  * Weisen Sie einem physischen Schalter Ihrer Wahl die Aktion **"Skript"** zu.
  * Wählen Sie im Auswahlfeld das Skript **`hottx`** aus und aktivieren Sie es.
* **Funktionsweise:** Das Skript läuft im Hintergrund, wenn der Schalter auf AN
  steht. Es sammelt die HoTT-Werte im Sender-RAM, verpackt sie in das kompakte
  10-Byte-Format und schiebt sie über den UART an die Box, welche die Daten
  unzensiert an die Teleview APP streamt.

---

## 💻 Hardware-Komponente & Steckbrief

* **Typ:** AZ-Delivery D1 Mini ESP32 (MINI32 V1.0.0)
* **Format:** Kompaktes D1-Mini-Layout
* **Chip:** ESP-WROOM-32 (Dual-Core, integriertes Bluetooth/WLAN)

### Exakte PIN-Belegung des 4-Pin XH-Telemetriesteckers:
*(Rechte Reihe auf der Platine)*
* **IO16 (RX2)** ➔ Verbunden mit dem TX-Pin der Hauptplatine der Fernsteuerung
* **IO17 (TX2)** ➔ Verbunden mit dem RX-Pin der Hauptplatine der Fernsteuerung
* **VCC (5V)**   ➔ Verbunden mit dem 5V/+-Pin der Hauptplatine der Fernsteuerung
* **GND (0V)**   ➔ Verbunden mit dem GND/--Pin der Hauptplatine der Fernsteuerung

### Zusätzliche Peripherie-Pins am MINI32:
*(Linke Reihe auf der Platine)*
* **GPIO 25** ➔ Physischer Modus-Schalter (BLE / Classic BT)
* **GPIO 26** ➔ Physischer Reset-Taster (Kopplungen manuell löschen)
* **GPIO 2**  ➔ Status-LED (Leuchtet bei aktiver Verbindung)

---

## 🛠️ Software-Installation (Flashen mit PlatformIO)

Die Firmware wird über die Entwicklungsumgebung PlatformIO auf das Modul
übertragen. Gehen Sie dazu wie folgt vor:

1. Laden Sie **Visual Studio Code (VS Code)** herunter und installieren Sie es.
2. Öffnen Sie in VS Code den Erweiterungs-Manager (Extensions) und installieren
   Sie das Plugin **PlatformIO IDE**.
3. Öffnen Sie in PlatformIO **"Open Project"** und wählen Sie den entpackten
   Projektordner dieses Repositories aus.
4. Verbinden Sie den ESP32 per USB-Kabel mit Ihrem Computer.
5. Klicken Sie in der blauen PlatformIO-Statusleiste (ganz unten links) auf das
   Haken-Symbol (**Build**), um den Code zu kompilieren.
6. Klicken Sie direkt daneben auf das Pfeil-Symbol nach rechts (**Upload**), um
   die Software auf das ESP32-Modul zu flashen.

---

## ⚙️ Wichtige Systemeinstellungen im EdgeTX-Menü

Damit das Modul im reinen Flugbetrieb Daten empfangen kann und das LUA-Tool zur
Einstellung funktioniert, müssen die Ports im Sender umgeschaltet werden.

1. **Schnittstelle vorbereiten:**
   * Gehen Sie über die **SYS-Taste** in die **Hardware-Einstellungen**.
   * Suchen Sie den genutzten internen Anschluss (z. B. **UART2 / Bluetooth**).
   * Schalten Sie die **Spannungsversorgung (Power ON)** für diesen Port ein.
2. **Für das LUA-Einstelltool:**
   * Stellen Sie den Port im Hardware-Menü temporär auf **LUA**.
   * Führen Sie die Änderungen im Tool `Bluetooth_Bridge.lua` wie beschrieben durch.
3. **Für den reinen Flugbetrieb:**
   * Nach dem Einstellen muss der serielle Port im Hardware-Menü des Senders
     wieder von **LUA** zurück auf **Telemetrie** (FrSky S.Port) gestellt werden.
   * Erst jetzt fließt der reguläre Telemetrie-Datenstrom zum ESP32.
   * *Ausnahme:* Fremdprotokolle wie Graupner HoTT nutzen das `hottx.lua` Skript,
     welches die Daten direkt einspeist.
