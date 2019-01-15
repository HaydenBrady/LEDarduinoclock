#ifdef __AVR__
  #include <avr/power.h>
#endif

#include <RTClib.h>
#include <Adafruit_NeoPixel.h>
#include <Wire.h>

// Pins
#define LDR_PIN             A0   // analog pin, PWM: 3, 5, 6, 9, 10, and 11. 
#define HOURS_BUTTON_PIN    2    // pin change interrupt INT0 (D8-D13)
#define MIN_BUTTON_PIN      3    // pin change interrupt INT1 (D8-D13)
#define DIGIT1_PIN          5    // Data to LED for minutes (10's place)
#define DIGIT2_PIN          6    // Data to LED for minutes (1's place) 
#define HOURS_PIN           9    // Data to LEDs for hours
#define SECONDS_PIN         10   // Data to LEDs for seconds 
#define TOGGLE_PIN          11   // pin change for daylight savings time 

// Constants
#define LDR_THRESHOLD_LOW   300 // I'm kinda making this binary 
#define LDR_THRESHOLD_HIGH  600 // with just a low and high val
#define NUMPIXELS_DIGITS    21  // 21 pixels for the digits
#define NUMPIXELS_HOURS     12  // 12 pixels for the hours
#define NUMPIXELS_SECONDS   60  // 60 pixels for the seconds 
#define NUMLEDS_PER_SEGMENT 3   // segments are lines that make up digets 
#define NUMSTRIPS           4   // 4 strips, seconds, minutes, minutes, hours 
#define BRIGHTNESS          10  // this is 10 out of 255, but will be updated later in the code. Brightness is corelated with color 

RTC_DS3231 rtc;       // establishing what kind of RTC I'm using 
DateTime currTime;    // setting date and time
// normal ints 
int currHour;         // you have to have an int to change 
int prevHour = -1;    // just a -1 int for hour, we will use the in the iteration steps
int currMinute;       // you have to have an int to change 
int prevMinute = -1;  // just a -1 int for mintues, we will use the in the iteration steps
int currSecond;       // you have to have an int to change 
int prevSecond = -1;  // just a -1 int for second, we will use the in the iteration steps
int photocellReading; // Photocell is the same as the LDR 

// fancy ints 
bool summerTime;                                  // summertime makes mores sense and is shorter than daylight savings
bool colorChanged = false;                        // A bool holds one of two values, true or false
volatile bool increase_hours_flag = false;        // value can be changed by something beyond the control of the code section, the push button
volatile unsigned long last_increase_hours = 0;   // extended size variable that cant go negative 
volatile bool increase_minutes_flag = false;      // same as the hours 
volatile unsigned long last_increase_minutes = 0; // same as the hours 
unsigned long debounce_delay = 200;               // checking twice in a short period of time to make sure the pushbutton is definitely pressed, in this case, 200 miliseconds 

// Setup NeoPixel library: create the different LED strips
// When we setup the NeoPixel library, we tell it how many pixels, and which pin to use to send signals.
// format is Adafruit_NeoPixel strip = Adafruit_NeoPixel(NUMPIXELS, PIN, NEO_RGB + NEO_KHZ400);
Adafruit_NeoPixel digit1Strip = Adafruit_NeoPixel(NUMPIXELS_DIGITS, DIGIT1_PIN, NEO_RGB + NEO_KHZ400);      // Digit1 
Adafruit_NeoPixel digit2Strip = Adafruit_NeoPixel(NUMPIXELS_DIGITS, DIGIT2_PIN, NEO_RGB + NEO_KHZ400);      // Digit2
Adafruit_NeoPixel hoursStrip = Adafruit_NeoPixel(NUMPIXELS_HOURS, HOURS_PIN, NEO_RGB + NEO_KHZ400);         // Hours 
Adafruit_NeoPixel secondsStrip = Adafruit_NeoPixel(NUMPIXELS_SECONDS, SECONDS_PIN, NEO_RGB + NEO_KHZ400);   // Seconds                                             
Adafruit_NeoPixel *strips [NUMSTRIPS] = { &digit1Strip, &digit2Strip, &hoursStrip, &secondsStrip };         // All strip data 

//uint32_t colorLight = hoursStrip.Color(30, 200, 255); // blue, uint32_t is an unsigned int of 32 bits that is standard across all platforms, so it can carry the LED's color data 
uint32_t colorLight = hoursStrip.Color(255, 128, 0);    // orange If I get this working well I'll add in two more midrange colors (255, 128, 0)
uint32_t colorDark = hoursStrip.Color(24, 0, 0);        // Red (24, 0, 0)
uint32_t colorOff = hoursStrip.Color(0, 0, 0);          // nothing 
uint32_t currColor = colorLight;                        // orange

// Segments:
//   1
// 0   2
//   3
// 4   6
//   5
// Activated segments for digits, the 10 for 0-9, 7 for max number of 1 by 3 lines 
int segments [10][7] = {
    { true,  true,  true,  false, true,  true,  true },      // 0: 0, 1, 2, 4, 5, 6
    { false, false, true,  false, false, false, true },      // 1: 2, 6
    { false, true,  true,  true,  true,  true,  false},      // 2: 1, 2, 3, 4, 5
    { false, true,  true,  true,  false, true,  true },      // 3: 1, 2, 3, 5, 6
    { true,  false, true,  true,  false, false, true },      // 4: 0, 2, 3, 6
    { true,  true,  false, true,  false, true,  true },      // 5: 0, 1, 3, 5, 6
    { true,  true,  false, true,  true,  true,  true },      // 6: 0, 1, 3, 4, 5, 6
    { false, true,  true,  false, false, false, true },      // 7: 1, 2, 6
    { true,  true,  true,  true,  true,  true,  true },      // 8: 0, 1, 2, 3, 4, 5, 6
    { true,  true,  true,  true,  false, true,  true }       // 9: 0, 1, 2, 3, 5, 6
};

void setup() {
    Serial.begin(9600);

    for (int i = 0; i < NUMSTRIPS; i++) {
        strips[i]->begin();                     // This initializes the NeoPixel library.
        strips[i]->setBrightness(BRIGHTNESS);   // calling the brightness value of 10 
        strips[i]->setPixelColor(0, colorDark); // first number is the number of pixels and then color 
        strips[i]->show();                      // Initialize all pixels to 'off'
    }

    pinMode(TOGGLE_PIN, INPUT);   //Reading from the toggle pin 

    // Wait for RTC
    while (! rtc.begin()) {                     // Data transfer takes some time, not much but some 
        // Serial.println("Couldn't find RTC"); // but if you havent realized we're getting the RTC up and running 
        delay(100);                             // Delaying 100 ms 
    }

    rtc.adjust(DateTime(F(__DATE__), F(__TIME__))); // Set the time to the date & time this sketch was compiled, this is the baseline from which the RTC will carry on 

    // Interrupts 
     attachInterrupt(digitalPinToInterrupt(HOURS_BUTTON_PIN), // translates the "actual digital pin" to the specific interrupt number
                     increase_hours_interrupt, RISING);       // +1 hours 
     attachInterrupt(digitalPinToInterrupt(MIN_BUTTON_PIN),   // translates the "actual digital pin" to the specific interrupt number
                     increase_minutes_interrupt, RISING);     // +1 minutes 
}

void loop() { // yo we're actualy starting the code 
  mainLoop(); // initializing main loop 
}

void mainLoop() {                           // actually useing that main loop 
    photocellReading = analogRead(LDR_PIN); // Set color based on LDR reading

    colorChanged = false;                                                  // reading the colorChanged bool 
    if (currColor == colorLight && photocellReading < LDR_THRESHOLD_LOW) { // if orange is the current color and the LDR is less than 300
        currColor = colorDark;                                             // then change the color to dark, in this case red 
        colorChanged = true;                                               // setting color, new bool
    }
    if (currColor == colorDark && photocellReading > LDR_THRESHOLD_HIGH) { // if red is the current color and the LDR is greater than 300
        currColor = colorLight;                                            // then change the color to orange 
        colorChanged = true;                                               // setting color, new bool 
    }

    summerTime = (digitalRead(TOGGLE_PIN) == HIGH); // Read toggle switch for non daylight savings time/summer time

    // Change time
    if (increase_hours_flag) {                  // volatile bool that can be changed by something beyond the control of the code section, the push button
      rtc.adjust(rtc.now() + TimeSpan(3600));   // seconds in an hour 
      increase_hours_flag = false;              // setting volatile bool
    }
    if (increase_minutes_flag) {                // volatile bool that can be changed by something beyond the control of the code section, the push button
      rtc.adjust(rtc.now() + TimeSpan(60));     // seconds in a minute
      increase_minutes_flag = false;            // setting volatile bool
    }

    // Update shown time
    currTime = rtc.now();           // calling the time from RTC
    currHour = currTime.hour();     // update hours
    currMinute = currTime.minute(); // update minutes
    currSecond = currTime.second(); // update seconds 
    
    if (currHour != prevHour || colorChanged) {     // if current hour (is not =) to prevHour(-1), (logical OR for boolean) color has changed
        prevHour = currHour;                        // prevHour now = currHour
        if (summerTime) {                           // Reading from daylight savings time toggle switch
            showHour(currHour, currColor);          // When changing the hour, we want to change the hour then remind it that daylight savings is a thing
        } else {                                    // Dont think I need a comment here 
            if (currHour == 0) {                    // comparator is = to, not setting anything 
                showHour(23, currColor);            // 23 is the max because we're starting from 0 
            } else {                                // Dont think I need a comment here 
                showHour(currHour - 1, currColor);  // final hour time set 
            }
        }
    }
    if (currMinute != prevMinute || colorChanged) { // if current minute (is not =) to prevMin(-1), (logical or for boolean) or color has changed
        prevMinute = currMinute;                    // prevMin now= currMin 
        showMinute(currMinute, currColor);          // showMin contains the current minute val from RTC and the color 
    }
    if (currSecond != prevSecond || colorChanged) { // if current seconds (is not =) to prevSec(-1), (logical or for boolean) or color has changed 
        prevSecond = currSecond;                    // prevSec now = currSec
        showSecond(currSecond, currColor);          // showSecond contains the current second val from RTC and the color 
     }   
}


void showHour(int currHour, uint32_t color) {       // calling the same showHour, but running through all pre initated values/ variable 
                                                    // Hours: 0 - 23, first LED in the hours strip is at hour 1
    if (currHour > 12) {                            // The RTC reports army time
      currHour -= 12;                               // so off set for -12 will keep us in 1 to 12 values 
    }
    for (int i = 0; i < NUMPIXELS_HOURS; i++) {     // increments i from 0 to 11 
        if (i < currHour) {                         // if i is smaller than the RTC recported hours then update it 
            hoursStrip.setPixelColor(i, color);     // hours strip of led. set i number of LED's to given color 
        } else {
            hoursStrip.setPixelColor(i, colorOff);  // turn everything off when done cycling i 
        }
    }
    hoursStrip.show();                              // sends the updated number of pixels and their color to the hardware. 
}

// 52 53 54 55 56 57 58 59 00 01 02 03 04 05 06 07            
// 51                                           08
// 50                                           09
// 49                                           10
// 48                                           11
// 47                                           12     
// 46                                           13 
// 45                                           14    
// 44                                           15    
// 43                                           16   
// 42                                           17   
// 41                                           18     
// 40                                           19                 
// 39                                           20
// 38                                           21
// 37 36 35 34 33 32 31 30 29 28 27 26 25 24 23 22

void showSecond(int currSecond, uint32_t color) {   // calling the same showSecond, but running through all pre initated values/ variable 
                                                    // Second: 0 - 59, first LED in the second's strip is at second 00
    for (int i = 0; i < NUMPIXELS_SECONDS; i++) {   // increments i from 0 to 59 
        if (i < currSecond) {                       // if i is smaller than the RTC recported seconds then update it 
            secondsStrip.setPixelColor(i, color);    // to be the accurate number of seconds (represented by i) and color also turning on the LED 
        } else {
            secondsStrip.setPixelColor(i, colorOff); // once i is 59 this will turn everything off 
        }
    }
    secondsStrip.show();                            // sends the updated number of pixels and their color to the hardware. 
}

void showMinute(int currMinute, uint32_t color) {    // calling the same showMinute, but running through all pre initated values/ variable 
    showDigit(&digit1Strip, currMinute / 10, color); // because we're making digets we cant run through these linearly 
    showDigit(&digit2Strip, currMinute % 10, color); // divide by 10 to get first diget, % calculates remainder for second didget 
}

// Digit connection scheme:
//   03 04 05 
// 02        06
// 01        07
// 00        08
//   11 10 09
// 12        20
// 13        19
// 14        18
//   15 16 17 
void showDigit(Adafruit_NeoPixel *strip, int digit, uint32_t color) {   // recognize this from NeoPixel library setup 
    for (int i = 0; i < NUMPIXELS_DIGITS; i++) {                        // increments i from 0 to 20 
        if (segments[digit][i / NUMLEDS_PER_SEGMENT]) {                 // calling the segments that we made earlier with true false to make the digits 
            strip->setPixelColor(i, color);                             // to be the accurate number of minutes (represented by i) and color also turning on the LED's
        } else {
            strip->setPixelColor(i, colorOff);                          // after cycling through 59 min this will turn everything off until it resets to 00
        }
    }
    strip->show();                                                      // sends the updated number of pixels and their color to the hardware. 
}

void pciSetup(byte pin) {
    *digitalPinToPCMSK(pin) |= bit (digitalPinToPCMSKbit(pin));  // enable pin
    PCIFR  |= bit (digitalPinToPCICRbit(pin));                   // clear any outstanding interrupt
    PCICR  |= bit (digitalPinToPCICRbit(pin));                   // enable interrupt for the group
}

ISR(PCINT0_vect) {
    if (digitalRead(HOURS_BUTTON_PIN) == HIGH) {
        if (millis() - last_increase_hours > debounce_delay) {   // makes sure you dont get a double hour increase from one button push 
            increase_hours_interrupt();
            last_increase_hours = millis();
        }
    }
    if (digitalRead(MIN_BUTTON_PIN) == HIGH) {
        if (millis() - last_increase_minutes > debounce_delay) { // makes sure you dont get a double minute increase from one button push 
            increase_minutes_interrupt();
            last_increase_minutes = millis();
        }
    }
}

void increase_hours_interrupt() {
    increase_hours_flag = true;         // the actual hour value controlled by the pushbutton 
}

void increase_minutes_interrupt() {
    increase_minutes_flag = true;       // the actual minute value controlled by the pushbutton 
}

