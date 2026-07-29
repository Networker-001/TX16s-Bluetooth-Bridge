# 🛰️ Bluetooth Bridge for RadioMaster TX16S
### (Optimized for Teleview Project & Telemetrie Viewer APP)

Dieses ESP32-Projekt realisiert ein Telemetrie-Gateway namens **Bluetooth Bridge** 
speziell für das **Teleview-Projekt** und die **Telemetrie Viewer APP**. Es dient als 
Schnittstelle für die **RadioMaster TX16S** (EdgeTX/OpenTX).

### 🚨 Technischer Hintergrund:
Einfache "Serial-to-Serial" Bluetooth-Module können die Telemetriedaten des
Multiprotokollmoduls (MPM) der TX16S zwar übertragen, diese haben jedoch keinen
Nutzen beim Empfänger. Da das MPM andere Header-Daten und zusätzliche,
überlange Status-Frames als ein reiner FrSky-Sender liefert, kann die App oder
das externe Display die Rohdaten nicht interpretieren. Es werden keine nutzbaren
Daten angezeigt.

### 💡 Funktionsweise dieses Projekts:
Die Firmware filtert und verarbeitet die reinkommenden Telemetriedaten in
Echtzeit direkt im RAM des ESP32. Sie isoliert die überlangen MPM-Statuspakete
und reicht die Daten strukturiert an die Bluetooth-Schnittstelle weiter. Dies
ermöglicht den fehlerfreien Betrieb des **Teleview-Moduls** (basierend auf dem
Lilygo T-Display-S3 Plus) und der **Telemetrie Viewer APP** für Mobiltelefone per Bluetooth.

---

## 🚀 Features

* 📺 **Teleview-Support:** Volle Kompatibilität mit dem externen
  **Teleview-Modul** und der **Telemetrie Viewer APP** zur Darstellung von
  Horizont- und Flugdaten.
* 📻 **Dual-Bluetooth-Verbindung:** Unterstützt **BLE** und **Classic BT
  (Serial)** zur Anbindung unterschiedlicher Endgeräte.
* 🎛️ **Live-LUA-Tool:** Konfiguration direkt über das beiliegende
  EdgeTX-LUA-Skript am Sende-Display (Modus, Name, PIN-Vergabe, Debug-Switch).

---

## 📊 Unterstützte Telemetrie-Protokolle

Das Modul arbeitet als Echtzeit-Übersetzer im Hintergrund:

1. **Natives FrSky / Universal-Telemetrie (1:1 Spiegelung):** Jeder reguläre
   Standard-SmartPort-Telemetriewert wird unverändert und fehlerfrei mit
   dem erforderlichen Startzeichen (`0x7E`) gefunkt (gilt für alle iNAV-
   und Standard-Sensoren).
2. **ArduPilot (Yaapu-Konvertierung):** Hochkomprimierte Passthrough-Daten
   (Yaapu) werden im RAM automatisch in das iNAV-Standardformat übersetzt,
   damit das Teleview-Display Horizont und Akku korrekt darstellt.
3. **Erweiterbar via LUA-Skripte:** Über Zusatzskripte (z. B. das beiliegende
   HoTT-Mirror V17) können auch andere Protokolle wie Graupner HoTT über den
   ESP32 an die Telemetrie Viewer APP übergeben werden.

---

## 💻 Hardware-Komponente (ESP32)

Für dieses Projekt wird folgendes kompaktes Mikrocontroller-Board verwendet:
* **Typ:** AZ-Delivery D1 Mini ESP32 (MINI32 V1.0.0)
* **Format:** Kompaktes D1-Mini-Layout (optimiert für engen Bauraum)
* **Chip:** ESP-WROOM-32 (Dual-Core, integriertes Bluetooth/WLAN)

---

## 🛠️ Software-Installation (Flashen mit PlatformIO)

Die Firmware wird über die Entwicklungsumgebung PlatformIO auf das Modul
übertragen. Gehen Sie dazu wie folgt vor:

1. Laden Sie **Visual Studio Code (VS Code)** herunter und installieren Sie es.
2. Öffnen Sie in VS Code den Erweiterungs-Manager (Extensions) und installieren
   Sie das Plugin **PlatformIO IDE**.
3. Klonen Sie dieses GitHub-Repository oder laden Sie es als ZIP-Archiv
   herunter und entpacken Sie es.
4. Wählen Sie in PlatformIO **"Open Project"** und öffnen Sie den entpackten
   Projektordner.
5. Verbinden Sie den ESP32 per USB-Kabel mit Ihrem Computer.
6. Klicken Sie in der blauen PlatformIO-Statusleiste (ganz unten links) auf
   das Haken-Symbol (**Build**), um den Code fehlerfrei zu kompilieren.
7. Klicken Sie direkt daneben auf das Pfeil-Symbol nach rechts (**Upload**),
   um die Software unzensiert auf das ESP32-Modul zu flashen.

---

---

## 📱 Die LUA-Skripte zur Konfiguration und Protokollerweiterung

Das Projekt nutzt zwei unterschiedliche LUA-Skripte auf der Fernsteuerung,
je nachdem, ob das System konfiguriert oder ein Fremdprotokoll geflogen wird.

### 1. Das Einstell-Werkzeug: `Bluetooth_Bridge.lua`
Dieses Tool dient dazu, die internen Grundeinstellungen der Box per Funk
direkt an der Fernsteuerung anzupassen.

* **Verzeichnis auf der SD-Karte:** Kopieren Sie die Datei in den Ordner:
  ➔ **`/SCRIPTS/TOOLS/`**
* **Bedienung am Sender:** Starten Sie das Skript an der TX16S über das
  EdgeTX-Systemmenü (SYS-Taste) im Reiter **"Tools"**. Nach dem Einstellen von
  Modus, Name oder PIN und dem Klick auf "Speichern" startet der ESP32 neu.

### 2. Die HoTT-Protokollerweiterung: `hottx.lua`
Dieses Hintergrund-Skript (V17 S.Port Mirror) wird zwingend benötigt, wenn ein
Modell mit Graupner HoTT-Empfänger geflogen wird, um die fahrzeugseitigen
Telemetriedaten korrekt an die Bluetooth Bridge zu übergeben.

* **Verzeichnis auf der SD-Karte:** Kopieren Sie die Datei in den Ordner:
  ➔ **`/SCRIPTS/FUNCTIONS/`**
* **Aktivierung über die Sonderfunktionen (Special Functions):**
  * Gehen Sie im EdgeTX-Modellmenü auf den Reiter **"Sonderfunktionen"**.
  * Weisen Sie einem physischen Schalter Ihrer Wahl (z. B. `SF` oder `SG`)
    die Aktion **"Skript"** (Script) zu.
  * Wählen Sie im Auswahlfeld das Skript **`hottx`** aus.
  * Setzen Sie ganz hinten den Haken bei **"Aktiv"** (Enable).
* **Funktionsweise im Flug:** Das Skript läuft nur dann im Hintergrund, wenn
  der zugewiesene Schalter auf **AN** steht. Es sammelt die HoTT-Werte im
  Sender-RAM, verpackt sie in das kompakte 10-Byte-Format und schiebt sie über
  den UART2 an die Box, welche die Daten unzensiert an die Telemetrie Viewer APP streamt.


### Bedienung am Sender und Systemeinstellungen:

1. **Wichtige Hardware-Vorgabe im EdgeTX-Systemmenü:**
   Bevor das Skript oder das Modul genutzt werden kann, müssen die internen
   Hardware-Schnittstellen der TX16S korrekt konfiguriert sein:
   * Gehen Sie über die **SYS-Taste** in die **Hardware-Einstellungen**.
   * Suchen Sie den internen Anschluss (z. B. **UART2 **).
   * Schalten Sie die **Spannungsversorgung (Power ON)** für diesen Port ein.
   * **Für das LUA-Einstelltool:** Stellen Sie den Port temporär auf **LUA**.
   * **Für den reinen Flugbetrieb:** Stellen Sie den Port fest auf **Telemetrie**
     (FrSky S.Port). Ausgenommen hiervon sind Fremdprotokolle wie Graupner HoTT,
     die ihre Daten direkt über das Zusatzskript einspeisen.

2. **Starten des Konfigurations-Tools:**
   * Drücken Sie lange auf die **SYS-Taste**, um in das Menü zu gelangen.
   * Wählen Sie über die Page-Tasten den Reiter **"Tools"** (Werkzeuge) aus.
   * Starten Sie das Skript **"Bluetooth_Bridge"** über das Scrollrad.
   * Hier können Sie nun den Bluetooth-Modus (BLE/Classic) wechseln, den
     Sendernamen ändern, die 4-stellige PIN eintippen oder das serielle
     Logging (`debug_telemetry`) ein- und ausschalten.
   * Nach dem Druck auf **"Speichern & Senden"** wird der Befehl per Telemetrie
     an den ESP32 geschickt. Dieser speichert die Werte im Flash und startet
     automatisch neu.

3. **Zurückschalten für den Flug:**
   * Nach erfolgreicher PIN- oder Namensänderung muss der serielle Port im
     Hardware-Menü der TX16S wieder von *LUA* zurück auf *Telemetrie* gestellt
     werden, damit der reguläre Telemetrie-Datenstrom zum ESP32 fließt.

---

## 🔧 Einbau & Hardware-Anschluss

Das Modul nutzt den PIN 17 des ESP32. Die Verbindung zur
Fernsteuerung wird über ein trennbares Kabel mit einer 4-Pin XH-Buchse realisiert.

* **Die Modul-Verkabelung:** 
  Auf der rechten Seite des MINI32-Boards wird eine 4-polige XH-Buchse aufgelötet.
  Diese führt die Pins der seriellen Schnittstelle 2 und die Spannungsversorgung:
  ➔ **IO16 (RX2)**, **IO17 (TX2)**, **GND** und **VCC (5V)**.
* **Der Anschluss in der Fernsteuerung:**
  Das vieradrige Telemetriekabel ist auf der Hauptplatine intern am UART 2
  verlötet. Es wird unsichtbar durch das Gehäuse nach hinten in den externen
  Modulschacht (JR-Schacht) der RadioMaster TX16S geführt.
* **Die Einbau-Optionen:**
  * **Variante A (Unterbringung im JR-Schacht):** 
    Durch den extrem kompakten D1-Mini-Formfaktor kann das Modul flach im leeren
    externen Modulschacht platziert werden. Über den dort liegenden XH-Stecker
    bleibt es im Handumdrehen trennbar und wird durch den Schachtdeckel geschützt.
    *(Hier Foto von Variante A einfügen)*
  * **Variante B (Interner Einbau):** 
    Alternativer, dauerhafter Einbau direkt im Gehäuse der TX16S rechts unten.

---

## 🔌 Der Anschluss-Steckbrief

Exakte PIN-Belegung des Telemetriemoduls:

PIN-Belegung des 4-Pin XH-Telemetriesteckers (Rechte Reihe auf der Platine):
* IO16 (RX2)  -> Verbunden mit dem TX-Pin der TX16S-Hauptplatine
* IO17 (TX2)  -> Verbunden mit dem RX-Pin der TX16S-Hauptplatine
* VCC (5V)    -> Verbunden mit dem 5V/+-Pin der TX16S-Hauptplatine
* GND (0V)    -> Verbunden mit dem GND/--Pin der TX16S-Hauptplatine

Zusätzliche Peripherie-Pins am MINI32 (Linke Reihe auf der Platine):
* GPIO 25     -> Physischer Modus-Schalter (BLE / Classic BT)
* GPIO 26     -> Physischer Reset-Taster (Kopplungen manuell löschen)
* GPIO 2      -> Status-LED (Leuchtet bei aktiver Verbindung)

---



## 📜 Lizenz

**MIT License** - Frei für alle Freunde der TX16s!
Der Code kann modifiziert, angepasst und für eigene Teleview-Systeme genutzt werden.


