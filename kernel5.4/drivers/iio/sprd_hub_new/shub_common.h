/* SPDX-License-Identifier: GPL-2.0 */
/*
 * File:shub_common.h
 *
 * Copyright (C) 2018 Spreadtrum Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 */

#ifndef SHUB_COMMON_INCLUDE_H
#define SHUB_COMMON_INCLUDE_H

#include <linux/iio/iio.h>
#include <linux/irq_work.h>
#include <linux/module.h>
#include <linux/workqueue.h>
#include "shub_protocol.h"

#define MAIN_VERSION		"2016_1116"
#define MODNAME			"sprd-sensor"
#undef pr_fmt
#define pr_fmt(fmt)		MODNAME ": %s %d:" fmt, __func__, __LINE__
#define TRANSFER_NODE		"/dev/spipe_pm0"
#define CALIBRATION_NODE	"/mnt/vendor/productinfo/sensor_calibration_data/"
/* calibration data format */
#define CALIBRATION_DATA_LENGTH	30
#define CALIB_PATH_MAX_LENG	100

#define READ_BUFF_SIZE		128
#define SERIAL_READ_BUFFER_MAX	128

/* MUST equal to the iio total channel bytes size */
#define MAX_CM4_MSG_SIZE	40
#define SYNC_TIME_DELAY_MS	10000

/*
 * dont open the macro,
 * system crash if sensor run long time
 */
#define SHUB_DATA_DUMP		0
#define SHUB_DEBUG_TS		0
/*
 * Description:
 * Define All kind of Sensors
 */
#define HANDLE_SENSOR_ID_BASE		0
#define HANDLE_SENSOR_WAKEUP_ID_BASE		0x64
#define HANDLE_COMPOSITE_ID_BASE	0x64
#define HANDLE_COMPOSITE_WAKEUP_ID_BASE	0x96
#define HANDLE_MCU_EVENT_BASE		0xc8
#define HANDLE_MAX			0xff

enum sensor_id {
	SENSOR_ID_START = HANDLE_SENSOR_ID_BASE,
	SENSOR_ACCELEROMETER,
	SENSOR_GEOMAGNETIC_FIELD,
	SENSOR_ORIENTATION,
	SENSOR_GYROSCOPE,
	SENSOR_LIGHT,
	SENSOR_PRESSURE,
	SENSOR_TEMPERATURE,
	SENSOR_PROXIMITY,
	SENSOR_GRAVITY,
	SENSOR_LINEAR_ACCELERATION,
	SENSOR_ROTATION_VECTOR,
	SENSOR_RELATIVE_HUMIDITY,
	SENSOR_AMBIENT_TEMPERATURE,
	SENSOR_MAGNETIC_FIELD_UNCALIBRATED,
	SENSOR_GAME_ROTATION_VECTOR,
	SENSOR_GYROSCOPE_UNCALIBRATED,
	SENSOR_SIGNIFICANT_MOTION,
	SENSOR_STEP_DETECTOR,
	SENSOR_STEP_COUNTER,
	SENSOR_GEOMAGNETIC_ROTATION_VECTOR,
	SENSOR_HEART_RATE,
	SENSOR_TILT_DETECTOR,
	SENSOR_WAKE_GESTURE,
	SENSOR_GLANCE_GESTURE,
	SENSOR_PICK_UP_GESTURE,
	SENSOR_WRIST_TILT_GESTURE,
	SENSOR_ID_END
};

enum wakeup_sensor_id {
	SENSOR_WAKE_UP_SENSOR_ID_START = HANDLE_SENSOR_WAKEUP_ID_BASE,
	SENSOR_WAKE_UP_ACCELEROMETER,
	SENSOR_WAKE_UP_GEOMAGNETIC_FIELD,
	SENSOR_WAKE_UP_ORIENTATION,
	SENSOR_WAKE_UP_GYROSCOPE,
	SENSOR_WAKE_UP_LIGHT,
	SENSOR_WAKE_UP_PRESSURE,
	SENSOR_WAKE_UP_TEMPERATURE,
	SENSOR_WAKE_UP_PROXIMITY,
	SENSOR_WAKE_UP_GRAVITY,
	SENSOR_WAKE_UP_LINEAR_ACCELERATION,
	SENSOR_WAKE_UP_ROTATION_VECTOR,
	SENSOR_WAKE_UP_RELATIVE_HUMIDITY,
	SENSOR_WAKE_UP_AMBIENT_TEMPERATURE,
	SENSOR_WAKE_UP_MAGNETIC_FIELD_UNCALIBRATED,
	SENSOR_WAKE_UP_GAME_ROTATION_VECTOR,
	SENSOR_WAKE_UP_GYROSCOPE_UNCALIBRATED,
	SENSOR_WAKE_UP_SIGNIFICANT_MOTION,
	SENSOR_WAKE_UP_STEP_DETECTOR,
	SENSOR_WAKE_UP_STEP_COUNTER,
	SENSOR_WAKE_UP_GEOMAGNETIC_ROTATION_VECTOR,
	SENSOR_WAKE_UP_HEART_RATE,
	SENSOR_WAKE_UP_TILT_DETECTOR,
	SENSOR_WAKE_UP_WAKE_GESTURE,
	SENSOR_WAKE_UP_GLANCE_GESTURE,
	SENSOR_WAKE_UP_PICK_UP_GESTURE,
	SENSOR_WAKE_UP_WRIST_TILT_GESTURE,
	SENSOR_WAKE_UP_SENSOR_ID_END
};

enum composite_sensor_id {
	SENSOR_COMPOSITE_SENSOR_ID_START = HANDLE_COMPOSITE_ID_BASE,
	SENSOR_COMPOSITE_SHAKE,
	SENSOR_COMPOSITE_TAP,
	SENSOR_COMPOSITE_FLIP,
	SENSOR_COMPOSITE_TWIST,
	SENSOR_COMPOSITE_POCKET_MODE,
	SENSOR_COMPOSITE_HAND_UP,
	SENSOR_COMPOSITE_HAND_DOWN,
	SENSOR_COMPOSITE_FACE_UP,
	SENSOR_COMPOSITE_FACE_DOWN,
	SENSOR_COMPOSITE_PRIVATE_SENSOR_A,
	SENSOR_COMPOSITE_CONTEXT_AWARENESS,
	SENSOR_COMPOSITE_STATIC_DETECTOR,
	SENSOR_COMPOSITE_VIRTUAL_GYRO,
	SENSOR_COMPOSITE_AIR_RECOGNITION,
	SENSOR_COMPOSITE_PDR,
	SENSOR_COLOR_TEMP = 121,
	SENSOR_COMPOSITE_SENSOR_ID_END
};

enum composite_wakeup_sensor_id {
	SENSOR_COMPOSITE_WAKE_UP_SENSOR_ID_START =
		HANDLE_COMPOSITE_WAKEUP_ID_BASE,
	SENSOR_COMPOSITE_WAKE_UP_SHAKE,
	SENSOR_COMPOSITE_WAKE_UP_TAP,
	SENSOR_COMPOSITE_WAKE_UP_FLIP,
	SENSOR_COMPOSITE_WAKE_UP_TWIST,
	SENSOR_COMPOSITE_WAKE_UP_POCKET_MODE,
	SENSOR_COMPOSITE_WAKE_UP_HAND_UP,
	SENSOR_COMPOSITE_WAKE_UP_HAND_DOWN,
	SENSOR_COMPOSITE_WAKE_UP_FACE_UP,
	SENSOR_COMPOSITE_WAKE_UP_FACE_DOWN,
	SENSOR_COMPOSITE_WAKE_UP_PRIVATE_SENSOR_A,
	SENSOR_COMPOSITE_WAKE_UP_CONTEXT_AWARENESS,
	SENSOR_COMPOSITE_WAKE_UP_STATIC_DETECTOR,
	SENSOR_COMPOSITE_WAKE_UP_VIRTUAL_GYRO,
	SENSOR_COMPOSITE_WAKE_UP_AIR_RECOGNITION,
	SENSOR_COMPOSITE_WAKE_UP_PDR,
	SENSOR_WAKE_UP_COLOR_TEMP = SENSOR_COLOR_TEMP +
				    HANDLE_COMPOSITE_WAKEUP_ID_BASE,
	SENSOR_COMPOSITE_WAKE_UP_SENSOR_ID_END
};

enum mcu_to_cpu_event_type {
	SENSOR_SPECIAL_ID_START = HANDLE_MCU_EVENT_BASE,
	SENSOR_META_DATA,
	SENSOR_SYNC_SYS_REAL_TIME,
	SENSOR_MCU_REINITIAL,
	SENSOR_SPECIAL_ID_END
};
#endif
