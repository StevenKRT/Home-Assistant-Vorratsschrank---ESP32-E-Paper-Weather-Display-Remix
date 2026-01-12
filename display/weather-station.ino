/*
 * ========================================
 * E-Paper Weather Display
 * ========================================
 * ESP32-basierte Wetterstation mit 7.5" E-Paper Display (800x480)
 *
 * Features:
 * - Aktuelle Wetterdaten von OpenWeatherMap API
 * - 5-Stunden Wettervorhersage
 * - Deep Sleep Modus mit konfigurierbaren Weckzeiten
 * - Invertiertes Design (schwarz/weiß)
 * - GothamRnd-Bold Schriftart für einheitliches Design
 *
 * Hardware: GDEW075T7 (7.5" 800x480 b/w)
 * Board: ESP32 Dev Module
 * ========================================
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <GxEPD2_BW.h>
#include <WiFiClientSecure.h>

#include "fonts/GothamRnd_Bold24pt7b.h"
#include "fonts/GothamRnd_Bold18pt7b.h"
#include "fonts/GothamRnd_Bold12pt7b.h"
#include "fonts/GothamRnd_Bold10pt7b.h"
#include "fonts/GothamRnd_Bold8pt7b.h"
#include "icons.h"
#include <time.h>

// Konfiguration laden
#include "config.h"

// ========================================
// DISPLAY INITIALISIERUNG
// ========================================
// Display initialisieren (7.5 inch 800x480)
// Pins werden aus config.h geladen
GxEPD2_BW<GxEPD2_750_T7, GxEPD2_750_T7::HEIGHT> display(GxEPD2_750_T7(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

// Farben (für invertiertes Design)
const uint16_t COLOR_BACKGROUND = GxEPD_BLACK;
const uint16_t COLOR_FOREGROUND = GxEPD_WHITE;

// ========================================
// DATENSTRUKTUREN
// ========================================
// Wetterdaten Struktur
struct WeatherData {
  float temperature;
  float humidity;
  float pressure;
  float windSpeed;
  int windDeg;
  String description;
  String icon;
  int weatherId;
  String sunrise;
  String sunset;
};

// Stündliche Vorhersage
struct HourlyForecast {
  int hour;
  float temperature;
  int weatherId;
};

WeatherData weather;
HourlyForecast forecast[5];

#define MAX_APPOINTMENTS 10

struct Appointment {
  String weekday;
  String date;
  String time;
  String title;
  String room;
};

Appointment appointments[MAX_APPOINTMENTS];
int appointmentCount = 0;
String headerTitle = "WETTER";

#define MAX_ITEMS 50
#define MAX_KEYDATA 4

struct ItemEntry {
  String name;
  time_t expiryTs;
  int daysUntil;
};

ItemEntry items[MAX_ITEMS];
int itemCount = 0;

String keydataLines[MAX_KEYDATA];
int keydataCount = 0;

String noteText = "";

bool wifiConnected = false;
String wifiIp = "";

// Laufzeit-Konfiguration, überschreibbar über JSON/settings
bool cfgShowIp = true;
bool cfgShowLastUpdate = true;
bool cfgWeatherEnabled = true;
bool cfgWeatherForecastEnabled = true;
String cfgWeatherProvider = "openweathermap";
String cfgWeatherApiKey = OWM_API_KEY;
String cfgWeatherCity = OWM_CITY;
String cfgWeatherCountry = OWM_COUNTRY_CODE;
String cfgWeatherLat = OWM_LAT;
String cfgWeatherLon = OWM_LON;
String cfgNtpServer = NTP_SERVER;
String cfgTimeZone = TIME_ZONE;

String cfgTableMode = "appointments"; // "appointments" | "items"

struct WakeTimeCfg {
  int hour;
  int minute;
};
WakeTimeCfg cfgWakeSchedule[16];
int cfgWakeScheduleSize = 0;
bool cfgWakeScheduleOverride = false;

// Konvertierung von Minuten zu Mikrosekunden für Deep Sleep
// WAKE_SCHEDULE und WAKE_SCHEDULE_SIZE kommen aus config.h
#define uS_TO_S_FACTOR 1000000ULL
#define S_TO_MIN_FACTOR 60

// Vorwärtsdeklaration
void applyWeatherFromJson(JsonObject root);
time_t parseIsoDateToLocalMidnight(const String &iso);
int daysUntilFromNow(time_t target);
void sortItemsByDaysUntil();

// ========================================
// SETUP & MAIN LOOP
// ========================================
void setup() {
  Serial.begin(115200);
  Serial.println("\n========================================");
  Serial.println("Weather Display aufgewacht!");
  Serial.println("========================================");

  // Display initialisieren
  display.init(115200);
  display.setRotation(1);  // Hochformat
  display.setTextColor(GxEPD_BLACK);

  // WiFi verbinden und Zeit synchronisieren
  connectWiFi();

  // Reihenfolge: Erst JSON (settings + appointments + ggf. local weather), dann ggf. OpenWeatherMap
  updateAppointments();
  updateWeather();
  displayWeather();

  // Display in den Sleep-Modus versetzen
  display.hibernate();

  // WiFi trennen um Energie zu sparen
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  // Berechne nächste Weckzeit
  uint64_t sleepSeconds = calculateNextWakeTime();

  // ESP32 in Deep Sleep versetzen
  Serial.println("========================================");
  Serial.println("Gehe in Deep Sleep...");
  Serial.println("========================================");
  Serial.flush();  // Warte bis alle Serial-Daten gesendet wurden

  esp_sleep_enable_timer_wakeup(sleepSeconds * uS_TO_S_FACTOR);
  esp_deep_sleep_start();
}

void loop() {
  // Loop wird nie erreicht, da ESP32 in Deep Sleep geht
  // Nach Deep Sleep startet der ESP32 neu und setup() wird erneut ausgeführt
}

// ========================================
// HILFSFUNKTIONEN
// ========================================
void connectWiFi() {
  Serial.print("Verbinde mit WiFi");

  // Status zurücksetzen
  wifiConnected = false;
  wifiIp = "";

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi verbunden!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());

    wifiConnected = true;
    wifiIp = WiFi.localIP().toString();

    // NTP Zeit synchronisieren (Start mit Defaults aus config.h)
    configTzTime(TIME_ZONE, NTP_SERVER);
    Serial.println("NTP Zeit wird synchronisiert...");

    // Warte auf Zeitsynchronisation
    struct tm timeinfo;
    int retry = 0;
    while (!getLocalTime(&timeinfo) && retry < 10) {
      delay(500);
      retry++;
    }
    if (retry < 10) {
      Serial.println("Zeit synchronisiert!");
    } else {
      Serial.println("Zeit-Synchronisation fehlgeschlagen!");
    }
  } else {
    Serial.println("\nWiFi Verbindung fehlgeschlagen!");
  }
}

String formatTime(long timestamp) {
  time_t t = timestamp;
  struct tm* timeinfo = localtime(&t);
  char buffer[6];
  sprintf(buffer, "%02d:%02d", timeinfo->tm_hour, timeinfo->tm_min);
  return String(buffer);
}

String convertUmlautsToAscii(String text) {
  // UTF-8 deutsche Umlaute zu ASCII-lesbaren Zeichen konvertieren
  text.replace("ä", "ae");
  text.replace("ö", "oe");
  text.replace("ü", "ue");
  text.replace("Ä", "Ae");
  text.replace("Ö", "Oe");
  text.replace("Ü", "Ue");
  text.replace("ß", "ss");
  return text;
}

String getWindDirection(int deg) {
  if (deg >= 337 || deg < 23) return "N";
  if (deg >= 23 && deg < 67) return "NO";
  if (deg >= 67 && deg < 113) return "O";
  if (deg >= 113 && deg < 157) return "SO";
  if (deg >= 157 && deg < 203) return "S";
  if (deg >= 203 && deg < 247) return "SW";
  if (deg >= 247 && deg < 293) return "W";
  return "NW";
}

time_t parseIsoDateToLocalMidnight(const String &iso) {
  // Erwartet mindestens "YYYY-MM-DD"
  if (iso.length() < 10) {
    return (time_t)0;
  }

  int y = iso.substring(0, 4).toInt();
  int m = iso.substring(5, 7).toInt();
  int d = iso.substring(8, 10).toInt();

  if (y < 1970 || m < 1 || m > 12 || d < 1 || d > 31) {
    return (time_t)0;
  }

  struct tm t;
  memset(&t, 0, sizeof(t));
  t.tm_year = y - 1900;
  t.tm_mon  = m - 1;
  t.tm_mday = d;
  t.tm_hour = 0;
  t.tm_min  = 0;
  t.tm_sec  = 0;

  // Lokalzeit (Zeitzone ist via configTzTime gesetzt)
  return mktime(&t);
}

int daysUntilFromNow(time_t target) {
  time_t nowTs = time(nullptr);
  if (nowTs <= 0 || target <= 0) {
    return 0;
  }

  // Auf Tagesgrenzen (lokal) normalisieren
  struct tm nowTm;
  localtime_r(&nowTs, &nowTm);
  nowTm.tm_hour = 0;
  nowTm.tm_min  = 0;
  nowTm.tm_sec  = 0;
  time_t todayMidnight = mktime(&nowTm);

  double diff = difftime(target, todayMidnight);
  long days = (long)(diff / 86400.0);

  return (int)days;
}

void sortItemsByDaysUntil() {
  // einfache Auswahl-Sortierung, kleinste daysUntil zuerst
  for (int i = 0; i < itemCount - 1; i++) {
    int minIdx = i;
    for (int j = i + 1; j < itemCount; j++) {
      if (items[j].daysUntil < items[minIdx].daysUntil) {
        minIdx = j;
      }
    }
    if (minIdx != i) {
      ItemEntry tmp = items[i];
      items[i] = items[minIdx];
      items[minIdx] = tmp;
    }
  }
}

// Berechnet die Sekunden bis zur nächsten geplanten Weckzeit
uint64_t calculateNextWakeTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Zeit konnte nicht abgerufen werden, verwende Fallback (1 Stunde)");
    return 3600; // Fallback: 1 Stunde
  }

  int currentHour = timeinfo.tm_hour;
  int currentMinute = timeinfo.tm_min;
  int currentTotalMinutes = currentHour * 60 + currentMinute;

  Serial.printf("Aktuelle Zeit: %02d:%02d\n", currentHour, currentMinute);

  int nextWakeMinutes = -1;
  bool foundToday = false;

  // Entweder überschriebenes waketimes-Array oder Default aus config.h
  if (cfgWakeScheduleOverride && cfgWakeScheduleSize > 0) {
    for (int i = 0; i < cfgWakeScheduleSize; i++) {
      int wakeMinutes = cfgWakeSchedule[i].hour * 60 + cfgWakeSchedule[i].minute;
      if (wakeMinutes > currentTotalMinutes) {
        nextWakeMinutes = wakeMinutes;
        foundToday = true;
        Serial.printf("Naechste Weckzeit heute (override): %02d:%02d\n",
                      cfgWakeSchedule[i].hour, cfgWakeSchedule[i].minute);
        break;
      }
    }

    if (!foundToday) {
      nextWakeMinutes = cfgWakeSchedule[0].hour * 60 + cfgWakeSchedule[0].minute + (24 * 60);
      Serial.printf("Naechste Weckzeit morgen (override): %02d:%02d\n",
                    cfgWakeSchedule[0].hour, cfgWakeSchedule[0].minute);
    }
  } else {
    for (int i = 0; i < WAKE_SCHEDULE_SIZE; i++) {
      int wakeMinutes = WAKE_SCHEDULE[i].hour * 60 + WAKE_SCHEDULE[i].minute;
      if (wakeMinutes > currentTotalMinutes) {
        nextWakeMinutes = wakeMinutes;
        foundToday = true;
        Serial.printf("Naechste Weckzeit heute: %02d:%02d\n",
                      WAKE_SCHEDULE[i].hour, WAKE_SCHEDULE[i].minute);
        break;
      }
    }

    if (!foundToday) {
      nextWakeMinutes = WAKE_SCHEDULE[0].hour * 60 + WAKE_SCHEDULE[0].minute + (24 * 60);
      Serial.printf("Naechste Weckzeit morgen: %02d:%02d\n",
                    WAKE_SCHEDULE[0].hour, WAKE_SCHEDULE[0].minute);
    }
  }

  int minutesUntilWake = nextWakeMinutes - currentTotalMinutes;
  uint64_t secondsUntilWake = minutesUntilWake * 60;

  Serial.printf("Sleep-Dauer: %d Minuten (%llu Sekunden)\n",
                minutesUntilWake, secondsUntilWake);

  return secondsUntilWake;
}

// ========================================
// API DATEN-ABRUF – WETTER
// ========================================
void updateWeather() {
  if (!cfgWeatherEnabled) {
    Serial.println("Wetteranzeige deaktiviert (settings.weather = false) – kein Abruf.");
    return;
  }

  if (cfgWeatherProvider == "local") {
    Serial.println("Weather provider 'local' – Wetterdaten werden aus JSON übernommen, kein API-Call.");
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Keine WiFi Verbindung (Weather)");
    connectWiFi();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("Abbruch: weiterhin keine WiFi-Verbindung (Weather)");
      return;
    }
  }

  // One Call 3.0 – URL zusammensetzen, mit evtl. überschriebenen Parametern
  String fullUrl =
      String("https://api.openweathermap.org/data/3.0/onecall?lat=") +
      cfgWeatherLat +
      "&lon=" + cfgWeatherLon +
      "&exclude=minutely,daily,alerts&appid=" +
      cfgWeatherApiKey +
      "&units=metric&lang=de";

  String url = fullUrl;
  url.remove(0, 8); // "https://"
  int slashIndex = url.indexOf('/');
  String host = (slashIndex >= 0) ? url.substring(0, slashIndex) : url;
  String path = (slashIndex >= 0) ? url.substring(slashIndex) : "/";

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(15000);

  Serial.printf("Verbinde zu %s:443 (Weather)\n", host.c_str());
  if (!client.connect(host.c_str(), 443)) {
    Serial.println("TLS-Verbindung fehlgeschlagen (Weather)");
    return;
  }

  String request =
      String("GET ") + path + " HTTP/1.1\r\n" +
      "Host: " + host + "\r\n" +
      "User-Agent: ESP32-WeatherDisplay\r\n" +
      "Accept: application/json\r\n" +
      "Accept-Encoding: identity\r\n" +
      "Connection: close\r\n\r\n";

  client.print(request);

  String line;
  int contentLength = -1;

  while (client.connected()) {
    line = client.readStringUntil('\n');

    if (line.startsWith("HTTP/")) {
      Serial.print("Statuszeile (Weather): ");
      Serial.print(line);
    }

    line.trim();
    if (line.length() == 0) {
      break;
    }

    if (line.startsWith("Content-Length:")) {
      String lenStr = line.substring(strlen("Content-Length:"));
      lenStr.trim();
      contentLength = lenStr.toInt();
    }
  }

  Serial.printf("Content-Length laut Header (Weather): %d\n", contentLength);

  String payload;
  if (contentLength > 0 && contentLength < 20000) {
    payload.reserve(contentLength + 1);
  }

  while (client.connected() || client.available()) {
    while (client.available()) {
      char c = client.read();
      payload += c;
    }
    delay(10);
  }
  client.stop();

  Serial.printf("Payload-Laenge (Weather): %d\n", payload.length());
  Serial.println("Payload (Weather) – Ausschnitt:");
  Serial.println(payload.substring(0, 200));

  if (payload.length() == 0) {
    Serial.println("Leeres Payload (Weather) – Abbruch");
    return;
  }

  DynamicJsonDocument doc(16384);
  DeserializationError error = deserializeJson(doc, payload);

  if (error) {
    Serial.print("JSON Parse Fehler (Weather): ");
    Serial.println(error.c_str());
    return;
  }

  JsonObject current = doc["current"];

  weather.temperature = current["temp"]        | 0.0f;
  weather.humidity    = current["humidity"]    | 0.0f;
  weather.pressure    = current["pressure"]    | 0.0f;
  weather.windSpeed   = current["wind_speed"]  | 0.0f;
  weather.windDeg     = current["wind_deg"]    | 0;

  weather.description = current["weather"][0]["description"] | "";
  weather.icon        = current["weather"][0]["icon"]        | "";
  weather.weatherId   = current["weather"][0]["id"]          | 0;

  long sunrise = current["sunrise"] | 0;
  long sunset  = current["sunset"]  | 0;
  weather.sunrise = formatTime(sunrise);
  weather.sunset  = formatTime(sunset);

  Serial.println("Aktuelle Wetterdaten (One Call) aktualisiert");
  Serial.printf("Temperatur: %.1f°C, Luftfeuchte: %.0f%%, Druck: %.0f hPa\n",
                weather.temperature, weather.humidity, weather.pressure);

  JsonArray hourly = doc["hourly"];
  int maxForecast = 5;

  for (int i = 0; i < maxForecast; i++) {
    int idx = i + 1;
    if (idx >= (int)hourly.size()) {
      break;
    }

    JsonObject h = hourly[idx];
    time_t dt = h["dt"] | 0;
    struct tm* timeinfo = localtime(&dt);

    if (timeinfo) {
      forecast[i].hour = timeinfo->tm_hour;
    } else {
      forecast[i].hour = 0;
    }

    forecast[i].temperature = h["temp"] | 0.0f;
    forecast[i].weatherId   = h["weather"][0]["id"] | 0;
  }

  Serial.println("Vorhersage (One Call hourly) aktualisiert");
}

// Lokale Wetterdaten aus JSON übernehmen (selbe Struktur wie OneCall)
void applyWeatherFromJson(JsonObject root) {
  if (!root.containsKey("current")) {
    Serial.println("Lokales Weather-Objekt enthaelt keinen 'current'-Block.");
    return;
  }

  JsonObject current = root["current"];

  weather.temperature = current["temp"]        | 0.0f;
  weather.humidity    = current["humidity"]    | 0.0f;
  weather.pressure    = current["pressure"]    | 0.0f;
  weather.windSpeed   = current["wind_speed"]  | 0.0f;
  weather.windDeg     = current["wind_deg"]    | 0;

  weather.description = current["weather"][0]["description"] | "";
  weather.icon        = current["weather"][0]["icon"]        | "";
  weather.weatherId   = current["weather"][0]["id"]          | 0;

  long sunrise = current["sunrise"] | 0;
  long sunset  = current["sunset"]  | 0;
  weather.sunrise = formatTime(sunrise);
  weather.sunset  = formatTime(sunset);

  Serial.println("Lokale Wetterdaten aus JSON uebernommen");
  Serial.printf("Temperatur: %.1f°C, Luftfeuchte: %.0f%%, Druck: %.0f hPa\n",
                weather.temperature, weather.humidity, weather.pressure);

  JsonArray hourly = root["hourly"];
  int maxForecast = 5;

  if (!hourly.isNull()) {
    for (int i = 0; i < maxForecast; i++) {
      int idx = i + 1;
      if (idx >= (int)hourly.size()) {
        break;
      }

      JsonObject h = hourly[idx];
      time_t dt = h["dt"] | 0;
      struct tm* timeinfo = localtime(&dt);

      if (timeinfo) {
        forecast[i].hour = timeinfo->tm_hour;
      } else {
        forecast[i].hour = 0;
      }

      forecast[i].temperature = h["temp"] | 0.0f;
      forecast[i].weatherId   = h["weather"][0]["id"] | 0;
    }

    Serial.println("Lokale Vorhersage (hourly) aus JSON uebernommen");
  }
}

// ========================================
// API DATEN-ABRUF – APPOINTMENTS + SETTINGS
// ========================================
void updateAppointments() {
  appointmentCount = 0;
  headerTitle = "WETTER";

  itemCount = 0;
  keydataCount = 0;
  noteText = "";
  cfgTableMode = "appointments";

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Keine WiFi Verbindung (Appointments)");
    return;
  }

  String url = String(APPOINTMENTS_URL);
  if (!url.startsWith("https://")) {
    Serial.println("APPOINTMENTS_URL muss mit https:// beginnen");
    return;
  }
  url.remove(0, 8); // "https://"

  int slashIndex = url.indexOf('/');
  String host = (slashIndex >= 0) ? url.substring(0, slashIndex) : url;
  String path = (slashIndex >= 0) ? url.substring(slashIndex) : "/";

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(15000);

  Serial.printf("Verbinde zu %s:443\n", host.c_str());
  if (!client.connect(host.c_str(), 443)) {
    Serial.println("TLS-Verbindung fehlgeschlagen (Appointments)");
    return;
  }

  String request =
      String("GET ") + path + " HTTP/1.1\r\n" +
      "Host: " + host + "\r\n" +
      "User-Agent: ESP32-WeatherDisplay\r\n" +
      "Accept: application/json\r\n" +
      "Accept-Encoding: identity\r\n" +
      "Connection: close\r\n\r\n";

  client.print(request);

  String line;
  int contentLength = -1;

  while (client.connected()) {
    line = client.readStringUntil('\n');

    if (line.startsWith("HTTP/")) {
      Serial.print("Statuszeile (Appointments): ");
      Serial.print(line);
    }

    line.trim();
    if (line.length() == 0) {
      break;
    }

    if (line.startsWith("Content-Length:")) {
      String lenStr = line.substring(strlen("Content-Length:"));
      lenStr.trim();
      contentLength = lenStr.toInt();
    }
  }

  Serial.printf("Content-Length laut Header: %d\n", contentLength);

  String payload;
  if (contentLength > 0 && contentLength < 20000) {
    payload.reserve(contentLength + 1);
  }

  while (client.connected() || client.available()) {
    while (client.available()) {
      char c = client.read();
      payload += c;
    }
    delay(10);
  }
  client.stop();

  Serial.printf("Payload-Laenge (Appointments): %d\n", payload.length());
  Serial.println("Payload (Appointments) – Ausschnitt:");
  Serial.println(payload.substring(0, 200));

  if (payload.length() == 0) {
    Serial.println("Leeres Payload (Appointments) – Abbruch");
    return;
  }

  DynamicJsonDocument doc(16384);
  DeserializationError error = deserializeJson(doc, payload);

  if (error) {
    Serial.print("JSON Parse Fehler (Appointments): ");
    Serial.println(error.c_str());
    return;
  }

  // Header (falls vorhanden)
  if (doc["header"].is<const char*>()) {
    headerTitle = String(doc["header"].as<const char*>());
  }

  // SETTINGS auswerten (optional)
  JsonObject settings = doc["settings"];
  if (!settings.isNull()) {
    // showip / showlastupdate / weather / weather_forecast
    if (!settings["showip"].isNull()) {
      cfgShowIp = settings["showip"];
    }
    if (!settings["showlastupdate"].isNull()) {
      cfgShowLastUpdate = settings["showlastupdate"];
    }
    if (!settings["weather"].isNull()) {
      cfgWeatherEnabled = settings["weather"];
    }
    if (!settings["weather_forecast"].isNull()) {
      cfgWeatherForecastEnabled = settings["weather_forecast"];
    }

    // weather_provider
    if (!settings["weather_provider"].isNull()) {
      String provider = settings["weather_provider"].as<String>();
      provider.toLowerCase();
      provider.trim();
      if (provider == "local") {
        cfgWeatherProvider = "local";
      } else {
        cfgWeatherProvider = "openweathermap";
      }
    }

    // tablemode
    if (!settings["tablemode"].isNull()) {
      String tm = settings["tablemode"].as<String>();
      tm.toLowerCase();
      tm.trim();
      if (tm == "items" || tm == "appointments") {
        cfgTableMode = tm;
      } else {
        cfgTableMode = "appointments";
      }
    }

    // Strings nur überschreiben, wenn nicht null/leer
    if (!settings["weather_apikey"].isNull()) {
      String s = settings["weather_apikey"].as<String>();
      if (s.length() > 0) {
        cfgWeatherApiKey = s;
      }
    }
    if (!settings["weather_city"].isNull()) {
      String s = settings["weather_city"].as<String>();
      if (s.length() > 0) {
        cfgWeatherCity = s;
      }
    }
    if (!settings["weather_country"].isNull()) {
      String s = settings["weather_country"].as<String>();
      if (s.length() > 0) {
        cfgWeatherCountry = s;
      }
    }
    if (!settings["weather_lat"].isNull()) {
      String s = settings["weather_lat"].as<String>();
      if (s.length() > 0) {
        cfgWeatherLat = s;
      }
    }
    if (!settings["weather_lon"].isNull()) {
      String s = settings["weather_lon"].as<String>();
      if (s.length() > 0) {
        cfgWeatherLon = s;
      }
    }

    if (!settings["ntp"].isNull()) {
      String s = settings["ntp"].as<String>();
      if (s.length() > 0) {
        cfgNtpServer = s;
      }
    }
    if (!settings["timezone"].isNull()) {
      String s = settings["timezone"].as<String>();
      if (s.length() > 0) {
        cfgTimeZone = s;
      }
    }

    // waketimes: Array von [hour, minute]
    cfgWakeScheduleSize = 0;
    cfgWakeScheduleOverride = false;
    if (settings["waketimes"].is<JsonArray>()) {
      JsonArray wArr = settings["waketimes"].as<JsonArray>();
      int idx = 0;
      for (JsonVariant v : wArr) {
        if (!v.is<JsonArray>()) {
          continue;
        }
        JsonArray pair = v.as<JsonArray>();
        if (pair.size() < 2) {
          continue;
        }
        if (idx >= 16) {
          break;
        }
        int h = pair[0] | 0;
        int m = pair[1] | 0;
        if (h < 0 || h > 23 || m < 0 || m > 59) {
          continue;
        }
        cfgWakeSchedule[idx].hour = h;
        cfgWakeSchedule[idx].minute = m;
        idx++;
      }
      if (idx > 0) {
        cfgWakeScheduleSize = idx;
        cfgWakeScheduleOverride = true;
      }
    }

    // Zeitzone/NTP ggf. sofort anwenden
    if (wifiConnected) {
      configTzTime(cfgTimeZone.c_str(), cfgNtpServer.c_str());
      Serial.print("Zeitzone/NTP aus settings angewendet: ");
      Serial.print(cfgTimeZone);
      Serial.print(" / ");
      Serial.println(cfgNtpServer);
    }
  }

  // keydata: Array von Strings (max 4)
  keydataCount = 0;
  if (doc["keydata"].is<JsonArray>()) {
    JsonArray kd = doc["keydata"].as<JsonArray>();
    for (JsonVariant v : kd) {
      if (keydataCount >= MAX_KEYDATA) {
        break;
      }
      if (v.is<const char*>()) {
        keydataLines[keydataCount] = String(v.as<const char*>());
        keydataCount++;
      } else if (v.is<String>()) {
        keydataLines[keydataCount] = v.as<String>();
        keydataCount++;
      }
    }
  }

  // note: String
  if (!doc["note"].isNull()) {
    if (doc["note"].is<const char*>()) {
      noteText = String(doc["note"].as<const char*>());
    } else if (doc["note"].is<String>()) {
      noteText = doc["note"].as<String>();
    }
  }

  // items: Einträge lesen und nach daysUntil sortieren
  itemCount = 0;
  if (doc["items"].is<JsonArray>()) {
    JsonArray itArr = doc["items"].as<JsonArray>();
    for (JsonVariant v : itArr) {
      if (itemCount >= MAX_ITEMS) {
        break;
      }
      if (!v.is<JsonObject>()) {
        continue;
      }

      JsonObject o = v.as<JsonObject>();

      String name = o["name"] | "";
      String expiryIso = o["expiryDate"] | "";

      time_t expTs = parseIsoDateToLocalMidnight(expiryIso);
      int days = daysUntilFromNow(expTs);

      items[itemCount].name = name;
      items[itemCount].expiryTs = expTs;
      items[itemCount].daysUntil = days;
      itemCount++;
    }
  }

  if (itemCount > 1) {
    sortItemsByDaysUntil();
  }

  // Lokales Wetter aus JSON, falls Provider "local"
  if (cfgWeatherProvider == "local") {
    JsonVariant wVar = doc["weather"];
    if (!wVar.isNull()) {
      if (wVar.is<JsonObject>()) {
        JsonObject wObj = wVar.as<JsonObject>();
        applyWeatherFromJson(wObj);
      } else if (wVar.is<JsonArray>()) {
        JsonArray wArr = wVar.as<JsonArray>();
        if (!wArr.isNull() && wArr.size() > 0 && wArr[0].is<JsonObject>()) {
          JsonObject wObj = wArr[0].as<JsonObject>();
          applyWeatherFromJson(wObj);
        }
      }
    }
  }

  // Terminliste
  JsonArray arr;
  if (doc["appointments"].is<JsonArray>()) {
    arr = doc["appointments"].as<JsonArray>();
  } else if (doc.is<JsonArray>()) {
    arr = doc.as<JsonArray>();
  }

  int count = 0;
  if (!arr.isNull()) {
    for (JsonObject obj : arr) {
      if (count >= MAX_APPOINTMENTS) {
        break;
      }

      appointments[count].weekday = obj["weekday"] | "";
      appointments[count].date    = obj["date"]    | "";
      appointments[count].time    = obj["time"]    | "";
      appointments[count].title   = obj["title"]   | "";
      appointments[count].room    = obj["room"]    | "";

      count++;
    }
  }

  appointmentCount = count;
  Serial.printf("Termine aktualisiert: %d\n", appointmentCount);
}

// ========================================
// DISPLAY RENDERING
// ========================================
String fitTextToWidth(const String &text, int maxWidth) {
  String result = text;
  int16_t x1, y1;
  uint16_t w, h;

  display.getTextBounds(result.c_str(), 0, 0, &x1, &y1, &w, &h);
  if (w <= maxWidth) {
    return result;
  }

  String base = text;
  while (base.length() > 0) {
    String candidate = base + "...";
    display.getTextBounds(candidate.c_str(), 0, 0, &x1, &y1, &w, &h);
    if (w <= maxWidth) {
      return candidate;
    }
    base.remove(base.length() - 1);
  }

  return "";
}

String fitTextToWidthMultilineLastEllipsis(const String &text, int maxWidth, String *lines, int maxLines) {
    int16_t x1, y1;
    uint16_t w, h;

    String s = text;
    s.replace("\r", " ");
    s.replace("\n", " ");
    s.trim();

    for (int i = 0; i < maxLines; i++) {
        lines[i] = "";
    }

    if (s.length() == 0 || maxLines <= 0) {
        return "";
    }

    int lineIndex = 0;

    while (s.length() > 0 && lineIndex < maxLines) {
        if (lineIndex == maxLines - 1) {
            // Letzte Zeile: ggf. mit "..." kuerzen
            lines[lineIndex] = fitTextToWidth(s, maxWidth);
            return "";
        }

        // Ganze Restzeichenkette passt?
        display.getTextBounds(s.c_str(), 0, 0, &x1, &y1, &w, &h);
        if ((int)w <= maxWidth) {
            lines[lineIndex] = s;
            return "";
        }

        // Greedy: so viel wie moeglich in diese Zeile packen (wortbasiert)
        int cut = s.length();
        int best = -1;

        while (cut > 0) {
            int spacePos = s.lastIndexOf(' ', cut - 1);
            if (spacePos <= 0) {
                break;
            }

            String candidate = s.substring(0, spacePos);
            candidate.trim();
            if (candidate.length() == 0) {
                cut = spacePos;
                continue;
            }

            display.getTextBounds(candidate.c_str(), 0, 0, &x1, &y1, &w, &h);
            if ((int)w <= maxWidth) {
                best = spacePos;
                break;
            }

            cut = spacePos;
        }

        if (best > 0) {
            String line = s.substring(0, best);
            line.trim();
            lines[lineIndex] = line;

            s = s.substring(best);
            s.trim();
        } else {
            // Kein Leerzeichen gefunden das passt: hart kuerzen (ohne Ellipsis, nur fuer Zwischenzeilen)
            String base = s;
            while (base.length() > 0) {
                display.getTextBounds(base.c_str(), 0, 0, &x1, &y1, &w, &h);
                if ((int)w <= maxWidth) {
                    lines[lineIndex] = base;
                    s = s.substring(base.length());
                    s.trim();
                    break;
                }
                base.remove(base.length() - 1);
            }

            if (lines[lineIndex].length() == 0) {
                break;
            }
        }

        lineIndex++;
    }

    return "";
}

void displayWeather() {
  display.setFullWindow();
  display.firstPage();

  do {
    display.fillScreen(GxEPD_WHITE);
    int16_t x1, y1;
    uint16_t w, h;

    // ===== HEADER (0-50) =====
    display.fillRect(0, 0, 480, 50, GxEPD_BLACK);
    display.setTextColor(GxEPD_WHITE);
    centerText(headerTitle.c_str(), 38, &GothamRnd_Bold18pt7b);

    // ===== AKTUELLES WETTER (50-180) =====
    display.setTextColor(GxEPD_BLACK);

    if (cfgWeatherEnabled) {
      const char* icon = getWeatherIcon(weather.weatherId);
      display.drawXBitmap(30, 70, (const uint8_t*)icon, 50, 50, GxEPD_BLACK);

      display.setFont(&GothamRnd_Bold24pt7b);
      char tempStr[10];
      sprintf(tempStr, "%.0f%cC", weather.temperature, (char)176);
      display.setCursor(120, 115);
      display.print(tempStr);

      display.setFont(&GothamRnd_Bold12pt7b);
      char humPressStr[30];
      sprintf(humPressStr, "%.0f %%    %.0f hPa", weather.humidity, weather.pressure);
      display.setCursor(120, 145);
      display.print(humPressStr);
    }

    // Trennlinie unter dem Wetterblock immer zeichnen
    display.drawLine(20, 175, 460, 175, GxEPD_BLACK);

    // ===== 5-STUNDEN FORECAST ODER KEYDATA+NOTE (180-340) =====
    if (cfgWeatherEnabled && cfgWeatherForecastEnabled) {
      int colWidth = 96;  // 480 / 5 = 96
      int forecastY = 205;

      for (int i = 0; i < 5; i++) {
        int colX = i * colWidth;
        int centerX = colX + colWidth / 2;

        display.setFont(&GothamRnd_Bold12pt7b);
        char hourStr[6];
        sprintf(hourStr, "%02d:00", forecast[i].hour);
        display.getTextBounds(hourStr, 0, 0, &x1, &y1, &w, &h);
        display.setCursor(centerX - w / 2, forecastY);
        display.print(hourStr);

        const char* fIcon = getWeatherIcon(forecast[i].weatherId);
        display.drawXBitmap(centerX - 25, forecastY + 10, (const uint8_t*)fIcon, 50, 50, GxEPD_BLACK);

        char fTempStr[8];
        sprintf(fTempStr, "%.0f%cC", forecast[i].temperature, (char)176);
        display.getTextBounds(fTempStr, 0, 0, &x1, &y1, &w, &h);
        display.setCursor(centerX - w / 2, forecastY + 80);
        display.print(fTempStr);
      }
    } else {
      display.setTextColor(GxEPD_BLACK);

      // Zwei Spalten: links keydata (max 4 Zeilen), rechts note (einzeilig, mit ... gekürzt)
      int leftX = 30;
      int rightX = 250;
      int topY = 205;
      int lineH = 28;

      int leftMaxWidth = 200;
      int rightMaxWidth = 200;

      display.setFont(&GothamRnd_Bold8pt7b);

      // links: keydata
      int lines = keydataCount;
      if (lines > MAX_KEYDATA) {
        lines = MAX_KEYDATA;
      }

      for (int i = 0; i < lines; i++) {
        String s = fitTextToWidth(keydataLines[i], leftMaxWidth);
        if (s.length() == 0) {
          continue;
        }
        display.setCursor(leftX, topY + (i * lineH));
        display.print(s);
      }

      // rechts: note (einzeilig)
      if (noteText.length() > 0) {
          String noteLines[4];
          fitTextToWidthMultilineLastEllipsis(noteText, rightMaxWidth, noteLines, 4);

          for (int i = 0; i < 4; i++) {
              if (noteLines[i].length() == 0) {
                  break;
              }
              display.setCursor(rightX, topY + (i * lineH));
              display.print(noteLines[i]);
          }
      }
    }

    // Trennlinie 2 – optisch unter Forecast/Bereich
    display.drawLine(20, 300, 460, 300, GxEPD_BLACK);

    // ===== TAGESINFOS / TERMINE ODER ITEMS =====
    display.setFont(&GothamRnd_Bold12pt7b);

    int rowStartY = 350;
    int rowHeight = 30;
    int footerY = 770;
    int marginBottom = 10;

    int availableHeight = footerY - marginBottom - rowStartY;
    int maxRows = availableHeight / rowHeight;
    if (maxRows < 0) {
      maxRows = 0;
    }

    if (cfgTableMode.equalsIgnoreCase("items")) {
      int rowsToDraw = itemCount;
      if (rowsToDraw > 10) {
        rowsToDraw = 10;
      }
      if (rowsToDraw > maxRows) {
        rowsToDraw = maxRows;
      }

      for (int i = 0; i < rowsToDraw; i++) {
        int baselineY = rowStartY + i * rowHeight;

        String name = items[i].name;
        int days = items[i].daysUntil;

        String daysText = String(days) + " Tage";

        int16_t tx1, ty1;
        uint16_t tw, th;

        display.getTextBounds(daysText.c_str(), 0, 0, &tx1, &ty1, &tw, &th);
        int daysRightX = 460;
        int daysX = daysRightX - (int)tw;

        int nameX = 40;
        int nameMaxWidth = daysX - 10 - nameX;
        if (nameMaxWidth < 0) {
          nameMaxWidth = 0;
        }

        String fittedName = (nameMaxWidth > 0) ? fitTextToWidth(name, nameMaxWidth) : "";
        if (fittedName.length() > 0) {
          display.setCursor(nameX, baselineY);
          display.print(fittedName);
        }

        display.setCursor(daysX, baselineY);
        display.print(daysText);
      }
    } else {
      int rowsToDraw = appointmentCount;
      if (rowsToDraw > maxRows) {
        rowsToDraw = maxRows;
      }

      for (int i = 0; i < rowsToDraw; i++) {
        int baselineY = rowStartY + i * rowHeight;

        String weekday = appointments[i].weekday;
        String date    = appointments[i].date;
        String time    = appointments[i].time;
        String title   = appointments[i].title;
        String room    = appointments[i].room;

        String prefix = weekday + " " + date + " " + time + " ";

        int16_t tx1, ty1;
        uint16_t tw, th;

        String roomText = room;
        display.getTextBounds(roomText.c_str(), 0, 0, &tx1, &ty1, &tw, &th);
        int roomRightX = 460;
        int roomX = roomRightX - (int)tw;

        int prefixX = 40;
        display.getTextBounds(prefix.c_str(), 0, 0, &tx1, &ty1, &tw, &th);
        display.setCursor(prefixX, baselineY);
        display.print(prefix);

        int titleStartX = prefixX + (int)tw + 6;
        int titleEndX = roomX - 10;
        int titleMaxWidth = titleEndX - titleStartX;

        if (titleMaxWidth > 0) {
          String fittedTitle = fitTextToWidth(title, titleMaxWidth);
          if (fittedTitle.length() > 0) {
            display.getTextBounds(fittedTitle.c_str(), 0, 0, &tx1, &ty1, &tw, &th);
            display.setCursor(titleStartX, baselineY);
            display.print(fittedTitle);
          }
        }

        display.setCursor(roomX, baselineY);
        display.print(roomText);
      }
    }

    // ===== FOOTER (750-800) =====
    display.setFont(&GothamRnd_Bold12pt7b);

    int footerYBase = 740;

    if (cfgShowIp) {
      String wifiStr;
      if (wifiConnected) {
        wifiStr = "WiFi " + wifiIp;
      } else {
        wifiStr = "WiFi: offline";
      }
      display.getTextBounds(wifiStr.c_str(), 0, 0, &x1, &y1, &w, &h);
      display.setCursor((480 - w) / 2, footerYBase);
      display.print(wifiStr);
      footerYBase += 30;
    }

    if (cfgShowLastUpdate) {
      String timeStr = getTimeString();
      display.getTextBounds(timeStr.c_str(), 0, 0, &x1, &y1, &w, &h);
      display.setCursor((480 - w) / 2, footerYBase);
      display.print(timeStr);
    }

  } while (display.nextPage());

  Serial.println("Display aktualisiert");
}

const char* getWeatherIcon(int weatherId) {
  // XBM Icon Mapping für OpenWeatherMap IDs
  if (weatherId == 800) return clear_day_bits;              // Sonnig
  if (weatherId == 801) return partly_cloudy_day_bits;      // Leicht bewölkt
  if (weatherId >= 802 && weatherId <= 804) return cloudy_bits;  // Bewölkt
  if (weatherId >= 200 && weatherId < 300) return rain_bits;     // Gewitter -> Regen
  if (weatherId >= 300 && weatherId < 400) return rain_bits;     // Niesel
  if (weatherId >= 500 && weatherId < 600) return rain_bits;     // Regen
  if (weatherId >= 600 && weatherId < 700) return snow_bits;     // Schnee
  if (weatherId >= 700 && weatherId < 800) return fog_bits;      // Nebel/Dunst
  return cloudy_bits;  // Unbekannt
}

void centerText(const char* text, int y, const GFXfont* font) {
  display.setFont(font);
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((480 - w) / 2, y);
  display.print(text);
}

String getTimeString() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "Zeit nicht verfügbar";
  }

  char timeStr[30];
  sprintf(timeStr, "%02d.%02d.%04d, %02d:%02d Uhr",
          timeinfo.tm_mday,
          timeinfo.tm_mon + 1,
          timeinfo.tm_year + 1900,
          timeinfo.tm_hour,
          timeinfo.tm_min);
  return String(timeStr);
}