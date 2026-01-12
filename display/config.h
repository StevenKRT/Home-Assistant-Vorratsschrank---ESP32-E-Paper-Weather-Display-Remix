/*
 * ========================================
 * Configuration Template
 * ========================================
 *
 * ANLEITUNG:
 * 1. Kopiere diese Datei zu "config.h"
 * 2. Passe die Werte unten an deine Umgebung an
 * 3. config.h wird von .gitignore ignoriert (deine Zugangsdaten bleiben privat)
 */

#ifndef CONFIG_H
#define CONFIG_H

// ========================================
// WiFi Konfiguration
// ========================================
const char* WIFI_SSID = "xxxxxxxxxxxxxx";
const char* WIFI_PASSWORD = "yyyyyyyyyyyyyy";

#define APPOINTMENTS_URL "https://homeassistant.local/local/vorratsschrank/data.json"

// ========================================
// OpenWeatherMap API
// ========================================
// Registriere dich auf: https://openweathermap.org/api
const char* OWM_API_KEY = "zzzzzzzzzzzzzzzzzz";
const char* OWM_CITY = "Hamburg";          // Stadt
const char* OWM_COUNTRY_CODE = "DE";    // Ländercode (AT, DE, CH, etc.)
const char* OWM_LAT = "11111111111111";
const char* OWM_LON = "222222222222";

// ========================================
// Zeit & NTP Konfiguration
// ========================================
const char* NTP_SERVER = "0.europe.pool.ntp.org";
// Zeitzonen String für verschiedene Regionen:
// Europe/Berlin:   "CET-1CEST,M3.5.0,M10.5.0/3"
// Europe/Vienna:   "CET-1CEST,M3.5.0,M10.5.0/3"
// Europe/Zurich:   "CET-1CEST,M3.5.0,M10.5.0/3"
// America/New_York: "EST5EDT,M3.2.0,M11.1.0"
const char* TIME_ZONE = "CET-1CEST,M3.5.0,M10.5.0/3";

// ========================================
// Display Pins (ESP32)
// ========================================
// Standard Pinout für Waveshare E-Paper Driver Board
#define EPD_CS      5
#define EPD_DC      17
#define EPD_RST     16
#define EPD_BUSY    4
#define EPD_SCK     18
#define EPD_MOSI    23

// ========================================
// Deep Sleep Weckzeiten
// ========================================
// Definiere die Uhrzeiten, zu denen das Display aktualisiert werden soll
// Format: {Stunde, Minute}
struct WakeTime {
  int hour;
  int minute;
};

const WakeTime WAKE_SCHEDULE[] = {
  {5, 30},   // 5:30 Uhr
  {6, 30},   // 6:30 Uhr
  {8, 0},    // 8:00 Uhr
  {11, 0},   // 11:00 Uhr
  {13, 0},   // 13:00 Uhr
  {15, 0},   // 15:00 Uhr
  {17, 0},   // 17:00 Uhr
  {20, 0},   // 20:00 Uhr
  {22, 0}    // 22:00 Uhr
};
const int WAKE_SCHEDULE_SIZE = sizeof(WAKE_SCHEDULE) / sizeof(WakeTime);

#endif // CONFIG_H
