/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    App/app_ble.c
  * @author  MCD Application Team
  * @brief   BLE Application
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2023 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"

#include "app_common.h"

#include "dbg_trace.h"

#include "ble.h"
#include "tl.h"
#include "app_ble.h"

#include "stm32_seq.h"
#include "shci.h"
#include "stm32_lpm.h"
#include "otp.h"

#include "p2p_client_app.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/

/**
 * security parameters structure
 */
typedef struct _tSecurityParams
{
  /**
   * IO capability of the device
   */
  uint8_t ioCapability;

  /**
   * Authentication requirement of the device
   * Man In the Middle protection required?
   */
  uint8_t mitm_mode;

  /**
   * bonding mode of the device
   */
  uint8_t bonding_mode;

  /**
   * this variable indicates whether to use a fixed pin
   * during the pairing process or a passkey has to be
   * requested to the application during the pairing process
   * 0 implies use fixed pin and 1 implies request for passkey
   */
  uint8_t Use_Fixed_Pin;

  /**
   * minimum encryption key size requirement
   */
  uint8_t encryptionKeySizeMin;

  /**
   * maximum encryption key size requirement
   */
  uint8_t encryptionKeySizeMax;

  /**
   * fixed pin to be used in the pairing process if
   * Use_Fixed_Pin is set to 1
   */
  uint32_t Fixed_Pin;

  /**
   * this flag indicates whether the host has to initiate
   * the security, wait for pairing or does not have any security
   * requirements.
   * 0x00 : no security required
   * 0x01 : host should initiate security by sending the slave security
   *        request command
   * 0x02 : host need not send the clave security request but it
   * has to wait for paiirng to complete before doing any other
   * processing
   */
  uint8_t initiateSecurity;
} tSecurityParams;

/**
 * global context
 * contains the variables common to all
 * services
 */
typedef struct _tBLEProfileGlobalContext
{
  /**
   * security requirements of the host
   */
  tSecurityParams bleSecurityParam;

  /**
   * gap service handle
   */
  uint16_t gapServiceHandle;

  /**
   * device name characteristic handle
   */
  uint16_t devNameCharHandle;

  /**
   * appearance characteristic handle
   */
  uint16_t appearanceCharHandle;

  /**
   * connection handle of the current active connection
   * When not in connection, the handle is set to 0xFFFF
   */
  uint16_t connectionHandle;

  /**
   * length of the UUID list to be used while advertising
   */
  uint8_t advtServUUIDlen;

  /**
   * the UUID list to be used while advertising
   */
  uint8_t advtServUUID[100];
} BleGlobalContext_t;

typedef struct
{
  BleGlobalContext_t BleApplicationContext_legacy;
  APP_BLE_ConnStatus_t Device_Connection_Status;
  uint8_t SwitchOffGPIO_timer_Id;
  uint8_t DeviceServerFound;
} BleApplicationContext_t;

#if OOB_DEMO != 0
typedef struct
{
  uint8_t  Identifier;
  uint16_t L2CAP_Length;
  uint16_t Interval_Min;
  uint16_t Interval_Max;
  uint16_t Slave_Latency;
  uint16_t Timeout_Multiplier;
} APP_BLE_p2p_Conn_Update_req_t;
#endif

/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private defines -----------------------------------------------------------*/
#define APPBLE_GAP_DEVICE_NAME_LENGTH 7
#define BD_ADDR_SIZE_LOCAL    6

/* USER CODE BEGIN PD */
#if OOB_DEMO != 0 
#define LED_ON_TIMEOUT            (0.005*1000*1000/CFG_TS_TICK_VAL) /**< 5ms */
#endif 
/* USER CODE END PD */

/* Private macros ------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
PLACE_IN_SECTION("MB_MEM1") ALIGN(4) static TL_CmdPacket_t BleCmdBuffer;

static const uint8_t M_bd_addr[BD_ADDR_SIZE_LOCAL] =
{
  (uint8_t)((CFG_ADV_BD_ADDRESS & 0x0000000000FF)),
  (uint8_t)((CFG_ADV_BD_ADDRESS & 0x00000000FF00) >> 8),
  (uint8_t)((CFG_ADV_BD_ADDRESS & 0x000000FF0000) >> 16),
  (uint8_t)((CFG_ADV_BD_ADDRESS & 0x0000FF000000) >> 24),
  (uint8_t)((CFG_ADV_BD_ADDRESS & 0x00FF00000000) >> 32),
  (uint8_t)((CFG_ADV_BD_ADDRESS & 0xFF0000000000) >> 40)
};

static uint8_t bd_addr_udn[BD_ADDR_SIZE_LOCAL];
/**
*   Identity root key used to derive LTK and CSRK
*/
static const uint8_t BLE_CFG_IR_VALUE[16] = CFG_BLE_IRK;

/**
* Encryption root key used to derive LTK and CSRK
*/
static const uint8_t BLE_CFG_ER_VALUE[16] = CFG_BLE_ERK;

tBDAddr SERVER_REMOTE_BDADDR;
uint8_t SERVER_REMOTE_ADDR_TYPE;

P2PC_APP_ConnHandle_Not_evt_t handleNotification;

static BleApplicationContext_t BleApplicationContext;

#if OOB_DEMO != 0
APP_BLE_p2p_Conn_Update_req_t APP_BLE_p2p_Conn_Update_req;
#endif

/* USER CODE BEGIN PV */
extern UART_HandleTypeDef hlpuart1;

static app_ble_oob_data_t m_oob_uart_data;
static app_ble_server_data_t m_oob_remote_data;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
static void BLE_UserEvtRx(void * pPayload);
static void BLE_StatusNot(HCI_TL_CmdStatus_t status);
static void Ble_Tl_Init(void);
static void Ble_Hci_Gap_Gatt_Init(void);
static const uint8_t* BleGetBdAddress(void);
static void Scan_Request(void);
static void Connect_Request(void);
static void Switch_OFF_GPIO(void);

/* USER CODE BEGIN PFP */
static void uart_rx_callback (void); // [STM]
static void ble_oob_process (void); // [STM]
static void ble_client_pairing_request(void); // [STM]
static void send_oob_data(void);
static void ble_oob_data_log (uint32_t size, uint8_t *p_data); // [STM]
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/* Functions Definition ------------------------------------------------------*/
void APP_BLE_Init(void)
{
  SHCI_CmdStatus_t status;
  tBleStatus ret = BLE_STATUS_INVALID_PARAMS;
  /* USER CODE BEGIN APP_BLE_Init_1 */

  /* USER CODE END APP_BLE_Init_1 */

  SHCI_C2_Ble_Init_Cmd_Packet_t ble_init_cmd_packet =
  {
    {{0,0,0}},                          /**< Header unused */
    {0,                                 /** pBleBufferAddress not used */
     0,                                 /** BleBufferSize not used */
     CFG_BLE_NUM_GATT_ATTRIBUTES,
     CFG_BLE_NUM_GATT_SERVICES,
     CFG_BLE_ATT_VALUE_ARRAY_SIZE,
     CFG_BLE_NUM_LINK,
     CFG_BLE_DATA_LENGTH_EXTENSION,
     CFG_BLE_PREPARE_WRITE_LIST_SIZE,
     CFG_BLE_MBLOCK_COUNT,
     CFG_BLE_MAX_ATT_MTU,
     CFG_BLE_SLAVE_SCA,
     CFG_BLE_MASTER_SCA,
     CFG_BLE_LS_SOURCE,
     CFG_BLE_MAX_CONN_EVENT_LENGTH,
     CFG_BLE_HSE_STARTUP_TIME,
     CFG_BLE_VITERBI_MODE,
     CFG_BLE_OPTIONS,
     0,
     CFG_BLE_MAX_COC_INITIATOR_NBR,
     CFG_BLE_MIN_TX_POWER,
     CFG_BLE_MAX_TX_POWER,
     CFG_BLE_RX_MODEL_CONFIG,
     CFG_BLE_MAX_ADV_SET_NBR,
     CFG_BLE_MAX_ADV_DATA_LEN,
     CFG_BLE_TX_PATH_COMPENS,
     CFG_BLE_RX_PATH_COMPENS,
     CFG_BLE_CORE_VERSION,
     CFG_BLE_OPTIONS_EXT
    }
  };

  /**
   * Initialize Ble Transport Layer
   */
  Ble_Tl_Init();

  /**
   * Do not allow standby in the application
   */
  UTIL_LPM_SetOffMode(1 << CFG_LPM_APP_BLE, UTIL_LPM_DISABLE);

  /**
   * Register the hci transport layer to handle BLE User Asynchronous Events
   */
  UTIL_SEQ_RegTask(1<<CFG_TASK_HCI_ASYNCH_EVT_ID, UTIL_SEQ_RFU, hci_user_evt_proc);

  /**
   * Starts the BLE Stack on CPU2
   */
  status = SHCI_C2_BLE_Init(&ble_init_cmd_packet);
  if (status != SHCI_Success)
  {
    APP_DBG_MSG("  Fail   : SHCI_C2_BLE_Init command, result: 0x%02x\n\r", status);
    /* if you are here, maybe CPU2 doesn't contain STM32WB_Copro_Wireless_Binaries, see Release_Notes.html */
    Error_Handler();
  }
  else
  {
    APP_DBG_MSG("  Success: SHCI_C2_BLE_Init command\n\r");
  }

  /**
   * Initialization of HCI & GATT & GAP layer
   */
  Ble_Hci_Gap_Gatt_Init();

  /**
   * Initialization of the BLE Services
   */
  SVCCTL_Init();

  /**
   * From here, all initialization are BLE application specific
   */
  UTIL_SEQ_RegTask(1<<CFG_TASK_START_SCAN_ID, UTIL_SEQ_RFU, Scan_Request);
  UTIL_SEQ_RegTask(1<<CFG_TASK_CONN_DEV_1_ID, UTIL_SEQ_RFU, Connect_Request);

  /**
   * Initialization of the BLE App Context
   */
  BleApplicationContext.Device_Connection_Status = APP_BLE_IDLE;

  /*Radio mask Activity*/
#if (OOB_DEMO != 0)
  ret = aci_hal_set_radio_activity_mask(0x0020);
  if (ret != BLE_STATUS_SUCCESS)
  {
    APP_DBG_MSG("  Fail   : aci_hal_set_radio_activity_mask command, result: 0x%x \n\r", ret);
  }
  else
  {
    APP_DBG_MSG("  Success: aci_hal_set_radio_activity_mask command\n\r");
  }
  APP_DBG_MSG("\n");
#endif
  /**
   * Initialize P2P Client Application
   */
  P2PC_APP_Init();

  /* USER CODE BEGIN APP_BLE_Init_3 */
  UTIL_SEQ_RegTask(1<<CFG_TASK_SEND_OOB_DATA_ID, UTIL_SEQ_RFU, send_oob_data); // [STM]
  
  uint8_t events[8]={0x9F,0x01,0x00,0x00,0x00,0x00,0x00,0x00};
  // [STM] LE read local P-256 public key complete event + LE generate DHKey complete event + and other events (check AN5270)
  
  HAL_GPIO_WritePin(LED_RED_GPIO_Port, LED_RED_Pin, GPIO_PIN_SET);
  
  ret = hci_le_set_event_mask(events);
  ret |= hci_le_read_local_p256_public_key();
  if(ret == BLE_STATUS_SUCCESS)
  {
    APP_DBG_MSG("  Success: hci_le_set_event_mask and hci_le_read_local_p256_public_key command.\n");
    APP_DBG_MSG("  Waiting for HCI_LE_READ_LOCAL_P256_PUBLIC_KEY_COMPLETE_EVENT\n");
    UTIL_SEQ_WaitEvt(1 << CFG_WAIT_EVT_READ_LOCAL_P256_PUBLIC_KEY_ID);
    
    // [STM] Generate OOB data
    ret = aci_gap_set_oob_data(0x00, // OOB device type = local device
                               0x00, 
                               0x00,
                               0x00,
                               0x00,
                               0x00);
    if(ret != 0)
    {
      HAL_GPIO_WritePin(LED_RED_GPIO_Port, LED_RED_Pin, GPIO_PIN_SET);
      APP_DBG_MSG("  Fail   : aci_gap_set_oob_data, result: 0x%x\n", ret);
    }
    else{
      APP_DBG_MSG("  Success: aci_gap_set_oob_data\n");
    }
    
    ret = aci_gap_get_oob_data(0x00, &m_oob_uart_data.address_type, m_oob_uart_data.address, &m_oob_uart_data.tk_size, m_oob_uart_data.tk);
    if(ret != 0)
    {
      HAL_GPIO_WritePin(LED_RED_GPIO_Port, LED_RED_Pin, GPIO_PIN_SET);
      APP_DBG_MSG("  Fail   : aci_gap_get_oob_data, result: 0x%x\n", ret);
    }
    else{
      APP_DBG_MSG("  Success: aci_gap_get_oob_data\r\n");
      APP_DBG_MSG("\r\n BLE Client address (type %s) %02X:%02X:%02X:%02X:%02X:%02X\r\n",
              m_oob_uart_data.address_type == 0 ? "Public" : "Random",
              m_oob_uart_data.address[5], m_oob_uart_data.address[4], m_oob_uart_data.address[3],
              m_oob_uart_data.address[2], m_oob_uart_data.address[1], m_oob_uart_data.address[0]);
      ble_oob_data_log(m_oob_uart_data.tk_size,m_oob_uart_data.tk);
      APP_DBG_MSG(" = Press SW1 to send OOB data via UART to BLE Server =\r\n\r\n");
    }
  }
  else
  {
    APP_DBG_MSG("  Fail   : hci_le_set_event_mask and hci_le_read_local_p256_public_key command, result: 0x%x\n", ret);
  }
  /* USER CODE END APP_BLE_Init_3 */

#if (OOB_DEMO != 0)
  HW_TS_Create(CFG_TIM_PROC_ID_ISR, &(BleApplicationContext.SwitchOffGPIO_timer_Id), hw_ts_SingleShot, Switch_OFF_GPIO);
#endif

#if (OOB_DEMO == 0)
  /**
   * Start scanning
   */
  UTIL_SEQ_SetTask(1 << CFG_TASK_START_SCAN_ID, CFG_SCH_PRIO_0);
#endif
  /* USER CODE BEGIN APP_BLE_Init_2 */
  UTIL_SEQ_RegTask(1<<CFG_TASK_OOB_PROCESS_ID, UTIL_SEQ_RFU, ble_oob_process);
  UTIL_SEQ_RegTask(1<<CFG_TASK_BLE_CLIENT_PAIRING_REQUEST_ID, UTIL_SEQ_RFU, ble_client_pairing_request);
  
  HW_UART_Receive_IT(hw_uart1, (uint8_t *)&m_oob_remote_data, sizeof(m_oob_remote_data), uart_rx_callback);
  /* USER CODE END APP_BLE_Init_2 */
  return;
}

SVCCTL_UserEvtFlowStatus_t SVCCTL_App_Notification(void *pckt)
{
  hci_event_pckt *event_pckt;
  evt_le_meta_event *meta_evt;
  hci_le_connection_complete_event_rp0 * connection_complete_event;
  evt_blecore_aci *blecore_evt;
  hci_le_advertising_report_event_rp0 * le_advertising_event;
  event_pckt = (hci_event_pckt*) ((hci_uart_pckt *) pckt)->data;
  hci_disconnection_complete_event_rp0 *cc = (void *) event_pckt->data;
  uint8_t result;
  uint8_t event_type, event_data_size;
  int k = 0;
  uint8_t adtype, adlength;
#if (OOB_DEMO != 0)
  tBleStatus ret = BLE_STATUS_INVALID_PARAMS;
#endif

  switch (event_pckt->evt)
  {
    /* USER CODE BEGIN evt */

    /* USER CODE END evt */
    case HCI_VENDOR_SPECIFIC_DEBUG_EVT_CODE:
      {
        handleNotification.P2P_Evt_Opcode = PEER_DISCON_HANDLE_EVT;
        blecore_evt = (evt_blecore_aci*) event_pckt->data;
        /* USER CODE BEGIN EVT_VENDOR */

        /* USER CODE END EVT_VENDOR */
        switch (blecore_evt->ecode)
        {
          /* USER CODE BEGIN ecode */

          /* USER CODE END ecode */

          case ACI_GAP_PROC_COMPLETE_VSEVT_CODE:
            {
              /* USER CODE BEGIN EVT_BLUE_GAP_PROCEDURE_COMPLETE */

              /* USER CODE END EVT_BLUE_GAP_PROCEDURE_COMPLETE */
              aci_gap_proc_complete_event_rp0 *gap_evt_proc_complete = (void*) blecore_evt->data;
              /* CHECK GAP GENERAL DISCOVERY PROCEDURE COMPLETED & SUCCEED */
              if (gap_evt_proc_complete->Procedure_Code == GAP_GENERAL_DISCOVERY_PROC
                  && gap_evt_proc_complete->Status == 0x00)
              {
                /* USER CODE BEGIN GAP_GENERAL_DISCOVERY_PROC */
                HAL_GPIO_WritePin(LED_BLUE_GPIO_Port, LED_BLUE_Pin, GPIO_PIN_RESET);
                /* USER CODE END GAP_GENERAL_DISCOVERY_PROC */
                APP_DBG_MSG("-- GAP GENERAL DISCOVERY PROCEDURE_COMPLETED\n\r");
                /*if a device found, connect to it, device 1 being chosen first if both found*/
                if (BleApplicationContext.DeviceServerFound == 0x01 && BleApplicationContext.Device_Connection_Status != APP_BLE_CONNECTED_CLIENT)
                {
                  UTIL_SEQ_SetTask(1 << CFG_TASK_CONN_DEV_1_ID, CFG_SCH_PRIO_0);
                }
              }
            }
            break;

   #if (OOB_DEMO != 0)
          case ACI_L2CAP_CONNECTION_UPDATE_REQ_VSEVT_CODE:
            {
              /* USER CODE BEGIN EVT_BLUE_L2CAP_CONNECTION_UPDATE_REQ */

              /* USER CODE END EVT_BLUE_L2CAP_CONNECTION_UPDATE_REQ */
              aci_l2cap_connection_update_req_event_rp0 *pr = (aci_l2cap_connection_update_req_event_rp0 *) blecore_evt->data;
              ret = aci_hal_set_radio_activity_mask(0x0000);
              if (ret != BLE_STATUS_SUCCESS)
              {
                APP_DBG_MSG("  Fail   : aci_hal_set_radio_activity_mask command, result: 0x%x \n\r", ret);
              }
              else
              {
                APP_DBG_MSG("  Success: aci_hal_set_radio_activity_mask command\n\r");
              }

              APP_BLE_p2p_Conn_Update_req.Identifier = pr->Identifier;
              APP_BLE_p2p_Conn_Update_req.L2CAP_Length = pr->L2CAP_Length;
              APP_BLE_p2p_Conn_Update_req.Interval_Min = pr->Interval_Min;
              APP_BLE_p2p_Conn_Update_req.Interval_Max = pr->Interval_Max;
              APP_BLE_p2p_Conn_Update_req.Slave_Latency = pr->Slave_Latency;
              APP_BLE_p2p_Conn_Update_req.Timeout_Multiplier = pr->Timeout_Multiplier;

              ret = aci_l2cap_connection_parameter_update_resp(BleApplicationContext.BleApplicationContext_legacy.connectionHandle,
                                                               APP_BLE_p2p_Conn_Update_req.Interval_Min,
                                                               APP_BLE_p2p_Conn_Update_req.Interval_Max,
                                                               APP_BLE_p2p_Conn_Update_req.Slave_Latency,
                                                               APP_BLE_p2p_Conn_Update_req.Timeout_Multiplier,
                                                               CONN_L1,
                                                               CONN_L2,
                                                               APP_BLE_p2p_Conn_Update_req.Identifier,
                                                               0x01);
              if(ret != BLE_STATUS_SUCCESS)
              {
                APP_DBG_MSG("  Fail   : aci_l2cap_connection_parameter_update_resp command, result: 0x%x \n\r", ret);
                /* USER CODE BEGIN BLE_STATUS_SUCCESS */
                HAL_GPIO_WritePin(LED_RED_GPIO_Port, LED_RED_Pin, GPIO_PIN_SET);
                /* USER CODE END BLE_STATUS_SUCCESS */
              }
              else
              {
                APP_DBG_MSG("  Success: aci_l2cap_connection_parameter_update_resp command\n\r");
              }

              ret = aci_hal_set_radio_activity_mask(0x0020);
              if (ret != BLE_STATUS_SUCCESS)
              {
                APP_DBG_MSG("  Fail   : aci_hal_set_radio_activity_mask command, result: 0x%x \n\r", ret);
              }
              else
              {
                APP_DBG_MSG("  Success: aci_hal_set_radio_activity_mask command\n\r");
              }
            }
            break;

          case ACI_HAL_END_OF_RADIO_ACTIVITY_VSEVT_CODE:
            {
              /* USER CODE BEGIN RADIO_ACTIVITY_EVENT */
              HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin, GPIO_PIN_SET);
              HW_TS_Start(BleApplicationContext.SwitchOffGPIO_timer_Id, (uint32_t)LED_ON_TIMEOUT);
              /* USER CODE END RADIO_ACTIVITY_EVENT */
            }
            break; /* ACI_HAL_END_OF_RADIO_ACTIVITY_VSEVT_CODE */
  #endif

          /* USER CODE BEGIN BLUE_EVT */

          /* USER CODE END BLUE_EVT */

          default:
            /* USER CODE BEGIN ecode_default */

            /* USER CODE END ecode_default */
            break;
        }
      }
      break;

    case HCI_DISCONNECTION_COMPLETE_EVT_CODE:
      {
        /* USER CODE BEGIN EVT_DISCONN_COMPLETE */

        /* USER CODE END EVT_DISCONN_COMPLETE */
        if (cc->Connection_Handle == BleApplicationContext.BleApplicationContext_legacy.connectionHandle)
        {
          BleApplicationContext.BleApplicationContext_legacy.connectionHandle = 0;
          BleApplicationContext.Device_Connection_Status = APP_BLE_IDLE;
          APP_DBG_MSG("\r\n\r** DISCONNECTION EVENT WITH SERVER \n\r");
          handleNotification.P2P_Evt_Opcode = PEER_DISCON_HANDLE_EVT;
          handleNotification.ConnectionHandle = BleApplicationContext.BleApplicationContext_legacy.connectionHandle;
          P2PC_APP_Notification(&handleNotification);
        }
      }
      break; /* HCI_DISCONNECTION_COMPLETE_EVT_CODE */

    case HCI_LE_META_EVT_CODE:
      {
        /* USER CODE BEGIN EVT_LE_META_EVENT */

        /* USER CODE END EVT_LE_META_EVENT */
        meta_evt = (evt_le_meta_event*) event_pckt->data;

        switch (meta_evt->subevent)
        {
          /* USER CODE BEGIN subevent */
          case HCI_LE_READ_LOCAL_P256_PUBLIC_KEY_COMPLETE_SUBEVT_CODE: // [STM]
            {
              UTIL_SEQ_SetEvt(1 << CFG_WAIT_EVT_READ_LOCAL_P256_PUBLIC_KEY_ID);
              APP_DBG_MSG(">>== HCI_LE_READ_LOCAL_P256_PUBLIC_KEY_COMPLETE_SUBEVT_CODE event \r\n");
              HAL_GPIO_WritePin(LED_RED_GPIO_Port, LED_RED_Pin, GPIO_PIN_RESET);
            }
            break;
          /* USER CODE END subevent */

          case HCI_LE_CONNECTION_COMPLETE_SUBEVT_CODE:
            /* USER CODE BEGIN EVT_LE_CONN_COMPLETE */

            /* USER CODE END EVT_LE_CONN_COMPLETE */
            /**
             * The connection is done,
             */
            connection_complete_event = (hci_le_connection_complete_event_rp0 *) meta_evt->data;
            BleApplicationContext.BleApplicationContext_legacy.connectionHandle = connection_complete_event->Connection_Handle;
            BleApplicationContext.Device_Connection_Status = APP_BLE_CONNECTED_CLIENT;

            /* CONNECTION WITH CLIENT */
            APP_DBG_MSG("\r\n\r**  CONNECTION COMPLETE EVENT WITH SERVER \n\r");
            handleNotification.P2P_Evt_Opcode = PEER_CONN_HANDLE_EVT;
            handleNotification.ConnectionHandle = BleApplicationContext.BleApplicationContext_legacy.connectionHandle;
            P2PC_APP_Notification(&handleNotification);

            result = aci_gatt_disc_all_primary_services(BleApplicationContext.BleApplicationContext_legacy.connectionHandle);
            if (result == BLE_STATUS_SUCCESS)
            {
              APP_DBG_MSG("\r\n\r** GATT SERVICES & CHARACTERISTICS DISCOVERY  \n\r");
              APP_DBG_MSG("* GATT :  Start Searching Primary Services \r\n\r");
            }
            else
            {
              APP_DBG_MSG("BLE_CTRL_App_Notification(), All services discovery Failed \r\n\r");
            }
            break; /* HCI_LE_CONNECTION_COMPLETE_SUBEVT_CODE */

          case HCI_LE_ADVERTISING_REPORT_SUBEVT_CODE:
            {
              uint8_t *adv_report_data;
              /* USER CODE BEGIN EVT_LE_ADVERTISING_REPORT */

              /* USER CODE END EVT_LE_ADVERTISING_REPORT */
              le_advertising_event = (hci_le_advertising_report_event_rp0 *) meta_evt->data;

              event_type = le_advertising_event->Advertising_Report[0].Event_Type;

              event_data_size = le_advertising_event->Advertising_Report[0].Length_Data;

              /* WARNING: be careful when decoding advertising report as its raw format cannot be mapped on a C structure.
              The data and RSSI values could not be directly decoded from the RAM using the data and RSSI field from hci_le_advertising_report_event_rp0 structure.
              Instead they must be read by using offsets (please refer to BLE specification).
              RSSI = (int8_t)*(uint8_t*) (adv_report_data + le_advertising_event->Advertising_Report[0].Length_Data);
              */
              adv_report_data = (uint8_t*)(&le_advertising_event->Advertising_Report[0].Length_Data) + 1;
              k = 0;

              /* search AD TYPE 0x09 (Complete Local Name) */
              /* search AD Type 0x02 (16 bits UUIDS) */
              if (event_type == ADV_IND)
              {
                /* ISOLATION OF BD ADDRESS AND LOCAL NAME */

                while(k < event_data_size)
                {
                  adlength = adv_report_data[k];
                  adtype = adv_report_data[k + 1];
                  switch (adtype)
                  {
                    case AD_TYPE_FLAGS: /* now get flags */
                      /* USER CODE BEGIN AD_TYPE_FLAGS */

                      /* USER CODE END AD_TYPE_FLAGS */
                      break;

                    case AD_TYPE_TX_POWER_LEVEL: /* Tx power level */
                      /* USER CODE BEGIN AD_TYPE_TX_POWER_LEVEL */

                      /* USER CODE END AD_TYPE_TX_POWER_LEVEL */
                      break;

                    case AD_TYPE_MANUFACTURER_SPECIFIC_DATA: /* Manufacturer Specific */
                      /* USER CODE BEGIN AD_TYPE_MANUFACTURER_SPECIFIC_DATA */
                      if(adlength >= 7 && adv_report_data[k + 2] == 0x01 && adv_report_data[k + 3] == CFG_DEV_ID_P2P_SERVER1)
                      {
                        aci_gap_terminate_gap_proc(0x02); // [STM] Stop scanning, adding code here considering STM32CubeMX
                        APP_DBG_MSG("Scanned device %02X:%02X:%02X:%02X:%02X:%02X\n\r",
                                        le_advertising_event->Advertising_Report[0].Address[5],
                                        le_advertising_event->Advertising_Report[0].Address[4],
                                        le_advertising_event->Advertising_Report[0].Address[3],
                                        le_advertising_event->Advertising_Report[0].Address[2],
                                        le_advertising_event->Advertising_Report[0].Address[1],
                                        le_advertising_event->Advertising_Report[0].Address[0]);
                      }
                      /* USER CODE END AD_TYPE_MANUFACTURER_SPECIFIC_DATA */
                      if (adlength >= 7 && adv_report_data[k + 2] == 0x01)
                      { /* ST VERSION ID 01 */
                        APP_DBG_MSG("--- ST MANUFACTURER ID --- \n\r");
                        switch (adv_report_data[k + 3])
                        {   /* Demo ID */
                           case CFG_DEV_ID_P2P_SERVER1: /* End Device 1 */
                           APP_DBG_MSG("-- SERVER DETECTED -- VIA MAN ID\n\r");
                           BleApplicationContext.DeviceServerFound = 0x01;
                           SERVER_REMOTE_ADDR_TYPE = le_advertising_event->Advertising_Report[0].Address_Type;
                           SERVER_REMOTE_BDADDR[0] = le_advertising_event->Advertising_Report[0].Address[0];
                           SERVER_REMOTE_BDADDR[1] = le_advertising_event->Advertising_Report[0].Address[1];
                           SERVER_REMOTE_BDADDR[2] = le_advertising_event->Advertising_Report[0].Address[2];
                           SERVER_REMOTE_BDADDR[3] = le_advertising_event->Advertising_Report[0].Address[3];
                           SERVER_REMOTE_BDADDR[4] = le_advertising_event->Advertising_Report[0].Address[4];
                           SERVER_REMOTE_BDADDR[5] = le_advertising_event->Advertising_Report[0].Address[5];
                           break;

                          default:
                            break;
                        }
                      }
                      break;

                    case AD_TYPE_SERVICE_DATA: /* service data 16 bits */
                      /* USER CODE BEGIN AD_TYPE_SERVICE_DATA */

                      /* USER CODE END AD_TYPE_SERVICE_DATA */
                      break;

                    default:
                      /* USER CODE BEGIN adtype_default */

                      /* USER CODE END adtype_default */
                      break;
                  } /* end switch adtype */
                  k += adlength + 1;
                } /* end while */
              } /* end if ADV_IND */
            }
            break;

          /* USER CODE BEGIN META_EVT */

          /* USER CODE END META_EVT */

          default:
            /* USER CODE BEGIN subevent_default */

            /* USER CODE END subevent_default */
            break;
        }
      }
      break; /* HCI_LE_META_EVT_CODE */

    /* USER CODE BEGIN EVENT_PCKT */

    /* USER CODE END EVENT_PCKT */

    default:
      /* USER CODE BEGIN evt_default */

      /* USER CODE END evt_default */
      break;
  }

  return (SVCCTL_UserEvtFlowEnable);
}

APP_BLE_ConnStatus_t APP_BLE_Get_Client_Connection_Status(uint16_t Connection_Handle)
{
  if (BleApplicationContext.BleApplicationContext_legacy.connectionHandle == Connection_Handle)
  {
    return BleApplicationContext.Device_Connection_Status;
  }
  return APP_BLE_IDLE;
}
/* USER CODE BEGIN FD */
void APP_BLE_Key_Button1_Action(void)
{
#if OOB_DEMO == 0 
  P2PC_APP_SW1_Button_Action();
#else 
  if(P2P_Client_APP_Get_State () != APP_BLE_CONNECTED_CLIENT)
  {
    //UTIL_SEQ_SetTask(1 << CFG_TASK_START_SCAN_ID, CFG_SCH_PRIO_0); [STM]
    UTIL_SEQ_SetTask(1 << CFG_TASK_SEND_OOB_DATA_ID, CFG_SCH_PRIO_0);
  }
  else 
  {
    P2PC_APP_SW1_Button_Action();
  }   
#endif 
}

void APP_BLE_Key_Button2_Action(void)
{
  UTIL_SEQ_SetTask( 1<<CFG_TASK_BLE_CLIENT_PAIRING_REQUEST_ID, CFG_SCH_PRIO_0);
}

void APP_BLE_Key_Button3_Action(void)
{
  tBleStatus ret = aci_gap_clear_security_db();
  APP_DBG_MSG("aci_gap_clear_security_db status = 0x%02X\n\r", ret);
}
/* USER CODE END FD */
/*************************************************************
 *
 * LOCAL FUNCTIONS
 *
 *************************************************************/
static void Ble_Tl_Init(void)
{
  HCI_TL_HciInitConf_t Hci_Tl_Init_Conf;

  Hci_Tl_Init_Conf.p_cmdbuffer = (uint8_t*)&BleCmdBuffer;
  Hci_Tl_Init_Conf.StatusNotCallBack = BLE_StatusNot;
  hci_init(BLE_UserEvtRx, (void*) &Hci_Tl_Init_Conf);

  return;
}

static void Ble_Hci_Gap_Gatt_Init(void)
{
  uint8_t role;
  uint16_t gap_service_handle, gap_dev_name_char_handle, gap_appearance_char_handle;
  const uint8_t *bd_addr;
  uint16_t appearance[1] = { BLE_CFG_GAP_APPEARANCE };
  tBleStatus ret = BLE_STATUS_INVALID_PARAMS;

  APP_DBG_MSG("Start Ble_Hci_Gap_Gatt_Init function\n\r");

  /**
   * Initialize HCI layer
   */
  /*HCI Reset to synchronise BLE Stack*/
  ret = hci_reset();
  if (ret != BLE_STATUS_SUCCESS)
  {
    APP_DBG_MSG("  Fail   : hci_reset command, result: 0x%x \n\r", ret);
  }
  else
  {
    APP_DBG_MSG("  Success: hci_reset command\n\r");
  }

 /**
   * Write the BD Address
   */
   bd_addr = BleGetBdAddress();
   ret = aci_hal_write_config_data(CONFIG_DATA_PUBADDR_OFFSET,
                                  CONFIG_DATA_PUBADDR_LEN,
                                  (uint8_t*) bd_addr);

   if (ret != BLE_STATUS_SUCCESS)
   {
    APP_DBG_MSG("  Fail   : aci_hal_write_config_data command, result: 0x%x \n\r", ret);
   }
   else
   {
    APP_DBG_MSG("  Success: aci_hal_write_config_data command\n\r");
   }

  /**
   * Write Identity root key used to derive LTK and CSRK
   */
  ret = aci_hal_write_config_data(CONFIG_DATA_IR_OFFSET, CONFIG_DATA_IR_LEN, (uint8_t*)BLE_CFG_IR_VALUE);
  if (ret != BLE_STATUS_SUCCESS)
  {
    APP_DBG_MSG("  Fail   : aci_hal_write_config_data command, result: 0x%x \n\r", ret);
  }
  else
  {
    APP_DBG_MSG("  Success: aci_hal_write_config_data command\n\r");
  }

  /**
   * Write Encryption root key used to derive LTK and CSRK
   */
  ret = aci_hal_write_config_data(CONFIG_DATA_ER_OFFSET, CONFIG_DATA_ER_LEN, (uint8_t*)BLE_CFG_ER_VALUE);
  if (ret != BLE_STATUS_SUCCESS)
  {
    APP_DBG_MSG("  Fail   : aci_hal_write_config_data command, result: 0x%x \n\r", ret);
  }
  else
  {
    APP_DBG_MSG("  Success: aci_hal_write_config_data command\n\r");
  }

  /**
   * Set TX Power to 0dBm.
   */
  ret = aci_hal_set_tx_power_level(1, CFG_TX_POWER);
  if (ret != BLE_STATUS_SUCCESS)
  {
    APP_DBG_MSG("  Fail   : aci_hal_set_tx_power_level command, result: 0x%x \n\r", ret);
  }
  else
  {
    APP_DBG_MSG("  Success: aci_hal_set_tx_power_level command\n\r");
  }

  /**
   * Initialize GATT interface
   */
  ret = aci_gatt_init();
  if (ret != BLE_STATUS_SUCCESS)
  {
    APP_DBG_MSG("  Fail   : aci_gatt_init command, result: 0x%x \n\r", ret);
  }
  else
  {
    APP_DBG_MSG("  Success: aci_gatt_init command\n\r");
  }

  /**
   * Initialize GAP interface
   */
  role = 0;

#if (BLE_CFG_PERIPHERAL == 1)
  role |= GAP_PERIPHERAL_ROLE;
#endif

#if (BLE_CFG_CENTRAL == 1)
  role |= GAP_CENTRAL_ROLE;
#endif

/* USER CODE BEGIN Role_Mngt*/

/* USER CODE END Role_Mngt */

  if (role > 0)
  {
    const char *name = "P2P_C";

    ret = aci_gap_init(role,
                       CFG_PRIVACY,
                       APPBLE_GAP_DEVICE_NAME_LENGTH,
                       &gap_service_handle,
                       &gap_dev_name_char_handle,
                       &gap_appearance_char_handle);

    if (ret != BLE_STATUS_SUCCESS)
    {
      APP_DBG_MSG("  Fail   : aci_gap_init command, result: 0x%x \n\r", ret);
    }
    else
    {
      APP_DBG_MSG("  Success: aci_gap_init command\n\r");
    }

    if (aci_gatt_update_char_value(gap_service_handle, gap_dev_name_char_handle, 0, strlen(name), (uint8_t *) name))
    {
      BLE_DBG_SVCCTL_MSG("Device Name aci_gatt_update_char_value failed.\n\r");
    }
  }

  if(aci_gatt_update_char_value(gap_service_handle,
                                gap_appearance_char_handle,
                                0,
                                2,
                                (uint8_t *)&appearance))
  {
    BLE_DBG_SVCCTL_MSG("Appearance aci_gatt_update_char_value failed.\n\r");
  }

  /**
   * Initialize IO capability
   */
  BleApplicationContext.BleApplicationContext_legacy.bleSecurityParam.ioCapability = CFG_IO_CAPABILITY;
  ret = aci_gap_set_io_capability(BleApplicationContext.BleApplicationContext_legacy.bleSecurityParam.ioCapability);
  if (ret != BLE_STATUS_SUCCESS)
  {
    APP_DBG_MSG("  Fail   : aci_gap_set_io_capability command, result: 0x%x \n\r", ret);
  }
  else
  {
    APP_DBG_MSG("  Success: aci_gap_set_io_capability command\n\r");
  }

  /**
   * Initialize authentication
   */
  BleApplicationContext.BleApplicationContext_legacy.bleSecurityParam.mitm_mode = CFG_MITM_PROTECTION;
  BleApplicationContext.BleApplicationContext_legacy.bleSecurityParam.encryptionKeySizeMin = CFG_ENCRYPTION_KEY_SIZE_MIN;
  BleApplicationContext.BleApplicationContext_legacy.bleSecurityParam.encryptionKeySizeMax = CFG_ENCRYPTION_KEY_SIZE_MAX;
  BleApplicationContext.BleApplicationContext_legacy.bleSecurityParam.Use_Fixed_Pin = CFG_USED_FIXED_PIN;
  BleApplicationContext.BleApplicationContext_legacy.bleSecurityParam.Fixed_Pin = CFG_FIXED_PIN;
  BleApplicationContext.BleApplicationContext_legacy.bleSecurityParam.bonding_mode = CFG_BONDING_MODE;

  ret = aci_gap_set_authentication_requirement(BleApplicationContext.BleApplicationContext_legacy.bleSecurityParam.bonding_mode,
                                               BleApplicationContext.BleApplicationContext_legacy.bleSecurityParam.mitm_mode,
                                               CFG_SC_SUPPORT,
                                               CFG_KEYPRESS_NOTIFICATION_SUPPORT,                                               BleApplicationContext.BleApplicationContext_legacy.bleSecurityParam.encryptionKeySizeMin,
                                               BleApplicationContext.BleApplicationContext_legacy.bleSecurityParam.encryptionKeySizeMax,
                                               BleApplicationContext.BleApplicationContext_legacy.bleSecurityParam.Use_Fixed_Pin,
                                               BleApplicationContext.BleApplicationContext_legacy.bleSecurityParam.Fixed_Pin,
                                               CFG_IDENTITY_ADDRESS);

  if (ret != BLE_STATUS_SUCCESS)
  {
    APP_DBG_MSG("  Fail   : aci_gap_set_authentication_requirement command, result: 0x%x \n\r", ret);
  }
  else
  {
    APP_DBG_MSG("  Success: aci_gap_set_authentication_requirement command\n\r");
  }

  /**
   * Initialize whitelist
   */
  if (BleApplicationContext.BleApplicationContext_legacy.bleSecurityParam.bonding_mode)
  {
    ret = aci_gap_configure_whitelist();
    if (ret != BLE_STATUS_SUCCESS)
    {
      APP_DBG_MSG("  Fail   : aci_gap_configure_whitelist command, result: 0x%x \n\r", ret);
    }
    else
    {
      APP_DBG_MSG("  Success: aci_gap_configure_whitelist command\n\r");
    }
  }
}

static void Scan_Request(void)
{
  /* USER CODE BEGIN Scan_Request_1 */

  /* USER CODE END Scan_Request_1 */
  tBleStatus result;
  if (BleApplicationContext.Device_Connection_Status != APP_BLE_CONNECTED_CLIENT)
  {
    /* USER CODE BEGIN APP_BLE_CONNECTED_CLIENT */
    HAL_GPIO_WritePin(LED_BLUE_GPIO_Port, LED_BLUE_Pin, GPIO_PIN_SET);
    /* USER CODE END APP_BLE_CONNECTED_CLIENT */
    result = aci_gap_start_general_discovery_proc(SCAN_P, SCAN_L, CFG_BLE_ADDRESS_TYPE, 1);
    if (result == BLE_STATUS_SUCCESS)
    {
    /* USER CODE BEGIN BLE_SCAN_SUCCESS */
      
    /* USER CODE END BLE_SCAN_SUCCESS */
      APP_DBG_MSG(" \r\n\r** START GENERAL DISCOVERY (SCAN) **  \r\n\r");
    }
    else
    {
    /* USER CODE BEGIN BLE_SCAN_FAILED */
      HAL_GPIO_WritePin(LED_RED_GPIO_Port, LED_RED_Pin, GPIO_PIN_SET);
    /* USER CODE END BLE_SCAN_FAILED */
      APP_DBG_MSG("-- BLE_App_Start_Limited_Disc_Req, Failed \r\n\r");
    }
  }
  /* USER CODE BEGIN Scan_Request_2 */
  
  /* USER CODE END Scan_Request_2 */
  return;
}

static void Connect_Request(void)
{
  /* USER CODE BEGIN Connect_Request_1 */

  /* USER CODE END Connect_Request_1 */
  tBleStatus result;

  APP_DBG_MSG("\r\n\r** CREATE CONNECTION TO SERVER **  \r\n\r");

  if (BleApplicationContext.Device_Connection_Status != APP_BLE_CONNECTED_CLIENT)
  {
    result = aci_gap_create_connection(SCAN_P,
                                       SCAN_L,
                                       SERVER_REMOTE_ADDR_TYPE, SERVER_REMOTE_BDADDR,
                                       CFG_BLE_ADDRESS_TYPE,
                                       CONN_P1,
                                       CONN_P2,
                                       0,
                                       SUPERV_TIMEOUT,
                                       CONN_L1,
                                       CONN_L2);

    if (result == BLE_STATUS_SUCCESS)
    {
      /* USER CODE BEGIN BLE_CONNECT_SUCCESS */

      /* USER CODE END BLE_CONNECT_SUCCESS */
      BleApplicationContext.Device_Connection_Status = APP_BLE_LP_CONNECTING;

    }
    else
    {
      /* USER CODE BEGIN BLE_CONNECT_FAILED */
      HAL_GPIO_WritePin(LED_RED_GPIO_Port, LED_RED_Pin, GPIO_PIN_SET);
      /* USER CODE END BLE_CONNECT_FAILED */
      BleApplicationContext.Device_Connection_Status = APP_BLE_IDLE;

    }
  }
  /* USER CODE BEGIN Connect_Request_2 */

  /* USER CODE END Connect_Request_2 */
  return;
}

static void Switch_OFF_GPIO()
{
  /* USER CODE BEGIN Switch_OFF_GPIO */
  HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin, GPIO_PIN_RESET);
  /* USER CODE END Switch_OFF_GPIO */
}

const uint8_t* BleGetBdAddress(void)
{
  uint8_t *otp_addr;
  const uint8_t *bd_addr;
  uint32_t udn;
  uint32_t company_id;
  uint32_t device_id;

  udn = LL_FLASH_GetUDN();

  if(udn != 0xFFFFFFFF)
  {
    company_id = LL_FLASH_GetSTCompanyID();
    device_id = LL_FLASH_GetDeviceID();

  /**
   * Public Address with the ST company ID
   * bit[47:24] : 24bits (OUI) equal to the company ID
   * bit[23:16] : Device ID.
   * bit[15:0] : The last 16bits from the UDN
   * Note: In order to use the Public Address in a final product, a dedicated
   * 24bits company ID (OUI) shall be bought.
   */
   bd_addr_udn[0] = (uint8_t)(udn & 0x000000FF);
   bd_addr_udn[1] = (uint8_t)((udn & 0x0000FF00) >> 8);
   bd_addr_udn[2] = (uint8_t)device_id;
   bd_addr_udn[3] = (uint8_t)(company_id & 0x000000FF);
   bd_addr_udn[4] = (uint8_t)((company_id & 0x0000FF00) >> 8);
   bd_addr_udn[5] = (uint8_t)((company_id & 0x00FF0000) >> 16);

   bd_addr = (const uint8_t *)bd_addr_udn;
  }
  else
  {
    otp_addr = OTP_Read(0);
    if(otp_addr)
    {
      bd_addr = ((OTP_ID0_t*)otp_addr)->bd_address;
    }
    else
    {
      bd_addr = M_bd_addr;
    }
  }

  return bd_addr;
}

/* USER CODE BEGIN FD_LOCAL_FUNCTIONS */
static void send_oob_data(void)
{
  HAL_StatusTypeDef status = HAL_UART_Transmit(&hlpuart1, (uint8_t *)&m_oob_uart_data, sizeof(m_oob_uart_data), 1000);
  
  APP_DBG_MSG("<<== BLE Client OOB data - UART TX to BLE server status = 0x%02X\r\n\r\n", status);
  
  //UTIL_SEQ_SetTask(1 << CFG_TASK_START_SCAN_ID, CFG_SCH_PRIO_0); // [STM] Do not scan
}
/* USER CODE END FD_LOCAL_FUNCTIONS */

/*************************************************************
 *
 * WRAP FUNCTIONS
 *
 *************************************************************/
void hci_notify_asynch_evt(void* pdata)
{
  UTIL_SEQ_SetTask(1 << CFG_TASK_HCI_ASYNCH_EVT_ID, CFG_SCH_PRIO_0);
  return;
}

void hci_cmd_resp_release(uint32_t flag)
{
  UTIL_SEQ_SetEvt(1 << CFG_IDLEEVT_HCI_CMD_EVT_RSP_ID);
  return;
}

void hci_cmd_resp_wait(uint32_t timeout)
{
  UTIL_SEQ_WaitEvt(1 << CFG_IDLEEVT_HCI_CMD_EVT_RSP_ID);
  return;
}

static void BLE_UserEvtRx(void * pPayload)
{
  SVCCTL_UserEvtFlowStatus_t svctl_return_status;
  tHCI_UserEvtRxParam *pParam;

  pParam = (tHCI_UserEvtRxParam *)pPayload;

  svctl_return_status = SVCCTL_UserEvtRx((void *)&(pParam->pckt->evtserial));
  if (svctl_return_status != SVCCTL_UserEvtFlowDisable)
  {
    pParam->status = HCI_TL_UserEventFlow_Enable;
  }
  else
  {
    pParam->status = HCI_TL_UserEventFlow_Disable;
  }

  return;
}

static void BLE_StatusNot(HCI_TL_CmdStatus_t status)
{
  uint32_t task_id_list;
  switch (status)
  {
    case HCI_TL_CmdBusy:
      /**
       * All tasks that may send an aci/hci commands shall be listed here
       * This is to prevent a new command is sent while one is already pending
       */
      task_id_list = (1 << CFG_LAST_TASK_ID_WITH_HCICMD) - 1;
      UTIL_SEQ_PauseTask(task_id_list);
      break;

    case HCI_TL_CmdAvailable:
      /**
       * All tasks that may send an aci/hci commands shall be listed here
       * This is to prevent a new command is sent while one is already pending
       */
      task_id_list = (1 << CFG_LAST_TASK_ID_WITH_HCICMD) - 1;
      UTIL_SEQ_ResumeTask(task_id_list);
      break;

    default:
      break;
  }
  return;
}

void SVCCTL_ResumeUserEventFlow(void)
{
  hci_resume_flow();
  return;
}

/**
  * @brief  Run OOB process, set the OOB data arrived via OOB communication (which is UART in this case)
  * @param p_data: BLE server data that has Server device's address and address type
  * @retval none
  */
void APP_BLE_set_oob(app_ble_server_data_t *p_data)
{
  tBleStatus status;
  
  status = aci_gap_set_oob_data(0x01, // Remote device
                                p_data->address_type, // BLE server address type
                                p_data->address, // BLE server address
                                0x00, // OOB Data type - TK
                                m_oob_uart_data.tk_size, // TK size
                                m_oob_uart_data.tk);  // TK
  
  APP_DBG_MSG("==>> aci_gap_set_oob_data (Remote device) result = 0x%02X\r\n", status);
  APP_DBG_MSG("<<== Received BLE Server address (type %s) %02X:%02X:%02X:%02X:%02X:%02X, schedule to connect with Server\r\n",
              p_data->address_type == 0 ? "Public" : "Random",
              p_data->address[5], p_data->address[4], p_data->address[3],
              p_data->address[2], p_data->address[1], p_data->address[0]);
  
  // [STM] Configure REMOTE (BLE Server) address data to connect via Bluetooth
  BleApplicationContext.DeviceServerFound = 0x01;
  SERVER_REMOTE_ADDR_TYPE = p_data->address_type;
  memcpy(SERVER_REMOTE_BDADDR, p_data->address, sizeof(SERVER_REMOTE_BDADDR));
  
  // [STM] schedule to connect with BLE Server
  UTIL_SEQ_SetTask(1 << CFG_TASK_CONN_DEV_1_ID, CFG_SCH_PRIO_0);
  
  //ble_oob_data_log(m_oob_uart_data.tk_size, m_oob_uart_data.tk);
}
/* USER CODE BEGIN FD_WRAP_FUNCTIONS */
static void uart_rx_callback (void) // [STM]
{
  UTIL_SEQ_SetTask( 1<<CFG_TASK_OOB_PROCESS_ID, CFG_SCH_PRIO_0);
}

static void ble_oob_process (void) // [STM]
{
  APP_BLE_set_oob(&m_oob_remote_data);
}

static void ble_client_pairing_request(void) // [STM]
{
  tBleStatus ret = aci_gap_send_pairing_req(BleApplicationContext.BleApplicationContext_legacy.connectionHandle,
                                            0x01); // [STM] Request pairing, force rebond
  APP_DBG_MSG("aci_gap_send_pairing_req, result: 0x%02X \n\r", ret);
}

static void ble_oob_data_log (uint32_t size, uint8_t *p_data)
{
  uint8_t msg[256];
  
  sprintf((char *) msg, "OOB TK Data generated by BLE client (size = %d) =", size);
  
  for(uint32_t k = 0; k < size; k++)
  {
    sprintf((char *)msg, "%s %02X", msg, p_data[k]);
  }
  APP_DBG_MSG("%s\r\n", msg);
}
/* USER CODE END FD_WRAP_FUNCTIONS */
