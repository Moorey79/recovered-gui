#include <max6675.h>
#include <SPI.h>
#include <EEPROM.h>
//<App !Start!>
// FILE: [gui.ino]
// Created by GUIslice Builder version: [0.17.b40]
//
// GUIslice Builder Generated File
//
// For the latest guides, updates and support view:
// https://github.com/ImpulseAdventure/GUIslice
//
//<App !End!>

// ------------------------------------------------
// Headers to include
// ------------------------------------------------
#include "gui_GSLC.h"
#include "GUIslice.h"
#include "GUIslice_drv.h"


// --- Pin Definitions ---
const int heaterControlPin = 46;
const int pumpControlPin = 44;
const int thermocoupleDO = 50;   // MISO
const int thermocoupleCS = 48;   // CS
const int thermocoupleCLK = 52;  // SCK

// --- EEPROM Addresses ---
#define EEPROM_PUMP_START 0
#define EEPROM_PUMP_STOP 2
#define EEPROM_HEATER_STOP 4
#define EEPROM_RUNTIME_1 6
#define EEPROM_RUNTIME_2 10 // +4 bytes
#define EEPROM_RUNTIME_3 14 // +4 bytes
#define EEPROM_RUNTIME_4 18 // +4 bytes
#define EEPROM_RUNTIME_5 22 // +4 bytes
#define EEPROM_RUNTIME_COUNT 26 // To store how many runtimes have been recorded

// --- Setpoint Variables ---
int pumpStartSetpoint;
int pumpStopSetpoint;
int heaterStopSetpoint;

// --- Hysteresis ---
const float pumpStartHysteresis = 2.0;

// Callback for the hidden reset runs button
bool CbResetRunsBtn(void* pvGui, void* pvElemRef, gslc_teTouch eTouch, int16_t nX, int16_t nY) {
  if (eTouch == GSLC_TOUCH_UP_IN) {
    Serial.println(F("Hidden reset runs button touched!")); // For debugging
    resetRuntimes();
  }
  return true;
}


// --- State Tracking Variables ---
bool processStarted = false;
bool heaterIsOn = false;
bool pumpIsOn = false;
static bool pumpHasBeenOnOnce = false;
int currentTemperature = 25;

// --- Timer Variables ---
unsigned long heaterOffTimer = 0;
unsigned long processStartTime = 0;

//previous runs variables
unsigned long lastRunTimes[5];
byte runtimeRecordCount = 0;
byte runtimeSaveIndex = 0;

// Predicted runtime variable
unsigned long averagePredictedRuntime = 0;
unsigned long countdownStartTime = 0;
bool countdownRunning = false;

// Predicted reset
unsigned long resetFlashStartTime = 0;
bool resetFlashActive = false;
const unsigned long resetFlashDuration = 1000; // Duration of the flash in milliseconds (1 second)
gslc_tsColor resetFlashColor = GSLC_COL_YELLOW; // Or any other GSLC_COL_ constant
// --- Condition Flags ---
bool heaterOffConditionMet = false;
unsigned long lastTempReadTime = 0;
const unsigned long tempReadInterval = 2000;   // 2000 milliseconds = 2 seconds
bool timerIsRunning = false; // New flag to control timer updates

// --- MAX6675 Thermocouple Object ---
MAX6675 thermocouple(thermocoupleCLK, thermocoupleCS, thermocoupleDO);

// --- Temperature Averaging Variables ---
#define NUM_READINGS 10
float readings[NUM_READINGS];
int readIndex = 0;

// --- SPI Settings ---
SPISettings slowSPI(500000, MSBFIRST, SPI_MODE0);   // 500 kHz

// Program Globals
// ------------------------------------------------
char tempStr[10];
char runtimeStr[12];

// --- Debug Flag ---
bool enableDebugSerial = false; // Set to true to enable detailed serial output

// Save some element references for direct access
//<Save_References !Start!>
gslc_tsElemRef* HeaterSTOPslide = NULL;
gslc_tsElemRef* HeaterSTOPval = NULL;
gslc_tsElemRef* PumpSTARTslide = NULL;
gslc_tsElemRef* PumpSTARTval = NULL;
gslc_tsElemRef* PumpSTOPslide = NULL;
gslc_tsElemRef* PumpSTOPval = NULL;
gslc_tsElemRef* m_HeatStat6 = NULL;
gslc_tsElemRef* m_PumpStat6 = NULL;
gslc_tsElemRef* m_heattog = NULL;
gslc_tsElemRef* m_pElemBtn1 = NULL;
gslc_tsElemRef* m_pElemBtn1_4 = NULL;
gslc_tsElemRef* m_pElemOutTime = NULL;
gslc_tsElemRef* m_pElemOutTxt20 = NULL;
gslc_tsElemRef* m_pElemProgress2 = NULL;
gslc_tsElemRef* m_pElemTextbox2_3_6 = NULL;
gslc_tsElemRef* m_pElemTextbox2_4_7 = NULL;
gslc_tsElemRef* m_pElemTextbox2_5 = NULL;
gslc_tsElemRef* m_pElem_ttg = NULL;
gslc_tsElemRef* m_pumptog = NULL;
gslc_tsElemRef* runBACKbtn = NULL;
gslc_tsElemRef* spBACKbtn = NULL;
gslc_tsElemRef* m_pElemEstTime = NULL; // Add this line

// Declare E_RESET_RUNS_BTN as a pointer.  Crucially, remove it from the enum.
gslc_tsElemRef* m_pE_RESET_RUNS_BTN;
//<Save_References !End!>

// Define debug message function
static int16_t DebugOut(char ch) {
  if (enableDebugSerial) {
    if (ch == (char)'\n') Serial.println("");
    else Serial.write(ch);
  }
  return 0;
}

// ------------------------------------------------
// Callback Methods
// ------------------------------------------------
// Common Button callback
bool CbBtnCommon(void* pvGui, void* pvElemRef, gslc_teTouch eTouch, int16_t nX, int16_t nY) {
  gslc_tsGui* pGui = (gslc_tsGui*)(pvGui);
  gslc_tsElemRef* pElemRef = (gslc_tsElemRef*)(pvElemRef);
  gslc_tsElem* pElem = gslc_GetElemFromRef(pGui, pElemRef);

  static unsigned long setpointsBtnPressTime = 0;
  static bool setpointsBtnHeld = false;


if (pElem->nId == E_ELEM_BTN4) { // SetPoints Button
    static unsigned long touchStartTime = 0;
    static bool isHolding = false;
    const unsigned long holdDuration = 5000; // 5 seconds
    const unsigned long flashInterval = 500; // 500 milliseconds
    static unsigned long lastFlashTime = 0;
    static bool flashState = false;

    if (eTouch == GSLC_TOUCH_DOWN) {
      touchStartTime = millis();
      isHolding = false;
      lastFlashTime = 0;
      flashState = false;
      if (enableDebugSerial) {}
    } else if (eTouch == GSLC_TOUCH_UP_OUT || eTouch == GSLC_TOUCH_UP_IN) {
      if (isHolding) {
        if (enableDebugSerial) {}
        resetRuntimes();
        isHolding = false;
        gslc_SetBkgndColor(&m_gui, GSLC_COL_BLACK); // Revert background
        gslc_Update(&m_gui);
      } else {
        gslc_SetPageCur(&m_gui, SetPoints);
        if (enableDebugSerial) {}
      }
    } else if (eTouch == GSLC_TOUCH_MOVE) {
      unsigned long currentTime = millis();
      // Check for hold duration and start flashing
      if (!isHolding && (currentTime - touchStartTime > holdDuration)) {
        if (enableDebugSerial) {}
        isHolding = true;
      }

      // Handle flashing if holding
      if (isHolding && (currentTime - lastFlashTime > flashInterval)) {
        lastFlashTime = currentTime;
        flashState = !flashState;
        gslc_SetBkgndColor(&m_gui, flashState ? resetFlashColor : GSLC_COL_BLACK);
        gslc_Update(&m_gui);
      }
    }
    return true;
  }

  if (eTouch == GSLC_TOUCH_UP_IN) {
    unsigned long avgHours = 0;
    unsigned long avgMinutes = 0;
    unsigned long avgSeconds = 0;
    switch (pElem->nId) {
      case E_StartBtn2:
        if (enableDebugSerial) {}
        // Save setpoints to EEPROM when the Start button is pressed
        EEPROM.write(EEPROM_PUMP_START, pumpStartSetpoint);
        EEPROM.write(EEPROM_PUMP_STOP, pumpStopSetpoint);
        EEPROM.write(EEPROM_HEATER_STOP, heaterStopSetpoint);

        // Set the heater state to ON
        heaterIsOn = true;

        processStartTime = millis(); // Record the start time

        // Calculate average predicted runtime
        averagePredictedRuntime = calculateAverageRuntime();
        if (enableDebugSerial) {}
        avgHours = (averagePredictedRuntime / (1000UL * 60 * 60));
        avgMinutes = (averagePredictedRuntime / (1000UL * 60)) % 60;
        avgSeconds = (averagePredictedRuntime / 1000) % 60;
        sprintf(runtimeStr, "%lu:%02lu:%02lu", avgHours, avgMinutes, avgSeconds);
        if (enableDebugSerial) {}

        if (m_pElemEstTime != NULL) {
          gslc_ElemSetTxtStr(&m_gui, m_pElemEstTime, runtimeStr);
        }
        countdownStartTime = millis();
        countdownRunning = true;

        gslc_SetPageCur(&m_gui, Running);
        processStarted = true; // Set the start flag
        timerIsRunning = true;
        break;
      case E_ELEM_BTN4:
        gslc_SetPageCur(&m_gui, SetPoints);
        break;
      case E_StartBtn:
        gslc_SetPageCur(&m_gui, E_PG_MAIN);
        processStarted = false; // Reset process flag when going back
        break;
      case E_Pumptog:
        if (eTouch == GSLC_TOUCH_UP_IN) {
          if (heaterIsOn || (currentTemperature >= pumpStopSetpoint && pumpIsOn)) {
            // Prevent the user from turning the pump OFF if the heater is ON
            // OR if the temperature is at or above the pump stop setpoint AND the pump is ON
            gslc_ElemXTogglebtnSetState(&m_gui, m_pumptog, true); // Force it back to ON
            if (enableDebugSerial) {}
          } else {
            // Allow the toggle to proceed as normal (turning OFF)
            pumpIsOn = gslc_ElemXTogglebtnGetState(&m_gui, m_pumptog);
          }
          // If the toggle is not prevented from changing, update pumpIsOn
          if (!heaterIsOn && !(currentTemperature >= pumpStopSetpoint && pumpIsOn)) {
            pumpIsOn = gslc_ElemXTogglebtnGetState(&m_gui, m_pumptog);
          }
        }
        break;
      case E_Heattog:
        // User is manually controlling the heater state via the toggle
        heaterIsOn = gslc_ElemXTogglebtnGetState(&m_gui, m_heattog);
        break;
      case E_ELEM_BTN7:
        gslc_SetPageCur(&m_gui, E_PG_MAIN);
        processStarted = false; // Reset process flag when going back
        break;
      default:
        break;
    }
  }
  return true;
}
//<Checkbox Callback !Start!>
//<Checkbox Callback !End!>
//<Keypad Callback !Start!>
//<Keypad Callback !End!>
//<Spinner Callback !Start!>
//<Spinner Callback !End!>
//<Listbox Callback !Start!>
//<Listbox Callback !End!>
//<Draw Callback !Start!>
//<Draw Callback !End!>


// Callback function for when a slider's position has been updated
bool CbSlidePos(void* pvGui, void* pvElemRef, int16_t nPos) {
  gslc_tsGui* pGui = (gslc_tsGui*)(pvGui);
  gslc_tsElemRef* pElemRef = (gslc_tsElemRef*)(pvElemRef);
  gslc_tsElem* pElem = gslc_GetElemFromRef(pGui, pElemRef);
  String sVal; // Declare sVal here

  switch (pElem->nId) {
    case E_Pump_Seek:
      pumpStartSetpoint = nPos; // Directly update the setpoint variable
      if (enableDebugSerial) {}
      sVal = String(pumpStartSetpoint);
      gslc_ElemSetTxtStr(pGui, PumpSTARTval, sVal.c_str()); // Update the display
      gslc_ElemDrawByRef(pGui, PumpSTARTval, GSLC_REDRAW_FULL);
      //EEPROM.write(EEPROM_PUMP_START, pumpStartSetpoint); // Save to EEPROM
      break;
    case E_Heat_Seek:
      heaterStopSetpoint = nPos; // Directly update the setpoint variable
      sVal = String(heaterStopSetpoint);
      gslc_ElemSetTxtStr(pGui, HeaterSTOPval, sVal.c_str()); // Update the display
      gslc_ElemDrawByRef(pGui, HeaterSTOPval, GSLC_REDRAW_FULL);
      //EEPROM.write(EEPROM_HEATER_STOP, heaterStopSetpoint); // Save to EEPROM
      break;
    case E_ELEM_SEEKBAR7: // Pump Stop Slider
      pumpStopSetpoint = nPos; // Directly update the setpoint variable
      sVal = String(pumpStopSetpoint);
      gslc_ElemSetTxtStr(pGui, PumpSTOPval, sVal.c_str()); // Update the display
      gslc_ElemDrawByRef(pGui, PumpSTOPval, GSLC_REDRAW_FULL);
      //EEPROM.write(EEPROM_PUMP_STOP, pumpStopSetpoint); // Save to EEPROM
      break;
    default:
      break;
  }
  return true;
}

// Function to sort a float array (Bubble Sort)
void sortFloatArray(float arr[], int n) {
  for (int i = 0; i < n - 1; i++) {
    for (int j = 0; j < n - i - 1; j++) {
      if (arr[j] > arr[j + 1]) {
        // Swap arr[j] and arr[j+1]
        float temp = arr[j];
        arr[j] = arr[j + 1];
        arr[j + 1] = temp;
      }
    }
  }
}

// Function to calculate the average, discarding the lowest and highest N values
float calculateTrimmedAverageManualSort(float data[], int size, int trimCount) {
  if (size <= 2 * trimCount) {
    // Not enough data to trim and average
    return NAN;
  }
  float sortedData[size];
  for (int i = 0; i < size; i++) {
    sortedData[i] = data[i];
  }
  sortFloatArray(sortedData, size); // Use the manual sort function

  float sum = 0;
  for (int i = trimCount; i < size - trimCount; i++) {
    sum += sortedData[i];
  }
  return sum / (size - 2 * trimCount);
}

// Function to save runtimes to EEPROM
void EEPROM_writeULong(int address, unsigned long value) {
  EEPROM.write(address, (value >> 24) & 0xFF);
  EEPROM.write(address + 1, (value >> 16) & 0xFF);
  EEPROM.write(address + 2, (value >> 8) & 0xFF);
  EEPROM.write(address + 3, value & 0xFF);
}

// Function to read runtimes from EEPROM
unsigned long EEPROM_readULong(int address) {
  unsigned long value = 0;
  value = ((unsigned long)EEPROM.read(address) << 24) |
          ((unsigned long)EEPROM.read(address + 1) << 16) |
          ((unsigned long)EEPROM.read(address + 2) << 8) |
          (unsigned long)EEPROM.read(address + 3);
  return value;
}

// Function to calculate the average runtime from EEPROM
unsigned long calculateAverageRuntime() {
  byte count = EEPROM.read(EEPROM_RUNTIME_COUNT);
  if (count == 0) {
    return 0; // No runtimes recorded yet
  }

  unsigned long totalRuntime = 0;
  for (int i = 0; i < count; i++) {
     int address;
    switch (i) {
      case 0: address = EEPROM_RUNTIME_1; break;
      case 1: address = EEPROM_RUNTIME_2; break;
      case 2: address = EEPROM_RUNTIME_3; break;
      case 3: address = EEPROM_RUNTIME_4; break;
      case 4: address = EEPROM_RUNTIME_5; break;
    }
    totalRuntime += EEPROM_readULong(address);
  }

  return totalRuntime / count;
}

// --- Function to reset the last 5 runtimes in EEPROM ---
void resetRuntimes() {
  if (enableDebugSerial) {}
  EEPROM_writeULong(EEPROM_RUNTIME_1, 0);
  EEPROM_writeULong(EEPROM_RUNTIME_2, 0);
  EEPROM_writeULong(EEPROM_RUNTIME_3, 0);
  EEPROM_writeULong(EEPROM_RUNTIME_4, 0);
  EEPROM_writeULong(EEPROM_RUNTIME_5, 0);
  EEPROM.write(EEPROM_RUNTIME_COUNT, 0);
  runtimeRecordCount = 0;
  runtimeSaveIndex = 0;
  averagePredictedRuntime = 0;
  if (enableDebugSerial) {}

  // Add the screen flash here using the global color
  flashScreen(resetFlashColor, 200);
}

// Function to flash the screen
void flashScreen(gslc_tsColor color, int duration) {
  gslc_SetBkgndColor(&m_gui, color);
  gslc_Update(&m_gui);
  delay(duration);
  gslc_SetBkgndColor(&m_gui, GSLC_COL_BLACK); // Revert to your default background color
  gslc_Update(&m_gui);
}

// Callback function for touching the PumpSTOPval text
bool CbResetRuntimesTouch(void* pvGui, void* pvElemRef, gslc_teTouch eTouch, int16_t nX, int16_t nY) {
  if (eTouch == GSLC_TOUCH_UP_IN) {
    resetRuntimes();
    // Optionally add a visual confirmation here, e.g., a quick screen flash
  }
  return true;
}

//<Tick Callback !Start!>
//<Tick Callback !End!>

void setup() {
  // ------------------------------------------------
  // Initialize
  // ------------------------------------------------
  // Load saved setpoints from EEPROM
  processStarted = false;

    // --- Initialize Heater and Pump Pins ---
  pinMode(heaterControlPin, OUTPUT);
  pinMode(pumpControlPin, OUTPUT);
  digitalWrite(heaterControlPin, LOW); // Start with both OFF
  digitalWrite(pumpControlPin, LOW);
  heaterIsOn = false;
  pumpIsOn = false;

  Serial3.begin(57600);
  // Wait for USB Serial
  //delay(1000);   // NOTE: Some devices require a delay after Serial.begin() before serial port can be used
  Serial.begin(57600);   // Set your desired baud rate for Serial

  pumpStartSetpoint = EEPROM.read(EEPROM_PUMP_START); // Correct way to read
  pumpStopSetpoint = EEPROM.read(EEPROM_PUMP_STOP);   // Correct way to read
  heaterStopSetpoint = EEPROM.read(EEPROM_HEATER_STOP); // Correct way to read
  if (enableDebugSerial) {}
  //gslc_Init(&m_gui, DISP_WIDTH, DISP_HEIGHT, (gslc_tsGui*)NULL, 0);


  // ... Initialize your GUI elements, including m_pumptog and m_heattog ...

  gslc_InitDebug(&DebugOut);

  // ------------------------------------------------
  // Create graphic elements
  // ------------------------------------------------
  InitGUIslice_gen();
    gslc_tsElemRef* pElemRef = NULL;
  delay(100); // Insert the delay here, after InitGUIslice_gen()

  // ------------------------------------------------
  // Initialize sliders and text values with loaded setpoints
  // ------------------------------------------------
  if (enableDebugSerial) {}
  if (enableDebugSerial) {}
  if (PumpSTARTslide != NULL && PumpSTARTval != NULL) {
    gslc_ElemXSeekbarSetPos(&m_gui, PumpSTARTslide, pumpStartSetpoint);
    String sVal = String(pumpStartSetpoint);
    gslc_ElemSetTxtStr(&m_gui, PumpSTARTval, sVal.c_str());
    gslc_ElemDrawByRef(&m_gui, PumpSTARTval, GSLC_REDRAW_FULL);
    if (enableDebugSerial) {}
  } else {
  }

  if (enableDebugSerial) {}
  if (enableDebugSerial) {}
  if (PumpSTOPslide != NULL && PumpSTOPval != NULL) {
    gslc_ElemXSeekbarSetPos(&m_gui, PumpSTOPslide, pumpStopSetpoint);
    String sVal = String(pumpStopSetpoint);
    gslc_ElemSetTxtStr(&m_gui, PumpSTOPval, sVal.c_str());
    gslc_ElemDrawByRef(&m_gui, PumpSTOPval, GSLC_REDRAW_FULL);
    if (enableDebugSerial) {}
  } else {
  }

  if (enableDebugSerial) {}
  if (enableDebugSerial) {}
  if (HeaterSTOPslide != NULL && HeaterSTOPval != NULL) {
    gslc_ElemXSeekbarSetPos(&m_gui, HeaterSTOPslide, heaterStopSetpoint);
    String sVal = String(heaterStopSetpoint);
    gslc_ElemSetTxtStr(&m_gui, HeaterSTOPval, sVal.c_str());
    gslc_ElemDrawByRef(&m_gui, HeaterSTOPval, GSLC_REDRAW_FULL);
    if (enableDebugSerial) {}
  } else {
  }

  for (int i = 0; i < NUM_READINGS; i++) {
    readings[i] = NAN; // Initialize all readings to NaN
  }


  // ... (rest of your setup code that uses the GUI elements) ...
}

// -----------------------------------
// Main event loop
// -----------------------------------
void loop() {
  // Check for incoming Serial commands (from USB/main serial)
  if (Serial.available() > 0) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    Serial3.print(F("Received command from Serial: "));
    Serial3.println(command);
    if (command == "pump_on") { gslc_ElemXTogglebtnSetState(&m_gui, m_pumptog, true); }
    else if (command == "pump_off") { gslc_ElemXTogglebtnSetState(&m_gui, m_pumptog, false); }
    else if (command == "heat_on") { gslc_ElemXTogglebtnSetState(&m_gui, m_heattog, true); }
    else if (command == "heat_off") { gslc_ElemXTogglebtnSetState(&m_gui, m_heattog, false); }
    else { Serial3.println(F("Unknown command received on Serial")); }
  }

  // Check for incoming Serial commands from ESP (on Serial3)
  if (Serial3.available() > 0) {
    String commandFromESP = Serial3.readStringUntil('\n');
    commandFromESP.trim();
    Serial.print(F("Received from ESP: ")); // Print to main serial for debugging
    Serial.println(commandFromESP);

    if (commandFromESP == "pump_on") {
      gslc_ElemXTogglebtnSetState(&m_gui, m_pumptog, true);
      pumpIsOn = true;
      digitalWrite(pumpControlPin, HIGH);
    } else if (commandFromESP == "pump_off") {
      gslc_ElemXTogglebtnSetState(&m_gui, m_pumptog, false);
      pumpIsOn = false;
      digitalWrite(pumpControlPin, LOW);
    } else if (commandFromESP == "heat_on") {
      gslc_ElemXTogglebtnSetState(&m_gui, m_heattog, true);
      heaterIsOn = true;
      digitalWrite(heaterControlPin, HIGH);
    } else if (commandFromESP == "heat_off") {
      gslc_ElemXTogglebtnSetState(&m_gui, m_heattog, false);
      heaterIsOn = false;
      digitalWrite(heaterControlPin, LOW);
    } else if (commandFromESP == "get_status") {
      // If the ESP requests the status, send it back immediately
      String statusString = "";
      statusString += "PUMP:";
      statusString += pumpIsOn ? "ON" : "OFF";
      statusString += ",HEATER:";
      statusString += heaterIsOn ? "ON" : "OFF";
      statusString += ",TEMP:";
      statusString += currentTemperature;
      Serial3.println(statusString);
      Serial.print(F("Sent status to ESP (on request): "));
      Serial.println(statusString);
    } else {
      Serial.print(F("Unknown command from ESP: "));
      Serial.println(commandFromESP);
    }
  }

  if (resetFlashActive) {
    gslc_SetBkgndColor(&m_gui, resetFlashColor);
    gslc_Update(&m_gui);
    if (millis() - resetFlashStartTime >= resetFlashDuration) {
      resetFlashActive = false;
      gslc_SetBkgndColor(&m_gui, GSLC_COL_BLACK); // Set back to your default background color (assuming black)
      gslc_Update(&m_gui);
    }
  }

  gslc_Update(&m_gui);

  if (m_heattog != NULL) {
    gslc_ElemXTogglebtnSetState(&m_gui, m_heattog, heaterIsOn);
  }
  if (m_pumptog != NULL) {
    gslc_ElemXTogglebtnSetState(&m_gui, m_pumptog, pumpIsOn);
  }

  unsigned long currentMillis = millis();
  static unsigned long espUpdateTimer = 0;
  const long espUpdateInterval = 10000; // 10 seconds

    if (currentMillis - lastTempReadTime >= tempReadInterval) {
      lastTempReadTime = currentMillis;
      delay(50);

      // --- Read Temperature ---
      digitalWrite(thermocoupleCS, LOW);
      delayMicroseconds(50);
      SPI.beginTransaction(slowSPI);
      float currentReading = thermocouple.readCelsius();
      SPI.endTransaction();
      digitalWrite(thermocoupleCS, HIGH);

      if (enableDebugSerial) {}

      if (!isnan(currentReading)) {
        readings[readIndex] = currentReading;
        readIndex = (readIndex + 1) % NUM_READINGS;
        float avgTemp = calculateTrimmedAverageManualSort(readings, NUM_READINGS, 2);
        if (!isnan(avgTemp)) {
          currentTemperature = round(avgTemp);
          if (enableDebugSerial) {}
        } else {
          currentTemperature = round(currentReading);
          if (enableDebugSerial) {}
        }
        if (m_pElemOutTxt20 != NULL) {
          sprintf(tempStr, "%d °C", currentTemperature);
          gslc_ElemSetTxtStr(&m_gui, m_pElemOutTxt20, tempStr);
        }
      } else {
        if (m_pElemOutTxt20 != NULL) {
          gslc_ElemSetTxtStr(&m_gui, m_pElemOutTxt20, "Error!");
        }
      }

      if (processStarted && countdownRunning && m_pElem_ttg != NULL) { // Use m_pElem_ttg here
        unsigned long currentTime = millis();
        long elapsedTime = currentTime - countdownStartTime;
        long remainingTimeMillis = averagePredictedRuntime - elapsedTime;

        if (remainingTimeMillis <= 0) {
          remainingTimeMillis = 0;
          countdownRunning = false;
        }

        unsigned long estHours = (remainingTimeMillis / (1000UL * 60 * 60));
        unsigned long estMinutes = (remainingTimeMillis / (1000UL * 60)) % 60;
        unsigned long estSeconds = (remainingTimeMillis / 1000) % 60;
        char estTimeStr[12];
        sprintf(estTimeStr, "%lu:%02lu:%02lu", estHours, estMinutes, estSeconds);
        gslc_ElemSetTxtStr(&m_gui, m_pElem_ttg, estTimeStr);
      }
    }
      // --- Targeted Debugging Logs (with Timer) - Activated when process starts ---
    if (processStarted) {
      unsigned long elapsedTime = currentMillis - processStartTime;
      unsigned long seconds = (elapsedTime / 1000) % 60;
      unsigned long minutes = (elapsedTime / (1000UL * 60)) % 60;
      unsigned long hours = (elapsedTime / (1000UL * 60 * 60));
      sprintf(runtimeStr, "%lu:%02lu:%02lu", hours, minutes, seconds);

      if (enableDebugSerial) {
      }
    }

  // --- Heater Control ---
    if (processStarted && heaterIsOn) {
      if (enableDebugSerial) {
      }
      if (currentTemperature >= heaterStopSetpoint) {
        digitalWrite(heaterControlPin, LOW);
        heaterIsOn = false;
        gslc_ElemXTogglebtnSetState(&m_gui, m_heattog, false);
        timerIsRunning = false; // Stop the timer when the process (heating) finishes

        // --- Save the runtime ---
        unsigned long elapsedTime = currentMillis - processStartTime;
        int saveAddress;
        switch (runtimeSaveIndex) {
          case 0: saveAddress = EEPROM_RUNTIME_1; break;
          case 1: saveAddress = EEPROM_RUNTIME_2; break;
          case 2: saveAddress = EEPROM_RUNTIME_3; break;
          case 3: saveAddress = EEPROM_RUNTIME_4; break;
          case 4: saveAddress = EEPROM_RUNTIME_5; break;
        }
        EEPROM_writeULong(saveAddress, elapsedTime);

        if (runtimeRecordCount < 5) {
          runtimeRecordCount++;
          EEPROM.write(EEPROM_RUNTIME_COUNT, runtimeRecordCount);
        }
        runtimeSaveIndex = (runtimeSaveIndex + 1) % 5; // Cycle through the save slots
        unsigned long hours = (elapsedTime / (1000UL * 60 * 60));
        unsigned long minutes = (elapsedTime / (1000UL * 60)) % 60;
        unsigned long seconds = (elapsedTime / 1000) % 60;
        sprintf(runtimeStr, "%lu:%02lu:%02lu", hours, minutes, seconds);
      } else {
        if (enableDebugSerial) {}
      }
    } else if (processStarted && !heaterIsOn) {
      // Heater ON condition is checked here, but the action is only to log in the DEBUG section now.
      if (enableDebugSerial && currentTemperature < heaterStopSetpoint) {
      }
    } else {
      if (enableDebugSerial) {}
    }

    // --- Pump Control ---
    if (enableDebugSerial) {}
    if (enableDebugSerial) {}
    if (enableDebugSerial) {}
    if (enableDebugSerial) {}
    if (enableDebugSerial) {}
    if (enableDebugSerial) {}
    if (enableDebugSerial) {}

    if (processStarted) {
      // --- Automatic Pump Turn ON based on Temperature ---
      if (!pumpIsOn) {
        if (enableDebugSerial) {}
        if (!pumpHasBeenOnOnce) {
          if (enableDebugSerial) {}
          if (currentTemperature > pumpStartSetpoint) {
            digitalWrite(pumpControlPin, HIGH);
            pumpIsOn = true;
            pumpHasBeenOnOnce = true;
            gslc_ElemXTogglebtnSetState(&m_gui, m_pumptog, true);
            if (enableDebugSerial) {}
            if (enableDebugSerial) {}
          } else {
            if (enableDebugSerial) {}
          }
        } else {
          if (enableDebugSerial) {}
          if (currentTemperature > (pumpStartSetpoint + pumpStartHysteresis)) {
            digitalWrite(pumpControlPin, HIGH);
            pumpIsOn = true;
            gslc_ElemXTogglebtnSetState(&m_gui, m_pumptog, true);
            if (enableDebugSerial) {}
          } else {
            if (enableDebugSerial) {}
          }
        }
      } else {
        if (enableDebugSerial) {}
      }

      // --- Manual Pump Turn OFF via Toggle ---
      if (gslc_ElemXTogglebtnGetState(&m_gui, m_pumptog) == false && pumpIsOn) {
        digitalWrite(pumpControlPin, LOW);
        pumpIsOn = false;
        timerIsRunning = false;
      }

      // --- Automatic Pump Turn OFF based on Temperature (if process is running and toggle is ON) ---
      if (gslc_ElemXTogglebtnGetState(&m_gui, m_pumptog) && !heaterIsOn && currentTemperature < pumpStopSetpoint && pumpIsOn) {
        digitalWrite(pumpControlPin, LOW);
        pumpIsOn = false;
        timerIsRunning = false;
        gslc_ElemXTogglebtnSetState(&m_gui, m_pumptog, false);
      }
    } else {
      if (enableDebugSerial) {}
      digitalWrite(pumpControlPin, LOW);
      pumpIsOn = false;
      pumpHasBeenOnOnce = false;
      gslc_ElemXTogglebtnSetState(&m_gui, m_pumptog, false);
    }

      // --- Timer Display Update ---
    if (processStarted && timerIsRunning && m_pElemOutTime != NULL) {
      unsigned long elapsedTime = currentMillis - processStartTime;
      unsigned long seconds = (elapsedTime / 1000) % 60;
      unsigned long minutes = (elapsedTime / (1000UL * 60)) % 60;
      unsigned long hours = (elapsedTime / (1000UL * 60 * 60));
      sprintf(runtimeStr, "%lu:%02lu:%02lu", hours, minutes, seconds);
      gslc_ElemSetTxtStr(&m_gui, m_pElemOutTime, runtimeStr);
    }

    // --- Send status to ESP every 10 seconds ---
    if (currentMillis - espUpdateTimer >= espUpdateInterval) {
      espUpdateTimer = currentMillis;
      String statusString = "";
      statusString += "PUMP:";
      statusString += pumpIsOn ? "ON" : "OFF";
      statusString += ",HEATER:";
      statusString += heaterIsOn ? "ON" : "OFF";
      statusString += ",TEMP:";
      statusString += currentTemperature;
      Serial3.println(statusString);
      if (enableDebugSerial) {
        Serial.print(F("Sent to ESP: "));
        Serial.println(statusString);
      }
    }
  }