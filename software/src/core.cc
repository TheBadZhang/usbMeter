#include "common.h"
#include "core.h"

#include "trick.h"
#include "main.h"

#include <stdio.h>
char strbuf[128] = {0};
uint8_t str_len = 0;

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "font.h"
#include "ssd1312.h"

#include "ina226.h"

#include "ws2812.h"





char uart_rx_buf[32];

bool CH217_OUT_FLAG = true;





float bus_voltage;
int16_t current_raw;
float current;
uint16_t power_raw;
float power;
float power_sum = 0;


void ina226_sample(void) {
	bus_voltage = ina226_getBusV();
	current = ina226_getCurrent();
	power_raw = ina226_getPowerReg();
	power = ina226_getPower();

	// 将电压、电流、功率数据传给上位机
	int bus_voltage_i = bus_voltage * 10000;
	int current_i = current * 10000;
	int power_i = power*10000;
	str_len = sprintf(strbuf, "ina226: %d.%04d, %d.%04d, %d.%04d\r\n",
		bus_voltage_i/10000, bus_voltage_i%10000,
		current_i/10000, current_i%10000,
		power_i/10000, power_i%10000);
	HAL_UART_Transmit_DMA(&huart2, (const uint8_t*)strbuf, str_len);
}



uint16_t returnmax(uint16_t* list, uint8_t len) {
	uint16_t max = 0;
	for (uint8_t i = 0; i < len; i++) {
		if (list[i] > max) {
			max = list[i];
		}
	}
	return max;
}
uint16_t returnmin(uint16_t* list, uint8_t len) {
	uint16_t min = 60000;
	for (uint8_t i = 0; i < len; i++) {
		if (list[i] < min) {
			min = list[i];
		}
	}
	return min;
}

class STATUS_BAR {
	uint16_t list[128] = {0};
	int32_t sum = 0;
	int32_t average = 0;
	int32_t d;
	uint8_t index = 0;
	bool if_index_max = false;

	// uint8_t x, y, w;
	uint8_t hw;   // 宽度的一半
public:

	void set_w(uint8_t w) {
		hw = w/2;
	}

	//* 数据条相关数值计算
	void calc(const uint16_t power_raw) {
		sum -= list[index];           // 减去旧值
		list[index] = power_raw;      // 更新新值
		sum += list[index];           // 加上新值

		// 计算平均值
		// 根据计数长短，平均值计算有所不同
		if (if_index_max) {
			average = sum / 128;
			d = (int32_t)power_raw*hw - sum*hw / 128;
		} else {
			average = sum / (index+1);
			if (index == 127) {
				if_index_max = true;
			}
			d = (power_raw - average) * hw;
		}

		index = (index + 1) % 128;

	}

	void draw(uint8_t y) {

		// ssd1312_showchar(64, 7, 0, num_6x8, 6, 8);

		if (d >= 0) {
			d /= (returnmax(list, 128) - average + 1);
			//* 样式1
			// ssd1312_drawSegs(64, 8, 64+d, 8, 0xff);

			//* 样式2
			for (uint8_t i = hw; i < hw+1+d; i++) {
				if (i%2==0) {
					if ((i-hw) % 10 == 0) {
						ssd1312_showchar(i-2, y+1, (i-hw)/10, num_6x8, 6, 8);
						ssd1312_drawSeg(i, y, 0x7f);
					} else {
						ssd1312_drawSeg(i, y, 0x78);
					}
				}
			}

		} else {
			d /= (average - returnmin(list, 128) + 1);

			//* 样式1
			// ssd1312_drawSegs(64+d, 8, 64, 8, 0xff);

			//* 样式2
			for (uint8_t i = hw; i > hw-1+d; i--) {
				if (i%2==0) {
					if ((hw-i) % 10 == 0) {
						ssd1312_showchar(i-2, y+1, (hw-i)/10, num_6x8, 6, 8);
						ssd1312_drawSeg(i, y, 0x7f);
					} else {
						ssd1312_drawSeg(i, y, 0x78);
					}
				}
			}
		}
	}

} power_bar;


// 浮点数显示
void show_float(uint8_t x, uint8_t y, const float num) {
	// 位数控制，保证显示的数字个数为4个
	uint8_t fixed = 3;
	if (num >= 1000) {
		fixed = 0;
	} else if (num >= 100) {
		fixed = 1;
	} else if (num >= 10) {
		fixed = 2;
	}
	// -u _printf_float
	// str_len = sprintf(strbuf, "%.*f", fixed, num);         // 如果空间充足的话可以使用这个输出浮点数
	int out_num = num*1000;
	str_len = sprintf(strbuf, "%d.%0*d", out_num/1000, fixed, out_num%1000);
	// 因为画面中只能显示5位数字，所以这边设吹了人为上限为 5 位
	for (uint8_t i = 0; i < 5; i++) {
		if (isnum(strbuf[i])) {
			ssd1312_showchar(x, y, strbuf[i]-'0', num_10x24, 10, 24);
			x += 11;
		} else if (strbuf[i] == '.' && i != 4) {
			ssd1312_drawBox(x+1, y*8+18, 3, 3, 1);
			x += 6;
		}// 否则什么也不做
	}
}

// 表主界面UI
void meterUI(void) {
	uint8_t v_x, v_y;
	uint8_t c_x, c_y;
	uint8_t p_x, p_y;
	uint8_t pm_x, pm_y;
	uint8_t pb_y;
	static int str_x = 0;
	static bool str_reverse = false;

	if (ssd1312_rotation == 1 || ssd1312_rotation == 3) {
		v_x = 0;
		v_y = 0;
		c_x = 0;
		c_y = 3;
		p_x = 64;
		p_y = 0;
		pm_x = 64;
		pm_y = 3;
		pb_y = 6;
	} else {
		v_x = 0;
		v_y = 0;
		c_x = 0;
		c_y = 3;
		p_x = 0;
		p_y = 6;
		pm_x = 0;
		pm_y = 9;
		pb_y = 14;
	}

	// 电压
	show_float(v_x, v_y, bus_voltage);
	ssd1312_showchar(v_x+50, v_y+1, 0, char_V2, 9, 16);

	// 电流
	// str_len = sprintf(strbuf, "%f", current);
	// HAL_UART_Transmit(&huart2, (uint8_t*)strbuf, str_len, 1000);
	// current *= 1;
	show_float(c_x, c_y, current);
	// ssd1312_showchar(c_x+50, c_y+1, 0, char_A2, 9, 16);
	ssd1312_showchar(c_x+50, c_y+1, 0, char_m, 6, 8);
	ssd1312_showchar(c_x+50+6, c_y+1, 0, char_A, 6, 8);

	// 功率
	show_float(p_x, p_y, power);
	// ssd1312_showchar(p_x+50, p_y+1, 0, char_W2, 9, 16);
	ssd1312_showchar(p_x+50, p_y+1, 0, char_m, 6, 8);
	ssd1312_showchar(p_x+50+6, p_y+1, 0, char_W, 6, 8);

	// 累计功率（瓦分钟）
	show_float(pm_x, pm_y, power_sum/60.0);
	ssd1312_showchar(pm_x+50, pm_y+1, 0, char_W, 6, 8);
	ssd1312_showchar(pm_x+50+6, pm_y+1, 0, char_m, 6, 8);


	// 竖屏显示下会多一块区域可以用于额外信息的显示
	if (ssd1312_rotation == 0 || ssd1312_rotation == 2) {
		str_len = sprintf(strbuf, "THIS IS A SO FUCKING LONG SCENTANCE THAT CANNOT DISPLAY NORMALLY!!");
		int8_t lines = 2;
		// ssd1312_showstr(str_x, 12, strbuf, 0, font_Fixedsys, 8, 16, 1, 2);
		// ssd1312_showstr(str_x, 12, strbuf, 0, font0816, 8, 16, 1, 2);
		ssd1312_showstr(str_x, 12, strbuf, str_len, font_0507, 5, 7, 1, 2);
		if (str_reverse) {
			if (str_x >= 0) {
				str_reverse = false;
			} else {
				str_x ++;
			}
		} else {
			if (str_x+(str_len)*5 <= 64*lines) {
				str_reverse = true;
			} else {
				str_x--;
			}
		}

\
		ssd1312_showchar(0, 12, 0, usbmeter2, 64, 16);
	}

	// 功率条（当前功率与近一段时间平均功率比值）
	power_bar.draw(pb_y);

}

// 电源控制
void power_control(void) {

	static int power_count = 0;
	// 功率控制，当功率超过设置值一定时间，关断输出
	// 如果不需要延迟，需要立马关断
	// 可以考虑修改为ina226_alert通过中断进行控制，这样可以更快速的响应
	if (power > 1) {
		power_count++;
	} else {
		CH217_OUT_FLAG = true;
	}

	if (power_count > 5) {
		power_count = 0;
		CH217_OUT_FLAG = false;
	}
}

void core(void) {

	// HAL_TIM_Base_Start(&htim3);
	// HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_3);

	// 开启定时器中断
	HAL_TIM_Base_Start_IT(&htim16);
	HAL_TIM_Base_Start_IT(&htim17);

	// 开启串口接收空闲中断
	HAL_UARTEx_ReceiveToIdle_DMA(&huart2, (uint8_t*)uart_rx_buf, sizeof(uart_rx_buf));

	// 初始化INA226
	// 16 次采样取平均值，VBUS和VSHUNT ADC转换时间 2.116ms
	ina226_init(INA226_AVG_16 | INA226_VBUS_2116uS | INA226_VSH_2116uS | INA226_MODE_CONT_SHUNT_AND_BUS, 20, 1000);
	clr(CH217_EN);

	// 初始化oled
	ssd1312_init(2);

	if (ssd1312_rotation == 1 || ssd1312_rotation == 3) {
		power_bar.set_w(128);
	} else {
		power_bar.set_w(64);
	}


	while (1) {

		// 电源控制 true 打开输出 false 关闭输出
		if (CH217_OUT_FLAG) {
			clr(CH217_EN);
		} else {
			set(CH217_EN);
		}

		// 清空oled屏幕缓存
		ssd1312_clear();

		// ssd1312_drawLine(0, 0, 127, 32, 0xff);
		// 绘制UI
		meterUI();
		// 刷新屏幕
		ssd1312_sendBuffer();


		HAL_Delay(50);
	}
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {
	if (htim->Instance == TIM16) {
		// 按键扫描
		if (!read(KEY1)) {
			CH217_OUT_FLAG = !CH217_OUT_FLAG;
		}
		if (!read(KEY2)) {
			uint32_t color = colors[0];
			for(uint16_t i = 0; i < sizeof(colors)/sizeof(uint32_t)-1; i++) {
				// colors[i] = tbz::rand();
				colors[i] = colors[i+1];
			}
			colors[sizeof(colors)/sizeof(uint32_t)-1] = color;
			ws2812_convert (colors, sizeof(colors)/sizeof(uint32_t));
			for (int i = 72; i < 128; i++) {
				buffer[i] = WS_RESET;
			}
			ws2812_display();
		}
	} else if (htim->Instance == TIM17) {
		ina226_sample();
		power_sum += power*0.00005;
		power_bar.calc(power_raw);
	}
}

bool my_compare(const char* a, const char* b) {
	for (; *a && *b; a++, b++) {
		if (*a != *b) {
			return false;
		}
	}
	return true;
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size) {
	if (huart->Instance == USART2) {
		if (uart_rx_buf[0] == 'A' && uart_rx_buf[1] == 'T' && uart_rx_buf[2] == '+') {
			if (my_compare("VBUS=O", uart_rx_buf+3)) {
				if (uart_rx_buf[9] == 'N') {
					CH217_OUT_FLAG = true;
				} else if (uart_rx_buf[9] == 'F') {
					CH217_OUT_FLAG = false;
				}
			} else if (my_compare("WS2812=", uart_rx_buf+3)) {
				sscanf(uart_rx_buf+10, "%x,%x,%x,%x",
					(unsigned int*)(&colors[0]), (unsigned int*)(&colors[1]),
					(unsigned int*)(&colors[2]), (unsigned int*)(&colors[3]));
				ws2812_convert (colors, sizeof(colors)/sizeof(uint32_t));
				ws2812_display();
			}
		}
		HAL_UARTEx_ReceiveToIdle_DMA(&huart2, (uint8_t*)uart_rx_buf, sizeof(uart_rx_buf));
	}
}


// oled 控制函数
void ssd1312_clr_rst(void) {
	clr(OLED_RST);
}
void ssd1312_set_rst(void) {
	set(OLED_RST);
}
void ssd1312_delay(void) {
	HAL_Delay(100);
}
void ssd1312_write_byte(uint8_t addr, uint8_t data) {
	// uint8_t buffer[2] = {addr, data};
	// HAL_I2C_Master_Transmit(&hi2c1, SSD1312_IIC_ADDRESS, buffer, 2, 1000);
	HAL_I2C_Mem_Write(&hi2c1, SSD1312_IIC_ADDRESS, addr, I2C_MEMADD_SIZE_8BIT, &data, 1, 1000);
}
void ssd1312_write_bytes(uint8_t addr, uint8_t* data, int len) {
	// HAL_I2C_Mem_Write(&hi2c1, SSD1312_IIC_ADDRESS, addr, I2C_MEMADD_SIZE_8BIT, data, len, 1000);
	HAL_I2C_Mem_Write_DMA(&hi2c1, SSD1312_IIC_ADDRESS, addr, I2C_MEMADD_SIZE_8BIT, data, len);
}


uint8_t ina226_writeReg (uint16_t regAddress, uint16_t regValue) {
	uint8_t retval[2] = {(uint8_t)(regValue >> 8), (uint8_t)(regValue & 0xff)};
	return HAL_I2C_Mem_Write(&hi2c2, INA226_ADDRESS, regAddress, I2C_MEMADD_SIZE_8BIT, retval, 2, 1000);
}
// 无符号读取寄存器
uint16_t ina226_readReg (uint16_t regAddress) {
	uint8_t retval[2] = {0};
	if (HAL_I2C_Mem_Read(&hi2c2, INA226_ADDRESS, regAddress, I2C_MEMADD_SIZE_8BIT, retval, 2, 1000) != HAL_OK) {
		return 0xFFFF;
	} else {
		return retval[0] << 8 | retval[1];
	}
}

// ws2812 控制函数
void ws2812_display(void) {
	HAL_SPI_Transmit_DMA(&hspi1, buffer, sizeof(colors)*8);
}