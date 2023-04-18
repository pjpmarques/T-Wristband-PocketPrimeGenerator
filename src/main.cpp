

#include <TFT_eSPI.h>
#include <esp_sleep.h>
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_wifi.h>

// ------------------------------------------------------------------------

// Fonts to be used on the screen
const int PRIME_FONT = 7;                   
const int MSG_FONT = 1;
const int PWR_FONT = 2;
const int ERROR_FONT = 2;

// Pin mappings
const int LED_PIN = 4;                      
const int CHARGE_PIN = 32;
const int TP_PIN_PIN = 33;
const int TP_PWR_PIN = 25;
const int BATT_PIN = 35;

// Other constants
const int VREF = 1100;                      // Vref for the ESP32 ADC
const int SLEEP_INTERVAL = 1000000;         // 1.0 sec (in us) -- the ESP will wakeup every sec to update the display
const int BAT_DELAY = 1000;                 // 1.0 sec (in ms) -- Time it will display the battery status

const long MAX_PRIME = 100001;              // Max size of a prime to calculate
bool* prime;                                // Dynamically defined array for keeping primes
unsigned long primeIndex = 0;               // This points to the next potencial prime to be displayed

TFT_eSPI tft;                               // The TFT display

float battery_voltage;                      // Last read battery voltage
const int V_UPDATE_INTERVAL = 5000;         // Update battery values every 5 seconds
const double V_DIE_THRESOLD = 3.0;          // Threshold for ESP32 to turn off
const double V_MAX_VOLTAGE = 4.34;          // Max battery charge

// ------------------------------------------------------------------------

// Calculate prime numbers using the Sieve of Eratosthenes algorithm. The results are stored in the prime buffer
void calculatePrimes(bool* prime, unsigned long N)
{
    // Initialize all numbers as prime except for 0 and 1    
    memset(prime, true, N);
    prime[0] = false;
    prime[1] = false;
    
    // Perform Sieve of Eratosthenes
    for (unsigned long p = 2; p*p <= N; p++) {
        if (prime[p] == true) {
            // Update all multiples of p as not prime
            for (unsigned long i = p*p; i <= N; i+= p) {
                prime[i] = false;
            }
        }
    }
}


// ------------------------------------------------------------------------

void setup() {
  // Deactivate all devices that we don't need so that we save energy
  esp_bluedroid_disable();
  esp_bt_controller_disable();
  esp_wifi_stop();

  // Start serial for debugging info
  Serial.begin(115200);
  Serial.println("");
  Serial.println("Prime Pocket Calculator -- Alive and kicking!");
  Serial.println();

  // Initialize TFT
  tft.init();
  tft.fillRect(0, 0, TFT_WIDTH, TFT_HEIGHT, TFT_BLACK);
  tft.setRotation(1);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString("Prime pocket calculator", 0, 0, MSG_FONT);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);

  // Configure digital pins (baterry, led and touch)  
  pinMode(LED_PIN, OUTPUT);
  pinMode(CHARGE_PIN, INPUT_PULLUP);
  pinMode(TP_PIN_PIN, INPUT);
  pinMode(TP_PWR_PIN, PULLUP);
  digitalWrite(TP_PWR_PIN, HIGH);  

  // Alocate memory to calculate primes. If not possible, abort and put device to sleep.
  prime = (bool*) malloc(MAX_PRIME);
  if (prime == NULL) {
    Serial.println("Not enough memory for calculating primes.");    
    tft.drawString("Not enough memory!", 0, 3*tft.fontHeight(MSG_FONT), ERROR_FONT);
    esp_light_sleep_start();
  }

  // Calculate prime numbers
  calculatePrimes(prime, MAX_PRIME);

  // Prepare device for sleeping while not actively working
  esp_sleep_enable_timer_wakeup(SLEEP_INTERVAL);
}

// ------------------------------------------------------------------------

// Get the calibrated battery voltage
float getVoltage()
{
  // In this hardware the voltage is provided by a 2:1 voltage voltage divider, from a 3.3 source. Thus, we need to multiply by 2*3.3
  return (analogRead(BATT_PIN) / 4095.0) * 2.0 * 3.3 * (VREF / 1000.0);
}

double lastVoltage = V_DIE_THRESOLD;      // Last voltage that was read from battery
unsigned long lastTime = 0;               // Last time the battery was read
unsigned long timeToDie = 0;              // Time until the battery quits (in seconds)

// Update the voltages being read from battery
void updateVoltages()
{
  // Only updates if more than one second has passed and the voltage reading changed by more than 0.01V
  double currentVoltage = getVoltage();
  unsigned long currentTime = millis();
  if ((abs(currentVoltage - lastVoltage) >= 0.01) && 
      (currentTime - lastTime >= 1000)) {
    
    // Discharge rate in Volts/Second
    double dischargeRate = 1000.0 * (currentVoltage - lastVoltage) / (currentTime - lastTime);
    timeToDie = (unsigned long)(- (currentVoltage - V_DIE_THRESOLD) / dischargeRate);

    lastVoltage = currentVoltage;
    lastTime = currentTime;
  }
}

// Display voltages on the screen when the button is pressed
void displayVoltage() {
  double percentageFull = 0.0;
  if (lastVoltage > V_DIE_THRESOLD) {
    percentageFull = (lastVoltage - V_DIE_THRESOLD) / (V_MAX_VOLTAGE - V_DIE_THRESOLD);
    percentageFull = (percentageFull > 1.0) ? 1.0 : percentageFull;
  }
  
  // Use RED is less than 10%, YELLOW for between 10% and 30%, GREEN in above 30%
  uint32_t fillColor = TFT_GREEN;
  if (percentageFull <= 0.10) {
    fillColor = TFT_RED;    
  } else if (percentageFull <= 0.30) {
    fillColor = TFT_YELLOW;
  }

  // Draw a bar showing the percentage full
  tft.fillRect(0, 0, (int)(TFT_HEIGHT*percentageFull), TFT_WIDTH/2, fillColor);
  tft.fillRect((int)(TFT_HEIGHT*percentageFull), 0, TFT_HEIGHT-(int)(TFT_HEIGHT*percentageFull), TFT_WIDTH/2, TFT_BLACK);
  tft.drawRect(0, 0, TFT_HEIGHT, TFT_WIDTH/2, TFT_BLUE);
  
  // Display battery values on the screen (voltage and percentage)
  char buf[80];  
  sprintf(buf, "Battery: %1.2fV (%3.1f%%)", getVoltage(), percentageFull*100.0);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);  
  tft.drawString(buf, 0, 3*tft.fontHeight(PWR_FONT), PWR_FONT);  

  if (!digitalRead(CHARGE_PIN)) {
    sprintf(buf, "Battery charging");
  } else if (lastVoltage < V_DIE_THRESOLD) {
    sprintf(buf, "Battery empty");
  } else {
    unsigned long timeToDecompose = timeToDie;
    unsigned long hours = timeToDecompose / 3600;
    timeToDecompose%= 3600;
    unsigned long mins = timeToDecompose / 60;

    if (hours >= 24) {
      unsigned long days = hours/24;
      sprintf(buf, "Remaining: %02lu days", days);    
    } else {
      sprintf(buf, "Remaining: %02luh %02lum", hours, mins);    
    }    
  }
  tft.drawString(buf, 0, 4*tft.fontHeight(PWR_FONT), PWR_FONT);  
  
  delay(BAT_DELAY);
}

// ------------------------------------------------------------------------

void loop() {
  // Update and display Voltages if button is pressed
  updateVoltages();

  if (digitalRead(TP_PIN_PIN)) {
    tft.fillRect(0, 0, TFT_HEIGHT, TFT_WIDTH, TFT_BLACK);
    do {
      displayVoltage();
    } while (digitalRead(TP_PIN_PIN));

    // Reset screen to normal mode
    tft.fillRect(0, 0, TFT_HEIGHT, TFT_WIDTH, TFT_BLACK);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString("Prime pocket calculator", 0, 0, MSG_FONT);
  }
  
  // Turn on or off the led according to the charging state of the battery
  digitalWrite(LED_PIN, !digitalRead(CHARGE_PIN));

  // Find next prime to be displayed by checking the prime array (prime[i] is true if i is prime)
  while (prime[primeIndex] == false)
    primeIndex = (primeIndex + 1) % MAX_PRIME;

  // Draw prime to the display  
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString("     ", 0, 2*tft.fontHeight(MSG_FONT), PRIME_FONT);
  tft.drawString(String(primeIndex), 0, 2*tft.fontHeight(MSG_FONT), PRIME_FONT);
  primeIndex = (primeIndex + 1) % MAX_PRIME;

  // Go to sleep for another second
  esp_light_sleep_start();
}
