# I just want to look at my shrimp.

This code was designed to run 24/7 on the light system I designed for sitting on top of my freshwater shrimp tank, and automate its operation with [the potential for user input](https://github.com/BeeGuyDude/Shrimp-Tower-Remote) \(by myself or my roommates). I 
[designed](https://cad.onshape.com/documents/7b9dc8e770756261066fab7a/w/9f3352857822c9e6814c799b/e/d9b55f73d2ba840edf661dc1?renderMode=0&uiState=6706f7fdfc9cb44254835a6c) and 3D printed a frame, chose the electronics, soldered the system together, and implemented
the control logic. 

### System hardware
- An ESP32-C3 breakout
- A DS1307 RTC breakout
- A 3.3 to 5v level shifter for LED signalling
- 4x 24 WS2812b RGB LED PCB rings
- 1x 7 WS2812b RGBW LED PCB disc
- A butchered USB cable for 5v input, with an inline 2-pin JST quick-disconnect

### System software features
- Wifi network projection and simple HTTP get request processing
- Time-based state transitions for day and night lighting, including sunrise/sunset and moonrise/moonset fading
- An active override capability to manually turn on the lights to a preset daylight brightness
- A custom RGBW hex color processing system for color differential calculation
- Smooth configurable transitions between color states utilizing the above

### Future features
- Temperature sensor reporting for winter months
- Float switch based warnings for evaporation
- Planned iteration on color system to use C++ structs with inbuilt diff calculation

