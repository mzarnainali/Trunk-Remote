#include <LittleFS.h>
#include <ESP8266WiFi.h>

//600ms pilse and delay 100 us noice

extern "C" {
#include "gpio.h"
}

// Required for LIGHT_SLEEP_T delay mode
extern "C" {
#include "user_interface.h"
}

#define DEBUG 0  // SET TO 0 OUT TO REMOVE TRACES
#if DEBUG
#define D_SerialBegin(...) Serial.begin(__VA_ARGS__);
#define D_print(...) Serial.print(__VA_ARGS__)
#define D_write(...) Serial.write(__VA_ARGS__)
#define D_println(...) Serial.println(__VA_ARGS__)
#else
#define D_SerialBegin(...)
#define D_print(...)
#define D_write(...)
#define D_println(...)
#endif
const char* filePath = "/data.txt";  // File path to store data
const int pinACC = 12;
const int pinLock = 14;
const int pinUnLock = 13;
const int pinTrukRelay = 5;

String defaultPattern = "xxx";
String targetPattern = "xxx";  // Predefined pattern to check
String learningPattern = "yxxy";
String resetPattern = "xxyy";
String resetPattern2 = "yyxx";
String detectedPattern = "";  // To store detected pattern
volatile bool learningMode = false;
volatile unsigned long lastPulseTime = 0;   // Time when the last pulse was detected
const unsigned long patternTimeout = 2000;  // 3-second timeout for pattern reset
////
unsigned long lastDebounceTimeX = 0;     // Last time button X was pressed
unsigned long lastDebounceTimeY = 0;     // Last time button Y was pressed
const unsigned long debounceDelay = 50;  // 400 ms debounce time
bool sleepMode = false;
int lockPlusStatus = 0;
int unLockPlusStatus = 0;
///
unsigned long lockTime = 0;
bool lockTimeCheck = false;
///
unsigned long unLockTime = 0;
bool unLockTimeCheck = false;

unsigned long lockStartTime = 0;    // Time when lock pin went HIGH
unsigned long unlockStartTime = 0;  // Time when unlock pin went HIGH


void fpm_wakup_cb_func1(void) {
  //D_println("hellow, waking up...");
  sleepMode = false;
  lastPulseTime = millis();
}

enum State { IDLE,
             WAIT_FOR_LOW,
             LOCK_DETECTED,
             UNLOCK_DETECTED };  // State machine states
State lockState = IDLE;          // State for lock pin
State unlockState = IDLE;        // State for unlock pin

void setup() {
  // Disable WiFi to save power
  WiFi.forceSleepBegin();
  delay(1);
  D_SerialBegin(115200);
  pinMode(LED_BUILTIN, OUTPUT);     // Set the LED pin as output
  digitalWrite(LED_BUILTIN, HIGH);  // Turn off the LED (LED is active LOW
  // Set GPIO pins 12, 13, 14, and 16 as input
  pinMode(pinACC, INPUT);
  pinMode(pinLock, INPUT);
  pinMode(pinUnLock, INPUT);
  pinMode(pinTrukRelay, OUTPUT);
  digitalWrite(pinTrukRelay, LOW);

  // Attach interrupt to GPIO13, triggering on rising edge (from LOW to HIGH)
  //attachInterrupt(digitalPinToInterrupt(pinLock), handleLock, RISING);  //RISING
  //attachInterrupt(digitalPinToInterrupt(pinUnLock), handleUnLock, RISING);

  digitalWrite(LED_BUILTIN, LOW);
  delay(300);
  digitalWrite(LED_BUILTIN, HIGH);
  delay(300);
  digitalWrite(LED_BUILTIN, LOW);
  delay(300);
  digitalWrite(LED_BUILTIN, HIGH);
  delay(300);
  digitalWrite(LED_BUILTIN, LOW);
  delay(300);
  digitalWrite(LED_BUILTIN, HIGH);
  delay(300);
  digitalWrite(LED_BUILTIN, LOW);
  delay(300);
  digitalWrite(LED_BUILTIN, HIGH);

  // delay(6000);
  D_println("LittleFS Stating");
  // Initialize LittleFS
  if (!LittleFS.begin()) {
    D_println("Failed to mount LittleFS");
    return;
  }
  delay(100);
  targetPattern = readData();
  if (targetPattern.length() == 0) {
    targetPattern = defaultPattern;
  }
}
unsigned long previousMillis = 0;  // Stores the last time the LED was updated
const long interval = 1000;        // Interval for LED blink (1 second)
bool lockPlus = false;
bool unLockPlus = false;
void loop() {
  unsigned long currentTime = millis();
  // State Machine for Lock Pin
  switch (lockState) {
    case IDLE:
      if (digitalRead(pinLock) == HIGH) {
        lockStartTime = currentTime;  // Start timing when pin goes HIGH
        lockState = LOCK_DETECTED;
      }
      break;

    case LOCK_DETECTED:
      if (digitalRead(pinLock) == LOW) {
        lockState = IDLE;  // Reset if the pin goes LOW before debounce time
      } else if ((currentTime - lockStartTime) >= debounceDelay) {
        handlePulse('y');          // Call function after debounce delay
        D_println("Lock...");
        lockState = WAIT_FOR_LOW;  // Reset state
      }
      break;

    case WAIT_FOR_LOW:
      if (digitalRead(pinLock) == LOW) {
        lockState = IDLE;  // Wait for pin to go LOW before allowing new detection
      }
      break;
  }

  // State Machine for Unlock Pin
  switch (unlockState) {
    case IDLE:
      if (digitalRead(pinUnLock) == HIGH) {
        unlockStartTime = currentTime;  // Start timing when pin goes HIGH
        unlockState = UNLOCK_DETECTED;
      }
      break;

    case UNLOCK_DETECTED:
      if (digitalRead(pinUnLock) == LOW) {
        unlockState = IDLE;  // Reset if the pin goes LOW before debounce time
      } else if ((currentTime - unlockStartTime) >= debounceDelay) {
        handlePulse('x');            // Call function after debounce delay
        D_println("UnLock...");
        unlockState = WAIT_FOR_LOW;  // Reset state
      }
      break;

    case WAIT_FOR_LOW:
      if (digitalRead(pinUnLock) == LOW) {
        unlockState = IDLE;  // Wait for pin to go LOW before allowing new detection
      }
      break;
  }

  if (learningMode) {
    if ((currentTime - lastPulseTime) > 5000) {
      learningMode = false;
      if (detectedPattern.length() > 1 && detectedPattern != learningPattern && detectedPattern != resetPattern && detectedPattern != resetPattern2 && detectedPattern != targetPattern) {
        targetPattern = detectedPattern;
        D_print("New Pattern Learned! ");
        D_println(targetPattern);
        saveData(targetPattern);
        openTrunk();
      }
    }
  } else {
    // bool isTargetPatternMatch = (targetPattern == "xx" || targetPattern == "xxy" || targetPattern == "yx" || targetPattern == "yxx");
    //if (!isTargetPatternMatch || (isTargetPatternMatch && (m - lastPulseTime) > 1)) {
    checkPatternAndOutput();
    //}
    // Regular operation: check for patterns and respond
    // Enter sleep mode if no pulses detected within the timeout period
    if ((currentTime - lastPulseTime) > 3000 && !sleepMode) {
      sleepMode = true;
      //D_print(m);
      //D_print(" - ");
      //D_print(lastPulseTime);
      //D_print(" = ");
      //D_println((m - lastPulseTime));
      D_print("Entering sleep mode...");
      D_println(detectedPattern);
      detectedPattern = "";
      //attachInterrupt(digitalPinToInterrupt(pinLock), handleLock, RISING);  //RISING
      //attachInterrupt(digitalPinToInterrupt(pinUnLock), handleUnLock, RISING);
      //delay(100);
      unlockState = IDLE;
      lockState = IDLE;
      learningMode=false;
      enterSleep();
    }
  }
#if DEBUG
  delay(10);
#else
  delay(10);
#endif
}

// Function to handle pulses on pinX or pinY
void handlePulse(char pulseType) {
  unsigned long currentTime = millis();
D_print(currentTime);D_print(" - ");D_print(lastPulseTime);D_print(" = ");D_println((currentTime - lastPulseTime) );
  if ((currentTime - lastPulseTime) > patternTimeout) {
    detectedPattern = "";
    D_println("Pattern Clear");
  }
  lastPulseTime = currentTime;
  //sleepMode = false;
  detectedPattern += pulseType;

  //D_print(pulseType);
}

// Function to check the detected pattern and control the output
void checkPatternAndOutput() {
  if (detectedPattern.length() == 0) {
    return;
  }
  if (digitalRead(pinACC) == HIGH) {
    return;
  }
  if (learningMode) {
    return;
  }

  if (detectedPattern.equals(targetPattern)) {
    D_println("Trunk Open");
    detectedPattern = "";
    openTrunk();
  } else if (detectedPattern.equals(learningPattern)) {
    D_println("Learning Mode");
    learningMode = true;
    detectedPattern = "";
    openTrunk();
  } else if (detectedPattern.equals(resetPattern) || detectedPattern.equals(resetPattern2)) {
    D_println("Reset Pattern");
    targetPattern = defaultPattern;
    detectedPattern = "";
    saveData(targetPattern);
    openTrunk();
  }
}

void openTrunk() {
  digitalWrite(pinTrukRelay, HIGH);
  digitalWrite(LED_BUILTIN, LOW);
  delay(500);
  digitalWrite(pinTrukRelay, LOW);
  digitalWrite(LED_BUILTIN, HIGH);
}

void doubleBeep() {
  digitalWrite(pinTrukRelay, HIGH);
  digitalWrite(LED_BUILTIN, LOW);
  delay(300);
  digitalWrite(pinTrukRelay, LOW);
  digitalWrite(LED_BUILTIN, HIGH);
  delay(300);
  digitalWrite(pinTrukRelay, HIGH);
  digitalWrite(LED_BUILTIN, LOW);
  delay(300);
  digitalWrite(pinTrukRelay, LOW);
  digitalWrite(LED_BUILTIN, HIGH);
  lastPulseTime = millis();
}

void tripleBeep() {
  digitalWrite(pinTrukRelay, HIGH);
  digitalWrite(LED_BUILTIN, LOW);
  delay(300);
  digitalWrite(pinTrukRelay, LOW);
  digitalWrite(LED_BUILTIN, HIGH);
  delay(300);
  digitalWrite(pinTrukRelay, HIGH);
  digitalWrite(LED_BUILTIN, LOW);
  delay(300);
  digitalWrite(pinTrukRelay, LOW);
  digitalWrite(LED_BUILTIN, HIGH);
  delay(300);
  digitalWrite(pinTrukRelay, HIGH);
  digitalWrite(LED_BUILTIN, LOW);
  delay(300);
  digitalWrite(pinTrukRelay, LOW);
  digitalWrite(LED_BUILTIN, HIGH);
  //lastPulseTime = millis();
}

void enterSleep() {
  // return;
  wifi_station_disconnect();
  wifi_set_opmode_current(NULL_MODE);
  wifi_fpm_set_sleep_type(LIGHT_SLEEP_T);                               // set sleep type, the above    posters wifi_set_sleep_type() didnt seem to work for me although it did let me compile and upload with no errors
  wifi_fpm_open();                                                      // Enables force sleep
  gpio_pin_wakeup_enable(GPIO_ID_PIN(pinLock), GPIO_PIN_INTR_HILEVEL);  // GPIO_ID_PIN(2) corresponds to GPIO2 on ESP8266-01 , GPIO_PIN_INTR_LOLEVEL for a logic low GPIO_PIN_INTR_HILEVEL for logic high, can also do other interrupts, see gpio.h above
  gpio_pin_wakeup_enable(GPIO_ID_PIN(pinUnLock), GPIO_PIN_INTR_HILEVEL);
  wifi_fpm_set_wakeup_cb(fpm_wakup_cb_func1);  // Set wakeup callback
  wifi_fpm_do_sleep(0xFFFFFFF);                // Sleep for longest possible time

  /*
    GPIO_PIN_INTR_POSEDGE: Triggers on a rising edge (from low to high).
    GPIO_PIN_INTR_NEGEDGE: Triggers on a falling edge (from high to low).
    GPIO_PIN_INTR_ANYEDGE: Triggers on both rising and falling edges.
    GPIO_PIN_INTR_LOLEVEL: Triggers when the pin is held at a low level (0V or logic LOW).
    GPIO_PIN_INTR_HILEVEL: Triggers when the pin is held at a high level (3.3V or logic HIGH).
    */
}

// Function to save data to LittleFS
void saveData(String data) {
  File file = LittleFS.open(filePath, "w");  // Open file for writing
  if (!file) {
    D_println("Failed to open file for writing");
    return;
  }
  file.print(data);  // Write data to file
  file.close();
  D_println("Data saved to LittleFS.");
}

// Function to read data from LittleFS
String readData() {
  File file = LittleFS.open(filePath, "r");  // Open file for reading
  if (!file) {
    D_println("Failed to open file for reading");
    return "";
  }

  String data = "";
  while (file.available()) {
    data += (char)file.read();  // Read each character and build the string
  }
  file.close();
  return data;
}
