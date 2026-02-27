#include "mcp_uart_tools.h"
#include "mcp_server.h"
#include "config.h"
#include <driver/uart.h>
#include <esp_log.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>
#include <driver/gpio.h>

#define UART_PORT_NUM      UART_NUM_1
#define UART_BAUD_RATE     115200
#define UART_BUF_SIZE      1024

#define UART_CTRL_GPIO    GPIO_NUM_11

#define TAG "MCP_UART"

// 停止指令
static const char* STOP_CMD = "stop\n";
// 2秒自动停止定时器
static TimerHandle_t auto_stop_timer = NULL;

// 串口初始化
static void InitializeUart() {
    static bool initialized = false;
    gpio_config_t io_conf = {
    .pin_bit_mask = 1ULL << UART_CTRL_GPIO,
    .mode = GPIO_MODE_OUTPUT,
    .pull_up_en = GPIO_PULLUP_DISABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_DISABLE,
};
gpio_config(&io_conf);
    gpio_set_level(UART_CTRL_GPIO,0);  // 发送前拉

    if (initialized) return;
    initialized = true;
    uart_config_t uart_config = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

   // 安装UART驱动程序
        esp_err_t err = uart_driver_install(UART_PORT_NUM, UART_BUF_SIZE, UART_BUF_SIZE, 0, NULL, 0);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "UART驱动安装失败: %d", err);
            return;
        } 
        err = uart_param_config(UART_PORT_NUM, &uart_config);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "UART参数配置失败: %d", err);
            return;
        }
        err = uart_set_pin(UART_PORT_NUM, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "UART引脚配置失败: %d", err);
            return;
        }
}

// 串口发送函数
static void SendUartCommand(const char* cmd) {
    InitializeUart();
    //gpio_set_level(UART_CTRL_GPIO, 0);  // 发送前拉高
    vTaskDelay(pdMS_TO_TICKS(1500));   
    uart_write_bytes(UART_PORT_NUM, cmd, strlen(cmd));

    //vTaskDelay(pdMS_TO_TICKS(200));   

    ESP_LOGI(TAG, "发送串口指令: %s", cmd);
    //gpio_set_level(UART_CTRL_GPIO,0);  // 发送前拉


}

// 全局变量用于存储接收到的字符串
static char received_data[UART_BUF_SIZE] = {0};

// 串口接收函数
// static void ReceiveUartData() {
//     uint8_t data[UART_BUF_SIZE];
//     int len = uart_read_bytes(UART_PORT_NUM, data, UART_BUF_SIZE,  pdMS_TO_TICKS(2000));
//     if (len > 0) {
//         // 将接收到的数据存储到全局变量中
//         strncpy(received_data, (char*)data, len);
//         received_data[len] = '\0';  // 确保字符串以'\0'结尾
//         ESP_LOGI(TAG, "接收到串口数据: %s", received_data);
//     }
// }

// 串口接收函数（带超时判断）
static std::string ReceiveUartDataWithTimeout(int timeout_ms) {
    uint8_t data[UART_BUF_SIZE];
    int len = uart_read_bytes(UART_PORT_NUM, data, UART_BUF_SIZE, pdMS_TO_TICKS(timeout_ms));
    ESP_LOGI(TAG, "开始接收串口数据，超时时间: %d ms", timeout_ms);
        if (len > 0) {
        // 将接收到的数据存储到全局变量中
        strncpy(received_data, (char*)data, len);
        received_data[len] = '\0'; // 确保字符串以'\0'结尾
        ESP_LOGI(TAG, "接收到串口数据: %s", received_data);
        return std::string(received_data); // 返回接收到的数据
    } else {
        ESP_LOGW(TAG, "超时未接收到数据");
        return "No Data Received"; // 超时未接收数据的标准返回值
    }
}



// 定时器回调：自动发送停止指令
static void AutoStopCallback(TimerHandle_t xTimer) {
    // SendUartCommand(STOP_CMD);
    ESP_LOGI(TAG, "自动发送停止指令: %s", STOP_CMD);
}

// 启动或重置2秒自动停止定时器
static void StartOrResetStopTimer() {
    if (auto_stop_timer == NULL) {
        auto_stop_timer = xTimerCreate("AutoStopTimer", pdMS_TO_TICKS(2000), pdFALSE, NULL, AutoStopCallback);
    }
    xTimerStop(auto_stop_timer, 0);
    xTimerStart(auto_stop_timer, 0);
    gpio_set_level(UART_CTRL_GPIO,1);  // 发送前拉

}

// 发送动作指令并自动2秒后停止
static void SendActionCommand(const char* cmd) {
    SendUartCommand(cmd);
    // StartOrResetStopTimer();
}

// 立即停止（发送ZK并停止定时器）
static void SendStopCommand() {
    SendUartCommand(STOP_CMD);
    if (auto_stop_timer) {
        xTimerStop(auto_stop_timer, 0);
    }
}

// MCP工具注册
void RegisterMcpUartTools() {
    auto& mcp_server = McpServer::GetInstance();
    // 前进
    mcp_server.AddTool("self.uart.go_forward", "往前进移动，发送forward", PropertyList(), [](const PropertyList&) -> ReturnValue {
        SendActionCommand("forward\n");
        return true;
    });
    // 后退
    mcp_server.AddTool("self.uart.back_up", "往后退移动，发送backward", PropertyList(), [](const PropertyList&) -> ReturnValue {
        SendActionCommand("backward\n");
        return true;
    });
    // 左转
    mcp_server.AddTool("self.uart.turn_left", "往左转移动，发送rotate_left", PropertyList(), [](const PropertyList&) -> ReturnValue {
        SendActionCommand("rotate_left\n");
        return true;
    });
    // 右
    mcp_server.AddTool("self.uart.turn_right", "往右转移动，发送rotate_right", PropertyList(), [](const PropertyList&) -> ReturnValue {
        SendActionCommand("rotate_right\n");
        return true;
    });

        // 右移
    mcp_server.AddTool("self.uart.right", "往右方向移动，发送right", PropertyList(), [](const PropertyList&) -> ReturnValue {
        SendActionCommand("right\n");
        return true;
    });
        // 左移
    mcp_server.AddTool("self.uart.right", "往左方向移动，发送left", PropertyList(), [](const PropertyList&) -> ReturnValue {
        SendActionCommand("left\n");
        return true;
    });

    // 右上
    mcp_server.AddTool("self.uart.upper_right", "往右上移动，发送KB", PropertyList(), [](const PropertyList&) -> ReturnValue {
        SendActionCommand("JB");
        return true;
    });
    // 右下
    mcp_server.AddTool("self.uart.low_right", "往右下移动，发送KD", PropertyList(), [](const PropertyList&) -> ReturnValue {
        SendActionCommand("JD");
        return true;
    });
    // 左上
    mcp_server.AddTool("self.uart.up_left", "往左上移动，发送KH", PropertyList(), [](const PropertyList&) -> ReturnValue {
        SendActionCommand("JH");
        return true;
    });
    // 左下
    mcp_server.AddTool("self.uart.low_left", "往左下移动，发送KF", PropertyList(), [](const PropertyList&) -> ReturnValue {
        SendActionCommand("JF");
        return true;
    });
    // 加速
    mcp_server.AddTool("self.uart.speed_up", "加速，发送KX", PropertyList(), [](const PropertyList&) -> ReturnValue {
        SendActionCommand("KX");
        return true;
    });
    // 减速
    mcp_server.AddTool("self.uart.speed_cut", "减速，发送KY", PropertyList(), [](const PropertyList&) -> ReturnValue {
        SendActionCommand("KY");
        return true;
    });
    // 停止
    mcp_server.AddTool("self.uart.stop", "停止停下，发送stop", PropertyList(), [](const PropertyList&) -> ReturnValue {
        SendStopCommand();
        return true;
    });
    // 新增工具：接收串口数据并存储
    // mcp_server.AddTool("self.uart.receive_data", "接收串口数据并存储", PropertyList(), [](const PropertyList&) -> ReturnValue {
    //     // 调用带超时的接收函数，等待2秒
    //   std::string result = ReceiveUartDataWithTimeout(2000);
    //   ESP_LOGI(TAG, "工具返回结果: %s", result.c_str());
    //   return result; // 返回接收结果
    // });
    mcp_server.AddTool("self.uart.receive_data", "接收串口数据并存储", PropertyList(), [](const PropertyList&) -> ReturnValue {
        // 调用带超时的接收函数，等待2秒
      InitializeUart();
      std::string result = ReceiveUartDataWithTimeout(2000);
      ESP_LOGI(TAG, "工具返回结果: %s", result.c_str());
      return result; // 返回接收结果
});

    // 点头、摇头、转头工具
    mcp_server.AddTool("self.uart.servo_nob", "识别到需要做点头动作或者表示同意，发送nob", PropertyList(), [](const PropertyList&) -> ReturnValue {
        SendActionCommand("nob\n");
        return true;
    });
    mcp_server.AddTool("self.uart.servo_shake", "识别到需要做摇头动作或者表示否定，发送shake", PropertyList(), [](const PropertyList&) -> ReturnValue {
        SendActionCommand("shake\n");
        return true;
    });
     mcp_server.AddTool("self.uart.servo_circle", "转头演示，发送circle", PropertyList(), [](const PropertyList&) -> ReturnValue {
        SendActionCommand("circle\n");
        return true;
    });

}
