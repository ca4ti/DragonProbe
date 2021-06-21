
#include <hardware/adc.h>

#include "tempsensor.h"

#define T_SLOPE (-0.001721f)
#define T_BIAS  (0.706f)
#define V_MAX   (3.3f)
#define D_RANGE (4096)
#define T_OFF   (27)

// convert float to x.4 fixed format
#define float2fix(x) (int)((x)*(1<<4))

// convert x.4 fixed to 8.4 fixed
__attribute__((__const__))
inline static int16_t trunc_8fix4(int fix) {
	if (fix >  4095) fix =  4095;
	if (fix < -4096) fix = -4096;
	return fix;
}

void tempsense_dev_init(void) {
	adc_init();
	adc_set_temp_sensor_enabled(true);
}
// 8.4
int16_t tempsense_dev_get_temp(void) {
	adc_select_input(4); // select temp sensor
	uint16_t result = adc_read();

	float voltage = result * (V_MAX / D_RANGE);

	float tempf = T_OFF + (voltage - T_BIAS) / T_SLOPE;

	// FIXME: use fixed point instead! but something's wrong with the formula below
	/*int temperature = float2fix(T_OFF - T_BIAS / T_SLOPE)
		+ (int)result * float2fix(V_MAX / (D_RANGE * T_SLOPE));*/

	return trunc_8fix4(/*temperature*/float2fix(tempf));
}

// RP2040 absolute min/max are -20/85
int16_t tempsense_dev_get_lower(void) { return trunc_8fix4(float2fix(-15)); }
int16_t tempsense_dev_get_upper(void) { return trunc_8fix4(float2fix( 75)); }
int16_t tempsense_dev_get_crit (void) { return trunc_8fix4(float2fix( 80)); }

