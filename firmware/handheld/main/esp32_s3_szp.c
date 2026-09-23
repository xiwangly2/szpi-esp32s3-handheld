#include <stdio.h>
#include <stdlib.h>
#include "esp32_s3_szp.h"
#include "ff.h"
#include "vfs_fat_internal.h"
#include <sys/stat.h>

static const char *TAG = "esp32_s3_szp";

/******************************************************************************/
/***************************  I2C ↓ *******************************************/
static i2c_master_bus_handle_t s_i2c_bus;
static i2c_master_dev_handle_t s_qmi8658_dev;
static i2c_master_dev_handle_t s_pca9557_dev;

static esp_err_t bsp_i2c_add_device(uint16_t addr, i2c_master_dev_handle_t *ret_handle)
{
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = BSP_I2C_FREQ_HZ,
    };

    return i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, ret_handle);
}

esp_err_t bsp_i2c_init(void)
{
    if (s_i2c_bus) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Starting I2C bus initialization...");

    i2c_master_bus_config_t i2c_conf = {
        .i2c_port = BSP_I2C_NUM,
        .sda_io_num = BSP_I2C_SDA,
        .scl_io_num = BSP_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = {
            .enable_internal_pullup = true,
        },
    };

    ESP_LOGI(TAG, "I2C config: port=%d, scl=%d, sda=%d, freq=%d", BSP_I2C_NUM, BSP_I2C_SCL, BSP_I2C_SDA, BSP_I2C_FREQ_HZ);

    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&i2c_conf, &s_i2c_bus), TAG, "I2C bus init failed");
    ESP_RETURN_ON_ERROR(bsp_i2c_add_device(QMI8658_SENSOR_ADDR, &s_qmi8658_dev), TAG, "QMI8658 I2C device init failed");
    ESP_RETURN_ON_ERROR(bsp_i2c_add_device(PCA9557_SENSOR_ADDR, &s_pca9557_dev), TAG, "PCA9557 I2C device init failed");

    ESP_LOGI(TAG, "I2C bus initialized successfully");
    return ESP_OK;
}

i2c_master_bus_handle_t bsp_i2c_get_bus_handle(void)
{
    return s_i2c_bus;
}
/***************************  I2C ↑  *******************************************/
/*******************************************************************************/


/*******************************************************************************/
/***************************  姿态传感器 QMI8658 ↓   ****************************/

// 读取QMI8658寄存器的值
esp_err_t qmi8658_register_read(uint8_t reg_addr, uint8_t *data, size_t len)
{
    if (s_qmi8658_dev == NULL || data == NULL || len == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = i2c_master_transmit_receive(s_qmi8658_dev, &reg_addr, 1, data, len, 1000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "QMI8658 read failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

// 给QMI8658的寄存器写值
esp_err_t qmi8658_register_write_byte(uint8_t reg_addr, uint8_t data)
{
    if (s_qmi8658_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t write_buf[2] = {reg_addr, data};
    esp_err_t ret = i2c_master_transmit(s_qmi8658_dev, write_buf, sizeof(write_buf), 1000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "QMI8658 write failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

// 初始化qmi8658
esp_err_t qmi8658_init(void)
{
    esp_err_t ret = ESP_OK;
    uint8_t id = 0; // 芯片的ID号

    for (uint8_t count = 0; count < 3; count++) {
        ret = qmi8658_register_read(QMI8658_WHO_AM_I, &id, 1); // 读芯片的ID号
        if (ret == ESP_OK && id == 0x05) {
            break;
        }
        vTaskDelay(100 / portTICK_PERIOD_MS);  // 延时100豪秒
    }

    if (ret != ESP_OK) {
        return ret;
    }
    if (id != 0x05) { // 判断读到的ID号是否是0x05
        ESP_LOGE(TAG, "QMI8658 unexpected WHO_AM_I: 0x%02x", id);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "QMI8658 OK!");  // 打印信息

    ESP_RETURN_ON_ERROR(qmi8658_register_write_byte(QMI8658_RESET, 0xb0), TAG, "QMI8658 reset failed");  // 复位
    vTaskDelay(10 / portTICK_PERIOD_MS);  // 延时10ms

    // 配置运动状态检测
    ESP_RETURN_ON_ERROR(qmi8658_register_write_byte(QMI8658_CATL1_L, 1), TAG, "QMI8658 config failed"); // AnyMotionXThr 必须是0~32之间的数
    ESP_RETURN_ON_ERROR(qmi8658_register_write_byte(QMI8658_CATL1_H, 1), TAG, "QMI8658 config failed"); // AnyMotionYThr 必须是0~32之间的数
    ESP_RETURN_ON_ERROR(qmi8658_register_write_byte(QMI8658_CATL2_L, 1), TAG, "QMI8658 config failed"); // AnyMotionZThr 必须是0~32之间的数
    ESP_RETURN_ON_ERROR(qmi8658_register_write_byte(QMI8658_CATL2_H, 1), TAG, "QMI8658 config failed"); // NoMotionXThr 必须是0~32之间的数
    ESP_RETURN_ON_ERROR(qmi8658_register_write_byte(QMI8658_CATL3_L, 1), TAG, "QMI8658 config failed"); // NoMotionYThr 必须是0~32之间的数
    ESP_RETURN_ON_ERROR(qmi8658_register_write_byte(QMI8658_CATL3_H, 1), TAG, "QMI8658 config failed"); // NoMotionZThr 必须是0~32之间的数
    ESP_RETURN_ON_ERROR(qmi8658_register_write_byte(QMI8658_CATL4_L, 0x77), TAG, "QMI8658 config failed"); // MOTION_MODE_CTRL 0111 0111
    ESP_RETURN_ON_ERROR(qmi8658_register_write_byte(QMI8658_CATL4_H, 0x01), TAG, "QMI8658 config failed"); // 0x01(means 1st command)
    ESP_RETURN_ON_ERROR(qmi8658_register_write_byte(QMI8658_CTRL9, 0x0E), TAG, "QMI8658 config failed"); // CTRL_CMD_CONFIGURE_MOTION

    ESP_RETURN_ON_ERROR(qmi8658_register_write_byte(QMI8658_CATL1_L, 1), TAG, "QMI8658 config failed"); // AnyMotionWindow
    ESP_RETURN_ON_ERROR(qmi8658_register_write_byte(QMI8658_CATL1_H, 1), TAG, "QMI8658 config failed"); // NoMotionWindow
    ESP_RETURN_ON_ERROR(qmi8658_register_write_byte(QMI8658_CATL2_L, 0xE8), TAG, "QMI8658 config failed"); // SigMotionWaitWindow[7:0]
    ESP_RETURN_ON_ERROR(qmi8658_register_write_byte(QMI8658_CATL2_H, 0x03), TAG, "QMI8658 config failed"); // SigMotionWaitWindow [15:8]
    ESP_RETURN_ON_ERROR(qmi8658_register_write_byte(QMI8658_CATL3_L, 0xE8), TAG, "QMI8658 config failed"); // SigMotionConfirmWindow[7:0]
    ESP_RETURN_ON_ERROR(qmi8658_register_write_byte(QMI8658_CATL3_H, 0x03), TAG, "QMI8658 config failed"); // SigMotionConfirmWindow[15:8]
    // qmi8658_register_write_byte(QMI8658_CATL4_L, 0x40); // NA
    ESP_RETURN_ON_ERROR(qmi8658_register_write_byte(QMI8658_CATL4_H, 0x02), TAG, "QMI8658 config failed"); // 0x02(means 2nd command)
    ESP_RETURN_ON_ERROR(qmi8658_register_write_byte(QMI8658_CTRL9, 0x0E), TAG, "QMI8658 config failed"); // CTRL_CMD_CONFIGURE_MOTION


    ESP_RETURN_ON_ERROR(qmi8658_register_write_byte(QMI8658_CTRL1, 0x40), TAG, "QMI8658 config failed"); // CTRL1 设置地址自动增加
    ESP_RETURN_ON_ERROR(qmi8658_register_write_byte(QMI8658_CTRL7, 0x03), TAG, "QMI8658 config failed"); // CTRL7 允许加速度和陀螺仪
    ESP_RETURN_ON_ERROR(qmi8658_register_write_byte(QMI8658_CTRL2, 0x95), TAG, "QMI8658 config failed"); // CTRL2 设置ACC 4g 250Hz
    ESP_RETURN_ON_ERROR(qmi8658_register_write_byte(QMI8658_CTRL3, 0xd5), TAG, "QMI8658 config failed"); // CTRL3 设置GRY 512dps 250Hz

    ESP_RETURN_ON_ERROR(qmi8658_register_write_byte(QMI8658_CTRL8, 0x0E), TAG, "QMI8658 config failed"); // CTRL7 允许Any-Motion No-Motion and Significant-Motion

    return ret;
}

// 关闭芯片运行
void qmi8658_close(void)
{
    if (s_qmi8658_dev != NULL) {
        qmi8658_register_write_byte(QMI8658_CTRL1, 0x01); // 关闭芯片运行
    }
}

// 读取加速度和陀螺仪寄存器值
esp_err_t qmi8658_Read_AccAndGry(t_sQMI8658 *p)
{
    if (p == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(p, 0, sizeof(*p));

    uint8_t status = 0;
    uint8_t buf[12] = {0};
    esp_err_t ret = qmi8658_register_read(QMI8658_STATUS0, &status, 1); // 读状态寄存器
    if (ret != ESP_OK) {
        return ret;
    }

    if (status & 0x03) // 判断加速度和陀螺仪数据是否可读
    {
        ret = qmi8658_register_read(QMI8658_AX_L, buf, sizeof(buf)); // 读加速度和陀螺仪值
        if (ret != ESP_OK) {
            return ret;
        }
        p->acc_x = (int16_t)((uint16_t)buf[1] << 8 | buf[0]);
        p->acc_y = (int16_t)((uint16_t)buf[3] << 8 | buf[2]);
        p->acc_z = (int16_t)((uint16_t)buf[5] << 8 | buf[4]);
        p->gyr_x = (int16_t)((uint16_t)buf[7] << 8 | buf[6]);
        p->gyr_y = (int16_t)((uint16_t)buf[9] << 8 | buf[8]);
        p->gyr_z = (int16_t)((uint16_t)buf[11] << 8 | buf[10]);
        return ESP_OK;
    }

    return ESP_ERR_INVALID_RESPONSE;
}

// 获取XYZ轴的倾角值
esp_err_t qmi8658_fetch_angleFromAcc(t_sQMI8658 *p)
{
    esp_err_t ret = qmi8658_Read_AccAndGry(p); // 读取加速度和陀螺仪的寄存器值
    if (ret != ESP_OK) {
        return ret;
    }

    float ax = (float)p->acc_x;
    float ay = (float)p->acc_y;
    float az = (float)p->acc_z;

    // 根据寄存器值计算倾角，并使用 atan2f 避免除零。
    p->AngleX = atan2f(ax, sqrtf(ay * ay + az * az)) * 57.29578f; // 180/π=57.29578
    p->AngleY = atan2f(ay, sqrtf(ax * ax + az * az)) * 57.29578f; // 180/π=57.29578
    p->AngleZ = atan2f(sqrtf(ax * ax + ay * ay), az) * 57.29578f; // 180/π=57.29578
    return ESP_OK;
}

// 获取Motion状态
esp_err_t qmi8658_fetch_motion(uint8_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *status = 0;
    return qmi8658_register_read(QMI8658_STATUS1, status, 1); // 读状态寄存器
}

/***************************  姿态传感器 QMI8658 ↑  ****************************/
/*******************************************************************************/


/***********************************************************/
/***************    IO扩展芯片 ↓   *************************/

// 读取PCA9557寄存器的值
esp_err_t pca9557_register_read(uint8_t reg_addr, uint8_t *data, size_t len)
{
    esp_err_t ret = i2c_master_transmit_receive(s_pca9557_dev, &reg_addr, 1, data, len, 1000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PCA9557 read failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

// 给PCA9557的寄存器写值
esp_err_t pca9557_register_write_byte(uint8_t reg_addr, uint8_t data)
{
    uint8_t write_buf[2] = {reg_addr, data};
    esp_err_t ret = i2c_master_transmit(s_pca9557_dev, write_buf, sizeof(write_buf), 1000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PCA9557 write failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

// 初始化PCA9557 IO扩展芯片
void pca9557_init(void)
{
    // 写入控制引脚默认值 DVP_PWDN=1  PA_EN = 0  LCD_CS = 1
    pca9557_register_write_byte(PCA9557_OUTPUT_PORT, 0x05);  
    // 把PCA9557芯片的IO1 IO1 IO2设置为输出 其它引脚保持默认的输入
    pca9557_register_write_byte(PCA9557_CONFIGURATION_PORT, 0xf8); 
}

// 设置PCA9557芯片的某个IO引脚输出高低电平
esp_err_t pca9557_set_output_state(uint8_t gpio_bit, uint8_t level)
{
    uint8_t data;
    esp_err_t res = ESP_FAIL;

    pca9557_register_read(PCA9557_OUTPUT_PORT, &data, 1);
    res = pca9557_register_write_byte(PCA9557_OUTPUT_PORT, SET_BITS(data, gpio_bit, level));

    return res;
}

// 控制 PCA9557_LCD_CS 引脚输出高低电平 参数0输出低电平 参数1输出高电平 
void lcd_cs(uint8_t level)
{
    pca9557_set_output_state(LCD_CS_GPIO, level);
}

// 控制 PCA9557_PA_EN 引脚输出高低电平 参数0输出低电平 参数1输出高电平 
void pa_en(uint8_t level)
{
    pca9557_set_output_state(PA_EN_GPIO, level);
}

// 控制 PCA9557_DVP_PWDN 引脚输出高低电平 参数0输出低电平 参数1输出高电平 
void dvp_pwdn(uint8_t level)
{
    pca9557_set_output_state(DVP_PWDN_GPIO, level);
}

/***************    IO扩展芯片 ↑   *************************/
/***********************************************************/


/***********************************************************/
/****************    LCD显示屏 ↓   *************************/

// 背光PWM初始化
esp_err_t bsp_display_brightness_init(void)
{
    // Setup LEDC peripheral for PWM backlight control
    const ledc_channel_config_t LCD_backlight_channel = {
        .gpio_num = BSP_LCD_BACKLIGHT,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LCD_LEDC_CH,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = 0,
        .duty = 0,
        .hpoint = 0,
        .flags.output_invert = true
    };
    const ledc_timer_config_t LCD_backlight_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = 0,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK
    };

    ESP_ERROR_CHECK(ledc_timer_config(&LCD_backlight_timer));
    ESP_ERROR_CHECK(ledc_channel_config(&LCD_backlight_channel));

    return ESP_OK;
}

// 背光亮度设置
esp_err_t bsp_display_brightness_set(int brightness_percent)
{
    if (brightness_percent > 100) {
        brightness_percent = 100;
    } else if (brightness_percent < 0) {
        brightness_percent = 0;
    }

    ESP_LOGI(TAG, "Setting LCD backlight: %d%%", brightness_percent);
    // LEDC resolution set to 10bits, thus: 100% = 1023
    uint32_t duty_cycle = (1023 * brightness_percent) / 100;
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LCD_LEDC_CH, duty_cycle));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LCD_LEDC_CH));

    return ESP_OK;
}

// 关闭背光
esp_err_t bsp_display_backlight_off(void)
{
    return bsp_display_brightness_set(0);
}

// 打开背光 最亮
esp_err_t bsp_display_backlight_on(void)
{
    return bsp_display_brightness_set(100);
}

// 定义液晶屏句柄
static esp_lcd_panel_handle_t panel_handle = NULL;
esp_lcd_panel_io_handle_t io_handle = NULL; 
static esp_lcd_touch_handle_t tp;   // 触摸屏句柄
static lv_disp_t *disp;      // 指向液晶屏
static lv_indev_t *disp_indev = NULL; // 指向触摸屏

// 液晶屏初始化
esp_err_t bsp_display_new(void)
{
    esp_err_t ret = ESP_OK;
    // 背光初始化
    ESP_RETURN_ON_ERROR(bsp_display_brightness_init(), TAG, "Brightness init failed");
    // 初始化SPI总线
    ESP_LOGD(TAG, "Initialize SPI bus");
    const spi_bus_config_t buscfg = {
        .sclk_io_num = BSP_LCD_SPI_CLK,
        .mosi_io_num = BSP_LCD_SPI_MOSI,
        .miso_io_num = GPIO_NUM_NC,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        //.max_transfer_sz = BSP_LCD_H_RES * BSP_LCD_V_RES * sizeof(uint16_t),
        .max_transfer_sz = BSP_LCD_H_RES * 40 * sizeof(uint16_t),
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(BSP_LCD_SPI_NUM, &buscfg, SPI_DMA_CH_AUTO), TAG, "SPI init failed");
    // 液晶屏控制IO初始化
    ESP_LOGD(TAG, "Install panel IO");
    const esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = BSP_LCD_DC,
        .cs_gpio_num = BSP_LCD_SPI_CS,
        .pclk_hz = BSP_LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = LCD_CMD_BITS,
        .lcd_param_bits = LCD_PARAM_BITS,
        .spi_mode = 2,
        .trans_queue_depth = 10,
    };
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)BSP_LCD_SPI_NUM, &io_config, &io_handle), err, TAG, "New panel IO failed");
    // 初始化液晶屏驱动芯片ST7789
    ESP_LOGD(TAG, "Install LCD driver");
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = BSP_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = BSP_LCD_BITS_PER_PIXEL,
    };
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle), err, TAG, "New panel failed");
    
    esp_lcd_panel_reset(panel_handle);  // 液晶屏复位
    lcd_cs(0);  // 拉低CS引脚
    esp_lcd_panel_init(panel_handle);  // 初始化配置寄存器
    esp_lcd_panel_invert_color(panel_handle, true); // 颜色反转
    esp_lcd_panel_swap_xy(panel_handle, true);  // 显示翻转 
    esp_lcd_panel_mirror(panel_handle, true, false); // 镜像

    return ret;

err:
    if (panel_handle) {
        esp_lcd_panel_del(panel_handle);
    }
    if (io_handle) {
        esp_lcd_panel_io_del(io_handle);
    }
    spi_bus_free(BSP_LCD_SPI_NUM);
    return ret;
}

// LCD显示初始化
esp_err_t bsp_lcd_init(void)
{
    esp_err_t ret = ESP_OK;

    
    ret = bsp_display_new(); // 液晶屏驱动初始化
    lcd_set_color(0x0000); // 设置整屏背景黑色
    ret = esp_lcd_panel_disp_on_off(panel_handle, true); // 打开液晶屏显示
    ret = bsp_display_backlight_on(); // 打开背光显示

    return  ret;
}

// 液晶屏初始化+添加LVGL接口
static lv_disp_t *bsp_display_lcd_init(void)
{
    /* 初始化液晶屏 */
    bsp_display_new(); // 液晶屏驱动初始化
    lcd_set_color(0xffff); // 设置整屏背景白色
    esp_lcd_panel_disp_on_off(panel_handle, true); // 打开液晶屏显示

    /* 液晶屏添加LVGL接口 */
    ESP_LOGD(TAG, "Add LCD screen");
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io_handle,
        .panel_handle = panel_handle,
        .buffer_size = BSP_LCD_H_RES * BSP_LCD_DRAW_BUF_HEIGHT,   // LVGL缓存大小 
        .double_buffer = true, // 是否开启双缓存
        .hres = BSP_LCD_H_RES, // 液晶屏的宽
        .vres = BSP_LCD_V_RES, // 液晶屏的高
        .monochrome = false,  // 是否单色显示器
        /* Rotation的值必须和液晶屏初始化里面设置的 翻转 和 镜像 一样 */
        .rotation = {
            .swap_xy = true,  // 是否翻转
            .mirror_x = true, // x方向是否镜像
            .mirror_y = false, // y方向是否镜像
        },
        .flags = {
            .buff_dma = true,  // 是否使用DMA 注意：dma与spiram不能同时为true
            .buff_spiram = false, // 是否使用PSRAM 注意：dma与spiram不能同时为true
        }
    };

    return lvgl_port_add_disp(&disp_cfg);
}

// 触摸屏初始化
esp_err_t bsp_touch_new(esp_lcd_touch_handle_t *ret_touch)
{
    ESP_LOGI(TAG, "Starting touch screen initialization...");
    
    /* Initialize touch */
    esp_lcd_touch_config_t tp_cfg = {
        .x_max = BSP_LCD_V_RES,
        .y_max = BSP_LCD_H_RES,
        .rst_gpio_num = GPIO_NUM_NC, // Shared with LCD reset
        .int_gpio_num = GPIO_NUM_NC, 
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 1,
            .mirror_x = 1,
            .mirror_y = 0,
        },
    };
    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG();
    tp_io_config.scl_speed_hz = 400000;
    
    ESP_LOGI(TAG, "Creating touch screen I2C IO with port: %d", BSP_I2C_NUM);
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(bsp_i2c_get_bus_handle(), &tp_io_config, &tp_io_handle), TAG, "");
    
    ESP_LOGI(TAG, "Creating FT5x06 touch screen driver");
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_ft5x06(tp_io_handle, &tp_cfg, ret_touch));
    
    ESP_LOGI(TAG, "Touch screen initialized successfully");
    return ESP_OK;
}

// 触摸屏初始化+添加LVGL接口
static lv_indev_t *bsp_display_indev_init(lv_disp_t *disp)
{
    /* 初始化触摸屏 */
    ESP_ERROR_CHECK(bsp_touch_new(&tp));
    assert(tp);

    /* 添加LVGL接口 */
    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = disp,
        .handle = tp,
    };

    return lvgl_port_add_touch(&touch_cfg);
}

// 开发板显示初始化
void bsp_lvgl_start(void)
{
    /* 初始化LVGL */
    lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_port_init(&lvgl_cfg);

    /* 初始化液晶屏 并添加LVGL接口 */
    disp = bsp_display_lcd_init();

    /* 初始化触摸屏 并添加LVGL接口 */
    disp_indev = bsp_display_indev_init(disp);

    /* 打开液晶屏背光 */
    bsp_display_backlight_on();

}

// 显示图片
void lcd_draw_pictrue(int x_start, int y_start, int x_end, int y_end, const unsigned char *gImage)
{
    // 分配内存 分配了需要的字节大小 且指定在外部SPIRAM中分配
    size_t pixels_byte_size = (x_end - x_start)*(y_end - y_start) * 2;
    uint16_t *pixels = (uint16_t *)heap_caps_malloc(pixels_byte_size, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
    if (NULL == pixels)
    {
        ESP_LOGE(TAG, "Memory for bitmap is not enough");
        return;
    }
    memcpy(pixels, gImage, pixels_byte_size);  // 把图片数据拷贝到内存
    esp_lcd_panel_draw_bitmap(panel_handle, x_start, y_start, x_end, y_end, (uint16_t *)pixels); // 显示整张图片数据
    heap_caps_free(pixels);  // 释放内存
}

// 设置液晶屏颜色
void lcd_set_color(uint16_t color)
{
    // 分配内存 这里分配了液晶屏一行数据需要的大小
    uint16_t *buffer = (uint16_t *)heap_caps_malloc(BSP_LCD_H_RES * sizeof(uint16_t), MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
    
    if (NULL == buffer)
    {
        ESP_LOGE(TAG, "Memory for bitmap is not enough");
    }
    else
    {
        for (size_t i = 0; i < BSP_LCD_H_RES; i++) // 给缓存中放入颜色数据
        {
            buffer[i] = color;
        }
        for (int y = 0; y < 240; y++) // 显示整屏颜色
        {
            esp_lcd_panel_draw_bitmap(panel_handle, 0, y, 320, y+1, buffer);
        }
        free(buffer); // 释放内存
    }
}
/***************    LCD显示屏 ↑   *************************/
/***********************************************************/



/***********************************************************/
/****************    摄像头 ↓   ****************************/
#if CAMERA_EN
// 定义lcd显示队列句柄
static QueueHandle_t xQueueLCDFrame = NULL;

// 摄像头硬件初始化
esp_err_t bsp_camera_init(void)
{
    ESP_LOGI(TAG, "Starting camera initialization...");
    
    dvp_pwdn(0); // 打开摄像头
    vTaskDelay(pdMS_TO_TICKS(80));

    esp_camera_deinit();
    vTaskDelay(pdMS_TO_TICKS(30));

    camera_config_t config = {0};
    config.ledc_channel = LEDC_CHANNEL_1;  // LEDC通道选择  用于生成XCLK时钟 但是S3不用
    config.ledc_timer = LEDC_TIMER_1; // LEDC timer选择  用于生成XCLK时钟 但是S3不用
    config.pin_d0 = CAMERA_PIN_D0;
    config.pin_d1 = CAMERA_PIN_D1;
    config.pin_d2 = CAMERA_PIN_D2;
    config.pin_d3 = CAMERA_PIN_D3;
    config.pin_d4 = CAMERA_PIN_D4;
    config.pin_d5 = CAMERA_PIN_D5;
    config.pin_d6 = CAMERA_PIN_D6;
    config.pin_d7 = CAMERA_PIN_D7;
    config.pin_xclk = CAMERA_PIN_XCLK;
    config.pin_pclk = CAMERA_PIN_PCLK;
    config.pin_vsync = CAMERA_PIN_VSYNC;
    config.pin_href = CAMERA_PIN_HREF;
    config.pin_sccb_sda = -1;   // 这里写-1 表示使用已经初始化的I2C接口
    config.pin_sccb_scl = CAMERA_PIN_SIOC;
    config.sccb_i2c_port = BSP_I2C_NUM;
    config.pin_pwdn = CAMERA_PIN_PWDN;
    config.pin_reset = CAMERA_PIN_RESET;
    config.xclk_freq_hz = XCLK_FREQ_HZ;
    config.pixel_format = PIXFORMAT_RGB565;
    config.frame_size = FRAMESIZE_QVGA;
    config.jpeg_quality = 12;
    config.fb_count = 2;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
    
    ESP_LOGI(TAG, "Camera config: sccb_i2c_port=%d, pin_sccb_sda=%d, pin_sccb_scl=%d", config.sccb_i2c_port, config.pin_sccb_sda, config.pin_sccb_scl);

    // camera init
    esp_err_t err = esp_camera_init(&config); // 配置上面定义的参数
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Camera init failed with error 0x%x", err);
        dvp_pwdn(1);
        return err;
    }

    sensor_t *s = esp_camera_sensor_get(); // 获取摄像头型号

    if (s->id.PID == GC0308_PID) {
        s->set_hmirror(s, 1);  // 这里控制摄像头镜像 写1镜像 写0不镜像
    }
    return ESP_OK;
}

int camera_lcd_flag = 0;
// lcd处理任务
static void task_process_lcd(void *arg)
{
    camera_fb_t *frame = NULL;

    while (camera_lcd_flag)
    {
        if (xQueueReceive(xQueueLCDFrame, &frame, portMAX_DELAY))
        {
            esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, frame->width, frame->height, (uint16_t *)frame->buf);
            esp_camera_fb_return(frame);
        }
    }
}

// 摄像头处理任务
static void task_process_camera(void *arg)
{
    while (camera_lcd_flag)
    {
        camera_fb_t *frame = esp_camera_fb_get();
        if (frame)
            xQueueSend(xQueueLCDFrame, &frame, portMAX_DELAY);
    }
}

// 让摄像头显示到LCD
void app_camera_lcd(void)
{
    camera_lcd_flag = 1;
    xQueueLCDFrame = xQueueCreate(2, sizeof(camera_fb_t *));
    xTaskCreatePinnedToCore(task_process_camera, "task_process_camera", 2 * 1024, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(task_process_lcd, "task_process_lcd", 2 * 1024, NULL, 5, NULL, 0);
}

#endif
/********************    摄像头 ↑   *************************/
/***********************************************************/

/***********************************************************/
/*********************    SD卡  ↓   *********************/
sdmmc_card_t *sdmmc_card = NULL;

esp_err_t bsp_sdcard_prepare_product_dirs(void)
{
    ESP_RETURN_ON_ERROR(bsp_sdcard_mount(), TAG, "mount sdcard failed");
    mkdir(SD_MOUNT_POINT "/szpi", 0775);
    mkdir(SD_MOUNT_POINT "/szpi/config", 0775);
    mkdir(SD_MOUNT_POINT "/szpi/cache", 0775);
    mkdir(SD_MOUNT_POINT "/szpi/cache/random", 0775);
    mkdir(SD_MOUNT_POINT "/szpi/music", 0775);
    mkdir(SD_MOUNT_POINT "/szpi/photos", 0775);
    mkdir(SD_MOUNT_POINT "/szpi/recordings", 0775);
    return ESP_OK;
}

static BYTE bsp_sdcard_format_flag(bsp_sdcard_format_t format)
{
    switch (format) {
    case BSP_SDCARD_FORMAT_FAT32:
        return FM_FAT32;
    case BSP_SDCARD_FORMAT_EXFAT:
        return FM_EXFAT;
    case BSP_SDCARD_FORMAT_AUTO:
    default:
        return FM_ANY;
    }
}

static uint16_t bsp_rd16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t bsp_rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t bsp_rd64(const uint8_t *p)
{
    return (uint64_t)bsp_rd32(p) | ((uint64_t)bsp_rd32(p + 4) << 32);
}

static const char *bsp_sdcard_fs_type_name(BYTE fs_type)
{
    switch (fs_type) {
    case FS_FAT12:
        return "FAT12";
    case FS_FAT16:
        return "FAT16";
    case FS_FAT32:
        return "FAT32";
#if FF_FS_EXFAT
    case FS_EXFAT:
        return "exFAT";
#endif
    default:
        return fs_type == 0 ? "unmounted" : "FAT";
    }
}

static const char *bsp_sdcard_mbr_type_name(uint8_t type)
{
    switch (type) {
    case 0x01:
        return "FAT12";
    case 0x04:
    case 0x06:
    case 0x0E:
        return "FAT16";
    case 0x0B:
    case 0x0C:
        return "FAT32";
    case 0x07:
        return "exFAT/NTFS";
    case 0x0F:
        return "Extended";
    case 0xEE:
        return "GPT protective";
    default:
        return "MBR part";
    }
}

static void bsp_sdcard_parse_gpt_name(const uint8_t *entry, char *out, size_t out_len)
{
    size_t pos = 0;
    if (out_len == 0) {
        return;
    }

    for (size_t i = 56; i + 1 < 128 && pos + 1 < out_len; i += 2) {
        uint16_t ch = bsp_rd16(entry + i);
        if (ch == 0) {
            break;
        }
        out[pos++] = (ch >= 0x20 && ch <= 0x7e) ? (char)ch : '?';
    }
    out[pos] = '\0';
    if (pos == 0) {
        snprintf(out, out_len, "GPT part");
    }
}

static bool bsp_sdcard_sector_has_fat_signature(const uint8_t *sector)
{
    return memcmp(sector + 3, "EXFAT   ", 8) == 0 ||
           memcmp(sector + 54, "FAT", 3) == 0 ||
           memcmp(sector + 82, "FAT32", 5) == 0;
}

static void bsp_sdcard_scan_mbr(const uint8_t *sector, bsp_sdcard_info_t *info)
{
    uint8_t count = 0;

    for (uint8_t i = 0; i < BSP_SDCARD_MAX_PARTITIONS; i++) {
        const uint8_t *entry = sector + 446 + (i * 16);
        uint8_t type = entry[4];
        uint32_t first_lba = bsp_rd32(entry + 8);
        uint32_t sectors = bsp_rd32(entry + 12);
        if (type == 0 || sectors == 0) {
            continue;
        }

        bsp_sdcard_partition_info_t *part = &info->partitions[count++];
        part->valid = true;
        part->bootable = entry[0] == 0x80;
        part->index = i + 1;
        part->mbr_type = type;
        part->first_lba = first_lba;
        part->sectors = sectors;
        part->last_lba = first_lba + (uint64_t)sectors - 1;
        snprintf(part->name, sizeof(part->name), "%s", bsp_sdcard_mbr_type_name(type));
    }

    if (count > 0) {
        info->table_type = BSP_SDCARD_TABLE_MBR;
        info->partition_count = count;
    } else if (bsp_sdcard_sector_has_fat_signature(sector)) {
        info->table_type = BSP_SDCARD_TABLE_NONE;
    }
}

static esp_err_t bsp_sdcard_scan_gpt(uint32_t sector_size, bsp_sdcard_info_t *info)
{
    uint8_t *sector = calloc(1, sector_size);
    if (sector == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = sdmmc_read_sectors(sdmmc_card, sector, 1, 1);
    if (ret != ESP_OK) {
        free(sector);
        return ret;
    }
    if (memcmp(sector, "EFI PART", 8) != 0) {
        free(sector);
        return ESP_ERR_NOT_FOUND;
    }

    uint64_t entries_lba = bsp_rd64(sector + 72);
    uint32_t entry_count = bsp_rd32(sector + 80);
    uint32_t entry_size = bsp_rd32(sector + 84);
    if (entry_size < 128 || entry_size > sector_size || entries_lba == 0) {
        free(sector);
        return ESP_ERR_INVALID_RESPONSE;
    }

    info->table_type = BSP_SDCARD_TABLE_GPT;
    uint8_t count = 0;
    uint32_t max_entries = entry_count < BSP_SDCARD_MAX_PARTITIONS ? entry_count : BSP_SDCARD_MAX_PARTITIONS;
    for (uint32_t i = 0; i < max_entries; i++) {
        uint64_t byte_offset = (uint64_t)i * entry_size;
        uint64_t lba = entries_lba + (byte_offset / sector_size);
        uint32_t offset = byte_offset % sector_size;
        ret = sdmmc_read_sectors(sdmmc_card, sector, lba, 1);
        if (ret != ESP_OK) {
            break;
        }

        const uint8_t *entry = sector + offset;
        bool empty_guid = true;
        for (uint8_t g = 0; g < 16; g++) {
            if (entry[g] != 0) {
                empty_guid = false;
                break;
            }
        }
        if (empty_guid) {
            continue;
        }

        uint64_t first_lba = bsp_rd64(entry + 32);
        uint64_t last_lba = bsp_rd64(entry + 40);
        if (first_lba == 0 || last_lba < first_lba) {
            continue;
        }

        bsp_sdcard_partition_info_t *part = &info->partitions[count++];
        part->valid = true;
        part->index = i + 1;
        part->first_lba = first_lba;
        part->last_lba = last_lba;
        part->sectors = last_lba - first_lba + 1;
        bsp_sdcard_parse_gpt_name(entry, part->name, sizeof(part->name));
    }

    info->partition_count = count;
    free(sector);
    return ESP_OK;
}

esp_err_t bsp_sdcard_get_info(bsp_sdcard_info_t *info)
{
    if (info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(info, 0, sizeof(*info));
    info->table_type = BSP_SDCARD_TABLE_UNKNOWN;
    snprintf(info->filesystem, sizeof(info->filesystem), "unknown");

    ESP_RETURN_ON_ERROR(bsp_sdcard_mount(), TAG, "mount sdcard before info failed");
    if (sdmmc_card == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    info->mounted = true;
    info->sector_size = sdmmc_card->csd.sector_size;
    if (info->sector_size == 0) {
        info->sector_size = 512;
    }
    info->card_sectors = sdmmc_card->csd.capacity;
    info->card_bytes = info->card_sectors * info->sector_size;

    vfs_fat_sd_ctx_t *ctx = get_vfs_fat_get_sd_ctx(sdmmc_card);
    if (ctx != NULL && ctx->fs != NULL) {
        snprintf(info->filesystem, sizeof(info->filesystem), "%s",
                 bsp_sdcard_fs_type_name(ctx->fs->fs_type));
    }

    uint8_t *sector = calloc(1, info->sector_size);
    if (sector == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = sdmmc_read_sectors(sdmmc_card, sector, 0, 1);
    if (ret != ESP_OK) {
        free(sector);
        return ret;
    }

    if (bsp_rd16(sector + 510) == 0xAA55) {
        bool protective_gpt = false;
        for (uint8_t i = 0; i < 4; i++) {
            if (sector[446 + i * 16 + 4] == 0xEE) {
                protective_gpt = true;
                break;
            }
        }

        if (protective_gpt && bsp_sdcard_scan_gpt(info->sector_size, info) == ESP_OK) {
            free(sector);
            return ESP_OK;
        }
        bsp_sdcard_scan_mbr(sector, info);
    }

    if (info->table_type == BSP_SDCARD_TABLE_UNKNOWN && bsp_sdcard_sector_has_fat_signature(sector)) {
        info->table_type = BSP_SDCARD_TABLE_NONE;
    }

    free(sector);
    return ESP_OK;
}

// 挂载SD卡
esp_err_t bsp_sdcard_mount(void)
{
    if (sdmmc_card != NULL) {
        ESP_LOGI(TAG, "SD card already mounted");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Mounting SD card");
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,  // 加载不成功是否需要格式化
        .max_files = 5,                   // 最大文件数
        .allocation_unit_size = 8 * 1024
    };

    sdmmc_host_t sdmmc_host = SDMMC_HOST_DEFAULT(); // SDMMC主机接口配置
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT(); // SDMMC插槽配置
    slot_config.width = 1;  // 设置为1线SD模式
    slot_config.clk = SD_CLK_IO; 
    slot_config.cmd = SD_CMD_IO;
    slot_config.d0 = SD_DAT0_IO;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP; // 打开内部上拉电阻

    ESP_LOGI(TAG, "Mounting filesystem Starting");

    esp_err_t ret = esp_vfs_fat_sdmmc_mount(SD_MOUNT_POINT, &sdmmc_host, &slot_config, &mount_config, &sdmmc_card);

    return ret;
}

esp_err_t bsp_sdcard_format(bsp_sdcard_format_t format)
{
    ESP_RETURN_ON_ERROR(bsp_sdcard_mount(), TAG, "mount sdcard before format failed");
    if (sdmmc_card == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    vfs_fat_sd_ctx_t *ctx = get_vfs_fat_get_sd_ctx(sdmmc_card);
    if (ctx == NULL || ctx->fs == NULL || ctx->pdrv == FF_DRV_NOT_USED) {
        ESP_LOGE(TAG, "SD card VFS context is not ready");
        return ESP_ERR_INVALID_STATE;
    }

    char drv[3] = {(char)('0' + ctx->pdrv), ':', 0};
    FRESULT res = f_mount(0, drv, 0);
    if (res != FR_OK) {
        ESP_LOGE(TAG, "f_mount unmount failed (%d)", res);
        return ESP_FAIL;
    }

    const size_t workbuf_size = 4096;
    void *workbuf = ff_memalloc(workbuf_size);
    if (workbuf == NULL) {
        (void)f_mount(ctx->fs, drv, 1);
        return ESP_ERR_NO_MEM;
    }

    size_t alloc_unit_size = esp_vfs_fat_get_allocation_unit_size(
        sdmmc_card->csd.sector_size,
        ctx->mount_config.allocation_unit_size ? ctx->mount_config.allocation_unit_size : 8 * 1024);
    MKFS_PARM opt = {
        .fmt = bsp_sdcard_format_flag(format),
        .n_fat = ctx->mount_config.use_one_fat ? 1 : 2,
        .align = 0,
        .n_root = ctx->mount_config.rootdir_entries,
        .au_size = alloc_unit_size,
    };

    ESP_LOGW(TAG, "Formatting SD card: fmt=0x%x au=%u", opt.fmt, (unsigned)alloc_unit_size);
    res = f_mkfs(drv, &opt, workbuf, workbuf_size);
    ff_memfree(workbuf);

    FRESULT remount_res = f_mount(ctx->fs, drv, 1);
    if (res != FR_OK) {
        ESP_LOGE(TAG, "f_mkfs failed (%d)", res);
        return ESP_FAIL;
    }
    if (remount_res != FR_OK) {
        ESP_LOGE(TAG, "f_mount remount failed (%d)", remount_res);
        return ESP_FAIL;
    }

    ESP_RETURN_ON_ERROR(bsp_sdcard_prepare_product_dirs(), TAG, "prepare sdcard dirs failed");
    return ESP_OK;
}

esp_err_t bsp_sdcard_unmount(void)
{
    if (sdmmc_card == NULL) {
        return ESP_OK;
    }

    esp_err_t ret = esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, sdmmc_card);
    if (ret == ESP_OK) {
        sdmmc_card = NULL;
    }
    return ret;
}
/**********************    SD卡 ↑  ************************/
/**********************************************************/



/***********************************************************/
/*********************    音频 ↓   *************************/
static esp_codec_dev_handle_t play_dev_handle;    // speaker句柄
static esp_codec_dev_handle_t record_dev_handle;  // microphone句柄

static i2s_chan_handle_t i2s_tx_chan = NULL; // 发送通道
static i2s_chan_handle_t i2s_rx_chan = NULL; // 接收通道
static const audio_codec_data_if_t *i2s_data_if = NULL;  /* Codec data interface */


// I2S总线初始化
esp_err_t bsp_audio_init(void)
{
    esp_err_t ret = ESP_FAIL;
    if (i2s_tx_chan && i2s_rx_chan) {
        /* Audio was initialized before */
        return ESP_OK;
    }

    /* Setup I2S peripheral */
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(BSP_I2S_NUM, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true; // Auto clear the legacy data in the DMA buffer
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &i2s_tx_chan, &i2s_rx_chan));

    /* Setup I2S channels */
    const i2s_std_config_t std_cfg_default = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(16000),   // 采样率16000
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(32, I2S_SLOT_MODE_STEREO),  // 32位 2通道
        .gpio_cfg = { 
            .mclk = GPIO_I2S_MCLK, 
            .bclk = GPIO_I2S_SCLK, 
            .ws   = GPIO_I2S_LRCK, 
            .dout = GPIO_I2S_DOUT, 
            .din  = GPIO_I2S_SDIN,
        },
    };

    if (i2s_tx_chan != NULL) {
        ESP_GOTO_ON_ERROR(i2s_channel_init_std_mode(i2s_tx_chan, &std_cfg_default), err, TAG, "I2S channel initialization failed");
        ESP_GOTO_ON_ERROR(i2s_channel_enable(i2s_tx_chan), err, TAG, "I2S enabling failed");
    }
    if (i2s_rx_chan != NULL) {
        ESP_GOTO_ON_ERROR(i2s_channel_init_std_mode(i2s_rx_chan, &std_cfg_default), err, TAG, "I2S channel initialization failed");
        ESP_GOTO_ON_ERROR(i2s_channel_enable(i2s_rx_chan), err, TAG, "I2S enabling failed");
    }

    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = BSP_I2S_NUM,
        .rx_handle = i2s_rx_chan,
        .tx_handle = i2s_tx_chan,
    };
    i2s_data_if = audio_codec_new_i2s_data(&i2s_cfg);
    if (i2s_data_if == NULL) {   
        goto err;
    }   

    return ESP_OK;

err:
    if (i2s_tx_chan) {
        i2s_del_channel(i2s_tx_chan);
    }
    if (i2s_rx_chan) {
        i2s_del_channel(i2s_rx_chan);
    }

    return ret;
}

// 初始化音频输出芯片
esp_codec_dev_handle_t bsp_audio_codec_speaker_init(void)
{
    ESP_LOGI(TAG, "Starting audio codec speaker initialization...");
    
    if (i2s_data_if == NULL) {
        /* Configure I2S peripheral and Power Amplifier */
        ESP_LOGI(TAG, "Initializing I2S peripheral...");
        ESP_ERROR_CHECK(bsp_audio_init());
    }
    assert(i2s_data_if);

    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();

    ESP_LOGI(TAG, "Creating I2C control interface for ES8311 codec with port: %d", BSP_I2C_NUM);
    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = BSP_I2C_NUM,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = bsp_i2c_get_bus_handle(),
    };
    const audio_codec_ctrl_if_t *i2c_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    assert(i2c_ctrl_if);
    ESP_LOGI(TAG, "I2C control interface created successfully");

    esp_codec_dev_hw_gain_t gain = {
        .pa_voltage = 5.0,
        .codec_dac_voltage = 3.3,
    };

    es8311_codec_cfg_t es8311_cfg = {
        .ctrl_if = i2c_ctrl_if,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = GPIO_PWR_CTRL,
        .pa_reverted = false,
        .master_mode = false,
        .use_mclk = true,
        .digital_mic = false,
        .invert_mclk = false,
        .invert_sclk = false,
        .hw_gain = gain,
    };
    const audio_codec_if_t *es8311_dev = es8311_codec_new(&es8311_cfg);
    assert(es8311_dev);

    esp_codec_dev_cfg_t codec_dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = es8311_dev,
        .data_if = i2s_data_if,
    };
    return esp_codec_dev_new(&codec_dev_cfg);
}

// 初始化音频输入芯片
esp_codec_dev_handle_t bsp_audio_codec_microphone_init(void)
{
    ESP_LOGI(TAG, "Starting audio codec microphone initialization...");
    
    if (i2s_data_if == NULL) {
        /* Configure I2S peripheral and Power Amplifier */
        ESP_LOGI(TAG, "Initializing I2S peripheral...");
        ESP_ERROR_CHECK(bsp_audio_init());
    }
    assert(i2s_data_if);

    ESP_LOGI(TAG, "Creating I2C control interface for ES7210 codec with port: %d", BSP_I2C_NUM);
    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = BSP_I2C_NUM,
        .addr = 0x82,
        .bus_handle = bsp_i2c_get_bus_handle(),
    };
    const audio_codec_ctrl_if_t *i2c_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    assert(i2c_ctrl_if);
    ESP_LOGI(TAG, "I2C control interface created successfully");

    es7210_codec_cfg_t es7210_cfg = {
        .ctrl_if = i2c_ctrl_if,
        .mic_selected = ES7120_SEL_MIC1 | ES7120_SEL_MIC2 | ES7120_SEL_MIC3 | ES7120_SEL_MIC4,
    };
    const audio_codec_if_t *es7210_dev = es7210_codec_new(&es7210_cfg);
    assert(es7210_dev);

    esp_codec_dev_cfg_t codec_es7210_dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
        .codec_if = es7210_dev,
        .data_if = i2s_data_if,
    };
    return esp_codec_dev_new(&codec_es7210_dev_cfg);
}

// 设置采样率
esp_err_t bsp_codec_set_fs(uint32_t rate, uint32_t bits_cfg, i2s_slot_mode_t ch)
{
    esp_err_t ret = ESP_OK;

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = rate,
        .channel = ch,
        .bits_per_sample = bits_cfg,
    };
    
    if (play_dev_handle) {
        ret = esp_codec_dev_close(play_dev_handle);
    }
    if (record_dev_handle) {
        ret |= esp_codec_dev_close(record_dev_handle);
        ret |= esp_codec_dev_set_in_gain(record_dev_handle, CODEC_DEFAULT_ADC_VOLUME);
    }

    if (play_dev_handle) {
        ret |= esp_codec_dev_open(play_dev_handle, &fs);
    }
    if (record_dev_handle) {
        ret |= esp_codec_dev_open(record_dev_handle, &fs);
    }
    return ret;
}

// 音频芯片初始化
esp_err_t bsp_codec_init(void)
{
    play_dev_handle = bsp_audio_codec_speaker_init();
    assert((play_dev_handle) && "play_dev_handle not initialized");

    record_dev_handle = bsp_audio_codec_microphone_init();
    assert((record_dev_handle) && "record_dev_handle not initialized");

    bsp_codec_set_fs(CODEC_DEFAULT_SAMPLE_RATE, CODEC_DEFAULT_BIT_WIDTH, CODEC_DEFAULT_CHANNEL);
    esp_codec_dev_set_out_vol(play_dev_handle, VOLUME_DEFAULT);

    return ESP_OK;
}

// 播放音乐
esp_err_t bsp_i2s_write(void *audio_buffer, size_t len, size_t *bytes_written, uint32_t timeout_ms)
{
    esp_err_t ret = ESP_OK;
    ret = esp_codec_dev_write(play_dev_handle, audio_buffer, len);
    *bytes_written = len;
    return ret;
}

// 设置静音与否
esp_err_t bsp_codec_mute_set(bool enable)
{
    esp_err_t ret = ESP_OK;
    ret = esp_codec_dev_set_out_mute(play_dev_handle, enable);
    return ret;
}

// 设置喇叭音量
esp_err_t bsp_codec_volume_set(int volume, int *volume_set)
{
    esp_err_t ret = ESP_OK;
    float v = volume;
    ret = esp_codec_dev_set_out_vol(play_dev_handle, (int)v);
    return ret;
}

// 获取MIC通道数
int bsp_get_feed_channel(void)
{
    return ADC_I2S_CHANNEL;
}

// 把I2S数据重新排布成模型需要的顺序
esp_err_t bsp_get_feed_data(bool is_get_raw_channel, int16_t *buffer, int buffer_len)
{
    esp_err_t ret = ESP_OK;
    
    int audio_chunksize = buffer_len / (sizeof(int16_t) * ADC_I2S_CHANNEL);

    ret = esp_codec_dev_read(record_dev_handle, (void *)buffer, buffer_len);
    
    if (!is_get_raw_channel) {
        for (int i = 0; i < audio_chunksize; i++) {
            int16_t ref = buffer[4 * i + 0];
            buffer[3 * i + 0] = buffer[4 * i + 1];
            buffer[3 * i + 1] = buffer[4 * i + 3];
            buffer[3 * i + 2] = ref;
        }
    }

    return ret;
}

/*********************    音频 ↑   *************************/
/***********************************************************/


