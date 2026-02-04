#include <WiFi.h>
#include <WebServer.h>

/* ================= CONFIGURATION ================= */
const char* ssid = "POCO";
const char* password = "33333333";

WebServer server(80);

/* ================= PIN DEFINITIONS (ESP32-S3) ================= */
// --- Flame Sensors (Analog Pins) ---
#define SENSOR_RIGHT  14  
#define SENSOR_LEFT   7  
#define SENSOR_CENTER 13

// --- Motor Driver Pins ---
const int enableRightMotor = 20; 
const int rightMotorPin1 = 16;
const int rightMotorPin2 = 17;

const int enableLeftMotor = 21;
const int leftMotorPin1 = 18;
const int leftMotorPin2 = 19;

// --- LED & Pump Pins ---
const int ledPin = 2; 
const int pumpPin = 10; 
bool ledState = false;

// --- Servo Pins ---
const int servoCamPin = 4;      // Camera Pan
const int servoWaterPin = 5;    // Water Trigger

/* ================= VARIABLES ================= */
const int servoFreq = 50;       
const int servoRes = 12;        

// Start angles at center (90)
int angleCam = 90;
int angleWater = 90;            

bool autoFireMode = false;      
unsigned long lastLoopTime = 0; 

// --- SENSITIVITY SETTINGS ---
// 4095 = No Fire. 0 = Touching Fire.

// 1. Detection Range: Sensors must read below this to "see" fire.
int fireThreshold = 3500; 

// 2. Stop Distance (10-15cm): 
// If the robot doesn't stop, INCREASE this number (e.g., make it 1500 or 2000).
// If it stops too early, DECREASE this number (e.g., make it 500).
int stopThreshold = 1000; 

// Servo Speed (Steps per cycle)
int servoSpeed = 8; 

/* ================= HELPER FUNCTIONS ================= */
void setServoAngle(int pin, int angle) {
  // --- LIMIT CONSTANT: 30 to 150 Degrees Only ---
  if (angle < 30) angle = 30;
  if (angle > 150) angle = 150;

  long pulseWidth = map(angle, 0, 180, 500, 2400);
  long dutyCycle = (pulseWidth * 4095) / 20000;
  ledcWrite(pin, dutyCycle);
}

// --- Motor Control Helpers ---
void moveForward() {
  digitalWrite(rightMotorPin1, HIGH); digitalWrite(rightMotorPin2, LOW);
  digitalWrite(leftMotorPin1, HIGH); digitalWrite(leftMotorPin2, LOW);
}
void moveBackward() {
  digitalWrite(rightMotorPin1, LOW); digitalWrite(rightMotorPin2, HIGH);
  digitalWrite(leftMotorPin1, LOW); digitalWrite(leftMotorPin2, HIGH);
}
void moveLeft() {
  digitalWrite(rightMotorPin1, HIGH); digitalWrite(rightMotorPin2, LOW);
  digitalWrite(leftMotorPin1, LOW); digitalWrite(leftMotorPin2, LOW);
}
void moveRight() {
  digitalWrite(rightMotorPin1, LOW); digitalWrite(rightMotorPin2, LOW);
  digitalWrite(leftMotorPin1, HIGH); digitalWrite(leftMotorPin2, LOW);
}
void stopMotors() {
  digitalWrite(rightMotorPin1, LOW); digitalWrite(rightMotorPin2, LOW);
  digitalWrite(leftMotorPin1, LOW); digitalWrite(leftMotorPin2, LOW);
}

/* ================= HTML PAGE ================= */
String webPage() {
  String page = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
  <title>FireBot S3</title>
  <style>
    body { font-family: sans-serif; text-align: center; background: #121212; color: #e0e0e0; margin: 0; padding: 5px; user-select: none; }
    .container { max-width: 400px; margin: auto; }
    .box { background: #1f1f1f; padding: 10px; border-radius: 8px; margin-bottom: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.5); }
    .button {
      display: inline-block; width: 60px; height: 45px; margin: 2px;
      font-size: 1.2em; font-weight: bold; color: #fff;
      background: #333; border: 1px solid #444; border-radius: 6px;
      cursor: pointer;
    }
    .btn-move { background: #007BFF; border: none; }
    .btn-stop { background: #d32f2f; border: none; }
    .btn-auto { background: #ff4400; color: #fff; width: auto; height: 30px; padding: 0 15px; font-size: 0.9rem; margin-right: 5px; }
    .btn-led { background: #ffb300; color: #000; width: auto; height: 30px; padding: 0 15px; font-size: 0.9rem; margin-left: 5px; }
    .controller { display: grid; grid-template-columns: repeat(3, 1fr); gap: 2px; justify-items: center; align-items: center; }
    .empty { width: 60px; height: 45px; }
    .slidecontainer { display: flex; align-items: center; gap: 8px; margin-top: 5px; }
    .slider { -webkit-appearance: none; flex-grow: 1; height: 8px; border-radius: 4px; background: #444; outline: none; }
    .slider::-webkit-slider-thumb { -webkit-appearance: none; width: 20px; height: 20px; border-radius: 50%; background: #00d2be; }
  </style>
  <script>
    function sendRequest(url) { var xhr = new XMLHttpRequest(); xhr.open("GET", url, true); xhr.send(); }
    function updateServo(type, val) { 
        document.getElementById("val-" + type).innerText = val; 
        sendRequest("/servo?type=" + type + "&value=" + val); 
    }
    function move(dir) { sendRequest('/' + dir); }
    function stop() { sendRequest('/stop'); }
    function toggleLed() { sendRequest('/toggle'); }
    function toggleAuto() {
        sendRequest('/auto_fire');
        var btn = document.getElementById('btn-auto-fire');
        if(btn.style.background === 'rgb(50, 205, 50)') {
            btn.style.background = '#ff4400';
            btn.innerHTML = "AUTO FIRE: OFF";
        } else {
            btn.style.background = '#32cd32';
            btn.innerHTML = "AUTO FIRE: ON";
        }
    }
  </script>
</head>
<body>
  <div class="container">
    <div class="box" style="display:flex; justify-content:space-between; align-items:center;">
       <button id="btn-auto-fire" class="button btn-auto" onclick="toggleAuto()" style="background: )rawliteral" + String((autoFireMode) ? "#32cd32" : "#ff4400") + R"rawliteral(;">AUTO FIRE: )rawliteral" + String((autoFireMode) ? "ON" : "OFF") + R"rawliteral(</button>
       <button class="button btn-led" onclick="toggleLed()">LED</button>
    </div>
    
    <div class="box">
      <div class="controller">
        <div class="empty"></div>
        <button class="button btn-move" onmousedown="move('forward')" ontouchstart="move('forward')" onmouseup="stop()" ontouchend="stop()">F</button>
        <div class="empty"></div>
        <button class="button btn-move" onmousedown="move('left')" ontouchstart="move('left')" onmouseup="stop()" ontouchend="stop()">L</button>
        <button class="button btn-stop" onclick="stop()">S</button>
        <button class="button btn-move" onmousedown="move('right')" ontouchstart="move('right')" onmouseup="stop()" ontouchend="stop()">R</button>
        <div class="empty"></div>
        <button class="button btn-move" onmousedown="move('backward')" ontouchstart="move('backward')" onmouseup="stop()" ontouchend="stop()">B</button>
        <div class="empty"></div>
      </div>
    </div>

    <div class="box">
      <span style="font-size:0.8rem;color:#888;">Water Servo (30-150)</span>
      <div class="slidecontainer">
        <input type="range" min="30" max="150" value=")rawliteral" + String(angleWater) + R"rawliteral(" class="slider" style="background:#0056b3;" oninput="updateServo('water', this.value)">
        <span id="val-water" style="width:25px; text-align:right;">)rawliteral" + String(angleWater) + R"rawliteral(</span>
      </div>
    </div>
    <div class="box">
      <span style="font-size:0.8rem;color:#888;">Camera Servo (30-150)</span>
      <div class="slidecontainer">
        <input type="range" min="30" max="150" value=")rawliteral" + String(angleCam) + R"rawliteral(" class="slider" oninput="updateServo('cam', this.value)">
        <span id="val-cam" style="width:25px; text-align:right;">)rawliteral" + String(angleCam) + R"rawliteral(</span>
      </div>
    </div>
  </div>
</body>
</html>
)rawliteral";
  return page;
}

/* ================= REQUEST HANDLERS ================= */
void handleRoot() { server.send(200, "text/html", webPage()); }
void handleToggle() { ledState = !ledState; digitalWrite(ledPin, ledState ? HIGH : LOW); server.send(200, "text/plain", "OK"); }
void handleAutoFire() { 
  autoFireMode = !autoFireMode; 
  stopMotors(); 
  digitalWrite(pumpPin, LOW); 
  server.send(200, "text/plain", "OK"); 
}

void handleServo() {
  if (server.hasArg("type") && server.hasArg("value")) {
    String type = server.arg("type");
    int val = server.arg("value").toInt();
    // Clamp values here as well for manual control
    if (val < 30) val = 30;
    if (val > 150) val = 150;

    if (type == "cam") { angleCam = val; setServoAngle(servoCamPin, angleCam); } 
    else if (type == "water") { autoFireMode = false; digitalWrite(pumpPin, LOW); angleWater = val; setServoAngle(servoWaterPin, angleWater); }
  }
  server.send(200, "text/plain", "OK");
}

void handleForward() { moveForward(); server.send(200, "text/plain", "OK"); }
void handleBackward() { moveBackward(); server.send(200, "text/plain", "OK"); }
void handleLeft() { moveLeft(); server.send(200, "text/plain", "OK"); }
void handleRight() { moveRight(); server.send(200, "text/plain", "OK"); }
void handleStop() { stopMotors(); server.send(200, "text/plain", "OK"); }

/* ================= SETUP & LOOP ================= */
void setup() {
  Serial.begin(115200);

  // Motors
  pinMode(enableRightMotor, OUTPUT); pinMode(rightMotorPin1, OUTPUT); pinMode(rightMotorPin2, OUTPUT);
  pinMode(enableLeftMotor, OUTPUT); pinMode(leftMotorPin1, OUTPUT); pinMode(leftMotorPin2, OUTPUT);
  digitalWrite(enableRightMotor, HIGH); digitalWrite(enableLeftMotor, HIGH);

  // LED & Sensors & Pump
  pinMode(ledPin, OUTPUT);
  pinMode(pumpPin, OUTPUT);
  digitalWrite(pumpPin, LOW);
  
  pinMode(SENSOR_RIGHT, INPUT);
  pinMode(SENSOR_LEFT, INPUT);
  pinMode(SENSOR_CENTER, INPUT);

  // Servos
  if (!ledcAttach(servoCamPin, servoFreq, servoRes)) Serial.println("Err Cam Servo");
  if (!ledcAttach(servoWaterPin, servoFreq, servoRes)) Serial.println("Err Water Servo");
  setServoAngle(servoCamPin, angleCam);
  setServoAngle(servoWaterPin, angleWater); 

  // WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\nConnected! IP: " + WiFi.localIP().toString());

  // Routes
  server.begin();
  server.on("/", handleRoot);
  server.on("/toggle", handleToggle);
  server.on("/auto_fire", handleAutoFire);
  server.on("/servo", handleServo);
  server.on("/forward", handleForward);
  server.on("/backward", handleBackward);
  server.on("/left", handleLeft);
  server.on("/right", handleRight);
  server.on("/stop", handleStop);
}

void loop() {
  server.handleClient();

  if (autoFireMode && (millis() - lastLoopTime > 30)) {
    lastLoopTime = millis();

    int valLeft = analogRead(SENSOR_LEFT);
    int valRight = analogRead(SENSOR_RIGHT);
    int valCenter = analogRead(SENSOR_CENTER);

    // Find smallest value (strongest fire)
    int minVal = min(valLeft, min(valCenter, valRight));

    // Debugging: View this in Serial Monitor to tune 'stopThreshold'
    // Serial.print("Min Val: "); Serial.println(minVal);

    bool fireDetected = (minVal < fireThreshold);

    if (!fireDetected) {
      stopMotors();
      digitalWrite(pumpPin, LOW);
    } 
    else {
      // --- LOGIC: AIM SERVO FIRST ---
      int targetAngle = angleWater; // Stay put if unsure

      // New constraints: Left=150, Right=30
      if (valLeft < fireThreshold && valCenter < fireThreshold) targetAngle = 120; // Shared Left-Center
      else if (valRight < fireThreshold && valCenter < fireThreshold) targetAngle = 60; // Shared Right-Center
      else if (valCenter < fireThreshold) targetAngle = 90;
      else if (valLeft < fireThreshold) targetAngle = 150; // Max Left
      else if (valRight < fireThreshold) targetAngle = 30; // Max Right

      // Move servo
      if (angleWater < targetAngle) angleWater = min(angleWater + servoSpeed, targetAngle);
      else if (angleWater > targetAngle) angleWater = max(angleWater - servoSpeed, targetAngle);
      setServoAngle(servoWaterPin, angleWater);

      // --- LOGIC: MOVE OR STOP ---
      // Check if we are close enough to STOP (10-15cm)
      if (minVal < stopThreshold) {
        stopMotors(); 
        // Only fire pump if servo is roughly aimed
        if (abs(angleWater - targetAngle) < 20) {
           digitalWrite(pumpPin, HIGH);
        } else {
           digitalWrite(pumpPin, LOW);
        }
      } 
      else {
        // Fire is detected, but FAR away -> Move closer
        digitalWrite(pumpPin, LOW); // Don't waste water while moving
        
        if (minVal == valCenter) {
           moveForward();
        } else if (minVal == valLeft) {
           moveLeft();
        } else if (minVal == valRight) {
           moveRight();
        }
      }
    }
  }
}