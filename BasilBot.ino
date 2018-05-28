///////////////////////////
// Written by Justin Lee //
///////////////////////////
//
//  "THE BEER-WARE LICENSE" (Revision 42) [phk]:
//
//  Justin Lee  <justin at taiz dot me>  wrote this file. As long as you retain
//  this notice you can do whatever you want with this stuff. If we meet some
//  day, and you think this stuff is worth it, you can buy me a beer in return!
//
//   > BasilBot.ino
//   > 2018-05-28
//   > Revision 5
//
//   Justin Lee  <justin at taiz dot me>
//


#define ON HIGH
#define OFF LOW
#define BLOCK true
#define NONBLOCK !BLOCK

#include <Wire.h>
#include <TimeLib.h>
#include <DS1307RTC.h>


///
// static stuff

#define serialSpeed 19200
#define serialReadDelay 1
//#define serialReadDelayEnable 1
//#define serialWaitForConsole 1
#define serialBlockDelay 50
#define serialSetupDelay 100

#define clockCount 489 // 490Hz for counting
#define clockInt 0 // interrupt number
#define clockIntPin 2 // interrupt pin
#define clockFreq 127 // 50% duty cycle PWM
#define clockOutPin 9 // attach this to clockIntPin
#define clockCheckFreq 5 // check the time ~5 seconds
#define heartBeatPin 13 // thing that blinks to let you know it lives

#define lampPin 12
#define pumpPin 7
#define waterLevelPin 3 //  aluminum or capacitive probe, or w/e
#define pumpEnablePin 4 // set as input pullup

#define lampOnTimeDef 230 // time in minutes
#define lampOffTimeDef 10
#define pumpOverrideDef false
#define lampOverrideDef false

#define dayTimeDef 350 // 5:50 AM
#define nightTimeDef 1430 // 11:50 PM


///
// quick macro

#define switchInByte readSerial(BLOCK);switch(inByte)


///
// strings in flash memory

#define _string_Intro   F("Connected to GrowBot v2\n=======================\n")
#define _string_LampRdy F("Lamp setup complete.")
#define _string_PumpRdy F("Pump setup complete.")
#define _string_SysRdy  F("Setup complete.\n")
#define _string_SysArm  F(" > System Armed")
#define _string_CmdRdy  F(" > Waiting for Command...")
#define _string_CmdStp  F(" > Done")
#define _string_BadCmd  F(" > Bad Command")
#define _string_CycDay  F(" > Cycle Day Start ")
#define _string_CycNght F(" > Cycle Night Start ")
#define _string_DayChng F(" > Date Changed To ")
#define _string_CurDate F(" > Date ")
#define _string_TimeGet F(" > Time ")
#define _string_TimeSet F(" > Time SET ")
#define _string_LampON  F(" > Lamp ON")
#define _string_LampOFF F(" > Lamp OFF")
#define _string_PumpON  F(" > Pump ON")
#define _string_PumpOFF F(" > Pump OFF")
#define _string_PumpOvE F(" > Pump Override Enabled")
#define _string_PumpOvD F(" > Pump Override Disabled")
#define _string_LampOvE F(" > Lamp Override Enabled")
#define _string_LampOvD F(" > Lamp Override Disabled")
#define _string_LampStI F(" > Lamp ON Time SET ")
#define _string_LampStO F(" > Lamp OFF Time SET ")
#define _string_LampStR F(" > Lamp Cycle Time RESET")
#define _string_LampStT F(" > Lamp Cycle Time ")


///
// global variables

volatile unsigned short tick;
volatile byte tock;
volatile boolean heartBeatState;
volatile boolean updateClockState;

unsigned short clockState; // time of day in minutes only
unsigned short lampOnTime, lampOffTime, lampStateTime; // for the lamp cycle timer
unsigned short dayTime, nightTime;

boolean pumpOverride; // ignore the switch or not
boolean lampOverride; // ignore the cycle timer


///
// swap space

byte inByte;
byte pumpDelayByte; // bullshit programming


///
// helper stuff

void clockCounter()
{
  // Called by interrupt.  This routine counts seconds (roughly)
  // to keep track of when to get the new time from the RTC.  It
  // also blips the heart beart LED on pin 13.

  if (++tick == clockCount)
  {
    tick = 0;

    // check roughly every updateTime seconds
    if (!(tock % clockCheckFreq))
    {
      updateClockState = true; // time to update clock
    }

    heartBeatState = !heartBeatState; // blink ~1Hz default
  }

  return;
}


tmElements_t getThyme()
{
  // Wrapper for routine directly below. Gets the time,
  // then prints it, all in one!

  tmElements_t thyme;
  RTC.read(thyme); // get time

  return (thyme);
}

void printTime()
{
  printTime(getThyme()); // send to next routine
}

void printTime(tmElements_t t)
{
  // Quick routine to print time over serial.

  if (t.Hour < 10) Serial.print('0');
  Serial.print(t.Hour);
  Serial.print(':');
  if (t.Minute < 10) Serial.print('0');
  Serial.print(t.Minute);
}

void printDate ()
{
  printDate(getThyme());
}

void printDate (tmElements_t t)
{
  Serial.print(tmYearToCalendar(t.Year));
  Serial.print('/');
  Serial.print(t.Month);
  Serial.print('/');
  Serial.print(t.Day);
}


byte readSerial(boolean block)
{
  // Handles reading data from the Serial connection with
  // optional blocking loop.

  inByte = 0; // reset the inByte so we don't choke on old data

  Serial.flush(); // clear serial buffer

  while (block && !Serial.available())
  {
    // flashes LED when stuck blocking loop
    digitalWrite(heartBeatPin, (heartBeatState = !heartBeatState));
    delay(serialBlockDelay);
  }

  if (Serial.available() > 0) // we have data
  {
    inByte = Serial.read(); // get the data

#ifdef serialReadDelayEnable
    delay(serialReadDelay); // breathing room (if needed)
#endif
  }

  return (inByte); // return the data, too for convenience
}


///
// loop routines

void updateClock()
{
  // Every time updateClockState is true this routine
  // will get the current time from the RTC and convert
  // it to the currently used time-of-day-in-minutes
  // format.

  digitalWrite(heartBeatPin, heartBeatState); // toggle LED ~1 per second

  if (updateClockState)
  {
    updateClockState = false; // reset the update flag

    tmElements_t thyme;
    RTC.read(thyme); // get the thyme

    clockState = thyme.Minute + (60 * thyme.Hour); // set local clock to time of day in minutes

    if (!clockState)
    {
      // at midnight print that the day has changed and output the date
      printTime(thyme);
      Serial.print(_string_DayChng);
      printDate(thyme);
      Serial.println();
    }
  }
}


void updateLamp()
{
  // Cycle timer.  For now this is a on/off cycle timer, it keeps
  // the lamp on for lampOnTime then off for lampOffTime.  In the
  // future it would be nice to a real alarm system.

  if (!lampOverride) // if the override is not enabled
  {
    if (clockState > dayTime && clockState < nightTime)
    {
      // As long as we are between dayTime and nightTime the
      // lamp will be enabled with the cycle timer running.
      // The cycle timer is used to keep the heat down and
      // prolong the life of the lamp.

      if (clockState >= lampStateTime) // and the clock time is >= our change time
      {
        lampStateTime = clockState;

        if (digitalRead(lampPin) == ON) // do the thing
        {
          lampStateTime += lampOffTime;
          digitalWrite(lampPin, OFF);

          printTime();
          Serial.println(_string_LampOFF);
        }
        else
        {
          lampStateTime += lampOnTime;
          digitalWrite(lampPin, ON);

          printTime();
          Serial.println(_string_LampON);
        }
      }
    }

    if (clockState == dayTime || clockState == nightTime)
    {
      // In either case we want to turn the lamp off and reset the
      // state.  For day time, we're not sure if the lamp has been
      // forced on or off during this time as the cycle timer does
      // not run at night.

      lampStateTime = 0;
      digitalWrite(lampPin, OFF);
    }
  }
}


void updatePump()
{
  // This routine updates the pump state based on the pump
  // switch every time pumpDelayByte == 0.  This is just a
  // temporary "debounce" setup.

  if (!pumpOverride) // if the override is not enabled
  {
    if (!pumpDelayByte--) // simple non-blocking delay
    {
      boolean enable = digitalRead(pumpEnablePin); // check current state

      if (digitalRead(pumpPin) != enable) // if different change output
      {
        digitalWrite(pumpPin, enable);

        printTime();
        Serial.println(enable ? _string_PumpON : _string_PumpOFF);
      }
    }
  }
}


void updateSerial()
{
  // This routine handles the serial input with a decision tree
  // that uses characters read from serial (mostly one at a time
  // for speed).  To start a parse loop send an 's' character, all
  // characters preceeding the first 's' are ignored.  The command
  // list is as follows:
  //
  //    scduN1.N2 => set the time to be considered Day Time with N1:N2
  //    scdg      => get the current Day Time
  //    scnuN1.N2 => set the time to be considered Night Time with N1:N2
  //    scng      => get the current Night Time
  //    sdg       => get the current date
  //    stuN1.N2  => set RTC time to N1:N2 (does not affect calendar)
  //    stg       => print the current RTC time to the Serial Console
  //    sli       => turn the lamp on (stays until next lamp state)
  //    slo       => turn the lamp off (like previous command)
  //    slbi      => turn on the lamp override (no state changes anymore)
  //    slbo      => turn off the lamp override (state changes resume immediately)
  //    sltiN1    => set the lamp ON time to N1 (in minutes)
  //    sltoN1    => set the lamp OFF time to N1 (in minutes)
  //    slttN1    => set the lamp STATE time to N1 (time remaining before state change)
  //    sltr      => reset the lamp STATE time (cause immediate state change)
  //    spi       => turn the pump on (until the pump switch is read again)
  //    spo       => turn the pump off (like previous command)
  //    spbi      => turn on the pump override (pump switch no longer read)
  //    spbo      => turn off the pump override (pump switch read again)

  if (readSerial(NONBLOCK) == 's') // start parse loop when we find an 's'
  {
    printTime();
    Serial.println(_string_CmdRdy);

    switchInByte // get a serial byte and parse it
    {
      // day/night time
    case 'c':
      switchInByte
      {
        // day
      case 'd':
        switchInByte
        {
          // update
        case 'u':
          unsigned short timeDayMin;
          timeDayMin = Serial.parseInt() * 60; // hours * min / hour
          readSerial(BLOCK); // get the spacer character (can be anything)
          timeDayMin += Serial.parseInt(); // add minutes

          if (timeDayMin < nightTime)
          {
            dayTime = timeDayMin;
          }
          else
          {
            printTime();
            Serial.println(_string_BadCmd);
          }
          break;

          // print
        case 'g':
          tmElements_t thyme;

          thyme.Hour = dayTime / 60; // get hours
          thyme.Minute = dayTime % 60; // get min remaining

          printTime();
          Serial.print(_string_CycDay);
          printTime(thyme);
          Serial.println();
          break;

        default:
          printTime();
          Serial.println(_string_BadCmd);
          break;
        }
        break;

        // night
      case 'n':
        switchInByte
        {
          // update
        case 'u':
          unsigned short timeNightMin;
          timeNightMin = Serial.parseInt() * 60; // hours * min / hour
          readSerial(BLOCK); // get the spacer character (can be anything)
          timeNightMin += Serial.parseInt(); // add minutes

          if (timeNightMin > dayTime)
          {
            nightTime = timeNightMin;
          }
          else
          {
            printTime();
            Serial.println(_string_BadCmd);
          }
          break;

          // print
        case 'g':
          tmElements_t thyme;

          thyme.Hour = nightTime / 60; // get hours
          thyme.Minute = nightTime % 60; // get min remaining

          printTime();
          Serial.print(_string_CycNght);
          printTime(thyme);
          Serial.println();
          break;

        default:
          printTime();
          Serial.println(_string_BadCmd);
          break;
        }
        break;

      default:
        printTime();
        Serial.println(_string_BadCmd);
      }
      break;
      //
      // end of day / night cycle

    case 'd':
      switchInByte
      {
      case 'g':
        printTime();
        Serial.print(_string_CurDate);
        printDate();
        Serial.println();
        break;

      default:
        printTime();
        Serial.println(_string_BadCmd);
        break;
      }
      break;

      // time
    case 't':
      tmElements_t thyme; // used below

      switchInByte
      {
        // command: stuN1.N2 -> set time N1:N2 (does not change calendar)
      case 'u':
        int h, m;
        RTC.read(thyme); // get the thyme
        h = Serial.parseInt();
        readSerial(BLOCK); // get the spacer character (can be anything)
        m = Serial.parseInt();
        thyme.Hour = h;
        thyme.Minute = m;
        thyme.Second = 0; // reset second counter
        RTC.write(thyme); // update the clock with new hours/minutes/seconds
        printTime(thyme);
        Serial.print(_string_TimeSet);
        printTime(thyme); // SO MUCH TIME
        Serial.println();
        break;

        // command: stg -> prints the current time to serial console
      case 'g':
        RTC.read(thyme); // get the thyme
        printTime(thyme);
        Serial.print(_string_TimeGet);
        printTime(thyme);
        Serial.println();
        break;

      default:
        printTime();
        Serial.println(_string_BadCmd);
        break;
      }
      break;
      //
      /// end time

      // lamp
    case 'l':
      switchInByte
      {
        // command: sli -> turn lamp on (sticks until next state)
      case 'i': // force lamp on
        digitalWrite(lampPin, ON);
        printTime();
        Serial.println(_string_LampON);
        break;

        // command: slo -> turn lamp off (like above)
      case 'o': // force lamp off
        digitalWrite(lampPin, OFF);
        printTime();
        Serial.println(_string_LampOFF);
        break;

      case 'b':
        switchInByte
        {
          // command: slbi -> lamp will ignore cycle timer
        case 'i':
          lampOverride = true;
          lampStateTime = 0; // reset state so next time it runs updateLamp() automatically
          printTime();
          Serial.println(_string_LampOvE);
          break;

          // command: slbo -> lamp obeys cycle timer for state control
        case 'o':
          lampOverride = false;
          printTime();
          Serial.println(_string_LampOvD);
          break;

        default:
          printTime();
          Serial.println(_string_BadCmd);
          break;
        }
        break;

      case 't':
        switchInByte
        {
          // command: sltiN1 -> lampOnTime = N1
        case 'i': // set length of time lamp stays on in minutes
          lampOnTime = Serial.parseInt();
          printTime();
          Serial.print(_string_LampStI);
          Serial.println(lampOnTime);
          break;

          // command: sltoN1 -> lampOffTime = N1
        case 'o': // set length of time lamp stays off in minutes
          lampOffTime = Serial.parseInt();
          printTime();
          Serial.print(_string_LampStO);
          Serial.println(lampOffTime);
          break;

          // command: slttN1 -> lampStateTime = N1
        case 't': // set the current state counter
          lampStateTime = Serial.parseInt();
          printTime();
          Serial.print(_string_LampStT);
          Serial.println(lampStateTime);
          break;

          // command: sltr -> lampStateTime = 0
        case 'r': // reset the state counter to zero
          lampStateTime = 0;
          printTime();
          Serial.println(_string_LampStR);
          break;

        default:
          printTime();
          Serial.println(_string_BadCmd);
          break;
        }
        break;

      default:
        printTime();
        Serial.println(_string_BadCmd);
        break;
      }
      break;
      //
      /// end lamp

      // pump
    case 'p':
      switchInByte
      {
        // command: spi -> turn pump on (sticks until pump switch is read again)
      case 'i':
        digitalWrite(pumpPin, ON);
        printTime();
        Serial.println(_string_PumpON);
        break;

        // command: spo -> turn pump off (like above)
      case 'o':
        digitalWrite(pumpPin, OFF);
        printTime();
        Serial.println(_string_PumpOFF);
        break;

      case 'b':
        switchInByte
        {
          // command: spbi -> pump ignores control switch
        case 'i':
          pumpOverride = true;
          printTime();
          Serial.println(_string_PumpOvE);
          break;

          // command: spbo -> pump obeys control switch
        case 'o':
          pumpOverride = false;
          printTime();
          Serial.println(_string_PumpOvD);
          break;

        default:
          printTime();
          Serial.println(_string_BadCmd);
          break;
        }
        break;

      default:
        printTime();
        Serial.println(_string_BadCmd);
        break;
      }
      break;
      //
      /// end pump

    default:
      printTime();
      Serial.println(_string_BadCmd);
      break;
    }

    printTime();
    Serial.println(_string_CmdStp);  // end of serial parser
  }
}


///
// setup routines

void setupSerial()
{
  // Hopefully fairly self explanatory.  Starts up the serial
  // communication channel to the computer or LCD or whatever.

  Serial.begin(serialSpeed);

#ifdef serialWaitForConsole
  while (!Serial);
#endif

  delay(serialSetupDelay);
  Serial.println(_string_Intro);
}


void setupClock()
{
  // By default this routine sets up an interrupt based simple clock
  // for determining when to check the RTC and update our local mcu
  // time.  By default it uses a PWM signal from Pin 9 (clockOutPin)
  // connected to Pin 2 (determined by clockInt) to trigger an interrupt
  // routine that drives a counter variable (tick).  The clock interrupt
  // sets updateClockState to true when the counter reaches its
  // predetermined stopping point, and the updateClock() routine will
  // get the time from the RTC and update the local time (clockState).

  attachInterrupt(clockInt, clockCounter, RISING); // magic
  analogReference(DEFAULT); // more magic
  pinMode(heartBeatPin, OUTPUT); // led on 13 usually
  analogWrite(clockOutPin, clockFreq); // setup PWM at 50% on clockOutPin

  updateClockState = true; // update the clock now

  dayTime = dayTimeDef;
  nightTime = nightTimeDef;
}


void setupLamp()
{
  // Turns on the lamp (using SSR or relay), sets the default on
  // and off times for the cycle timer, waits a short time then
  // checks to see if the lamp is actually on, then turns the lamp
  // off until setup is complete.

  pinMode(lampPin, OUTPUT);

  lampOnTime = lampOnTimeDef;
  lampOffTime = lampOffTimeDef;

  // delay( until lamp should be on )
  // read photosensor
  // verify lamp is on

  lampOverride = lampOverrideDef;

  digitalWrite(lampPin, OFF); // force lamp off

  Serial.println(_string_LampRdy);
}


void setupPump()
{
  // Sets up the pump and the control pin for use with a
  // grounding switch.  A debounce capacitor should be added
  // in parallel with the switch for best results.

  pinMode(pumpPin, OUTPUT);
  pinMode(pumpEnablePin, INPUT_PULLUP); // connect switch to ground

  // HACK FOR NOW: make fake ground next to pin 4
  pinMode(3, OUTPUT); digitalWrite(3, LOW);

  pumpOverride = pumpOverrideDef;

  Serial.println(_string_PumpRdy);
}


///
// arduino core

void setup()
{
  // Setup all the peripherals.

  setupClock();
  setupSerial();
  setupLamp();
  setupPump();

  Serial.println(_string_SysRdy);
  printTime();
  Serial.println(_string_SysArm);
}


void loop() // run this code forever
{
  updateClock(); // get new time from rtc
  updateLamp(); // handle cycle timer
  updatePump(); // check / respond to the pump switch
  updateSerial(); // handle serial events
}

