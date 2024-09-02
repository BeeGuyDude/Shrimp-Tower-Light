#include <Adafruit_NeoPixel.h>
#include "RTClib.h"
#include "WiFi.h"
#include "ESPAsyncWebServer.h"

//LED Ring Configs
#define LARGE_LED_PIN   10
#define LARGE_LED_COUNT 24
#define SMALL_LED_PIN   8
#define SMALL_LED_COUNT 7
const int MAX_DAYTIME_BRIGHTNESS {102};
const int MAX_NIGHTTIME_BRIGHTNESS {12};

//LED Color Hex Codes
uint32_t RED = 0xFF0000;
uint32_t GREEN = 0x00FF00;
uint32_t BLUE = 0x0000FF;
uint32_t NIGHTTIME_BLUE = 0x3300FF;
uint32_t WHITE = 0xFFFFFF;
// uint32_t WARM_WHITE = 0xCFA6FF;
uint32_t WARM_WHITE = 0xFFBA6A;
uint32_t WARM_WHITE_GRBW = 0xCFA6FFFF;

//Device Instances
RTC_DS1307 rtc;
char daysOfTheWeek[7][12] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
Adafruit_NeoPixel largeRings(LARGE_LED_COUNT, LARGE_LED_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel smallRing(SMALL_LED_COUNT, SMALL_LED_PIN, NEO_GRBW + NEO_KHZ800);
AsyncWebServer server(69);

//Working (Global) Variables  
double maxOperatingBrightness = 0;
double brightness = 0;
bool manualOverrideTriggered = false;
bool wasDaytime = false;
DateTime now;

//TUNING CONSTANTS
#define MANUAL_OVERRIDE_TIME_MINUTES 15
#define TRANSITION_FADE_TIME_SECONDS 3

#define SUNRISE_HOUR 10
#define SUNSET_HOUR 19
#define MOONRISE_HOUR 23
#define MOONSET_HOUR 2

//WIFI CONSTANTS
const char* ssid = "Shrimpternet Core";
const char* password = "pimpshrimpin";

//DAYLIGHT STATE STORAGE
typedef enum {
  SUNRISE,
  SUNLIGHT,
  SUNSET,
  MOONRISE,
  MOONLIGHT,
  MOONSET,
  DARK
} DaylightState;
DaylightState daylightState;

void setup() {
  Serial.begin(115200);
  // while (!Serial);
  Serial.println("BEGINNING STARTUP");

  //Wifi Initialization
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);
  Serial.println("Wifi Broadcasting...");
  // while (WiFi.waitForConnectResult() != WL_CONNECTED) {
  //   Serial.print(".");
  //   delay(333);
  // }
  Serial.println("");
  Serial.print("IP Address: ");
  Serial.println(WiFi.softAPIP());

  //WebServer Request Handling
  server.on("/time", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/plain", getTime().c_str());
  });
  server.on("/override", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/plain", getOverrideState().c_str());
  });
  server.on("/daylight-period", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/plain", getDaylightPeriodState().c_str());
  });

  //Ring Initialization
  largeRings.begin();
  smallRing.begin();
  debugCycleLEDs();

  //Flush Brightness to impossible value to force initial state update (updateBrightness());
  largeRings.setBrightness(255);
  smallRing.setBrightness(255);

  //RTC configuration
  if (!rtc.begin()) {
    Serial.println("FUCK FUCK FUCK FUCK TIME IS FUCKED GOD WE'RE ALL DAMNED TO ETERNITY");
  }
  now = rtc.now();

  //Daylight period state initialization
  bool isDaytime = true;
  if (now.hour() == SUNRISE_HOUR) {
    daylightState = SUNRISE;
  } else if (now.hour() > SUNRISE_HOUR && now.hour() < SUNSET_HOUR) {
    daylightState = SUNLIGHT;
  } else if (now.hour() == SUNSET_HOUR) {
    daylightState = SUNSET;
  } else if (now.hour() == MOONRISE_HOUR) {
    isDaytime = false;
    daylightState = MOONRISE;
  } else if (now.hour() > MOONRISE_HOUR || now.hour() < MOONSET_HOUR) {
    isDaytime = false;
    daylightState = MOONLIGHT;
  } else if (now.hour() == MOONSET_HOUR) {
    isDaytime = false;
    daylightState = MOONSET;
  } else {
    daylightState = DARK;
  }
  if (isDaytime) {
    setDaytimeColorProfile();
    maxOperatingBrightness = MAX_DAYTIME_BRIGHTNESS;
  } else {
    setNighttimeColorProfile();
    maxOperatingBrightness = MAX_NIGHTTIME_BRIGHTNESS;
  }
  server.begin();

  //Test String converter functions for use in HTTP requests
  Serial.println("STRING CONVERTER FUNCTION TESTS");
  Serial.print("Time: ");
  Serial.print(getTime());
  Serial.print(" | Daylight Period: ");
  Serial.print(getDaylightPeriodState());
  Serial.print(" | Override: ");
  Serial.println(getOverrideState());

  Serial.println("STARTUP COMPLETED");
  Serial.println("");
}

void loop() {

  //Update time from RTC, make sure is valid
  DateTime collectedTime = rtc.now();
  if (collectedTime.year() != 2000) now = collectedTime;

  //Daylight period state check
  updateDaylightPeriod();

  //Brightness update based on daylight period
  Serial.print("IT IS CURRENTLY: ");
  switch(daylightState) {
    case SUNRISE:
      brightness = (now.minute() / 60.0f) * maxOperatingBrightness;
      Serial.print("SUNRISE");
      break;
    case SUNLIGHT:
      brightness = maxOperatingBrightness;
      Serial.print("SUNLIGHT");
      break;
    case SUNSET:
      brightness = ((60 - now.minute()) / 60.0f) * maxOperatingBrightness;
      Serial.print("SUNSET");
      break;
    case MOONRISE:
      brightness = (now.minute() / 60.0f) * maxOperatingBrightness;
      Serial.print("MOONRISE");
      break;
    case MOONLIGHT:
      brightness = maxOperatingBrightness;
      Serial.print("MOONLIGHT");
      break;
    case MOONSET:
      brightness = ((60 - now.minute()) / 60.0f) * maxOperatingBrightness;
      Serial.print("MOONSET");
      break;
    case DARK:
      brightness = 0;
      Serial.print("DARK");
      break;
  }

  Serial.print(" | Brightness Target: ");
  Serial.print(brightness);
  Serial.print(" | Hardware Brightness: ");
  Serial.print(largeRings.getBrightness());

  //Update brightness if it has changed
  Serial.print(" | Changing? ");
  if (largeRings.getBrightness() != brightness) {
    updateBrightness(brightness);
    Serial.println("YES");
  } else {
    Serial.println("NO");
  }
  Serial.println("");
  delay(2000);
}

void updateDaylightPeriod() {
  switch(daylightState) {
    case SUNRISE:
      if (now.hour() > SUNRISE_HOUR) daylightState = SUNLIGHT;
      break;
    case SUNLIGHT:
      if (now.hour() == SUNSET_HOUR) daylightState = SUNSET;
      break;
    case SUNSET:
      if (now.hour() > SUNSET_HOUR) daylightState = DARK;
      break;
    case MOONRISE:
      if (now.hour() > MOONRISE_HOUR) daylightState = MOONLIGHT;
      break;
    case MOONLIGHT:
      if (now.hour() == MOONSET_HOUR) daylightState = MOONSET;
      break;
    case MOONSET:
      if (now.hour() > MOONSET_HOUR) daylightState = DARK;
      break;
    case DARK:
      if (now.hour() == MOONRISE_HOUR) {
        setNighttimeColorProfile();
        maxOperatingBrightness = MAX_NIGHTTIME_BRIGHTNESS;
        daylightState = MOONRISE;
      } else if (now.hour() == SUNRISE_HOUR) {
        setDaytimeColorProfile();
        maxOperatingBrightness = MAX_DAYTIME_BRIGHTNESS;
        daylightState = SUNRISE;
      }
      break;
  }
}

void updateBrightness(int brightness) {
  largeRings.setBrightness(brightness);
  smallRing.setBrightness(brightness);
  largeRings.show();
  smallRing.show();
}

void setDaytimeColorProfile() {
  //Large ring full spectrum lighting
  for (int i = 0; i < LARGE_LED_COUNT; i++) {
    if (i % 8 == 0) {
      largeRings.setPixelColor(i, RED);
    } else if ((i + 2) % 8 == 0) {
      largeRings.setPixelColor(i, GREEN);
    } else if ((i + 5) % 8 == 0) {
      // largeRings.setPixelColor(i, BLUE);
      largeRings.setPixelColor(i, WARM_WHITE);
    } else {
      largeRings.setPixelColor(i, WARM_WHITE);
    }
  }

  //Small ring full white
  for (int i = 0; i < SMALL_LED_COUNT; i++) {
    smallRing.setPixelColor(i, WARM_WHITE_GRBW);
  }
}

void setNighttimeColorProfile() {
  //Set both rings to deep blue
  for (int i = 0; i < LARGE_LED_COUNT; i++) largeRings.setPixelColor(i, NIGHTTIME_BLUE);
  for (int i = 0; i < SMALL_LED_COUNT; i++) smallRing.setPixelColor(i, NIGHTTIME_BLUE);
}

void debugCycleLEDs() {
  Serial.println("Debug cycling LEDs...");
  for (int i = 0; i < 24; i++) {
    largeRings.setPixelColor(i, WARM_WHITE);
    // largeRings.setLedColor(i, WHITE);
    largeRings.show();
    delay(75);
  }
  for (int i = 0; i < smallRing.numPixels(); i++) {
    smallRing.setPixelColor(i, WARM_WHITE_GRBW);
    smallRing.show();
    delay(100);
  }
  delay(500);
  largeRings.fill(largeRings.Color(0,0,0));
  largeRings.show();
  smallRing.fill(smallRing.Color(0,0,0));
  smallRing.show();
  delay(500);
  Serial.println("Debug cycling complete.");
}

String getTime() {
  String timeString = String(now.hour()) + ':' + String(now.minute());
  return timeString;
}

String getOverrideState() {
  return String(manualOverrideTriggered);
}

String getDaylightPeriodState() {
  return String(daylightState);
}

