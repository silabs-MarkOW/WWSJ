/***************************************************************************//**
 * @file
 * @brief Core application logic.
 *******************************************************************************
 * # License
 * <b>Copyright 2020 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * SPDX-License-Identifier: Zlib
 *
 * The licensor of this software is Silicon Laboratories Inc.
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 *
 ******************************************************************************/
#include "em_common.h"
#include "app_assert.h"
#include "sl_bluetooth.h"
#include "app.h"
#include "gatt_db.h"
#ifdef SL_CATALOG_APP_LOG_PRESENT
#  include "app_log.h"
#else
#  define app_log(x,...) (void)x
#endif

// The advertising set handle allocated from Bluetooth stack.
static uint8_t advertising_set_handle = 0xff;
static uint8_t conn = 0xff;
static uint8_t address; // response slot

static struct legacyAdv_s {
  uint8_t length;
  uint8_t payload[31];
} legacyMain, legacyScan;

static void adv_init(struct legacyAdv_s *adv) {
  adv->length = 0;
}

static void adv_add_flags(struct legacyAdv_s *adv, uint8_t flags) {
  adv->payload[adv->length++] = 2; // flag length = 2
  adv->payload[adv->length++] = 1; // flags AdType = 1
  adv->payload[adv->length++] = flags;
}

static void adv_add_name(struct legacyAdv_s *adv) { // Use Device Name in GATT
  sl_status_t sc;
  size_t max_length, length;
  max_length = sizeof(adv->payload) - adv->length - 2;
  sc = sl_bt_gatt_server_read_attribute_value(gattdb_device_name, 0, max_length, &length, &adv->payload[adv->length+2]);
  app_assert_status(sc);
  adv->payload[adv->length++] = 2 + length;
  adv->payload[adv->length++] = 0x09; // complete name
  adv->length += length;
}

static void adv_add_manufacturer_data(struct legacyAdv_s *adv, uint16_t id, uint16_t address_handle, uint16_t name_handle, uint8_t length, uint8_t *data) { // Use Device Name in GATT
  adv->payload[adv->length++] = length + 5;
  adv->payload[adv->length++] = 0xff;
  memcpy(&adv->payload[adv->length],&id, 2);
  adv->length += 2;
  memcpy(&adv->payload[adv->length],&address_handle, 2);
  adv->length += 2;
  memcpy(&adv->payload[adv->length],&name_handle, 2);
  adv->length += 2;
  memcpy(&adv->payload[adv->length],data, length);
  adv->length += length;
}

void start_advertising(void) {
  sl_status_t sc;
  // Start advertising and enable connections.
  sc = sl_bt_legacy_advertiser_start(advertising_set_handle,
                                     sl_bt_legacy_advertiser_connectable);
  app_assert_status(sc);
}
/**************************************************************************//**
 * Application Init.
 *****************************************************************************/
void app_init(void)
{
  app_log("app_init()\r\n");
}

/**************************************************************************//**
 * Application Process Action.
 *****************************************************************************/
SL_WEAK void app_process_action(void)
{
  /////////////////////////////////////////////////////////////////////////////
  // Put your additional application code here!                              //
  // This is called infinitely.                                              //
  // Do not call blocking functions from here!                               //
  /////////////////////////////////////////////////////////////////////////////
}

/**************************************************************************//**
 * Bluetooth stack event handler.
 * This overrides the dummy weak implementation.
 *
 * @param[in] evt Event coming from the Bluetooth stack.
 *****************************************************************************/
void sl_bt_on_event(sl_bt_msg_t *evt)
{
  sl_status_t sc;

  switch (SL_BT_MSG_ID(evt->header)) {
    // -------------------------------
    // This event indicates the device has started and the radio is ready.
    // Do not call any stack command before receiving this boot event!
    case sl_bt_evt_system_boot_id:

      // set parameter to receive sync
      sc = sl_bt_past_receiver_set_default_sync_receive_parameters(
        sl_bt_past_receiver_mode_synchronize,
        0,
        1000,
        sl_bt_sync_report_all);
      app_assert_status(sc);

      // Create an advertising set.
      sc = sl_bt_advertiser_create_set(&advertising_set_handle);
      app_assert_status(sc);

      // initialize main and scan response advertisements payloads
      adv_init(&legacyMain);
      adv_init(&legacyScan);
      adv_add_flags(&legacyMain, 6);
      adv_add_name(&legacyMain); // place Device Name in main advertisement
      adv_add_manufacturer_data(&legacyScan,
                                0x2ff, //Silicon Labs' Company ID
                                gattdb_address, // GATT handle of address ... avoid discovery and random UUID
                                gattdb_device_name, // GATT handle of device name
                                14, (void*)"WWSJ PAwR Demo"); // magic number to identify devices

      // Set data for advertising
      sc = sl_bt_legacy_advertiser_set_data(advertising_set_handle, 0, legacyMain.length, legacyMain.payload);
      app_assert_status(sc);
      sc = sl_bt_legacy_advertiser_set_data(advertising_set_handle, 1, legacyScan.length, legacyScan.payload);
      app_assert_status(sc);

      // Set advertising interval to 100ms.
      sc = sl_bt_advertiser_set_timing(
        advertising_set_handle,
        160, // min. adv. interval (milliseconds * 1.6)
        160, // max. adv. interval (milliseconds * 1.6)
        0,   // adv. duration
        0);  // max. num. adv. events
      app_assert_status(sc);

      start_advertising();
      break;

    // -------------------------------
    // This event indicates that a new connection was opened.
    case sl_bt_evt_connection_opened_id:
#define ED evt->data.evt_connection_opened
      app_log("evt_connection_opened\r\n");
      conn = ED.connection;
      break;
#undef ED

    // -------------------------------
    // This event indicates that a connection was closed.
    case sl_bt_evt_connection_closed_id:
      app_log("evt_connection_closed\r\n");
      break;

    case sl_bt_evt_pawr_sync_transfer_received_id:
      app_log("evt_pawr_sync_transfer_received\r\n");
      sc = sl_bt_connection_close(conn);
      app_assert_status(sc);
      break;

#undef sl_bt_pawr_sync_set_response_data
    case sl_bt_evt_pawr_sync_subevent_report_id: {
      uint32_t uptime =  sl_sleeptimer_get_tick_count();
      sc = sl_bt_pawr_sync_set_response_data(
        evt->data.evt_pawr_sync_subevent_report.sync,
        evt->data.evt_pawr_sync_subevent_report.event_counter,
        evt->data.evt_pawr_sync_subevent_report.subevent,
        evt->data.evt_pawr_sync_subevent_report.subevent,
        address,
        4,
        (uint8_t*)&uptime);
      }
      break;

    case sl_bt_evt_sync_closed_id:
      app_log("evt_sync_closed\r\n");
      start_advertising();
      break;

    case sl_bt_evt_gatt_server_user_write_request_id:
#define ED evt->data.evt_gatt_server_user_write_request
      address = ED.value.data[0];
      app_log("GATT write address: %d\r\n",address);
      sc = sl_bt_gatt_server_send_user_write_response(ED.connection, ED.characteristic, 0);
      app_assert_status(sc);
      break;
#undef ED

    ///////////////////////////////////////////////////////////////////////////
    // Add additional event handlers here as your application requires!      //
    ///////////////////////////////////////////////////////////////////////////

    // -------------------------------
    // Default event handler.
    default:
      app_log("Unhandled event:");
      for(size_t i = 0; i < 4; i++) app_log(" %02x",((uint8_t*)&evt->header)[i]);
      for(size_t i = 0; i < SL_BT_MSG_LEN(evt->header); i++) app_log(" %02x",((uint8_t*)&evt->data)[i]);
      app_log("\r\n");
      break;
  }
}
