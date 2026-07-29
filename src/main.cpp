#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <BluetoothSerial.h>
#include <esp_task_wdt.h>
#include <Preferences.h> // Flash-Speicher-Bibliothek

Preferences preferences;  // Speicher-Objekt erstellen

static bool debug_telemetry = false; // Für alle Klassen und Funktionen sichtbar

// --- PINS  ---
#define MODE_SWITCH_PIN 25    
#define TRIGGER_SWITCH_PIN 26 
#define RX_PIN 16             // Eingang 
#define TX_PIN 17             // Unbenutzt
#define STATUS_LED 2 



// ========================================================



// Statische Datenpuffer für den Protokoll-Wächter
static uint8_t packetBuffer[120]; 
static uint8_t convertedBuffer[10];
static int pIdx = 0;
static int logicalCount = 0;
static bool isEscaped = false;
static uint8_t mode = 0;           // 0: Suche, 1: MPM, 2: S.Port
static uint8_t lastByte = 0;
static unsigned long last999Time = 0; 
static bool lsbMatched = false; 
static int expectedPayloadLength = 10; 
static bool paketFertig = false;       

// ========================================================


#include <math.h> // Erforderlich fuer Trigonometrie (sin, cos, asin, atan2)

// Globaler RAM-Speicher fuer deine echten Outdoor-Startkoordinaten
double real_home_lat = 0.0;
double real_home_lon = 0.0;
bool home_position_fixed = false;

// =================================================================
// 2. TELEMETRIE ID-DEFINITIONEN (SmartPort)
// =================================================================
// --- BASIS DEFINITIONEN (K=1) ---
#define ID_VFAS         0x0210         // ID Hauptspannung
#define ID_CURRENT      0x0200         // ID Stromstaerke
#define ID_CAPACITY     0x0600         // ID Verbrauch
#define ID_ALTITUDE     0x0100         // ID Hoehe
#define ID_CLIMB_RATE   0x0110         // ID Vario
#define ID_RSSI         0xF101         // ID Signalstaerke
#define ID_GPS_SPEED    0x0830         // ID Geschwindigkeit (Log: 505, 311...)
//#define ID_SATS         0x0400         // ID Satelliten (Log: 106)
#define ID_SATS         0x0480         // ID Satelliten (Log: 106)
#define ID_GPS_LAT      0x0800         // ID Breitengrad (Log: 31585...)
#define ID_GPS_LON      0x0810         // ID Laengengrad
#define ID_ACC_X        0x0700         // ID Beschleunigung X (Log: -10)
#define ID_ACC_Y        0x0710         // ID Beschleunigung Y (Log: -2)
#define ID_ACC_Z        0x0720         // ID Beschleunigung Z (Log: 101)
#define ID_INAV_MODE    0x0470         // ID iNav Flugmodus (Log: 0, 2)
#define ID_DIST_HOME    0x0420         // GEAENDERT: iNav nutzt oft 0x0420 fuer Distanz
#define ID_CELS         0x0510         // ID Einzelzellen
#define ID_RXBT         0x0010         // ID Empfaengerspannung
#define ID_FUEL         0x0410         // ID Akku-Prozent (Log: 1608)
#define ID_HEADING      0x0840         // ID Kompasskurs (Log: 19350...)
#define ID_RPM          0x0500         // ID Drehzahl
#define ID_TEMP_ESC     0x0910         // ID ESC Temperatur (Log: 404)
#define ID_GPS_ALT      0x0460         // ID GPS Hoehe (Log: 1800)
#define ID_GPS_DIST_H   0x0420         // ID GPS Distanz Horizontal
#define ID_GPS_TIME     0xF105         // ID GPS Zeitstempel (Log: 992002)
#define ID_GPS_DATA2    0x0450         // ID GPS Zusatzdaten (Log: 1519...)
#define ID_GPS_TEST     0x0820         // NEU: Fuer Index 25 (Log: 2156...)
#define ID_BATT         0xF104         // ID Batteriespannung (Log: 2100)
#define ID_FIRST        0x0400         // ID Satelliten (Log: 106)


// --- DYNAMISCHE CONFIG FÜR SCHRITT 1 ---
char device_name[32] = "TX16S"; // KORREKTUR: Platz für max. 31 Zeichen + Nullterminator
uint32_t ble_pin = 1234;              


BluetoothSerial SerialBT;
bool isBleMode = false;
bool deviceConnected = false;
BLECharacteristic *pTxCharacteristic;

// --- VARIABLEN FÜR BUTTON-LOGIK (ZUSÄTZLICH) ---
unsigned long buttonPressTime = 0;
bool buttonActive = false;

// Erweiterte Verbindungs-Überwachung für BLE
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) { 
        deviceConnected = true; 
        if (debug_telemetry) {
            // \n am Anfang erzwingt eine saubere neue Zeile im Log
            Serial.println("\n------------------------------------------------------------");
            Serial.println("[BLE-STATUS] 🔵 Ein Geraet hat physisch angeklopft...");
            Serial.println("------------------------------------------------------------");
        }
    }
    void onDisconnect(BLEServer* pServer) { 
        deviceConnected = false; 
        if (debug_telemetry) {
            Serial.println("\n------------------------------------------------------------");
            Serial.println("[BLE-STATUS] 🔴 Verbindung verloren! Warte auf neuen Client...");
            Serial.println("------------------------------------------------------------");
        }
        delay(500); 
        pServer->getAdvertising()->start(); 
    }
};

class MySecurityCallbacks : public BLESecurityCallbacks {
    uint32_t onPassKeyRequest() {
        if (debug_telemetry) {
            Serial.print("\n[BLE-SECURITY] 🔑 LilyGo fordert PIN an! Sende Werkscode: ");
            Serial.println(ble_pin);
        }
        return ble_pin;
    }
    void onPassKeyNotify(uint32_t pass_key) {}
    bool onSecurityRequest() { return true; }
    bool onConfirmPIN(uint32_t pin) { return true; }
    
    void onAuthenticationComplete(esp_ble_auth_cmpl_t cmpl) {
        if (debug_telemetry) {
            Serial.println("\n------------------------------------------------------------");
            if (cmpl.success) {
                Serial.println("[BLE-SECURITY] ✅ PIN-Abgleich ERFOLGREICH! Geraet dauerhaft gekoppelt.");
            } else {
                Serial.print("[BLE-SECURITY] ❌ KOPPLUNG FEHLGESCHLAGEN! Grund-Code: ");
                Serial.println(cmpl.fail_reason);
            }
            Serial.println("------------------------------------------------------------\n");
        }
    }
};






// --- HILFSFUNKTION FÜR RESET ---
void resetPairedDevices() {
    Serial.println("!!! RESET: Lösche alle gespeicherten Bluetooth-Kopplungen !!!");
    
    // Holt die Anzahl der gekoppelten Geräte
    int dev_num = esp_ble_get_bond_device_num();
    if (dev_num > 0) {
        esp_ble_bond_dev_t *dev_list = (esp_ble_bond_dev_t *)malloc(sizeof(esp_ble_bond_dev_t) * dev_num);
        esp_ble_get_bond_device_list(&dev_num, dev_list);
        for (int i = 0; i < dev_num; i++) {
            // Löscht jedes einzelne Gerät aus dem Speicher
            esp_ble_remove_bond_device(dev_list[i].bd_addr);
        }
        free(dev_list);
    }

    // Optisches Feedback: LED blinkt schnell
    for(int i=0; i<10; i++) {
        digitalWrite(STATUS_LED, HIGH); delay(50);
        digitalWrite(STATUS_LED, LOW); delay(50);
    }
    Serial.println("Reset abgeschlossen. Bridge bereit für neue Kopplung.");
}


void setupBLE() {
    BLEDevice::init(device_name);
    

    BLEDevice::setMTU(512); // Passend zum Lilygo-Code

    BLEServer *pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    BLEService *pService = pServer->createService("6E400001-B5A3-F393-E0A9-E50E24DCCA9E");
    pTxCharacteristic = pService->createCharacteristic(
        "6E400003-B5A3-F393-E0A9-E50E24DCCA9E",
        BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_READ
    );

    // ERZWINGT VERSCHLÜSSELUNG FÜR DIESE DATEN:
    // Schützt vor unbefugtem Zugriff ohne PIN
    pTxCharacteristic->setAccessPermissions(ESP_GATT_PERM_READ_ENCRYPTED | ESP_GATT_PERM_WRITE_ENCRYPTED);

    pTxCharacteristic->addDescriptor(new BLE2902());
    
    // RX Characteristic (Wichtig für manche Client-Stacks)
    pService->createCharacteristic("6E400002-B5A3-F393-E0A9-E50E24DCCA9E", BLECharacteristic::PROPERTY_WRITE);
    
    pService->start();

    // --- SICHERHEIT FÜR LILYGO OPTIMIERT ---
    BLESecurity *pSecurity = new BLESecurity();
    // Nur Bonding, kein MITM (das verhindert den Rot/Blau Crash)
    //pSecurity->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_BOND); 
    
    // "Keine Ein-/Ausgabe" zwingt den Stack zum automatischen Abgleich
    //pSecurity->setCapability(ESP_IO_CAP_NONE); 
    
    // Von NONE auf IO (Input/Output) oder KBDISP (Keyboard Display)
    pSecurity->setCapability(ESP_IO_CAP_IO); 
    pSecurity->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND); // MITM aktivieren für PIN-Zwang

    pSecurity->setStaticPIN(1234);

    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    // UNBEDINGT NÖTIG: Damit der Lilygo-Scan das Gerät erkennt
    pAdvertising->addServiceUUID("6E400001-B5A3-F393-E0A9-E50E24DCCA9E");
    pAdvertising->setScanResponse(true);
    pAdvertising->start();
    
    BLEDevice::setSecurityCallbacks(new MySecurityCallbacks()); // Aktiviert den PIN-Wächter

}


void setup() {
    // ========================================================
    // --- SCHRITT 1: GEDÄCHTNIS INITIALISIEREN & LADEN ---
    // ========================================================
    // Öffnen des Speicher-Ordner "config" im Lese/Schreibmodus
    preferences.begin("config", false);

    // Erststart-Check: Falls das Gedächtnis komplett leer ist
    if (!preferences.isKey("init_done")) {
        Serial.println("[NVS] Erststart! Schreibe Werkseinstellungen...");
        preferences.putBool("is_ble", true);
        preferences.putUInt("bt_pin", 1234);
        preferences.putString("bt_name", "TX16S"); 
        preferences.putBool("is_debug", false); // NEU: Werkszustand für Debugger ist AUS
        preferences.putBool("init_done", true); 
    }

    // Werte aus dem Flash-Speicher in Variablen laden
    isBleMode = preferences.getBool("is_ble", false);
    ble_pin = preferences.getUInt("bt_pin", 1234);
    debug_telemetry = preferences.getBool("is_debug", false); // NEU: Live aus dem Flash laden

    // === HIER DIE REISSLEINE FÜR DEN TEST: WERT ERZWINGEN ===
    //debug_telemetry = true; // <--- Das überschreibt den Flash-Wert NUR im RAM für diesen Start!

    // Den Text sicher aus dem Flash in device_name-Array kopieren
    String savedName = preferences.getString("bt_name", "TX16S");
    strncpy(device_name, savedName.c_str(), sizeof(device_name) - 1);
    device_name[sizeof(device_name) - 1] = '\0'; 

    preferences.end(); // Speicher wieder schließen
    
// DYNAMISCH: Diese Ausgabe prüft die geladene Variable!
if (debug_telemetry) {
    Serial.print("[NVS] Konfiguration geladen. Name: "); Serial.print(device_name);
    Serial.print(" | Modus: "); Serial.print(isBleMode ? "BLE" : "Classic BT");
    Serial.print(" | PIN: "); Serial.println(ble_pin);
    Serial.print(" | Telemetrie-Debug: EIN\n");
}

    // ========================================================


    // 1. Initialisierung (Nutzt jetzt den geladenen dynamischen Namen!)
    BLEDevice::init(device_name);

    // 2. Löschen alle alten Bindungen über den Security-Manager
    // Das ersetzt das fehlerhafte nvs_flash_erase()
    BLESecurity *pSecurity = new BLESecurity();
    
    // Dieser Befehl löscht beim Start alle alten Schlüssel aus dem Speicher
    // (Einmalig drin lassen, flashen, danach wieder auskommentieren)
    // pSecurity->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_BOND); // Basis-Setup

    // Setzen das Gerät auf "Keine Ein/Ausgabe"
    // pSecurity->setCapability(ESP_IO_CAP_NONE); 
    
    // Von NONE auf IO (Input/Output) oder KBDISP (Keyboard Display)
    pSecurity->setCapability(ESP_IO_CAP_IO); 
    pSecurity->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND); // MITM aktivieren für PIN-Zwang

    // Nutzt den geladenen PIN aus dem Flash-Speicher!
    pSecurity->setStaticPIN(ble_pin); 
   

    pinMode(MODE_SWITCH_PIN, INPUT_PULLUP);
    pinMode(TRIGGER_SWITCH_PIN, INPUT_PULLUP);
    pinMode(STATUS_LED, OUTPUT);
    
    esp_task_wdt_init(5, true);
    esp_task_wdt_add(NULL);

    // Initialisierung Serial2 mit funktionierenden Pins
    Serial2.begin(115200, SERIAL_8N1, RX_PIN, -1);
    Serial.begin(115200); // Für Debugging, optional

    // HINWEIS: Die Hardware-Brücke (MODE_SWITCH_PIN) bleibt als Schutz aktiv.
    // Wenn der Pin auf Masse gezogen wird, erzwingen wir Classic BT, egal was das LUA wollte.
    if (digitalRead(MODE_SWITCH_PIN) == LOW) {
        isBleMode = false;
        Serial.println("[NOTFALL] Hardware-Schalter aktiv: Erzwinge Classic BT!");
    } else {
        // Falls keine Hardware-Brücke gesteckt ist, gilt der geladene Flash-Wert!
        isBleMode = preferences.getBool("is_ble", isBleMode); 
    }

    
    if (isBleMode) {
      setupBLE(); 
    } else {
      SerialBT.begin(device_name);
    
      // Event-Wächter für Classic BT (Zeigt an wer sich verbindet)
      if (debug_telemetry) {
        SerialBT.register_callback([](esp_spp_cb_event_t event, esp_spp_cb_param_t *param){
            if (event == ESP_SPP_SRV_OPEN_EVT) {
               Serial.print("[BT-CLASSIC] ✅ Client erfolgreich verbunden! MAC: ");
               for (int i = 0; i < 6; i++) {
                  Serial.print(param->srv_open.rem_bda[i], HEX);
                  if (i < 5) Serial.print(":");
              }
              Serial.println();
            }
            if (event == ESP_SPP_CLOSE_EVT) {
              Serial.println("[BT-CLASSIC] ❌ Client hat die Verbindung getrennt.");
            }
        });
      }
}

}




// --- BAUSTEIN 1: TASTER-RESET ---
void handleResetButton() {
    bool buttonPressed = (digitalRead(TRIGGER_SWITCH_PIN) == LOW);
    if (buttonPressed && !buttonActive) {
        buttonPressTime = millis();
        buttonActive = true;
    } 
    if (!buttonPressed && buttonActive) {
        if (millis() - buttonPressTime > 3000) resetPairedDevices();
        buttonActive = false;
    }
}

// --- BAUSTEIN 2: UART-TELEMETRIE EINLESEN ---
void readIncomingTelemetry() {
    while (Serial2.available() && !paketFertig) {
        uint8_t c = Serial2.read();

        if (mode == 0) { // Suchmodus
            if (lastByte == 0x4D && c == 0x50) {
                mode = 1; pIdx = 0; logicalCount = 0;
                expectedPayloadLength = 10; 
                packetBuffer[pIdx++] = 0x4D; packetBuffer[pIdx++] = 0x50;
            } else if (c == 0x7E) {
                mode = 2; pIdx = 0; logicalCount = 0;
                packetBuffer[pIdx++] = 0x7E; isEscaped = false;
            }
            // === BEFEHLE VOM LUA-SCRIPT ERKENNEN ===
            else if (lastByte == 0xAA && c == 0xBB) {
                mode = 3; // Schalte um auf "Einstellungs-Modus"
                pIdx = 0;
                logicalCount = 0; // Zähler für die kommenden Bytes nullen
                packetBuffer[pIdx++] = 0xAA;
                packetBuffer[pIdx++] = 0xBB;
            }
            lastByte = c;
            continue;
        }

        if (pIdx < 120) packetBuffer[pIdx++] = c;

        if (mode == 1) { // MPM Modus
            logicalCount++;
            if (logicalCount == 2) expectedPayloadLength = c; 
            if (logicalCount == 5) lsbMatched = (c == 0x50); 
            if (logicalCount == 6) {
                if (lsbMatched && c == 0x04) last999Time = millis(); 
                lsbMatched = false; 
            }
            if (logicalCount >= (expectedPayloadLength + 1)) paketFertig = true; 
        } 
        else if (mode == 2) { // Natives S.Port Modus
            if (c == 0x7D) {
                isEscaped = true; 
            } else {
                if (!isEscaped) logicalCount++;
                else isEscaped = false;
            }
            if (logicalCount >= 9) paketFertig = true; 
        }
        // === MODUS 3: EINSTELLUNGS-MODUS (Sammelt die Bytes nach AA BB) ===
        else if (mode == 3) {
            logicalCount++;
            
            // Feste Länge für Standardbefehle: Typ (1 Byte) + Wert (1 Byte) = 2 Bytes nach dem Header
            static int luaExpectedPayload = 2; 

            // Sonderfall Name ändern (Befehl 0x04): Das 4. Gesamtbyte (Index 3) enthält die Anzahl der Buchstaben!
            if (logicalCount == 2 && packetBuffer[2] == 0x04) {
                luaExpectedPayload = c + 2; // Befehlstyp + Längen-Byte + Buchstaben-Anzahl
            }

            if (logicalCount >= luaExpectedPayload) {
                paketFertig = true;
                luaExpectedPayload = 2; // Reset für das nächste Mal
            }
        }
        
        if (pIdx >= 120) { mode = 0; pIdx = 0; logicalCount = 0; paketFertig = false; }
        lastByte = c;
    }
}


void processLuaCommand() {
    uint8_t commandType = packetBuffer[2];  // Das 3. Byte enthält den Befehl
    uint8_t commandValue = packetBuffer[3]; // Das 4. Byte enthält den Wert (oder die Textlänge)

    if (debug_telemetry) {
        Serial.print("[LUA]\tCmd: ");   Serial.print(commandType, HEX);
        Serial.print("\tVal/Len: ");   Serial.print(commandValue);
        Serial.print("\tStatus: ");
    }

    bool triggerRestart = false;
    bool success = false; // Merkt sich, ob der Befehl erfolgreich war

    switch (commandType) {
        case 0x01: // Bluetooth-Modus ändern (Classic BT / BLE)
            if (debug_telemetry) Serial.println("MODUS_CHANGE");
            preferences.begin("config", false);
            preferences.putBool("is_ble", (commandValue == 1));
            preferences.end();
            success = true;
            triggerRestart = true;
            break;
            
        case 0x02: // ===  ECHTE PIN ALS TEXT EMPFANGEN & WANDELN ===
            if (debug_telemetry) Serial.println("PIN_TEXT_CHANGE");
            // Sicherheitsgrenze: PIN-Länge muss über 0 und unter 10 Zeichen liegen
            if (commandValue > 0 && commandValue < 10) {
                String newPinStr = "";
                // Sammelt die ASCII-Zeichen nacheinander ab Index 4 aus dem Puffer
                for (int i = 0; i < commandValue; i++) {
                    newPinStr += (char)packetBuffer[4 + i];
                }
                
                // Text-zu-Zahl-Wandlung (toInt)
                uint32_t full_four_digit_pin = newPinStr.toInt();
                
                preferences.begin("config", false);
                preferences.putUInt("bt_pin", full_four_digit_pin);
                preferences.end();
                
                if (debug_telemetry) {
                    Serial.printf("[LUA]\tEchte PIN aus Text erfolgreich konvertiert und gespeichert: %04d\n", full_four_digit_pin);
                }
                
                success = true;
                triggerRestart = true; // ESP32 startet neu und übernimmt die neue Kopplungs-PIN
            }
            break;

        case 0x03: // Koppel-Reset ausführen 
            if (commandValue == 1) { 
                if (debug_telemetry) Serial.println("EXECUTE_RESET");
                resetPairedDevices(); 
                success = true; 
            } else {
                if (debug_telemetry) Serial.println("RESET_DENIED");
            }
            break;

        case 0x04: // Bluetooth-Namen speichern (ASCII-Textkette)
            if (debug_telemetry) Serial.println("NAME_CHANGE");
            if (commandValue > 0 && commandValue < 30) {
                String newName = "";
                for (int i = 0; i < commandValue; i++) {
                    newName += (char)packetBuffer[4 + i];
                }
                preferences.begin("config", false);
                preferences.putString("bt_name", newName);
                preferences.end();
                success = true;
                triggerRestart = true;
            }
            break;
 
        case 0x05: // Debug-Modus per LUA ändern (Log-Verhalten ein/aus)
            if (debug_telemetry) Serial.println("DEBUG_CHANGE");
            preferences.begin("config", false);
            preferences.putBool("is_debug", (commandValue == 1));
            preferences.end();
            success = true;
            triggerRestart = true; 
            break;

        default:
            if (debug_telemetry) Serial.println("UNKNOWN_COMMAND");
            break;
    }

    /*
    // === ERWEITERTE RÜCKMELDUNG ÜBER PIN 17 AN DIE TX16S ===
    if (success) {
        Serial2.write(0xCC);        // 1. Byte: Status OK
        Serial2.write(commandType); // 2. Byte: Welcher Befehl war erfolgreich?
    } else {
        Serial2.write(0xEE);        // 1. Byte: Status FEHLER
        Serial2.write(commandType); // 2. Byte: Welcher Befehl ist fehlgeschlagen?
    }
    */

    // Reset für das LUA-Paket (Absolut identisch mit deinem Original)
    mode = 0; pIdx = 0; logicalCount = 0; isEscaped = false; paketFertig = false;

    if (triggerRestart) {
        if (debug_telemetry) Serial.println("[LUA] Einstellungen gespeichert. Starte ESP32 neu...");
        delay(1000); 
        ESP.restart(); 
    }
}



// ==============================================================================
// --- ARDUPILOT (YAAPU) ZU INAV TELEMETRIE-ÜBERSETZER ---
// ==============================================================================

void sendInavBluetoothPacket(uint8_t sensorId, uint16_t appId, uint32_t dataValue) {
    // REPARATUR: Wir befüllen exakt das Array, das dein Log unten ausliest!
    // Dadurch wird die Anzeige im Monitor zu 100% synchron mit dem Bluetooth-Stream.
    convertedBuffer[0] = 0x7E;                           // S.Port Startbyte
    convertedBuffer[1] = sensorId;                       // Physische Sensor ID (iNAV Standard = 0x1B)
    convertedBuffer[2] = 0x10;                           // Frame-Typ: Daten-Frame
    convertedBuffer[3] = appId & 0xFF;                   // App-ID Low (z.B. 0x01)
    convertedBuffer[4] = (appId >> 8) & 0xFF;            // App-ID High (z.B. 0xF1)
    convertedBuffer[5] = dataValue & 0xFF;               // Daten Byte 1 (Low)
    convertedBuffer[6] = (dataValue >> 8) & 0xFF;        // Daten Byte 2
    convertedBuffer[7] = (dataValue >> 16) & 0xFF;       // Daten Byte 3
    convertedBuffer[8] = (dataValue >> 24) & 0xFF;       // Daten Byte 4 (High)
    
    // FrSky Checksumme berechnen: 0xFF - (Summe aller Bytes von Index 1 bis 8)
    uint16_t checksum_sum = 0;
    for (int i = 1; i <= 8; i++) {
        checksum_sum += convertedBuffer[i];
    }
    convertedBuffer[9] = 0xFF - (checksum_sum & 0xFF);

    // Abzug über Bluetooth
    extern bool isBleMode; 
    extern BLECharacteristic* pTxCharacteristic;

    if (isBleMode) {
        pTxCharacteristic->setValue(convertedBuffer, 10); 
        pTxCharacteristic->notify();
    } else {
        SerialBT.write(convertedBuffer, 10);
    }
}


/*
===================================================================================================
DOKUMENTATION: MAPPING-STECKBRIEF (YAAPU-EINGANG ZU 1:1 FRSKY-MULTIPLEX-IDS)
===================================================================================================
PAKET ID | ARDUPILOT / YAAPU BIT-EXTRAKTION         | BLUETOOTH iNAV-HEX-ID & SKALIERUNG
===================================================================================================

0x5003   | [AKKU-HAUPTZENTRALE]                     |

         | -> Bits  0 -  8 : Hauptspannung          | ==> 0x0210 (VFAS)      | Faktor 100.0f (Hundertstel-Volt)
         | -> Bit   9      : Strom-Zehnerpotenz     | ==> 0x0200 (CURRENT)   | Faktor 10.0f  (Zehntel-Ampere)
         | -> Bits 10 - 16 : Strom-Ziffernbits      |    (Rechnung: Ziffern * 10^Potenz)
         | -> Bits 17 - 31 : Verbrauch (mAh)        | ==> 0x0600 (CAPACITY)  | Faktor 1.0f   (mAh direkt 1:1)
---------+------------------------------------------+----------------------------------------------
0x5005   | [DYNAMISCHE FLUGDATEN]                   |

         | -> Bits 16 - 31 : Kompasskurs (Yaw)      | ==> 0x0840 (HEADING)   | Faktor 100.0f (Hundertstel-Grad)
         |                                          |    (Liefert Zehntel-Grad -> Wert * 10)
         | -> Bits  1 -  7 : Steigrate (Vario) Base | ==> 0x0110 (CLIMB_RATE)| Faktor 1.0f   (cm/s)
         | -> Bit   0      : Steigrate Potenz       |    (Liefert dm/s -> Wert * 10)
         | -> Bit   8      : Steigrate Vorzeichen   |    (Bit 8 == 1 -> Wert ist negativ)
         | -> Bits 10 - 16 : GPS-Speed Base         | ==> 0x0830 (GPS_SPEED) | iNAV-SmartPort-VFR-Raster
         | -> Bit   9      : GPS-Speed Potenz       |    (Liefert dm/s -> Wert * 36)
---------+------------------------------------------+----------------------------------------------
0x5004   | [HOME-NAVIGATION]                        |

         | -> Bits  2 - 11 : Entfernung zu Home     | ==> 0x0420 (DIST_HOME) | Faktor 1.0f   (Meter starr 1:1)
         | -> Bits  0 -  1 : Entfernung Potenz      |    (Rechnung: Base * 10^Potenz)
         | -> Bits 14 - 23 : Relative Flughöhe Base | ==> 0x0100 (ALTITUDE)  | Faktor 100.0f (Zentimeter)
         | -> Bits 12 - 13 : Relative Höhe Potenz   |    (Rechnung: Base * 10^Potenz)
         | -> Bit  24      : Höhe Vorzeichen        |    (Bit 24 == 1 -> Flieger unter Startniveau)
---------+------------------------------------------+----------------------------------------------
0x5002   | [GPS-EMPFANGSSTATUS & 1:1 KOORDINATEN]   |

         | -> Bits  0 -  3 : Anzahl Satelliten      | ==> 0x0480 (SATS)      | Verschachteltes iNAV-Fix-Raster!
         | -> Bits 16 - 18 : ArduPilot GPS-Fix-Typ   |                         (Formel: Fix-Typ * 100 + Sats)
         |                                          |                         (Fix >= 3 -> 17 für stabilen 3D-Fix)
         |                                          |                         (Fix == 2 -> 14 für reinen 2D-Fix)
         | -> Bits 24 - 30 : GPS-Höhe (GALT) Base   | ==> 0x0460 (GPS_ALT)   | Faktor 100.0f (Zentimeter)
         | -> Bits 22 - 23 : GPS-Höhe Potenz        |    (Liefert dm -> Wert * 10)
         | -> Bit  31      : GPS-Höhe Vorzeichen    |    (Bit 31 == 1 -> unter Meeresspiegel)
         |                                          |
         | -> Bits  0 - 29 : Reine Live-Koordinate  | ==> 0x0800 (LAT / LON) | FrSky-S.Port Multiplex-Weiche
         | -> Bits 30 - 31 : GPS-Typen-Erkennung     |    (Typ 1 = LAT -> Bit 31 bleibt starr 0)
         |                                          |    (Typ 2 = LON -> Bit 31 wird starr auf 1 gezwungen)
---------+------------------------------------------+----------------------------------------------
0x5001   | [COPTER / FLIEGER STATUS]                |

         | -> Bits  0 -  4 : ArduPilot Flugmodus    | ==> 0x0470 (INAV_MODE) | iNAV-Bit-Statusregister
         | -> Bit   8      : Armed / Disarmed Status|    (Bit 0: Armed | Bit 1: Angle | Bit 2: Horizon
         |                                          |     Bit 3: Position Hold | Bit 4: RTH)
         | -> Bits 26 - 31 : IMU-Innentemperatur    | ==> 0x0910 (TEMP_ESC)  | Faktor 1.0f   (°C direkt 1:1)
         |                                          |    (Rechnung: Gelesene Bits + 19)
---------+------------------------------------------+----------------------------------------------
0x5006   | [TRÄGHEITS- & FLUGLAGEDATEN (IMU)]       |

         | -> Bits  0 - 10 : Roll-Winkel (φ) Base   | ==> 0x0700 (ACC_X)     | Projizierte G-Kräfte 
         | -> Bits 11 - 20 : Pitch-Winkel (θ) Base  | ==> 0x0710 (ACC_Y)     | an die handleACC Gegenseite
         | -> (Trigonometrische Rückrechnung im ESP)| ==> 0x0720 (ACC_Z)     | Standardisiert im 1G=100 Raster
---------+------------------------------------------+----------------------------------------------
0x5007   | [ROLLIERENDE REGISTER-PARAMETER]         |

         | -> Bits 24 - 27 : Parameter-Index (ID)   | ==> 0x0450 (GPS_DATA2) | iNAV-Zusatzdaten-Kanal
         | -> Bits  0 - 23 : Parameter-Wert (Value) |    (Wenn ID == 8 -> Thermik-Status: 
         |                                          |     0 = Suchen, 1 = Update, 2 = Zentrieren)
         |                                          | ==> 0x0470 (INAV_MODE) | Parallel in Bit 16/17 von 0x0470
===================================================================================================
*/



// [DOKUKONFORM] C++ Übersetzung mit dem FrSky-Ziel-Faktor 600.000f fuer handleGPS
void calculateRealLiveGPS(uint32_t distance, uint32_t angle, uint32_t& target_lat, uint32_t& target_lon) {
    if (!home_position_fixed) {
        // Fallback Hambuehren Werkbank im DDMM.MMMM Format (Dezimalgrad * 600000.0f)
        target_lat = (uint32_t)(52.6394 * 600000.0); // 31583640
        target_lon = (uint32_t)(9.8228 * 600000.0);  // 5893680
        return;
    }

    double lat1 = real_home_lat * M_PI / 180.0;
    double lon1 = real_home_lon * M_PI / 180.0;
    double Ad = (double)distance / 6371000.0; 
    double angle_rad = (double)angle * M_PI / 180.0;

    double lat2 = asin(sin(lat1) * cos(Ad) + cos(lat1) * sin(Ad) * cos(angle_rad));
    double lon2 = lon1 + atan2(sin(angle_rad) * sin(Ad) * cos(lat1), cos(Ad) - sin(lat1) * sin(lat2));

    // Umrechnung in Dezimalgrad und Skalierung starr mit 600.000 fuer deinen Empfaenger
    target_lat = (uint32_t)((lat2 * 180.0 / M_PI) * 600000.0);
    target_lon = (uint32_t)((lon2 * 180.0 / M_PI) * 600000.0);
}

void convertYaapuToInav(uint8_t sensorID, uint8_t yaapuSubID, uint32_t yaapuData) {
    
    uint8_t b1 = yaapuData & 0xFF;         
    uint8_t b2 = (yaapuData >> 8) & 0xFF;  
    uint8_t b3 = (yaapuData >> 16) & 0xFF; 
    uint8_t b4 = (yaapuData >> 24) & 0xFF; 

    static uint32_t last_valid_current_bits = 0;
    static uint32_t shared_mode_register = 0;
    
    static uint32_t current_outdoor_distance = 0;
    static uint32_t current_outdoor_angle = 0;

    // ==================================================================
    // [DOKUKONFORM] UNZENSIERTES ROHDATEN-LOG (NUR BEI AKTIVEM FLAG)
    // ==================================================================
    if (debug_telemetry) {
        Serial.printf("[YAAPU-IN] ID: 0x50%02X | Sensor: 0x%02X | Full-HEX: 0x%08X -> B1: %u | B2: %u | B3: %u | B4: %u", 
                      yaapuSubID, sensorID, yaapuData, b1, b2, b3, b4);
    }

    // ==================================================================
    // 1. [DOKUKONFORM] PAKET 0x03 (AKKU-HAUPTZENTRALE) -> SENDET AN APP
    // ==================================================================
    if (yaapuSubID == 0x03) {
        uint32_t raw_volt = yaapuData & 0x1FF; 
        uint32_t iNAV_vfas = raw_volt * 10; 

        uint32_t raw_curr = (yaapuData >> 10) & 0x7F;
        uint8_t power = (yaapuData >> 9) & 0x01;
        uint32_t iNAV_current = raw_curr * ((power == 1) ? 10 : 1);

        uint32_t iNAV_capacity = (yaapuData >> 17) & 0x7FFF;

        sendInavBluetoothPacket(0x1B, 0x0210, iNAV_vfas);     
        sendInavBluetoothPacket(0x1B, 0x0200, iNAV_current);  
        sendInavBluetoothPacket(0x1B, 0x0600, iNAV_capacity); 

        if (debug_telemetry) {
            Serial.printf(" | [BLUETOOTH-TX 0x03] VFAS: %u | CURRENT: %u | CAPACITY: %u", iNAV_vfas, iNAV_current, iNAV_capacity);
        }
    }
    
    // ==================================================================
    // 2. [DOKUKONFORM] PAKET 0x06 (REINE FLUGLAGE / ROLLPITCH) -> SENDET AN APP
    // ==================================================================
    else if (yaapuSubID == 0x06) {
        uint32_t raw_roll = yaapuData & 0x7FF;
        uint32_t raw_pitch = (yaapuData >> 11) & 0x3FF;

        float roll_deg = ((float)raw_roll - 900.0f) * 0.2f;
        float pitch_deg = ((float)raw_pitch - 450.0f) * 0.2f; 

        float roll_rad = -roll_deg * M_PI / 180.0f;
        float pitch_rad = -pitch_deg * M_PI / 180.0f;

        int32_t accX = (int32_t)(100.0f * sin(pitch_rad));
        int32_t accY = (int32_t)(100.0f * sin(roll_rad) * cos(pitch_rad));
        int32_t accZ = (int32_t)(100.0f * cos(roll_rad) * cos(pitch_rad));

        sendInavBluetoothPacket(0x1B, 0x0700, (uint32_t)accX); 
        sendInavBluetoothPacket(0x1B, 0x0710, (uint32_t)accY); 
        sendInavBluetoothPacket(0x1B, 0x0720, (uint32_t)accZ); 

        if (debug_telemetry) {
            Serial.printf(" | [BLUETOOTH-TX 0x06] ACC-X: %d | ACC-Y: %d | ACC-Z: %d", accX, accY, accZ);
        }
    }
    
    // ==================================================================
    // 3. [DOKUKONFORM] PAKET 0x05 (KURS, CLIMB & SPEED) -> SENDET AN APP
    // ==================================================================
    else if (yaapuSubID == 0x05) {
        uint32_t raw_heading_tenths = (yaapuData >> 16) & 0xFFFF; 
        uint32_t iNAV_heading = raw_heading_tenths * 10; 

        uint32_t v_base = (yaapuData >> 1) & 0x7F;
        uint8_t v_pow = yaapuData & 0x01;
        int32_t raw_vspeed_dms = v_base * ((v_pow == 1) ? 10 : 1); 
        if (((yaapuData >> 8) & 0x01) == 1) raw_vspeed_dms = -raw_vspeed_dms;
        int32_t iNAV_climb = raw_vspeed_dms * 10; 

        uint32_t s_base = (yaapuData >> 10) & 0x7F;
        uint8_t s_pow = (yaapuData >> 9) & 0x01;
        uint32_t raw_speed_dms = s_base * ((s_pow == 1) ? 10 : 1); 
        uint32_t iNAV_speed = raw_speed_dms * 36; 

        sendInavBluetoothPacket(0x1B, 0x0840, iNAV_heading);         
        sendInavBluetoothPacket(0x1B, 0x0110, (uint32_t)iNAV_climb); 
        sendInavBluetoothPacket(0x1B, 0x0830, iNAV_speed);           

        if (debug_telemetry) {
            Serial.printf(" | [BLUETOOTH-TX 0x05] HEADING: %u | CLIMB: %d | SPEED: %u", iNAV_heading, iNAV_climb, iNAV_speed);
        }
    }

    // ==================================================================
    // 4. [DOKUKONFORM] PAKET 0x04 (ENTFERNUNG & HOME-HÖHE) -> SENDET AN APP
    // ==================================================================
    else if (yaapuSubID == 0x04) {
        uint32_t dist_base = (yaapuData >> 2) & 0x3FF;
        uint8_t dist_pow = yaapuData & 0x03;
        current_outdoor_distance = dist_base * ((dist_pow == 1) ? 10 : (dist_pow == 2 ? 100 : (dist_pow == 3 ? 1000 : 1)));

        uint32_t alt_base = (yaapuData >> 14) & 0x3FF;
        uint8_t alt_pow = (yaapuData >> 12) & 0x03;
        int32_t iNAV_altitude = alt_base * ((alt_pow == 1) ? 10 : (alt_pow == 2 ? 100 : (alt_pow == 3 ? 1000 : 1)));
        if (((yaapuData >> 24) & 0x01) == 1) iNAV_altitude = -iNAV_altitude;
        iNAV_altitude = iNAV_altitude * 10; 

        sendInavBluetoothPacket(0x1B, 0x0420, current_outdoor_distance); 
        sendInavBluetoothPacket(0x1B, 0x0100, (uint32_t)iNAV_altitude);   

        if (debug_telemetry) {
            Serial.printf(" | [BLUETOOTH-TX 0x04] DIST: %u | ALTITUDE: %d", current_outdoor_distance, iNAV_altitude);
        }
    }


    // ==================================================================
    // 5. [DOKUKONFORM] PAKET 0x02 (SATELLITEN-VERSCHACHTELUNG & 1:1 GPS)
    // ==================================================================
    else if (yaapuSubID == 0x02) {
        // A) SATELLITEN & FIX-VERSCHACHTELUNG (Starr nach handleSats-Gesetz)
        uint32_t raw_sats = yaapuData & 0x0F; 
        uint8_t ardu_fix  = (yaapuData >> 16) & 0x07; 

        uint32_t inav_fix_type = 0;
        if (ardu_fix >= 3) {
            inav_fix_type = 17; // Stabilisierter 3D-Fix fuer deine App
        } else if (ardu_fix == 2) {
            inav_fix_type = 14; // Reiner 2D-Fix fuer deine App
        }

        uint32_t iNAV_sats_combined = (inav_fix_type * 100) + raw_sats;

        // B) GPS-HÖHE -> Sende starr an 0x0460
        uint32_t galt_base = (yaapuData >> 24) & 0x7F;
        uint8_t galt_pow = (yaapuData >> 22) & 0x03;
        int32_t iNAV_gps_alt = galt_base * ((galt_pow == 1) ? 10 : (galt_pow == 2 ? 100 : (galt_pow == 3 ? 1000 : 1)));
        if (((yaapuData >> 31) & 0x01) == 1) iNAV_gps_alt = -iNAV_gps_alt;
        iNAV_gps_alt = iNAV_gps_alt * 10; 

        sendInavBluetoothPacket(0x1B, 0x0480, iNAV_sats_combined);               
        sendInavBluetoothPacket(0x1B, 0x0460, (uint32_t)iNAV_gps_alt); 

        // C) REINES 1:1 GPS-MULTIPLEXING AN ID 0x0800
        uint32_t raw_coord = yaapuData & 0x3FFFFFFF;
        uint32_t coord_type = (yaapuData >> 30) & 0x3;
        uint32_t frsky_gps_frame = 0;

        if (coord_type == 1) {
            frsky_gps_frame = raw_coord & 0x7FFFFFFF;
            sendInavBluetoothPacket(0x1B, 0x0800, frsky_gps_frame);
            if (debug_telemetry) {
                Serial.printf(" | [BLUETOOTH-TX 0x02] COMBINED-SATS: %u | LAT-RAW: %u", iNAV_sats_combined, frsky_gps_frame);
            }
        } 
        else if (coord_type == 2) {
            frsky_gps_frame = raw_coord | 0x80000000;
            sendInavBluetoothPacket(0x1B, 0x0800, frsky_gps_frame);
            if (debug_telemetry) {
                Serial.printf(" | [BLUETOOTH-TX 0x02] COMBINED-SATS: %u | LON-RAW: %u", iNAV_sats_combined, frsky_gps_frame);
            }
        }
        else {
            if (debug_telemetry) {
                Serial.printf(" | [BLUETOOTH-TX 0x02] COMBINED-SATS: %u | GPS_ALT: %d", iNAV_sats_combined, iNAV_gps_alt);
            }
        }
    }



    // ==================================================================
    // 6. [DOKUKONFORM] PAKET 0x01 (STATUS, MODUS & ARMED) -> SENDET AN APP
    // ==================================================================
    else if (yaapuSubID == 0x01) {
        uint8_t ardu_mode = yaapuData & 0x1F;
        uint8_t status_armed = (yaapuData >> 8) & 0x01;
        
        shared_mode_register &= 0xFFFF0000;
        if (status_armed == 1) shared_mode_register |= (1 << 0); 
        
        if (ardu_mode == 0)       shared_mode_register |= (1 << 1); 
        else if (ardu_mode == 2)  shared_mode_register |= (1 << 2); 
        else if (ardu_mode == 5)  shared_mode_register |= (1 << 3); 
        else if (ardu_mode == 11) shared_mode_register |= (1 << 4); 
        else if (ardu_mode == 20) shared_mode_register |= (1 << 2) | (1 << 3); 

        uint32_t iNAV_temp = ((yaapuData >> 26) & 0x3F) + 19;

        sendInavBluetoothPacket(0x1B, 0x0470, shared_mode_register); 
        sendInavBluetoothPacket(0x1B, 0x0910, iNAV_temp);            

        if (debug_telemetry) {
            Serial.printf(" | [BLUETOOTH-TX 0x01] iNAV_MODE: %u | TEMP_ESC: %u", shared_mode_register, iNAV_temp);
        }
    }

    // ==================================================================
    // 7. [DOKUKONFORM] PAKET 0x07 (ROLLIERENDE PARAMETER) -> SENDET AN APP
    // ==================================================================
    else if (yaapuSubID == 0x07) {
        uint32_t param_id = (yaapuData >> 24) & 0x0F;
        uint32_t param_value = yaapuData & 0xFFFFFF;

        if (param_id == 8) {
            uint32_t iNAV_thermal_state = param_value; 
            
            sendInavBluetoothPacket(0x1B, 0x0450, iNAV_thermal_state);   

            shared_mode_register &= ~(0x03 << 16);             
            shared_mode_register |= (iNAV_thermal_state << 16); 
            
            sendInavBluetoothPacket(0x1B, 0x0470, shared_mode_register); 

            if (debug_telemetry) {
                Serial.printf(" | [BLUETOOTH-TX 0x07] GPS_DATA2: %u | iNAV_MODE: %u", iNAV_thermal_state, shared_mode_register);
            }
        }
    }

    // Zeilenumbruch ebenfalls starr an das Flag binden!
    if (debug_telemetry) {
        Serial.println();
    }
}

void dispatchTelemetryPacket(bool connected) {
    if (!paketFertig) return;

    // === ALLGEMEINER LUA-TÜRSTEHER (Unberührt aus deinem Original) ===
    if (mode == 3) {
        processLuaCommand(); 
        return;              
    }

    // ================================================================
    // GEPRÜFTE WEICHEN-STRUKTUR (Absolut entriegelt & krisenfest)
    // ================================================================
    uint8_t* debugBuffer = packetBuffer;
    int debugSize = pIdx;
    bool isStandardTelemetryFrame = (mode == 1 && expectedPayloadLength == 9);
    bool isModulStatusText = (mode == 1 && packetBuffer[4] == 0x01);

    if (isModulStatusText) {
        mode = 0; pIdx = 0; logicalCount = 0; isEscaped = false; paketFertig = false;
        return;
    }

    if (isStandardTelemetryFrame) {
        // Yaapu-Türsteher: Index 7 muss 0x50 sein, Index 6 ist die Sub-ID
        if (packetBuffer[7] == 0x50 && packetBuffer[6] <= 0x0F) {
            uint8_t yaapuSubID = packetBuffer[6]; 
            
            uint32_t incomingData = ((uint32_t)packetBuffer[11] << 24) |
                                    ((uint32_t)packetBuffer[10] << 16) |
                                    ((uint32_t)packetBuffer[9]  << 8)  |
                                    packetBuffer[8];

            uint8_t currentSensorID = packetBuffer[4];
            
            // Daten werden ab Einschalten permanent im Hintergrund generiert!
            convertYaapuToInav(currentSensorID, yaapuSubID, incomingData);
            
            // Starre Trennung: Funktion sofort beenden! Yaapu überspringt die inav-Tabelle.
            mode = 0; pIdx = 0; logicalCount = 0; isEscaped = false; paketFertig = false;
            return;
        }

        // --- REGULÄRER iNAV / STANDARD DURCHLÄUFER (9 Bytes aber kein Yaapu) ---
        convertedBuffer[0] = 0x7E;  
        for(int i = 0; i < 8; i++) convertedBuffer[i+1] = packetBuffer[i+4];
        convertedBuffer[9] = packetBuffer[pIdx-1]; 

        debugBuffer = convertedBuffer; debugSize = 10;

        if (connected) {
            if (isBleMode) {
                pTxCharacteristic->setValue(convertedBuffer, 10); 
                pTxCharacteristic->notify();
            } else {
                SerialBT.write(convertedBuffer, 10);
            }
        }
    } 
    else {
        // --- GRENZKONTROLLE FÜR PAKETE AUSSERHALB DER 9-BYTE PAYLOAD ---
        if (pIdx > 14) {
            if (debug_telemetry) {
                if (packetBuffer[0] == 0x4D && packetBuffer[1] == 0x50) {
                    uint8_t v_major = packetBuffer[2];
                    uint8_t v_minor = packetBuffer[3];
                    uint8_t v_rev   = packetBuffer[4];
                    uint8_t v_patch = packetBuffer[5];
                    uint8_t mpm_status = packetBuffer[6];
                    uint8_t mpm_option = packetBuffer[7];
                    int16_t mpm_finetune = (int16_t)((packetBuffer[9] << 8) | packetBuffer[8]);

                    Serial.printf(" | [MPM] VER: %u.%u.%u.%u | STAT: 0x%02X | OPT: 0x%02X | DATA-BLOCK: %02X %02X %02X %02X | PROT: ", 
                                  v_major, v_minor, v_rev, v_patch, mpm_status, mpm_option, packetBuffer[8], packetBuffer[9], packetBuffer[10], packetBuffer[11]);

                    for (int i = 12; i < pIdx; i++) {
                        if (packetBuffer[i] >= 32 && packetBuffer[i] <= 126) {
                            Serial.printf("%c", (char)packetBuffer[i]); 
                        }
                    }
                    Serial.println(); 
                } 
                else {
                    Serial.print(" | [SERIAL] UNKNOWN OVERSIZE FRAME! Paket-HEX: ");
                    for (int i = 0; i < pIdx; i++) {
                        Serial.printf("%02X ", packetBuffer[i]);
                    }
                    Serial.println();
                }
            }
        } 
        else {
            if (connected) {
                if (isBleMode) {
                    pTxCharacteristic->setValue(packetBuffer, pIdx); 
                    pTxCharacteristic->notify();
                } else {
                    SerialBT.write(packetBuffer, pIdx);
                }
            }
        }
    }

    // ====================================================================
    // ORIGINALER DEBUG-BLOCK (JETZT MIT ECHTER KLARTEXT-UNTERSCHEIDUNG)
    // ====================================================================
    if (debug_telemetry) {
        if (mode == 1) { Serial.print("MPM\tLen: "); Serial.print(expectedPayloadLength); } 
        else if (mode == 2) { Serial.print("S.PORT\tLen: 9"); }
        else if (mode == 3) { Serial.print("LUA_CFG\tLen: "); Serial.print(pIdx); }
        
        Serial.print("\tSentBytes: "); Serial.print(isModulStatusText ? 0 : debugSize); 
        Serial.print("\tStatus: ");
        
        if (!connected) {
            Serial.print("NO_BT_CLIENT ");
        } else if (isModulStatusText) {
            Serial.print("BLOCKED_STAT ");
        } else {
            // REPARATUR-WEICHE: Prüft unbestechlich, aus welchem Puffer die Daten stammen!
            if (debugBuffer == convertedBuffer) {
                Serial.print("UMSETZ_7E    "); // ESP hat das 7E künstlich vorne drangebaut
            } else {
                Serial.print("NATIV_7E     "); // Das Paket kam bereits fertig als 7E aus dem Kabel
            }
        }
        
        Serial.print("\tTX_Data: ");
        if (isModulStatusText) {
            Serial.print("[Modul-Status unterdrueckt]");
        } else { 
            for (int i = 0; i < debugSize; i++) { 
                if (debugBuffer[i] < 0x10) Serial.print("0"); 
                Serial.print(debugBuffer[i], HEX); Serial.print(" "); 
            } 
        }
        
        Serial.println(); 
    }

    // Reset für das Telemetrie-Paket (Unveraendert wie in Original)
    mode = 0; pIdx = 0; logicalCount = 0; isEscaped = false; paketFertig = false; 
}

void loop() {
    esp_task_wdt_reset();

    // 1. Taster abfragen
    handleResetButton();

    // 2. Verbindungsstatus prüfen
    bool connected = isBleMode ? deviceConnected : SerialBT.hasClient();
    digitalWrite(STATUS_LED, connected ? LOW : HIGH);

    // 3. Telemetrie-Bytes sammeln
    readIncomingTelemetry();

    // 4. Paket verarbeiten und absenden
    dispatchTelemetryPacket(connected);
}



    void loop_o() {
    esp_task_wdt_reset();

    // --- 1. BUTTON-LOGIK (RESET) ---
    bool buttonPressed = (digitalRead(TRIGGER_SWITCH_PIN) == LOW);
    if (buttonPressed && !buttonActive) {
        buttonPressTime = millis();
        buttonActive = true;
    } 
    if (!buttonPressed && buttonActive) {
        if (millis() - buttonPressTime > 3000) resetPairedDevices();
        buttonActive = false;
    }

    bool connected = isBleMode ? deviceConnected : SerialBT.hasClient();
    digitalWrite(STATUS_LED, connected ? LOW : HIGH);

    // --- 2. VARIABLEN FÜR DEN PROTOKOLL-WÄCHTER ---
    static uint8_t packetBuffer[128]; // Unbedingt mit [128] Klammern stehen lassen!
    static int pIdx = 0;
    static int logicalCount = 0;
    static bool isEscaped = false;
    static uint8_t mode = 0;           // 0: Suche, 1: MPM, 2: S.Port
    static uint8_t lastByte = 0;
    static unsigned long last999Time = 0; 
    static bool lsbMatched = false; 

    // Die 3 neuen Variablen:
    static int expectedPayloadLength = 10; 
    static bool paketFertig = false;       
    static uint8_t convertedBuffer[10];    

    bool conversionActive = (millis() - last999Time < 2000); 

    // --- 3. DATEN-VERARBEITUNG (Einlesen vom Fließband) ---
    // Schleife stoppt kurz, sobald ein Paket komplett im Puffer liegt
    while (Serial2.available() && !paketFertig) {
        uint8_t c = Serial2.read();

        // START-ERKENNUNG (Suchmodus)
        if (mode == 0) {
            if (lastByte == 0x4D && c == 0x50) {
                mode = 1; pIdx = 0; logicalCount = 0;
                expectedPayloadLength = 10; // Reset auf Standard-Fallback
                packetBuffer[pIdx++] = 0x4D;
                packetBuffer[pIdx++] = 0x50;
            } else if (c == 0x7E) {
                mode = 2; pIdx = 0; logicalCount = 0;
                packetBuffer[pIdx++] = 0x7E;
                isEscaped = false;
            }
            lastByte = c;
            continue;
        }

        // DATEN SAMMELN IM PUFFER
        if (pIdx < 120) packetBuffer[pIdx++] = c;

        // MODUS 1: MPM Modus (Dynamische Länge)
        if (mode == 1) {
            logicalCount++;
            
            // Das 4. Gesamt-Byte auslesen (enthält die codierte Länge)
            if (logicalCount == 2) {
                expectedPayloadLength = c; 
            }

            // Erkennung Sensor (ID 0x0450)
            if (logicalCount == 5) {
                   lsbMatched = (c == 0x50); 
            }
            if (logicalCount == 6) {
                if (lsbMatched && c == 0x04) {
                    last999Time = millis(); 
                }
                lsbMatched = false; 
            }

            // Wenn die gemeldete Länge erreicht ist, Schleife stoppen
            if (logicalCount >= (expectedPayloadLength + 1)) {
                paketFertig = true; 
            }
        } 
        // MODUS 2: Natives S.Port Modus (Feste Länge)
        else if (mode == 2) { 
            if (c == 0x7D) {
                isEscaped = true; 
            } else {
                if (!isEscaped) logicalCount++;
                else isEscaped = false;
            }
            if (logicalCount >= 9) {
                paketFertig = true; 
            }
        }
        
        if (pIdx >= 120) { mode = 0; pIdx = 0; logicalCount = 0; paketFertig = false; }
        lastByte = c;
    }

       // --- 4. SICHERER SENDER-BLOCK MIT PROTOKOLL-FILTER ---
    if (paketFertig) {
        uint8_t* debugBuffer = packetBuffer;
        int debugSize = pIdx;
        static uint8_t convertedBuffer[10]; 

        // 1. Ist es ein Standard MPM-Telemetrieframe laut Log? (Länge == 9)
        bool isStandardTelemetryFrame = (mode == 1 && expectedPayloadLength == 9);

        // 2. UNIVERSELLER FILTER: Wir blockieren NUR reine Modul-Statustexte (Typ 0x01).
        // Echte Telemetriedaten (Typ 0x02) wie lange GPS-Pakete dürfen NIEMALS blockiert werden!
        bool isModulStatusText = (mode == 1 && packetBuffer[2] == 0x01);

        if (connected && !isModulStatusText) { // <--- Filter greift nur bei Status-Texten
            if (isStandardTelemetryFrame && conversionActive) {
                // UMSETZUNG: Baue das echte FrSky 10-Byte Paket für den LilyGo
                convertedBuffer[0] = 0x7E; 
                for(int i = 0; i < 8; i++) {
                    convertedBuffer[i+1] = packetBuffer[i+4];
                }
                convertedBuffer[9] = packetBuffer[pIdx-1]; // Echte Checksumme mitnehmen

                debugBuffer = convertedBuffer;
                debugSize = 10;

                if (isBleMode) {
                    pTxCharacteristic->setValue(convertedBuffer, 10);
                    pTxCharacteristic->notify();
                } else {
                    SerialBT.write(convertedBuffer, 10);
                }
            } else {
                // 1:1 DURCHREICHEN (Für alle langen GPS-Pakete oder sonstige nützliche Daten)
                if (isBleMode) {
                    pTxCharacteristic->setValue(packetBuffer, pIdx);
                    pTxCharacteristic->notify();
                } else {
                    SerialBT.write(packetBuffer, pIdx);
                }
            }
        }
        
        // --- TABELLARISCHES DEBUGGING ---
        if (debug_telemetry) {
        if (mode == 1) {
            Serial.print("MPM\tLen: ");
            Serial.print(expectedPayloadLength);
        } else if (mode == 2) {
            Serial.print("S.PORT\tLen: 9");
        }
        
        Serial.print("\tSentBytes: ");
        Serial.print(isModulStatusText ? 0 : debugSize); 
        Serial.print("\tStatus: ");
        
        if (!connected) {
            Serial.print("NO_BT_CLIENT ");
        } else if (isModulStatusText) {
            Serial.print("BLOCKED_STAT "); // Klar markiert als blockierter Status
        } else if (isStandardTelemetryFrame && conversionActive) {
            Serial.print("UMSETZ_10BYTE");
        } else {
            Serial.print("1:1_MIRROR   ");
        }
        
        Serial.print("\tTX_Data: ");
        if (isModulStatusText) {
            Serial.print("[Modul-Status unterdrueckt]");
        } else {
            for (int i = 0; i < debugSize; i++) {
                if (debugBuffer[i] < 0x10) Serial.print("0"); 
                Serial.print(debugBuffer[i], HEX);
                Serial.print(" ");
            }
        }
        Serial.println();
        }
        
        // --- RADIKALER RESET FÜR DAS NÄCHSTE PAKET ---
        mode = 0; 
        pIdx = 0;
        logicalCount = 0;
        isEscaped = false;
        paketFertig = false; 
    }

    }
