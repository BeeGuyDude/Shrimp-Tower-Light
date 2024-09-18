#include <Adafruit_NeoPixel.h>
#include "RTClib.h"
#include <esp_wifi.h>
#include "ESPAsyncWebServer.h"

#include <vector>
#include <map>
#include <math.h>

//LED Ring Configs
#define LARGE_LED_PIN   10
#define LARGE_LED_COUNT 24
#define SMALL_LED_PIN   8
#define SMALL_LED_COUNT 7

const double MAX_SUNLIGHT_BRIGHTNESS  {0.4};
const double MAX_MOONLIGHT_BRIGHTNESS {0.03};
const double MAX_OVERRIDE_BRIGHTNESS  {0.3};

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

//Working (Global) Variables  
bool manualOverrideTriggered = false;
DateTime now;
//Colors stored in two discrete vectors to minimize annoying index offsets when dealing with large and small colorVects
std::vector<uint32_t> largeRingsColors(LARGE_LED_COUNT, 0x000000);
std::vector<uint32_t> smallRingColors(SMALL_LED_COUNT, 0x00000000);

//TUNING CONSTANTS
#define MANUAL_OVERRIDE_TIME_MINUTES 10
#define TRANSITION_FADE_TIME_SECONDS 3
#define TRANSITION_FADE_STEPS        50

#define SUNRISE_HOUR 10
#define SUNSET_HOUR 19
#define MOONRISE_HOUR 23
#define MOONSET_HOUR 2

//WIFI CONSTANTS
const char* ssid = "Shrimpternet Beacon";
const char* password = "pimpshrimpin";
const String timeEndpointString = "/time";
const String periodStateEndpointString = "/daylight-period";
const String overrideEndpointString = "/override";

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

  //Ring Initialization
  largeRings.begin();
  smallRing.begin();
  largeRings.setBrightness(255);
  smallRing.setBrightness(255);
  //Following function is light boot cycle, see implementation for details
  debugCycleLEDs();

  //Wifi Initialization
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);
  Serial.println("Wifi Broadcasting...");

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
  server.begin();

  //Test String converter functions for use in HTTP requests
  Serial.println("STRING CONVERTER FUNCTION TESTS");
  Serial.print("Time: ");
  Serial.print(getTime());
  Serial.print(" | Daylight Period: ");
  Serial.print(getDaylightPeriodState());
  Serial.print(" | Override: ");
  Serial.println(getOverrideState());

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

  //Update time from RTC, make sure is valid
  DateTime collectedTime = rtc.now();
  if (collectedTime.year() != 2000) now = collectedTime;

  //Daylight period state check
  updateDaylightPeriod();
  Serial.print("IT IS CURRENTLY: ");
  Serial.println(getDaylightPeriodState());

  updateLightColorStates();
  updateRingsFromVects();

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

//Take largeRingsColors and smallRingColors, feed them to the hardware, and update it
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

  //Small ring full white
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

//Scale hex code color according to brightness and return scaled hex code
uint32_t brightnessScaleHex(uint32_t hexColor, double brightnessPercentage) {
  //Store RGB(W) values in a vector for iterative operations
  std::vector<uint32_t> colorValues{};  //uint32_t used instead of 8 to minimize conversion hassle, yes it's inefficient - sue me.

  //Crack hex code color value into individual byte values
  int numColors = (hexColor > 0xFFFFFF) ? 4 : 3;  //Assumes RGBW colors have r value >0
  for (int i = numColors; i > 0; i--) {
    colorValues.push_back(uint8_t((hexColor >> (i-1)*8) & 0xFF));  //Use bitmask to extract out each color value
  }

  //Compute brightness percentage and modulate hex code to match it 
  for (int i = 0; i < numColors; i++) colorValues[i] *= brightnessPercentage;

  //Re-pack color values into hex code and return it
  uint32_t modifiedHexColor = 0x0000000;
  for (int i = 0; i < numColors; i++) {
    modifiedHexColor = modifiedHexColor << 8;
    modifiedHexColor |= colorValues[i];
  }
  return modifiedHexColor;
}

std::vector<uint32_t> crackHexCodeChannels(uint32_t hexColor, int numChannels) {
  std::vector<uint32_t> colorChannels;
  if (numChannels == 3) {   //RGB values, byteshift by one: (i-1)*8
    for (int i = numChannels; i > 0; i--) colorChannels.push_back(uint8_t((hexColor >> (i-1)*8) & 0xFF));  //Use bitmask to extract out each color value
  } else {    //RGBW values, crunch normally: (i)*8
    for (int i = numChannels; i > 0; i--) colorChannels.push_back(uint8_t((hexColor >> (i)*8) & 0xFF));  //Use bitmask to extract out each color value
  }
  return colorChannels;
}

std::vector<std::vector<uint32_t>> crackHexVectChannels(std::vector<uint32_t> hexColorVect, int numChannels) {
  std::vector<std::vector<uint32_t>> colorChannelsVect;
  for (int i = 0; i < hexColorVect.size(); i++) colorChannelsVect.push_back(crackHexCodeChannels(hexColorVect[i], numChannels));
  return colorChannelsVect;
}

//Fade from current color map to pair of RGB and RGBW hex codes
void fadeToColor(uint32_t hexColorRGB, uint32_t hexColorRGBW, float fadeSeconds) {
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
    Serial.println("SMALL RINGS FADED");

    //Update both rings, and delay for step time increment
    largeRings.show();
    smallRing.show();
    delay(fadeSeconds / TRANSITION_FADE_STEPS * 1000);   //Fade time given as a float value, multiply by 1000 for milliseconds()
  }
}

//Fade from current color map to pair of uint32_t vectors
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
    Serial.println("SMALL RINGS FADED");

    //Update both rings, and delay for step time increment
    largeRings.show();
    smallRing.show();
    delay(fadeSeconds / TRANSITION_FADE_STEPS * 1000);   //Fade time given as a float value, multiply by 1000 for milliseconds()
  }
  
}

void debugCycleLEDs() {
  Serial.println("Debug cycling LEDs...");
  //Initial circular boot cycle
  for (int i = 0; i < 24; i++) {
    largeRings.setPixelColor(i, WARM_WHITE);
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

String getOverrideState() {
  return String(manualOverrideTriggered);
}

String getDaylightPeriodState() {
  return daylightStateStrings[daylightState];
}

