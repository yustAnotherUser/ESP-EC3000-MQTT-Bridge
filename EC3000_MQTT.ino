#include <SPI.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <U8g2lib.h>

#include "wifidata.h"
#include "whitelist.h"
// #include "pins.h"
#include "fonts.h"

// benutzte Pins auf ESP32-C3 Board mit 0.42" OLED
// (nur zur Übersicht; werden später im Code initialisiert)
//
// PIN 5 ist SDA; I2C; Display (und evtl. sonstige I2C Sensoren)
// PIN 6 ist SCL; I2C; Display (und evtl. sonstige I2C Sensoren)
// PIN 8 ist die LED auf der Platine (blau auf den günstigen AliExpress Boards und an PIN 8; es gibt wohl auch welche mit einer RGB LED an PIN 2?)
// PIN 9 ist der "BO0" Knopf

// // Pin Definitionen RFM69 an ESP32-S2
// #define RFM69_CS   34  // NSS (Chip Select)
// #define RFM69_INT  5   // DIO0 (Interrupt)
// #define RFM69_MOSI 35  // SPI MOSI
// #define RFM69_MISO 37  // SPI MISO
// #define RFM69_SCK  36  // SPI SCK
// #define RFM69_RST  18  // RESET pin

// Pin Definitionen RFM69 (frei wählbar) an ESP32-C3 mit 0,42" OLED
#define RFM69_CS 7
#define RFM69_MOSI 3
#define RFM69_MISO 10
#define RFM69_SCK 4
#define RFM69_RST 2

// Hersteller Pin Definitionen am günstigen ESP32-C3 Board mit verbautem 0.42" 72x40 OLED
#define OLED_SCL 6  // https://www.aliexpress.com/item/1005008125231916.html
#define OLED_SDA 5  // oder suche "ESP32-C3 oled 0 42"

#define LED_PIN 8     // (fest auf der Platine verdrahtet)
#define BUTTON_PIN 9  // (fest auf der Platine verdrahtet)

#define LED_VALID_DURATION 3      // 3ms for valid packets
#define LED_INVALID_DURATION 100  // 100ms for invalid packets
#define LED_VALID_BRIGHTNESS 250  // ~2% brightness for valid (0 = brightest, 255 = off)
#define LED_INVALID_BRIGHTNESS 0  // Full brightness for invalid
#define LED_PWM_FREQ 5000         // 5kHz PWM frequency
#define LED_PWM_RESOLUTION 8      // 8-bit resolution (0-255)

// RFM69 Definitionen
#define FREQUENCY_KHZ 868300
#define DATA_RATE 20000
#define PAYLOAD_SIZE 47
#define FRAME_LENGTH 38

// Knopf Behandlung
#define FONT_PRESS_DURATION 500   // 500ms for font cycle
#define LONG_PRESS_DURATION 2000  // 2s to start auto-cycling through all IDs
#define FONT_POPUP_DURATION 1250  // 1250ms popup with the current font array number

#define AUTO_CYCLE_INTERVAL 5000  // Anzeigedauer pro ID

// only used for LAYOUT 2 !
#define LOADING_BAR_WIDTH 2  // Vertical bar width
#define ID_GAP 1             // Gap between bar and ID

// Ab hier nichts mehr ändern außer du bist dir absolut sicher
// Global variables for LED timing
unsigned long ledValidOnTime = 0;
unsigned long ledInvalidOnTime = 0;
bool ledValidIsOn = false;
bool ledInvalidIsOn = false;

// Fontcounter popup
unsigned long fontPopupStart = 0;  // Popup timer
bool showFontPopup = false;        // Popup state

// Auto-cycle
bool autoCycle = true;            // Auto-cycle state
unsigned long lastCycleTime = 0;  // Auto-cycle timer

// Display initialization
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, 6, 5);
// Display buffer dimensions
const unsigned int BufferWidth = 132;
const unsigned int BufferHeight = 64;
const unsigned int ScreenWidth = 72;
const unsigned int ScreenHeight = 40;
const unsigned int xOffset = (BufferWidth - ScreenWidth) / 2;
const unsigned int yOffset = (BufferHeight - ScreenHeight) / 2;

// Define constants and variables
// const int buttonPin = 9;                  // GPIO pin for the button
// const unsigned long debounceDelay = 250;  // Debounce delay in milliseconds

// Logging toggle
bool logOnlyFailed = true;  // true = log only failed, false = log all

// Payload buffer
uint8_t m_payload[64];
uint8_t m_payloadPointer = 0;
bool m_payloadReady = false;

// Frame structure
struct Frame {
  uint16_t ID;
  uint32_t TotalSeconds;
  uint32_t OnSeconds;
  double Consumption;
  float Power;
  float MaximumPower;
  uint16_t NumberOfResets;
  bool IsOn;
  uint8_t Reception;
  uint16_t CRC;
};

// Reset and Consumption tracking
// designers note: make this far bigger as you have real IDs. like 10-20 times. you never know how bad your packet gets messed up in transmission and how many false IDs you may receive in a very short amount of time which would clog up the tracking if this is way to low...
// keep in mind IDs that are not re-received within 132 seconds will be removed from the list anyways (as they are most likely false anyways) to keep it from the otherwise unevitable long time clog up.
#define MAX_IDS 80
struct Tracker {
  uint16_t ID;
  uint16_t LastResets;
  double LastConsumption;
  bool Initialized;
  unsigned long LastSeen;  // Timestamp in milliseconds
} trackers[MAX_IDS] = { 0 };

// WiFi and MQTT clients
WiFiClient espClient;
PubSubClient client(espClient);

// Display page management
struct DisplayPage {
  uint16_t ID;
  float Power;
  double Consumption;
  unsigned long lastUpdate;
  bool active;
};

DisplayPage displayPages[MAX_IDS];
uint8_t currentPage = 0;
uint8_t totalPages = 0;

// Fontswitching
uint8_t currentFontIndex = 0;        // Start with first font
unsigned long buttonPressStart = 0;  // Track press start time
bool buttonIsPressed = false;        // Track press state

// Variables to track button state and timing
bool lastButtonState = HIGH;         // Previous state of the button (HIGH due to INPUT_PULLUP)
bool currentButtonState = HIGH;      // Current state of the button
unsigned long lastDebounceTime = 0;  // Last time the button state changed

void debugLog(const String& msg) {
  // TODO: Make it actually worship the login bool more as it currently is a bit messy in the console.... :/
  Serial.println(msg);
  if (client.connected()) {
    client.publish("EC3000/debug", msg.c_str(), true);
  }
}

// RFM69 Register Magic
void WriteReg(uint8_t addr, uint8_t value) {
  digitalWrite(RFM69_CS, LOW);
  SPI.transfer(addr | 0x80);
  SPI.transfer(value);
  digitalWrite(RFM69_CS, HIGH);
}

uint8_t ReadReg(uint8_t addr) {
  digitalWrite(RFM69_CS, LOW);
  SPI.transfer(addr & 0x7F);
  uint8_t result = SPI.transfer(0);
  digitalWrite(RFM69_CS, HIGH);
  return result;
}

uint8_t GetByteFromFifo() {
  return ReadReg(0x00);
}

// EC3000 decoding functions
void DescramblePayload(byte* payload) {
  byte ctr = PAYLOAD_SIZE;
  uint8_t inpbyte, outbyte = 0;
  uint32_t scramshift = 0xF185D3AC;
  while (ctr--) {
    inpbyte = *payload;
    for (byte bit = 0; bit < 8; ++bit) {
      byte ibit = (inpbyte & 0x80) >> 7;
      byte obit = ibit ^ (Count1bits(scramshift & 0x31801) & 0x01);
      scramshift = scramshift << 1 | ibit;
      inpbyte <<= 1;
      outbyte = outbyte << 1 | obit;
    }
    *payload++ = outbyte ^ 0xFF;
  }
}

uint16_t UpdateCRC(uint16_t crc, byte data) {
  data ^= crc & 0xFF;
  data ^= data << 4;
  return ((((uint16_t)data << 8) | (crc >> 8)) ^ (uint8_t)(data >> 4) ^ ((uint16_t)data << 3));
}

byte Count1bits(uint32_t v) {
  byte c;
  for (c = 0; v; c++) {
    v &= v - 1;
  }
  return c;
}

uint16_t ShiftReverse(byte* payload) {
  byte rblen = 47;
  uint16_t i, ec3klen;
  uint16_t crc;

  ec3klen = rblen - 1;
  Del0BitsAndRevBits(payload + 1, ec3klen);
  crc = 0xFFFF;
  if (ec3klen >= FRAME_LENGTH) {
    for (i = 0; i < FRAME_LENGTH; ++i) {
      crc = UpdateCRC(crc, payload[1 + i]);
    }
  }
  ShiftLeft(payload, rblen, 4 + 8);
  return crc;
}

void ShiftLeft(byte* payload, byte blen, byte shift) {
  uint8_t offs, bits, slen, i;
  uint16_t wbuf;

  if (shift == 0) return;
  offs = shift / 8;
  bits = 8 - shift % 8;
  slen = blen - offs - 1;
  wbuf = payload[offs];
  for (i = 0; i < slen; ++i) {
    wbuf = wbuf << 8 | payload[i + offs + 1];
    payload[i] = wbuf >> bits;
  }
  payload[slen] = wbuf << (uint8_t)(8 - bits);
}

void Del0BitsAndRevBits(byte* payload, byte blen) {
  uint8_t sval, dval, bit;
  uint8_t si, sbi, di, dbi, n1bits;

  di = dval = dbi = n1bits = 0;
  for (si = 0; si < blen; ++si) {
    sval = payload[si];
    for (sbi = 0; sbi < 8; ++sbi) {
      bit = sval & 0x80;
      sval <<= 1;
      if (n1bits >= 5 && bit == 0) {
        n1bits = 0;
        continue;
      }
      if (bit) n1bits++;
      else n1bits = 0;
      dval = dval >> 1 | bit;
      dbi++;
      if (dbi == 8) {
        payload[di++] = dval;
        dval = dbi = 0;
      }
    }
  }
  if (dbi) payload[di] = dval >> (uint8_t)(8 - dbi);
}

void DecodeFrame(byte* payload, struct Frame* frame) {
  DescramblePayload(payload);
  frame->CRC = ShiftReverse(payload);

  frame->ID = (payload[0] << 8) | payload[1];
  frame->TotalSeconds = (uint32_t)payload[29] << 20 | (uint32_t)payload[30] << 12 | (uint32_t)payload[2] << 8 | (uint32_t)payload[3];
  frame->OnSeconds = (uint32_t)payload[35] << 20 | (uint32_t)payload[36] << 12 | (uint32_t)payload[6] << 8 | (uint32_t)payload[7];

  uint64_t cons = 0;
  cons |= payload[14];
  cons |= (uint16_t)payload[13] << 8;
  cons |= (uint16_t)payload[12] << 16;
  cons |= (uint32_t)(payload[11] & 0x0F) << 24;
  cons |= (uint64_t)payload[34] << 28;
  cons |= (uint64_t)payload[33] << 36;
  frame->Consumption = cons / 3600000.0;

  frame->Power = ((uint16_t)payload[15] << 8 | payload[16]) / 10.0;
  frame->MaximumPower = ((uint16_t)payload[17] << 8 | payload[18]) / 10.0;
  frame->NumberOfResets = (payload[36] << 4) | (payload[37] >> 4);
  frame->IsOn = payload[37] & 0x08;
  frame->Reception = 0;
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Connecting to MQTT...");
    String clientId = "ESP-EC3000-";
    clientId += String(random(0xffff), HEX);
    if (client.connect(clientId.c_str(), mqtt_user, mqtt_password)) {
      client.publish("EC3000/debug", "EC3000 MQTT Bridge connected!");
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" retrying in 5 seconds");
      delay(5000);
    }
  }
}

void printTimeBreakdown(uint32_t seconds) {
  uint32_t minutes = seconds / 60;
  uint32_t hours = minutes / 60;
  uint32_t days = hours / 24;
  uint32_t years = days / 365;

  minutes %= 60;
  hours %= 24;
  days %= 365;

  Serial.print(" (");
  if (years) Serial.print(years), Serial.print("y ");
  if (days || years) Serial.print(days), Serial.print("d ");
  Serial.print(hours), Serial.print("h ");
  Serial.print(minutes), Serial.print("m)");
}

bool checkResets(uint16_t id, uint16_t resets, uint16_t* lastResets) {
  // TODO: Does not work reliably AND WILL CAUSE MISSING DATA AT SOME POINT FOREVER because of wierd jump the EC3000 makes sometimes.
  // !! Make sure the Sanity check later is a comment (//) so it will not be used !!
  unsigned long now = millis();
  for (int i = 0; i < MAX_IDS; i++) {
    if (trackers[i].Initialized && trackers[i].ID == id) {
      *lastResets = trackers[i].LastResets;
      if (resets == trackers[i].LastResets || resets == trackers[i].LastResets + 1) {
        trackers[i].LastResets = resets;
        trackers[i].LastSeen = now;
        debugLog("Resets OK: ID=0x" + String(id, HEX) + ", Resets=" + String(resets));
        return true;
      }
      debugLog("Resets Failed: ID=0x" + String(id, HEX) + ", Expected=" + String(*lastResets + 1) + ", Got=" + String(resets));
      trackers[i].LastSeen = now;
      return false;
    }
    if (!trackers[i].Initialized) {
      trackers[i].ID = id;
      trackers[i].LastResets = resets;
      trackers[i].LastConsumption = 0.0;
      trackers[i].Initialized = true;
      trackers[i].LastSeen = now;
      *lastResets = resets;
      debugLog("Resets Init: ID=0x" + String(id, HEX) + ", Resets=" + String(resets));
      return true;
    }
  }
  debugLog("Too many IDs! Increase MAX_IDS.");
  return false;
}

bool isWhitelisted(String id) {
  for (int i = 0; i < whitelistSize; i++) {
    if (id.equalsIgnoreCase(whitelist[i])) {
      return true;
    }
  }
  return false;
}

bool checkConsumption(uint16_t id, double consumption, double* lastConsumption) {
  // Rewritten - version 2
  unsigned long now = millis();
  for (int i = 0; i < MAX_IDS; i++) {
    if (trackers[i].Initialized && trackers[i].ID == id) {
      *lastConsumption = trackers[i].LastConsumption;
      double delta = consumption - *lastConsumption;
      if (!trackers[i].LastConsumption) {
        trackers[i].LastConsumption = consumption;
        trackers[i].LastSeen = now;
        debugLog("Cons Init: ID=0x" + String(id, HEX) + ", Cons=" + String(consumption, 3));
        return true;
      }
      if (delta == 0) {  // Nur wenn Delta gleich Null ist, ist es "OK"
        debugLog("Cons Same: ID=0x" + String(id, HEX) + ", Last=" + String(*lastConsumption, 3) + ", New=" + String(consumption, 3) + ", Delta=" + String(delta, 3));
        trackers[i].LastSeen = now;
        trackers[i].LastConsumption = consumption;  // Aktualisiere auch den letzten Verbrauch
        return true;
      }
      if (delta < 0) {
        debugLog("Cons Invalid: ID=0x" + String(id, HEX) + ", Last=" + String(*lastConsumption, 3) + ", New=" + String(consumption, 3) + ", Delta=" + String(delta, 3));
        trackers[i].LastSeen = now;
        return false;  // Negative Änderung ist ungültig
      }
      if (delta > 0.025) {
        debugLog("Cons Failed: ID=0x" + String(id, HEX) + ", Last=" + String(*lastConsumption, 3) + ", New=" + String(consumption, 3) + ", Delta=" + String(delta, 3));
        trackers[i].LastSeen = now;
        return false;
      }
      trackers[i].LastConsumption = consumption;
      trackers[i].LastSeen = now;
      debugLog("Cons OK: ID=0x" + String(id, HEX) + ", Cons=" + String(consumption, 3));
      return true;
    }
  }
  for (int i = 0; i < MAX_IDS; i++) {
    if (!trackers[i].Initialized) {
      trackers[i].ID = id;
      trackers[i].LastResets = 0;
      trackers[i].LastConsumption = consumption;
      trackers[i].Initialized = true;
      trackers[i].LastSeen = now;
      *lastConsumption = consumption;
      debugLog("Cons Fallback Init: ID=0x" + String(id, HEX) + ", Cons=" + String(consumption, 3));
      return true;
    }
  }
  return false;
}

void updateDisplayPage(uint16_t id, float power, double consumption) {
  unsigned long now = millis();

  // Find existing page or create new
  for (int i = 0; i < MAX_IDS; i++) {
    if (displayPages[i].active && displayPages[i].ID == id) {
      displayPages[i].Power = power;
      displayPages[i].Consumption = consumption;
      displayPages[i].lastUpdate = now;
      return;
    }
  }

  // Create new page if space available
  for (int i = 0; i < MAX_IDS; i++) {
    if (!displayPages[i].active) {
      displayPages[i].ID = id;
      displayPages[i].Power = power;
      displayPages[i].Consumption = consumption;
      displayPages[i].lastUpdate = now;
      displayPages[i].active = true;
      totalPages++;
      return;
    }
  }
}

// DISPLAY LAYOUTS - USE ONLY ONE !
// (check fonts.h for the Website with the names and a preview of the fonts.)
//
// // LAYOUT 1 - 3 Lines (ID, "Loading Bar", Power, Consumption)
// void drawDisplay() {
//   u8g2.clearBuffer();
//   if (showFontPopup && millis() - fontPopupStart < FONT_POPUP_DURATION) {
//     // Popup: show font index only
//     u8g2.setFont(u8g2_font_fub30_tn);  // Large numeric font (~30px)
//     char popup[3];
//     snprintf(popup, sizeof(popup), "%d", currentFontIndex);
//     int width = u8g2.getStrWidth(popup);
//     int height = u8g2.getFontAscent() - u8g2.getFontDescent();
//     int x = xOffset + (ScreenWidth - width) / 2;    // Center horizontally
//     int y = yOffset + (ScreenHeight - height) / 2;  // Center vertically
//     u8g2.setCursor(x, y);
//     u8g2.print(popup);
//     Serial.println("Drawing popup: " + String(popup));
//   } else {
//     // Normal display
//     showFontPopup = false;  // Hide popup after 3s
//     u8g2.setFont(fonts[currentFontIndex]);
//     if (totalPages == 0) {
//       u8g2.setCursor(xOffset, yOffset);
//       u8g2.print("...");  // Show that we are still Waiting for the very first packets ... only seen at system startup until the first packet comes in
//     } else {
//       // Find the actual page index
//       int displayIndex = 0;
//       for (int i = 0, count = 0; i < MAX_IDS; i++) {
//         if (displayPages[i].active) {
//           if (count == currentPage) {
//             displayIndex = i;
//             break;
//           }
//           count++;
//         }
//       }
//       // Calculate loading bar progress (5 seconds total)
//       unsigned long elapsed = millis() - displayPages[displayIndex].lastUpdate;
//       int barWidth = (elapsed * ScreenWidth) / 5000;
//       if (barWidth > ScreenWidth) {
//         barWidth = ScreenWidth;
//       }

//       // Draw content with current font
//       u8g2.setFont(fonts[currentFontIndex]);
//       char buffer[20];

//       // ID
//       snprintf(buffer, sizeof(buffer), "%04X ID", displayPages[displayIndex].ID);
//       u8g2.setCursor(xOffset, yOffset);
//       u8g2.print(buffer);

//       // Loading bar
//       u8g2.drawHLine(xOffset, yOffset + 13, barWidth);

//       // Power
//       snprintf(buffer, sizeof(buffer), "%.1f W", displayPages[displayIndex].Power);
//       u8g2.setCursor(xOffset, yOffset + 14);
//       u8g2.print(buffer);

//       // Consumption
//       snprintf(buffer, sizeof(buffer), "%.3f KWH", displayPages[displayIndex].Consumption);
//       u8g2.setCursor(xOffset, yOffset + 28);
//       u8g2.print(buffer);
//     }
//   }
//   u8g2.sendBuffer();
// }

// LAYOUT 2 (Loading Bar on the very left vertically, ID besides it also vertically, big remaining space for the last two values; 2 lines on a 40px height display means therefore fonts up to in 20 pixels height)
void drawDisplay() {
  u8g2.clearBuffer();

  if (showFontPopup && millis() - fontPopupStart < FONT_POPUP_DURATION) {
    u8g2.setFont(u8g2_font_fub30_tn);
    char popup[3];
    snprintf(popup, sizeof(popup), "%d", currentFontIndex);
    int width = u8g2.getStrWidth(popup);
    int height = u8g2.getFontAscent() - u8g2.getFontDescent();
    int x = xOffset + (ScreenWidth - width) / 2;
    int y = yOffset + (ScreenHeight - height) / 2;
    u8g2.setCursor(x, y);
    u8g2.print(popup);
    Serial.println("Drawing popup: " + String(popup));
  } else {
    showFontPopup = false;

    if (totalPages == 0) {
      u8g2.setFont(fonts[currentFontIndex]);
      u8g2.setFontDirection(0);
      u8g2.setCursor(xOffset, yOffset);
      u8g2.print("...");
    } else {
      // Find the actual page index
      int displayIndex = 0;
      for (int i = 0, count = 0; i < MAX_IDS; i++) {
        if (displayPages[i].active) {
          if (count == currentPage) {
            displayIndex = i;
            break;
          }
          count++;
        }
      }

      // Vertical loading bar (column 0, top-to-bottom)
      unsigned long elapsed = millis() - displayPages[displayIndex].lastUpdate;
      int barHeight = (elapsed * ScreenHeight) / 5000;
      if (barHeight > ScreenHeight) {
        barHeight = ScreenHeight;
      }
      u8g2.drawBox(xOffset, yOffset, LOADING_BAR_WIDTH, barHeight);

      // Vertical ID (fixed font, rotated)
      // u8g2.setFont(u8g2_font_crox4tb_tr); // 13px height
      u8g2.setFont(u8g2_font_10x20_tf);  // 13px height
      u8g2.setFontDirection(3);          // 90° counterclockwise
      char idBuffer[5];
      snprintf(idBuffer, sizeof(idBuffer), "%04X", displayPages[displayIndex].ID);
      u8g2.setCursor(xOffset + LOADING_BAR_WIDTH + ID_GAP, yOffset + 40);  // Adjust for text length
      u8g2.print(idBuffer);

      // Power and Consumption (swapped font, horizontal)
      u8g2.setFont(fonts[currentFontIndex]);
      u8g2.setFontDirection(0);  // Back to horizontal
      char buffer[20];

      // Power
      snprintf(buffer, sizeof(buffer), "%.1f W", displayPages[displayIndex].Power);
      u8g2.setCursor(xOffset + LOADING_BAR_WIDTH + ID_GAP + 20, yOffset + 0);
      u8g2.print(buffer);

      // Consumption
      snprintf(buffer, sizeof(buffer), "%.3f kwh", displayPages[displayIndex].Consumption);
      u8g2.setCursor(xOffset + LOADING_BAR_WIDTH + ID_GAP + 20, yOffset + 20);
      u8g2.print(buffer);
    }
  }

  u8g2.sendBuffer();
  // Serial.println("Drawing page: " + String(totalPages ? currentPage : -1));
}


void handleButton() {
  int reading = digitalRead(BUTTON_PIN);
  // Serial.print("Button reading: ");
  // Serial.println(reading);

  if (reading == LOW && lastButtonState == HIGH) {
    // Button just pressed
    buttonPressStart = millis();
    buttonIsPressed = true;
    Serial.println("Button pressed - starting timer");
  } else if (reading == HIGH && lastButtonState == LOW) {
    // Button released
    unsigned long pressDuration = millis() - buttonPressStart;
    buttonIsPressed = false;

    if (autoCycle) {
      // Any press during auto-cycle stops it
      autoCycle = false;
      Serial.println("Button press - Stopping auto-cycle");
      drawDisplay();
    } else if (pressDuration < FONT_PRESS_DURATION) {
      // Short press: cycle page
      if (totalPages > 0) {
        currentPage = (currentPage + 1) % totalPages;
        Serial.println("Short press - Cycling to page: " + String(currentPage));
        drawDisplay();
      } else {
        Serial.println("Short press - No pages to cycle");
      }
    } else if (pressDuration < LONG_PRESS_DURATION) {
      // Medium press: cycle font
      currentFontIndex = (currentFontIndex + 1) % numFonts;
      fontPopupStart = millis();
      showFontPopup = true;
      Serial.println("Medium press - Cycling to font index: " + String(currentFontIndex));
      drawDisplay();
    } else {
      // Long press: start auto-cycle
      if (totalPages > 0) {
        autoCycle = true;
        lastCycleTime = millis();
        currentPage = 0;  // Start from first page
        Serial.println("Long press - Starting auto-cycle");
        drawDisplay();
      } else {
        Serial.println("Long press - No pages to auto-cycle");
      }
    }
    Serial.println("Button released - duration: " + String(pressDuration) + "ms");
  }

  lastButtonState = reading;
}

void cleanStaleIDs() {
  unsigned long now = millis();
  for (int i = 0; i < MAX_IDS; i++) {
    if (trackers[i].Initialized && (now - trackers[i].LastSeen > 66000)) {
      debugLog("Removing stale ID: " + String(trackers[i].ID, HEX));
      trackers[i].Initialized = false;
      // Also remove from display pages
      for (int j = 0; j < MAX_IDS; j++) {
        if (displayPages[j].active && displayPages[j].ID == trackers[i].ID) {
          displayPages[j].active = false;
          totalPages--;
          if (currentPage >= totalPages && totalPages > 0) {
            currentPage = totalPages - 1;
          }
          break;
        }
      }
    }
  }
}

void printFrame(struct Frame* frame, float rssi) {
  Serial.printf("ID: %08lX | RSSI: %6.1f dBm | Total: %08lu | On: %08lu | Cons: %06.3f kWh | Power: %05.1f W | Max: %05.1f W | Resets: %04u | On?: %s | CRC: 0x%04X\n",
                frame->ID,
                rssi,
                frame->TotalSeconds,
                frame->OnSeconds,
                frame->Consumption,
                frame->Power,
                frame->MaximumPower,
                frame->NumberOfResets,
                frame->IsOn ? "Yes" : "No",
                frame->CRC);
}

void setup() {
  // Initialize display
  u8g2.begin();
  // u8g2.setFont(u8g2_font_8bitclassic_tr); // Set a readable font
  // u8g2.setFont(u8g2_font_missingplanet_tr);
  // u8g2.setFont(u8g2_font_questgiver_tr);
  u8g2.setFont(u8g2_font_crox1tb_tr);
  // u8g2.setFont(u8g2_font_lastapprenticethin_tr);
  // u8g2.setFont(u8g2_font_eckpixel_tr);
  // u8g2.setFont(u8g2_font_tenthinnerguys_tr);
  // u8g2.setFont(u8g2_font_NokiaSmallBold_tr);
  u8g2.setFontRefHeightExtendedText();
  u8g2.setDrawColor(1);
  u8g2.setBusClock(25000000);
  u8g2.setFontPosTop();
  u8g2.setContrast(255);
  // u8g2.setFontDirection(0);
  u8g2.clearBuffer();
  u8g2.setCursor(xOffset, yOffset);
  u8g2.print("EC3000 MQTT Bridge");
  u8g2.sendBuffer();

  // Configure LED PWM with new ESP-IDE > v3 API (in short: it's now a single ledcAttach instead of ledcSetup and ledcAttachPin or whatever it was called before and the ledcWrite now uses the pin and the value - simple)
  pinMode(LED_PIN, OUTPUT);
  ledcAttach(LED_PIN, LED_PWM_FREQ, LED_PWM_RESOLUTION);  // Auto-assigns channel
  ledcWrite(LED_PIN, 255);                                // Off for active-low

  Serial.begin(115200);
  // while (!Serial) delay(10); // umstritten
  delay(2000);  // einfacher und zuverlässiger

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
  u8g2.setCursor(xOffset, yOffset + 14);
  u8g2.print("WLAN verbunden!");
  u8g2.sendBuffer();

  client.setServer(mqtt_server, mqtt_port);
  reconnect();
  
  // Reset RFM69 ... if you don't have the cable connected to the RFM69 RESET pin it doesn't matter because it works anyways
  pinMode(RFM69_CS, OUTPUT);
  digitalWrite(RFM69_CS, HIGH);

  pinMode(RFM69_RST, OUTPUT);
  digitalWrite(RFM69_RST, HIGH);
  delay(10);
  digitalWrite(RFM69_RST, LOW);
  delay(10);

  // Add button pin setup
  pinMode(BUTTON_PIN, INPUT_PULLUP);  // Configure Button Pin with internal pull-up resistor

  // Initialize display pages
  for (int i = 0; i < MAX_IDS; i++) {
    displayPages[i].active = false;
  }

  SPI.begin(RFM69_SCK, RFM69_MISO, RFM69_MOSI, RFM69_CS);
  // SPI.setClockDivider(SPI_CLOCK_DIV4); // 4x lower speed
  // SPI.setDataMode(SPI_MODE0); // set the standard mode explicitly
  delay(10);

  // put RFM69 in standby
  WriteReg(0x01, 0x04);
  delay(10);

  // Perform some register writes, read it back and compare the values to confirm we have an RFM69.
  WriteReg(0x38, 0x0A);
  if (ReadReg(0x38) != 0x0A) {
    Serial.println("RFM69 detection failed at step 1!");
    u8g2.setCursor(xOffset, yOffset + 14);
    u8g2.print("No RFM69");
    u8g2.setCursor(xOffset, yOffset + 28);
    u8g2.print("found!!");
    u8g2.sendBuffer();
    while (1)
      ;
  }
  WriteReg(0x38, 0x40);
  if (ReadReg(0x38) != 0x40) {
    Serial.println("RFM69 detection failed at step 2!");
    u8g2.setCursor(xOffset, yOffset + 14);
    u8g2.print("No RFM69");
    u8g2.setCursor(xOffset, yOffset + 28);
    u8g2.print("found!!");
    u8g2.sendBuffer();
    while (1)
      ;
  }
  Serial.println("RFM69 detected!");

  // Set RFM69 Frequency to 868.3 MhZ
  unsigned long f = (((FREQUENCY_KHZ * 1000UL) << 2) / (32000000UL >> 11)) << 6;
  WriteReg(0x07, f >> 16);
  WriteReg(0x08, f >> 8);
  WriteReg(0x09, f);

  // Set RFM69 Datarate to 20000 baud (20kbps)
  uint16_t r = ((32000000UL + (DATA_RATE / 2)) / DATA_RATE);
  WriteReg(0x03, r >> 8);
  WriteReg(0x04, r & 0xFF);

  // Set EC3000 Preamble Bytes and Modulation and so on...
  WriteReg(0x02, 0x00);
  WriteReg(0x05, 0x01);
  WriteReg(0x06, 0x48);
  WriteReg(0x11, 0x9F);
  WriteReg(0x13, 0x0F);
  WriteReg(0x18, 0x80);
  WriteReg(0x19, 0x42);
  WriteReg(0x28, 0x10);
  WriteReg(0x29, 220);
  WriteReg(0x2E, 0xA0);
  WriteReg(0x2F, 0x13);
  WriteReg(0x30, 0xF1);
  WriteReg(0x31, 0x85);
  WriteReg(0x32, 0xD3);
  WriteReg(0x33, 0xAC);
  WriteReg(0x37, 0x08);
  WriteReg(0x38, 64);
  WriteReg(0x3C, 0x8F);
  WriteReg(0x3D, 0x12);
  WriteReg(0x6F, 0x30);

  // put RFM69 finally in listening mode!
  WriteReg(0x01, (ReadReg(0x01) & 0xE3) | 0x10);

  Serial.println("RFM69 initialized successfully!");
  u8g2.setCursor(xOffset, yOffset + 28);
  u8g2.print("Found RFM69");
  u8g2.sendBuffer();

  // Whitelist publish für HomeAssistant?
  // for (int i = 0; i < whitelistSize; i++) {
  //   uint16_t id = strtol(whitelist[i], nullptr, 16);  // Umwandlung von Hex-String zu uint16_t
  //   publishDiscoveryMessages(id);
  // }

  //Read back the register values and show the just set Frequency and Baudrate (this is/was for debug)
  uint8_t reg07 = ReadReg(0x07);
  uint8_t reg08 = ReadReg(0x08);
  uint8_t reg09 = ReadReg(0x09);
  uint32_t freqVal = ((uint32_t)reg07 << 16) | (reg08 << 8) | reg09;
  float freqMHz = (freqVal * 32000000.0) / (1UL << 19) / 1000000.0;

  uint8_t reg03 = ReadReg(0x03);
  uint8_t reg04 = ReadReg(0x04);
  uint16_t bitrateVal = (reg03 << 8) | reg04;
  float bitrate = 32000000.0 / bitrateVal / 1000.0;

  Serial.print("Listening at ");
  Serial.print(freqMHz);
  Serial.print(" MHz with ");
  Serial.print(bitrate);
  Serial.println(" kbps...");
  u8g2.setFont(u8g2_font_smart_patrol_nbp_tr);  // Set a readable font
  // u8g2.setFont(u8g2_font_tenfatguys_tr);
  delay(2750);
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  cleanStaleIDs();
  handleButton();

  // Auto-cycle pages
  if (autoCycle && totalPages > 0 && millis() - lastCycleTime >= AUTO_CYCLE_INTERVAL) {
    currentPage = (currentPage + 1) % totalPages;
    lastCycleTime = millis();
    Serial.println("Auto-cycle - Showing page: " + String(currentPage));
    drawDisplay();
  }

  if (ReadReg(0x28) & 0x04) {
    m_payloadPointer = 0;
    for (int i = 0; i < 64; i++) {
      uint8_t bt = GetByteFromFifo();
      m_payload[i] = bt;
      m_payloadPointer++;
    }
    m_payloadReady = true;

    uint8_t ec3k_payload[PAYLOAD_SIZE];
    for (int i = 0; i < PAYLOAD_SIZE; i++) {
      ec3k_payload[i] = m_payload[i];
    }

    struct Frame frame;
    DecodeFrame(ec3k_payload, &frame);

    float rssi = -(ReadReg(0x27) / 2.0);

    // Sanity checks
    bool valid = true;
    String reason = "";

    String idStr = String(frame.ID, HEX);  // wandelt z. B. 0xBF8C → "bf8c"
    idStr.toUpperCase();                   // jetzt: "BF8C"

    if (!isWhitelisted(idStr)) {
      valid = false;
      reason += "wrong ID" + idStr;
      return;
    }

    if (frame.OnSeconds > frame.TotalSeconds) {
      valid = false;
      reason += "OnSeconds > TotalSeconds; ";
    }

    if (!frame.IsOn && frame.Power != 0) {
      valid = false;
      reason += "IsOn=No aber Power>0; ";
    }

    if (frame.Power > 3600) {
      valid = false;
      reason += "Power > 3600W; ";
    }

    if (frame.Power > frame.MaximumPower) {
      valid = false;
      reason += "Power > MaximumPower; ";
    }

    // !!!!   DON'T USE THIS  !!!!
    // TODO
    //
    // uint16_t lastResets;
    // if (!checkResets(frame.ID, frame.NumberOfResets, &lastResets)) {
    //   valid = false;
    //   reason += "Resets not same or +1 (last=" + String(lastResets) + "); ";
    // }

    double lastConsumption;
    if (!checkConsumption(frame.ID, frame.Consumption, &lastConsumption)) {
      valid = false;
      reason += "Consumption invalid (last=" + String(lastConsumption, 3) + "); ";
    }

    // Logging logic
    if (logOnlyFailed) {
      if (!valid) {
        printFrame(&frame, rssi);
        Serial.print("Discarded: ");
        client.publish("EC3000/debug", "Discarded");
        Serial.println(reason);
        char debtopic[32];
        char debpayload[256];
        snprintf(debtopic, sizeof(debtopic), "EC3000/debug");
        snprintf(debpayload, sizeof(debpayload), reason.c_str());
        client.publish(debtopic, debpayload);
        Serial.println();
      }
    } else {
      printFrame(&frame, rssi);
      if (!valid) {
        Serial.print("Discarded: ");
        Serial.println(reason);
      }
      Serial.println();
    }

    // MQTT publish only valid packets
    if (valid) {
      // Dim flash for valid packet
      ledcWrite(LED_PIN, LED_VALID_BRIGHTNESS);
      ledValidOnTime = millis();
      ledValidIsOn = true;
      ledInvalidIsOn = false;  // Cancel any invalid flash
      // Update display page when valid data received
      updateDisplayPage(frame.ID, frame.Power, frame.Consumption);
      drawDisplay();

      char topic[32];
      char payload[256];
      snprintf(topic, sizeof(topic), "EC3000/%04X", frame.ID);
      snprintf(payload, sizeof(payload),
               "{\"TotalSeconds\":%lu,\"OnSeconds\":%lu,\"Consumption\":%.3f,\"Power\":%.1f,\"MaximumPower\":%.1f,\"NumberOfResets\":%u,\"IsOn\":%d,\"CRC\":\"0x%04X\",\"RSSI\":%.2f}",
               frame.TotalSeconds, frame.OnSeconds, frame.Consumption, frame.Power, frame.MaximumPower, frame.NumberOfResets, frame.IsOn, frame.CRC, rssi);
      client.publish(topic, payload);
    } else {
      // Send the bad packet to "EC3000/debug/bad" for possible further logging and analysis
      char topic[32];
      char payload[256];
      snprintf(topic, sizeof(topic), "EC3000/debug/bad");
      snprintf(payload, sizeof(payload),
               "{\"ID\":\"0x%04X\",\"TotalSeconds\":%lu,\"OnSeconds\":%lu,\"Consumption\":%.3f,\"Power\":%.1f,\"MaximumPower\":%.1f,\"NumberOfResets\":%u,\"IsOn\":%d,\"CRC\":\"0x%04X\",\"RSSI\":%.2f}",
               frame.ID, frame.TotalSeconds, frame.OnSeconds, frame.Consumption, frame.Power, frame.MaximumPower, frame.NumberOfResets, frame.IsOn, frame.CRC, rssi);
      client.publish(topic, payload);
      // Full brightness for invalid packet
      ledcWrite(LED_PIN, LED_INVALID_BRIGHTNESS);  // Full brightness
      ledInvalidOnTime = millis();
      ledInvalidIsOn = true;
      ledValidIsOn = false;  // Cancel any valid flash
      Serial.println("Invalid packet received - LED ON (full)");
    }

    WriteReg(0x28, 0x04);
    m_payloadReady = false;
  }

  // Turn LED off after respective durations
  if (ledValidIsOn && millis() - ledValidOnTime >= LED_VALID_DURATION) {
    ledcWrite(LED_PIN, 255);  // Off for active-low
    ledValidIsOn = false;
    // Serial.println("LED OFF (valid)");
  }
  if (ledInvalidIsOn && millis() - ledInvalidOnTime >= LED_INVALID_DURATION) {
    ledcWrite(LED_PIN, 255);  // Off for active-low
    ledInvalidIsOn = false;
    // Serial.println("LED OFF (invalid)");
  }

  // Periodically refresh display to maintain loading bar animation
  static unsigned long lastDisplayUpdate = 0;
  if (millis() - lastDisplayUpdate > 100) {
    drawDisplay();
    lastDisplayUpdate = millis();
  }
}
