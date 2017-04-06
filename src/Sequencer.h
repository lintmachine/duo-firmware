#ifndef Sequencer_h
#define Sequencer_h

#include "TempoHandler.h"

// class Sequencer {
//   public:
//     void init();
//     void restart();
//     void start();
//     void stop();
//     void advance();
//     void advance_without_play();
//     void clock_tick();
//     void clock_reset();
//     void reset();
//     void update();
//   private:
// };

//Initial sequencer values
uint8_t step_note[] = { 1,0,6,9,0,4,0,5 };
uint8_t step_enable[] = { 1,0,1,1,1,1,0,1 };
uint8_t step_velocity[] = { 100,100,100,100,100,100,100,100 };

void sequencer_init();
void sequencer_restart();
void sequencer_start();
void sequencer_stop();
void sequencer_advance();
void sequencer_tick_clock();
void sequencer_reset();
void sequencer_update();
void keyboard_to_note();
int keyboard_get_highest_note();
int keyboard_get_latest_note();

void sequencer_reset_clock();
static void sequencer_advance_without_play();
static void sequencer_trigger_note();
static void sequencer_untrigger_note();
bool sequencer_is_running = false;
bool note_is_done_playing = false;

uint8_t sequencer_clock = 0;

uint32_t next_step_time = 0;
uint32_t gate_off_time = 0;
uint32_t note_on_time;
uint32_t previous_note_on_time;
uint32_t note_off_time;

bool double_speed = false;

const uint8_t SEQUENCER_NUM_STEPS = 8;

TempoHandler tempo_handler;

void sequencer_init() {
  for(int i = 0; i < SEQUENCER_NUM_STEPS; i++) {
    step_note[i] = random(9);
  }
  tempo_handler.setHandleTempoEvent(sequencer_tick_clock);
  tempo_handler.setHandleResetEvent(sequencer_reset_clock);
  sequencer_stop();
  current_step = SEQUENCER_NUM_STEPS - 1;
}

void sequencer_restart() {
  MIDI.sendRealTime(midi::Start);
  // TODO: According to MIDI specs, we should wait at least a millisecond before sending timer packets
  current_step = SEQUENCER_NUM_STEPS - 1;
  tempo_handler.midi_clock_reset();
  sequencer_is_running = true;
  sequencer_clock = 0;
}

void sequencer_reset_clock() {
  sequencer_clock -= (sequencer_clock%12);
}

void sequencer_start() {
  MIDI.sendRealTime(midi::Continue);
  tempo_handler.midi_clock_reset();
  sequencer_is_running = true;
}

void sequencer_stop() {
  if(sequencer_is_running) {
    MIDI.sendRealTime(midi::Stop);
    sequencer_is_running = false;
    sequencer_untrigger_note();
  }
  midi_clock = 0;
}

void sequencer_toggle_start() {
  if(sequencer_is_running) {
    sequencer_stop();
  } else {
    sequencer_start();
  }
}

void sequencer_tick_clock() {
  uint8_t divider = 12;
  if(double_speed) {
    divider = 6;
  }
  if(sequencer_is_running && (sequencer_clock % divider)==0) {
    sequencer_advance();
  } 
  sequencer_clock++;
  if(sequencer_clock >= 96) {
    sequencer_clock = 0;
  }
}

void sequencer_advance_without_play() {
  if(!note_is_done_playing) {
    sequencer_untrigger_note();
  }
  if (!next_step_is_random && !random_flag) {
    current_step++;
    current_step%=SEQUENCER_NUM_STEPS;
  } else {
    random_flag = false;
    current_step = ((current_step + random(2, SEQUENCER_NUM_STEPS))%SEQUENCER_NUM_STEPS);
  }

  // Sample keys
  if(keyboard_get_latest_note() != -1) {
    step_note[current_step] = keyboard_get_latest_note();
    step_enable[current_step] = 1;
    step_velocity[current_step] = INITIAL_VELOCITY; 
  }
}

void sequencer_advance() {
  sequencer_advance_without_play();
  sequencer_trigger_note();
}

void sequencer_reset() {
  current_step = SEQUENCER_NUM_STEPS;
}

void sequencer_update() {
  tempo_handler.update();

  if(!note_is_done_playing && millis() >= note_off_time && note_is_triggered) { 
    sequencer_untrigger_note();
  }
}

static void sequencer_trigger_note() {
  note_is_triggered = true;
  note_is_done_playing = false;
  previous_note_on_time = millis();
  note_off_time = previous_note_on_time + gate_length_msec;

  step_velocity[current_step] = INITIAL_VELOCITY;

  note_on(SCALE[step_note[current_step]]+transpose, step_velocity[current_step], step_enable[current_step]);
}

static void sequencer_untrigger_note() {
  note_is_done_playing = true;
  note_off();
  note_is_triggered = false;
  note_off_time = millis() + tempo_interval - gate_length_msec; // Set note off time to sometime in the future
}


#define KEYBOARD_SIZE 10

uint8_t keyboard_map[KEYBOARD_SIZE] = { 0,0,0,0,0,0,0,0,0,0 };
uint8_t keyboard_latest_note = 0;

int keyboard_get_number_pressed_notes() {
  int n = 0;
  for(int i = KEYBOARD_SIZE; i >= 0; i--) {
    if(keyboard_map[i]){
      n++;
    }
  }
  return n;
}

int keyboard_get_highest_note() {
  for(int i = KEYBOARD_SIZE; i >= 0; i--) {
    if(keyboard_map[i]){
      return i;
    }
  }
  return -1;
}

int keyboard_get_latest_note() {
  for(int i = 0; i < KEYBOARD_SIZE; i++) {
    if(keyboard_map[i]){
      return keyboard_latest_note;
    }
  }
  return -1;
}

void keyboard_set_note(uint8_t note) {
  if(!keyboard_map[note]) {
    keyboard_latest_note = note;
  }
  keyboard_map[note] = 1;
}

void keyboard_unset_note(uint8_t note) {
  keyboard_map[note] = 0;
}

void keyboard_to_note() {
  static int n = 0; // Latest note

  if(!sequencer_is_running) {
    // If a key has been pressed,
    int k = keyboard_get_latest_note();
    if(n != k) {
      if(k == -1) {
        note_off();
      } else {
        sequencer_advance_without_play();
        note_on(SCALE[keyboard_get_latest_note()]+transpose, INITIAL_VELOCITY, true);
      }
      n = k;
    }
  }
}

#endif