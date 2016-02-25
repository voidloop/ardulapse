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
byte rotaryProcess() 
{
  const static byte ttable[6][4] = {
  {0x3 , 0x2, 0x1,  0x0}, 
  {0x83, 0x0, 0x1,  0x0},
  {0x43, 0x2, 0x0,  0x0}, 
  {0x3 , 0x5, 0x4,  0x0},
  {0x3 , 0x3, 0x4, 0x40}, 
  {0x3 , 0x5, 0x0, 0x80}
  };
  
  static volatile byte rotaryState = 0;
  
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
enum ProgramState {
	S_MENU,
	S_START,
	S_STOP,
	S_DELAY,
	S_SET_DELAY,
	S_SET_FOCUS,
	S_SET_LONG,
	S_SET_INTVL,
	S_SET_FRAMES,
	S_WAKING_UP_START,
	S_WAKING_UP_END,
	S_FOCUS,
	S_SHUTTER, 
	S_RELEASE,
	S_RUNNING,
	S_FIRST_CAPTURE
};

//------------------------------------------------------------------------------
// MENU SECTION
//------------------------------------------------------------------------------
enum MenuEntry {
	M_DELAY  = 0,
	M_FOCUS  = 1,
	M_LONG   = 2,
	M_INTVL  = 3,
	M_FRAMES = 4,
	M_START  = 5
};

// menu text 
const char menuText[][17] {
	"Delay", 
	"Focus",
	"Long",
	"Interval",
    "Frames",
	"Start"
};

// main menu entries
MenuEntry mainMenu[] = {
	M_DELAY,
	M_FOCUS,
	M_LONG,
	M_INTVL,
	M_FRAMES,
	M_START
}; 
	
//------------------------------------------------------------------------------
// MAIN PROGRAM VARIABLES AND RANGES
//------------------------------------------------------------------------------
#define MAX_NUM_FRAMES    9999

#define MIN_FOCUS_DELAY    300
#define MAX_FOCUS_DELAY   1000

#define MIN_SHUTTER_OPEN   300
#define MAX_SHUTTER_OPEN 60000

// these defines are the lengths of values to print on LCD 
// padded with leading zeros, if you've adjusted the above defines 
// please remember to adjust also the folloing
#define LCD_NUM_FRAMES   3
#define LCD_FOCUS_DELAY  4
#define LCD_SHUTTER_OPEN 4

#define LCD_COLS 16
#define LCD_ROWS  2

// current rotary encoder push-button state
bool  rotaryPressed = false;

// current program state
ProgramState currentProgramState = S_MENU;

// current menu index
byte currentMenuIndex = 0;  

unsigned long focusDelay = MIN_FOCUS_DELAY;   // FOCUS
unsigned long shutterOpen = MIN_SHUTTER_OPEN; // LONG 

unsigned long frameMax = 0;
unsigned long frameCount = 0;
unsigned long frameInterval = 0;
 
unsigned long startDelay = 0;    // DELAY 

// timing
unsigned long previousMillis = 0;

char lcdBuf[17];


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
	unsigned long currentMillis = millis(); 	

	// debounce rotary push-button
	if (currentMillis - previousMillis < buttonDelay)
		return;

	previousMillis = currentMillis;

	switch (currentProgramState) {
    	// change program state to S_STOP 
		case S_FIRST_CAPTURE:
 		case S_RUNNING:
		case S_WAKING_UP_END:
		case S_SHUTTER:
		case S_FOCUS:
		case S_DELAY:
			currentProgramState = S_STOP;
			digitalWrite(FOCUS_PIN, LOW);
        	digitalWrite(SHUTTER_PIN, LOW);
			break;
    
		default:
			rotaryPressed = true;
			break;    
	}
}

//------------------------------------------------------------------------------
void updateLcdMenu() 
{
	MenuEntry menuEntry = mainMenu[currentMenuIndex];

	lcd.clear();
	lcd.print(PROGRAM_NAME);
	lcd.setCursor(0, 1);
	lcd.print(currentMenuIndex + 1);
    lcd.print(". ");
	lcd.print(menuText[menuEntry]);
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
void changeLcdTimer(unsigned long *seconds) 
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

	byte rotaryState = rotaryProcess();

	if (!rotaryState) 
		return; 

	userInput = true;
  
	if (setMode) {           // change value <-----
		if (rotaryState == ROTARY_LEFT) {   
			if (timer[currentItem] > 0)
				timer[currentItem]--;
			else if (currentItem==itemHours)
				timer[currentItem] = 99;
			else 
				timer[currentItem] = 59;
		}
		else {
			if (currentItem == itemHours)
	    		timer[currentItem] = (timer[currentItem] + 1) % 100;
			else 
				timer[currentItem] = (timer[currentItem] + 1) % 60;
    	}	
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
void printLcdNumber(unsigned long *valuePtr, byte valueLen) {
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
void settingNumber(unsigned long *valueP, 
				   unsigned long minV,  unsigned long maxV, 
				   unsigned long inc, byte lcdLen) 
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
  static bool refreshLcd = true; 

  // return to main menu 
  if (rotaryIsPressed()) {
    if (currentItem == itemOk) {
      currentProgramState = S_MENU;
      setMode = false;
      refreshLcd = true;
      updateLcdMenu();
      return;  
    }
    else { // switch "set mode"
      refreshLcd = true; 
      setMode = !setMode;
    }
  }
  
  if (refreshLcd) {
    refreshLcd = false;
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

  // process user input
  byte rotaryState = rotaryProcess();

  // no user input
  if (!rotaryState) 
    return;

  refreshLcd = true;

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
void menuState() 
{
	// change menu state if rotary button was pressed

	if (rotaryIsPressed()) {
		changeProgramState();
		return;	
	}

	byte rotaryState = rotaryProcess();

	if (!rotaryState)
		return;

	if (rotaryState == ROTARY_LEFT) {
    	if (currentMenuIndex > 0)
			currentMenuIndex--;
	}
	else { // ROTARY_RIGHT  
 		if (currentMenuIndex < sizeof(mainMenu)/sizeof(MenuEntry)-1)
			currentMenuIndex++;
	}

	updateLcdMenu();
}

//------------------------------------------------------------------------------
void changeProgramState() 
{
	lcd.clear();
	switch(mainMenu[currentMenuIndex]) {	
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
			// set interval
			if (frameInterval == 0) {
				lcd.print("Interval"); 
				currentProgramState = S_SET_INTVL;
				return;
			}
			// wait 
			if (startDelay > 0) {
				lcd.print("Waiting...");
				currentProgramState = S_DELAY;				
			}
			// start
			else {
				lcd.print("Waking up...");
				currentProgramState = S_START;
				digitalWrite(FOCUS_PIN, HIGH);
			}
			previousMillis = millis();        
			return;
	};
}

//------------------------------------------------------------------------------
// update lcd timer with a delay
inline void updateLcdTimer(unsigned long timer, unsigned long curr, unsigned long prev) 
{
  static unsigned long previousMillis = 0;
  
  if (millis() - previousMillis >= 200) {
    lcd.setCursor(0, 1);
    printLcdTimer(timer-(curr-prev)/1000);
    previousMillis = millis();
  }
}


//------------------------------------------------------------------------------
void loop() 
{  

	char buf[20];
 
  unsigned long currentMillis = 0;
  
  switch (currentProgramState) 
  {
    case S_START:
      currentMillis = millis();
      if (currentMillis - previousMillis >= 500) {   
        currentProgramState = S_FIRST_CAPTURE;
        digitalWrite(FOCUS_PIN, LOW);        
        previousMillis = millis();
      }
      return;

	case S_DELAY:
		currentMillis = millis();	
		if (currentMillis - previousMillis >= startDelay*1000) {
			currentProgramState = S_START;
			lcd.clear();
			lcd.print("Waking up...");
			previousMillis = millis();
		} 
		updateLcdTimer(startDelay, currentMillis, previousMillis); 
		return;

    case S_FIRST_CAPTURE: 
      currentMillis = millis();
      if (currentMillis - previousMillis >= 3000) {   
        currentProgramState = S_FOCUS;
        digitalWrite(FOCUS_PIN, HIGH);
        lcd.setCursor(0, 0);
        lcd.print("Frame:          ");
		frameCount = 0;
		previousMillis = millis();		
	  }
      return;

    case S_WAKING_UP_START: 
      currentMillis = millis();      
      if (currentMillis - previousMillis >= 500) {
        digitalWrite(FOCUS_PIN, LOW);      
        currentProgramState = S_WAKING_UP_END;        
        lcd.setCursor(0, 0);
        lcd.print("Waking up...    ");
        previousMillis = millis();
      }
      updateLcdTimer(frameInterval, currentMillis, previousMillis);
      return;

    case S_WAKING_UP_END: 
      currentMillis = millis();
      if (currentMillis - previousMillis >= 1500) {
        currentProgramState = S_RUNNING;        
        lcd.setCursor(0, 0);
        lcd.print("Frame:          ");
        previousMillis = millis();
      }
      updateLcdTimer(frameInterval, currentMillis, previousMillis);
      return;
   
    case S_RUNNING:
      currentMillis = millis();
      if (currentMillis - previousMillis >= frameInterval*1000) {       
        digitalWrite(FOCUS_PIN, HIGH);
        currentProgramState = S_FOCUS;
        previousMillis = millis();
        return;
      }currentProgramState = S_RUNNING;
      updateLcdTimer(frameInterval, currentMillis, previousMillis);
      return;


    case S_FOCUS:
      currentMillis = millis();
      if (currentMillis - previousMillis >= focusDelay) {
        digitalWrite(SHUTTER_PIN, HIGH);
        currentProgramState = S_SHUTTER;
		lcd.setCursor(0, 0);
		sprintf(buf, "Frames:  %03lu/%03lu", frameMax, frameMax);
        //previousMillis = millis(); // ??????????
      }
      return;


    case S_SHUTTER:
      currentMillis = millis();
      if (currentMillis - previousMillis >= (shutterOpen+focusDelay)) {        
        digitalWrite(FOCUS_PIN, LOW);
        digitalWrite(SHUTTER_PIN, LOW);
		frameCount++;
		if (frameMax != 0 && frameCount >= frameMax) 
			currentProgramState = S_STOP;
		else 
			currentProgramState = S_RUNNING;
		lcd.print(buf);
        previousMillis = millis(); // ??????????
      }
      return;

    case S_STOP: // TODO: continue menu
      updateLcdMenu(); 
      currentProgramState = S_MENU; 
    case S_MENU:
      menuState();
      return;

    case S_SET_FOCUS:
      changeLcdNumber(&focusDelay, MIN_FOCUS_DELAY, MAX_FOCUS_DELAY, 10, LCD_FOCUS_DELAY);
      return;
    
   case S_SET_LONG:
      changeLcdNumber(&shutterOpen, MIN_SHUTTER_OPEN, MAX_SHUTTER_OPEN, 10, LCD_SHUTTER_OPEN);
      return;

    case S_SET_FRAMES:
      changeLcdNumber(&frameMax, 0, MAX_NUM_FRAMES, 1, LCD_NUM_FRAMES);
      return;

    case S_SET_DELAY:
      changeLcdTimer(&startDelay);
      return; 
      
    case S_SET_INTVL:
      changeLcdTimer(&frameInterval);
      return;
  };
}

