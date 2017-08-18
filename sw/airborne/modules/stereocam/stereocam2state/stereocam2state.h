/*
 * Copyright (C) Kimberly McGuire
 *
 * This file is part of paparazzi
 *
 */
/**
 * @file "modules/stereocam/stereocam2state/stereocam2state.h"
 * @author Kimberly McGuire
 * This module sends the data retreived from an external stereocamera modules, to the state filter of the drone. This is done so that the guidance modules can use that information for couadcopter
 */

#ifndef STEREOCAM2STATE_H
#define STEREOCAM2STATE_H

#include <std.h>
#include "modules/stereocam/stereocam.h"

extern void stereo_to_state_init(void);
extern void stereo_to_state_periodic(void);

extern float redroplet_wait;
extern uint8_t gate_count_thresh;

extern int8_t fps, win_x, win_y, win_size, win_fitness, nus_gate_heading, body2cam;
extern int8_t nus_switch;
extern int16_t nus_turn_cmd, nus_climb_cmd;
extern uint8_t pos_thresh, fit_thresh, size_thresh, climb_cmd_max, turn_cmd_max, cnt_left, cnt_middle, cnt_right, cnt_thresh, nus_filter_order, Nmsg2skip;
extern bool nus_gate_detected;

#endif
