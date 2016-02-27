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
byte rotary_encoder() 
{
    const static byte ttable[6][4] = {
        {0x3 , 0x2, 0x1,  0x0}, 
        {0x83, 0x0, 0x1,  0x0},
        {0x43, 0x2, 0x0,  0x0}, 
        {0x3 , 0x5, 0x4,  0x0},
        {0x3 , 0x3, 0x4, 0x40}, 
        {0x3 , 0x5, 0x0, 0x80}
    };

    static volatile byte rotary_state = 0;

    char pin_state = (digitalRead(ROTARY_PIN2) << 1) | digitalRead(ROTARY_PIN1);
    rotary_state = ttable[rotary_state & 0xf][pin_state];

    return (rotary_state & 0xc0);
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
    S_SET_SHUTTER,
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
	M_DELAY = 0,
	M_FOCUS, 
    M_SHUTTER,
	M_LONG,
	M_INTVL, 
	M_FRAMES,
	M_START,
};

// menu text 
const char g_menu_text[][17] {
	"Delay", 
	"Focus",
    "Shutter",
	"Long",
	"Interval",
    "Frames",
	"Start"
};

// main menu entries
MenuEntry g_main_menu[] = {
	M_DELAY,
	M_FOCUS,
    M_SHUTTER,
	M_LONG,
	M_INTVL,
	M_FRAMES,
	M_START
}; 

//------------------------------------------------------------------------------
// +----------------+
// |TITLE           |
// | 00[00]00'' [OK]|
// +----------------+
// 4 items: hours, minutes, seconds, ok
//------------------------------------------------------------------------------
enum ChangeTimerItem {
    CT_HOURS = 0,
    CT_MINUTES = 1,
    CT_SECONDS = 2,
    CT_OK = 3
};

//------------------------------------------------------------------------------
// +----------------+
// |TITLE           |
// |[000]      [OK] |
// +----------------+
// two items: value, ok 
//------------------------------------------------------------------------------
enum ChangeNumberItem {
    CN_NUMBER = 0,
    CN_OK
};

//------------------------------------------------------------------------------
// MAIN PROGRAM VARIABLES AND RANGES
//------------------------------------------------------------------------------
#define MAX_NUM_FRAMES    9999

#define MIN_FOCUS_DELAY    300
#define MAX_FOCUS_DELAY   1000

#define MIN_SHUTTER_OPEN   300 
#define MAX_SHUTTER_OPEN  1000 

#define WAKE_UP_MIN_INTVL   20000 
#define WAKE_UP_BEFORE_SHOT  4000

// current rotary encoder push-button state
bool          g_rotary_pressed = false;
ProgramState  g_program_state = S_MENU;
byte          g_menu_index = 0;  
bool          g_wake_up = false;

unsigned long g_focus_delay = MIN_FOCUS_DELAY;   // FOCUS
unsigned long g_shutter_open = MIN_SHUTTER_OPEN; // SHUTTER
unsigned long g_long_exposure = 0;               // LONG
unsigned long g_start_delay = 0;                 // DELAY 
unsigned long g_max_frames = 0;                  // FRAMES
unsigned long g_frame_interval = 0;              // INTERVAL

unsigned long g_frames_count = 0;
unsigned long g_prev_millis = 0;

volatile bool g_refresh = true;
char          g_lcd_buffer[20];
byte          g_timer[3];

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
  attachInterrupt(BUTTON_INT, rotary_button, RISING); 
  pinMode(ROTARY_PIN1, INPUT_PULLUP);
  pinMode(ROTARY_PIN2, INPUT_PULLUP);



  // serial init
  Serial.begin(115200);

  // lcd init
  lcd.begin(16, 2);
  refresh_menu();
}

//------------------------------------------------------------------------------
inline bool rotary_is_pressed() 
{
  if (g_rotary_pressed == true) {
    g_rotary_pressed = false; 
    return true;  
  }
  return false;
}

//------------------------------------------------------------------------------
void rotary_button() 
{
	static unsigned long prev_millis = 0; 
	const unsigned long button_delay = 250; 
	unsigned long curr_millis = millis(); 	

	// debounce rotary push-button
	if (curr_millis - prev_millis < button_delay)
		return;

	prev_millis = curr_millis;

	switch (g_program_state) {
    	// change program state to S_STOP 
		case S_FIRST_CAPTURE:
 		case S_RUNNING:
		case S_WAKING_UP_END:
		case S_SHUTTER:
		case S_FOCUS:
		case S_DELAY:
            g_refresh = true; // WHY!?!??!?!
			g_program_state = S_STOP;
			digitalWrite(FOCUS_PIN, LOW);
        	digitalWrite(SHUTTER_PIN, LOW);
			break;
    
		default:
		    g_rotary_pressed = true;
			break;    
	}
}

//------------------------------------------------------------------------------
void refresh_menu() 
{
	MenuEntry entry = g_main_menu[g_menu_index];

	lcd.clear();
	lcd.print(PROGRAM_NAME);
	lcd.setCursor(0, 1);
	lcd.print(g_menu_index + 1);
    lcd.print(". ");
	lcd.print(g_menu_text[entry]);
}

//------------------------------------------------------------------------------
inline unsigned long g_timer_to_millisec() 
{
    unsigned long rval =  ( (unsigned long) g_timer[0]*3600 + 
           (unsigned long) g_timer[1]*60 + 
           (unsigned long) g_timer[2] ) * 1000;


    return rval;
}

//------------------------------------------------------------------------------
inline void g_timer_from_millisec(unsigned long millisec) 
{

    unsigned long remainder = millisec % 1000;

    if (remainder == 0)
        millisec /= 1000;
    else 
        millisec = (millisec + 1000 - remainder) / 1000;

    g_timer[0] = millisec / 3600;
    g_timer[1] = (millisec / 60) % 60; 
    g_timer[2] = millisec % 60;
}

//------------------------------------------------------------------------------
inline void 
refresh_change_timer(ChangeTimerItem curr_item, bool brackets)
{
    if (brackets) {
        switch (curr_item) {
            case CT_HOURS: 
                sprintf(g_lcd_buffer, "[%02d]%02d'%02d''  OK ", 
                        g_timer[0], g_timer[1], g_timer[2]);
                break;
            case CT_MINUTES:
                sprintf(g_lcd_buffer, " %02d[%02d]%02d''  OK ", 
                        g_timer[0], g_timer[1], g_timer[2]);
                break;
            case CT_SECONDS:
                sprintf(g_lcd_buffer, " %02d:%02d[%02d]   OK ", 
                        g_timer[0], g_timer[1], g_timer[2]);
                break;
            case CT_OK:
                sprintf(g_lcd_buffer, " %02d:%02d'%02d'' [OK]", 
                        g_timer[0], g_timer[1], g_timer[2]);
                break;
        }
    }
    else {
        switch (curr_item) {
            case CT_HOURS:
                sprintf(g_lcd_buffer, " %02d %02d'%02d''  OK ", 
                        g_timer[0], g_timer[1], g_timer[2]);
                break;
            case CT_MINUTES:
                sprintf(g_lcd_buffer, " %02d %02d %02d''  OK ", 
                        g_timer[0], g_timer[1], g_timer[2]);
                break;
            case CT_SECONDS:
                sprintf(g_lcd_buffer, " %02d:%02d %02d    OK ", 
                        g_timer[0], g_timer[1], g_timer[2]);
                break;
            case CT_OK:
                sprintf(g_lcd_buffer, " %02d:%02d'%02d''  OK ", 
                        g_timer[0], g_timer[1], g_timer[2]);
                break;
        }
    }

    lcd.setCursor(0, 1);
    lcd.print(g_lcd_buffer);
    Serial.println(g_lcd_buffer);
}


//------------------------------------------------------------------------------
void change_timer(unsigned long *millisec)
{ 
    const unsigned long blink_delay = 500;
    static ChangeTimerItem curr_item = CT_OK;
    //static bool refresh = true;
    static bool set_mode = false;
    static bool brackets = true;

    // update lcd on user input
    if (g_refresh) {
        g_refresh = false;
        refresh_change_timer(curr_item, brackets);
    }
 
    // blink in "set mode"
    if (set_mode) {
        unsigned long curr_millis = millis();
        if (curr_millis - g_prev_millis > blink_delay) {
            g_prev_millis = curr_millis;
            g_refresh = true;
            brackets = !brackets;
        }
    }
    
    if (rotary_is_pressed()) {
        g_refresh = true;
        if (curr_item == CT_OK) {
            *millisec = g_timer_to_millisec();
            set_mode = false;
            g_refresh = true;
            g_program_state = S_MENU;  // return to main menu
            refresh_menu();            // refresh lcd menu
            return;
        } 
        
        set_mode = !set_mode;
        if (!set_mode) brackets = true;
    }

	byte rotary_state = rotary_encoder();

	if (!rotary_state) 
		return; 
   
    g_refresh = true;
 
    // change a timer value
	if (set_mode) {
        int max_value = 0;
        curr_item == CT_HOURS ? max_value = 99 : max_value = 59;
                
		if (rotary_state == ROTARY_LEFT) {   
			if (g_timer[curr_item] > 0)
				g_timer[curr_item]--;
			else 
				g_timer[curr_item] = max_value;
		}
		else {
	        g_timer[curr_item] = (g_timer[curr_item]+1) % (max_value+1);
    	}	
	}
    // change selected item 
	else {         
    	if (rotary_state == ROTARY_LEFT) {
            if (curr_item > 0 )
                curr_item = (ChangeTimerItem)(curr_item - 1);
            else
                curr_item = CT_OK;
		}
		else  {  
      		curr_item = (ChangeTimerItem)((curr_item + 1) % 4);
		}
	}
}

//------------------------------------------------------------------------------
inline void 
refresh_change_number(unsigned long *num, ChangeNumberItem curr_item, bool brackets)
{
    if (brackets) {
        switch (curr_item) {
            case CN_NUMBER:
                sprintf(g_lcd_buffer, "[%04lu]       OK ", *num);
                break;
            case CN_OK:
                sprintf(g_lcd_buffer, " %04lu       [OK]", *num);
                break;
        }
    } else {
        sprintf(g_lcd_buffer, " %04lu        OK ", *num);
    }

    lcd.setCursor(0, 1);
    lcd.print(g_lcd_buffer);
}

//------------------------------------------------------------------------------
void
change_number(unsigned long *num, unsigned long nmin, unsigned long nmax, 
              unsigned long inc) 
{
    const unsigned long blink_delay = 500;
    static ChangeNumberItem curr_item = CN_OK;
    //static bool refresh = true;
    static bool set_mode = false;
    static bool brackets = true;
  
    // update lcd on user input
    if (g_refresh) {
        g_refresh = false;
        refresh_change_number(num, curr_item, brackets);
    }
 
    // blink in "set mode"
    if (set_mode) {
        unsigned long curr_millis = millis();
        if (curr_millis - g_prev_millis > blink_delay) {
            g_prev_millis = curr_millis;
            g_refresh = true;
            brackets = !brackets;
        }
    }

    // return to main menu 
    if (rotary_is_pressed()) {
        g_refresh = true;
        if (curr_item == CN_OK) {
            g_program_state = S_MENU;
            set_mode = false;
            g_refresh = true;
            refresh_menu();
            return;  
        }
        
        set_mode = !set_mode;   
        if (!set_mode) brackets = true;
    }
  
    // process user input
    byte rotary_state = rotary_encoder();

    // no user input
    if (!rotary_state) 
        return;

    g_refresh = true;

    if (!set_mode) 
        curr_item == CN_OK ? curr_item = CN_NUMBER : curr_item = CN_OK;
    else
        if (rotary_state == ROTARY_LEFT) {
            if (*num > nmin) *num -= inc;
            //*num > nmin ? *num -= inc : *num = nmax;
        } else {
            if (*num < nmax) *num += inc;
            //*num < nmax ? *num += inc : *num = nmin;
        }
}

//------------------------------------------------------------------------------
void menu_state() 
{
	// change menu state if rotary button was pressed

	if (rotary_is_pressed()) {
		change_program_state();
		return;	
	}

	byte rotary_state = rotary_encoder();

	if (!rotary_state)
		return;

	if (rotary_state == ROTARY_LEFT) {
    	if (g_menu_index > 0)
			g_menu_index--;
	}
	else { // ROTARY_RIGHT  
 		if (g_menu_index < sizeof(g_main_menu)/sizeof(MenuEntry)-1)
			g_menu_index++;
	}

	refresh_menu();
}

//------------------------------------------------------------------------------
void change_program_state() 
{
	lcd.clear();
	switch(g_main_menu[g_menu_index]) {	
		case M_DELAY:
			lcd.print("Delay (timer)");  
            g_timer_from_millisec(g_start_delay);
			g_program_state = S_SET_DELAY;
			return;
      
		case M_FOCUS:
			lcd.print("Focus (ms)");
			g_program_state = S_SET_FOCUS;
			return;

		case M_SHUTTER:
			lcd.print("Shutter (ms)");
			g_program_state = S_SET_SHUTTER;
			return;
     
		case M_LONG:
			lcd.print("Long (timer)");
            g_timer_from_millisec(g_long_exposure);
			g_program_state = S_SET_LONG;
			return;  
      
		case M_FRAMES:
			lcd.print("Frames (#)");
			g_program_state = S_SET_FRAMES;
			return;
     
		case M_INTVL:
			lcd.print("Interval (timer)");
            g_timer_from_millisec(g_frame_interval);
			g_program_state = S_SET_INTVL;
			return;
    
		case M_START:
			// set interval
			if (g_frame_interval == 0) {
				lcd.print("Interval (timer)");
                g_timer_from_millisec(g_frame_interval);
				g_program_state = S_SET_INTVL;
				return;
			}
          

            // if g_shutter_open are seconds 
            if (g_shutter_open == 0) {
                g_shutter_open = MIN_SHUTTER_OPEN;
            }

			// wait 
			if (g_start_delay > 0) {
				lcd.print("Waiting");
				g_program_state = S_DELAY;				
			}
			// start
			else {
				lcd.print("Waking up");                
				g_program_state = S_START;
				digitalWrite(FOCUS_PIN, HIGH);
			}
			g_prev_millis = millis();        
			return;
	};
}

//------------------------------------------------------------------------------
// update lcd timer with a delay
inline void refresh_delay_timer(unsigned long curr_millis) 
{
    static unsigned long prev_millis = 0;

    if (curr_millis - prev_millis > 500) {
        prev_millis = curr_millis;
        g_timer_from_millisec(g_start_delay - (curr_millis - g_prev_millis));
        sprintf(g_lcd_buffer, "%02d:%02d'%02d''", 
                g_timer[0], g_timer[1], g_timer[2]);
        lcd.setCursor(0, 1);
        lcd.print(g_lcd_buffer);
    }
}

//------------------------------------------------------------------------------
// update lcd timer with a delay
inline void refresh_running_timer(unsigned long curr_millis) 
{
    static unsigned long prev_millis = millis();

    if (curr_millis - prev_millis > 900) {
        prev_millis = curr_millis;
        g_timer_from_millisec(g_frame_interval - (curr_millis - g_prev_millis));

        sprintf(g_lcd_buffer, "%02d:%02d'%02d''  %4lu", 
        g_timer[0], g_timer[1], g_timer[2], g_frames_count);

        lcd.setCursor(0, 1);
        lcd.print(g_lcd_buffer);
    }
}


//------------------------------------------------------------------------------
void loop() 
{   
    unsigned long curr_millis = millis();
    unsigned long shutter_delay = 0;

    switch (g_program_state) 
    {
        case S_START:
            if (curr_millis - g_prev_millis >= 500) {
                g_prev_millis = curr_millis; 
                g_program_state = S_FIRST_CAPTURE;
                digitalWrite(FOCUS_PIN, LOW);        
            }
            return;

        case S_DELAY:
		    if (curr_millis - g_prev_millis >= g_start_delay) {
                g_prev_millis = curr_millis; 
        		g_program_state = S_START;
				digitalWrite(FOCUS_PIN, HIGH);

    	    	lcd.clear();
    		    lcd.print("Waking up");
    	    } 
            refresh_delay_timer(curr_millis); 
	    	return;

        case S_FIRST_CAPTURE: 
            if (curr_millis - g_prev_millis >= 2000) {   
                g_prev_millis = curr_millis;
                g_program_state = S_FOCUS;
                digitalWrite(FOCUS_PIN, HIGH);
       		    g_frames_count = 0;
                lcd.clear();
                sprintf(g_lcd_buffer, "Running     %4lu", g_max_frames);
                lcd.print(g_lcd_buffer);
	        }
            return;

       case S_WAKING_UP_START: 
            refresh_running_timer(curr_millis);
            if (curr_millis - g_prev_millis >= g_frame_interval - WAKE_UP_BEFORE_SHOT + 500) {
                //g_prev_millis = curr_millis;                
                digitalWrite(FOCUS_PIN, LOW);      
                g_program_state = S_WAKING_UP_END;  
                g_wake_up = false;
            }
            return;

        case S_WAKING_UP_END:
            refresh_running_timer(curr_millis);
            if (curr_millis - g_prev_millis >= g_frame_interval - WAKE_UP_BEFORE_SHOT + 2000) {
                //g_prev_millis = curr_millis;                
                g_program_state = S_RUNNING;
                lcd.setCursor(0, 0);
                lcd.print("Running  "); 
            }
            return;

        case S_RUNNING:
            refresh_running_timer(curr_millis);

            if (g_wake_up && curr_millis - g_prev_millis >= g_frame_interval - WAKE_UP_BEFORE_SHOT) {
                digitalWrite(FOCUS_PIN, HIGH);
                lcd.setCursor(0, 0);
                lcd.print("Waking up");
                g_program_state = S_WAKING_UP_START;
                return;                
            }

            if (curr_millis - g_prev_millis >= g_frame_interval) { 
                g_prev_millis = curr_millis;    
                digitalWrite(FOCUS_PIN, HIGH);
                g_program_state = S_FOCUS;
            }
            return;


        case S_FOCUS:
            if (curr_millis - g_prev_millis >= g_focus_delay) {
                //g_prev_millis = curr_millis;
                digitalWrite(SHUTTER_PIN, HIGH);
                g_program_state = S_SHUTTER;
            }
            return;


        case S_SHUTTER:
            refresh_running_timer(curr_millis);

            shutter_delay = g_focus_delay;
            g_long_exposure != 0 ? shutter_delay += g_long_exposure : 
                                   shutter_delay += g_shutter_open;
            
            if (curr_millis - g_prev_millis >= shutter_delay) {   
                //g_prev_millis = curr_millis;                     
                digitalWrite(FOCUS_PIN, LOW);
                digitalWrite(SHUTTER_PIN, LOW);
                g_frames_count++;
		        if (g_max_frames != 0 && g_frames_count >= g_max_frames) 
        			g_program_state = S_STOP;
		        else 
			        g_program_state = S_RUNNING;

                if (g_frame_interval >= WAKE_UP_MIN_INTVL)
                    g_wake_up = true;

            }
            return;

        
        case S_STOP:
            refresh_menu();
            g_program_state = S_MENU; 
        case S_MENU:
            menu_state();
            return;

        case S_SET_FOCUS:
            change_number(&g_focus_delay, MIN_FOCUS_DELAY, MAX_FOCUS_DELAY, 100);
            return;

        case S_SET_SHUTTER:
            change_number(&g_shutter_open, MIN_SHUTTER_OPEN, MAX_SHUTTER_OPEN, 100);
            return;
 
        case S_SET_LONG:
            change_timer(&g_long_exposure);
            return;

        case S_SET_FRAMES:
            change_number(&g_max_frames, 0, MAX_NUM_FRAMES, 1);
            return;

        case S_SET_DELAY:
            change_timer(&g_start_delay);
            return; 
      
        case S_SET_INTVL:
            change_timer(&g_frame_interval);
            return;
      } // switch
}

