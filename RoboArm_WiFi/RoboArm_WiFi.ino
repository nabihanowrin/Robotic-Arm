// =====================================================================
//  RoboArm WiFi - ESP32 4-DOF robotic arm
//  Control: 4 physical joysticks  +  phone slider page
//
//  Library: ESP32Servo (Library Manager)
//
//  Phone (no IP copy from Serial):
//    1. Join WiFi named  RoboArm   password  roboarm123
//    2. "Sign in to network" should show sliders
//    3. If not, Chrome -> http://192.168.4.1   (http, NOT https)
//    4. Turn OFF mobile data if the page will not load
// =====================================================================
#include <ESP32Servo.h>
#include <math.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>

#include "config.h"
#include "web_page.h"

// Servo 1 = Base, 2 = Shoulder, 3 = Elbow, 4 = Gripper
enum Joint { BASE = 0, SHOULDER = 1, ELBOW = 2, GRIPPER = 3 };
const int NUM_JOINTS = 4;

Servo servos[NUM_JOINTS];

//                                 Base Shoulder Elbow Gripper
const int joyPins[NUM_JOINTS]   = {34,   35,     32,   33};   // ADC1 -> WiFi-r shathe kaj kore
const int servoPins[NUM_JOINTS] = {18,   19,     21,   22};

// ================= LIMITS (ekhon ei value-i ashol limit) =================
const float minAngle[NUM_JOINTS] = {5,   10,  8,  10};
const float maxAngle[NUM_JOINTS] = {145, 80, 82,  50};

// Home position
const float homeAngle[NUM_JOINTS] = {75, 45, 45, 29};

const bool invertJoy[NUM_JOINTS]   = {false, false, false, false};
const bool mirrorServo[NUM_JOINTS] = {false, false, true, false};

// ================= SPEED / DEADBAND (per joint) =================
//                                        Base Shoulder Elbow Gripper
const float maxSpeed[NUM_JOINTS]      = {2.0,  1.2,   1.2,  1.5};
const int   pulseDeadband[NUM_JOINTS] = {6,    10,    10,   8};

const int PULSE_MIN = 600;
const int PULSE_MAX = 2300;

// ================= JOYSTICK TUNING =================
const int   START_DEADZONE = 400;    // WiFi noise-e drift korle ektu barao (e.g. 450)
const int   STOP_DEADZONE  = 250;
const float ALPHA          = 0.12;   // kom = smooth
const float SERVO_SMOOTH   = 0.20;
const float SNAP_DIST      = 0.4;    // target-er kachhe ele snap
const int   LOOP_MS        = 20;
const int   NUM_SAMPLES    = 16;

// Joystick range auto-learn
const float START_EXTENT = 1200.0;   // shuru-te dhora range (center theke)
const float MIN_EXTENT   = 900.0;    // er cheye chhoto hobe na
const float FULL_SCALE   = 0.90;     // 90% thelle full speed

const bool DEBUG = true;

// ================= VARIABLES =================
float angle[NUM_JOINTS];
float outAngle[NUM_JOINTS];
float filtered[NUM_JOINTS];
int   center[NUM_JOINTS];
int   lastPulse[NUM_JOINTS];
bool  joyActive[NUM_JOINTS] = {false, false, false, false};
int   rawNow[NUM_JOINTS];

// Joystick-er ashol max/min (auto-learn)
float joyMaxSeen[NUM_JOINTS];
float joyMinSeen[NUM_JOINTS];

// ================= WEB CONTROL =================
WebServer server(HTTP_PORT);
DNSServer dnsServer;

float         webVel[NUM_JOINTS]     = {0, 0, 0, 0};   // hold-button speed (-1..1)
unsigned long webVelTime[NUM_JOINTS] = {0, 0, 0, 0};   // last 'V' message time

// ================= FUNCTIONS =================
int toPulse(int i, float logical) {
  float a = logical;
  if (mirrorServo[i]) a = (minAngle[i] + maxAngle[i]) - a;
  return PULSE_MIN + (int)(a * (PULSE_MAX - PULSE_MIN) / 180.0 + 0.5);
}

// Highest ar lowest sample bad diye average
int readJoy(int pin) {
  long sum = 0;
  int mn = 4095, mx = 0;
  for (int n = 0; n < NUM_SAMPLES; n++) {
    int v = analogRead(pin);
    sum += v;
    if (v < mn) mn = v;
    if (v > mx) mx = v;
    delayMicroseconds(100);
  }
  return (sum - mn - mx) / (NUM_SAMPLES - 2);
}

void calibrateJoysticks() {
  delay(2000);   // joystick chhuo na
  for (int i = 0; i < NUM_JOINTS; i++) {
    long sum = 0;
    for (int n = 0; n < 100; n++) {
      sum += readJoy(joyPins[i]);
      delay(5);
    }
    center[i]     = sum / 100;
    filtered[i]   = center[i];
    joyMaxSeen[i] = center[i] + START_EXTENT;
    joyMinSeen[i] = center[i] - START_EXTENT;

    Serial.print("Joystick ");
    Serial.print(i + 1);
    Serial.print(" Center = ");
    Serial.println(center[i]);
  }
}

// Servo ek ek kore chalu (current spike kombe)
void initServos() {
  for (int i = 0; i < NUM_JOINTS; i++) {
    angle[i]    = homeAngle[i];
    outAngle[i] = homeAngle[i];
    int p = toPulse(i, angle[i]);

    servos[i].setPeriodHertz(50);
    servos[i].attach(servoPins[i], PULSE_MIN, PULSE_MAX);
    servos[i].writeMicroseconds(p);
    lastPulse[i] = p;
    delay(300);
  }
}

// Phase 1: shob joystick pora (servo pulse-er por noi, noise kom)
void readAllJoysticks() {
  for (int i = 0; i < NUM_JOINTS; i++) {
    rawNow[i] = readJoy(joyPins[i]);
    filtered[i] += ALPHA * (rawNow[i] - filtered[i]);
  }
}

// Phase 2: target angle hisab kore servo-te pathano
void updateJoint(int i) {
  float diff      = filtered[i] - center[i];
  float magnitude = fabsf(diff);

  // Joystick range shikhe nao
  if (filtered[i] > joyMaxSeen[i]) joyMaxSeen[i] = filtered[i];
  if (filtered[i] < joyMinSeen[i]) joyMinSeen[i] = filtered[i];

  // Hysteresis
  if (!joyActive[i]) {
    if (magnitude > START_DEADZONE) joyActive[i] = true;
  } else {
    if (magnitude < STOP_DEADZONE) joyActive[i] = false;
  }

  if (joyActive[i]) {
    // Joystick sobshomoy priority pabe -> phone-er hold command bad
    webVel[i] = 0;

    // Je dike thelchho, oi dike-r ashol range
    float extent = (diff > 0) ? (joyMaxSeen[i] - center[i])
                              : (center[i] - joyMinSeen[i]);
    if (extent < MIN_EXTENT) extent = MIN_EXTENT;
    extent *= FULL_SCALE;

    float norm = (magnitude - START_DEADZONE) / (extent - START_DEADZONE);
    norm = constrain(norm, 0.0, 1.0);
    norm = norm * norm;   // soft start

    if (diff < 0)     norm = -norm;
    if (invertJoy[i]) norm = -norm;

    angle[i] += norm * maxSpeed[i];
    angle[i]  = constrain(angle[i], minAngle[i], maxAngle[i]);
  } else if (webVel[i] != 0) {
    // Phone hold-button: signal bondho hole (phone lock / WiFi drop) thamo
    if (millis() - webVelTime[i] > WEB_VEL_TIMEOUT) {
      webVel[i] = 0;
    } else {
      angle[i] += webVel[i] * maxSpeed[i];
      angle[i]  = constrain(angle[i], minAngle[i], maxAngle[i]);
    }
  }

  // Smooth + snap (hunting/gorgor kombe)
  float err = angle[i] - outAngle[i];
  if (fabsf(err) < SNAP_DIST) outAngle[i] = angle[i];
  else                        outAngle[i] += SERVO_SMOOTH * err;

  int p = toPulse(i, outAngle[i]);
  if (abs(p - lastPulse[i]) >= pulseDeadband[i]) {
    servos[i].writeMicroseconds(p);
    lastPulse[i] = p;
  }
}

// ================= WEB: messages =================
void formatLimits(char *buf, size_t len) {
  snprintf(buf, len, "L,%d,%d,%d,%d,%d,%d,%d,%d",
           (int)minAngle[0], (int)minAngle[1], (int)minAngle[2], (int)minAngle[3],
           (int)maxAngle[0], (int)maxAngle[1], (int)maxAngle[2], (int)maxAngle[3]);
}

void formatAngles(char *buf, size_t len) {
  snprintf(buf, len, "A,%d,%d,%d,%d",
           (int)lroundf(angle[0]), (int)lroundf(angle[1]),
           (int)lroundf(angle[2]), (int)lroundf(angle[3]));
}

// Phone theke asha command:
//   S,j,angle  -> slider (absolute)     V,j,v -> hold speed (-1..1)
//   H          -> home                  X     -> stop (jekhane ache shekhane thamo)
void handleCommand(char *msg) {
  char cmd = msg[0];

  if (cmd == 'H') {
    for (int i = 0; i < NUM_JOINTS; i++) {
      webVel[i] = 0;
      if (!joyActive[i]) angle[i] = homeAngle[i];
    }
    return;
  }
  if (cmd == 'X') {
    for (int i = 0; i < NUM_JOINTS; i++) {
      webVel[i] = 0;
      angle[i]  = outAngle[i];   // current position-ei freeze
    }
    return;
  }
  if (cmd != 'S' && cmd != 'V') return;

  char *p1 = strchr(msg, ',');
  if (!p1) return;
  char *p2 = strchr(p1 + 1, ',');
  if (!p2) return;

  int   j   = atoi(p1 + 1);
  float val = atof(p2 + 1);
  if (j < 0 || j >= NUM_JOINTS) return;
  if (joyActive[j]) return;      // joystick dhora thakle phone ignore

  if (cmd == 'S') {
    webVel[j] = 0;
    angle[j]  = constrain(val, minAngle[j], maxAngle[j]);
  } else {  // 'V'
    webVel[j]     = constrain(val, -1.0f, 1.0f);
    webVelTime[j] = millis();
  }
}

void sendPage() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleHttpCmd() {
  String m = server.arg("m");
  char msg[32];
  m.toCharArray(msg, sizeof(msg));
  handleCommand(msg);
  server.send(200, "text/plain", "ok");
}

void handleHttpState() {
  char buf[40];
  formatAngles(buf, sizeof(buf));
  server.send(200, "text/plain", buf);
}

void handleHttpLimits() {
  char buf[64];
  formatLimits(buf, sizeof(buf));
  server.send(200, "text/plain", buf);
}

void printHowToConnect() {
  Serial.println();
  Serial.println("============================================");
  Serial.println("  SLIDER PAGE — do not hunt for an IP");
  Serial.println("  1. Phone WiFi  ->  RoboArm");
  Serial.println("  2. Password    ->  roboarm123");
  Serial.println("  3. Open the Sign-in page, or type:");
  Serial.println("     http://192.168.4.1");
  Serial.println("  Turn OFF mobile data if it will not load.");
  Serial.println("============================================");
}

// ================= WEB: WiFi + servers =================
void setupWiFi() {
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);

  IPAddress ip(192, 168, 4, 1);
  IPAddress gw(192, 168, 4, 1);
  IPAddress mask(255, 255, 255, 0);
  WiFi.softAPConfig(ip, gw, mask);
  WiFi.softAP(AP_SSID, AP_PASSWORD, 1, 0, 4);
  delay(200);

  dnsServer.start(53, "*", ip);   // any website name -> slider page
  printHowToConnect();
}

void setupServers() {
  server.on("/", HTTP_GET, sendPage);
  server.on("/c", HTTP_GET, handleHttpCmd);
  server.on("/s", HTTP_GET, handleHttpState);
  server.on("/l", HTTP_GET, handleHttpLimits);
  // Phone captive-portal checks -> show sliders instead of "can't reach"
  server.on("/generate_204", HTTP_GET, sendPage);
  server.on("/gen_204", HTTP_GET, sendPage);
  server.on("/hotspot-detect.html", HTTP_GET, sendPage);
  server.on("/ncsi.txt", HTTP_GET, sendPage);
  server.on("/connecttest.txt", HTTP_GET, sendPage);
  server.on("/canonical.html", HTTP_GET, sendPage);
  server.onNotFound(sendPage);
  server.begin();
}

void printNetInfo() {
  printHowToConnect();
  Serial.printf("Phones on RoboArm WiFi: %u\n", WiFi.softAPgetStationNum());
}

void handleNetwork() {
  dnsServer.processNextRequest();
  server.handleClient();
}

// ================= MAIN =================
void setup() {
  Serial.begin(115200);

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);

  calibrateJoysticks();   // WiFi chalur AGE (noise chhara center)
  initServos();
  setupWiFi();
  setupServers();
}

void loop() {
  static int dbgCount = 0;
  static unsigned long lastNetInfo = 0;
  unsigned long t0 = millis();

  if (t0 - lastNetInfo >= 5000) {   // proti 5 second-e address dekhao
    lastNetInfo = t0;
    printNetInfo();
  }

  handleNetwork();
  readAllJoysticks();

  for (int i = 0; i < NUM_JOINTS; i++) updateJoint(i);

  if (DEBUG && ++dbgCount >= 10) {   // 5 bar/second
    dbgCount = 0;
    Serial.print("Angle B:");  Serial.print(angle[BASE], 0);
    Serial.print(" S:");       Serial.print(angle[SHOULDER], 0);
    Serial.print(" E:");       Serial.print(angle[ELBOW], 0);
    Serial.print(" G:");       Serial.print(angle[GRIPPER], 0);
    Serial.print(" | Elbow joy min/center/max: ");
    Serial.print(joyMinSeen[ELBOW], 0);  Serial.print("/");
    Serial.print(center[ELBOW]);         Serial.print("/");
    Serial.println(joyMaxSeen[ELBOW], 0);
  }

  // Baki shomoy network service koro (phone response druto hobe)
  while (millis() - t0 < LOOP_MS) {
    handleNetwork();
    delay(1);
  }
}
