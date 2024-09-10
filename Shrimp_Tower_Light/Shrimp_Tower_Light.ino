#include <Adafruit_NeoPixel.h>
#include "RTClib.h"
#include <esp_wifi.h>
#include "ESPAsyncWebServer.h"

#include <vector>
#include <map>

//LED Ring Configs
#define LARGE_LED_PIN   10
#define LARGE_LED_COUNT 24
#define SMALL_LED_PIN   8
#define SMALL_LED_COUNT 7
const double MAX_SUNLIGHT_BRIGHTNESS {0.4};
const double MAX_MOONLIGHT_BRIGHTNESS {0.08};

//LED Color Hex Codes
uint32_t RED = 0xFF0000;
uint32_t GREEN = 0x00FF00;
uint32_t BLUE = 0x0000FF;
uint32_t NIGHTTIME_BLUE = 0x3300FF;
uint32_t NIGHTTIME_BLUE_GRBW = 0x3300FF00;
uint32_t WHITE = 0xFFFFFF;
uint32_t WARM_WHITE = 0xFFBA6A;
uint32_t WARM_WHITE_GRBW = 0xCFA6FFFF;

//Device Instances
RTC_DS1307 rtc;
char daysOfTheWeek[7][12] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
Adafruit_NeoPixel largeRings(LARGE_LED_COUNT, LARGE_LED_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel smallRing(SMALL_LED_COUNT, SMALL_LED_PIN, NEO_GRBW + NEO_KHZ800);
AsyncWebServer server(80);

//Working (Global) Variables  
double maxOperatingBrightness = 0;
double brightness = 0;
bool manualOverrideTriggered = false;
DateTime now;

//TUNING CONSTANTS
#define MANUAL_OVERRIDE_TIME_MINUTES 15
#define TRANSITION_FADE_TIME_SECONDS 3

#define SUNRISE_HOUR 10
#define SUNSET_HOUR 19
#define MOONRISE_HOUR 23
#define MOONSET_HOUR 2

//WIFI CONSTANTS
const char* ssid = "Shrimpternet Beacon";
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
//Store strings for states for ease of debugging
std::map<DaylightState, String> daylightStateStrings{
  {SUNRISE, "Sunrise"},
  {SUNLIGHT, "Sunlight"},
  {SUNSET, "Sunset"},
  {MOONRISE, "Moonrise"},
  {MOONLIGHT, "Moonlight"},
  {MOONSET, "Moonset"},
  {DARK, "Dark"}
};

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
  WiFi.setTxPower(WIFI_POWER_8_5dBm);

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
  if (now.hour() == SUNRISE_HOUR) {
    daylightState = SUNRISE;
  } else if (now.hour() > SUNRISE_HOUR && now.hour() < SUNSET_HOUR) {
    daylightState = SUNLIGHT;
  } else if (now.hour() == SUNSET_HOUR) {
    daylightState = SUNSET;
  } else if (now.hour() == MOONRISE_HOUR) {
    daylightState = MOONRISE;
  } else if (now.hour() > MOONRISE_HOUR || now.hour() < MOONSET_HOUR) {
    daylightState = MOONLIGHT;
  } else if (now.hour() == MOONSET_HOUR) {
    daylightState = MOONSET;
  } else {
    daylightState = DARK;
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
  Serial.print("IT IS CURRENTLY: ");
  Serial.println(getDaylightPeriodState());

  //Brightness/color update based on daylight period
  switch(daylightState) {
    case SUNRISE:
      setSunlightColorProfile(now.minute() / 60.0f);
      break;
    case SUNLIGHT:
      setSunlightColorProfile(1);
      break;
    case SUNSET:
      setSunlightColorProfile((60 - now.minute()) / 60.0f);
      break;
    case MOONRISE:
      setMoonlightColorProfile(now.minute() / 60.0f);
      break;
    case MOONLIGHT:
      setMoonlightColorProfile(1);
      break;
    case MOONSET:
      setMoonlightColorProfile((60 - now.minute()) / 60.0f);
      break;
    case DARK:
      setDarknessColorProfile();
      break;
  }
  largeRings.show();
  smallRing.show();

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
      if (now.hour() != SUNRISE_HOUR) daylightState = SUNLIGHT;
      break;
    case SUNLIGHT:
      if (now.hour() == SUNSET_HOUR) daylightState = SUNSET;
      break;
    case SUNSET:
      if (now.hour() != SUNSET_HOUR) daylightState = DARK;
      break;
    case MOONRISE:
      if (now.hour() != MOONRISE_HOUR) daylightState = MOONLIGHT;
      break;
    case MOONLIGHT:
      if (now.hour() == MOONSET_HOUR) daylightState = MOONSET;
      break;
    case MOONSET:
      if (now.hour() != MOONSET_HOUR) daylightState = DARK;
      break;
    case DARK:
      if (now.hour() == MOONRISE_HOUR) {
        daylightState = MOONRISE;
      } else if (now.hour() == SUNRISE_HOUR) {
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

void setSunlightColorProfile(double brightnessPercentage) {
  //Large ring full spectrum lighting
  for (int i = 0; i < LARGE_LED_COUNT; i++) {
    if (i % 8 == 0) {
      largeRings.setPixelColor(i, brightnessScaleHex(RED, brightnessPercentage * MAX_SUNLIGHT_BRIGHTNESS));
    } else if ((i + 2) % 8 == 0) {
      largeRings.setPixelColor(i, brightnessScaleHex(GREEN, brightnessPercentage * MAX_SUNLIGHT_BRIGHTNESS));
    } else if ((i + 5) % 8 == 0) {
      // largeRings.setPixelColor(i, BLUE);
      largeRings.setPixelColor(i, brightnessScaleHex(WARM_WHITE, brightnessPercentage * MAX_SUNLIGHT_BRIGHTNESS));
    } else {
      largeRings.setPixelColor(i, brightnessScaleHex(WARM_WHITE, brightnessPercentage * MAX_SUNLIGHT_BRIGHTNESS));
    }
  }

  //Small ring full white
  for (int i = 0; i < SMALL_LED_COUNT; i++) {
    smallRing.setPixelColor(i, brightnessScaleHex(WARM_WHITE_GRBW, brightnessPercentage * MAX_SUNLIGHT_BRIGHTNESS));
  }

  largeRings.show();
  smallRing.show();
}

void setMoonlightColorProfile(double brightnessPercentage) {
  //Set both rings to deep blue
  for (int i = 0; i < LARGE_LED_COUNT; i++) largeRings.setPixelColor(i, brightnessScaleHex(NIGHTTIME_BLUE, brightnessPercentage * MAX_MOONLIGHT_BRIGHTNESS));
  for (int i = 0; i < SMALL_LED_COUNT; i++) smallRing.setPixelColor(i, brightnessScaleHex(NIGHTTIME_BLUE, brightnessPercentage * MAX_MOONLIGHT_BRIGHTNESS));

  largeRings.show();
  smallRing.show();
}

void setDarknessColorProfile() {
  //Turn both rings off
  for (int i = 0; i < LARGE_LED_COUNT; i++) largeRings.setPixelColor(i, 0x000000);
  for (int i = 0; i < SMALL_LED_COUNT; i++) smallRing.setPixelColor(i, 0x00000000);

  largeRings.show();
  smallRing.show();
}

//Scale hex code color according to brightness and return scaled hex code
uint32_t brightnessScaleHex(uint32_t hexColor, double brightnessPercentage) {
  //Store RGB(W) values in a vector for iterative operations
  std::vector<uint8_t> colorValues{};

  //Crack hex code color value into individual byte values
  int numColors = (hexColor > 0xFFFFFF) ? 4 : 3;  //Assumes RGBW colors have r value >0
  for (int i = numColors; i > 0; i--) {
    colorValues.push_back(hexColor >> ((i-1)*8 & 0xFF));  //Use bitmask to extract out each color value
  }

  //Compute brightness percentage and modulate hex code to match it 
  for (int i = 0; i < numColors; i++) colorValues[i] *= brightnessPercentage;

  //Re-pack color values into hex code and return it
  uint32_t modifiedHexColor = 0x0000000;
  for (int i = 0; i < numColors; i++) {
    modifiedHexColor << i*8;
    modifiedHexColor | colorValues[i];
  }
  return modifiedHexColor;
}

void debugCycleLEDs() {
  Serial.println("Debug cycling LEDs...");
  for (int i = 0; i < 24; i++) {
    largeRings.setPixelColor(i, WARM_WHITE);
    // largeRings.setLedColor(i, WHITE);
    largeRings.show();
    delay(100);
  }
  for (int i = 0; i < smallRing.numPixels(); i++) {
    smallRing.setPixelColor(i, WARM_WHITE_GRBW);
    smallRing.show();
    delay(150);
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
  return daylightStateStrings[daylightState];
}

