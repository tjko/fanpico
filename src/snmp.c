/* snmp.c
   Copyright (C) 2025 Timo Kokkonen <tjko@iki.fi>

   SPDX-License-Identifier: GPL-3.0-or-later

   This file is part of FanPico.

   FanPico is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   FanPico is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with FanPico. If not, see <https://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include "pico/stdlib.h"
#ifdef LIB_PICO_CYW43_ARCH
#include "pico/cyw43_arch.h"
#include "lwip/dns.h"
#include "lwip/apps/snmp.h"
#include "lwip/apps/snmp_mib2.h"
#include "lwip/apps/snmp_table.h"
#include "lwip/apps/snmp_scalar.h"
#if TLS_SUPPORT
#include "lwip/altcp_tls.h"
#endif
#endif

#include "fanpico.h"

#ifdef WIFI_SUPPORT



/* NOTE! Private MIB for FanPico uses Private Enterprise Number (PEN)
 * that belongs to FanPico author. It is NOT allowed to allocate new
 * objects under this ID (63264) without permission from author.
 *
 * If you need to create your own private MIB, you must get
 * your own Private Enteprise Number from IANA.
 *
 * See: https://www.iana.org/assignments/enterprise-numbers/
 */
static const struct snmp_obj_id fanpico_base_oid = {
	9,                        // OID length
	{                         // OID:
		1, 3, 6, 1, 4, 1, //  SNMPv2-SMI::enterprises
		63264,            //  Private Enterprise Number
		1,                //  1 = Devices
		1                 //  1 = FanPico
	}
};



#define MAKE_GET_FUNC(funcname, var, var_type) \
	static s16_t funcname(struct snmp_node_instance* instance, void* value) \
	{ \
		var_type *val = (var_type *)value; \
		*val = var; \
		return sizeof(*val); \
	}

MAKE_GET_FUNC(get_fan_count, FAN_COUNT, u32_t)
MAKE_GET_FUNC(get_mbfan_count, MBFAN_COUNT, u32_t)
MAKE_GET_FUNC(get_sensor_count, SENSOR_COUNT, u32_t)
MAKE_GET_FUNC(get_vsensor_count, VSENSOR_COUNT, u32_t)


static const struct snmp_oid_range fan_table_oid_ranges[] = { { 1, FAN_COUNT } };
static const struct snmp_oid_range mbfan_table_oid_ranges[] = { { 1, MBFAN_COUNT } };
static const struct snmp_oid_range sensor_table_oid_ranges[] = { { 1, SENSOR_COUNT } };
static const struct snmp_oid_range vsensor_table_oid_ranges[] = { { 1, VSENSOR_COUNT } };


static snmp_err_t fan_table_get(const u32_t* column, const u32_t* row,
				union snmp_variant_value* value, u32_t* value_len)
{
	const struct fanpico_state *st = fanpico_state;
	int i = *row - 1;
	double rpm;


	if (i < 0 || i >= FAN_COUNT)
		return SNMP_ERR_NOSUCHINSTANCE;

	switch (*column) {
	case 1: // id
		value->u32 = *row;
		break;
	case 2: // name
		value->const_ptr = (const void*)cfg->fans[i].name;
		*value_len = strlen(cfg->fans[i].name);
		break;
	case 3: //rpm
		rpm = st->fan_freq[i] * 60 / cfg->fans[i].rpm_factor;
		value->u32 = rpm;
		break;
	case 4: // frequency
		value->u32 = round_decimal(st->fan_freq[i], 2) * 100;
		break;
	case 5: // duty_cycle
		value->u32 = round_decimal(st->fan_duty[i], 0);
		break;
	default:
		return SNMP_ERR_NOSUCHINSTANCE;
	}

	return SNMP_ERR_NOERROR;
}

static snmp_err_t fan_table_get_cell_val(const u32_t* column, const u32_t* row_oid, u8_t row_oid_len,
				union snmp_variant_value* value, u32_t* value_len)
{
	log_msg(LOG_INFO, "fan_table_get_cell_val(column=%lu,row_oid=%lu,row_oid_len=%u,value=%p,value_len=%lu)", *column, *row_oid, row_oid_len, value, *value_len);

	if (!snmp_oid_in_range(row_oid, row_oid_len,
				fan_table_oid_ranges,
				LWIP_ARRAYSIZE(fan_table_oid_ranges)))
		return SNMP_ERR_NOSUCHINSTANCE;

	return fan_table_get(column, row_oid, value, value_len);
}

static snmp_err_t fan_table_get_next_cell_inst_and_val(const u32_t* column, struct snmp_obj_id* row_oid,
						union snmp_variant_value* value, u32_t* value_len)
{
	struct snmp_next_oid_state state;
	u32_t result_temp[LWIP_ARRAYSIZE(fan_table_oid_ranges)];

	//log_msg(LOG_INFO, "fan_table_get_next(column=%lu,row_oid={%u:%lu},value=%p,value_len=%lu)", *column, row_oid->len, row_oid->id[0], value, *value_len);

	snmp_next_oid_init(&state, row_oid->id, row_oid->len, result_temp, LWIP_ARRAYSIZE(fan_table_oid_ranges));

	for (u32_t i = 1; i <= FAN_COUNT; i++) {
		u32_t test_oid[LWIP_ARRAYSIZE(fan_table_oid_ranges)];
		test_oid[0] = i;
		snmp_next_oid_check(&state, test_oid, LWIP_ARRAYSIZE(fan_table_oid_ranges), (void*)i);
	}

	if (state.status == SNMP_NEXT_OID_STATUS_SUCCESS) {
		snmp_oid_assign(row_oid, state.next_oid, state.next_oid_len);
		return fan_table_get(column, (const u32_t*)&state.reference, value, value_len);
	}

	return SNMP_ERR_NOSUCHINSTANCE;
}


static snmp_err_t mbfan_table_get(const u32_t* column, const u32_t* row,
				union snmp_variant_value* value, u32_t* value_len)
{
	const struct fanpico_state *st = fanpico_state;
	int i = *row - 1;
	double rpm;


	if (i < 0 || i >= MBFAN_COUNT)
		return SNMP_ERR_NOSUCHINSTANCE;

	switch (*column) {
	case 1: // id
		value->u32 = *row;
		break;
	case 2: // name
		value->const_ptr = (const void*)cfg->mbfans[i].name;
		*value_len = strlen(cfg->mbfans[i].name);
		break;
	case 3: //rpm
		rpm = st->mbfan_freq[i] * 60 / cfg->mbfans[i].rpm_factor;
		value->u32 = rpm;
		break;
	case 4: // frequency
		value->u32 = round_decimal(st->mbfan_freq[i], 2) * 100;
		break;
	case 5: // duty_cycle
		value->u32 = round_decimal(st->mbfan_duty[i], 0);
		break;
	default:
		return SNMP_ERR_NOSUCHINSTANCE;
	}

	return SNMP_ERR_NOERROR;
}

static snmp_err_t mbfan_table_get_cell_val(const u32_t* column, const u32_t* row_oid, u8_t row_oid_len,
				union snmp_variant_value* value, u32_t* value_len)
{
	log_msg(LOG_INFO, "mbfan_table_get_cell_val(column=%lu,row_oid=%lu,row_oid_len=%u,value=%p,value_len=%lu)", *column, *row_oid, row_oid_len, value, *value_len);

	if (!snmp_oid_in_range(row_oid, row_oid_len,
				mbfan_table_oid_ranges,
				LWIP_ARRAYSIZE(mbfan_table_oid_ranges)))
		return SNMP_ERR_NOSUCHINSTANCE;

	return mbfan_table_get(column, row_oid, value, value_len);
}

static snmp_err_t mbfan_table_get_next_cell_inst_and_val(const u32_t* column, struct snmp_obj_id* row_oid,
						union snmp_variant_value* value, u32_t* value_len)
{
	struct snmp_next_oid_state state;
	u32_t result_temp[LWIP_ARRAYSIZE(mbfan_table_oid_ranges)];

	//log_msg(LOG_INFO, "fan_table_get_next(column=%lu,row_oid={%u:%lu},value=%p,value_len=%lu)", *column, row_oid->len, row_oid->id[0], value, *value_len);

	snmp_next_oid_init(&state, row_oid->id, row_oid->len, result_temp, LWIP_ARRAYSIZE(mbfan_table_oid_ranges));

	for (u32_t i = 1; i <= MBFAN_COUNT; i++) {
		u32_t test_oid[LWIP_ARRAYSIZE(mbfan_table_oid_ranges)];
		test_oid[0] = i;
		snmp_next_oid_check(&state, test_oid, LWIP_ARRAYSIZE(mbfan_table_oid_ranges), (void*)i);
	}

	if (state.status == SNMP_NEXT_OID_STATUS_SUCCESS) {
		snmp_oid_assign(row_oid, state.next_oid, state.next_oid_len);
		return mbfan_table_get(column, (const u32_t*)&state.reference, value, value_len);
	}

	return SNMP_ERR_NOSUCHINSTANCE;
}



static snmp_err_t sensor_table_get(const u32_t* column, const u32_t* row,
				union snmp_variant_value* value, u32_t* value_len)
{
	const struct fanpico_state *st = fanpico_state;
	int i = *row - 1;


	if (i < 0 || i >= SENSOR_COUNT)
		return SNMP_ERR_NOSUCHINSTANCE;

	switch (*column) {
	case 1: // id
		value->u32 = *row;
		break;
	case 2: // name
		value->const_ptr = (const void*)cfg->sensors[i].name;
		*value_len = strlen(cfg->sensors[i].name);
		break;
	case 3: // temperature
		value->s32 = round_decimal(st->temp[i], 1) * 10;
		break;
	case 4: // duty_cycle
		double pwm = sensor_get_duty(&cfg->sensors[i].map, st->temp[i]);
		value->u32 = round_decimal(pwm, 0);
		break;
	default:
		return SNMP_ERR_NOSUCHINSTANCE;
	}

	return SNMP_ERR_NOERROR;
}

static snmp_err_t sensor_table_get_cell_val(const u32_t* column, const u32_t* row_oid, u8_t row_oid_len,
				union snmp_variant_value* value, u32_t* value_len)
{
	log_msg(LOG_INFO, "mbfan_table_get_cell_val(column=%lu,row_oid=%lu,row_oid_len=%u,value=%p,value_len=%lu)", *column, *row_oid, row_oid_len, value, *value_len);

	if (!snmp_oid_in_range(row_oid, row_oid_len,
				sensor_table_oid_ranges,
				LWIP_ARRAYSIZE(sensor_table_oid_ranges)))
		return SNMP_ERR_NOSUCHINSTANCE;

	return sensor_table_get(column, row_oid, value, value_len);
}

static snmp_err_t sensor_table_get_next_cell_inst_and_val(const u32_t* column, struct snmp_obj_id* row_oid,
						union snmp_variant_value* value, u32_t* value_len)
{
	struct snmp_next_oid_state state;
	u32_t result_temp[LWIP_ARRAYSIZE(sensor_table_oid_ranges)];


	snmp_next_oid_init(&state, row_oid->id, row_oid->len, result_temp, LWIP_ARRAYSIZE(sensor_table_oid_ranges));

	for (u32_t i = 1; i <= SENSOR_COUNT; i++) {
		u32_t test_oid[LWIP_ARRAYSIZE(sensor_table_oid_ranges)];
		test_oid[0] = i;
		snmp_next_oid_check(&state, test_oid, LWIP_ARRAYSIZE(sensor_table_oid_ranges), (void*)i);
	}

	if (state.status == SNMP_NEXT_OID_STATUS_SUCCESS) {
		snmp_oid_assign(row_oid, state.next_oid, state.next_oid_len);
		return sensor_table_get(column, (const u32_t*)&state.reference, value, value_len);
	}

	return SNMP_ERR_NOSUCHINSTANCE;
}


static snmp_err_t vsensor_table_get(const u32_t* column, const u32_t* row,
				union snmp_variant_value* value, u32_t* value_len)
{
	const struct fanpico_state *st = fanpico_state;
	int i = *row - 1;


	if (i < 0 || i >= VSENSOR_COUNT)
		return SNMP_ERR_NOSUCHINSTANCE;

	switch (*column) {
	case 1: // id
		value->u32 = *row;
		break;
	case 2: // name
		value->const_ptr = (const void*)cfg->vsensors[i].name;
		*value_len = strlen(cfg->vsensors[i].name);
		break;
	case 3: // temperature
		value->s32 = round_decimal(st->vtemp[i], 1) * 10;
		break;
	case 4: // humidity
		value->u32 = round_decimal(st->vhumidity[i],1) * 10;
		break;
	case 5: // pressure
		value->u32 = round_decimal(st->vpressure[i], 1) * 10;
		break;
	case 6: // duty_cycle
		double pwm = sensor_get_duty(&cfg->vsensors[i].map, st->vtemp[i]);
		value->u32 = round_decimal(pwm, 0);
		break;
	default:
		return SNMP_ERR_NOSUCHINSTANCE;
	}

	return SNMP_ERR_NOERROR;
}

static snmp_err_t vsensor_table_get_cell_val(const u32_t* column, const u32_t* row_oid, u8_t row_oid_len,
				union snmp_variant_value* value, u32_t* value_len)
{
	log_msg(LOG_INFO, "mbfan_table_get_cell_val(column=%lu,row_oid=%lu,row_oid_len=%u,value=%p,value_len=%lu)", *column, *row_oid, row_oid_len, value, *value_len);

	if (!snmp_oid_in_range(row_oid, row_oid_len,
				vsensor_table_oid_ranges,
				LWIP_ARRAYSIZE(vsensor_table_oid_ranges)))
		return SNMP_ERR_NOSUCHINSTANCE;

	return vsensor_table_get(column, row_oid, value, value_len);
}

static snmp_err_t vsensor_table_get_next_cell_inst_and_val(const u32_t* column, struct snmp_obj_id* row_oid,
						union snmp_variant_value* value, u32_t* value_len)
{
	struct snmp_next_oid_state state;
	u32_t result_temp[LWIP_ARRAYSIZE(vsensor_table_oid_ranges)];


	snmp_next_oid_init(&state, row_oid->id, row_oid->len, result_temp, LWIP_ARRAYSIZE(vsensor_table_oid_ranges));

	for (u32_t i = 1; i <= VSENSOR_COUNT; i++) {
		u32_t test_oid[LWIP_ARRAYSIZE(vsensor_table_oid_ranges)];
		test_oid[0] = i;
		snmp_next_oid_check(&state, test_oid, LWIP_ARRAYSIZE(vsensor_table_oid_ranges), (void*)i);
	}

	if (state.status == SNMP_NEXT_OID_STATUS_SUCCESS) {
		snmp_oid_assign(row_oid, state.next_oid, state.next_oid_len);
		return vsensor_table_get(column, (const u32_t*)&state.reference, value, value_len);
	}

	return SNMP_ERR_NOSUCHINSTANCE;
}




/* Fan Group */
static const struct snmp_scalar_node fan_count = SNMP_SCALAR_CREATE_NODE_READONLY(
	1, SNMP_ASN1_TYPE_INTEGER, get_fan_count);

static const struct snmp_table_simple_col_def fan_table_cols[] = {
	{ 1, SNMP_ASN1_TYPE_INTEGER,      SNMP_VARIANT_VALUE_TYPE_U32       }, // id
	{ 2, SNMP_ASN1_TYPE_OCTET_STRING, SNMP_VARIANT_VALUE_TYPE_CONST_PTR }, // name
	{ 3, SNMP_ASN1_TYPE_UNSIGNED32,   SNMP_VARIANT_VALUE_TYPE_U32       }, // rpm
	{ 4, SNMP_ASN1_TYPE_UNSIGNED32,   SNMP_VARIANT_VALUE_TYPE_U32       }, // frequency
	{ 5, SNMP_ASN1_TYPE_UNSIGNED32,   SNMP_VARIANT_VALUE_TYPE_U32       }, // duty_cycle
};
static const struct snmp_table_simple_node fan_table = SNMP_TABLE_CREATE_SIMPLE(
	2, fan_table_cols,
	fan_table_get_cell_val,
	fan_table_get_next_cell_inst_and_val);


/* MBFan Group */
static const struct snmp_scalar_node mbfan_count = SNMP_SCALAR_CREATE_NODE_READONLY(
	3, SNMP_ASN1_TYPE_INTEGER, get_mbfan_count);

static const struct snmp_table_simple_node mbfan_table = SNMP_TABLE_CREATE_SIMPLE(
	4, fan_table_cols,
	mbfan_table_get_cell_val,
	mbfan_table_get_next_cell_inst_and_val);


/* Sensor Group */
static const struct snmp_scalar_node sensor_count = SNMP_SCALAR_CREATE_NODE_READONLY(
	5, SNMP_ASN1_TYPE_INTEGER, get_sensor_count);

static const struct snmp_table_simple_col_def sensor_table_cols[] = {
	{ 1, SNMP_ASN1_TYPE_INTEGER,      SNMP_VARIANT_VALUE_TYPE_U32       }, // id
	{ 2, SNMP_ASN1_TYPE_OCTET_STRING, SNMP_VARIANT_VALUE_TYPE_CONST_PTR }, // name
	{ 3, SNMP_ASN1_TYPE_INTEGER,      SNMP_VARIANT_VALUE_TYPE_S32       }, // temperature
	{ 4, SNMP_ASN1_TYPE_UNSIGNED32,   SNMP_VARIANT_VALUE_TYPE_U32       }, // duty_cycle
};
static const struct snmp_table_simple_node sensor_table = SNMP_TABLE_CREATE_SIMPLE(
	6, sensor_table_cols,
	sensor_table_get_cell_val,
	sensor_table_get_next_cell_inst_and_val);


/* Vsendor Group */
static const struct snmp_scalar_node vsensor_count = SNMP_SCALAR_CREATE_NODE_READONLY(
	7, SNMP_ASN1_TYPE_INTEGER, get_vsensor_count);

static const struct snmp_table_simple_col_def vsensor_table_cols[] = {
	{ 1, SNMP_ASN1_TYPE_INTEGER,      SNMP_VARIANT_VALUE_TYPE_U32       }, // id
	{ 2, SNMP_ASN1_TYPE_OCTET_STRING, SNMP_VARIANT_VALUE_TYPE_CONST_PTR }, // name
	{ 3, SNMP_ASN1_TYPE_INTEGER,      SNMP_VARIANT_VALUE_TYPE_S32       }, // temperature
	{ 4, SNMP_ASN1_TYPE_UNSIGNED32,   SNMP_VARIANT_VALUE_TYPE_U32       }, // humidity
	{ 5, SNMP_ASN1_TYPE_UNSIGNED32,   SNMP_VARIANT_VALUE_TYPE_U32       }, // pressure
	{ 6, SNMP_ASN1_TYPE_UNSIGNED32,   SNMP_VARIANT_VALUE_TYPE_U32       }, // duty_cycle
};
static const struct snmp_table_simple_node vsensor_table = SNMP_TABLE_CREATE_SIMPLE(
	8, vsensor_table_cols,
	vsensor_table_get_cell_val,
	vsensor_table_get_next_cell_inst_and_val);


static const struct snmp_node* const fanpico_object_nodes[] = {
	&fan_count.node.node,
	&fan_table.node.node,
	&mbfan_count.node.node,
	&mbfan_table.node.node,
	&sensor_count.node.node,
	&sensor_table.node.node,
	&vsensor_count.node.node,
	&vsensor_table.node.node,
};
static const struct snmp_tree_node fanpico_object_root = SNMP_CREATE_TREE_NODE(1, fanpico_object_nodes);


static const struct snmp_node* const fanpico_mib_nodes[] = {
	&fanpico_object_root.node,
};
static const struct snmp_tree_node fanpico_mib_root = SNMP_CREATE_TREE_NODE(1, fanpico_mib_nodes);

static const struct snmp_mib fanpico_mib = {
	fanpico_base_oid.id,
	fanpico_base_oid.len,
	&fanpico_mib_root.node
};

static const struct snmp_mib *mibs[] = {
	&mib2,
	&fanpico_mib,
};


void fanpico_snmp_init()
{
	snmp_set_device_enterprise_oid(&fanpico_base_oid);

	snmp_set_community(cfg->snmp_community);
	snmp_set_community_write(cfg->snmp_community_write);

	snmp_mib2_set_sysdescr((uint8_t*)"FanPico-" FANPICO_MODEL " Fan Controller v"
			FANPICO_VERSION FANPICO_BUILD_TAG, NULL);
	snmp_mib2_set_syscontact_readonly((const uint8_t*)cfg->snmp_contact, NULL);
	snmp_mib2_set_syslocation_readonly((const uint8_t*)cfg->snmp_location, NULL);
	snmp_mib2_set_sysname_readonly((const uint8_t*)cfg->name, NULL);

	snmp_set_mibs(mibs, LWIP_ARRAYSIZE(mibs));
	snmp_init();
}

#endif /* WIFI_SUPPORT */
