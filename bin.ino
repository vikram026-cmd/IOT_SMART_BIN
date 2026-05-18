#include <Servo.h>
#include <ESP8266WiFi.h>
#include <ThingSpeak.h>

// ================= WIFI =================
const char* ssid = "iTEST";
const char* password = "12345678";

// ============== THINGSPEAK ==============
unsigned long channelID = 3376020;
const char* writeAPIKey = "40TK1SFUKBPGRN4R";

WiFiClient client;

// ================= WIFI CONTROL =================
bool wifiEnabled = true;           // Can disable WiFi on startup timeout
bool wifiDisabledByTimeout = false; // Track if WiFi was disabled by timeout
unsigned long wifiStartTime = 0;
const unsigned long WIFI_TIMEOUT = 60000; // 60 seconds timeout

// ================= SERVO =================
Servo dustbinServo;

const int servoPin = D4;

const int openAngle = 0;
const int closeAngle = 180;

// ================= IR SENSORS =================
const int personIR = D1;  // Person detection IR

const int wet50IR  = D2;
const int wet100IR = D5;

const int dry50IR  = D6;
const int dry100IR = D7;

// ================= VARIABLES =================
int wetLevel = 0;
int dryLevel = 0;
int lidStatus = 0;

unsigned long lastUploadTime = 0;
unsigned long leaveTime = 0;
unsigned long lastServoCommandTime = 0;

// Track previous person detection state for change detection
int previousPersonDetected = HIGH; // Assume no person at start

// ================= FUNCTION PROTOTYPES =================
void initializeWiFi();
void checkWiFiConnection();
void handlePersonDetection();
void handleWasteLevels();
void uploadToCloud();

void setup()
{
  Serial.begin(115200);
  delay(100);

  Serial.println("\n\n================= STARTUP =================");

  // Sensor Pins
  pinMode(personIR, INPUT);
  pinMode(wet50IR, INPUT);
  pinMode(wet100IR, INPUT);
  pinMode(dry50IR, INPUT);
  pinMode(dry100IR, INPUT);

  // Servo Setup
  dustbinServo.attach(servoPin);
  dustbinServo.write(closeAngle); // Start closed

  Serial.println("Hardware initialized");

  // WiFi initialization
  initializeWiFi();

  Serial.println("Setup complete\n");
}

void loop()
{
  // ================= CHECK WIFI CONNECTION =================
  checkWiFiConnection();

  // ================= PERSON DETECTION (HIGH PRIORITY) =================
  handlePersonDetection();

  // ================= WASTE LEVEL DETECTION =================
  handleWasteLevels();

  // ================= SERIAL MONITOR =================
  Serial.println("====================");
  Serial.print("Wet Level  : ");
  Serial.println(wetLevel);
  Serial.print("Dry Level  : ");
  Serial.println(dryLevel);
  Serial.print("Lid Status : ");
  Serial.println(lidStatus);
  Serial.print("WiFi Status: ");
  //delay(2000);
  if(wifiDisabledByTimeout)
  {
    Serial.println("DISABLED (Timeout) - Reset ESP to enable");
  }
  else if(WiFi.status() == WL_CONNECTED)
  {
    Serial.println("CONNECTED");
  }
  else
  {
    Serial.println("DISCONNECTED");
  }

  // ================= CLOUD UPLOAD =================
  uploadToCloud();

  // Reduced delay for faster servo response
  delay(1000); // 20ms loop cycle for responsive servo control
}

// ================= WIFI INITIALIZATION =================
void initializeWiFi()
{
  wifiStartTime = millis();
  wifiEnabled = true;
  wifiDisabledByTimeout = false;

  Serial.print("Attempting WiFi connection (60 sec timeout)...");

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED)
  {
    if(millis() - wifiStartTime >= WIFI_TIMEOUT)
    {
      // WiFi timeout - disable WiFi and work offline
      WiFi.disconnect(true); // Disconnect and turn off WiFi radio
      wifiEnabled = false;
      wifiDisabledByTimeout = true;

      Serial.println("\nWiFi connection timeout!");
      Serial.println("Disabling WiFi - Working in OFFLINE MODE");
      Serial.println("Reset ESP to enable WiFi again\n");
      return;
    }

    delay(500);
    Serial.print(".");
  }

  // WiFi connected successfully
  Serial.println("\nWiFi Connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  ThingSpeak.begin(client);
  Serial.println("ThingSpeak initialized\n");
}

// ================= CHECK WIFI CONNECTION =================
void checkWiFiConnection()
{
  // If WiFi was disabled by timeout, don't try to reconnect
  if(wifiDisabledByTimeout)
  {
    return;
  }

  // Only attempt connection if WiFi is enabled
  if(!wifiEnabled)
  {
    return;
  }

  // WiFi is already handled by background tasks in ESP8266
  // No need to constantly check unless connection is lost
}

// ================= PERSON DETECTION HANDLER =================
void handlePersonDetection()
{
  // IR Sensor Logic:
  // LOW (0V) = Object detected
  // HIGH (5V) = No object detected

  int personDetected = digitalRead(personIR);

  // Check if state changed (debounce with state tracking)
  if(personDetected != previousPersonDetected)
  {
    // Only react to valid state changes
    if(personDetected == LOW) // Person DETECTED
    {
      // Open immediately
      dustbinServo.write(openAngle);
      lidStatus = 1;
      leaveTime = 0;
      lastServoCommandTime = millis();

      Serial.println(">>> Person Detected - Lid OPENING");
    }
    else if(personDetected == HIGH) // Person LEFT
    {
      // Start timer when person leaves
      leaveTime = millis();
      Serial.println(">>> Person Left - 2 sec timer started");
    }

    previousPersonDetected = personDetected;
  }

  // Handle 2-second delay after person leaves
  if(personDetected == HIGH && leaveTime > 0)
  {
    if(millis() - leaveTime >= 2000)
    {
      dustbinServo.write(closeAngle);
      lidStatus = 0;
      leaveTime = 0;
      lastServoCommandTime = millis();

      Serial.println(">>> Lid CLOSING");
    }
  }
}

// ================= WASTE LEVEL HANDLER =================
void handleWasteLevels()
{
  // IR Sensor Logic:
  // LOW (0V) = Object detected (full)
  // HIGH (5V) = No object detected (empty)

  // ===== WET BIN =====
  int wet50  = digitalRead(wet50IR);
  int wet100 = digitalRead(wet100IR);

  if(wet100 == LOW)
  {
    wetLevel = 100;
  }
  else if(wet50 == LOW)
  {
    wetLevel = 50;
  }
  else
  {
    wetLevel = 0;
  }

  // ===== DRY BIN =====
  int dry50  = digitalRead(dry50IR);
  int dry100 = digitalRead(dry100IR);

  if(dry100 == LOW)
  {
    dryLevel = 100;
  }
  else if(dry50 == LOW)
  {
    dryLevel = 50;
  }
  else
  {
    dryLevel = 0;
  }
}

// ================= CLOUD UPLOAD =================
void uploadToCloud()
{
  // Upload every 30 seconds
  if(millis() - lastUploadTime > 30000)
  {
    lastUploadTime = millis();

    // Only upload if WiFi is enabled and connected
    if(wifiEnabled && !wifiDisabledByTimeout && WiFi.status() == WL_CONNECTED)
    {
      ThingSpeak.setField(1, wetLevel);
      ThingSpeak.setField(2, dryLevel);
      ThingSpeak.setField(3, lidStatus);

      int response = ThingSpeak.writeFields(channelID, writeAPIKey);

      if(response == 200)
      {
        Serial.println("☁ Cloud Upload: SUCCESS");
      }
      else
      {
        Serial.print("☁ Cloud Upload: FAILED (Code ");
        Serial.print(response);
        Serial.println(")");
      }
    }
    else
    {
      Serial.println("📱 Working OFFLINE (Local only)");
    }
  }
}