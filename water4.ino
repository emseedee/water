/*
 * Version 4 for Teensy LC, with single channel 12V Bluefish pump switched by relay. 
 * 
 * Automated watering system for greenhouse. 
 * 
 * Designed to run on an allotment with no mains power, and water collected from greenhouse roof to tank. 
 * 
 * Power is from 12V battery, charged by solar panel.
 * 
 * Fires every hour, and operates a single micro-pump for a preset time 
 * On-times of pump is set by pot VR1; time is between 10 secs and 10 minutes.
 * 
 * Once cycle is complete, waits for an hour before running again.
 * 
 * If there's no water in the tank, goes into a mode where the alarm led flashes, until system is reset 
 * by a change in state of the empty switch 
 * 
 * Suppresses watering if it's dark.
 * 
 * Reset switch press caught by interrupt and causes reset via function rst_ISR()
 * This is useful to abort the 1 hour delay between cycles, to check that system is working OK
 * 
 * Reset also triggered by diagnostic display being plugged in or unplugged, or by change of
 * water out (empty) or prime switches.
 * 
 * Has 3 modes of operation: 
 * 
 * a) if LCD module is plugged in, goes into diagnostic mode,
 * which cycles display through all parameters, otherwise goes into watering mode. 
 * 
 * b) If there's no water in the tank, signal led shows fast flashes, until system is reset 
 * by a change in state of the empty switch 
 * 
 * c) if prime switch is on, pump switched on permanently
 * 
 * d) otherwise goes into watering mode. If water runs out, change in state of empty switch causes
 * system reset, system then detects empty state and goes into mode b), above 
 * 
 * Signal LED has 3 modes: 
 * slow short flash (20/2000 mS)for waiting between cycles,
 * slow longer flash (500/1500 mS) for pump running, 
 * fast flash (20/500 mS) for out of water
 */
 
#include <LiquidCrystal.h>
#include <Chrono.h>

/*
 * First let's set up some constants
 */
 const int int_led=13;                // on-board led
 const int signal_led = 21;           // signal led
 const int pump = 7;                  // pump output pin
 const int prime_switch = 20;         // prime switch
 const int empty_switch = 22;         // water empty switch 
 const int reset_switch = 23;         // reset (i.e. cancel 1 hour timeout)
 const int ldr = A0;                  // light / dark indicator pin
 const int pump_res = A1;             // pump on-time control pin
 const int dark_thresh = 512;         // dark / light threshold (fully dark = 1023, fully light = 0)
 const int disp_D4 = 0;               // display D4 pin
 const int disp_D5 = 1;               // display D5 pin
 const int disp_D6 = 2;               // display D6 pin
 const int disp_D7 = 3;               // display D7 pin
 const int disp_RS = 4;               // display RS pin
 const int disp_EN = 5;               // display EN pin
 const int disp_present = 8;          // ground if display plugged in, else floating
 const int min_time = 5000;           // minimum pump run time (mSec)
 const int max_time = 600000;         // maximum pump run time (mSec)
 const int cycle_time = 3600000L;     // delay time until next cycle (3,600,000 mSec = 1 hour)
 const int led_on_pumping = 500;      // led on time while pump on
 const int led_on_idle = 10;          // led on time not pumping
 const int led_off_pumping = 1500;    // led off time while pump on
 const int led_off_idle = 2000;       // led off time not pumping
/*
 * And we'll set up some global variables
 */
 
 int pump_res_val;               // timer pot readings
 int pump_time;                  // pump on-times in mSec (min = 10,000; max = 600,000)
 bool pump_state = true;         // initial state of pump
 bool led_state = true;          // initial state of signal LED
 bool diag_mode;                 // are we in diagnostic mode?
 int next_led_event;             // time for next led event
 long next_pump_event;           // time for next pump event

// initialize the lcd
LiquidCrystal lcd(disp_RS,disp_EN,disp_D4,disp_D5,disp_D6,disp_D7);

// instantiate 2 chrono timers
Chrono led_chrono;
Chrono pump_chrono; 

/*
 * Now declare the function prototypes
 */

bool light();           // true if it's light
void rst_ISR();         // does a system reset when reset switch is pressed


void rst_ISR() 

 /* 
  *  Perform a software reset when the reset button is pressed or empty switch changes state
  */
  {
  SCB_AIRCR = 0x05FA0004;
}

bool light()
{
 bool is_light;
 is_light = analogRead(ldr)<dark_thresh;
 return is_light;
}

void setup() 
{
pinMode(int_led,OUTPUT); 
pinMode(signal_led, OUTPUT);
pinMode(disp_present,INPUT_PULLUP);
pinMode(empty_switch,INPUT_PULLUP);
pinMode(reset_switch,INPUT_PULLUP);
pinMode(prime_switch,INPUT_PULLUP);
pinMode(pump,OUTPUT);
pinMode(disp_D4,OUTPUT);
pinMode(disp_D5,OUTPUT);
pinMode(disp_D6,OUTPUT);
pinMode(disp_D7,OUTPUT);
pinMode(disp_RS,OUTPUT);
pinMode(disp_EN,OUTPUT);
pinMode(ldr,INPUT);
pinMode(pump_res,INPUT);

// flash on-board LED to confirm that we've reset

int count;
for (count=0; count<3; count++)
{
  digitalWrite(int_led, HIGH);
  delay(250);
  digitalWrite(int_led,LOW);
  delay(250);
}

while ( !digitalReadFast(reset_switch) ); // pause until reset switch released

attachInterrupt(digitalPinToInterrupt(reset_switch),rst_ISR, FALLING);  // catch reset switch event
attachInterrupt(digitalPinToInterrupt(prime_switch),rst_ISR, CHANGE);   // catch prime switch event
attachInterrupt(digitalPinToInterrupt(empty_switch),rst_ISR, CHANGE);   // catch change in empty switch 
attachInterrupt(digitalPinToInterrupt(disp_present),rst_ISR, CHANGE);   // catch display being plugged in or removed 

/*
 * start with pump and led off
 */
digitalWrite(pump,LOW); 
digitalWrite(signal_led,HIGH);
/*
 * read timer pot & set initial value for pump on time
 */
pump_res_val = analogRead(pump_res);  
pump_time = min_time +( (max_time - min_time) * pump_res_val / 1023);
next_pump_event = pump_time;
  

  diag_mode = (digitalRead(disp_present)==LOW); // are we in diag mode?
  Serial.begin(9600);                           // initialise output for LCD
}

void loop() {

if (diag_mode) {
/*
 * Read each parameter in turn, and display the parameter name and the value for 2 seconds 
 * system will reset when display unit unplugged
 */

 int counter;
  
 for (counter = 1; counter <=4; counter++){
  Serial.println(counter);
  lcd.clear();
  lcd.begin(16,2);
  switch (counter)
  { case 1: 
      if (digitalRead(empty_switch)==HIGH) 
      {
        lcd.print("We have water");
      }
      else 
      {
        lcd.print("Tank is empty");
      }
      break;
    case 2:
      lcd.print("Light = ");
      lcd.setCursor(8,0);
      lcd.print(int(1023-analogRead(ldr))*100/1023);
      lcd.print("%");
 //     lcd.print(analogRead(ldr));
      lcd.setCursor(0,1);
      if (light()) {
        lcd.print("It's daytime");
      }
      else {
      lcd.print("It's night");
    }
      break;
    case 3:
      pump_res_val = analogRead(pump_res);  
      pump_time = min_time +( (max_time - min_time) * pump_res_val / 1023);
      lcd.print("Pot 1 = ");
      lcd.setCursor(8,0);
      lcd.print(int(pump_res_val*100/1023));
      lcd.print("%");
      lcd.setCursor(0,1);
      lcd.print("Time = ");
      lcd.setCursor(7,1);
      lcd.print(int(pump_time/60000));
      lcd.print(":");
      lcd.print(int(pump_time/1000)%60);
      break;    
    case 4:
      if (!digitalRead(prime_switch)) {
        lcd.print("Prime is on");
      }
      else {
         lcd.print("Prime is off");
      }
     }
  delay(2000);
  
 }
  
}  
else {              // not in diag mode

/*  
 *   If the water tank is empty, flash the light and do nothing else until system is reset
 *   perghaps by water being refilled
 */
  if (digitalRead(empty_switch) == LOW)
  { while (true)           // loop until reset button pressed or water is added
    {digitalWrite(signal_led, LOW);
     delay(20);
     digitalWrite(signal_led,HIGH);
     delay(500);
    } 
  }
else  { 
 /*
 * This is the main timer loop which we only do if we have water
 * 
 * First manage the pump - read current pot setting and calculate pump on times in mSec, 
 * min is 10 sec, max is 10 mins; pot reading in range 0 - 1023
 */

  Serial.write(13);
  Serial.println("Reading pump res");
  pump_res_val = analogRead(pump_res);  
  pump_time = min_time +( (max_time - min_time) * pump_res_val / 1023);
  Serial.println(pump_time);
  Serial.println(pump_state);
/*
 * Now calculate next pump event
 */

 if (pump_state)
   { next_pump_event = pump_time;} // update delay time in-flight
 else
   { next_pump_event = cycle_time;}

  if (pump_chrono.hasPassed(next_pump_event))
    { pump_chrono.restart(); 
      pump_state =! pump_state;
/*      
 *  Now factor in the light state and the prime switch     
 */
     pump_state = !digitalRead(prime_switch) or (pump_state and light()); 
    }
  /*  
   *   Update pump output eith curent state
   */
  digitalWrite(pump, pump_state);
 
 /*
  * That's the pump sorted, not decide what to do with the signal led
  * 
  * First work out when the next change of state is due
  */

  if (pump_state)
    { if (led_state)
      {next_led_event=led_on_pumping;}
      else
      {next_led_event=led_off_pumping;}
    }
      else if(led_state)
        {next_led_event=led_on_idle;}
        else
        {next_led_event=led_off_idle;}

  if(led_chrono.hasPassed(next_led_event))
    {led_chrono.restart();
     led_state = ! led_state;
     digitalWrite(signal_led,!led_state);
     }

/* 
 *  And that's all folks!
 */

}
}

}
