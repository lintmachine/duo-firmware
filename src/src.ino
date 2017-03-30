/*
  DATO DUO firmware
  (c) 2016, David Menting <david@dato.mu>
*/
#include "Arduino.h"
#include <Keypad.h>
#include "TouchSlider.h"

#define VERSION "0.5.2"

const int MIDI_CHANNEL = 1;
const int SYNC_LENGTH_MSEC = 12;

// Musical settings
const uint8_t SCALE[] = { 49,51,54,56,58,61,63,66,68,70 }; // Low with 2 note split
const float   SAMPLERATE_STEPS[] = { 44100,4435,2489,1109 }; 

#define INITIAL_VELOCITY 100

// Globals that should not be globals
int gate_length_msec = 40;

// Sequencer settings
uint8_t current_step; // TODO: should be sequencer_num_steps
int tempo = 0;
uint8_t set_key = 9;
float osc_saw_frequency = 0.;
float osc_pulse_frequency = 0.;
float osc_pulse_target_frequency = 0.;
float osc_saw_target_frequency = 0.;
uint8_t osc_pulse_midi_note = 0;
bool note_is_playing = 0;
bool note_is_triggered = false;
int transpose = 0;
bool next_step_is_random = false;
int num_notes_held = 0;
int tempo_interval;
bool random_flag = 0;

uint32_t midi_clock = 0;
uint16_t audio_peak_values;
uint16_t peak_update_time;

uint16_t keyboard_map = 0;
uint16_t old_keyboard_map = 0;
const uint16_t KEYBOARD_MASK = 0b11111111111;

void keys_scan();
void pots_read();
void drum_init();
void drum_read();

void note_on(uint8_t midi_note, uint8_t velocity, bool enabled);
void note_off();

void keyboard_to_note();
float detune(int note, int amount);

int tempo_interval_msec();

void enter_dfu();

#include "pinmap.h"
#include "MidiFunctions.h"
#include "Buttons.h"
#include "Synth.h"
#include "Sequencer.h"
#include "Leds.h"
#include "DrumSynth.h"
#include "Pitch.h"
#include "Power.h"

void setup() {

  pins_init();
  amp_disable();
  headphone_disable();
  sequencer_init();
  audio_init();
  led_init();
  Serial.begin(57600);

  midi_init();

  MIDI.setHandleStart(sequencer_restart);
  MIDI.setHandleContinue(sequencer_restart);
  MIDI.setHandleStop(sequencer_stop);

  keys_init();
  drum_init();
  touch_init();
  
  previous_note_on_time = millis();
  
  Serial.print("Dato DUO firmware ");
  Serial.println(VERSION);
}

void loop() {
  if(power_check()) {
    keys_scan();
    keyboard_to_note();          
    sequencer_update();
    midi_handle();
    pitch_update();
    pots_read();
    led_update();
    drum_read();
  }
}

// Scans the keypad and handles step enable and keys
void keys_scan() {
  if(muxDigitalRead(DELAY_PIN)) {
    delayMixer.gain(0, 0.0); // Delay input
    delayMixer.gain(3, 0.0);
  } else {
    delayMixer.gain(0, 0.5); // Delay input
    delayMixer.gain(3, 0.4); // Hat delay input
  }

  if (keypad.getKeys())  {
    for (int i=0; i<LIST_MAX; i++) {
      if ( keypad.key[i].stateChanged ) {
        char k = keypad.key[i].kchar;
        switch (keypad.key[i].kstate) {  // Report active key state : IDLE, PRESSED, HOLD, or RELEASED
            case PRESSED:   
                if (k <= KEYB_9 && k >= KEYB_0) {
                  bitSet(keyboard_map,(k - KEYB_0));
                } else if (k <= STEP_8 && k >= STEP_1) {
                  step_enable[k-STEP_1] = 1-step_enable[k-STEP_1];
                  if(!step_enable[k-STEP_1]) { leds(k-STEP_1) = CRGB::Black; }
                  step_velocity[k-STEP_1] = INITIAL_VELOCITY;
                } else if (k == BTN_SEQ2) {
                  if(!sequencer_is_running) {
                    sequencer_advance();
                  }
                  double_speed = true;
                } else if (k == BTN_DOWN) {
                  transpose--;
                  if(transpose<-12){transpose = -24;}
                } else if (k == BTN_UP) {
                  transpose++;
                  if(transpose>12){transpose = 24;}
                } else if (k == BTN_SEQ1) {
                  next_step_is_random = true;
                  if(!sequencer_is_running) {
                    sequencer_advance();
                  }
                  random_flag = true;
                } else if (k == SEQ_START) {
                  sequencer_toggle_start();
                }
                break;
            case HOLD:
                if (k == SEQ_START) {
                  power_off();
                }
                break;
            case RELEASED:
                if (k <= KEYB_9 && k >= KEYB_0) {
                  // MIDI.sendNoteOff(SCALE[k-KEYB_0]+transpose,64,MIDI_CHANNEL);
                  bitClear(keyboard_map,(k - KEYB_0));
                } else if (k == BTN_SEQ2) {
                  double_speed = false;
                } else if (k == BTN_DOWN) {
                  if(transpose<-12){transpose = -12;}
                  if(transpose>12){transpose = 12;}
                } else if (k == BTN_UP) {
                  if(transpose<-12){transpose = -12;}
                  if(transpose>12){transpose = 12;}
                } else if (k == BTN_SEQ1) {
                  next_step_is_random = false;
                } 
                break;
            case IDLE:
                if (k <= KEYB_9 && k >= KEYB_0) {
                  bitClear(keyboard_map,(k - KEYB_0));
                }
                break;
        }
      }
    }
  } 
}

void pots_read() {
  // Read out the pots/switches
  gate_length_msec = map(analogRead(GATE_POT),1023,0,10,200);

  int volume_pot_value = muxAnalogRead(FADE_POT);
  int resonance = muxAnalogRead(FILTER_RES_POT);
  int amp_env_release = map(muxAnalogRead(AMP_ENV_POT),0,1023,30,500);
  uint32_t filter_pot_value = muxAnalogRead(FILTER_FREQ_POT);
  int pulse_pot_value = muxAnalogRead(OSC_PW_POT);
  detune_amount = muxAnalogRead(OSC_DETUNE_POT);
  analogWrite(FILTER_LED, filter_pot_value>>2);
  analogWrite(OSC_LED, 255-(pulse_pot_value>>2));

  // Audio interrupts have to be off to apply settings
  AudioNoInterrupts();

  osc_saw.frequency(osc_saw_frequency);

  if(detune_amount > 800) {
    osc_saw.amplitude(map(detune_amount,800,1023,400,0)/1000.);
  } else {
    osc_saw.amplitude(0.4);
  }
  osc_pulse.frequency(osc_pulse_frequency);
  osc_pulse.pulseWidth(map(pulse_pot_value,0,1023,1000,100)/1000.0);

  filter1.frequency(((filter_pot_value*filter_pot_value)/3072)+40);
  filter1.resonance(map(resonance,0,1023,70,500)/100.0); // 0.7-5.0 range

  envelope1.release(amp_env_release);

  if(digitalRead(BITC_PIN)) {
    bitcrusher1.sampleRate(SAMPLERATE_STEPS[0]);
  } else {
    bitcrusher1.sampleRate(SAMPLERATE_STEPS[2]);
  }

  audio_volume(volume_pot_value);

  AudioInterrupts(); 
}

void note_on(uint8_t midi_note, uint8_t velocity, bool enabled) {
  // Override velocity if button on the synth is pressed
  if(!digitalRead(ACCENT_PIN)) {
    velocity = 127;
  }

  note_is_playing = midi_note;

  if(enabled) {
    AudioNoInterrupts();

    dc1.amplitude(velocity / 127.); // DC amplitude controls filter env amount.
    osc_pulse_midi_note = midi_note;
    osc_pulse_target_frequency = (int)midi_note_to_frequency(midi_note);
    osc_pulse.frequency(osc_pulse_frequency);
    // Detune OSC2
    osc_saw.frequency(detune(osc_pulse_midi_note,detune_amount));

    AudioInterrupts(); 

    MIDI.sendNoteOn(midi_note, velocity, MIDI_CHANNEL);
    envelope1.noteOn();
    envelope2.noteOn();
  } else {
    leds(current_step) = LED_WHITE;
  }
}

void note_off() {
  if (note_is_playing) {
    MIDI.sendNoteOff(note_is_playing, 64, MIDI_CHANNEL);
    if(!step_enable[current_step]) {
      leds(current_step) = CRGB::Black;
    } else {
      envelope1.noteOff();
      envelope2.noteOff();
    }
    note_is_playing = 0;
  } 
  //Purge all held notes

}

/*
 * enter_dfu() writes a special string to the K20DX256's VBAT register and reboots the mcu
 * This signals the Kiibohd DFU bootloader that it should start in firmware update mode
 */
void enter_dfu() {
  #define VBAT                    *(volatile uint8_t *)0x4003E000 // Register available in all power states
  const uint8_t sys_reset_to_loader_magic[22] = "\xff\x00\x7fRESET TO LOADER\x7f\x00\xff";

  // Blank all leds and turn the power button blue before rebooting
  FastLED.clear();
  physical_leds[0] = CRGB::Blue;
  FastLED.show();
  // Reset
  for (unsigned int pos = 0; pos < sizeof(sys_reset_to_loader_magic); pos++ ) {
    (&VBAT)[ pos ] = sys_reset_to_loader_magic[ pos ];
  }
  SCB_AIRCR = 0x05FA0004; // software reset
}