/*
 * Copyright (C) Kimberly McGuire
 *
 * This file is part of paparazzi
 *
 */
/**
 * @file "modules/stereocam/stereocam2state/stereocam2state.c"
 * @author Kimberly McGuire
 * This module sends the data retreived from an external stereocamera modules, to the state filter of the drone. This is done so that the guidance modules can use that information for couadcopter
 */

#include "modules/stereocam/stereocam2state/stereocam2state.h"

#include "subsystems/abi.h"
#include "subsystems/datalink/telemetry.h"

#include "subsystems/radio_control.h"
#include "subsystems/gps/gps_datalink.h"

#include "paparazzi.h"
#include "math/pprz_algebra.h"

#include "modules/droplet/droplet.h"

extern struct Int32Eulers stab_att_sp_euler;

#ifndef STEREOCAM2STATE_SENDER_ID
#define STEREOCAM2STATE_SENDER_ID ABI_BROADCAST
#endif

#ifndef STEREOCAM2STATE_RECEIVED_DATA_TYPE
#define STEREOCAM2STATE_RECEIVED_DATA_TYPE 0
#endif

int8_t fps;
int8_t win_x, win_y, win_size, win_fitness;
uint8_t cnt_left, cnt_middle, cnt_right;
int16_t nus_turn_cmd = 0;
int16_t nus_climb_cmd = 0;
int8_t nus_gate_heading = 0; // gate heading relative to the current heading, in degrees
int8_t body2cam = 0; // offset between heading and camera direction, in degrees
int8_t nus_switch = 0;

uint8_t pos_thresh = 10; // steer only if window center is more than pos_thresh of the center
uint8_t fit_thresh = 12; // maximal fitness that is still considered as correct detection
uint8_t size_thresh = 10; // minimal size that is considered to be a window
uint8_t turn_cmd_max = 50; // percentage of MAX_PPRZ
uint8_t climb_cmd_max = 10; // percentage of MAX_PPRZ
uint8_t cnt_thresh = 80; // threshold for max amount of pixels in a bin (x10)
uint8_t nus_filter_order = 0.4; // complementary filter setting
uint8_t Nmsg2skip = 0; // number of camera messages to be skipped

uint32_t disparities_high, disparities_total, histogram_obs, count_disps_left, count_disps_right;

float redroplet_wait = 4.;
uint8_t gate_count_thresh = 4;

uint8_t gate_count = 0;

static void send_stereo_data(struct transport_tx *trans, struct link_device *dev)
 {
//	int16_t course=(DegOfRad(gps_datalink.course) / ((int32_t)1e6)); // sent in deca degrees (0.1 deg)
//	int32_t north=gps_datalink.utm_pos.north;
//	int32_t east=gps_datalink.utm_pos.east;
//	int32_t alt=gps_datalink.utm_pos.alt;

	pprz_msg_send_STEREO_DATA(trans, dev, AC_ID,
                         &win_x, &win_y, &win_size, &win_fitness, &nus_turn_cmd, &nus_climb_cmd, &nus_gate_heading,
                         &fps, &gate_count, &cnt_middle, &cnt_right, &droplet_active);
	pprz_msg_send_STEREO_LOW_TEXTURE(trans, dev, AC_ID,
	                     &disparities_high, &disparities_total, &histogram_obs, &count_disps_left, &count_disps_right);
						// , &course, &north, &east, &alt
}

static float vel_body_x, vel_body_y, vel_body_z;
int16_t flow_x, flow_y, div_x, div_y;
float flow_fps;
uint16_t agl;
static void send_opticflow(struct transport_tx *trans, struct link_device *dev)
 {
  // Reusing the OPTIC_FLOW_EST telemetry messages, with some values replaced by 0
  uint16_t dummy_uint16 = 0;
  int16_t dummy_int16 = 0;
  float dummy_float = 0;

  float div = (float)div_x / 100.;
  float speed = sqrtf(vel_body_x*vel_body_x + vel_body_y*vel_body_y + vel_body_z*vel_body_z);

  pprz_msg_send_OPTIC_FLOW_EST(trans, dev, AC_ID, &flow_fps, &agl, &dummy_uint16, &flow_x, &flow_y, &div_x, &div_y,
      &vel_body_x, &vel_body_y, &speed, &dummy_float, &div);
}

void stereocam_to_state(void);

void stereo_to_state_init(void)
{
	register_periodic_telemetry(DefaultPeriodic, PPRZ_MSG_ID_STEREO_DATA, send_stereo_data);
	register_periodic_telemetry(DefaultPeriodic, PPRZ_MSG_ID_OPTIC_FLOW_EST, send_opticflow);
}

void stereo_to_state_cb(void)
{
  /* radio switch */
  static int8_t gate_heading = 0;
  static int8_t gate_detected = 0;
  //static uint8_t gate_count = 0;
  static float gate_time = 0;
  static uint8_t through_gate = 0;

  if (radio_control.values[5] < 0){ // this should be ELEV D/R
    nus_switch = 0;
    nus_turn_cmd = 0;
  } else {
    nus_switch = 1;
  }

  static uint8_t msg_counter = 0;

  if (stereocam_data.fresh){
    if (stereocam_data.len == 8){// && msg_counter++ >= Nmsg2skip) { // length of NUS window detection code
      int8_t* pointer=stereocam_data.data; // to transform uint8 message back to int8

      win_x = pointer[0];
      win_y = pointer[1];
      win_size = pointer[2];
      win_fitness = pointer[3];
      fps = pointer[4];
      cnt_left = stereocam_data.data[5];
      cnt_middle = stereocam_data.data[6];
      cnt_right = stereocam_data.data[7];

      msg_counter = 0;

      if (win_size > size_thresh && win_fitness > fit_thresh) // valid gate detection
      {
        if(++gate_count > gate_count_thresh){
          // nus_turn_cmd=MAX_PPRZ/100*turn_cmd_max*nus_switch*win_x/64;
          gate_heading = 30*win_x/64 + body2cam;
          gate_detected = 1;
          gate_time = get_sys_time_float();
          // temporarily deactivate droplet
          droplet_active = 0;
          nus_turn_cmd = 0;
        } else {
          nus_gate_heading = 30*win_x/64 + body2cam;
        }

        // nus_climb_cmd=MAX_PPRZ*climb_cmd_max/100*nus_switch*win_y/48*100/(2*win_size); // gate size is 1 meter, win size is half of the gate size in pixels
        // TODO change climb cmd based on body pitch
        nus_climb_cmd=0;
      } /*else if (win_size < size_thresh && win_fitness > fit_thresh) {// incomplete window detected, use previous command
        // keeping the same command
        nus_climb_cmd = 0;
        // gate_heading=0;
        // nus_turn_cmd=0;
      }*/ else if (gate_count > gate_count_thresh) {  // substantial gate detection event, assume we passed through door
        if(!through_gate && get_sys_time_float() - gate_time > redroplet_wait){  // wait fixed time before reactivating droplet
          // turn slightly right to help that we traverse the room correctly
          gate_heading += 45 * droplet_turn_direction;
          nus_gate_heading = gate_heading;  // overwrite filter
          gate_detected = 1;
          through_gate = 1;
        } else if(through_gate && get_sys_time_float() - gate_time > redroplet_wait + 1.){  // wait fixed time before reactivating droplet
          droplet_turn_direction = -droplet_turn_direction;         // reverse droplet direction
          droplet_active = 1;                       // reactivate droplet
          gate_count = 0;                           // reset gate counter
          through_gate = 0;
        }
      } else { // no window detected - if (win_fitness < fit_thresh)
        nus_climb_cmd = 0;
        gate_heading = 0;
        nus_gate_heading = 0;
        through_gate = 0;

        if(gate_count > 0){
          gate_count--;
        }

        droplet_active = 1;

        /*
        if (cnt_left > cnt_thresh && cnt_right < cnt_thresh) {
          nus_turn_cmd=nus_switch*MAX_PPRZ/100*turn_cmd_max;
        }	else if (cnt_left < cnt_thresh && cnt_right > cnt_thresh) {
          nus_turn_cmd=-nus_switch*MAX_PPRZ/100*turn_cmd_max;
        }	else {
          nus_turn_cmd=0;
        }
        */
      }

      /* simple filter */
      nus_gate_heading = (nus_filter_order-1)*nus_gate_heading/nus_filter_order + gate_heading/nus_filter_order;

      if (gate_detected && nus_switch){
        gate_detected = 0;
        /* add the gate heading to the current heading */
        stab_att_sp_euler.psi = ANGLE_BFP_OF_REAL(RadOfDeg(nus_gate_heading)) + stabilization_attitude_get_heading_i();
        INT32_ANGLE_NORMALIZE(stab_att_sp_euler.psi);
      }

      //autopilot_guided_goto_body_relative(0.0, 0.0, nus_climb_cmd, 0.0)
    } else if (stereocam_data.len == 20) {
      //run_droplet((uint32_t)stereocam_data.data[0], (uint32_t)stereocam_data.data[4]);
      uint32_t* buffer32 = (uint32_t*)stereocam_data.data;
      run_droplet_low_texture(buffer32[0], buffer32[1], buffer32[2], buffer32[3], buffer32[4]);

      disparities_high = buffer32[0];
      disparities_total = buffer32[1];
      histogram_obs = buffer32[2];
      count_disps_left = buffer32[3];
      count_disps_right = buffer32[4];

    } else if (stereocam_data.len == 24) {
      int32_t* buffer32 = (int32_t*)stereocam_data.data;
      wall_estimate(buffer32[0], buffer32[1], buffer32[2], buffer32[3], buffer32[4], buffer32[5]);
    } else if (stereocam_data.len == 25) {
      stereocam_to_state();
    }
	  stereocam_data.fresh = 0;
	}
}

void stereocam_to_state(void)
{

  // Get info from stereocam data
  // 0 = stereoboard's #define SEND_EDGEFLOW
  // opticflow
  div_x = (int16_t)stereocam_data.data[0] << 8;
  div_x |= (int16_t)stereocam_data.data[1];
  flow_x = (int16_t)stereocam_data.data[2] << 8;
  flow_x |= (int16_t)stereocam_data.data[3];
  div_y = (int16_t)stereocam_data.data[4] << 8;
  div_y |= (int16_t)stereocam_data.data[5];
  flow_y = (int16_t)stereocam_data.data[6] << 8;
  flow_y |= (int16_t)stereocam_data.data[7];

  flow_fps = (float)stereocam_data.data[9];
  agl = stereocam_data.data[8]; // in cm

  // velocity
  int16_t vel_y_int = (int16_t)stereocam_data.data[12] << 8;
  vel_y_int |= (int16_t)stereocam_data.data[13];

  int16_t vel_x_int = (int16_t)stereocam_data.data[16] << 8;
  vel_x_int |= (int16_t)stereocam_data.data[17];
  int16_t vel_z_int = (int16_t)stereocam_data.data[18] << 8;
  vel_z_int |= (int16_t)stereocam_data.data[19];

  float RES = 100.;

  vel_body_x = (float)vel_x_int / RES;
  vel_body_y = (float)vel_y_int / RES;
  vel_body_z = (float)vel_z_int / RES;

  // Derotate velocity and transform from frame to body coordinates
  // TODO: send resolution directly from stereocam

  //Send velocity estimate to state
  //TODO:: Make variance dependable on line fit error, after new horizontal filter is made
  if (0){//abs(vel_body_x) < 5. && abs(vel_body_x) < 5.) {
    AbiSendMsgVELOCITY_ESTIMATE(STEREOCAM2STATE_SENDER_ID, get_sys_time_usec(),
                                vel_body_x,
                                vel_body_y,
                                vel_body_z,
                                0.3f
                               );
  }
}
