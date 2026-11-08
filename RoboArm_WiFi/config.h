#pragma once

// Phone control uses the ESP32's OWN WiFi (hotspot).
// You do NOT copy an IP from Serial Monitor.
//
//  1. Phone WiFi list -> join  RoboArm
//  2. Password        ->  roboarm123
//  3. A "Sign in to network" page should open with sliders.
//     If not, open Chrome and type:  http://192.168.4.1
//     (must be http, not https)
//  4. If the page never loads, turn OFF mobile data, then retry.

#define AP_SSID        "RoboArm"
#define AP_PASSWORD    "roboarm123"    // at least 8 characters

#define HTTP_PORT        80
#define WEB_VEL_TIMEOUT  300   // ms: hold-button signal na ele joint thambe
