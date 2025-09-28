#include <Adafruit_NeoPixel.h>
#include "RTClib.h"
#include <esp_wifi.h>
#include "ESPAsyncWebSrv.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Discord_WebHook.h>

#include <vector>
#include <map>
#include <math.h>

//LED Ring Configs
#define LARGE_LED_PIN   10
#define LARGE_LED_COUNT 24
#define SMALL_LED_PIN   8
#define SMALL_LED_COUNT 7

//Sensor Pin Configs
#define THERMAL_PROBE_PIN 4
#define FLOAT_SWITCH_PIN 21

const double MAX_SUNLIGHT_BRIGHTNESS  {0.4};
const double MAX_MOONLIGHT_BRIGHTNESS {0.03};
const double MAX_OVERRIDE_BRIGHTNESS  {0.2};

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

AsyncWebServer server(80);  //I tried to make this port 69 but HTTP got mad at me, boowomp

OneWire oneWire(THERMAL_PROBE_PIN);
DallasTemperature sensors(&oneWire);
DeviceAddress probe;
#define TEMPERATURE_PRECISION 12  //This is in raw bits for temperature dumps off the data bus

//Working (Global) Variables  
bool manualOverrideRequested = false; //Stores button presses
bool manualOverrideTriggered = false; //Stores current override state
DateTime now;
DateTime overrideEndTime;

//Sensor variables
bool isWaterBelowThreshold;
float tempF;

//Colors stored in two discrete vectors to minimize annoying index offsets when dealing with large and small colorVects
std::vector<uint32_t> largeRingsColors(LARGE_LED_COUNT, 0x000000);
std::vector<uint32_t> smallRingColors(SMALL_LED_COUNT, 0x00000000);

//TUNING CONSTANTS
#define MANUAL_OVERRIDE_TIME_MINUTES 5
#define TRANSITION_FADE_TIME_SECONDS 3
#define TRANSITION_FADE_STEPS        100
#define OVERRIDE_REQUEST_RETURN_STRING "it's that shrimple"

#define SUNRISE_HOUR 10
#define SUNSET_HOUR 19
#define MOONRISE_HOUR 23
#define MOONSET_HOUR 2

//WIFI CONSTANTS
const char* ssid = "Shrimpternet Beacon";
const char* password = "pimpshrimpin";
const char* external_ssid = "this is just";
const char* external_password = "my actual wifi lol";
const char* timeEndpointString = "/time";
const char* periodStateEndpointString = "/daylight-period";
const char* overrideEndpointString = "/override";
const char* overrideEndpointSetString = "/override-set";

//Discord Webhook handler instance and variables
Discord_Webhook pollWebhook, eventWebhook;
String poll_webhook_id = "haha";
String poll_webhook_token = "you";
String event_webhook_id = "really";
String event_webhook_token = "thought";
bool webhookJustTriggered = false;

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
  Serial.begin(9600);
  // while (!Serial);
  Serial.println("BEGINNING STARTUP");

  //RTC configuration
  if (!rtc.begin()) {
    Serial.println("System has failed to grasp the concept of time, please introduce it to organized religion (or Descartes)");
  }
  now = rtc.now();

  //Thermal probe configuration
  sensors.begin();
  Serial.print("Locating devices...");
  Serial.print("Found ");
  Serial.print(sensors.getDeviceCount(), DEC);
  Serial.println(" devices.");
  if (!sensors.getAddress(probe, 0)) {
    Serial.println("Unable to find address for Device 0");
  } else {
    Serial.println("Found thermal probe device successfully!");
  }
  sensors.setResolution(probe, TEMPERATURE_PRECISION);

  //Waterline state initialization
  pinMode(FLOAT_SWITCH_PIN, INPUT);
  isWaterBelowThreshold = digitalRead(FLOAT_SWITCH_PIN);

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

  //Ring Initialization
  largeRings.begin();
  smallRing.begin();
  largeRings.setBrightness(255);
  smallRing.setBrightness(255);
  //Following function is light boot cycle, see implementation for details
  debugCycleLEDs();

  //Flush any lingering wifi settings
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  //Configure for station and access point 
  WiFi.mode(WIFI_AP_STA);
  
  //Configure station and connect to apartment wifi, and send status update via webhook once complete
  Serial.print("Connecting to SSID: ");
  Serial.println(external_ssid);
  WiFi.useStaticBuffers(true);
  WiFi.begin(external_ssid, external_password);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected!");
  eventWebhook.begin(event_webhook_id, event_webhook_token);
  pollWebhook.begin(poll_webhook_id, poll_webhook_token);
  eventWebhook.send("Shrimpternet Beacon has connected to the internet after reset!");
  pollWebhook.send("Shrimpternet Beacon has connected to the internet after reset!");

  //With outgoing comms configured, initialize local network for remote connection and overrides
  WiFi.softAP(ssid, password);
  Serial.println("Wifi Broadcasting...");
  Serial.println("");
  Serial.print("IP Address: ");
  Serial.println(WiFi.softAPIP());
  eventWebhook.send("Shrimpternet Beacon is now projecting tap network! - IP: [" + WiFi.softAPIP().toString() + "]");

  //WebServer HTTP request handling
  server.on(timeEndpointString, HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/plain", getTime().c_str());
  });
  server.on(periodStateEndpointString, HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/plain", getDaylightPeriodState().c_str());
  });
  server.begin();
  server.on(overrideEndpointString, HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/plain", getOverrideTime().c_str());
  });
  //This request is unique in that it returns information but also sets a system flag, see setOverrideTime() method
  server.on(overrideEndpointSetString, HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/plain", setOverrideTime().c_str());
  });

  //Test string converter functions for use in HTTP requests
  Serial.println("STRING CONVERTER FUNCTION TESTS");
  Serial.print("Time: ");
  Serial.print(getTime());
  Serial.print(" | Daylight Period: ");
  Serial.print(getDaylightPeriodState());
  Serial.print(" | Override: ");
  Serial.println(getOverrideTime());

  //Test Hex color code converter functionality
  Serial.println("HEX CONVERTER FUNCTION TESTS");
  Serial.print("Warm White: ");
  Serial.print(WARM_WHITE);
  Serial.print(" | WARM WHITE 50%: ");
  Serial.println(brightnessScaleHex(WARM_WHITE, 0.5));

  Serial.println("STARTUP COMPLETED");
  Serial.println("");
}

void loop() {
  //Poll sensors and update internal variables
  isWaterBelowThreshold = digitalRead(FLOAT_SWITCH_PIN);
  sensors.requestTemperatures();
  tempF = sensors.getTempF(probe);
  Serial.println("==== SENSOR POLL ====");
  Serial.println("Temp: " + String(tempF) + "°F");
  Serial.println("Float Switch: " + String(isWaterBelowThreshold));
  Serial.println("=====================");

  //Update time from RTC, make sure is valid (If voltage brownout occurs, time will return 00:00 01/01/2000)
  DateTime collectedTime = rtc.now();
  if (collectedTime.year() != 2000) now = collectedTime;
  Serial.print("Time: ");
  Serial.print(now.hour(), DEC);
  Serial.print(":");
  Serial.print(now.minute(), DEC);
  Serial.print(":");
  Serial.println(now.second(), DEC);
  Serial.println("=====================");

  //Check if sensor update period has elapsed, and update information and post to webhook if so
  if ((now.minute() % 20) == 0) {   //Post every 20 minutes, update this to be configurable
    if (!webhookJustTriggered) {
      String nowString = String(now.hour()) + ":" + String(now.minute()) + ":" + String(now.second());
      pollWebhook.sendEmbed("Current RTC Time", nowString, "F205DB");
      pollWebhook.sendEmbed("Temperature (°F)", String(tempF), "F205DB"); //Hot pink embed
      String waterLevelString = isWaterBelowThreshold ? "LOW!" : "Nominal.";
      pollWebhook.sendEmbed("Water Level", waterLevelString, "F205DB"); //Hot pink embed
      webhookJustTriggered = true;
    }
  } else {
    webhookJustTriggered = false;
  }

  //Daylight period state update
  updateDaylightPeriod();
  Serial.print("IT IS CURRENTLY: ");
  Serial.println(getDaylightPeriodState());

  //Color state vects will always be updated periodically, hardware setting is handled below
  updateLightColorStates();

  //If override requested, fade to override brightness
  if (manualOverrideRequested) {
    if (!manualOverrideTriggered) {
      //Inside check so as to only send an event notification on a new override request
      eventWebhook.send("Someone has requested an override of the lighting system!");
      
      //Fade to override color if request is fresh
      setSunlightColorProfile(MAX_OVERRIDE_BRIGHTNESS);
      fadeToVect(largeRingsColors, smallRingColors, TRANSITION_FADE_TIME_SECONDS);
      manualOverrideTriggered = true;
    } 
    overrideEndTime = now + TimeSpan(0, 0, MANUAL_OVERRIDE_TIME_MINUTES, 0);
    manualOverrideRequested = false;
  }
  Serial.print("MANUAL OVERRIDE REQUESTED: ");
  Serial.println(manualOverrideTriggered);

  //Update lights based on if manual override is in progress or not
  if (!manualOverrideTriggered) { //If no override is triggered, update lights as per standard
    Serial.println("UPDATING RINGS FROM COLOR VECTORS");
    updateRingsFromVects();
  } else {                        //Override triggered, check if it has ended
    //Assume lights have already been faded to override setting; fade back down if ended
    if (now > overrideEndTime) {
      fadeToVect(largeRingsColors, smallRingColors, TRANSITION_FADE_TIME_SECONDS);
      manualOverrideTriggered = false;
    }
  }

  //Update period delay
  delay(1000);
  Serial.println("");
}

//Process state transition given current daylight state
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
      //Dark state occurs both between the (Day --> Night) and (Night --> Day) transitions; thus, it needs edge transitions for both cases
      if (now.hour() == MOONRISE_HOUR) {
        daylightState = MOONRISE;
      } else if (now.hour() == SUNRISE_HOUR) {
        daylightState = SUNRISE;
      }
      break;
  }
}

//Set normal operation color map vectors based on the current daylight period
void updateLightColorStates() {
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
}

//Take largeRingsColors and (normal operation color state vectors) feed them to the hardware, and update it
void updateRingsFromVects() { 
  for (int i = 0; i < LARGE_LED_COUNT; i++) largeRings.setPixelColor(i, largeRingsColors[i]);
  for (int i = 0; i < SMALL_LED_COUNT; i++) smallRing.setPixelColor(i, smallRingColors[i]);
  largeRings.show();
  smallRing.show();
}

//Fill color vects with white interspersed with red and green pixels for full spectrum lighting
void setSunlightColorProfile(double brightnessPercentage) {
  //Large ring full spectrum lighting
  for (int i = 0; i < LARGE_LED_COUNT; i++) {
    if (i % 8 == 0) {
      largeRingsColors[i] = brightnessScaleHex(RED, brightnessPercentage * MAX_SUNLIGHT_BRIGHTNESS);
    } else if ((i + 2) % 8 == 0) {
      largeRingsColors[i] = brightnessScaleHex(GREEN, brightnessPercentage * MAX_SUNLIGHT_BRIGHTNESS);
    } else if ((i + 5) % 8 == 0) {
      // largeRings.setPixelColor(i, BLUE);
      largeRingsColors[i] = brightnessScaleHex(WARM_WHITE, brightnessPercentage * MAX_SUNLIGHT_BRIGHTNESS);
    } else {
      largeRingsColors[i] = brightnessScaleHex(WARM_WHITE, brightnessPercentage * MAX_SUNLIGHT_BRIGHTNESS);
    }
  }

  //Small ring full white (as it has warm white built-in LEDs)
  for (int i = 0; i < SMALL_LED_COUNT; i++) smallRingColors[i] = brightnessScaleHex(WARM_WHITE_GRBW, brightnessPercentage * MAX_SUNLIGHT_BRIGHTNESS);
}

//Fill color vects with purple-y color for moonlight mode
void setMoonlightColorProfile(double brightnessPercentage) {
  for (int i = 0; i < LARGE_LED_COUNT; i++) largeRingsColors[i] = brightnessScaleHex(NIGHTTIME_BLUE, brightnessPercentage * MAX_MOONLIGHT_BRIGHTNESS);
  for (int i = 0; i < SMALL_LED_COUNT; i++) smallRingColors[i] = brightnessScaleHex(NIGHTTIME_BLUE_GRBW, brightnessPercentage * MAX_MOONLIGHT_BRIGHTNESS);
}

//Fill color vects with 0s (off)
void setDarknessColorProfile() {
  //Turn both rings off
  for (int i = 0; i < LARGE_LED_COUNT; i++) largeRingsColors[i] = 0x000000;  //RGB 0
  for (int i = 0; i < SMALL_LED_COUNT; i++) smallRingColors[i] = 0x00000000; //RGBW 0
}

//Crack a given uint32_t RGB Hex code and split it into 3/4 channels stored as a vector, in order
std::vector<uint32_t> crackHexCodeChannels(uint32_t hexColor, int numChannels) {
  std::vector<uint32_t> colorChannels;
  if (numChannels == 3) {   //RGB values, byteshift by one: (i-1)*8
    for (int i = numChannels; i > 0; i--) colorChannels.push_back(uint8_t((hexColor >> (i-1)*8) & 0xFF));  //Use bitmask to extract out each color value
  } else {    //RGBW values, crunch normally: (i)*8
    for (int i = numChannels; i > 0; i--) colorChannels.push_back(uint8_t((hexColor >> (i)*8) & 0xFF));  //Use bitmask to extract out each color value
  }
  return colorChannels;
}

//Crack a vector of uint32_t RGB Hex codes via the above function, and return a vector of vectors of their channels
std::vector<std::vector<uint32_t>> crackHexVectChannels(std::vector<uint32_t> hexColorVect, int numChannels) {
  std::vector<std::vector<uint32_t>> colorChannelsVect;
  for (int i = 0; i < hexColorVect.size(); i++) colorChannelsVect.push_back(crackHexCodeChannels(hexColorVect[i], numChannels));
  return colorChannelsVect;
}

//Fade from current HARDWARE color map to pair of RGB and RGBW hex codes
void fadeToColor(uint32_t hexColorRGB, uint32_t hexColorRGBW, float fadeSeconds) {
  //TODO: Refactor this to just populate some vectors with their respective hex codes and call fadeToVect()
  
  //Dump current hardware color vectors to internal one for processing
  //Yes, there is a method to do this directly; it's an array of uint8_t-s, and that is not worth my time to deal with.
  std::vector<uint32_t> hardwareColorVectLarge;
  std::vector<uint32_t> hardwareColorVectSmall;
  for (int i = 0; i < LARGE_LED_COUNT; i++) hardwareColorVectLarge.push_back(largeRings.getPixelColor(i));
  for (int i = 0; i < SMALL_LED_COUNT; i++) hardwareColorVectSmall.push_back(smallRing.getPixelColor(i));
  Serial.println("HARDWARE COLORS DUMPED");

  //Split RGB hex code and existing hardware vect into individual channel values to be used for crunching diffs
  std::vector<uint32_t> hexColorChannelsRGB = crackHexCodeChannels(hexColorRGB, 3);
  std::vector<uint32_t> hexColorChannelsRGBW = crackHexCodeChannels(hexColorRGBW, 4);
  std::vector<std::vector<uint32_t>> hardwareColorChannelsLarge = crackHexVectChannels(hardwareColorVectLarge, 3);
  std::vector<std::vector<uint32_t>> hardwareColorChannelsSmall = crackHexVectChannels(hardwareColorVectSmall, 4);  
  Serial.println("CHANNELS CRACKED");

  //Store color channel diffs as vector of vectors
  std::vector<std::vector<int>> channelDiffsVectLarge;
  std::vector<std::vector<int>> channelDiffsVectSmall;
  std::vector<std::vector<bool>> channelDiffsNegVectLarge;  //Bool indicates whether a - sign is present in front of diff
  std::vector<std::vector<bool>> channelDiffsNegVectSmall;

  //Calculate diffs by cracking them into |diff| and sign values
  for (int colorIndex = 0; colorIndex < LARGE_LED_COUNT; colorIndex++) {
    //Crack channel diffs and signs at the same time
    std::vector<int> channelDiffs;
    std::vector<bool> channelNegs;
    for (int channel = 0; channel < 3; channel++) {
      if (hexColorChannelsRGB[channel] > hardwareColorChannelsLarge[colorIndex][channel]) {   //Positive channel diff
        channelDiffs.push_back(hexColorChannelsRGB[channel] - hardwareColorChannelsLarge[colorIndex][channel]);
        channelNegs.push_back(false);
      } else {
        channelDiffs.push_back(hardwareColorChannelsLarge[colorIndex][channel] - hexColorChannelsRGB[channel]);
        channelNegs.push_back(true);
      }
    }
    //Push vector of channel diffs and vector of diff signs to 2dim diff vector at same time
    channelDiffsVectLarge.push_back(channelDiffs);
    channelDiffsNegVectLarge.push_back(channelNegs);
  }
  //Repeat above for small ring
  for (int colorIndex = 0; colorIndex < SMALL_LED_COUNT; colorIndex++) {
    //Crack channel diffs and signs at the same time
    std::vector<int> channelDiffs;
    std::vector<bool> channelNegs;
    for (int channel = 0; channel < 4; channel++) {
      if (hexColorChannelsRGBW[channel] > hardwareColorChannelsSmall[colorIndex][channel]) {   //Positive channel diff
        channelDiffs.push_back(hexColorChannelsRGBW[channel] - hardwareColorChannelsSmall[colorIndex][channel]);
        channelNegs.push_back(false);
      } else {
        channelDiffs.push_back(hardwareColorChannelsSmall[colorIndex][channel] - hexColorChannelsRGBW[channel]);
        channelNegs.push_back(true);
      }
    }
    //Push vector of channel diffs and vector of diff signs to 2dim diff vector at same time
    channelDiffsVectSmall.push_back(channelDiffs);
    channelDiffsNegVectSmall.push_back(channelNegs);
  }

  //Iteratively fade by adding diffs to hardware values over configured amount of time
  for (int step = 1; step <= TRANSITION_FADE_STEPS; step++) {
    //Dump colors and fade diffs to both sets of rings
    for (int i = 0; i < LARGE_LED_COUNT; i++) {
      //largeRings.Color() compiles RGB value into uint32_t, could technically be done iteratively but I'm lazy and have been programming this method for 3 hours
      largeRings.setPixelColor(i, largeRings.Color(
        hardwareColorChannelsLarge[i][0] + (float)((channelDiffsVectLarge[i][0] * pow(-1, (int)channelDiffsNegVectLarge[i][0])) / TRANSITION_FADE_STEPS) * (float)step,
        hardwareColorChannelsLarge[i][1] + (float)((channelDiffsVectLarge[i][1] * pow(-1, (int)channelDiffsNegVectLarge[i][1])) / TRANSITION_FADE_STEPS) * (float)step,
        hardwareColorChannelsLarge[i][2] + (float)((channelDiffsVectLarge[i][2] * pow(-1, (int)channelDiffsNegVectLarge[i][2])) / TRANSITION_FADE_STEPS) * (float)step
      ));
      //TODO: Make this absolute brick slightly more reasonable with a helper function
    }
    for (int i = 0; i < SMALL_LED_COUNT; i++) {
      //largeRings.Color() compiles RGB value into uint32_t, could technically be done iteratively but I'm lazy and have been programming this method for 3 hours
      smallRing.setPixelColor(i, smallRing.Color(
        hardwareColorChannelsSmall[i][0] + (float)((channelDiffsVectSmall[i][0] * pow(-1, (int)channelDiffsNegVectSmall[i][0])) / TRANSITION_FADE_STEPS) * (float)step,
        hardwareColorChannelsSmall[i][1] + (float)((channelDiffsVectSmall[i][1] * pow(-1, (int)channelDiffsNegVectSmall[i][1])) / TRANSITION_FADE_STEPS) * (float)step,
        hardwareColorChannelsSmall[i][2] + (float)((channelDiffsVectSmall[i][2] * pow(-1, (int)channelDiffsNegVectSmall[i][2])) / TRANSITION_FADE_STEPS) * (float)step,
        hardwareColorChannelsSmall[i][3] + (float)((channelDiffsVectSmall[i][3] * pow(-1, (int)channelDiffsNegVectSmall[i][3])) / TRANSITION_FADE_STEPS) * (float)step
      )); //God help your soul if you have to debug the above statements
    }
    // Serial.println("SMALL RINGS FADED");

    //Update both rings, and delay for step time increment
    largeRings.show();
    smallRing.show();
    delay(fadeSeconds / TRANSITION_FADE_STEPS * 1000);   //Fade time given as a float value, multiply by 1000 for milliseconds()
  }
}  //Crack hex code color value into individual byte values

//Scale hex code color according to brightness and return scaled hex code
uint32_t brightnessScaleHex(uint32_t hexColor, double brightnessPercentage) {
  //Store RGB(W) values in a vector for iterative operations
  std::vector<uint32_t> colorValues{};  //uint32_t used instead of 8 to minimize conversion hassle, yes it's inefficient - sue me.
  int numChannels = (hexColor > 0xFFFFFF) ? 4 : 3;  //Assumes RGBW colors have r value >0
  colorValues = crackHexCodeChannels(hexColor, numChannels);

  //Compute brightness percentage and modulate hex code to match it 
  for (int i = 0; i < numChannels; i++) colorValues[i] *= brightnessPercentage;

  //Re-pack color values into hex code and return it
  uint32_t modifiedHexColor = 0x0000000;
  for (int i = 0; i < numChannels; i++) {  //Crack hex code color value into individual byte values
    modifiedHexColor = modifiedHexColor << 8;
    modifiedHexColor |= colorValues[i];
  }
  return modifiedHexColor;
}

//Fade from current HARDWARE color map to pair of uint32_t vectors
void fadeToVect(std::vector<uint32_t> hexColorVectLarge, std::vector<uint32_t> hexColorVectSmall, float fadeSeconds) {
  //Dump current hardware color vectors to internal one for processing
  //Yes, there is a method to do this directly; it's an array of uint8_t-s, and that is not worth my time to deal with.
  std::vector<uint32_t> hardwareColorVectLarge;
  std::vector<uint32_t> hardwareColorVectSmall;
  for (int i = 0; i < LARGE_LED_COUNT; i++) hardwareColorVectLarge.push_back(largeRings.getPixelColor(i));
  for (int i = 0; i < SMALL_LED_COUNT; i++) hardwareColorVectSmall.push_back(smallRing.getPixelColor(i));
  Serial.println("HARDWARE COLORS DUMPED");

  //Split RGB hex code and existing hardware vect into individual channel values to be used for crunching diffs
  std::vector<std::vector<uint32_t>> hexVectColorChannelsRGB = crackHexVectChannels(hexColorVectLarge, 3);
  std::vector<std::vector<uint32_t>> hexVectColorChannelsRGBW = crackHexVectChannels(hexColorVectSmall, 4);
  std::vector<std::vector<uint32_t>> hardwareColorChannelsLarge = crackHexVectChannels(hardwareColorVectLarge, 3);
  std::vector<std::vector<uint32_t>> hardwareColorChannelsSmall = crackHexVectChannels(hardwareColorVectSmall, 4);  
  Serial.println("CHANNELS CRACKED");

  //Store color channel diffs as vector of vectors
  std::vector<std::vector<int>> channelDiffsVectLarge;
  std::vector<std::vector<int>> channelDiffsVectSmall;
  std::vector<std::vector<bool>> channelDiffsNegVectLarge;  //Bool indicates whether a - sign is present in front of diff
  std::vector<std::vector<bool>> channelDiffsNegVectSmall;

  //Calculate diffs by cracking them into |diff| and sign values
  for (int colorIndex = 0; colorIndex < LARGE_LED_COUNT; colorIndex++) {
    //Crack channel diffs and signs at the same time
    std::vector<int> channelDiffs;
    std::vector<bool> channelNegs;
    for (int channel = 0; channel < 3; channel++) {
      if (hexVectColorChannelsRGB[colorIndex][channel] > hardwareColorChannelsLarge[colorIndex][channel]) {   //Positive channel diff
        channelDiffs.push_back(hexVectColorChannelsRGB[colorIndex][channel] - hardwareColorChannelsLarge[colorIndex][channel]);
        channelNegs.push_back(false);
      } else {
        channelDiffs.push_back(hardwareColorChannelsLarge[colorIndex][channel] - hexVectColorChannelsRGB[colorIndex][channel]);
        channelNegs.push_back(true);
      }
    }
    //Push vector of channel diffs and vector of diff signs to 2dim diff vector at same time
    channelDiffsVectLarge.push_back(channelDiffs);
    channelDiffsNegVectLarge.push_back(channelNegs);
  }
  //Repeat above for small ring
  for (int colorIndex = 0; colorIndex < SMALL_LED_COUNT; colorIndex++) {
    //Crack channel diffs and signs at the same time
    std::vector<int> channelDiffs;
    std::vector<bool> channelNegs;
    for (int channel = 0; channel < 4; channel++) {
      if (hexVectColorChannelsRGBW[colorIndex][channel] > hardwareColorChannelsSmall[colorIndex][channel]) {   //Positive channel diff
        channelDiffs.push_back(hexVectColorChannelsRGBW[colorIndex][channel] - hardwareColorChannelsSmall[colorIndex][channel]);
        channelNegs.push_back(false);
      } else {
        channelDiffs.push_back(hardwareColorChannelsSmall[colorIndex][channel] - hexVectColorChannelsRGBW[colorIndex][channel]);
        channelNegs.push_back(true);
      }
    }
    //Push vector of channel diffs and vector of diff signs to 2dim diff vector at same time
    channelDiffsVectSmall.push_back(channelDiffs);
    channelDiffsNegVectSmall.push_back(channelNegs);
  }

  //Iteratively fade by adding diffs to hardware values over configured amount of time
  for (int step = 1; step <= TRANSITION_FADE_STEPS; step++) {
    //Dump colors and fade diffs to both sets of rings
    for (int i = 0; i < LARGE_LED_COUNT; i++) {
      //largeRings.Color() compiles RGB value into uint32_t, could technically be done iteratively but I'm lazy and have been programming this method for 3 hours
      largeRings.setPixelColor(i, largeRings.Color(
        hardwareColorChannelsLarge[i][0] + (float)((channelDiffsVectLarge[i][0] * pow(-1, (int)channelDiffsNegVectLarge[i][0])) / TRANSITION_FADE_STEPS) * (float)step,
        hardwareColorChannelsLarge[i][1] + (float)((channelDiffsVectLarge[i][1] * pow(-1, (int)channelDiffsNegVectLarge[i][1])) / TRANSITION_FADE_STEPS) * (float)step,
        hardwareColorChannelsLarge[i][2] + (float)((channelDiffsVectLarge[i][2] * pow(-1, (int)channelDiffsNegVectLarge[i][2])) / TRANSITION_FADE_STEPS) * (float)step
      ));
    }
    for (int i = 0; i < SMALL_LED_COUNT; i++) {
      //largeRings.Color() compiles RGB value into uint32_t, could technically be done iteratively but I'm lazy and have been programming this method for 3 hours
      smallRing.setPixelColor(i, smallRing.Color(
        hardwareColorChannelsSmall[i][0] + (float)((channelDiffsVectSmall[i][0] * pow(-1, (int)channelDiffsNegVectSmall[i][0])) / TRANSITION_FADE_STEPS) * (float)step,
        hardwareColorChannelsSmall[i][1] + (float)((channelDiffsVectSmall[i][1] * pow(-1, (int)channelDiffsNegVectSmall[i][1])) / TRANSITION_FADE_STEPS) * (float)step,
        hardwareColorChannelsSmall[i][2] + (float)((channelDiffsVectSmall[i][2] * pow(-1, (int)channelDiffsNegVectSmall[i][2])) / TRANSITION_FADE_STEPS) * (float)step,
        hardwareColorChannelsSmall[i][3] + (float)((channelDiffsVectSmall[i][3] * pow(-1, (int)channelDiffsNegVectSmall[i][3])) / TRANSITION_FADE_STEPS) * (float)step
      )); //God help your soul if you have to debug the above statements
    }
    // Serial.println("SMALL RINGS FADED");

    //Update both rings, and delay for step time increment
    largeRings.show();
    smallRing.show();
    delay(fadeSeconds / TRANSITION_FADE_STEPS * 1000);   //Fade time given as a float value, multiply by 1000 for milliseconds()
  }
  
}

//Turn on LEDs and cycle through all available profiles, and then back off again
void debugCycleLEDs() {
  Serial.println("Debug cycling LEDs...");
  //Animate the LEDs turning on in a circle, for shiggles
  for (int i = 0; i < 24; i++) {
    largeRings.setPixelColor(i, brightnessScaleHex(WARM_WHITE, MAX_OVERRIDE_BRIGHTNESS));
    // largeRings.setLedColor(i, WHITE);
    largeRings.show();
    delay(100);
  }
  for (int i = 0; i < smallRing.numPixels(); i++) {
    smallRing.setPixelColor(i, WARM_WHITE_GRBW);
    smallRing.show();
    delay(100);
  }
  delay(750);

  //Fade to sunlight profile
  setSunlightColorProfile(1);
  fadeToVect(largeRingsColors, smallRingColors, 1.5);
  delay(750);

  //Fade to moonlight profile
  setMoonlightColorProfile(1);
  fadeToVect(largeRingsColors, smallRingColors, 1.5);
  delay(750);

  //Fade off
  setDarknessColorProfile();
  fadeToVect(largeRingsColors, smallRingColors, 1.5);
  delay(750);

  //Update color profile from actual state and slowly fade to it
  updateLightColorStates();
  fadeToVect(largeRingsColors, smallRingColors, 3);
  delay(250);  

  Serial.println("Debug cycling complete.");
}

String getTime() {
  String timeString = String(now.hour()) + ':' + String(now.minute());
  return timeString;
}

String getOverrideTime() {
  String overrideString;
  if (manualOverrideTriggered) {
    TimeSpan timeRemaining = overrideEndTime - now;
    overrideString = String(timeRemaining.minutes()) + ':' + String(timeRemaining.seconds());
  } else {
    overrideString = "Disabled";
  }
  return String(overrideString);
}

String setOverrideTime() {
  manualOverrideRequested = true;
  return OVERRIDE_REQUEST_RETURN_STRING;
}

String getDaylightPeriodState() {
  if (!manualOverrideTriggered) {
    return daylightStateStrings[daylightState];
  } else {
    return "!OVERRIDE!";
  }
}

