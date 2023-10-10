// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2011 Unisoc Co., Ltd.
 * Changhua.Zhang <Changhua.Zhang@unisoc.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/gpio/consumer.h>
#include <linux/iio/consumer.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/nvmem-consumer.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_wakeup.h>
#include <linux/power_supply.h>
#include <linux/power/charger-manager.h>
#include <linux/power/sprd_battery_info.h>
#include <linux/power/sprd_fuel_gauge_core.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/usb/phy.h>
#include <linux/rtc.h>

/* rtc reg default value */
#define SPRD_FGU_DEFAULT_CAP				GENMASK(11, 0)
#define SPRD_FGU_NORMAL_POWERON				0x5
#define SPRD_FGU_RTC2_RESET_VALUE			0xA05
/* uusoc vbat */
#define SPRD_FGU_LOW_VBAT_REGION			3400
#define SPRD_FGU_LOW_VBAT_REC_REGION			3450
#define SPRD_FGU_LOW_VBAT_UUSOC_STEP			7
/* sleep calib */
#define SPRD_FGU_SLP_CAP_CALIB_SLP_TIME			300
#define SPRD_FGU_CAP_CALIB_TEMP_LOW			100
#define SPRD_FGU_CAP_CALIB_TEMP_HI			450
#define SPRD_FGU_SR_ARRAY_LEN				100
#define SPRD_FGU_SR_STOP_CHARGE_TIMES			(30 * 60)
#define SPRD_FGU_SR_SLEEP_MIN_TIME_S			(10 * 60)
#define SPRD_FGU_SR_AWAKE_MAX_TIME_S			90
#define SPRD_FGU_SR_AWAKE_BIG_CUR_MAX_TIME_S		30
#define SPRD_FGU_SR_SLEEP_AVG_CUR_MA			30
#define SPRD_FGU_SR_AWAKE_AVG_CUR_MA			200
#define SPRD_FGU_SR_LAST_SLEEP_TIME_S			(4 * 60)
#define SPRD_FGU_SR_LAST_AWAKE_TIME_S			30
#define SPRD_FGU_SR_DUTY_RATIO				95
#define SPRD_FGU_SR_TOTAL_TIME_S			(30 * 60)
#define SPRD_FGU_SR_VALID_VOL_CNT			3
#define SPRD_FGU_SR_MAX_VOL_MV				4500
#define SPRD_FGU_SR_MIN_VOL_MV				3400
/* discharing calibration */
#define SPRD_FGU_CAP_CALIB_ALARM_CAP			30
/* track cap */
#define SPRD_FGU_TRACK_CAP_START_VOLTAGE		3650
#define SPRD_FGU_TRACK_CAP_START_CURRENT		50
#define SPRD_FGU_TRACK_HIGH_TEMP_THRESHOLD		450
#define SPRD_FGU_TRACK_LOW_TEMP_THRESHOLD		150
#define SPRD_FGU_TRACK_TIMEOUT_THRESHOLD		108000
#define SPRD_FGU_TRACK_NEW_OCV_VALID_THRESHOLD		(SPRD_FGU_TRACK_TIMEOUT_THRESHOLD / 60)
#define SPRD_FGU_TRACK_START_CAP_HTHRESHOLD		200
#define SPRD_FGU_TRACK_START_CAP_LTHRESHOLD		10
#define SPRD_FGU_TRACK_START_CAP_SWOCV_HTHRESHOLD	100
#define SPRD_FGU_TRACK_WAKE_UP_MS			16000
#define SPRD_FGU_TRACK_UPDATING_WAKE_UP_MS		200
#define SPRD_FGU_TRACK_DONE_WAKE_UP_MS			6000
#define SPRD_FGU_TRACK_OCV_VALID_TIME			15
#define SPRD_FGU_CAPACITY_TRACK_0S			0
#define SPRD_FGU_CAPACITY_TRACK_3S			3
#define SPRD_FGU_CAPACITY_TRACK_15S			15
#define SPRD_FGU_CAPACITY_TRACK_100S			100
#define SPRD_FGU_WORK_MS				msecs_to_jiffies(15000)
/* unuse cap */
#define SPRD_FGU_RESIST_ALG_REIST_CNT			40
#define SPRD_FGU_RESIST_ALG_OCV_GAP_UV			20000
#define SPRD_FGU_RESIST_ALG_OCV_CNT			10
#define SPRD_FGU_RBAT_CMP_MOH				10
/* RTC OF 2021-08-06 15 : 44*/
#define SPRD_FGU_MISCDATA_RTC_TIME			(1621355101)
#define SPRD_FGU_SHUTDOWN_TIME				(15 * 60)
/* others define */
#define SPRD_FGU_CAP_CALC_WORK_8S			8
#define SPRD_FGU_CAP_CALC_WORK_15S			15
#define SPRD_FGU_CAP_CALC_WORK_LOW_TEMP			50
#define SPRD_FGU_CAP_CALC_WORK_LOW_CAP			50
#define SPRD_FGU_CAP_CALC_WORK_BIG_CURRENT		3000
#define SPRD_FGU_POCV_VOLT_THRESHOLD			3400
#define SPRD_FGU_TEMP_BUFF_CNT				10
#define SPRD_FGU_LOW_TEMP_REGION			100
#define SPRD_FGU_CURRENT_BUFF_CNT			8
#define SPRD_FGU_DISCHG_CNT				4
#define SPRD_FGU_VOLTAGE_BUFF_CNT			8
#define SPRD_FGU_MAGIC_NUMBER				0x5a5aa5a5
#define SPRD_FGU_DEBUG_EN_CMD				0x5a5aa5a5
#define SPRD_FGU_DEBUG_DIS_CMD				0x5a5a5a5a
#define SPRD_FGU_GOOD_HEALTH_CMD			0x7f7f7f7f
#define SPRD_FGU_FCC_PERCENT				1000
#define SPRD_FGU_REG_MAX				0x260
#define interpolate(x, x1, y1, x2, y2) \
	((y1) + ((((y2) - (y1)) * ((x) - (x1))) / ((x2) - (x1))))

struct power_supply_vol_temp_table {
	int vol;	/* microVolts */
	int temp;	/* celsius */
};

struct power_supply_capacity_temp_table {
	int temp;	/* celsius */
	int cap;	/* capacity percentage */
};

enum sprd_fgu_track_state {
	CAP_TRACK_INIT,
	CAP_TRACK_IDLE,
	CAP_TRACK_UPDATING,
	CAP_TRACK_DONE,
	CAP_TRACK_ERR,
};

enum sprd_fgu_track_mode {
	CAP_TRACK_MODE_UNKNOWN,
	CAP_TRACK_MODE_SW_OCV,
	CAP_TRACK_MODE_POCV,
	CAP_TRACK_MODE_LP_OCV,
};

struct sprd_fgu_ocv_info {
	s64 ocv_rtc_time;
	int ocv_uv;
	bool valid;
};

struct sprd_fgu_track_capacity {
	enum sprd_fgu_track_state state;
	bool clear_cap_flag;
	int start_cc_mah;
	int start_cap;
	int end_vol;
	int end_cur;
	s64 start_time;
	bool cap_tracking;
	int learned_mah;
	struct sprd_fgu_ocv_info lpocv_info;
	struct sprd_fgu_ocv_info pocv_info;
	density_ocv_table *dens_ocv_table;
	int dens_ocv_table_len;
	enum sprd_fgu_track_mode mode;
};

struct sprd_fgu_debug_info {
	bool temp_debug_en;
	bool vbat_now_debug_en;
	bool ocv_debug_en;
	bool cur_now_debug_en;
	bool batt_present_debug_en;
	bool chg_vol_debug_en;
	bool batt_health_debug_en;

	int debug_temp;
	int debug_vbat_now;
	int debug_ocv;
	int debug_cur_now;
	bool debug_batt_present;
	int debug_chg_vol;
	int debug_batt_health;

	int sel_reg_id;
};

struct sprd_fgu_sysfs {
	char *name;
	struct attribute_group attr_g;
	struct device_attribute attr_sprd_fgu_dump_info;
	struct device_attribute attr_sprd_fgu_sel_reg_id;
	struct device_attribute attr_sprd_fgu_reg_val;
	struct device_attribute attr_sprd_fgu_enable_sleep_calib;
	struct device_attribute attr_sprd_fgu_relax_cnt_th;
	struct device_attribute attr_sprd_fgu_relax_cur_th;
	struct attribute *attrs[7];

	struct sprd_fgu_data *data;
};

/*
 * struct sprd_fgu_cap_remap_table
 * @cnt: record the counts of battery capacity of this scope
 * @lcap: the lower boundary of the capacity scope before transfer
 * @hcap: the upper boundary of the capacity scope before transfer
 * @lb: the lower boundary of the capacity scope after transfer
 * @hb: the upper boundary of the capacity scope after transfer
*/
struct sprd_fgu_cap_remap_table {
	int cnt;
	int lcap;
	int hcap;
	int lb;
	int hb;
};

/*
 * struct sprd_fgu_data: describe the FGU device
 * @regmap: regmap for register access
 * @dev: platform device
 * @battery: battery power supply
 * @base: the base offset for the controller
 * @lock: protect the structure
 * @gpiod: GPIO for battery detection
 * @channel: IIO channel to get battery temperature
 * @charge_chan: IIO channel to get charge voltage
 * @internal_resist: the battery internal resistance in mOhm
 * @total_mah: the total capacity of the battery in mAh
 * @init_cap: the initial capacity of the battery in mAh
 * @alarm_cap: the alarm capacity
 * @normal_temp_cap: the normal temperature capacity
 * @max_volt_uv: the maximum constant input voltage in millivolt
 * @min_volt_uv: the minimum drained battery voltage in microvolt
 * @boot_volt_uv: the voltage measured during boot in microvolt
 * @table_len: the capacity table length
 * @temp_table_len: temp_table length
 * @cap_table_len：the capacity temperature table length
 * @resist_table_len: the resistance table length
 * @comp_resistance: the coulomb counter internal and the board ground resistance
 * @index: record temp_buff array index
 * @temp_buff: record the battery temperature for each measurement
 * @bat_temp: the battery temperature
 * @cap_table: capacity table with corresponding ocv
 * @temp_table: the NTC voltage table with corresponding battery temperature
 * @cap_temp_table: the capacity table with corresponding temperature
 * @resist_table: resistance percent table with corresponding temperature
 */
struct sprd_fgu_data {
	struct regmap *regmap;
	struct device *dev;
	struct power_supply *battery;
	u32 base;
	struct mutex lock;
	struct gpio_desc *gpiod;
	struct iio_channel *channel;
	struct iio_channel *charge_chan;
	bool bat_present;
	int internal_resist;
	int total_mah;
	int design_mah;
	int init_cap;
	int alarm_cap;
	int boot_cap;
	int normal_temp_cap;
	int bat_soc;
	int uusoc_mah;
	int init_mah;
	int cc_uah;
	int max_volt_uv;
	int min_volt_uv;
	int boot_volt_uv;
	int table_len;
	int temp_table_len;
	int cap_table_len;
	int resist_table_len;
	int cap_calib_dens_ocv_table_len;
	int first_calib_volt;
	int first_calib_cap;
	int uusoc_vbat;
	unsigned int comp_resistance;
	int batt_ovp_threshold;
	int index;
	int ocv_uv;
	int batt_mv;
	int temp_buff[SPRD_FGU_TEMP_BUFF_CNT];
	int cur_now_buff[SPRD_FGU_CURRENT_BUFF_CNT];
	bool dischg_trend[SPRD_FGU_DISCHG_CNT];
	int bat_temp;
	bool online;
	bool is_first_poweron;
	bool is_ovp;
	bool invalid_pocv;
	u32 chg_type;
	int cap_remap_total_cnt;
	int cap_remap_full_percent;
	int cap_remap_table_len;
	struct sprd_fgu_cap_remap_table *cap_remap_table;
	struct sprd_fgu_track_capacity track;
	struct power_supply_battery_ocv_table *cap_table;
	struct power_supply_vol_temp_table *temp_table;
	struct power_supply_capacity_temp_table *cap_temp_table;
	struct power_supply_resistance_temp_table *resist_table;
	struct usb_phy *usb_phy;
	struct notifier_block usb_notify;
	int chg_sts;
	struct sprd_fgu_debug_info debug_info;
	density_ocv_table *cap_calib_dens_ocv_table;

	struct sprd_fgu_sysfs *sysfs;
	struct delayed_work fgu_work;
	struct delayed_work cap_track_work;
	struct delayed_work cap_calculate_work;
	struct sprd_fgu_info *fgu_info;

	/* multi resistance */
	int *target_rbat_table;
	int **rbat_table;
	int rabat_table_len;
	int *rbat_temp_table;
	int rbat_temp_table_len;
	int *rbat_ocv_table;
	int rbat_ocv_table_len;
	bool support_multi_resistance;
	bool support_debug_log;

	/* boot capacity calibration */
	bool support_boot_calib;
	s64 shutdown_rtc_time;

	/* charge cycle */
	int charge_cycle;

	/* basp */
	bool support_basp;
	int basp_volt_uv;
	struct sprd_battery_ocv_table **basp_ocv_table;
	int basp_ocv_table_len;
	int *basp_full_design_table;
	int basp_full_design_table_len;
	int *basp_voltage_max_table;
	int basp_voltage_max_table_len;

	int work_enter_cc_uah;
	int work_exit_cc_uah;
	int last_cc_uah;
	s64 work_enter_times;
	s64 work_exit_times;

	/* sleep resume calibration */
	s64 awake_times;
	s64 sleep_times;
	s64 stop_charge_times;
	int sleep_cc_uah;
	int awake_cc_uah;
	int awake_avg_cur_ma;
	int sr_time_sleep[SPRD_FGU_SR_ARRAY_LEN];
	int sr_time_awake[SPRD_FGU_SR_ARRAY_LEN];
	int sr_index_sleep;
	int sr_index_awake;
	int sr_ocv_uv;
};

static bool is_charger_mode;
static bool allow_charger_enable;
static void sprd_fgu_capacity_calibration(struct sprd_fgu_data *data, bool int_mode);
static void sprd_fgu_discharging_calibration(struct sprd_fgu_data *data, int *cap);
static int sprd_fgu_resistance_algo(struct sprd_fgu_data *data, int cur_ua, int vol_uv);

static inline int sprd_fgu_uah2current(int uah, int times)
{
	/* To avoid data overflow, divide uah by 100 firstly */
	return DIV_ROUND_CLOSEST(uah * 36, times * 10);
}

static int sprd_fgu_ocv2cap(struct power_supply_battery_ocv_table *table,
			    int table_len, int ocv_uv)
{
	int i, cap;

	for (i = 0; i < table_len; i++) {
		if (ocv_uv > table[i].ocv)
			break;
	}

	if (i > 0 && i < table_len) {
		cap = interpolate(ocv_uv,
				  table[i].ocv,
				  table[i].capacity * 10,
				  table[i - 1].ocv,
				  table[i - 1].capacity * 10);
	} else if (i == 0) {
		cap = table[0].capacity * 10;
	} else {
		cap = table[table_len - 1].capacity * 10;
	}

	return cap;
}

static int sprd_fgu_cap2ocv(struct power_supply_battery_ocv_table *table,
			    int table_len, int cap)
{
	int i, ocv_uv;

	for (i = 0; i < table_len; i++) {
		if (cap > table[i].capacity * 10)
			break;
	}

	if (i > 0 && i < table_len) {
		ocv_uv = interpolate(cap,
				     table[i].capacity * 10,
				     table[i].ocv,
				     table[i - 1].capacity * 10,
				     table[i - 1].ocv);
	} else if (i == 0) {
		ocv_uv = table[0].ocv;
	} else {
		ocv_uv = table[table_len - 1].ocv;
	}

	return ocv_uv;
}

static int sprd_fgu_vol2temp(struct power_supply_vol_temp_table *table,
			     int table_len, int vol_uv)
{
	int i, temp;

	for (i = 0; i < table_len; i++) {
		if (vol_uv > table[i].vol)
			break;
	}

	if (i > 0 && i < table_len) {
		temp = interpolate(vol_uv,
				   table[i].vol,
				   table[i].temp,
				   table[i - 1].vol,
				   table[i - 1].temp);
	} else if (i == 0) {
		temp = table[0].temp;
	} else {
		temp = table[table_len - 1].temp;
	}

	return temp - 1000;
}

static int sprd_fgu_temp2resist_ratio(struct power_supply_resistance_temp_table *table,
				      int table_len, int bat_temp)
{
	int i, scale_ratio;

	for (i = 0; i < table_len; i++) {
		if (bat_temp > table[i].temp * 10)
			break;
	}

	if (i > 0 && i < table_len) {
		scale_ratio = interpolate(bat_temp,
					  table[i].temp * 10,
					  table[i].resistance,
					  table[i - 1].temp * 10,
					  table[i - 1].resistance);
	} else if (i == 0) {
		scale_ratio = table[0].resistance;
	} else {
		scale_ratio = table[table_len - 1].resistance;
	}

	return scale_ratio;
}

static int sprd_fgu_temp2cap(struct power_supply_capacity_temp_table *table,
			     int table_len, int temp)
{
	int i, capacity;

	for (i = 0; i < table_len; i++) {
		if (temp > table[i].temp * 10)
			break;
	}

	if (i > 0 && i < table_len) {
		capacity = interpolate(temp,
				       table[i].temp * 10,
				       table[i].cap * 10,
				       table[i - 1].temp * 10,
				       table[i - 1].cap * 10);
	} else if (i == 0) {
		capacity = table[0].cap * 10;
	} else {
		capacity = table[table_len - 1].cap * 10;
	}

	return capacity;
}

static void sprd_fgu_cap_remap_init_boundary(struct sprd_fgu_data *data, int index)
{

	if (index == 0) {
		data->cap_remap_table[index].lb = (data->cap_remap_table[index].lcap) * 1000;
		data->cap_remap_total_cnt = data->cap_remap_table[index].lcap;
	} else {
		data->cap_remap_table[index].lb = data->cap_remap_table[index - 1].hb +
			(data->cap_remap_table[index].lcap -
			 data->cap_remap_table[index - 1].hcap) * 1000;
		data->cap_remap_total_cnt += (data->cap_remap_table[index].lcap -
					      data->cap_remap_table[index - 1].hcap);
	}

	data->cap_remap_table[index].hb = data->cap_remap_table[index].lb +
		(data->cap_remap_table[index].hcap - data->cap_remap_table[index].lcap) *
		data->cap_remap_table[index].cnt * 1000;

	data->cap_remap_total_cnt +=
		(data->cap_remap_table[index].hcap - data->cap_remap_table[index].lcap) *
		data->cap_remap_table[index].cnt;

	dev_info(data->dev, "%s, cap_remap_table[%d].lb =%d,cap_remap_table[%d].hb = %d\n",
		 __func__, index, data->cap_remap_table[index].lb, index,
		 data->cap_remap_table[index].hb);
}

static int sprd_fgu_init_cap_remap_table(struct sprd_fgu_data *data)
{
	struct device_node *np = data->dev->of_node;
	const __be32 *list;
	int i, size;

	list = of_get_property(np, "fgu-cap-remap-table", &size);
	if (!list || !size) {
		dev_err(data->dev, "%s  get fgu-cap-remap-table fail\n", __func__);
		return 0;
	}
	data->cap_remap_table_len = (u32)size / (3 * sizeof(__be32));
	data->cap_remap_table = devm_kzalloc(data->dev, sizeof(struct sprd_fgu_cap_remap_table) *
				(data->cap_remap_table_len + 1), GFP_KERNEL);
	if (!data->cap_remap_table) {
		dev_err(data->dev, "%s, get cap_remap_table fail\n", __func__);
		return -ENOMEM;
	}
	for (i = 0; i < data->cap_remap_table_len; i++) {
		data->cap_remap_table[i].lcap = be32_to_cpu(*list++);
		data->cap_remap_table[i].hcap = be32_to_cpu(*list++);
		data->cap_remap_table[i].cnt = be32_to_cpu(*list++);

		sprd_fgu_cap_remap_init_boundary(data, i);

		dev_info(data->dev, "cap_remap_table[%d].lcap= %d,cap_remap_table[%d].hcap = %d,"
			 "cap_remap_table[%d].cnt= %d\n", i, data->cap_remap_table[i].lcap,
			 i, data->cap_remap_table[i].hcap, i, data->cap_remap_table[i].cnt);
	}

	if (data->cap_remap_table[data->cap_remap_table_len - 1].hcap != 100)
		data->cap_remap_total_cnt +=
			(100 - data->cap_remap_table[data->cap_remap_table_len - 1].hcap);

	dev_info(data->dev, "cap_remap_total_cnt =%d, cap_remap_table_len = %d\n",
		 data->cap_remap_total_cnt, data->cap_remap_table_len);

	return 0;
}

/*
 * sprd_fgu_capacity_remap - remap fuel_cap
 * Return the remapped cap
 */
static int sprd_fgu_capacity_remap(struct sprd_fgu_data *data, int fuel_cap)
{
	int i, temp, cap = 0;

	if (data->cap_remap_full_percent) {
		fuel_cap = fuel_cap * 100 / data->cap_remap_full_percent;
		if (fuel_cap > SPRD_FGU_FCC_PERCENT)
			fuel_cap  = SPRD_FGU_FCC_PERCENT;
	}

	if (!data->cap_remap_table)
		return fuel_cap;

	if (fuel_cap < 0) {
		fuel_cap = 0;
		return 0;
	} else if (fuel_cap >  SPRD_FGU_FCC_PERCENT) {
		fuel_cap  = SPRD_FGU_FCC_PERCENT;
		return fuel_cap;
	}

	temp = fuel_cap * data->cap_remap_total_cnt;

	for (i = 0; i < data->cap_remap_table_len; i++) {
		if (temp <= data->cap_remap_table[i].lb) {
			if (i == 0)
				cap = DIV_ROUND_CLOSEST(temp, 100);
			else
				cap = DIV_ROUND_CLOSEST((temp -
					data->cap_remap_table[i - 1].hb), 100) +
					data->cap_remap_table[i - 1].hcap * 10;
			break;
		} else if (temp <= data->cap_remap_table[i].hb) {
			cap = DIV_ROUND_CLOSEST((temp - data->cap_remap_table[i].lb),
						data->cap_remap_table[i].cnt * 100)
				+ data->cap_remap_table[i].lcap * 10;
			break;
		}

		if (i == data->cap_remap_table_len - 1 && temp > data->cap_remap_table[i].hb)
			cap = DIV_ROUND_CLOSEST((temp - data->cap_remap_table[i].hb), 100)
				+ data->cap_remap_table[i].hcap;

	}

	return cap;
}

static int sprd_fgu_get_boot_mode(struct sprd_fgu_data *data)
{
	struct device_node *cmdline_node;
	const char *cmd_line;
	char *match;
	char result[5];
	int ret;

	cmdline_node = of_find_node_by_path("/chosen");
	ret = of_property_read_string(cmdline_node, "bootargs", &cmd_line);
	if (ret)
		return ret;

	if (strncmp(cmd_line, "charger", strlen("charger")) == 0)
		is_charger_mode =  true;

	match = strstr(cmd_line, "androidboot.mode=");
	if (match) {
		memcpy(result, (match + strlen("androidboot.mode=")), sizeof(result) - 1);
		dev_info(data->dev, "result = %s\n", result);
		if ((!strcmp(result, "cali")) || (!strcmp(result, "auto")))
			allow_charger_enable = true;
	}

	dev_info(data->dev, "cmd_line = %s，allow_charger_enable = %d\n", cmd_line,
		 allow_charger_enable);

	return 0;
}

static int sprd_fgu_set_basp_volt(struct sprd_fgu_data *data, int max_volt_uv)
{
	int i, index;

	if (!data->support_basp || max_volt_uv == -1 || !data->basp_voltage_max_table ||
	    !data->basp_full_design_table || !data->basp_ocv_table)
		return 0;

	for (i = 0; i < data->basp_voltage_max_table_len; i++) {
		if (max_volt_uv >= data->basp_voltage_max_table[i])
			break;
	}

	if (i == data->basp_voltage_max_table_len)
		index = i - 1;
	else
		index = i;

	data->basp_volt_uv = data->basp_voltage_max_table[index];
	data->total_mah = data->basp_full_design_table[index]  / 1000;
	data->design_mah = data->total_mah;

	data->table_len = data->basp_ocv_table_len;
	data->cap_table = (struct power_supply_battery_ocv_table *)
		(data->basp_ocv_table[index]);

	dev_info(data->dev, "%s, basp_volt_uv = %d, basp_index = %d, max_volt_uv= %d,"
		 "total_mah = %d\n",
		 __func__, data->basp_volt_uv, index, max_volt_uv, data->total_mah);

	return 0;
}

static int sprd_fgu_parse_cmdline_match(struct sprd_fgu_data *data, char *match_str,
					char *result, int size)
{
	struct device_node *cmdline_node = NULL;
	const char *cmdline;
	char *match, *match_end;
	int len, match_str_len, ret;

	if (!result || !match_str)
		return -EINVAL;

	memset(result, '\0', size);
	match_str_len = strlen(match_str);

	cmdline_node = of_find_node_by_path("/chosen");
	ret = of_property_read_string(cmdline_node, "bootargs", &cmdline);
	if (ret) {
		dev_warn(data->dev, "%s failed to read bootargs\n", __func__);
		return -EINVAL;
	}

	match = strstr(cmdline, match_str);
	if (!match) {
		dev_warn(data->dev, "Match: %s fail in cmdline\n", match_str);
		return -EINVAL;
	}

	match_end = strstr((match + match_str_len), " ");
	if (!match_end) {
		dev_warn(data->dev, "Match end of : %s fail in cmdline\n", match_str);
		return -EINVAL;
	}

	len = match_end - (match + match_str_len);
	if (len < 0) {
		dev_warn(data->dev, "Match cmdline :%s fail, len = %d\n", match_str, len);
		return -EINVAL;
	}

	memcpy(result, (match + match_str_len), len);

	return 0;
}

static void sprd_fgu_parse_shutdown_rtc_time(struct sprd_fgu_data *data)
{
	char result[32] = {};
	int ret;
	char *str;

	str = "charge.shutdown_rtc_time=";
	data->shutdown_rtc_time = -1;
	ret = sprd_fgu_parse_cmdline_match(data, str, result, sizeof(result));
	if (!ret) {
		ret = kstrtoll(result, 10, &data->shutdown_rtc_time);
		if (ret) {
			data->shutdown_rtc_time = -1;
			dev_err(data->dev, "Covert shutdown_rtc_time fail, ret = %d, result = %s",
				ret, result);
		}
	}
}

static void sprd_fgu_parse_charge_cycle(struct sprd_fgu_data *data)
{
	char result[32] = {};
	int ret;
	char *str;

	str = "charge.charge_cycle=";
	data->charge_cycle = -1;
	ret = sprd_fgu_parse_cmdline_match(data, str, result, sizeof(result));
	if (!ret) {
		ret = kstrtoint(result, 10, &data->charge_cycle);
		if (ret) {
			data->charge_cycle = -1;
			dev_err(data->dev, "Covert charge_cycle fail, ret = %d, result = %s\n",
				ret, result);
		}
	}
}

static void sprd_fgu_parse_basp(struct sprd_fgu_data *data)
{
	char result[32] = {};
	int ret;
	char *str;

	str = "charge.basp=";
	data->basp_volt_uv = -1;
	ret = sprd_fgu_parse_cmdline_match(data, str, result, sizeof(result));
	if (!ret) {
		ret = kstrtoint(result, 10, &data->basp_volt_uv);
		if (ret) {
			data->basp_volt_uv = -1;
			dev_err(data->dev, "Covert basp fail, ret = %d, result = %s\n",
				ret, result);
		}

		ret = sprd_fgu_set_basp_volt(data, data->basp_volt_uv);
		if (ret)
			dev_err(data->dev, "Fail to set basp volt\n");
	}
}

static void sprd_fgu_parse_learned_mah(struct sprd_fgu_data *data)
{
	char result[32] = {};
	int ret;
	char *str;

	str = "charge.total_mah=";
	data->track.learned_mah = -1;
	ret = sprd_fgu_parse_cmdline_match(data, str, result, sizeof(result));
	if (!ret) {
		ret = kstrtoint(result, 10, &data->track.learned_mah);
		if (ret) {
			data->track.learned_mah = -1;
			dev_err(data->dev, "Covert learned_mah fail, ret = %d, result = %s\n",
				ret, result);
		}
	}
}

static void sprd_fgu_parse_cmdline(struct sprd_fgu_data *data)
{
	/* parse shutdown rtc time */
	if (data->support_boot_calib)
		sprd_fgu_parse_shutdown_rtc_time(data);

	/* parse charge cycle */
	sprd_fgu_parse_charge_cycle(data);

	/* parse basp */
	if (data->support_basp)
		sprd_fgu_parse_basp(data);

	/* parse learned total mah */
	if (data->track.cap_tracking)
		sprd_fgu_parse_learned_mah(data);

	dev_info(data->dev, "shutdown_rtc_time = %lld, charge_cycle = %d, basp = %d, "
		 "learned_mah = %d\n",
		 data->shutdown_rtc_time, data->charge_cycle, data->basp_volt_uv,
		 data->track.learned_mah);
}

static int sprd_fgu_get_rtc_time(struct sprd_fgu_data *data, s64 *time)
{
	struct rtc_time tm;
	struct rtc_device *rtc;
	int ret;

	rtc = alarmtimer_get_rtcdev();
	if (!rtc) {
		dev_err(data->dev, "NO RTC dev!!!\n");
		return -EINVAL;
	}

	ret = rtc_read_time(rtc, &tm);
	if (ret) {
		dev_err(data->dev, "failed to read rtc time, ret = %d\n", ret);
		return ret;
	}

	*time = rtc_tm_to_time64(&tm);

	return 0;
}

static void sprd_fgu_capacity_loss_by_temperature(struct sprd_fgu_data *data, int *cap)
{
	int temp_cap, ret;
	struct sprd_fgu_info *fgu_info = data->fgu_info;

	if (data->cap_table_len > 0) {
		temp_cap = sprd_fgu_temp2cap(data->cap_temp_table,
					     data->cap_table_len, data->bat_temp);
		/*
		 * Battery capacity at different temperatures, we think
		 * the change is linear, the follow the formula: y = ax + k
		 *
		 * for example: display 100% at 25 degrees need to display
		 * 100% at -10 degrees, display 10% at 25 degrees need to
		 * display 0% at -10 degrees, substituting the above special
		 * points will deduced follow formula.
		 * formula 1:
		 * Capacity_Delta = 100 - Capacity_Percentage(T1)
		 * formula 2:
		 * Capacity_temp = (Capacity_Percentage(current) -
		 * Capacity_Delta) * 100 /(100 - Capacity_Delta)
		 */
		*cap = DIV_ROUND_CLOSEST((*cap + temp_cap - 1000) * 1000, temp_cap);
		if (*cap < 0) {
			dev_info(data->dev, "%s Capacity_temp < 0, adjust !!!\n", __func__);
			*cap = 0;
		} else if (*cap > SPRD_FGU_FCC_PERCENT) {
			dev_info(data->dev, "%s Capacity_temp > 1000, adjust !!!\n", __func__);
			*cap = SPRD_FGU_FCC_PERCENT;
		}

		if (*cap <= 5) {
			ret =  fgu_info->ops->get_vbat_now(fgu_info, &data->batt_mv);
			if (ret) {
				dev_err(data->dev, "get battery vol error.\n");
				return;
			}

			if (data->batt_mv > SPRD_FGU_LOW_VBAT_REGION)
				*cap = 5;
		}
	}
}

/* @val: value of battery ocv in mV*/
static int sprd_fgu_get_vbat_ocv(struct sprd_fgu_data *data, int *val)
{
	int vol_mv, cur_ma, resistance, scale_ratio, ret;
	struct sprd_fgu_info *fgu_info = data->fgu_info;

	ret = fgu_info->ops->get_vbat_now(fgu_info, &vol_mv);
	if (ret)
		return ret;

	ret = fgu_info->ops->get_current_now(fgu_info, &cur_ma);
	if (ret)
		return ret;

	if (data->support_multi_resistance) {
		resistance = sprd_fgu_resistance_algo(data, cur_ma * 1000, vol_mv * 1000);
		goto resistance_algo;
	}

	resistance = data->internal_resist;
	if (data->resist_table_len > 0) {
		scale_ratio = sprd_fgu_temp2resist_ratio(data->resist_table,
							 data->resist_table_len,
							 data->bat_temp);
		resistance = data->internal_resist * scale_ratio / 100;
	}

resistance_algo:
	*val = vol_mv - cur_ma * (resistance + SPRD_FGU_RBAT_CMP_MOH) / 1000;

	return 0;
}

static int sprd_fgu_get_basp_volt(struct sprd_fgu_data *data, int *max_volt_uv)
{
	int ret = 0;

	*max_volt_uv = data->basp_volt_uv;

	return ret;
}

static void sprd_fgu_dump_battery_info(struct sprd_fgu_data *data, char *str)
{
	int i, j;

	dev_info(data->dev, "%s, ocv_table_len = %d, temp_table_len = %d, rabat_table_len = %d, "
		 "basp_ocv_table_len = %d, basp_full_design_table_len = %d, "
		 "basp_voltage_max_table_len = %d, track.end_vol = %d, track.end_cur = %d, "
		 "first_calib_volt = %d, first_calib_cap = %d, total_mah = %d, max_volt_uv = %d, "
		 "internal_resist = %d, min_volt_uv = %d\n",
		 str, data->rbat_ocv_table_len, data->rbat_temp_table_len,
		 data->rabat_table_len, data->basp_ocv_table_len,
		 data->basp_full_design_table_len, data->basp_voltage_max_table_len,
		 data->track.end_vol, data->track.end_cur, data->first_calib_volt,
		 data->first_calib_cap, data->total_mah, data->max_volt_uv,
		 data->internal_resist, data->min_volt_uv);

	if (data->rbat_temp_table_len > 0) {
		for (i = 0; i < data->rbat_temp_table_len; i++)
			dev_info(data->dev, "%s, internal_resistance_temp[%d] = %d\n",
				 str, i, data->rbat_temp_table[i]);
	}

	if (data->rbat_ocv_table_len > 0) {
		for (i = 0; i < data->rbat_ocv_table_len; i++)
			dev_info(data->dev, "%s, battery_internal_resistance_ocv_table[%d] = %d\n",
				 str, i, data->rbat_ocv_table[i]);
	}

	for (i = 0; i < data->rbat_temp_table_len; i++) {
		for (j = 0; j < data->rabat_table_len; j++)
			dev_info(data->dev, "%s, resistance_table[%d][%d] = %d\n",
				 str, i, j, data->rbat_table[i][j]);
	}

	if (data->target_rbat_table) {
		for (i = 0; i < data->rabat_table_len; i++)
			dev_info(data->dev, "%s, target_rbat_table[%d] = %d\n",
				 str, i, data->target_rbat_table[i]);
	}

	if (data->basp_full_design_table) {
		for (i = 0; i < data->basp_full_design_table_len; i++)
			dev_info(data->dev, "%s, basp_full_design_table[%d] = %d\n",
				 str, i, data->basp_full_design_table[i]);
	}

	if (data->basp_voltage_max_table) {
		for (i = 0; i < data->basp_voltage_max_table_len; i++)
			dev_info(data->dev, "%s, basp_voltage_max_table[%d] = %d\n",
				 str, i, data->basp_voltage_max_table[i]);
	}

	if (data->basp_ocv_table) {
		for (i = 0; i < data->basp_voltage_max_table_len; i++) {
			for (j = 0; j < data->basp_ocv_table_len; j++)
				dev_info(data->dev, "%s, basp_ocv_table[%d][%d] = (%d, %d)\n",
					 str, i, j, data->basp_ocv_table[i][j].ocv,
					 data->basp_ocv_table[i][j].capacity);
		}
	}

	if (data->cap_table) {
		for (i = 0; i < data->table_len; i++)
			dev_info(data->dev, "cap_table[%d].ocv = %d, cap_table[%d].cap = %d\n",
				 i, data->cap_table[i].ocv, i, data->cap_table[i].capacity);
	}

}

static void sprd_fgu_dump_info(struct sprd_fgu_data *data)
{
	struct sprd_fgu_info *fgu_info = data->fgu_info;

	fgu_info->ops->dump_fgu_info(fgu_info, DUMP_FGU_INFO_LEVEL_1);

	dev_info(data->dev, "init_cap = %d, init_mah = %d, normal_cap = %d, data->cc_mah = %d, "
		 "Tbat = %d, uusoc_vbat = %d, uusoc_mah = %d, track_sts = %d\n",
		 data->init_cap, data->init_mah, data->normal_temp_cap, data->cc_uah / 1000,
		 data->bat_temp, data->uusoc_vbat, data->uusoc_mah, data->track.state);
}

static bool sprd_fgu_is_in_low_energy_dens(struct sprd_fgu_data *data, int ocv_uv,
					   density_ocv_table *table, int len)
{
	bool is_matched = false;
	int i;

	if (len == 0) {
		dev_warn(data->dev, "energy density ocv table len is 0 !!!!\n");
		return is_matched;
	}

	for (i = 0; i < len; i++) {
		if (ocv_uv > table[i].engy_dens_ocv_lo &&
		    ocv_uv < table[i].engy_dens_ocv_hi) {
			dev_info(data->dev, "low ernergy dens matched, vol = %d\n", ocv_uv);
			is_matched = true;
			break;
		}
	}

	if (!is_matched)
		dev_info(data->dev, "ocv_uv[%d] is out of dens range\n", ocv_uv);

	return is_matched;
}

static void sprd_fgu_calc_charge_cycle(struct sprd_fgu_data *data, int cap, int *fgu_cap)
{
	int delta_cap;

	if (*fgu_cap == SPRD_FGU_MAGIC_NUMBER)
		*fgu_cap = cap;

	delta_cap = cap - *fgu_cap;

	if (data->support_debug_log)
		dev_info(data->dev, "%s: delta_cap = %d, fgu_cap = %d, cap = %d\n",
			 __func__, delta_cap, *fgu_cap, cap);

	*fgu_cap = cap;

	/*
	 * Formula:
	 * charge_cycle(0.001 cycle) = accumulate_cap  * 1000 /  SPRD_FGU_FCC_PERCENT
	 */
	if (delta_cap > 0)
		data->charge_cycle += delta_cap * 1000 / SPRD_FGU_FCC_PERCENT;
}

static int sprd_fgu_get_boot_voltage(struct sprd_fgu_data *data, int *pocv_uv)
{
	int vol_mv, oci_ma, ret, ocv_mv, fgu_sts;
	struct sprd_fgu_info *fgu_info = data->fgu_info;

	ret = fgu_info->ops->get_poci(fgu_info, &oci_ma);
	if (ret) {
		dev_err(data->dev, "Failed to get poci, ret = %d\n", ret);
		return ret;
	}

	ret = fgu_info->ops->get_pocv(fgu_info, &vol_mv);
	if (ret) {
		dev_err(data->dev, "Failed to get pocv, ret = %d\n", ret);
		return ret;
	}

	*pocv_uv = vol_mv * 1000 - oci_ma * data->internal_resist;

	ret = fgu_info->ops->get_fgu_sts(fgu_info, SPRD_FGU_INVALID_POCV_STS_CMD, &fgu_sts);
	if (ret)
		return ret;

	data->invalid_pocv = !!fgu_sts;
	if (vol_mv < SPRD_FGU_POCV_VOLT_THRESHOLD || data->invalid_pocv) {
		dev_info(data->dev, "pocv is %s\n", data->invalid_pocv ? "invalid" : "valid");
		ret = sprd_fgu_get_vbat_ocv(data, &ocv_mv);
		if (ret) {
			dev_err(data->dev, "Failed to read volt, ret = %d\n", ret);
			return ret;
		}
		*pocv_uv = ocv_mv * 1000;
	}
	dev_info(data->dev, "oci_ma = %d, vol_mv = %d, pocv = %d\n", oci_ma, vol_mv, *pocv_uv);

	return 0;
}

static void sprd_fgu_boot_cap_calibration(struct sprd_fgu_data *data,
					  int pocv_cap, int pocv_uv, int *cap)
{
	s64 cur_time, shutdown_time;
	int ret;

	if (!data->support_boot_calib) {
		dev_warn(data->dev, "Boot calib: not support boot calibration !!!!\n");
		return;
	}

	if (data->shutdown_rtc_time == 0 || data->shutdown_rtc_time == -1 ||
	    data->shutdown_rtc_time < SPRD_FGU_MISCDATA_RTC_TIME) {
		dev_err(data->dev, "Boot calib: shutdown_rtc_time = %lld not meet\n",
			data->shutdown_rtc_time);
		return;
	}

	if (!sprd_fgu_is_in_low_energy_dens(data, pocv_uv, data->cap_calib_dens_ocv_table,
					    data->cap_calib_dens_ocv_table_len)) {
		dev_warn(data->dev, "Boot calib: pocv_uv is not in low energy dens !!!!\n");
		return;
	}

	if (data->bat_temp < SPRD_FGU_CAP_CALIB_TEMP_LOW ||
		data->bat_temp > SPRD_FGU_CAP_CALIB_TEMP_HI) {
		dev_err(data->dev, "Boot calib: temp = %d out range\n", data->bat_temp);
		return;
	}

	ret = sprd_fgu_get_rtc_time(data, &cur_time);
	if (ret)
		return;

	if (cur_time < SPRD_FGU_MISCDATA_RTC_TIME) {
		dev_err(data->dev, "Boot calib: current rtc time = %lld less than %d\n",
			cur_time, SPRD_FGU_MISCDATA_RTC_TIME);
		return;
	}

	shutdown_time = cur_time - data->shutdown_rtc_time;
	if (shutdown_time < SPRD_FGU_SHUTDOWN_TIME) {
		dev_err(data->dev, "Boot calib: shutdown time = %lld not meet\n", shutdown_time);
		return;
	}

	data->track.pocv_info.valid = true;
	data->track.pocv_info.ocv_uv = pocv_uv;
	data->track.pocv_info.ocv_rtc_time = cur_time;


	dev_info(data->dev, "Boot calib: pocv_cap = %d, *cap = %d\n", pocv_cap, *cap);

	if (pocv_cap > *cap + 30)
		*cap += (pocv_cap - *cap - 30);
	else if (pocv_cap < *cap - 30)
		*cap -= (*cap - pocv_cap - 30);
}

/*
 * When system boots on, we can not read battery capacity from coulomb
 * registers, since now the coulomb registers are invalid. So we should
 * calculate the battery open circuit voltage, and get current battery
 * capacity according to the capacity table.
 */
static int sprd_fgu_get_boot_capacity(struct sprd_fgu_data *data, int *cap)
{
	int pocv_uv, ret, pocv_cap;
	struct sprd_fgu_info *fgu_info = data->fgu_info;
	bool is_first_poweron = fgu_info->ops->is_first_poweron(fgu_info);

	ret = sprd_fgu_get_boot_voltage(data, &pocv_uv);
	if (ret) {
		dev_err(data->dev, "Failed to get boot voltage, ret = %d\n", ret);
		return ret;
	}
	data->boot_volt_uv = pocv_uv;

	/*
	 * Parse the capacity table to look up the correct capacity percent
	 * according to current battery's corresponding OCV values.
	 */
	pocv_cap = sprd_fgu_ocv2cap(data->cap_table, data->table_len, pocv_uv);

	/*
	 * If system is not the first power on, we should use the last saved
	 * battery capacity as the initial battery capacity. Otherwise we should
	 * re-calculate the initial battery capacity.
	 */
	if (!is_first_poweron) {
		ret = fgu_info->ops->read_last_cap(fgu_info, cap);
		if (ret) {
			dev_err(data->dev, "Failed to read last cap, ret = %d\n", ret);
			return ret;
		}

		data->boot_cap = *cap;
		ret = fgu_info->ops->read_normal_temperature_cap(fgu_info, cap);
		if (ret) {
			dev_err(data->dev, "Failed to read normal temperature cap, ret = %d\n", ret);
			sprd_fgu_boot_cap_calibration(data, pocv_cap, pocv_uv, cap);
			return ret;
		}

		if (*cap == SPRD_FGU_DEFAULT_CAP || *cap == SPRD_FGU_RTC2_RESET_VALUE) {
			*cap = data->boot_cap;
			sprd_fgu_boot_cap_calibration(data, pocv_cap, pocv_uv, cap);
			ret = fgu_info->ops->save_normal_temperature_cap(fgu_info, *cap);
			if (ret < 0)
				dev_err(data->dev, "Failed to initialize fgu user area status1 register\n");
		} else {
			sprd_fgu_boot_cap_calibration(data, pocv_cap, pocv_uv, cap);
		}

		data->normal_temp_cap = *cap;
		ret = fgu_info->ops->save_normal_temperature_cap(fgu_info, data->normal_temp_cap);
		if (ret) {
			dev_err(data->dev, "Failed to save normal temperature capacity, ret = %d\n", ret);
			return ret;
		}

		dev_info(data->dev, "init: boot_cap = %d, normal_cap = %d\n", data->boot_cap, *cap);

		return fgu_info->ops->save_boot_mode(fgu_info, SPRD_FGU_NORMAL_POWERON);
	}

	*cap = pocv_cap;
	sprd_fgu_capacity_loss_by_temperature(data, cap);
	data->boot_cap = sprd_fgu_capacity_remap(data, *cap);
	ret = fgu_info->ops->save_last_cap(fgu_info, data->boot_cap);
	if (ret) {
		dev_err(data->dev, "Failed to save last cap, ret = %d\n", ret);
		return ret;
	}

	data->normal_temp_cap = pocv_cap;
	ret = fgu_info->ops->save_normal_temperature_cap(fgu_info, data->normal_temp_cap);
	if (ret) {
		dev_err(data->dev, "Failed to save normal temperature capacity, ret = %d\n", ret);
		return ret;
	}

	data->is_first_poweron = true;
	dev_info(data->dev, "First_poweron: pocv_uv = %d, pocv_cap = %d, "
		 "boot_cap = %d\n", pocv_uv, pocv_cap, data->boot_cap);
	*cap = pocv_cap;
	return fgu_info->ops->save_boot_mode(fgu_info, SPRD_FGU_NORMAL_POWERON);
}

static int sprd_fgu_uusoc_algo(struct sprd_fgu_data *data, int *uusoc_mah)
{
	int vol_mv, cur_ma, ret, cur_avg_ma;
	int resistance_moh = 0, ocv_pzero_uv;
	int ocv_pzero_cap, ocv_pzero_mah;
	struct sprd_fgu_info *fgu_info = data->fgu_info;

	ret = fgu_info->ops->get_vbat_now(fgu_info, &vol_mv);
	if (ret) {
		dev_info(data->dev, "UUSOC fail to get vbat, ret = %d\n", ret);
		return ret;
	}

	ret = fgu_info->ops->get_current_now(fgu_info, &cur_ma);
	if (ret) {
		dev_info(data->dev, "UUSOC fail to get cur_ma, ret = %d\n", ret);
		return ret;
	}

	ret = fgu_info->ops->get_current_avg(fgu_info, &cur_avg_ma);
	if (ret) {
		dev_info(data->dev, "UUSOC fail to get cur_avg_ma, ret = %d\n", ret);
		return ret;
	}

	ocv_pzero_uv = data->cap_table[data->table_len - 1].ocv;
	if (cur_avg_ma < 0) {
		resistance_moh = sprd_fgu_resistance_algo(data, cur_ma * 1000, vol_mv * 1000);
		ocv_pzero_uv -= cur_avg_ma * resistance_moh;
	}

	ocv_pzero_cap = sprd_fgu_ocv2cap(data->cap_table, data->table_len, ocv_pzero_uv);

	ocv_pzero_mah = fgu_info->ops->cap2mah(fgu_info, data->total_mah, ocv_pzero_cap);
	*uusoc_mah = ocv_pzero_mah;

	dev_info(data->dev, "UUSOC: cur_avg_ma = %d, resistance_moh = %d, ocv_pzero_uv = %d, "
		 "ocv_pzero_cap = %d, ocv_pzero_mah = %d\n",
		 cur_avg_ma, resistance_moh, ocv_pzero_uv, ocv_pzero_cap, ocv_pzero_mah);

	return 0;
}

static int sprd_fgu_get_capacity(struct sprd_fgu_data *data, int *cap)
{
	int ret, delta_cap;
	static int last_fgu_cap = SPRD_FGU_MAGIC_NUMBER;
	struct sprd_fgu_info *fgu_info = data->fgu_info;

	ret = fgu_info->ops->get_cc_uah(fgu_info, &data->cc_uah, true);
	if (ret) {
		dev_err(data->dev, "failed to get cc uah!\n");
		return ret;
	}

	/*
	 * If convert to capacity percent of the battery total capacity,
	 * you need to divide by 10.
	 */
	delta_cap = DIV_ROUND_CLOSEST(data->cc_uah, data->total_mah);
	*cap = delta_cap + data->init_cap;

	sprd_fgu_calc_charge_cycle(data, *cap, &last_fgu_cap);

	data->normal_temp_cap = *cap;
	if (data->normal_temp_cap < 0)
		data->normal_temp_cap = 0;
	else if (data->normal_temp_cap > 1000)
		data->normal_temp_cap = 1000;

	if (*cap < 0) {
		*cap = 0;
		dev_err(data->dev, "ERORR: normal_cap is < 0, adjust!!!\n");
		sprd_fgu_dump_info(data);
		data->uusoc_vbat = 0;
		data->init_cap = fgu_info->ops->adjust_cap(fgu_info, 0);
		return 0;
	} else if (*cap > SPRD_FGU_FCC_PERCENT) {
		dev_info(data->dev, "normal_cap is > 1000, adjust !!!\n");
		sprd_fgu_dump_info(data);
		*cap = SPRD_FGU_FCC_PERCENT;
		data->init_cap = fgu_info->ops->adjust_cap(fgu_info, SPRD_FGU_FCC_PERCENT);
		return 0;
	}

	if (data->support_multi_resistance) {
		ret = sprd_fgu_uusoc_algo(data, &data->uusoc_mah);
		if (ret) {
			dev_info(data->dev, "Fail to get uusoc, ret = %d\n", ret);
			goto normal_cap_calc;
		}

		data->init_mah = fgu_info->ops->cap2mah(fgu_info, data->total_mah, data->init_cap);
		*cap = DIV_ROUND_CLOSEST((data->init_mah - data->uusoc_mah) * 1000 + data->cc_uah,
					 (data->total_mah - data->uusoc_mah));

		if (*cap < 0) {
			dev_info(data->dev, "UUSOC *cap < 0, adjust !!!\n");
			sprd_fgu_dump_info(data);
			*cap = 0;
		} else if (*cap > SPRD_FGU_FCC_PERCENT) {
			dev_info(data->dev, "UUSOC *cap > 1000, adjust !!!\n");
			sprd_fgu_dump_info(data);
			*cap = SPRD_FGU_FCC_PERCENT;
		}

		goto capacity_calibration;
	}

normal_cap_calc:
	sprd_fgu_capacity_loss_by_temperature(data, cap);
capacity_calibration:
	sprd_fgu_capacity_calibration(data, false);

	*cap -= data->uusoc_vbat;
	if (*cap < 0) {
		dev_info(data->dev, "Capacity_temp < 0, adjust !!!\n");
		*cap = 0;
	} else if (*cap > SPRD_FGU_FCC_PERCENT) {
		dev_info(data->dev, "Capacity_temp > 1000, adjust !!!\n");
		*cap = SPRD_FGU_FCC_PERCENT;
	}

	sprd_fgu_discharging_calibration(data, cap);

	return 0;
}

static int sprd_fgu_resistance_algo_init_resistance(struct sprd_fgu_data *data, int *rbat_table)
{
	int i, min;

	if (!rbat_table)
		return 0;

	min = rbat_table[0];
	for (i = 1; i < data->rabat_table_len; i++) {
		if (min >  rbat_table[i])
			min = rbat_table[i];
	}

	return min;
}

static void sprd_fgu_find_resistance_by_ocv(struct sprd_fgu_data *data,
					    int *rbat_table, int ocv, int *rbat)
{
	int i, delta_rbat;

	for (i = 0; i < data->rbat_ocv_table_len; i++) {
		if (ocv > data->rbat_ocv_table[i])
			break;
	}

	if (i == data->rbat_ocv_table_len) {
		*rbat = rbat_table[data->rbat_ocv_table_len - 1];
		return;
	} else if (i == 0) {
		*rbat = rbat_table[0];
		return;
	}

	delta_rbat =
		DIV_ROUND_CLOSEST((rbat_table[i] - rbat_table[i - 1]) *
				  (ocv - data->rbat_ocv_table[i - 1]),
				  (data->rbat_ocv_table[i] - data->rbat_ocv_table[i - 1]));
	*rbat =  rbat_table[i - 1] + delta_rbat;

	if (data->support_debug_log)
		dev_info(data->dev, "%s: i = %d, ocv = %d, rbat_table[%d]= %d, rbat_table[%d]= %d, "
			 "ocv_table[%d] = %d, cv_table[%d] = %d, delta_rbat = %d, rbat = %d\n",
			 __func__, i, ocv, i, rbat_table[i], i - 1, rbat_table[i - 1],
			 i, data->rbat_ocv_table[i], i - 1, data->rbat_ocv_table[i - 1],
			 delta_rbat, *rbat);

}

static bool sprd_fgu_is_resistance_valid(struct sprd_fgu_data *data, int *table, int len)
{
	int sum = 0, min = 0, max = 0, avg = 0, i;
	bool is_valid = true;

	if (!table)
		return false;

	min = max = table[0];
	for (i = 0; i < len; i++) {
		if (table[i] == 0)
			return false;

		if (min > table[i])
			min =  table[i];
		if (max < table[i])
			max = table[i];

		sum += table[i];
	}

	avg = DIV_ROUND_CLOSEST(sum, len);

	if (data->support_debug_log)
		dev_info(data->dev, "%s, avg = %d, min = %d, max = %d\n",
			 __func__, avg, min, max);

	if (max - min > SPRD_FGU_RESIST_ALG_OCV_GAP_UV)
		return false;

	for (i = 0; i < len; i++) {
		if ((abs(avg - table[i]) > (SPRD_FGU_RESIST_ALG_OCV_GAP_UV / 2))) {
			is_valid = false;
			break;
		}
	}

	return is_valid;
}

static int sprd_fgu_calc_ocv(int vol_uv, int cur_ua, int rbat_moh, int rbat_cmp_moh)
{
	return (vol_uv - (cur_ua / 1000) * (rbat_moh + rbat_cmp_moh));
}

static int sprd_fgu_resistance_algo(struct sprd_fgu_data *data, int cur_ua, int vol_uv)
{
	int ocv_uv = vol_uv;
	int resistance_moh, rbat_cmp_moh = SPRD_FGU_RBAT_CMP_MOH;
	int *rbat_table;
	int i, j, sum = 0, cnt = 0;
	int resistance_moh_tab[SPRD_FGU_RESIST_ALG_REIST_CNT] = {0};
	int ocv_tab[SPRD_FGU_RESIST_ALG_OCV_CNT] = {0};
	bool resistance_valid = false;

	sprd_battery_find_resistance_table(data->battery,
					   data->rbat_table,
					   data->rabat_table_len,
					   data->rbat_temp_table,
					   data->rbat_temp_table_len,
					   data->bat_temp,
					   data->target_rbat_table);

	rbat_table = data->target_rbat_table;

	resistance_moh = sprd_fgu_resistance_algo_init_resistance(data, rbat_table);
	ocv_uv = sprd_fgu_calc_ocv(vol_uv, cur_ua, resistance_moh, rbat_cmp_moh);

	if (data->support_debug_log)
		dev_info(data->dev, "%s, vol_uv = %d, cur = %d, init ocv = %d, "
			 "init resistance_moh = %d, rcmp = %d\n",
			 __func__, vol_uv, cur_ua, ocv_uv, resistance_moh, rbat_cmp_moh);

	for (i = 0; i < SPRD_FGU_RESIST_ALG_REIST_CNT; i++) {
		ocv_uv = sprd_fgu_calc_ocv(vol_uv, cur_ua, resistance_moh, rbat_cmp_moh);
		ocv_tab[(i % SPRD_FGU_RESIST_ALG_OCV_CNT)] = ocv_uv;

		sprd_fgu_find_resistance_by_ocv(data, rbat_table, ocv_uv, &resistance_moh);
		resistance_moh_tab[i] = resistance_moh;

		resistance_valid = sprd_fgu_is_resistance_valid(data, ocv_tab,
								SPRD_FGU_RESIST_ALG_OCV_CNT);
		if (resistance_valid)
			break;
	}

	if (!resistance_valid)
		i = SPRD_FGU_RESIST_ALG_REIST_CNT - 1;

	for (j = i; j >= 0; j--) {
		cnt++;
		sum += resistance_moh_tab[j];
		if (cnt >= SPRD_FGU_RESIST_ALG_OCV_CNT)
			break;
	}

	resistance_moh = DIV_ROUND_CLOSEST(sum, SPRD_FGU_RESIST_ALG_OCV_CNT);

	if (data->support_debug_log)
		dev_info(data->dev, "Get resistance_moh =  %d, resistance_valid = %d\n",
			 resistance_moh, resistance_valid);

	return resistance_moh;

}

static int sprd_fgu_get_charge_vol(struct sprd_fgu_data *data, int *val)
{
	int ret, vol_mv;

	ret = iio_read_channel_processed(data->charge_chan, &vol_mv);
	if (ret < 0)
		return ret;

	*val = vol_mv;
	return 0;
}

static int sprd_fgu_get_average_temp(struct sprd_fgu_data *data, int temp)
{
	int i, min, max;
	int sum = 0;

	if (data->temp_buff[0] == -500) {
		for (i = 0; i < SPRD_FGU_TEMP_BUFF_CNT; i++)
			data->temp_buff[i] = temp;
	}

	if (data->index >= SPRD_FGU_TEMP_BUFF_CNT)
		data->index = 0;

	data->temp_buff[data->index++] = temp;
	min = max = data->temp_buff[0];

	for (i = 0; i < SPRD_FGU_TEMP_BUFF_CNT; i++) {
		if (data->temp_buff[i] > max)
			max = data->temp_buff[i];

		if (data->temp_buff[i] < min)
			min = data->temp_buff[i];

		sum += data->temp_buff[i];
	}

	sum = sum - max - min;

	return sum / (SPRD_FGU_TEMP_BUFF_CNT - 2);
}

static int sprd_fgu_get_temp(struct sprd_fgu_data *data, int *temp)
{
	int vol_ntc_uv, vol_adc_mv, ret;
	struct sprd_fgu_info *fgu_info = data->fgu_info;

	ret = iio_read_channel_processed(data->channel, &vol_adc_mv);
	if (ret < 0)
		return ret;

	vol_ntc_uv = vol_adc_mv * 1000;
	if (data->comp_resistance) {
		int bat_current_ma, resistance_vol, calib_resistance_vol, temp_vol;

		ret = fgu_info->ops->get_current_now(fgu_info, &bat_current_ma);
		if (ret) {
			dev_err(data->dev, "failed to get battery current\n");
			return ret;
		}

		resistance_vol = bat_current_ma * data->comp_resistance;
		resistance_vol = DIV_ROUND_CLOSEST(resistance_vol, 10);
		calib_resistance_vol = bat_current_ma * (fgu_info->calib_resist / 10);
		calib_resistance_vol =
			DIV_ROUND_CLOSEST(calib_resistance_vol, 1000) + resistance_vol;

		temp_vol = (vol_ntc_uv / 10 - resistance_vol) * calib_resistance_vol;
		temp_vol = DIV_ROUND_CLOSEST(temp_vol, (187500 - calib_resistance_vol));

		vol_ntc_uv = temp_vol * 10 + vol_ntc_uv - resistance_vol * 10;

		dev_info(data->dev, "bat_current_ma = %d, vol_adc_mv = %d, vol_ntc_uv = %d\n",
			 bat_current_ma, vol_adc_mv, vol_ntc_uv);
		if (vol_ntc_uv < 0)
			vol_ntc_uv = 0;
	}

	if (data->temp_table_len > 0) {
		*temp = sprd_fgu_vol2temp(data->temp_table, data->temp_table_len, vol_ntc_uv);
		dev_info(data->dev, "%s: temp = %d\n", __func__, *temp);
		*temp = sprd_fgu_get_average_temp(data, *temp);
	} else {
		*temp = 200;
	}

	data->bat_temp = *temp;

	return 0;
}

static void sprd_fgu_get_health(struct sprd_fgu_data *data, int *health)
{
	if (data->is_ovp)
		*health = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
	else
		*health = POWER_SUPPLY_HEALTH_GOOD;
}

static int sprd_fgu_suspend_calib_check_chg_sts(struct sprd_fgu_data *data)
{
	int ret = -EINVAL;

	if (data->chg_sts != POWER_SUPPLY_STATUS_NOT_CHARGING &&
	    data->chg_sts != POWER_SUPPLY_STATUS_DISCHARGING) {
		dev_info(data->dev, "Suspend calib charging status = %d, not meet conditions\n",
			 data->chg_sts);
		return ret;
	}

	return 0;
}

static int sprd_fgu_suspend_calib_check_temp(struct sprd_fgu_data *data)
{
	int ret, temp;

	ret = sprd_fgu_get_temp(data, &temp);
	if (ret) {
		dev_err(data->dev, "Suspend calib failed to temp, ret = %d\n", ret);
		return ret;
	}

	if (temp < SPRD_FGU_CAP_CALIB_TEMP_LOW || temp > SPRD_FGU_CAP_CALIB_TEMP_HI) {
		dev_err(data->dev, "Suspend calib  temp = %d out range\n", temp);
		ret = -EINVAL;
	}

	dev_info(data->dev, "%s, temp = %d\n", __func__, temp);

	return ret;
}

static int sprd_fgu_suspend_calib_check_sleep_time(struct sprd_fgu_data *data)
{
	s64 cur_time;
	int ret;
	struct sprd_fgu_info *fgu_info = data->fgu_info;

	ret = sprd_fgu_get_rtc_time(data, &cur_time);
	if (ret)
		return -EINVAL;

	fgu_info->slp_cap_calib.resume_time = cur_time;

	dev_info(data->dev, "%s, resume_time = %lld, suspend_time = %lld\n",
		 __func__, fgu_info->slp_cap_calib.resume_time, fgu_info->slp_cap_calib.suspend_time);

	/* sleep time > 300s */
	if ((fgu_info->slp_cap_calib.resume_time - fgu_info->slp_cap_calib.suspend_time <
	    SPRD_FGU_SLP_CAP_CALIB_SLP_TIME) ||
	    fgu_info->slp_cap_calib.suspend_time == 0) {
		dev_info(data->dev, "suspend time not meet: suspend_time = %lld, resume_time = %lld\n",
			 fgu_info->slp_cap_calib.suspend_time,
			 fgu_info->slp_cap_calib.resume_time);
		return -EINVAL;
	}

	return 0;
}

static int sprd_fgu_suspend_calib_check_sleep_cur(struct sprd_fgu_data *data)
{
	int cc_uah, times, sleep_cur_ma = 0, ret = 0;
	struct sprd_fgu_info *fgu_info = data->fgu_info;

	ret = fgu_info->ops->get_cc_uah(fgu_info, &fgu_info->slp_cap_calib.resume_cc_uah, false);
	if (ret)
		return ret;

	cc_uah = fgu_info->slp_cap_calib.suspend_cc_uah - fgu_info->slp_cap_calib.resume_cc_uah;
	times = (int)(fgu_info->slp_cap_calib.resume_time -  fgu_info->slp_cap_calib.suspend_time);
	sleep_cur_ma = sprd_fgu_uah2current(cc_uah, times);

	dev_info(data->dev, "%s, suspend_cc_uah = %d, resume_cc_uah = %d, cc_uah = %d, "
		 "times = %d, sleep_cur_ma = %d\n",
		 __func__, fgu_info->slp_cap_calib.suspend_cc_uah,
		 fgu_info->slp_cap_calib.resume_cc_uah, cc_uah, times, sleep_cur_ma);

	if (abs(sleep_cur_ma) > fgu_info->slp_cap_calib.relax_cur_threshold) {
		dev_info(data->dev, "Sleep calib sleep current = %d, not meet conditions\n",
			 sleep_cur_ma);
		return -EINVAL;
	}

	return ret;
}

static int sprd_fgu_suspend_calib_get_ocv(struct sprd_fgu_data *data)
{
	int ret, i, cur_ma = 0x7fffffff;
	u32 vol_mv = 0;
	struct sprd_fgu_info *fgu_info = data->fgu_info;

	for (i = SPRD_FGU_VOLTAGE_BUFF_CNT - 1; i >= 0; i--) {
		vol_mv = 0;
		ret = fgu_info->ops->get_vbat_buf(fgu_info, i, &vol_mv);
		if (ret) {
			dev_info(data->dev, "Sleep calib fail to get vbat_buf[%d]\n", i);
			continue;
		}

		cur_ma = 0x7fffffff;
		ret = fgu_info->ops->get_current_buf(fgu_info, i, &cur_ma);
		if (ret) {
			dev_info(data->dev, "Sleep calib fail to get cur_buf[%d]\n", i);
			continue;
		}

		if (abs(cur_ma) < fgu_info->slp_cap_calib.relax_cur_threshold) {
			dev_info(data->dev, "Sleep calib get cur[%d] = %d meet condition\n", i, cur_ma);
			break;
		}
	}

	if (vol_mv == 0 || cur_ma == 0x7fffffff) {
		dev_info(data->dev, "Sleep calib fail to get cur and vol: cur = %d, vol = %d\n",
			 cur_ma, vol_mv);
		return -EINVAL;
	}

	dev_info(data->dev, "Sleep calib vol = %d, cur = %d, i = %d\n", vol_mv, cur_ma, i);

	fgu_info->slp_cap_calib.resume_ocv_uv = vol_mv * 1000;

	return 0;
}

static void sprd_fgu_suspend_calib_cap_calib(struct sprd_fgu_data *data)
{
	struct sprd_fgu_info *fgu_info = data->fgu_info;

	fgu_info->slp_cap_calib.resume_ocv_cap =
		sprd_fgu_ocv2cap(data->cap_table, data->table_len, fgu_info->slp_cap_calib.resume_ocv_uv);

	dev_info(data->dev, "%s, resume_ocv_cap = %d, normal_temp_cap = %d, init_cap = %d\n",
		 __func__, fgu_info->slp_cap_calib.resume_ocv_cap,
		 data->normal_temp_cap, data->init_cap);

	if (fgu_info->slp_cap_calib.resume_ocv_cap > data->normal_temp_cap + 30)
		data->init_cap += (fgu_info->slp_cap_calib.resume_ocv_cap -
				   data->normal_temp_cap - 30);
	else if (fgu_info->slp_cap_calib.resume_ocv_cap < data->normal_temp_cap - 30)
		data->init_cap -= (data->normal_temp_cap -
				   fgu_info->slp_cap_calib.resume_ocv_cap - 30);

	data->track.lpocv_info.valid = true;
	data->track.lpocv_info.ocv_uv = fgu_info->slp_cap_calib.resume_ocv_uv;
	data->track.lpocv_info.ocv_rtc_time = fgu_info->slp_cap_calib.resume_time;
}

static void sprd_fgu_suspend_calib_check(struct sprd_fgu_data *data)
{
	int ret;
	struct sprd_fgu_info *fgu_info = data->fgu_info;

	if (!fgu_info->slp_cap_calib.support_slp_calib)
		return;

	ret = sprd_fgu_suspend_calib_check_chg_sts(data);
	if (ret)
		return;

	ret = fgu_info->ops->suspend_calib_check_relax_counter_sts(fgu_info);
	if (ret)
		return;

	ret = sprd_fgu_suspend_calib_check_sleep_time(data);
	if (ret)
		return;

	ret = sprd_fgu_suspend_calib_check_sleep_cur(data);
	if (ret)
		return;

	ret = sprd_fgu_suspend_calib_check_temp(data);
	if (ret)
		return;

	ret = sprd_fgu_suspend_calib_get_ocv(data);
	if (ret)
		return;

	if (!sprd_fgu_is_in_low_energy_dens(data, fgu_info->slp_cap_calib.resume_ocv_uv,
					    data->cap_calib_dens_ocv_table,
					    data->cap_calib_dens_ocv_table_len))
		return;

	sprd_fgu_suspend_calib_cap_calib(data);

	return;
}

static void sprd_fgu_suspend_calib_config(struct sprd_fgu_data *data)
{
	s64 cur_time;
	int ret = 0;
	struct sprd_fgu_info *fgu_info = data->fgu_info;

	if (!fgu_info->slp_cap_calib.support_slp_calib)
		return;

	ret = sprd_fgu_get_rtc_time(data, &cur_time);
	if (ret)
		cur_time = 0;

	fgu_info->slp_cap_calib.suspend_time =  cur_time;
	fgu_info->ops->get_cc_uah(fgu_info, &fgu_info->slp_cap_calib.suspend_cc_uah, false);

	ret = fgu_info->ops->relax_mode_config(fgu_info);
	if (!ret)
		fgu_info->ops->enable_fgu_int(fgu_info, SPRD_FGU_POWER_LOW_CNT_INT_CMD, true);

	return;
}

static int sprd_fgu_batt_ovp_threshold_config(struct sprd_fgu_data *data)
{
	int ret = 0;
	struct sprd_fgu_info *fgu_info = data->fgu_info;

	ret = fgu_info->ops->set_high_overload(fgu_info, data->batt_ovp_threshold);
	if (ret) {
		dev_err(data->dev, "failed to set fgu high overload\n");
		return ret;
	}

	ret = fgu_info->ops->enable_fgu_int(fgu_info, SPRD_FGU_VOLT_HIGH_INT_CMD, true);
	if (ret)
		return ret;

	data->is_ovp = false;
	dev_info(data->dev, "%s %d overload threshold config done!\n", __func__, __LINE__);

	return ret;
}

static int sprd_fgu_get_property(struct power_supply *psy,
				 enum power_supply_property psp,
				 union power_supply_propval *val)
{
	struct sprd_fgu_data *data = power_supply_get_drvdata(psy);
	struct sprd_fgu_info *fgu_info = data->fgu_info;
	int ret = 0, value = 0;

	if (!data) {
		pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
		return -EINVAL;
	}

	mutex_lock(&data->lock);

	switch (psp) {
	case POWER_SUPPLY_PROP_HEALTH:
		if (data->debug_info.batt_health_debug_en) {
			val->intval = data->debug_info.debug_batt_health;
			break;
		}

		sprd_fgu_get_health(data, &value);
		val->intval = value;
		break;

	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = data->bat_present;

		if (data->debug_info.batt_present_debug_en)
			val->intval = data->debug_info.debug_batt_present;

		break;

	case POWER_SUPPLY_PROP_TEMP:
		if (data->debug_info.temp_debug_en)
			val->intval = data->debug_info.debug_temp;
		else if (data->temp_table_len <= 0 || (data->bat_present == 0 && allow_charger_enable))
			val->intval = 200;
		else {
			ret = sprd_fgu_get_temp(data, &value);
			if (ret < 0 && !data->debug_info.temp_debug_en)
				goto error;

			ret = 0;
			val->intval = value;
		}

		break;

	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;

	case POWER_SUPPLY_PROP_CAPACITY:
		if (val->intval == CM_BOOT_CAPACITY) {
			val->intval = data->boot_cap;
			break;
		}
		val->intval = sprd_fgu_capacity_remap(data, data->bat_soc);
		break;

	case POWER_SUPPLY_PROP_VOLTAGE_AVG:
		ret = fgu_info->ops->get_vbat_avg(fgu_info, &value);
		if (ret)
			goto error;

		val->intval = value * 1000;
		break;

	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		if (data->debug_info.vbat_now_debug_en) {
			val->intval = data->debug_info.debug_vbat_now;
			break;
		}

		ret = fgu_info->ops->get_vbat_now(fgu_info, &value);
		if (ret)
			goto error;

		val->intval = value * 1000;
		break;

	case POWER_SUPPLY_PROP_VOLTAGE_OCV:
		if (data->debug_info.ocv_debug_en) {
			val->intval = data->debug_info.debug_ocv;
			break;
		}
		ret = sprd_fgu_get_vbat_ocv(data, &value);
		if (ret)
			goto error;

		val->intval = value * 1000;
		break;

	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		if (data->debug_info.chg_vol_debug_en) {
			val->intval = data->debug_info.debug_chg_vol;
			break;
		}

		ret = sprd_fgu_get_charge_vol(data, &value);
		if (ret)
			goto error;

		val->intval = value * 1000;
		break;

	case POWER_SUPPLY_PROP_CURRENT_AVG:
		ret = fgu_info->ops->get_current_avg(fgu_info, &value);
		if (ret)
			goto error;

		val->intval = value * 1000;
		break;

	case POWER_SUPPLY_PROP_CURRENT_NOW:
		if (data->debug_info.cur_now_debug_en) {
			val->intval = data->debug_info.debug_cur_now;
			break;
		}

		ret = fgu_info->ops->get_current_now(fgu_info, &value);
		if (ret)
			goto error;

		val->intval = value * 1000;
		break;

	case POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN:
		val->intval = data->total_mah * 1000;
		break;

	case POWER_SUPPLY_PROP_CYCLE_COUNT:
		val->intval = data->charge_cycle;
		break;

	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		ret = sprd_fgu_get_basp_volt(data, &val->intval);
		break;
	default:
		ret = -EINVAL;
		break;
	}

error:
	mutex_unlock(&data->lock);
	return ret;
}

static int sprd_fgu_set_property(struct power_supply *psy,
				 enum power_supply_property psp,
				 const union power_supply_propval *val)
{
	struct sprd_fgu_data *data = power_supply_get_drvdata(psy);
	int ret = 0, ui_cap, normal_cap;
	struct sprd_fgu_info *fgu_info = data->fgu_info;
	struct timespec64 cur_time;

	if (!data) {
		pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
		return -EINVAL;
	}

	mutex_lock(&data->lock);

	switch (psp) {
	case POWER_SUPPLY_PROP_CAPACITY:
		ret = fgu_info->ops->save_last_cap(fgu_info, val->intval);
		if (ret < 0)
			dev_err(data->dev, "failed to save battery capacity\n");

		ret = fgu_info->ops->save_normal_temperature_cap(fgu_info, data->normal_temp_cap);
		if (ret < 0)
			dev_err(data->dev, "failed to save normal temperature capacity\n");

		ret = fgu_info->ops->read_last_cap(fgu_info, &ui_cap);
		if (ret < 0) {
			ui_cap = -1;
			dev_err(data->dev, "failed to read ui capacity\n");
		}

		if (val->intval != ui_cap) {
			dev_info(data->dev, "ui cap save failed, save it again!"
				 "save_cap = %d, read_cap = %d\n", val->intval, ui_cap);
			ret = fgu_info->ops->save_last_cap(fgu_info, val->intval);
			if (ret < 0)
				dev_err(data->dev, "%d failed to save battery capacity\n", __LINE__);
		}

		ret = fgu_info->ops->read_normal_temperature_cap(fgu_info, &normal_cap);
		if (ret < 0) {
			normal_cap = -1;
			dev_err(data->dev, "failed to read normal capacity\n");
		}

		if (data->normal_temp_cap != normal_cap) {
			dev_info(data->dev, "normal cap save failed, save it again!"
				 "save_cap = %d, read_cap = %d\n", data->normal_temp_cap, normal_cap);
			ret = fgu_info->ops->save_normal_temperature_cap(fgu_info, data->normal_temp_cap);
			if (ret < 0)
				dev_err(data->dev, "%d failed to save normal temperature capacity\n", __LINE__);
		}
		break;

	case POWER_SUPPLY_PROP_STATUS:
		data->chg_sts = val->intval;
		if (data->chg_sts != POWER_SUPPLY_STATUS_CHARGING) {
			cur_time = ktime_to_timespec64(ktime_get_boottime());
			data->stop_charge_times = cur_time.tv_sec;
		}
		break;

	case POWER_SUPPLY_PROP_CALIBRATE:
		data->init_cap = fgu_info->ops->adjust_cap(fgu_info, val->intval);
		break;

	case POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN:
		data->total_mah = val->intval / 1000;
		break;

	case POWER_SUPPLY_PROP_TEMP:
		if (val->intval == SPRD_FGU_DEBUG_EN_CMD) {
			dev_info(data->dev, "Change battery temperature to debug mode\n");
			data->debug_info.temp_debug_en = true;
			data->debug_info.debug_temp = 200;
			break;
		} else if (val->intval == SPRD_FGU_DEBUG_DIS_CMD) {
			dev_info(data->dev, "Recovery battery temperature to normal mode\n");
			data->debug_info.temp_debug_en = false;
			break;
		} else if (!data->debug_info.temp_debug_en) {
			dev_info(data->dev, "Battery temperature not in debug mode\n");
			break;
		}

		data->debug_info.debug_temp = val->intval;
		dev_info(data->dev, "Battery debug temperature = %d\n", val->intval);
		break;

	case POWER_SUPPLY_PROP_PRESENT:
		if (val->intval == SPRD_FGU_DEBUG_EN_CMD) {
			dev_info(data->dev, "Change battery present to debug mode\n");
			data->debug_info.debug_batt_present = true;
			data->debug_info.batt_present_debug_en = true;
			break;
		} else if (val->intval == SPRD_FGU_DEBUG_DIS_CMD) {
			dev_info(data->dev, "Recovery battery present to normal mode\n");
			data->debug_info.batt_present_debug_en = false;
			break;
		} else if (!data->debug_info.batt_present_debug_en) {
			dev_info(data->dev, "Battery present not in debug mode\n");
			break;
		}

		data->debug_info.debug_batt_present = !!val->intval;
		mutex_unlock(&data->lock);
		cm_notify_event(data->battery, data->debug_info.debug_batt_present ?
				CM_EVENT_BATT_IN : CM_EVENT_BATT_OUT, NULL);
		dev_info(data->dev, "Battery debug present = %d\n", !!val->intval);
		return ret;

	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		if (val->intval == SPRD_FGU_DEBUG_EN_CMD) {
			dev_info(data->dev, "Change voltage_now to debug mode\n");
			data->debug_info.debug_vbat_now = 4000000;
			data->debug_info.vbat_now_debug_en = true;
			break;
		} else if (val->intval == SPRD_FGU_DEBUG_DIS_CMD) {
			dev_info(data->dev, "Recovery voltage_now to normal mode\n");
			data->debug_info.vbat_now_debug_en = false;
			data->debug_info.debug_vbat_now = 0;
			break;
		} else if (!data->debug_info.vbat_now_debug_en) {
			dev_info(data->dev, "Voltage_now not in debug mode\n");
			break;
		}

		data->debug_info.debug_vbat_now = val->intval;
		dev_info(data->dev, "Battery debug voltage_now = %d\n", val->intval);
		break;

	case POWER_SUPPLY_PROP_CURRENT_NOW:
		if (val->intval == SPRD_FGU_DEBUG_EN_CMD) {
			dev_info(data->dev, "Change current_now to debug mode\n");
			data->debug_info.debug_cur_now = 1000000;
			data->debug_info.cur_now_debug_en = true;
			break;
		} else if (val->intval == SPRD_FGU_DEBUG_DIS_CMD) {
			dev_info(data->dev, "Recovery current_now to normal mode\n");
			data->debug_info.cur_now_debug_en = false;
			data->debug_info.debug_cur_now = 0;
			break;
		} else if (!data->debug_info.cur_now_debug_en) {
			dev_info(data->dev, "Current_now not in debug mode\n");
			break;
		}

		data->debug_info.debug_cur_now = val->intval;
		dev_info(data->dev, "Battery debug current_now = %d\n", val->intval);
		break;

	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		if (val->intval == SPRD_FGU_DEBUG_EN_CMD) {
			dev_info(data->dev, "Change charge voltage to debug mode\n");
			data->debug_info.debug_chg_vol = 5000000;
			data->debug_info.chg_vol_debug_en = true;
			break;
		} else if (val->intval == SPRD_FGU_DEBUG_DIS_CMD) {
			dev_info(data->dev, "Recovery charge voltage to normal mode\n");
			data->debug_info.chg_vol_debug_en = false;
			data->debug_info.debug_chg_vol = 0;
			break;
		} else if (!data->debug_info.chg_vol_debug_en) {
			dev_info(data->dev, "Charge voltage not in debug mode\n");
			break;
		}

		data->debug_info.debug_chg_vol = val->intval;
		dev_info(data->dev, "Battery debug charge voltage = %d\n", val->intval);
		break;

	case POWER_SUPPLY_PROP_VOLTAGE_OCV:
		if (val->intval == SPRD_FGU_DEBUG_EN_CMD) {
			dev_info(data->dev, "Change OCV voltage to debug mode\n");
			data->debug_info.debug_ocv = 4000000;
			data->debug_info.ocv_debug_en = true;
			break;
		} else if (val->intval == SPRD_FGU_DEBUG_DIS_CMD) {
			dev_info(data->dev, "Recovery OCV voltage to normal mode\n");
			data->debug_info.ocv_debug_en = false;
			data->debug_info.debug_ocv = 0;
			break;
		} else if (!data->debug_info.ocv_debug_en) {
			dev_info(data->dev, "OCV voltage not in debug mode\n");
			break;
		}

		data->debug_info.debug_ocv = val->intval;
		dev_info(data->dev, "Battery debug OCV voltage = %d\n", val->intval);
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		if (val->intval == SPRD_FGU_GOOD_HEALTH_CMD) {
			data->is_ovp = false;

			ret = fgu_info->ops->enable_fgu_int(fgu_info, SPRD_FGU_VOLT_HIGH_INT_CMD, true);

			break;
		} else if (val->intval == SPRD_FGU_DEBUG_EN_CMD) {
			dev_info(data->dev, "Change Battery Health to debug mode\n");
			data->debug_info.batt_health_debug_en = true;
			data->debug_info.debug_batt_health = 1;
			break;
		} else if (val->intval == SPRD_FGU_DEBUG_DIS_CMD) {
			dev_info(data->dev, "Recovery  Battery Health to normal mode\n");
			data->debug_info.batt_health_debug_en = false;
			data->debug_info.debug_batt_health = 1;
			break;
		} else if (!data->debug_info.batt_health_debug_en) {
			dev_info(data->dev, "OCV  Battery Health not in debug mode\n");
			break;
		}

		data->debug_info.debug_batt_health = val->intval;
		dev_info(data->dev, "Battery debug  Battery Health = %#x\n", val->intval);
		break;

	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		ret = sprd_fgu_set_basp_volt(data, val->intval);
		break;

	default:
		ret = -EINVAL;
	}

	mutex_unlock(&data->lock);
	return ret;
}

static void sprd_fgu_external_power_changed(struct power_supply *psy)
{
	struct sprd_fgu_data *data = power_supply_get_drvdata(psy);

	if (!data) {
		pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
		return;
	}

	power_supply_changed(data->battery);
}

static int sprd_fgu_property_is_writeable(struct power_supply *psy,
					  enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_TEMP:
	case POWER_SUPPLY_PROP_CAPACITY:
	case POWER_SUPPLY_PROP_CALIBRATE:
	case POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN:
	case POWER_SUPPLY_PROP_PRESENT:
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
	case POWER_SUPPLY_PROP_CURRENT_NOW:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
	case POWER_SUPPLY_PROP_VOLTAGE_OCV:
	case POWER_SUPPLY_PROP_HEALTH:
		return 1;

	default:
		return 0;
	}
}

static enum power_supply_property sprd_fgu_props[] = {
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_OCV,
	POWER_SUPPLY_PROP_VOLTAGE_AVG,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_AVG,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE,
	POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN,
	POWER_SUPPLY_PROP_CALIBRATE,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
};

static const struct power_supply_desc sprd_fgu_desc = {
	.name			= "sc27xx-fgu",
	.type			= POWER_SUPPLY_TYPE_UNKNOWN,
	.properties		= sprd_fgu_props,
	.num_properties		= ARRAY_SIZE(sprd_fgu_props),
	.get_property		= sprd_fgu_get_property,
	.set_property		= sprd_fgu_set_property,
	.external_power_changed	= sprd_fgu_external_power_changed,
	.property_is_writeable	= sprd_fgu_property_is_writeable,
	.no_thermal		= true,
};

static void sprd_fgu_adjust_uusoc_vbat(struct sprd_fgu_data *data)
{
	if (data->batt_mv >= SPRD_FGU_LOW_VBAT_REC_REGION) {
		data->uusoc_vbat = 0;
	} else if (data->batt_mv >= SPRD_FGU_LOW_VBAT_REGION) {
		if (data->uusoc_vbat >= SPRD_FGU_LOW_VBAT_UUSOC_STEP)
			data->uusoc_vbat -= SPRD_FGU_LOW_VBAT_UUSOC_STEP;
	}
}

static void sprd_fgu_low_capacity_match_ocv(struct sprd_fgu_data *data)
{
	struct sprd_fgu_info *fgu_info = data->fgu_info;

	if (data->ocv_uv < data->min_volt_uv && data->normal_temp_cap > data->alarm_cap) {
		data->init_cap -= 5;
		if (data->init_cap < 0)
			data->init_cap = 0;
	} else if (data->ocv_uv > data->min_volt_uv && data->normal_temp_cap <= data->alarm_cap) {
		data->init_cap = fgu_info->ops->adjust_cap(fgu_info, data->alarm_cap);
	} else if (data->ocv_uv <= data->cap_table[data->table_len - 1].ocv) {
		data->init_cap = fgu_info->ops->adjust_cap(fgu_info, 0);
	} else if (data->first_calib_volt > 0 && data->first_calib_cap > 0 &&
		   data->ocv_uv <= data->first_calib_volt &&
		   data->normal_temp_cap > data->first_calib_cap) {
		data->init_cap -= 5;
		if (data->init_cap < 0)
			data->init_cap = 0;
	} else if (data->batt_mv < SPRD_FGU_LOW_VBAT_REGION &&
		   data->normal_temp_cap > data->alarm_cap)
		data->uusoc_vbat += SPRD_FGU_LOW_VBAT_UUSOC_STEP;

	sprd_fgu_adjust_uusoc_vbat(data);
}

static bool sprd_fgu_discharging_current_trend(struct sprd_fgu_data *data)
{
	int i, ret, cur_ma = 0;
	bool is_discharging = true;
	struct sprd_fgu_info *fgu_info = data->fgu_info;

	if (data->cur_now_buff[SPRD_FGU_CURRENT_BUFF_CNT - 1] == SPRD_FGU_MAGIC_NUMBER) {
		is_discharging = false;
		for (i = 0; i < SPRD_FGU_CURRENT_BUFF_CNT; i++) {
			ret = fgu_info->ops->get_current_buf(fgu_info, i, &cur_ma);
			if (ret) {
				dev_err(data->dev, "fail to init cur_now_buff[%d]\n", i);
				return is_discharging;
			}

			data->cur_now_buff[i] = cur_ma;
		}

		return is_discharging;
	}

	for (i = 0; i < SPRD_FGU_CURRENT_BUFF_CNT; i++) {
		if (data->cur_now_buff[i] > 0)
			is_discharging = false;
	}

	for (i = 0; i < SPRD_FGU_CURRENT_BUFF_CNT; i++) {
		ret = fgu_info->ops->get_current_buf(fgu_info, i, &cur_ma);
		if (ret) {
			dev_err(data->dev, "fail to get cur_now_buff[%d]\n", i);
			data->cur_now_buff[SPRD_FGU_CURRENT_BUFF_CNT - 1] =
				SPRD_FGU_MAGIC_NUMBER;
			is_discharging = false;
			return is_discharging;
		}

		data->cur_now_buff[i] = cur_ma;
		if (data->cur_now_buff[i] > 0)
			is_discharging = false;
	}

	return is_discharging;
}

static bool sprd_fgu_discharging_cc_mah_trend(struct sprd_fgu_data *data)
{
	struct sprd_fgu_info *fgu_info = data->fgu_info;
	int cur_cc_uah, ret;

	ret = fgu_info->ops->get_cc_uah(fgu_info, &cur_cc_uah, false);
	if (ret) {
		dev_err(data->dev, "%s failed get cur_cc_uah!!\n", __func__);
		return false;
	}

	return (data->last_cc_uah != SPRD_FGU_MAGIC_NUMBER) &&
		(data->last_cc_uah > cur_cc_uah) ? true : false;
}

static bool sprd_fgu_discharging_trend(struct sprd_fgu_data *data)
{
	bool discharging = true;
	static int dischg_cnt;
	int i;

	if (dischg_cnt >= SPRD_FGU_DISCHG_CNT)
		dischg_cnt = 0;

	if (!sprd_fgu_discharging_current_trend(data)) {
		discharging =  false;
		goto charging;
	}

	if (!sprd_fgu_discharging_cc_mah_trend(data)) {
		discharging =  false;
		goto charging;
	}

	data->dischg_trend[dischg_cnt++] = true;

	for (i = 0; i < SPRD_FGU_DISCHG_CNT; i++) {
		if (!data->dischg_trend[i]) {
			discharging =  false;
			return discharging;
		}
	}

	if (data->chg_sts == POWER_SUPPLY_STATUS_CHARGING && discharging)
		dev_info(data->dev, "%s: discharging\n", __func__);

	return discharging;

charging:
	data->dischg_trend[dischg_cnt++] = false;
	return discharging;
}

static void sprd_fgu_discharging_calibration(struct sprd_fgu_data *data, int *cap)
{
	int ret, low_temp_ocv;
	int vol_mv, vbat_avg_mv, vol_uv, vbat_avg_uv;
	struct sprd_fgu_info *fgu_info = data->fgu_info;

	if (data->bat_temp <= SPRD_FGU_LOW_TEMP_REGION) {
		dev_err(data->dev, "exceed temp range not need to calibrate.\n");
		return;
	}

	if (*cap > SPRD_FGU_CAP_CALIB_ALARM_CAP)
		return;

	if (data->chg_sts != POWER_SUPPLY_STATUS_CHARGING ||
	    sprd_fgu_discharging_trend(data)) {
		low_temp_ocv = sprd_fgu_cap2ocv(data->cap_table,
						data->table_len,
						SPRD_FGU_CAP_CALIB_ALARM_CAP);

		/* Get current battery voltage */
		ret = fgu_info->ops->get_vbat_now(fgu_info, &vol_mv);
		if (ret) {
			dev_err(data->dev, "get current battery voltage error.\n");
			return;
		}

		/* Get average value of battery voltage */
		ret = fgu_info->ops->get_vbat_avg(fgu_info, &vbat_avg_mv);
		if (ret) {
			dev_err(data->dev, "get average value of battery voltage error.\n");
			return;
		}

		vol_uv = vol_mv * 1000;
		vbat_avg_uv = vbat_avg_mv * 1000;
		dev_info(data->dev, "discharging_trend low_temp_ocv = %d, vbat = %d, vbat_avg = %d\n",
			 low_temp_ocv, vol_uv, vbat_avg_uv);
		if (vol_uv > low_temp_ocv && vbat_avg_uv > low_temp_ocv)
			*cap = SPRD_FGU_CAP_CALIB_ALARM_CAP;
	}
}

static void sprd_fgu_capacity_calibration(struct sprd_fgu_data *data, bool int_mode)
{
	int ret, ocv_mv;
	struct sprd_fgu_info *fgu_info = data->fgu_info;

	ret = sprd_fgu_get_vbat_ocv(data, &ocv_mv);
	if (ret) {
		dev_err(data->dev, "get battery ocv error.\n");
		return;
	}

	data->ocv_uv = ocv_mv * 1000;

	ret =  fgu_info->ops->get_vbat_now(fgu_info, &data->batt_mv);
	if (ret) {
		dev_err(data->dev, "get battery vol error.\n");
		return;
	}

	/*
	 * If we are in charging mode or the battery temperature is
	 * 10 degrees or less, then we do not need to calibrate the
	 * lower capacity.
	 */
	if ((!sprd_fgu_discharging_trend(data) &&
	     data->chg_sts == POWER_SUPPLY_STATUS_CHARGING) ||
	    data->bat_temp <= SPRD_FGU_LOW_TEMP_REGION) {
		sprd_fgu_adjust_uusoc_vbat(data);
		return;
	}

	if (!data->cap_table) {
		dev_info(data->dev, "%s: cap_table allocate not ready\n", __func__);
		return;
	}

	sprd_fgu_low_capacity_match_ocv(data);

	if (data->ocv_uv <= data->min_volt_uv) {
		if (!int_mode)
			return;

		/*
		 * After adjusting the battery capacity, we should set the
		 * lowest alarm voltage instead.
		 */
		data->min_volt_uv = data->cap_table[data->table_len - 1].ocv;
		data->alarm_cap = sprd_fgu_ocv2cap(data->cap_table, data->table_len, data->min_volt_uv);

		if (data->alarm_cap < 10)
			data->alarm_cap = 10;

		fgu_info->ops->set_low_overload(fgu_info, data->min_volt_uv / 1000);
	}
}

static void sprd_fgu_batt_ovp_notfiy(struct sprd_fgu_data *data)
{
	int ret;
	struct sprd_fgu_info *fgu_info = data->fgu_info;

	data->is_ovp = true;

	ret = fgu_info->ops->enable_fgu_int(fgu_info, SPRD_FGU_VOLT_HIGH_INT_CMD, false);
	if (ret)
		dev_err(data->dev, "failed to disable high overload int\n");

	cm_notify_event(data->battery, CM_EVENT_BATT_OVERVOLTAGE, NULL);
}

static irqreturn_t sprd_fgu_interrupt(int irq, void *dev_id)
{
	struct sprd_fgu_data *data = dev_id;
	int ret, cap;
	u32 status = 0;
	struct sprd_fgu_info *fgu_info = data->fgu_info;

	if (!data) {
		pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
		return IRQ_HANDLED;
	}

	mutex_lock(&data->lock);

	ret = fgu_info->ops->get_fgu_int(fgu_info, &status);
	if (ret)
		goto out;

	if (status & BIT(SPRD_FGU_POWER_LOW_CNT_INT_EVENT)) {
		fgu_info->slp_cap_calib.power_low_cnt_int_ocurred = true;
		dev_info(data->dev, "%s, power_low_cnt_int ocurred!!\n", __func__);
		fgu_info->ops->clr_fgu_int_bit(fgu_info, SPRD_FGU_POWER_LOW_CNT_INT_CMD);
	}

	if (status & BIT(SPRD_FGU_VOLT_HIGH_INT_EVENT)) {
		sprd_fgu_batt_ovp_notfiy(data);
		dev_info(data->dev, "volt_high_int ocurred!!\n");
		fgu_info->ops->clr_fgu_int_bit(fgu_info, SPRD_FGU_VOLT_HIGH_INT_CMD);
	}

	/*
	 * When low overload voltage interrupt happens, we should calibrate the
	 * battery capacity in lower voltage stage.
	 */
	if (status & BIT(SPRD_FGU_VOLT_LOW_INT_EVENT)) {
		ret = sprd_fgu_get_capacity(data, &cap);
		if (ret)
			goto out;
		sprd_fgu_capacity_calibration(data, true);
		power_supply_changed(data->battery);
		dev_info(data->dev, "volt_low_int ocurred!!\n");
		fgu_info->ops->clr_fgu_int_bit(fgu_info, SPRD_FGU_VOLT_LOW_INT_CMD);
	}

out:
	mutex_unlock(&data->lock);

	return IRQ_HANDLED;
}

static irqreturn_t sprd_fgu_bat_detection(int irq, void *dev_id)
{
	struct sprd_fgu_data *data = dev_id;
	int state;

	if (!data) {
		pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
		return IRQ_HANDLED;
	}

	mutex_lock(&data->lock);

	state = gpiod_get_value_cansleep(data->gpiod);
	if (state < 0) {
		dev_err(data->dev, "failed to get gpio state\n");
		mutex_unlock(&data->lock);
		return IRQ_RETVAL(state);
	}

	data->bat_present = !!state;

	mutex_unlock(&data->lock);

	power_supply_changed(data->battery);

	cm_notify_event(data->battery,
			data->bat_present ? CM_EVENT_BATT_IN : CM_EVENT_BATT_OUT,
			NULL);

	return IRQ_HANDLED;
}

static void sprd_fgu_disable(void *_data)
{
	struct sprd_fgu_data *data = _data;
	struct sprd_fgu_info *fgu_info = data->fgu_info;

	if (!data) {
		pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
		return;
	}

	fgu_info->ops->enable_fgu_module(fgu_info, false);
}

static int sprd_fgu_usb_change(struct notifier_block *nb, unsigned long limit, void *info)
{
	u32 type;
	struct sprd_fgu_data *data =
		container_of(nb, struct sprd_fgu_data, usb_notify);

	if (!data) {
		pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
		return NOTIFY_OK;
	}

	pm_stay_awake(data->dev);

	if (limit)
		data->online = true;
	else
		data->online = false;

	type = data->usb_phy->chg_type;

	switch (type) {
	case SDP_TYPE:
		data->chg_type = POWER_SUPPLY_USB_TYPE_SDP;
		break;

	case DCP_TYPE:
		data->chg_type = POWER_SUPPLY_USB_TYPE_DCP;
		break;

	case CDP_TYPE:
		data->chg_type = POWER_SUPPLY_USB_TYPE_CDP;
		break;

	default:
		data->chg_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
	}

	pm_relax(data->dev);

	return NOTIFY_OK;
}

static bool sprd_fgu_cap_track_is_ocv_valid(struct sprd_fgu_data *data, int *ocv_uv,
					    struct sprd_fgu_ocv_info *ocv_info)
{
	s64 cur_time;
	int ret;

	if (!ocv_info->valid)
		return false;

	ret = sprd_fgu_get_rtc_time(data, &cur_time);
	if (ret)
		return false;

	ocv_info->valid = false;

	if (cur_time - ocv_info->ocv_rtc_time > SPRD_FGU_TRACK_OCV_VALID_TIME) {
		dev_info(data->dev, "capacity track ocv is invalid cur_time = %lld, rtc_time = %lld\n",
			 cur_time, ocv_info->ocv_rtc_time);
		return false;
	}

	if (!sprd_fgu_is_in_low_energy_dens(data, ocv_info->ocv_uv,
					    data->track.dens_ocv_table,
					    data->track.dens_ocv_table_len))
		return false;

	*ocv_uv = ocv_info->ocv_uv;


	return true;
}

static bool sprd_fgu_cap_track_is_sw_ocv_valid(struct sprd_fgu_data *data, int *ocv_uv)
{
	int i, ret, cap, cur_ma = 0, vol_mv = 0;
	int resistance, scale_ratio, ocv_mv;
	struct sprd_fgu_info *fgu_info = data->fgu_info;

	if (data->bat_temp > SPRD_FGU_TRACK_HIGH_TEMP_THRESHOLD ||
	    data->bat_temp < SPRD_FGU_TRACK_LOW_TEMP_THRESHOLD) {
		dev_err(data->dev, "exceed temp range, sw ocv is invalid\n");
		return false;
	}

	resistance = data->internal_resist;
	if (data->resist_table_len > 0) {
		scale_ratio = sprd_fgu_temp2resist_ratio(data->resist_table,
							 data->resist_table_len,
							 data->bat_temp);
		resistance = data->internal_resist * scale_ratio / 100;
	}

	for (i = 0; i < 8; i++) {
		ret = fgu_info->ops->get_current_buf(fgu_info, i, &cur_ma);
		if (ret)
			return false;

		ret = fgu_info->ops->get_vbat_buf(fgu_info, i, &vol_mv);
		if (ret)
			return false;

		if (abs(cur_ma) > SPRD_FGU_TRACK_CAP_START_CURRENT)
			return false;

		ocv_mv = vol_mv - cur_ma * (resistance + SPRD_FGU_RBAT_CMP_MOH) / 1000;
		if (ocv_mv > SPRD_FGU_TRACK_CAP_START_VOLTAGE)
			return false;

		*ocv_uv += ocv_mv * 1000;
	}

	*ocv_uv /= 8;

	cap = sprd_fgu_ocv2cap(data->cap_table, data->table_len, *ocv_uv);

	if (cap > SPRD_FGU_TRACK_START_CAP_SWOCV_HTHRESHOLD ||
	    cap < SPRD_FGU_TRACK_START_CAP_LTHRESHOLD) {
		dev_info(data->dev, "start_cap = %d does not satisfy track start condition\n", cap);
		return false;
	}

	dev_info(data->dev, "sow ocv  cur_ma = %d, vol_mV = %d, ocv_uv = %d\n",
		 cur_ma, vol_mv, *ocv_uv);

	return true;
}

static bool sprd_fgu_is_meet_cap_track_start_conditon(struct sprd_fgu_data *data, int *ocv_uv)
{
	if (sprd_fgu_cap_track_is_ocv_valid(data, ocv_uv, &data->track.pocv_info)) {
		data->track.mode = CAP_TRACK_MODE_POCV;
		dev_info(data->dev, "capacity track pocv = %d meet start condition", *ocv_uv);
	} else if (sprd_fgu_cap_track_is_ocv_valid(data, ocv_uv, &data->track.lpocv_info)) {
		data->track.mode = CAP_TRACK_MODE_LP_OCV;
		dev_info(data->dev, "capacity track lpocv = %d meet start condition", *ocv_uv);
	} else if (sprd_fgu_cap_track_is_sw_ocv_valid(data, ocv_uv)) {
		data->track.mode = CAP_TRACK_MODE_SW_OCV;
		dev_info(data->dev, "capacity track sw ocv = %d  meet start condition", *ocv_uv);
	} else {
		return false;
	}

	return true;
}

static bool sprd_fgu_is_new_cap_track_start_conditon_meet(struct sprd_fgu_data *data)
{
	int ocv_uv, cap;

	if (!sprd_fgu_cap_track_is_ocv_valid(data, &ocv_uv, &data->track.lpocv_info))
		return false;

	cap = sprd_fgu_ocv2cap(data->cap_table, data->table_len, ocv_uv);
	if (cap > SPRD_FGU_TRACK_START_CAP_HTHRESHOLD ||
	    cap < SPRD_FGU_TRACK_START_CAP_LTHRESHOLD)
		return false;

	if ((data->track.mode == CAP_TRACK_MODE_LP_OCV) &&
	    ((ktime_divns(ktime_get_boottime(), NSEC_PER_SEC) -
	      data->track.start_time) < SPRD_FGU_TRACK_NEW_OCV_VALID_THRESHOLD)) {
		return false;
	}

	dev_info(data->dev, "capacity track lpocv = %d new  start condition meet", ocv_uv);
	/*
	 * It need to set valid to true becase it will clear in
	 * sprd_fgu_cap_track_is_ocv_valid.
	 */
	data->track.lpocv_info.valid = true;

	return true;
}

static bool sprd_fgu_cap_track_is_meet_end_conditon(struct sprd_fgu_data *data)
{
	int i, ret, cur_now = 0, vol_now = 0;
	struct sprd_fgu_info *fgu_info = data->fgu_info;

	for (i = 0; i < 5; i++) {
		ret = fgu_info->ops->get_current_buf(fgu_info, i, &cur_now);
		if (ret)
			return false;

		ret = fgu_info->ops->get_vbat_buf(fgu_info, i, &vol_now);
		if (ret)
			return false;

		if (cur_now <= 0 || cur_now > data->track.end_cur || vol_now < data->track.end_vol)
			return false;
	}

	return true;

}

static void sprd_fgu_cap_track_state_init(struct sprd_fgu_data *data, int *cycle)
{
	int design_mah, learned_mah;

	design_mah = data->design_mah;
	learned_mah = data->track.learned_mah;

	data->track.state = CAP_TRACK_IDLE;

	if (data->track.pocv_info.valid)
		*cycle = SPRD_FGU_CAPACITY_TRACK_0S;

	if (learned_mah <= 0) {
		dev_err(data->dev, "[init] learned_mah is invalid.\n");
		return;
	}

	if (((learned_mah > design_mah) && ((learned_mah - design_mah) < design_mah / 10)) ||
	    ((design_mah > learned_mah) && ((design_mah - learned_mah) < design_mah / 2)))
		data->total_mah = learned_mah;
}

static void sprd_fgu_cap_track_state_idle(struct sprd_fgu_data *data, int *cycle)
{
	int ret, ocv_uv, cc_uah;
	struct sprd_fgu_info *fgu_info = data->fgu_info;

	if (!data->bat_present) {
		*cycle = SPRD_FGU_CAPACITY_TRACK_100S;
		dev_dbg(data->dev, "[idle] battery is not present, monitor later.\n");
		return;
	}

	if (!sprd_fgu_is_meet_cap_track_start_conditon(data, &ocv_uv))
		return;

	data->track.start_cap = sprd_fgu_ocv2cap(data->cap_table, data->table_len, ocv_uv);

	/*
	 * When the capacity tracking start condition is met, the battery is almost empty,
	 * so we set a starting threshold, if it is greater than it will not enable
	 * the capacity tracking function, now we set the capacity tracking monitor
	 * initial percentage threshold to 20%.
	 */
	if (data->track.start_cap > SPRD_FGU_TRACK_START_CAP_HTHRESHOLD ||
	    data->track.start_cap < SPRD_FGU_TRACK_START_CAP_LTHRESHOLD) {
		dev_dbg(data->dev, "[idle] start_cap = %d does not satisfy the track start condition\n",
			data->track.start_cap);
		data->track.start_cap = 0;
		return;
	}

	ret = fgu_info->ops->get_cc_uah(fgu_info, &cc_uah, false);
	if (ret) {
		dev_err(data->dev, "[idle] failed to get start cc_uah.\n");
		return;
	}

	data->track.start_time = ktime_divns(ktime_get_boottime(), NSEC_PER_SEC);
	data->track.start_cc_mah = cc_uah / 1000;
	data->track.state = CAP_TRACK_UPDATING;

	dev_info(data->dev, "[idle] start_time = %lld, start_cc_mah = %d, start_cap = %d\n",
		 data->track.start_time, cc_uah / 1000, data->track.start_cap);
}

static void sprd_fgu_cap_track_state_updating(struct sprd_fgu_data *data, int *cycle)
{
	int ibat_avg_ma, vbat_avg_mv, ibat_now_ma, ret;
	struct sprd_fgu_info *fgu_info = data->fgu_info;

	if (!data->bat_present) {
		*cycle = SPRD_FGU_CAPACITY_TRACK_100S;
		data->track.state = CAP_TRACK_IDLE;
		dev_err(data->dev, "[updating] battery is not present, return to idle state.\n");
		return;
	}

	if (sprd_fgu_is_new_cap_track_start_conditon_meet(data)) {
		pm_wakeup_event(data->dev, SPRD_FGU_TRACK_UPDATING_WAKE_UP_MS);
		*cycle = SPRD_FGU_CAPACITY_TRACK_0S;
		data->track.state = CAP_TRACK_IDLE;
		return;
	}

	if (data->chg_sts != POWER_SUPPLY_STATUS_CHARGING)
		return;

	if (data->bat_temp > SPRD_FGU_TRACK_HIGH_TEMP_THRESHOLD ||
	    data->bat_temp < SPRD_FGU_TRACK_LOW_TEMP_THRESHOLD) {
		*cycle = SPRD_FGU_CAPACITY_TRACK_100S;
		dev_dbg(data->dev, "[updating] exceed temp range, monitor capacity track later.\n");
		return;
	}

	if ((data->chg_type == POWER_SUPPLY_USB_TYPE_UNKNOWN)
	    || (data->chg_type == POWER_SUPPLY_USB_TYPE_SDP))
		return;

	if ((ktime_divns(ktime_get_boottime(), NSEC_PER_SEC) -
	     data->track.start_time) > SPRD_FGU_TRACK_TIMEOUT_THRESHOLD) {
		data->track.state = CAP_TRACK_IDLE;
		dev_err(data->dev, "capacity tracktime out.\n");
		return;
	}

	ret = fgu_info->ops->get_current_avg(fgu_info, &ibat_avg_ma);
	if (ret) {
		dev_err(data->dev, "failed to get ibat average current.\n");
		return;
	}

	ret = fgu_info->ops->get_current_now(fgu_info, &ibat_now_ma);
	if (ret) {
		dev_err(data->dev, "failed to get ibat current now.\n");
		return;
	}

	ret = fgu_info->ops->get_vbat_avg(fgu_info, &vbat_avg_mv);
	if (ret) {
		dev_err(data->dev, "failed to get battery voltage.\n");
		return;
	}

	if (vbat_avg_mv > data->track.end_vol &&
	    (ibat_avg_ma > 0 && ibat_avg_ma < data->track.end_cur) &&
	    (ibat_now_ma > 0 && ibat_now_ma < data->track.end_cur)) {
		dev_info(data->dev, "capacity track finish condition is met!!!\n");
		pm_wakeup_event(data->dev, SPRD_FGU_TRACK_DONE_WAKE_UP_MS);
		data->track.state = CAP_TRACK_DONE;
		*cycle = SPRD_FGU_CAPACITY_TRACK_3S;
	}
}

static void sprd_fgu_cap_track_state_done(struct sprd_fgu_data *data, int *cycle)
{
	int ret, ibat_avg_ma = 0, vbat_avg_mv = 0, ibat_now_ma = 0;
	int delta_mah, total_mah, design_mah, start_mah, end_mah, cur_cc_uah;
	struct sprd_fgu_info *fgu_info = data->fgu_info;

	*cycle = SPRD_FGU_CAPACITY_TRACK_3S;

	if (!data->bat_present) {
		*cycle = SPRD_FGU_CAPACITY_TRACK_100S;
		data->track.state = CAP_TRACK_IDLE;
		dev_err(data->dev, "[done] battery is not present, return to idle state.\n");
		return;
	}

	if ((data->chg_type == POWER_SUPPLY_USB_TYPE_UNKNOWN)
	    || (data->chg_type == POWER_SUPPLY_USB_TYPE_SDP)) {
		data->track.state = CAP_TRACK_UPDATING;
		dev_err(data->dev, "[done] chg_type not support, return to updating state\n");
		return;
	}

	if (data->chg_sts != POWER_SUPPLY_STATUS_CHARGING) {
		*cycle = SPRD_FGU_CAPACITY_TRACK_15S;
		data->track.state = CAP_TRACK_UPDATING;
		dev_err(data->dev, "[done] Not charging, Return to updating state\n");
		return;
	}

	if (data->bat_temp > SPRD_FGU_TRACK_HIGH_TEMP_THRESHOLD ||
	    data->bat_temp < SPRD_FGU_TRACK_LOW_TEMP_THRESHOLD) {
		data->track.state = CAP_TRACK_UPDATING;
		*cycle = SPRD_FGU_CAPACITY_TRACK_15S;
		dev_err(data->dev, "[done] exceed temp range, return to updating state.\n");
		return;
	}

	ret = fgu_info->ops->get_current_avg(fgu_info, &ibat_avg_ma);
	if (ret) {
		dev_err(data->dev, "failed to get battery current.\n");
		return;
	}

	ret = fgu_info->ops->get_current_now(fgu_info, &ibat_now_ma);
	if (ret) {
		dev_err(data->dev, "failed to get now current.\n");
		return;
	}

	ret = fgu_info->ops->get_vbat_avg(fgu_info, &vbat_avg_mv);
	if (ret) {
		dev_err(data->dev, "failed to get battery voltage.\n");
		return;
	}

	if (!sprd_fgu_cap_track_is_meet_end_conditon(data)) {
		if (vbat_avg_mv > data->track.end_vol &&
		    (ibat_avg_ma > 0 && ibat_avg_ma < data->track.end_cur) &&
		    (ibat_now_ma > 0 && ibat_now_ma < data->track.end_cur)) {
			*cycle = SPRD_FGU_CAPACITY_TRACK_3S;
		} else {
			*cycle = SPRD_FGU_CAPACITY_TRACK_15S;
			data->track.state = CAP_TRACK_UPDATING;
			dev_info(data->dev, "[done] does not meet end conditons, return to updating"
				 " status, vbat_avg_mv = %d, ibat_avg_ma = %d, ibat_now_ma = %d\n",
				 vbat_avg_mv, ibat_avg_ma, ibat_now_ma);
		}

		return;
	}

	ret = fgu_info->ops->get_cc_uah(fgu_info, &cur_cc_uah, false);
	if (ret) {
		dev_err(data->dev, "[done] failed to get cur_cc_uah.\n");
		return;
	}

	total_mah = data->total_mah;
	design_mah = data->design_mah;
	/*
	 * Due to the capacity tracking function started, the coulomb amount corresponding
	 * to the initial percentage was not counted, so we need to compensate initial coulomb
	 * with following formula, we assume that coulomb and capacity are directly proportional.
	 *
	 * For example:
	 * if capacity tracking function started,  the battery percentage is 3%, we will count
	 * the capacity from 3% to 100%, it will discard capacity from 0% to 3%, so we use
	 * "total_mah * (start_cap / 100)" to compensate.
	 *
	 * formula:
	 * end_mah = total_mah * (start_cap / 100) + delta_mah
	 */
	delta_mah = cur_cc_uah / 1000 - data->track.start_cc_mah;
	start_mah = (total_mah * data->track.start_cap) / 1000;
	end_mah = start_mah + delta_mah;

	dev_info(data->dev, "Capacity track end: cur_cc_mah = %d, start_cc_mah = %d,"
		 "delta_mah = %d, total_mah = %d, design_mah = %d, start_mah = %d,"
		 "end_mah = %d, ibat_avg_ma = %d, ibat_now_ma = %d, vbat_avg_mv = %d\n",
		 cur_cc_uah / 1000, data->track.start_cc_mah, delta_mah, total_mah, design_mah,
		 start_mah, end_mah, ibat_avg_ma, ibat_now_ma, vbat_avg_mv);

	data->track.state = CAP_TRACK_IDLE;
	if (((end_mah > design_mah) && ((end_mah - design_mah) < design_mah / 10)) ||
	    ((design_mah > end_mah) && ((design_mah - end_mah) < design_mah / 2))) {
		data->total_mah = end_mah;
		pm_wakeup_event(data->dev, SPRD_FGU_TRACK_WAKE_UP_MS);
		dev_info(data->dev, "track capacity done: end_mah = %d, diff_mah = %d\n",
			 end_mah, (end_mah - total_mah));
	} else {
		dev_info(data->dev, "less than half standard capacity.\n");
	}
}

static int sprd_fgu_cap_track_state_machine(struct sprd_fgu_data *data)
{
	int cycle = SPRD_FGU_CAPACITY_TRACK_15S;

	switch (data->track.state) {
	case CAP_TRACK_INIT:
		sprd_fgu_cap_track_state_init(data, &cycle);
		break;
	case CAP_TRACK_IDLE:
		sprd_fgu_cap_track_state_idle(data, &cycle);
		break;
	case CAP_TRACK_UPDATING:
		sprd_fgu_cap_track_state_updating(data, &cycle);
		break;
	case CAP_TRACK_DONE:
		sprd_fgu_cap_track_state_done(data, &cycle);
		break;
	case CAP_TRACK_ERR:
		dev_err(data->dev, "track status error\n");
		break;

	default:
		break;
	}

	return cycle;
}
static int sprd_fgu_cap_calc_work_cycle(struct sprd_fgu_data *data)
{
	int ret = 0, temp, cur_ma = 0, delta_cc_uah;
	int work_cycle = SPRD_FGU_CAP_CALC_WORK_15S;
	s64 times;

	ret = sprd_fgu_get_temp(data, &temp);
	if (ret) {
		dev_err(data->dev, "failed get temp!!\n");
		return work_cycle;
	}

	if (temp < SPRD_FGU_CAP_CALC_WORK_LOW_TEMP ||
	    data->bat_soc < SPRD_FGU_CAP_CALC_WORK_LOW_CAP) {
		dev_info(data->dev, "temp = %d, battery soc = %d\n", temp, data->bat_soc);
		work_cycle = SPRD_FGU_CAP_CALC_WORK_8S;
		return work_cycle;
	}

	if (data->work_exit_times != 0 && data->work_enter_cc_uah > data->work_exit_cc_uah) {
		times = data->work_enter_times - data->work_exit_times;
		delta_cc_uah = data->work_enter_cc_uah - data->work_exit_cc_uah;
		if (times != 0) {
			cur_ma = sprd_fgu_uah2current(delta_cc_uah, times);
			if (cur_ma > SPRD_FGU_CAP_CALC_WORK_BIG_CURRENT) {
				dev_info(data->dev, "%s cur_ma = %d!!\n", __func__, cur_ma);
				work_cycle = SPRD_FGU_CAP_CALC_WORK_8S;
			}
		}
	}

	return work_cycle;
}
static void sprd_fgu_cap_calculate_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct sprd_fgu_data *data = container_of(dwork, struct sprd_fgu_data,
						  cap_calculate_work);
	int ret = 0, work_cycle = SPRD_FGU_CAP_CALC_WORK_15S;
	struct sprd_fgu_info *fgu_info = data->fgu_info;
	struct timespec64 cur_time;

	if (!data) {
		pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
		return;
	}

	ret = fgu_info->ops->get_cc_uah(fgu_info, &data->work_enter_cc_uah, false);
	if (ret) {
		dev_err(data->dev, "failed get work_enter_cc_uah!!\n");
		goto out;
	}

	cur_time = ktime_to_timespec64(ktime_get_boottime());
	data->work_enter_times = cur_time.tv_sec;

	ret = sprd_fgu_get_capacity(data, &data->bat_soc);
	if (ret) {
		dev_err(data->dev, "failed get capacity!!\n");
		goto out;
	}

	work_cycle = sprd_fgu_cap_calc_work_cycle(data);

	ret = fgu_info->ops->get_cc_uah(fgu_info, &data->work_exit_cc_uah, false);
	if (ret) {
		dev_err(data->dev, "failed get work_exit_cc_uah!!\n");
		goto out;
	}

	cur_time = ktime_to_timespec64(ktime_get_boottime());
	data->work_exit_times = cur_time.tv_sec;

	data->last_cc_uah = data->work_exit_cc_uah;

out:
	dev_info(data->dev, "battery soc = %d, cycle = %d\n", data->bat_soc, work_cycle);
	schedule_delayed_work(&data->cap_calculate_work, msecs_to_jiffies(work_cycle * 1000));
}

static void sprd_fgu_cap_track_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct sprd_fgu_data *data = container_of(dwork,
			struct sprd_fgu_data, cap_track_work);
	int work_cycle = SPRD_FGU_CAPACITY_TRACK_15S;

	if (!data) {
		pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
		return;
	}

	if (!data->track.cap_tracking)
		return;

	work_cycle = sprd_fgu_cap_track_state_machine(data);

	schedule_delayed_work(&data->cap_track_work, msecs_to_jiffies(work_cycle * 1000));
}

static void sprd_fgu_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct sprd_fgu_data *data = container_of(dwork, struct sprd_fgu_data, fgu_work);

	if (!data) {
		pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
		return;
	}

	sprd_fgu_dump_info(data);

	schedule_delayed_work(&data->fgu_work, SPRD_FGU_WORK_MS);
}

static ssize_t sprd_fgu_dump_info_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct sprd_fgu_sysfs *sysfs =
		container_of(attr, struct sprd_fgu_sysfs,
			     attr_sprd_fgu_dump_info);
	struct sprd_fgu_data *data = sysfs->data;

	if (!data) {
		dev_err(dev, "%s sprd_fgu_data is null\n", __func__);
		return snprintf(buf, PAGE_SIZE, "%s sprd_fgu_data is null\n", __func__);
	}

	data->support_debug_log = !data->support_debug_log;

	sprd_fgu_dump_battery_info(data, "dump_info");

	return snprintf(buf, PAGE_SIZE, "[batt present:%d];\n[total_mah:%d];\n[init_cap:%d];\n"
			"[cc_mah:%d];\n[alarm_cap:%d];\n[boot_cap:%d];\n[normal_temp_cap:%d];\n"
			"[max_volt:%d];\n[min_volt:%d];\n[first_calib_volt:%d];\n[first_calib_cap:%d];\n"
			"[uusoc_vbat:%d];\n[boot_vol:%d];\n[bat_temp:%d];\n[online:%d];\n"
			"[is_first_poweron:%d];\n[chg_type:%d]\n[support_debug_log:%d]\n",
			data->bat_present, data->total_mah, data->init_cap, data->cc_uah / 1000,
			data->alarm_cap, data->boot_cap, data->normal_temp_cap, data->max_volt_uv,
			data->min_volt_uv, data->first_calib_volt, data->first_calib_cap,
			data->uusoc_vbat, data->boot_volt_uv, data->bat_temp, data->online,
			data->is_first_poweron, data->chg_type, data->support_debug_log);
}

static ssize_t sprd_fgu_sel_reg_id_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct sprd_fgu_sysfs *sysfs =
		container_of(attr, struct sprd_fgu_sysfs,
			     attr_sprd_fgu_sel_reg_id);
	struct sprd_fgu_data *data = sysfs->data;

	if (!data) {
		dev_err(dev, "%s sprd_fgu_data is null\n", __func__);
		return snprintf(buf, PAGE_SIZE, "%s sprd_fgu_data is null\n", __func__);
	}

	return snprintf(buf, PAGE_SIZE, "[sel_reg_id:0x%x]\n", data->debug_info.sel_reg_id);
}

static ssize_t sprd_fgu_sel_reg_id_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t count)
{
	struct sprd_fgu_sysfs *sysfs =
		container_of(attr, struct sprd_fgu_sysfs,
			     attr_sprd_fgu_sel_reg_id);
	struct sprd_fgu_data *data = sysfs->data;
	u32 val;
	int ret;

	if (!data) {
		dev_err(dev, "%s sprd_fgu_data is null\n", __func__);
		return count;
	}

	ret =  kstrtouint(buf, 16, &val);
	if (ret) {
		dev_err(data->dev, "fail to get addr, ret = %d\n", ret);
		return count;
	}

	if (val > SPRD_FGU_REG_MAX) {
		dev_err(data->dev, "val = %d, out of SPRD_FGU_REG_MAX\n", val);
		return count;
	}

	data->debug_info.sel_reg_id = val;

	return count;
}

static ssize_t sprd_fgu_reg_val_show(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct sprd_fgu_sysfs *sysfs =
		container_of(attr, struct sprd_fgu_sysfs,
			     attr_sprd_fgu_reg_val);
	struct sprd_fgu_data *data = sysfs->data;
	struct sprd_fgu_info *fgu_info = data->fgu_info;
	u32 reg_val;
	int ret;

	if (!data) {
		dev_err(dev, "%s sprd_fgu_data is null\n", __func__);
		return snprintf(buf, PAGE_SIZE, "%s sprd_fgu_data is null\n", __func__);
	}

	ret = fgu_info->ops->get_reg_val(fgu_info, data->debug_info.sel_reg_id, &reg_val);
	if (ret)
		return snprintf(buf, PAGE_SIZE, "Fail to read [REG_0x%x], ret = %d\n",
				data->debug_info.sel_reg_id, ret);

	return snprintf(buf, PAGE_SIZE, "[REG_0x%x][0x%x]\n",
			data->debug_info.sel_reg_id, reg_val);
}

static ssize_t sprd_fgu_reg_val_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct sprd_fgu_sysfs *sysfs =
		container_of(attr, struct sprd_fgu_sysfs,
			     attr_sprd_fgu_reg_val);
	struct sprd_fgu_data *data = sysfs->data;
	struct sprd_fgu_info *fgu_info = data->fgu_info;
	u32 reg_val;
	int ret;

	if (!data) {
		dev_err(dev, "%s sprd_fgu_data is null\n", __func__);
		return count;
	}

	ret =  kstrtouint(buf, 16, &reg_val);
	if (ret) {
		dev_err(data->dev, "fail to get addr, ret = %d\n", ret);
		return count;
	}

	dev_info(data->dev, "Try to set [REG_0x%x][0x%x]\n", data->debug_info.sel_reg_id, reg_val);

	ret = fgu_info->ops->set_reg_val(fgu_info, data->debug_info.sel_reg_id, reg_val);
	if (ret)
		dev_err(data->dev, "fail to set [REG_0x%x][0x%x], ret = %d\n",
			data->debug_info.sel_reg_id, reg_val, ret);

	return count;
}

static ssize_t sprd_fgu_enable_sleep_calib_show(struct device *dev,
						struct device_attribute *attr,
						char *buf)
{
	struct sprd_fgu_sysfs *sysfs =
		container_of(attr, struct sprd_fgu_sysfs,
			     attr_sprd_fgu_enable_sleep_calib);
	struct sprd_fgu_data *data = sysfs->data;
	struct sprd_fgu_info *fgu_info = data->fgu_info;

	if (!data) {
		dev_err(dev, "%s sprd_fgu_data is null\n", __func__);
		return snprintf(buf, PAGE_SIZE, "%s sprd_fgu_data is null\n", __func__);
	}

	return snprintf(buf, PAGE_SIZE, "capacity sleep calibration function [%s]\n",
			fgu_info->slp_cap_calib.support_slp_calib ? "Enabled" : "Disabled");
}

static ssize_t sprd_fgu_enable_sleep_calib_store(struct device *dev,
						 struct device_attribute *attr,
						 const char *buf, size_t count)
{
	struct sprd_fgu_sysfs *sysfs =
		container_of(attr, struct sprd_fgu_sysfs,
			     attr_sprd_fgu_enable_sleep_calib);
	struct sprd_fgu_data *data = sysfs->data;
	struct sprd_fgu_info *fgu_info = data->fgu_info;
	bool enbale_slp_calib;
	int ret;

	if (!data) {
		dev_err(dev, "%s sprd_fgu_data is null\n", __func__);
		return count;
	}

	ret =  kstrtobool(buf, &enbale_slp_calib);
	if (ret) {
		dev_err(data->dev, "fail to get sleep_calib info, ret = %d\n", ret);
		return count;
	}

	fgu_info->slp_cap_calib.support_slp_calib = enbale_slp_calib;

	dev_info(data->dev, "Try to [%s] capacity sleep calibration function\n",
		 fgu_info->slp_cap_calib.support_slp_calib ? "Enabled" : "Disabled");

	return count;
}

static ssize_t sprd_fgu_relax_cnt_th_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct sprd_fgu_sysfs *sysfs =
		container_of(attr, struct sprd_fgu_sysfs,
			     attr_sprd_fgu_relax_cnt_th);
	struct sprd_fgu_data *data = sysfs->data;
	struct sprd_fgu_info *fgu_info = data->fgu_info;

	if (!data) {
		dev_err(dev, "%s sprd_fgu_data is null\n", __func__);
		return snprintf(buf, PAGE_SIZE, "%s sprd_fgu_data is null\n", __func__);
	}

	return snprintf(buf, PAGE_SIZE, "[power_low_cnt_th][%d]\n",
			fgu_info->slp_cap_calib.power_low_counter_threshold);
}

static ssize_t sprd_fgu_relax_cnt_th_store(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t count)
{
	struct sprd_fgu_sysfs *sysfs =
		container_of(attr, struct sprd_fgu_sysfs,
			     attr_sprd_fgu_relax_cnt_th);
	struct sprd_fgu_data *data = sysfs->data;
	struct sprd_fgu_info *fgu_info = data->fgu_info;
	u32 power_low_cnt;
	int ret;

	if (!data) {
		dev_err(dev, "%s sprd_fgu_data is null\n", __func__);
		return count;
	}

	ret =  kstrtouint(buf, 10, &power_low_cnt);
	if (ret) {
		dev_err(data->dev, "fail to get power_low_cnt info, ret = %d\n", ret);
		return count;
	}

	fgu_info->slp_cap_calib.power_low_counter_threshold = power_low_cnt;

	dev_info(data->dev, "Try to set [power_low_cnt_th] to [%d]\n",
		 fgu_info->slp_cap_calib.power_low_counter_threshold);

	return count;
}

static ssize_t sprd_fgu_relax_cur_th_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct sprd_fgu_sysfs *sysfs =
		container_of(attr, struct sprd_fgu_sysfs,
			     attr_sprd_fgu_relax_cur_th);
	struct sprd_fgu_data *data = sysfs->data;
	struct sprd_fgu_info *fgu_info = data->fgu_info;

	if (!data) {
		dev_err(dev, "%s sprd_fgu_data is null\n", __func__);
		return snprintf(buf, PAGE_SIZE, "%s sprd_fgu_data is null\n", __func__);
	}

	return snprintf(buf, PAGE_SIZE, "[relax_cur_th][%d]\n",
			fgu_info->slp_cap_calib.relax_cur_threshold);
}

static ssize_t sprd_fgu_relax_cur_th_store(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t count)
{
	struct sprd_fgu_sysfs *sysfs =
		container_of(attr, struct sprd_fgu_sysfs,
			     attr_sprd_fgu_relax_cur_th);
	struct sprd_fgu_data *data = sysfs->data;
	struct sprd_fgu_info *fgu_info = data->fgu_info;
	u32 relax_cur;
	int ret;

	if (!data) {
		dev_err(dev, "%s sprd_fgu_data is null\n", __func__);
		return count;
	}

	ret =  kstrtouint(buf, 10, &relax_cur);
	if (ret) {
		dev_err(data->dev, "fail to get relax_cur info, ret = %d\n", ret);
		return count;
	}

	fgu_info->slp_cap_calib.relax_cur_threshold = relax_cur;

	dev_info(data->dev, "Try to set [relax_cur_th] to [%d]\n",
		 fgu_info->slp_cap_calib.relax_cur_threshold);

	return count;
}

static int sprd_fgu_register_sysfs(struct sprd_fgu_data *data)
{
	struct sprd_fgu_sysfs *sysfs;
	int ret;

	sysfs = devm_kzalloc(data->dev, sizeof(*sysfs), GFP_KERNEL);
	if (!sysfs)
		return -ENOMEM;

	data->sysfs = sysfs;
	sysfs->data = data;
	sysfs->name = "sprd_fgu_sysfs";
	sysfs->attrs[0] = &sysfs->attr_sprd_fgu_dump_info.attr;
	sysfs->attrs[1] = &sysfs->attr_sprd_fgu_sel_reg_id.attr;
	sysfs->attrs[2] = &sysfs->attr_sprd_fgu_reg_val.attr;
	sysfs->attrs[3] = &sysfs->attr_sprd_fgu_enable_sleep_calib.attr;
	sysfs->attrs[4] = &sysfs->attr_sprd_fgu_relax_cnt_th.attr;
	sysfs->attrs[5] = &sysfs->attr_sprd_fgu_relax_cur_th.attr;
	sysfs->attrs[6] = NULL;
	sysfs->attr_g.name = "debug";
	sysfs->attr_g.attrs = sysfs->attrs;

	sysfs_attr_init(&sysfs->attr_sprd_fgu_dump_info.attr);
	sysfs->attr_sprd_fgu_dump_info.attr.name = "dump_info";
	sysfs->attr_sprd_fgu_dump_info.attr.mode = 0444;
	sysfs->attr_sprd_fgu_dump_info.show = sprd_fgu_dump_info_show;

	sysfs_attr_init(&sysfs->attr_sprd_fgu_sel_reg_id.attr);
	sysfs->attr_sprd_fgu_sel_reg_id.attr.name = "sel_reg_id";
	sysfs->attr_sprd_fgu_sel_reg_id.attr.mode = 0644;
	sysfs->attr_sprd_fgu_sel_reg_id.show = sprd_fgu_sel_reg_id_show;
	sysfs->attr_sprd_fgu_sel_reg_id.store = sprd_fgu_sel_reg_id_store;

	sysfs_attr_init(&sysfs->attr_sprd_fgu_reg_val.attr);
	sysfs->attr_sprd_fgu_reg_val.attr.name = "reg_val";
	sysfs->attr_sprd_fgu_reg_val.attr.mode = 0644;
	sysfs->attr_sprd_fgu_reg_val.show = sprd_fgu_reg_val_show;
	sysfs->attr_sprd_fgu_reg_val.store = sprd_fgu_reg_val_store;

	sysfs_attr_init(&sysfs->attr_sprd_fgu_enable_sleep_calib.attr);
	sysfs->attr_sprd_fgu_enable_sleep_calib.attr.name = "enable_sleep_calib";
	sysfs->attr_sprd_fgu_enable_sleep_calib.attr.mode = 0644;
	sysfs->attr_sprd_fgu_enable_sleep_calib.show = sprd_fgu_enable_sleep_calib_show;
	sysfs->attr_sprd_fgu_enable_sleep_calib.store = sprd_fgu_enable_sleep_calib_store;

	sysfs_attr_init(&sysfs->attr_sprd_fgu_relax_cnt_th.attr);
	sysfs->attr_sprd_fgu_relax_cnt_th.attr.name = "relax_cnt_th";
	sysfs->attr_sprd_fgu_relax_cnt_th.attr.mode = 0644;
	sysfs->attr_sprd_fgu_relax_cnt_th.show = sprd_fgu_relax_cnt_th_show;
	sysfs->attr_sprd_fgu_relax_cnt_th.store = sprd_fgu_relax_cnt_th_store;

	sysfs_attr_init(&sysfs->attr_sprd_fgu_relax_cur_th.attr);
	sysfs->attr_sprd_fgu_relax_cur_th.attr.name = "relax_cur_th";
	sysfs->attr_sprd_fgu_relax_cur_th.attr.mode = 0644;
	sysfs->attr_sprd_fgu_relax_cur_th.show = sprd_fgu_relax_cur_th_show;
	sysfs->attr_sprd_fgu_relax_cur_th.store = sprd_fgu_relax_cur_th_store;

	ret = sysfs_create_group(&data->battery->dev.kobj, &sysfs->attr_g);
	if (ret < 0)
		dev_err(data->dev, "Cannot create sysfs , ret = %d\n", ret);

	return ret;
}

static int sprd_fgu_parse_sprd_battery_info(struct sprd_fgu_data *data,
					    struct sprd_battery_info *info)
{
	struct sprd_battery_ocv_table *table;
	int i;

	/*
	 * For SC27XX fuel gauge device, we only use one ocv-capacity
	 * table in normal temperature 20 Celsius.
	 */
	table = sprd_battery_find_ocv2cap_table(info, 20, &data->table_len);
	if (!table)
		return -EINVAL;

	data->cap_table = devm_kmemdup(data->dev, table,
				       data->table_len * sizeof(*table),
				       GFP_KERNEL);
	if (!data->cap_table)
		return -ENOMEM;

	/*
	 * We should give a initial temperature value of temp_buff.
	 */
	data->temp_buff[0] = -500;

	data->temp_table_len = info->battery_vol_temp_table_len;
	if (data->temp_table_len > 0) {
		data->temp_table = devm_kmemdup(data->dev, info->battery_vol_temp_table,
						data->temp_table_len *
						sizeof(struct sprd_battery_vol_temp_table),
						GFP_KERNEL);
		if (!data->temp_table)
			return -ENOMEM;
	}

	data->cap_table_len = info->battery_temp_cap_table_len;
	if (data->cap_table_len > 0) {
		data->cap_temp_table = devm_kmemdup(data->dev, info->battery_temp_cap_table,
						    data->cap_table_len *
						    sizeof(struct sprd_battery_temp_cap_table),
						    GFP_KERNEL);
		if (!data->cap_temp_table)
			return -ENOMEM;
	}

	data->resist_table_len = info->battery_temp_resist_table_len;
	if (data->resist_table_len > 0) {
		data->resist_table = devm_kmemdup(data->dev, info->battery_temp_resist_table,
						  data->resist_table_len *
						  sizeof(struct sprd_battery_resistance_temp_table),
						  GFP_KERNEL);
		if (!data->resist_table)
			return -ENOMEM;
	}

	data->rbat_temp_table_len = info->battery_internal_resistance_temp_table_len;
	if (data->rbat_temp_table_len > 0) {
		data->rbat_temp_table =
			devm_kmemdup(data->dev,
				     info->battery_internal_resistance_temp_table,
				     (u32)data->rbat_temp_table_len * sizeof(int), GFP_KERNEL);
		if (!data->rbat_temp_table)
			return -ENOMEM;
	}

	data->rbat_ocv_table_len = info->battery_internal_resistance_ocv_table_len;
	if (data->rbat_ocv_table_len > 0) {
		data->rbat_ocv_table =
			devm_kmemdup(data->dev,
				     info->battery_internal_resistance_ocv_table,
				     (u32)data->rbat_ocv_table_len * sizeof(int), GFP_KERNEL);
		if (!data->rbat_ocv_table)
			return -ENOMEM;
	}

	data->rabat_table_len = info->battery_internal_resistance_table_len[0];
	if (data->rabat_table_len > 0) {
		data->rbat_table = devm_kzalloc(data->dev,
						(u32)data->rbat_temp_table_len * sizeof(int *),
						GFP_KERNEL);
		if (!data->rbat_table) {
			dev_err(data->dev, "Fail to alloc rbat_table\n");
			return -ENOMEM;
		}
		for (i = 0; i < data->rbat_temp_table_len; i++) {
			data->rbat_table[i] =
				devm_kmemdup(data->dev,
					     info->battery_internal_resistance_table[i],
					     (u32)data->rabat_table_len * sizeof(int), GFP_KERNEL);
			if (!data->rbat_table[i]) {
				dev_err(data->dev, "data->rbat_table[%d]\n", i);
				return -ENOMEM;
			}
		}
	}

	if (data->rabat_table_len > 0) {
		data->target_rbat_table = devm_kzalloc(data->dev,
						       (u32)data->rabat_table_len * sizeof(int),
						       GFP_KERNEL);
		if (!data->target_rbat_table) {
			dev_err(data->dev, "Fail to alloc resist_table\n");
			return -ENOMEM;
		}
	}

	if (data->rbat_temp_table_len > 0 && data->rabat_table_len > 0 &&
	    data->rbat_ocv_table_len > 0 && data->rbat_temp_table &&
	    data->rbat_ocv_table && data->rbat_table && data->target_rbat_table)
		data->support_multi_resistance = true;

	data->cap_calib_dens_ocv_table_len = info->cap_calib_dens_ocv_table_len;
	if (data->cap_calib_dens_ocv_table_len > 0) {
		data->cap_calib_dens_ocv_table =
			devm_kmemdup(data->dev, info->cap_calib_dens_ocv_table,
				     (u32)data->cap_calib_dens_ocv_table_len *
				     sizeof(density_ocv_table),
				     GFP_KERNEL);
		if (!data->cap_calib_dens_ocv_table) {
			dev_err(data->dev, "data->cap_calib_dens_ocv_table is null\n");
			return -ENOMEM;
		}
	}

	data->track.dens_ocv_table_len = info->cap_track_dens_ocv_table_len;
	if (data->track.dens_ocv_table_len > 0) {
		data->track.dens_ocv_table =
			devm_kmemdup(data->dev, info->cap_track_dens_ocv_table,
				     (u32)data->track.dens_ocv_table_len * sizeof(density_ocv_table),
				     GFP_KERNEL);
		if (!data->track.dens_ocv_table) {
			dev_err(data->dev, "data->track.dens_ocv_table is null\n");
			return -ENOMEM;
		}
	}

	data->basp_full_design_table_len = info->basp_charge_full_design_uah_table_len;
	if (data->basp_full_design_table_len > 0) {
		data->basp_full_design_table =
			devm_kmemdup(data->dev, info->basp_charge_full_design_uah_table,
				     data->basp_full_design_table_len * sizeof(int), GFP_KERNEL);
		if (!data->basp_full_design_table) {
			dev_err(data->dev, "data->basp_full_design_table is null\n");
			return -ENOMEM;
		}
	}

	data->basp_voltage_max_table_len = info->basp_constant_charge_voltage_max_uv_table_len;
	if (data->basp_voltage_max_table_len > 0) {
		data->basp_voltage_max_table =
			devm_kmemdup(data->dev, info->basp_constant_charge_voltage_max_uv_table,
				     data->basp_voltage_max_table_len * sizeof(int), GFP_KERNEL);
		if (!data->basp_voltage_max_table) {
			dev_err(data->dev, "data->basp_voltage_max_table is null\n");
			return -ENOMEM;
		}
	}

	data->basp_ocv_table_len = info->basp_ocv_table_len[0];
	if (data->basp_ocv_table_len > 0) {
		data->basp_ocv_table =
			devm_kzalloc(data->dev, data->basp_voltage_max_table_len * sizeof(int *),
				     GFP_KERNEL);

		if (!data->basp_ocv_table) {
			dev_err(data->dev, "Fail to alloc basp_ocv_table\n");
			return -ENOMEM;
		}

		for (i = 0; i < data->basp_voltage_max_table_len; i++) {
			data->basp_ocv_table[i] =
				devm_kmemdup(data->dev, info->basp_ocv_table[i],
					     data->basp_ocv_table_len *
					     sizeof(struct sprd_battery_ocv_table),
					     GFP_KERNEL);
			if (!data->basp_ocv_table[i]) {
				dev_err(data->dev, "data->basp_ocv_table[%d]\n", i);
				return -ENOMEM;
			}
		}
	}

	if (info->fullbatt_track_end_voltage_uv > 0)
		data->track.end_vol = info->fullbatt_track_end_voltage_uv / 1000;
	else
		dev_warn(data->dev, "no fgu track.end_vol support\n");

	if (info->fullbatt_track_end_current_uA > 0)
		data->track.end_cur = info->fullbatt_track_end_current_uA / 1000;
	else
		dev_warn(data->dev, "no fgu track.end_cur support\n");

	if (info->first_capacity_calibration_voltage_uv > 0)
		data->first_calib_volt = info->first_capacity_calibration_voltage_uv;
	else
		dev_warn(data->dev, "no fgu first_calib_volt support\n");

	if (info->batt_ovp_threshold_uv > 0)
		data->batt_ovp_threshold = info->batt_ovp_threshold_uv / 1000;
	else
		dev_warn(data->dev, "no fgu battery ovp threshold support\n");

	if (info->first_capacity_calibration_capacity > 0)
		data->first_calib_cap = info->first_capacity_calibration_capacity;
	else
		dev_warn(data->dev, "no fgu first_calib_cap support\n");

	if (info->charge_full_design_uah > 0)
		data->total_mah = info->charge_full_design_uah / 1000;
	else
		dev_warn(data->dev, "no fgu charge_full_design_uah support\n");

	data->design_mah = data->total_mah;

	if (info->constant_charge_voltage_max_uv > 0)
		data->max_volt_uv = info->constant_charge_voltage_max_uv;
	else
		dev_warn(data->dev, "no fgu constant_charge_voltage_max_uv support\n");

	if (info->factory_internal_resistance_uohm > 0)
		data->internal_resist = info->factory_internal_resistance_uohm / 1000;
	else
		dev_warn(data->dev, "no fgu factory_internal_resistance_uohm support\n");

	if (info->voltage_min_design_uv > 0)
		data->min_volt_uv = info->voltage_min_design_uv;
	else
		dev_warn(data->dev, "no fgu voltage_min_design_uv support\n");

	if (data->support_debug_log)
		sprd_fgu_dump_battery_info(data, "parse_resistance_table");

	return 0;
}

static int sprd_fgu_hw_init(struct sprd_fgu_data *data)
{
	int ret;
	struct sprd_battery_info info = {};
	struct sprd_fgu_info *fgu_info = data->fgu_info;
	struct timespec64 cur_time;

	data->cur_now_buff[SPRD_FGU_CURRENT_BUFF_CNT - 1] = SPRD_FGU_MAGIC_NUMBER;

	ret = sprd_battery_get_battery_info(data->battery, &info);
	if (ret) {
		sprd_battery_put_battery_info(data->battery, &info);
		dev_err(data->dev, "failed to get sprd battery information\n");
		return ret;
	}

	ret = sprd_fgu_parse_sprd_battery_info(data, &info);
	sprd_battery_put_battery_info(data->battery, &info);
	if (ret) {
		dev_err(data->dev, "failed to parse battery information, ret = %d\n", ret);
		return ret;
	}

	sprd_fgu_parse_cmdline(data);

	data->alarm_cap = sprd_fgu_ocv2cap(data->cap_table, data->table_len, data->min_volt_uv);
	/*
	 * We must keep the alarm capacity is larger than 0%. When in monkey
	 * test, the precision power supply setting 4000mv, but the fake battery
	 * has been simulated into a real battery. Due to it has been discharging,
	 * the battery capacity has been decreasing, finally will reach 0%, so upper
	 * layer will issue a command to shutdown. we in order to prevent such problem,
	 * we determine if the ocv voltage is greater than data->min_volt_uv and cap is
	 * small alarm capacity. We will recalculate the battery capacity based on ocv voltage.
	 */
	if (data->alarm_cap < 10)
		data->alarm_cap = 10;

	ret = fgu_info->ops->fgu_calibration(fgu_info);
	if (ret) {
		dev_err(data->dev, "failed to calibrate fgu, ret = %d\n", ret);
		return ret;
	}

	/* Enable the FGU module and FGU RTC clock to make it work*/
	ret = fgu_info->ops->enable_fgu_module(fgu_info, true);
	if (ret)
		goto disable_fgu;

	ret = fgu_info->ops->clr_fgu_int(fgu_info);
	if (ret) {
		dev_err(data->dev, "failed to clear interrupt status\n");
		goto disable_fgu;
	}

	/*
	 * Set the voltage low overload threshold, which means when the battery
	 * voltage is lower than this threshold, the controller will generate
	 * one interrupt to notify.
	 */
	ret = fgu_info->ops->set_low_overload(fgu_info, data->min_volt_uv / 1000);
	if (ret) {
		dev_err(data->dev, "failed to set fgu low overload\n");
		goto disable_fgu;
	}

	/*
	 * Set the capacity delta threshold, that means when the capacity
	 * change is multiples of the delta threshold, the controller
	 * will generate one interrupt to notify the users to update the battery
	 * capacity. Now we set the 1% capacity value.
	 */
	ret = fgu_info->ops->set_cap_delta_thre(fgu_info, data->total_mah, 10);
	if (ret) {
		goto disable_fgu;
	}

	ret = fgu_info->ops->enable_relax_cnt_mode(fgu_info);
	if (ret) {
		dev_err(data->dev, "Fail to enable RELAX_CNT_MODE, re= %d\n", ret);
		goto disable_fgu;
	}

	ret = sprd_fgu_get_temp(data, &data->bat_temp);
	if (ret) {
		dev_err(data->dev, "failed to get battery temperature\n");
		goto disable_fgu;
	}

	/*
	 * Get the boot battery capacity when system powers on, which is used to
	 * initialize the coulomb counter. After that, we can read the coulomb
	 * counter to measure the battery capacity.
	 */
	ret = sprd_fgu_get_boot_capacity(data, &data->init_cap);
	if (ret) {
		dev_err(data->dev, "failed to get boot capacity\n");
		goto disable_fgu;
	}
	if (data->batt_ovp_threshold) {
		ret = sprd_fgu_batt_ovp_threshold_config(data);
		if (ret) {
			dev_err(data->dev, "failed to set overload thershold config\n");
			goto disable_fgu;
		}
	}

	/*
	 * Convert battery capacity to the corresponding initial coulomb counter
	 * and set into coulomb counter registers.
	 */
	ret = fgu_info->ops->reset_cc_mah(fgu_info, data->total_mah, data->init_cap);
	if (ret) {
		dev_err(data->dev, "failed to reset cc mah!\n");
		goto disable_fgu;
	}

	cur_time = ktime_to_timespec64(ktime_get_boottime());
	data->awake_times = data->stop_charge_times = cur_time.tv_sec;
	data->awake_cc_uah = 0;
	dev_info(data->dev, "suspend calib: current current time_stamp = %lld\n",
		 data->awake_times);

	return 0;

disable_fgu:
	fgu_info->ops->enable_fgu_module(fgu_info, false);

	return ret;
}

static int sprd_fgu_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = pdev->dev.of_node;
	struct power_supply_config fgu_cfg = { };
	struct sprd_fgu_data *data;
	int ret, irq;

	if (!np || !dev) {
		pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
		return -EINVAL;
	}

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data) {
		pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
		return -ENOMEM;
	}

	data->normal_temp_cap = SPRD_FGU_MAGIC_NUMBER;
	data->last_cc_uah = SPRD_FGU_MAGIC_NUMBER;
	data->chg_sts = POWER_SUPPLY_STATUS_DISCHARGING;

	data->dev = &pdev->dev;
	platform_set_drvdata(pdev, data);

	data->regmap = dev_get_regmap(dev->parent, NULL);
	if (!data->regmap) {
		dev_err(dev, "failed to get regmap\n");
		return -ENODEV;
	}

	data->track.cap_tracking = device_property_read_bool(dev, "fgu-capacity-track");
	if (data->track.cap_tracking) {
		data->usb_phy = devm_usb_get_phy_by_phandle(dev, "phys", 0);
		if (IS_ERR(data->usb_phy)) {
			dev_err(dev, "failed to find USB phy, ret = %ld\n",
				PTR_ERR(data->usb_phy));
			return -EPROBE_DEFER;
		}
	}

	data->channel = devm_iio_channel_get(dev, "bat-temp");
	if (IS_ERR(data->channel)) {
		dev_err(dev, "failed to get IIO channel, ret = %ld\n", PTR_ERR(data->channel));
		return -ENXIO;
	}

	data->charge_chan = devm_iio_channel_get(dev, "charge-vol");
	if (IS_ERR(data->charge_chan)) {
		dev_err(dev, "failed to get charge IIO channel, ret = %ld\n",
			PTR_ERR(data->charge_chan));
		return -ENXIO;
	}

	ret = sprd_fgu_init_cap_remap_table(data);
	if (ret)
		dev_err(dev, "%s init cap remap table fail\n", __func__);

	data->fgu_info = sprd_fgu_info_register(dev);
	if (IS_ERR(data->fgu_info)) {
		dev_err(dev, "failed to get fgu_info!!!\n");
		return -EPROBE_DEFER;
	}

	ret = device_property_read_u32(dev,
				       "sprd,comp-resistance-mohm",
				       &data->comp_resistance);
	if (ret)
		dev_warn(dev, "no fgu compensated resistance support\n");

	data->support_boot_calib =
		device_property_read_bool(&pdev->dev, "sprd,capacity-boot-calibration");
	if (!data->support_boot_calib)
		dev_info(&pdev->dev, "Do not support boot calibration function\n");

	ret = sprd_fgu_get_boot_mode(data);
	if (ret)
		dev_warn(dev, "get_boot_mode can't not parse bootargs property\n");

	data->support_basp =
		device_property_read_bool(&pdev->dev, "sprd,basp");
	if (!data->support_basp)
		dev_info(&pdev->dev, "Do not support basp function\n");

	data->gpiod = devm_gpiod_get(&pdev->dev, "bat-detect", GPIOD_IN);
	if (IS_ERR(data->gpiod)) {
		dev_err(dev, "failed to get battery detection GPIO\n");
		return -ENXIO;
	}

	ret = gpiod_get_value_cansleep(data->gpiod);
	if (ret < 0) {
		dev_err(dev, "failed to get gpio state\n");
		return ret;
	}

	data->bat_present = !!ret;
	mutex_init(&data->lock);
	mutex_lock(&data->lock);

	fgu_cfg.drv_data = data;
	fgu_cfg.of_node = np;
	data->battery = devm_power_supply_register(dev, &sprd_fgu_desc, &fgu_cfg);
	if (IS_ERR(data->battery)) {
		dev_err(dev, "failed to register power supply");
		ret = -ENXIO;
		goto err;
	}

	ret = devm_add_action_or_reset(dev, sprd_fgu_disable, data);
	if (ret) {
		dev_err(dev, "failed to add fgu disable action\n");
		goto err;
	}

	ret = sprd_fgu_hw_init(data);
	if (ret) {
		dev_err(dev, "failed to initialize fgu hardware\n");
		goto err;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "no irq resource specified\n");
		ret = irq;
		goto err;
	}

	ret = devm_request_threaded_irq(dev, irq, NULL,	sprd_fgu_interrupt,
					IRQF_NO_SUSPEND | IRQF_ONESHOT,
					pdev->name, data);
	if (ret) {
		dev_err(dev, "failed to request fgu IRQ\n");
		goto err;
	}

	irq = gpiod_to_irq(data->gpiod);
	if (irq < 0) {
		dev_err(dev, "failed to translate GPIO to IRQ\n");
		ret = irq;
		goto err;
	}

	ret = devm_request_threaded_irq(dev, irq, NULL,
					sprd_fgu_bat_detection,
					IRQF_ONESHOT | IRQF_TRIGGER_RISING |
					IRQF_TRIGGER_FALLING,
					pdev->name, data);
	if (ret) {
		dev_err(dev, "failed to request IRQ\n");
		goto err;
	}

	device_init_wakeup(dev, true);
	pm_wakeup_event(data->dev, SPRD_FGU_TRACK_WAKE_UP_MS);

	if (!data->track.cap_tracking || !data->track.end_vol || !data->track.end_cur) {
		dev_warn(dev, "Not support fgu track. cap_tracking = %d, end_vol = %d, end_cur = %d\n",
			 data->track.cap_tracking, data->track.end_vol, data->track.end_cur);
		data->track.cap_tracking = false;
	}

	/* init capacity track function */
	if (data->track.cap_tracking) {
		data->usb_notify.notifier_call = sprd_fgu_usb_change;
		ret = usb_register_notifier(data->usb_phy, &data->usb_notify);
		if (ret) {
			dev_err(dev, "failed to register notifier:%d\n", ret);
			goto err;
		}

		data->track.state = CAP_TRACK_INIT;
		dev_info(data->dev, "end_vol = %d, end_cur = %d\n",
			 data->track.end_vol, data->track.end_cur);
	}

	INIT_DELAYED_WORK(&data->fgu_work, sprd_fgu_work);
	INIT_DELAYED_WORK(&data->cap_track_work, sprd_fgu_cap_track_work);
	INIT_DELAYED_WORK(&data->cap_calculate_work, sprd_fgu_cap_calculate_work);
	schedule_delayed_work(&data->fgu_work, 0);
	schedule_delayed_work(&data->cap_track_work, 0);
	schedule_delayed_work(&data->cap_calculate_work,
			      msecs_to_jiffies(SPRD_FGU_CAP_CALC_WORK_15S * 1000));

	ret = sprd_fgu_register_sysfs(data);
	if (ret)
		dev_err(&pdev->dev, "register sysfs fail, ret = %d\n", ret);

	/*
	 * Fuel gauge unit initialization requires an initial
	 * battery soc value.
	 */
	ret = sprd_fgu_get_capacity(data, &data->bat_soc);
	if (ret)
		dev_err(data->dev, "%s failed get capacity!\n", __func__);

	mutex_unlock(&data->lock);

	return 0;

err:
	sprd_fgu_disable(data);
	mutex_unlock(&data->lock);
	mutex_destroy(&data->lock);
	return ret;
}

static int sprd_fgu_sr_get_duty_ratio(struct sprd_fgu_data *data)
{
	int total_sleep_time = 0, total_awake_time = 0, cnt = 0, duty_ratio = 0;
	int last_sleep_idx = (data->sr_index_sleep - 1 < 0) ?
		SPRD_FGU_SR_ARRAY_LEN - 1 : data->sr_index_sleep - 1;
	int last_awake_idx = (data->sr_index_awake - 1 < 0) ?
		SPRD_FGU_SR_ARRAY_LEN - 1 : data->sr_index_awake - 1;

	do {
		total_sleep_time += data->sr_time_sleep[last_sleep_idx];
		total_awake_time += data->sr_time_awake[last_awake_idx];

		last_sleep_idx = (data->sr_index_sleep - 1 < 0) ?
			SPRD_FGU_SR_ARRAY_LEN - 1 : data->sr_index_sleep - 1;
		last_awake_idx = (data->sr_index_awake - 1 < 0) ?
			SPRD_FGU_SR_ARRAY_LEN - 1 : data->sr_index_awake - 1;

		cnt++;
		if (cnt >= SPRD_FGU_SR_ARRAY_LEN)
			break;
	} while (total_sleep_time + total_awake_time < SPRD_FGU_SR_TOTAL_TIME_S);

	if (total_sleep_time + total_awake_time >= SPRD_FGU_SR_TOTAL_TIME_S) {
		duty_ratio = total_sleep_time * 100 /
			(total_sleep_time + total_awake_time);
		dev_info(data->dev, "%s suspend calib: total_awake_time = %d, "
			 "total_sleep_time = %d, duty_ratio = %d!!!\n",
			 __func__, total_awake_time, total_sleep_time, duty_ratio);
	}

	return duty_ratio;
}

static bool sprd_fgu_sr_need_update_ocv(struct sprd_fgu_data *data)
{
	int last_awake_time = 0, last_sleep_time = 0, duty_ratio = 0;

	/* get last awake time */
	if (data->sr_index_awake >= 0 && data->sr_index_awake < SPRD_FGU_SR_ARRAY_LEN) {
		last_awake_time = (data->sr_index_awake - 1 < 0) ?
			data->sr_time_awake[SPRD_FGU_SR_ARRAY_LEN - 1] :
			data->sr_time_awake[data->sr_index_awake - 1];
	}

	/* get last sleep time */
	if (data->sr_index_sleep >= 0 && data->sr_index_sleep < SPRD_FGU_SR_ARRAY_LEN) {
		last_sleep_time = (data->sr_index_sleep - 1 < 0) ?
			data->sr_time_sleep[SPRD_FGU_SR_ARRAY_LEN - 1] :
			data->sr_time_sleep[data->sr_index_sleep - 1];
	}

	duty_ratio = sprd_fgu_sr_get_duty_ratio(data);

	if (last_sleep_time > SPRD_FGU_SR_LAST_SLEEP_TIME_S &&
	    last_awake_time < SPRD_FGU_SR_LAST_AWAKE_TIME_S &&
	    abs(data->awake_avg_cur_ma) < SPRD_FGU_SR_AWAKE_AVG_CUR_MA &&
	    duty_ratio > SPRD_FGU_SR_DUTY_RATIO) {
		dev_info(data->dev, "%s suspend calib: last_sleep_time = %d, last_awake_time = %d, "
			 "awake_avg_cur_ma = %d, duty_ratio = %d!!!\n", __func__,
			 last_sleep_time, last_awake_time, data->awake_avg_cur_ma, duty_ratio);
		return true;
	}

	dev_info(data->dev, "%s suspend calib: last_sleep_time = %d, last_awake_time = %d, "
		 "awake_avg_cur_ma = %d is not meet!!!\n", __func__, last_sleep_time,
		 last_awake_time, data->awake_avg_cur_ma);

	return false;
}

static bool sprd_fgu_sr_ocv_is_valid(struct sprd_fgu_data *data)
{
	struct sprd_fgu_info *fgu_info = data->fgu_info;
	struct timespec64 cur_time;
	s64 sleep_time = 0;
	int cur_cc_uah = 0, sleep_delta_cc_uah, cur_ma, ret = 0;

	cur_time = ktime_to_timespec64(ktime_get_boottime());
	sleep_time = cur_time.tv_sec - data->sleep_times;

	if (sleep_time < SPRD_FGU_SR_SLEEP_MIN_TIME_S &&
	    !sprd_fgu_sr_need_update_ocv(data)) {
		dev_info(data->dev, "%s suspend calib: sleep_time = %lld "
			 "is not meet update ocv!!!\n", __func__, sleep_time);
		return false;
	}

	ret = fgu_info->ops->get_cc_uah(fgu_info, &cur_cc_uah, false);
	if (ret) {
		dev_err(data->dev, "%s suspend calib: failed get cur_cc_mah!!\n", __func__);
		return false;
	}

	sleep_delta_cc_uah = data->sleep_cc_uah - cur_cc_uah;
	if (sleep_time > 0) {
		cur_ma = sprd_fgu_uah2current(sleep_delta_cc_uah, sleep_time);
		dev_info(data->dev, "%s suspend calib: sleep_time = %lld, current cc_uah = %d , "
			 "sleep_cc_uah = %d, sleep_delta_cc_uah = %d, sleep_avg_cur_ma = %d\n",
			 __func__, sleep_time, data->sleep_cc_uah, cur_cc_uah,
			 sleep_delta_cc_uah, cur_ma);
		if (cur_ma > SPRD_FGU_SR_SLEEP_AVG_CUR_MA) {
			dev_info(data->dev, "%s suspend calib: cur_ma = %d "
				 "is not meet update ocv!!!\n", __func__, cur_ma);
			return false;
		}
	} else {
		dev_info(data->dev, "%s suspend calib: sleep_time = %lld "
			 "is not meet update ocv!!!\n", __func__, sleep_time);
		return false;
	}

	return true;
}

static int sprd_fgu_sr_get_ocv(struct sprd_fgu_data *data)
{
	int ret, i, cur_ma = 0x7fffffff, total_vol_mv = 0, valid_cnt = 0;
	u32 vol_mv = 0, vol_uv = 0;
	struct sprd_fgu_info *fgu_info = data->fgu_info;

	for (i = SPRD_FGU_VOLTAGE_BUFF_CNT - 1; i >= 0; i--) {
		vol_mv = 0;
		ret = fgu_info->ops->get_vbat_buf(fgu_info, i, &vol_mv);
		if (ret) {
			dev_info(data->dev, "%s suspend calib: fail to get vbat_buf[%d]\n",
				 __func__, i);
			continue;
		}

		cur_ma = 0x7fffffff;
		ret = fgu_info->ops->get_current_buf(fgu_info, i, &cur_ma);
		if (ret) {
			dev_info(data->dev, "%s suspend calib: fail to get cur_buf[%d]\n",
				 __func__, i);
			continue;
		}

		if (abs(cur_ma) > SPRD_FGU_SR_SLEEP_AVG_CUR_MA) {
			dev_info(data->dev, "%s suspend calib: get cur[%d] is invalid = %dmA\n",
				 __func__, i, cur_ma);
			continue;
		}

		if (vol_mv > SPRD_FGU_SR_MAX_VOL_MV || vol_mv < SPRD_FGU_SR_MIN_VOL_MV) {
			dev_info(data->dev, "%s suspend calib: get vol[%d] is invalid = %dmV\n",
				 __func__, i, vol_mv);
			continue;
		}

		dev_info(data->dev, "%s suspend calib: get index:[%d] valid current = %dmA, "
			 "valid voltage = %dmV\n", __func__, i, cur_ma, vol_mv);
		total_vol_mv += vol_mv;
		valid_cnt++;
	}

	if (valid_cnt < SPRD_FGU_SR_VALID_VOL_CNT) {
		dev_info(data->dev, "%s suspend calib: fail to get cur and vol: cur = %dmA, "
			 "vol = %dmV or valid_cnt = %d < %d!!!\n",
			 __func__, cur_ma, vol_mv, valid_cnt, SPRD_FGU_SR_VALID_VOL_CNT);
		return -EINVAL;
	}

	dev_info(data->dev, "%s suspend calib: total_vol = %dmV, valid_cnt = %d, vol = %dmV\n",
		  __func__, total_vol_mv, valid_cnt, total_vol_mv / valid_cnt);

	vol_uv = total_vol_mv * 1000 / valid_cnt;
	if (sprd_fgu_is_in_low_energy_dens(data, vol_uv, data->cap_calib_dens_ocv_table,
					   data->cap_calib_dens_ocv_table_len)) {
		data->sr_ocv_uv = vol_mv * 1000;
		dev_info(data->dev, "%s suspend calib: get sr_ocv_uv = %duV!!!\n",
			 __func__, data->sr_ocv_uv);
	}

	return 0;
}

static void sprd_fgu_sr_calib_resume_check(struct sprd_fgu_data *data)
{
	struct sprd_fgu_info *fgu_info = data->fgu_info;
	int ret = 0;
	struct timespec64 cur_time;
	s64 cur_times, sleep_time = 0;

	cur_time = ktime_to_timespec64(ktime_get_boottime());
	cur_times = data->awake_times = cur_time.tv_sec;
	sleep_time = cur_times - data->sleep_times;
	if (sleep_time >= 0) {
		data->sr_time_sleep[data->sr_index_sleep] = sleep_time;
		data->sr_index_sleep++;
		data->sr_index_sleep = data->sr_index_sleep % SPRD_FGU_SR_ARRAY_LEN;
	} else {
		dev_err(data->dev, "%s suspend calib: sleep_time = %lld, "
			"is not meet!!!\n", __func__, sleep_time);
	}

	ret = fgu_info->ops->get_cc_uah(fgu_info, &data->awake_cc_uah, false);
	if (ret) {
		dev_err(data->dev, "%s suspend calib: failed get awake_cc_uah!!\n", __func__);
		return;
	}

	dev_info(data->dev, "%s suspend calib: current time_stamp = %lld, "
		 "stop charge time_stamp = %lld, sleep_time = %lld, current cc_mah = %d\n",
		 __func__, cur_times, data->stop_charge_times, sleep_time, data->awake_cc_uah);

	if ((cur_times - data->stop_charge_times) > SPRD_FGU_SR_STOP_CHARGE_TIMES &&
	    sprd_fgu_sr_ocv_is_valid(data)) {
		ret = sprd_fgu_sr_get_ocv(data);
		if (ret)
			dev_err(data->dev, "%s suspend calib: failed get sr_ocv_uv!!\n", __func__);
	}
}

#if IS_ENABLED(CONFIG_PM_SLEEP)
static int sprd_fgu_resume(struct device *dev)
{
	struct sprd_fgu_data *data = dev_get_drvdata(dev);
	struct sprd_fgu_info *fgu_info = data->fgu_info;
	int ret = 0;

	if (!data) {
		pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
		return -EINVAL;
	}

	sprd_fgu_sr_calib_resume_check(data);

	sprd_fgu_suspend_calib_check(data);

	ret = fgu_info->ops->enable_fgu_int(fgu_info, SPRD_FGU_CLBCNT_DELTA_INT_CMD, false);
	if (ret) {
		dev_err(data->dev, "failed to disable clbcnt delta interrupt\n");
		return ret;
	}

	ret = fgu_info->ops->enable_fgu_int(fgu_info, SPRD_FGU_VOLT_LOW_INT_CMD, false);
	if (ret) {
		dev_err(data->dev, "failed to disable low voltage interrupt\n");
		return ret;
	}

	schedule_delayed_work(&data->fgu_work, 0);
	schedule_delayed_work(&data->cap_track_work, 0);
	schedule_delayed_work(&data->cap_calculate_work, 0);

	return 0;
}

static void sprd_fgu_clear_sr_time_array(struct sprd_fgu_data *data)
{
	memset(&data->sr_time_sleep, 0, sizeof(data->sr_time_sleep));
	memset(&data->sr_time_awake, 0, sizeof(data->sr_time_awake));
	data->sr_index_sleep = 0;
	data->sr_index_awake = 0;
}

static void sprd_fgu_sr_calib_suspend_check(struct sprd_fgu_data *data)
{
	struct sprd_fgu_info *fgu_info = data->fgu_info;
	struct timespec64 cur_time;
	s64 cur_times, awake_time = 0;
	int awake_delta_cc_uah, ret = 0;

	cur_time = ktime_to_timespec64(ktime_get_boottime());
	cur_times = data->sleep_times = cur_time.tv_sec;
	awake_time = cur_times - data->awake_times;
	ret = fgu_info->ops->get_cc_uah(fgu_info, &data->sleep_cc_uah, false);
	if (ret) {
		dev_err(data->dev, "%s suspend calib: failed get sleep_cc_uah!!\n", __func__);
		return;
	}

	if (awake_time > 0) {
		awake_delta_cc_uah = data->sleep_cc_uah - data->awake_cc_uah;
		data->awake_avg_cur_ma = sprd_fgu_uah2current(awake_delta_cc_uah, awake_time);
		dev_info(data->dev, "%s suspend calib: current time_stamp = %lld, "
			 "awake_time = %lld, cureent cc_uah = %d, awake_delta_cc_uah = %d, "
			 "awake_avg_cur_ma = %d\n", __func__, cur_times, awake_time,
			 data->sleep_cc_uah, awake_delta_cc_uah, data->awake_avg_cur_ma);
	}

	if (awake_time > SPRD_FGU_SR_AWAKE_MAX_TIME_S ||
	    (abs(data->awake_avg_cur_ma) > SPRD_FGU_SR_AWAKE_AVG_CUR_MA &&
	     awake_time > SPRD_FGU_SR_AWAKE_BIG_CUR_MAX_TIME_S)){
		sprd_fgu_clear_sr_time_array(data);
		dev_info(data->dev, "%s suspend calib: awake_time = %llds > %ds, "
			 "or awake_avg_cur_ma = %dmA > %dmA and awake_time = %llds > %ds, "
			 "need to clear_sr_time_array!\n",
			 __func__, awake_time, SPRD_FGU_SR_AWAKE_MAX_TIME_S,
			 abs(data->awake_avg_cur_ma), SPRD_FGU_SR_AWAKE_AVG_CUR_MA,
			 SPRD_FGU_SR_AWAKE_BIG_CUR_MAX_TIME_S, awake_time);
	} else if (awake_time > 0) {
		data->sr_time_awake[data->sr_index_awake] = awake_time;
		data->sr_index_awake++;
		data->sr_index_awake = data->sr_index_awake % SPRD_FGU_SR_ARRAY_LEN;
	} else {
		dev_err(data->dev, "%s suspend calib: awake_time = %lld, not meet!!!\n",
			__func__, awake_time);
	}
}


static int sprd_fgu_suspend(struct device *dev)
{
	struct sprd_fgu_data *data = dev_get_drvdata(dev);
	int ret, ocv_uv;
	struct sprd_fgu_info *fgu_info = data->fgu_info;

	if (!data) {
		pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
		return -EINVAL;
	}

	sprd_fgu_sr_calib_suspend_check(data);

	/*
	 * If we are charging, then no need to enable the FGU interrupts to
	 * adjust the battery capacity.
	 */
	if (data->chg_sts == POWER_SUPPLY_STATUS_CHARGING ||
	    data->chg_sts == POWER_SUPPLY_STATUS_FULL)
		return 0;

	ret = fgu_info->ops->enable_fgu_int(fgu_info, SPRD_FGU_VOLT_LOW_INT_CMD, true);
	if (ret) {
		dev_err(data->dev, "failed to enable low voltage interrupt\n");
		return ret;
	}

	ret = sprd_fgu_get_vbat_ocv(data, &ocv_uv);
	if (ret)
		goto disable_int;

	ocv_uv *= 1000;

	/*
	 * If current OCV is less than the minimum voltage, we should enable the
	 * coulomb counter threshold interrupt to notify events to adjust the
	 * battery capacity.
	 */
	if (ocv_uv < data->min_volt_uv) {
		ret = fgu_info->ops->enable_fgu_int(fgu_info, SPRD_FGU_CLBCNT_DELTA_INT_CMD, true);
		if (ret) {
			dev_err(data->dev, "failed to enable coulomb threshold int\n");
			goto disable_int;
		}
	}

	cancel_delayed_work_sync(&data->fgu_work);
	cancel_delayed_work_sync(&data->cap_track_work);
	cancel_delayed_work_sync(&data->cap_calculate_work);

	sprd_fgu_suspend_calib_config(data);

	return 0;

disable_int:
	fgu_info->ops->enable_fgu_int(fgu_info, SPRD_FGU_VOLT_LOW_INT_CMD, false);
	return ret;
}
#endif

static void sprd_fgu_shutdown(struct platform_device *pdev)
{
	struct sprd_fgu_data *data = platform_get_drvdata(pdev);

	if (!data)
		return;

	cancel_delayed_work_sync(&data->fgu_work);
	cancel_delayed_work_sync(&data->cap_track_work);
	cancel_delayed_work_sync(&data->cap_calculate_work);
}

static const struct dev_pm_ops sprd_fgu_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(sprd_fgu_suspend, sprd_fgu_resume)
};

static const struct of_device_id sprd_fgu_of_match[] = {
	{ .compatible = "sprd,ump518-fgu", },
	{ }
};
MODULE_DEVICE_TABLE(of, sprd_fgu_of_match);

static struct platform_driver sprd_fgu_driver = {
	.shutdown = sprd_fgu_shutdown,
	.probe = sprd_fgu_probe,
	.driver = {
		.name = "sprd-fgu",
		.of_match_table = sprd_fgu_of_match,
		.pm = &sprd_fgu_pm_ops,
	}
};

module_platform_driver(sprd_fgu_driver);

MODULE_DESCRIPTION("Spreadtrum PMICs Fual Gauge Unit Driver");
MODULE_LICENSE("GPL v2");
