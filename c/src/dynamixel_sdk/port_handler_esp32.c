/*******************************************************************************
* Copyright 2017 ROBOTIS CO., LTD.
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/

/* Author: Ryu Woon Jung (Leon) */

#if defined(__ESP32__)

// #include <stdio.h>
// #include <fcntl.h>
#include <string.h>
// #include <stdlib.h>
// #include <unistd.h>
// #include <termios.h>
// #include <time.h>
// #include <sys/time.h>
// #include <sys/ioctl.h>
// #include <linux/serial.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "port_handler_esp32.h"

static const char *TAG = "dynamixel";

#define LATENCY_TIMER  4  // UART通信だから必須ではないが、念の為設定しておく

// struct termios2 {
//   tcflag_t c_iflag;       /* input mode flags */
//   tcflag_t c_oflag;       /* output mode flags */
//   tcflag_t c_cflag;       /* control mode flags */
//   tcflag_t c_lflag;       /* local mode flags */
//   cc_t c_line;            /* line discipline */
//   cc_t c_cc[19];          /* control characters */
//   speed_t c_ispeed;       /* input speed */
//   speed_t c_ospeed;       /* output speed */
// };

// #ifndef TCGETS2
// #define TCGETS2     _IOR('T', 0x2A, struct termios2)
// #endif
// #ifndef TCSETS2
// #define TCSETS2     _IOW('T', 0x2B, struct termios2)
// #endif
// #ifndef BOTHER
// #define BOTHER      0010000
// #endif

typedef struct
{
  int     socket_fd;
  int     baudrate;
  char    port_name[100];
  uart_port_t port;

  double  packet_start_time;
  double  packet_timeout;
  double  tx_time_per_byte;

  int tx_pin;
  int rx_pin;
  int out_en_pin;
}PortData;

static PortData *portData;

int portHandlerESP32(const char *port_name, const int tx_pin, const int rx_pin, const int out_en_pin)
{
  int port_num;

  if (portData == NULL)
  {
    port_num = 0;
    g_used_port_num = 1;
    portData = (PortData *)calloc(1, sizeof(PortData));
    g_is_using = (uint8_t*)calloc(1, sizeof(uint8_t));
  }
  else
  {
    for (port_num = 0; port_num < g_used_port_num; port_num++)
    {
      if (!strcmp(portData[port_num].port_name, port_name))
        break;
    }

    if (port_num == g_used_port_num)
    {
      for (port_num = 0; port_num < g_used_port_num; port_num++)
      {
        if (portData[port_num].socket_fd != -1)
          break;
      }

      if (port_num == g_used_port_num)
      {
        g_used_port_num++;
        portData = (PortData*)realloc(portData, g_used_port_num * sizeof(PortData));
        g_is_using = (uint8_t*)realloc(g_is_using, g_used_port_num * sizeof(uint8_t));
      }
    }
    else
    {
      ESP_LOGI(TAG, "[PortHandler setup] The port number %d has same device name... reinitialize port number %d!!\n", port_num, port_num);
    }
  }

  portData[port_num].socket_fd = -1;
  portData[port_num].baudrate = DEFAULT_BAUDRATE;
  portData[port_num].packet_start_time = 0.0;
  portData[port_num].packet_timeout = 0.0;
  portData[port_num].tx_time_per_byte = (1000.0 / (double)portData[port_num].baudrate) * 10.0;
  portData[port_num].tx_pin = tx_pin;
  portData[port_num].rx_pin = rx_pin;
  portData[port_num].out_en_pin = out_en_pin;
  portData[port_num].port = UART_NUM_1;

  // GPIOの設定
  gpio_reset_pin(out_en_pin);
  gpio_set_direction(out_en_pin, GPIO_MODE_OUTPUT);

  g_is_using[port_num] = False;

  setPortNameESP32(port_num, port_name);

  return port_num;
}

uint8_t openPortESP32(int port_num)
{
  uart_config_t uart_config = {
        .baud_rate = portData[port_num].baudrate,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

  const int rx_buffer_size = 2048;
  const int tx_buffer_size = 2048;

  ESP_ERROR_CHECK(uart_driver_install(portData[port_num].port, rx_buffer_size, tx_buffer_size, 0, NULL, 0));
  ESP_ERROR_CHECK(uart_param_config(portData[port_num].port, &uart_config));
  ESP_ERROR_CHECK(uart_set_pin(portData[port_num].port, portData[port_num].tx_pin, portData[port_num].rx_pin, -1, -1));

  return True;
}

void closePortESP32(int port_num)
{
  if (uart_is_driver_installed(portData[port_num].port))
  {
    uart_driver_delete(portData[port_num].port);
  }
}

void clearPortESP32(int port_num)
{
  uart_flush(portData[port_num].port);
}

void setPortNameESP32(int port_num, const char *port_name)
{
  strcpy(portData[port_num].port_name, port_name);
}

char *getPortNameESP32(int port_num)
{
  return portData[port_num].port_name;
}

uint8_t setBaudRateESP32(int port_num, const int baudrate)
{
  portData[port_num].baudrate = baudrate;
  portData[port_num].tx_time_per_byte = (1000.0 / (double)portData[port_num].baudrate) * 10.0;
  ESP_ERROR_CHECK(uart_set_baudrate(portData[port_num].port, portData[port_num].baudrate));

  return True;
}

int getBaudRateESP32(int port_num)
{
  return portData[port_num].baudrate;
}

// int getBytesAvailableESP32(int port_num)
// {
//   int bytes_available;
//   ioctl(portData[port_num].socket_fd, FIONREAD, &bytes_available);
//   return bytes_available;
// }

int readPortESP32(int port_num, uint8_t *packet, int length)
{
  // 最大 length バイト読み取り（20msまで待つ）
  int len = uart_read_bytes(portData[port_num].port, packet, length, 20 / portTICK_PERIOD_MS);
  return len;
}

static void set_direction_tx()
{
  // Tri State bufferのOEを操作する
  gpio_set_level(portData[0].out_en_pin, 1);  // TX
}

static void set_direction_rx()
{
  // Tri State bufferのOEを操作する
  gpio_set_level(portData[0].out_en_pin, 0);  // RX
}

int writePortESP32(int port_num, uint8_t *packet, int length)
{
  // 書き込み前に方向をTXへ
  set_direction_tx();  // 必要に応じて呼ぶ（半二重通信の場合）

  int len = uart_write_bytes(portData[port_num].port, (const char *)packet, length);

  uart_wait_tx_done(portData[port_num].port, 20 / portTICK_PERIOD_MS);  // 書き込み完了を待つ

  set_direction_rx();  // TX完了後すぐRXへ戻す

  return len;
}

void setPacketTimeoutESP32(int port_num, uint16_t packet_length)
{
  portData[port_num].packet_start_time = getCurrentTimeESP32();
  portData[port_num].packet_timeout = (portData[port_num].tx_time_per_byte * (double)packet_length) + (LATENCY_TIMER * 2.0) + 2.0;
}

void setPacketTimeoutMSecESP32(int port_num, double msec)
{
  portData[port_num].packet_start_time = getCurrentTimeESP32();
  portData[port_num].packet_timeout = msec;
}

uint8_t isPacketTimeoutESP32(int port_num)
{
  double elapsed = getCurrentTimeESP32() - portData[port_num].packet_start_time;

  if (elapsed > portData[port_num].packet_timeout)
  {
      portData[port_num].packet_timeout = 0;  // タイムアウト済み
      return True;
  }
  return False;
}

double getCurrentTimeESP32()
{
  // esp_timer_get_time() は us 単位 → ms に変換
  return (double)(esp_timer_get_time()) / 1000.0;
}

#endif
