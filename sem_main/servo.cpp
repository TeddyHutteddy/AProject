/*
 * servo.cpp
 *
 * Created: 18/01/2018 4:06:13 PM
 *  Author: teddy
 */

#include "servo.h"
#include "main_config.h"

namespace {
	constexpr TCCServoStaticConfig<float>::Result res0 = TCCServoStaticConfig<float>::calculateResult(
		config::servo::clockFrequency, config::servo::period, config::servo::midpoint0, config::servo::deviation0, config::servo::minAngle, config::servo::maxAngle);
}

namespace {
	constexpr TCCServoStaticConfig<float>::Result res1 = TCCServoStaticConfig<float>::calculateResult(
		config::servo::clockFrequency, config::servo::period, config::servo::midpoint1, config::servo::deviation1, config::servo::minAngle, config::servo::maxAngle);
}

TCCServo0::TCCServo0() : Parent(res0, TCC_MATCH_CAPTURE_CHANNEL_0)
{
	tcc_config config;
	tcc_get_config_defaults(&config, TCC0);

	config.compare.channel_function[0] = TCC_CHANNEL_FUNCTION_COMPARE;
	config.compare.match[0] = res0.counter_midpoint;
	config.compare.wave_generation = TCC_WAVE_GENERATION_SINGLE_SLOPE_PWM;
	config.compare.wave_polarity[0] = TCC_WAVE_POLARITY_1;
	config.compare.wave_ramp = TCC_RAMP_RAMP1;

	config.pins.enable_wave_out_pin[0] = true;
	config.pins.wave_out_pin[0] = PIN_PA08E_TCC0_WO0;
	config.pins.wave_out_pin_mux[0] = MUX_PA08E_TCC0_WO0;

	config.counter.clock_prescaler = res0.prescaleSetting;
	config.counter.clock_source = GCLK_GENERATOR_0;
	config.counter.direction = TCC_COUNT_DIRECTION_UP;
	config.counter.period = res0.prescaledTicksPerPeriod;

	config.double_buffering_enabled = false;
	config.run_in_standby = true;

	tcc_init(&instance, TCC0, &config);
	tcc_enable(&instance);
}

 TCCServo1::TCCServo1() : Parent (res1, TCC_MATCH_CAPTURE_CHANNEL_0)
{
	tcc_config config;
	tcc_get_config_defaults(&config, TCC1);

	config.compare.channel_function[0] = TCC_CHANNEL_FUNCTION_COMPARE;
	config.compare.match[0] = res1.counter_midpoint;
	config.compare.wave_generation = TCC_WAVE_GENERATION_SINGLE_SLOPE_PWM;
	config.compare.wave_polarity[0] = TCC_WAVE_POLARITY_1;
	config.compare.wave_ramp = TCC_RAMP_RAMP1;

	config.pins.enable_wave_out_pin[0] = true;
	config.pins.wave_out_pin[0] = PIN_PA10E_TCC1_WO0;
	config.pins.wave_out_pin_mux[0] = MUX_PA10E_TCC1_WO0;

	config.counter.clock_prescaler = res1.prescaleSetting;
	config.counter.clock_source = GCLK_GENERATOR_0;
	config.counter.direction = TCC_COUNT_DIRECTION_UP;
	config.counter.period = res1.prescaledTicksPerPeriod;

	config.double_buffering_enabled = false;
	config.run_in_standby = true;

	tcc_init(&instance, TCC1, &config);
	tcc_enable(&instance);
}
