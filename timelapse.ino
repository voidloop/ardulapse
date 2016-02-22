// Time Lapse Controller 

#include <LiquidCrystal.h>

#define PROGRAM_NAME "TimeLapse!      "

//------------------------------------------------------------------------------
// HARDWARE SECTION
//------------------------------------------------------------------------------
#define SHUTTER_PIN    A4
#define FOCUS_PIN      A5
#define BACKLIGHT_PIN   7
#define BUTTON_PIN      2
#define BUTTON_INT      0 // interrupt 0 = pin 2
#define ROTARY_PIN1    A3
#define ROTARY_PIN2    A2
#define BACKLIGHT_PIN  15

//------------------------------------------------------------------------------
// ROTARY ENCODER SECTION
//------------------------------------------------------------------------------
#define ROTARY_LEFT  0x40
#define ROTARY_RIGHT 0x80 

/* Read input pins and process for events. Call this either from a
 * loop or an interrupt (eg pin change or timer).
 * More info: http://www.buxtronix.net/2011/10/rotary-encoders-done-properly.html
 *
 * Returns 0 on no event, otherwise 0x80 or 0x40 depending on the direction.
 */
unsigned char rotaryProcess() 
{
  static const unsigned char ttable[6][4] = {
  {0x3 , 0x2, 0x1,  0x0}, 
  {0x83, 0x0, 0x1,  0x0},
  {0x43, 0x2, 0x0,  0x0}, 
  {0x3 , 0x5, 0x4,  0x0},
  {0x3 , 0x3, 0x4, 0x40}, 
  {0x3 , 0x5, 0x0, 0x80}
  };
  
  static volatile unsigned char rotaryState = 0;
  
  char pinState = (digitalRead(ROTARY_PIN2) << 1) | digitalRead(ROTARY_PIN1);
  rotaryState = ttable[rotaryState & 0xf][pinState];

#ifdef TIMELAPSE_DEBUG
  if ((rotaryState & 0xc0) != 0) 
     Serial.println(rotaryState & 0xc0, DEC); 
#endif

  return (rotaryState & 0xc0);
}

//------------------------------------------------------------------------------
// PROGRAM STATE SECTION
//------------------------------------------------------------------------------
#define S_MENU       0
#define S_START      1
#define S_STOP       2
#define S_DELAY      3
#define S_SET_DELAY  4
#define S_SET_FOCUS  5
#define S_SET_LONG   6
#define S_SET_INTVL  7
#define S_SET_FRAMES 8
#define S_WAKING_UP  9

//------------------------------------------------------------------------------
// MENU SECTION
//------------------------------------------------------------------------------
// number of items in the main menu
#define MENU_ITEMS  6

// menu states 
#define M_DELAY  0
#define M_FOCUS  1
#define M_LONG   2
#define M_INTVL  3
#define M_FRAMES 4
#define M_START  5

char mainMenu[MENU_ITEMS][17] = { 
    "1. Delay        ",
    "2. Focus        ",
    "3. Long         ",
    "4. Interval     ",
    "5. Frames       ",
    "6. Start        " 
};

//------------------------------------------------------------------------------
// MAIN PROGRAM VARIABLES AND RANGES
//------------------------------------------------------------------------------
#define MAX_NUM_FRAMES     399

#define MIN_FOCUS_DELAY    300
#define MAX_FOCUS_DELAY   1000

#define MIN_SHUTTER_OPEN  300
#define MAX_SHUTTER_OPEN  5000

// these defines are the lengths of values to print on LCD 
// padded with leading zeros, if you've adjusted the above defines 
// please remember to adjust also the folloing
#define LCD_NUM_FRAMES   3
#define LCD_FOCUS_DELAY  4
#define LCD_SHUTTER_OPEN 4

// current rotary encoder push-button state
bool  rotaryPressed = false;

// current program state
short currentProgramState = S_MENU;

// current menu state 
short currentMenuState = M_DELAY;  

int  focusDelay = MIN_FOCUS_DELAY;   // FOCUS - millis
int  shutterOpen = MIN_SHUTTER_OPEN; // LONG - millis
int  numFrames = 0;     // FRAMES - number
long betweenFrames = 0; // INTERVAL - seconds
long startDelay = 0;    // DELAY - seconds

unsigned long frameMillis = 0;   // see loop funtion - millis
unsigned long wakingUpMillis = 0;


// LCD 
LiquidCrystal lcd(7, 6, 5, 4, 3, 14);

//------------------------------------------------------------------------------
void setup() 
{
  // backlight
  pinMode(BACKLIGHT_PIN, OUTPUT);
  digitalWrite(BACKLIGHT_PIN, HIGH);  
  
  // camera init
  pinMode(FOCUS_PIN, OUTPUT);
  pinMode(SHUTTER_PIN, OUTPUT);
  digitalWrite(SHUTTER_PIN, LOW);
  digitalWrite(FOCUS_PIN, LOW);

  // rotary init
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(BUTTON_INT, rotaryButton, RISING); 
  pinMode(ROTARY_PIN1, INPUT_PULLUP);
  pinMode(ROTARY_PIN2, INPUT_PULLUP);



  // serial init
  Serial.begin(115200);

  // lcd init
  lcd.begin(16, 2);
  updateLcdMenu();
}

//------------------------------------------------------------------------------
inline bool rotaryIsPressed() 
{
  if (rotaryPressed == true) {
    rotaryPressed = false; 
    return true;  
  }
  return false;
}

//------------------------------------------------------------------------------
// ISR for the rotary button:
void rotaryButton() 
{
  static unsigned long previousMillis = 0; 
  const unsigned long buttonDelay = 250; 

  // debounce rotary push-button
  if (millis() - previousMillis < buttonDelay)
    return;

  previousMillis = millis();

  switch (currentProgramState) {
    // change program state to S_STOP if current 
    // state is S_START or S_WAKING_UP
    case S_START:
    case S_WAKING_UP:
      currentProgramState = S_STOP;
      currentMenuState = M_START;
      break;
    
    default:
      rotaryPressed = true;
      break;    
  }

}



//------------------------------------------------------------------------------
void updateLcdMenu() 
{
  lcd.setCursor(0, 0);
  lcd.print(PROGRAM_NAME);
  lcd.setCursor(0, 1);
  lcd.print(mainMenu[currentMenuState]);
}

//------------------------------------------------------------------------------
inline unsigned long timerToSeconds(char timer[3]) 
{
  return (unsigned long)timer[0]*3600 + 
         (unsigned long)timer[1]*60 + 
         (unsigned long)timer[2];
}

//------------------------------------------------------------------------------
inline void timerFromSeconds(char timer[3], unsigned long seconds) 
{
  timer[0] = seconds / 3600;
  timer[1] = (seconds / 60) % 60; 
  timer[2] = seconds % 60;
}

//------------------------------------------------------------------------------
inline void printLcdTimerValues(char timer[3]) 
{
  if (timer[0] < 10) lcd.print(0);
  lcd.print((byte)timer[0]); 
  lcd.print(' ');
  
  if (timer[1] < 10) lcd.print(0);
  lcd.print((byte)timer[1]);
  lcd.print(' ');
  
  if (timer[2] < 10) lcd.print(0);
  lcd.print((byte)timer[2]);
  lcd.print(" ");
}

//------------------------------------------------------------------------------
inline void printLcdTimer(unsigned long seconds) 
{  
  char timer[3]; 
  timerFromSeconds(timer, seconds);
  
  if (timer[0] < 10) lcd.print(0);
  lcd.print((byte)timer[0]); 
  lcd.print(':');
  
  if (timer[1] < 10) lcd.print(0);
  lcd.print((byte)timer[1]);
  lcd.print('\'');
  
  if (timer[2] < 10) lcd.print(0);
  lcd.print((byte)timer[2]);
  lcd.print("''");
}

//------------------------------------------------------------------------------
void printLcdTimerBrackets(byte currentItem, bool blinkBrackets)
{
  const byte itemHours = 0;
  const byte itemMinutes = 1;
  const byte itemSeconds = 2;
  const byte itemOk = 3;
  
  static long currentMillis = 0, previousMillis = 0;
  static bool bracketVisible = false;

  // blink 
  if (blinkBrackets) { 
    currentMillis = millis();
    if (currentMillis - previousMillis > 500) {
      previousMillis = currentMillis;
      bracketVisible = !bracketVisible;
    }
  }
  else 
    bracketVisible = true;
    
  if (bracketVisible) {
    switch (currentItem) {
      case itemHours:
        lcd.setCursor(0, 1); lcd.print('['); 
        lcd.setCursor(3, 1); lcd.print(']');
        lcd.setCursor(6, 1); lcd.print('\'');
        lcd.setCursor(9, 1); lcd.print("''");
        lcd.setCursor(12, 1); lcd.print(' '); 
        lcd.setCursor(15, 1); lcd.print(' '); 
        break; 
      case itemMinutes:
        lcd.setCursor(0, 1); lcd.print(' ');
        lcd.setCursor(3, 1); lcd.print('['); 
        lcd.setCursor(6, 1); lcd.print(']');
        lcd.setCursor(9, 1); lcd.print("''");
        lcd.setCursor(12, 1); lcd.print(' '); 
        lcd.setCursor(15, 1); lcd.print(' '); 
        break; 
      case itemSeconds:
        lcd.setCursor(0, 1); lcd.print(' '); 
        lcd.setCursor(3, 1); lcd.print(':');
        lcd.setCursor(6, 1); lcd.print('['); 
        lcd.setCursor(9, 1); lcd.print("] ");
        lcd.setCursor(12, 1); lcd.print(' '); 
        lcd.setCursor(15, 1); lcd.print(' ');
        break;
      case itemOk:
        lcd.setCursor(0, 1); lcd.print(' '); 
        lcd.setCursor(3, 1); lcd.print(':');
        lcd.setCursor(6, 1); lcd.print('\'');
        lcd.setCursor(9, 1); lcd.print("''");
        lcd.setCursor(12, 1); lcd.print('['); 
        lcd.setCursor(15, 1); lcd.print(']');
        break;
    }
  } 
  else {
        switch (currentItem) {
          case itemHours:
            lcd.setCursor(0, 1); lcd.print(' '); 
            lcd.setCursor(3, 1); lcd.print(' ');
            lcd.setCursor(6, 1); lcd.print('\'');
            lcd.setCursor(9, 1); lcd.print("''");
            lcd.setCursor(12, 1); lcd.print(' '); 
            lcd.setCursor(15, 1); lcd.print(' '); 
            break; 
          case itemMinutes:
            lcd.setCursor(0, 1); lcd.print(' ');
            lcd.setCursor(3, 1); lcd.print(' '); 
            lcd.setCursor(6, 1); lcd.print(' ');
            lcd.setCursor(9, 1); lcd.print("''");
            lcd.setCursor(12, 1); lcd.print(' '); 
            lcd.setCursor(15, 1); lcd.print(' '); 
            break; 
          case itemSeconds:
            lcd.setCursor(0, 1); lcd.print(' '); 
            lcd.setCursor(3, 1); lcd.print(':');
            lcd.setCursor(6, 1); lcd.print(' '); 
            lcd.setCursor(9, 1); lcd.print("  ");
            lcd.setCursor(12, 1); lcd.print(' '); 
            lcd.setCursor(15, 1); lcd.print(' ');
            break;
          case itemOk:
            lcd.setCursor(0, 1); lcd.print(' '); 
            lcd.setCursor(3, 1); lcd.print(':');
            lcd.setCursor(6, 1); lcd.print('\'');
            lcd.setCursor(9, 1); lcd.print("''");
            lcd.setCursor(12, 1); lcd.print(' '); 
            lcd.setCursor(15, 1); lcd.print(' ');
            break; 
        }
      } 

}

//------------------------------------------------------------------------------
void changeLcdTimer(long *seconds) 
{ 
  // +----------------+
  // |TITLE           |
  // | 00[00]00'' [OK]|
  // +----------------+
  // 4 items: hours, minutes, seconds, ok

  const byte itemHours = 0;
  const byte itemMinutes = 1;
  const byte itemSeconds = 2;
  const byte itemOk = 3; 
  
  static byte currentItem = itemOk;
  static char timer[3];
  
  static bool userInput = true;
  static bool setMode = false;
  static bool setTimer = true;

  // TODO: not elegant...
  if (setTimer) {
    setTimer = false;
    timerFromSeconds(timer, *seconds);
  }
  
  // update lcd on user input
  if (userInput) {
    userInput = false;
    lcd.setCursor(1, 1);
    printLcdTimerValues(timer);
    lcd.setCursor(13, 1);
    lcd.print("OK");
    printLcdTimerBrackets(currentItem, setMode);
  }

  if (setMode) 
    printLcdTimerBrackets(currentItem, true);
  
  if (rotaryIsPressed()) {
    if (currentItem == itemOk) {
      *seconds = timerToSeconds(timer);
      setMode = false;               
      userInput = true;
      setTimer = true; 
      currentProgramState = S_MENU;  // return to main menu
      updateLcdMenu();               // update lcd menu
      return;
    } 
    else {
      setMode = !setMode;
      userInput = true;
    }
  }

  unsigned char rotaryState = rotaryProcess();

  if (!rotaryState) 
    return; 

  userInput = true;
  
  if (setMode) {           // change value <-----
    if (rotaryState == ROTARY_LEFT) {   
      timer[currentItem]--;
    }
    else {  
      timer[currentItem]++;
    }

    if      (timer[itemHours]   > 99) timer[itemHours]   =  0;
    else if (timer[itemHours]   <  0) timer[itemHours]   = 99;
    if      (timer[itemMinutes] > 59) timer[itemMinutes] =  0;
    else if (timer[itemMinutes] <  0) timer[itemMinutes] = 59;
    if      (timer[itemSeconds] > 59) timer[itemSeconds] =  0;
    else if (timer[itemSeconds] <  0) timer[itemSeconds] = 59;

  }
  else {                  // change item <-----
    if (rotaryState == ROTARY_LEFT) { 
      currentItem > 0 ? currentItem-- : currentItem = itemOk;
    }
    else  {  
      currentItem = (currentItem + 1) % 4;
    }
  }

}

//------------------------------------------------------------------------------
void printLcdNumberBrackets(byte currentItem, int valueLen, bool blinkBrackets) 
{
  const byte itemOk = 1;

  static unsigned long currentMillis = 0, previousMillis = 0;
  static bool bracketVisible = false;

  if (blinkBrackets) { 
    currentMillis = millis();
    if (currentMillis - previousMillis > 500) {
      previousMillis = currentMillis;
      bracketVisible = !bracketVisible;
    }
  }
  else 
    bracketVisible = true;

  if (bracketVisible) {
    if (currentItem == itemOk) {
      lcd.setCursor(0, 1); lcd.print(' '); 
      lcd.setCursor(valueLen+1, 1); lcd.print(' ');
      lcd.setCursor(12, 1); lcd.print('['); 
      lcd.setCursor(15, 1); lcd.print(']');
    }
    else {
      lcd.setCursor(0, 1); lcd.print('['); 
      lcd.setCursor(valueLen+1, 1); lcd.print(']');
      lcd.setCursor(12, 1); lcd.print(' '); 
      lcd.setCursor(15, 1); lcd.print(' ');
    }
  }
  else {
      lcd.setCursor(0, 1); lcd.print(' '); 
      lcd.setCursor(valueLen+1, 1); lcd.print(' ');
      lcd.setCursor(12, 1); lcd.print(' '); 
      lcd.setCursor(15, 1); lcd.print(' ');    
  }
}

//------------------------------------------------------------------------------
void printLcdNumber(int *valuePtr, short valueLen) {
  // padding with zeros
  int currentMax = 10;
  for (byte i = 1; i < valueLen; ++i) {
    if (*valuePtr < currentMax)
      lcd.print('0');
    currentMax *= 10;
  }
  lcd.print(*valuePtr);
}

//------------------------------------------------------------------------------
void changeLcdNumber(int *valuePtr, int minValue, int maxValue, 
                     int increment, short valueLen) 
{
  // +----------------+
  // |TITLE           |
  // |[000]      [OK] |
  // +----------------+
  // two items: value, ok 
  
  const byte  itemValue = 0;
  const byte  itemOk = 1;    
  static byte currentItem = itemOk;
   
  static bool setMode = false;
  static bool userInput = true; 

  // return to main menu 
  if (rotaryIsPressed()) {
    if (currentItem == itemOk) {
      currentProgramState = S_MENU;
      setMode = false;
      userInput = true;
      updateLcdMenu();
      return;  
    }
    else { // switch "set mode"
      userInput = true; 
      setMode = !setMode;
    }
  }
  
  // process user input
  unsigned char rotaryState = rotaryProcess();

  if (userInput) {
    userInput = false;
    // display items to LCD
    lcd.setCursor(1, 1);
    printLcdNumber(valuePtr, valueLen);
    lcd.setCursor(13, 1);
    lcd.print("OK");
    printLcdNumberBrackets(currentItem, valueLen, false);
  }

  // if setMode blink brackets
  if (setMode) {
    printLcdNumberBrackets(currentItem, valueLen, true); 
  }

  // no user input
  if (!rotaryState) 
    return;

  userInput = true;

  if (!setMode) 
    if (rotaryState == ROTARY_LEFT)
      currentItem == 0 ? currentItem = itemOk : currentItem--;
    else 
      currentItem == itemOk ? currentItem = 0 : currentItem++;
  else
    if (rotaryState == ROTARY_LEFT)
      *valuePtr == minValue ? *valuePtr = maxValue : (*valuePtr) -= increment;
    else 
      *valuePtr == maxValue ? *valuePtr = minValue : (*valuePtr) += increment;
}

//------------------------------------------------------------------------------
void navigateLcdMenu() 
{
  // change menu state if rotary button was pressed
  if (rotaryIsPressed()) {
    changeProgramState();
    return;
  }

  unsigned char rotaryState = rotaryProcess();
  if (rotaryState == ROTARY_LEFT) {
    if (currentMenuState > 0)
      currentMenuState--;
    updateLcdMenu();
  }
  else if (rotaryState == ROTARY_RIGHT) {  
    if (currentMenuState < MENU_ITEMS - 1)
      currentMenuState++;
    updateLcdMenu();
  }
}

//------------------------------------------------------------------------------
void changeProgramState() 
{
  lcd.clear();
  switch(currentMenuState) {
    case M_DELAY:
      lcd.print("Delay");  
      currentProgramState = S_SET_DELAY;
      return;
      
    case M_FOCUS:
      lcd.print("Focus");
      currentProgramState = S_SET_FOCUS;
      return;
     
    case M_LONG:
      lcd.print("Long");
      currentProgramState = S_SET_LONG;
      return;  
      
    case M_FRAMES:
      lcd.print("Frames");
      currentProgramState = S_SET_FRAMES;
      return;
     
    case M_INTVL:
      lcd.print("Interval"); 
      currentProgramState = S_SET_INTVL;
      return;
    
    case M_START:
      if (betweenFrames == 0) {
        lcd.print("Interval"); 
        currentProgramState = S_SET_INTVL;
        return;
      }
      lcd.print("Waking up...");
      currentProgramState = S_WAKING_UP;
      wakeUp();
      wakingUpMillis = millis();  // reset wake up timer
      return;
  };
}


//------------------------------------------------------------------------------
void snapPhoto() 
{
    if (betweenFrames >= 30) {
      wakeUp();  
    }
   
    digitalWrite(FOCUS_PIN, HIGH);
    delay(focusDelay);
    digitalWrite(SHUTTER_PIN, HIGH);
    delay(shutterOpen);
    digitalWrite(SHUTTER_PIN, LOW);
    digitalWrite(FOCUS_PIN, LOW);
}

//------------------------------------------------------------------------------
void wakeUp() 
{
  digitalWrite(FOCUS_PIN, HIGH);
  delay(500);
  digitalWrite(FOCUS_PIN, LOW);  
}

//------------------------------------------------------------------------------
void wakingUp() 
{
  const unsigned long wakingUpDelay = 2000;
  if (millis() - wakingUpMillis >= wakingUpDelay) {
    currentProgramState = S_START;
    frameMillis = millis(); 
    lcd.clear();
    lcd.print("Taking photos");
    snapPhoto();
  }
}

//------------------------------------------------------------------------------
void loop() 
{ 
  switch (currentProgramState) 
  {
    case S_WAKING_UP: 
      wakingUp();
      return; 
      
    case S_START:
      if (millis() - frameMillis >= betweenFrames*1000) {
        frameMillis = millis();
        snapPhoto();
      }
      
      lcd.setCursor(0, 1);
      printLcdTimer(betweenFrames-(millis()-frameMillis)/1000);
      return;

    case S_STOP: // TODO: continue menu
      updateLcdMenu(); 
      currentProgramState = S_MENU; 
    case S_MENU:
      navigateLcdMenu();
      return;

    case S_SET_FOCUS:
      changeLcdNumber(&focusDelay, MIN_FOCUS_DELAY, MAX_FOCUS_DELAY, 10, LCD_FOCUS_DELAY);
      return;
    
   case S_SET_LONG:
      changeLcdNumber(&shutterOpen, MIN_SHUTTER_OPEN, MAX_SHUTTER_OPEN, 10, LCD_SHUTTER_OPEN);
      return;

    case S_SET_FRAMES:
      changeLcdNumber(&numFrames, 0, MAX_NUM_FRAMES, 1, LCD_NUM_FRAMES);
      return;

    case S_SET_DELAY:
      changeLcdTimer(&startDelay);
      return; 
      
    case S_SET_INTVL:
      changeLcdTimer(&betweenFrames);
      return;
  };
}

