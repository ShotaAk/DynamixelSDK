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

#include "driver/uart.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "port_handler_esp32.h"

static const char *TAG = "dynamixel";

#define LATENCY_TIMER  16  // msec (USB latency timer)
                           // You should adjust the latency timer value. From the version Ubuntu 16.04.2, the default latency timer of the usb serial is '16 msec'.
                           // When you are going to use sync / bulk read, the latency timer should be loosen.
                           // the lower latency timer value, the faster communication speed.

                           // Note:
                           // You can check its value by:
                           // $ cat /sys/bus/usb-serial/devices/ttyUSB0/latency_timer
                           //
                           // If you think that the communication is too slow, type following after plugging the usb in to change the latency timer
                           //
                           // Method 1. Type following (you should do this everytime when the usb once was plugged out or the connection was dropped)
                           // $ echo 1 | sudo tee /sys/bus/usb-serial/devices/ttyUSB0/latency_timer
                           // $ cat /sys/bus/usb-serial/devices/ttyUSB0/latency_timer
                           //
                           // Method 2. If you want to set it as be done automatically, and don't want to do above everytime, make rules file in /etc/udev/rules.d/. For example,
                           // $ echo ACTION==\"add\", SUBSYSTEM==\"usb-serial\", DRIVER==\"ftdi_sio\", ATTR{latency_timer}=\"1\" > 99-dynamixelsdk-usb.rules
                           // $ sudo cp ./99-dynamixelsdk-usb.rules /etc/udev/rules.d/
                           // $ sudo udevadm control --reload-rules
                           // $ sudo udevadm trigger --action=add
                           // $ cat /sys/bus/usb-serial/devices/ttyUSB0/latency_timer
                           //
                           // or if you have another good idea that can be an alternatives,
                           // please give us advice via github issue https://github.com/ROBOTIS-GIT/DynamixelSDK/issues

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
  portData[port_num].tx_time_per_byte = 0.0;
  portData[port_num].tx_pin = tx_pin;
  portData[port_num].rx_pin = rx_pin;
  portData[port_num].out_en_pin = out_en_pin;

  g_is_using[port_num] = False;

  setPortNameESP32(port_num, port_name);

  return port_num;
}

uint8_t openPortESP32(int port_num)
{
  static bool uart_installed[UART_NUM_MAX] = {false};

  const int rx_buffer_size = 2048;
  const int tx_buffer_size = 2048;

  if (!uart_installed[port_num]) {
      uart_driver_install(port_num, rx_buffer_size, tx_buffer_size, 0, NULL, 0);
      uart_installed[port_num] = true;
  }

  return setBaudRateESP32(port_num, portData[port_num].baudrate);
}

// void closePortESP32(int port_num)
// {
//   if (portData[port_num].socket_fd != -1)
//   {
//     close(portData[port_num].socket_fd);
//     portData[port_num].socket_fd = -1;
//   }
// }

// void clearPortESP32(int port_num)
// {
//   tcflush(portData[port_num].socket_fd, TCIFLUSH);
// }

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

  closePortESP32(port_num);

  uart_config_t uart_config = {
        .baud_rate = baudrate,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
  ESP_ERROR_CHECK(uart_param_config(port_num, &uart_config));
  ESP_ERROR_CHECK(uart_set_pin(port_num, portData[port_num].tx_pin, portData[port_num].rx_pin, -1, -1));

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

// int readPortESP32(int port_num, uint8_t *packet, int length)
// {
//   return read(portData[port_num].socket_fd, packet, length);
// }

// int writePortESP32(int port_num, uint8_t *packet, int length)
// {
//   return write(portData[port_num].socket_fd, packet, length);
// }

// void setPacketTimeoutESP32(int port_num, uint16_t packet_length)
// {
//   portData[port_num].packet_start_time = getCurrentTimeESP32();
//   portData[port_num].packet_timeout = (portData[port_num].tx_time_per_byte * (double)packet_length) + (LATENCY_TIMER * 2.0) + 2.0;
// }

// void setPacketTimeoutMSecESP32(int port_num, double msec)
// {
//   portData[port_num].packet_start_time = getCurrentTimeESP32();
//   portData[port_num].packet_timeout = msec;
// }

// uint8_t isPacketTimeoutESP32(int port_num)
// {
//   if (getTimeSinceStartESP32(port_num) > portData[port_num].packet_timeout)
//   {
//     portData[port_num].packet_timeout = 0;
//     return True;
//   }
//   return False;
// }

// double getCurrentTimeESP32()
// {
//   struct timespec tv;
//   clock_gettime(CLOCK_REALTIME, &tv);
//   return ((double)tv.tv_sec * 1000.0 + (double)tv.tv_nsec * 0.001 * 0.001);
// }

// double getTimeSinceStartESP32(int port_num)
// {
//   double time_since_start;

//   time_since_start = getCurrentTimeESP32() - portData[port_num].packet_start_time;
//   if (time_since_start < 0.0)
//     portData[port_num].packet_start_time = getCurrentTimeESP32();

//   return time_since_start;
// }

// uint8_t setupPortESP32(int port_num, int cflag_baud)
// {
//   struct termios newtio;

//   portData[port_num].socket_fd = open(portData[port_num].port_name, O_RDWR | O_NOCTTY | O_NONBLOCK);

//   if (portData[port_num].socket_fd < 0)
//   {
//     printf("[PortHandlerESP32::SetupPort] Error opening serial port!\n");
//     return False;
//   }

//   bzero(&newtio, sizeof(newtio)); // clear struct for new port settings

//   newtio.c_cflag = cflag_baud | CS8 | CLOCAL | CREAD;
//   newtio.c_iflag = IGNPAR;
//   newtio.c_oflag = 0;
//   newtio.c_lflag = 0;
//   newtio.c_cc[VTIME] = 0;
//   newtio.c_cc[VMIN] = 0;

//   // clean the buffer and activate the settings for the port
//   tcflush(portData[port_num].socket_fd, TCIFLUSH);
//   tcsetattr(portData[port_num].socket_fd, TCSANOW, &newtio);

//   portData[port_num].tx_time_per_byte = (1000.0 / (double)portData[port_num].baudrate) * 10.0;
//   return True;
// }

// uint8_t setCustomBaudrateESP32(int port_num, int speed)
// {
//   struct termios2 options;

//   if (ioctl(portData[port_num].socket_fd, TCGETS2, &options) != 01)
//   {
//     options.c_cflag &= ~CBAUD;
//     options.c_cflag |= BOTHER;
//     options.c_ispeed = speed;
//     options.c_ospeed = speed;

//     if (ioctl(portData[port_num].socket_fd, TCSETS2, &options) != -1)
//       return True;
//   }

//   // try to set a custom divisor
//   struct serial_struct ss;
//   if (ioctl(portData[port_num].socket_fd, TIOCGSERIAL, &ss) != 0)
//   {
//     printf("[PortHandlerESP32::SetCustomBaudrate] TIOCGSERIAL failed!\n");
//     return False;
//   }

//   ss.flags = (ss.flags & ~ASYNC_SPD_MASK) | ASYNC_SPD_CUST;
//   ss.custom_divisor = (ss.baud_base + (speed / 2)) / speed;
//   int closest_speed = ss.baud_base / ss.custom_divisor;

//   if (closest_speed < speed * 98 / 100 || closest_speed > speed * 102 / 100)
//   {
//     printf("[PortHandlerESP32::setCustomBaudrate] Cannot set speed to %d, closest is %d \n", speed, closest_speed);
//     return False;
//   }

//   if (ioctl(portData[port_num].socket_fd, TIOCSSERIAL, &ss) < 0)
//   {
//     printf("[PortHandlerESP32::setCustomBaudrate] TIOCSSERIAL failed!\n");
//     return False;
//   }

//   portData[port_num].tx_time_per_byte = (1000.0 / (double)speed) * 10.0;
//   return True;
// }

// int getCFlagBaud(int baudrate)
// {
//   switch (baudrate)
//   {
//     case 9600:
//       return B9600;
//     case 19200:
//       return B19200;
//     case 38400:
//       return B38400;
//     case 57600:
//       return B57600;
//     case 115200:
//       return B115200;
//     case 230400:
//       return B230400;
//     case 460800:
//       return B460800;
//     case 500000:
//       return B500000;
//     case 576000:
//       return B576000;
//     case 921600:
//       return B921600;
//     case 1000000:
//       return B1000000;
//     case 1152000:
//       return B1152000;
//     case 1500000:
//       return B1500000;
//     case 2000000:
//       return B2000000;
//     case 2500000:
//       return B2500000;
//     case 3000000:
//       return B3000000;
//     case 3500000:
//       return B3500000;
//     case 4000000:
//       return B4000000;
//     default:
//       return -1;
//   }
// }

#endif
