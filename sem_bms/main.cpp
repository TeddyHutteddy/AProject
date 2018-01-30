/*
 * sem_bms.cpp
 *
 * Created: 22/01/2018 3:37:31 PM
 * Author : teddy
 */ 

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <stdlib.h>
#include <math.h>

constexpr float CutoffCurrent = 10;
constexpr float CutoffTemperature = 100;
constexpr float CutoffVoltage = 3;

constexpr float vdiv_factor(float const r1, float const r2) {
	return r2 / (r1 + r2);
}

constexpr float vdiv_input(float const output, float const factor) {
	return output / factor;
}

template <typename adc_t, adc_t max>
constexpr float adc_voltage(adc_t const value, float const vref) {
	return vref * (static_cast<float>(value) / static_cast<float>(max));
}

template <typename adc_t, adc_t max>
constexpr adc_t adc_value(float const voltage, float const vref) {
	return (voltage / vref) * static_cast<float>(max);
}

float milliVoltsToDegreesC(float const m) {
	float result = 2230.8 - m;
	result *= (4 * 0.00433);
	result += square(-13.528);
	result = 13.582 - sqrt(result);
	result /= (2 * -0.00433);
	return result + 30;
}

float calculateTemperature(uint16_t const value) {
	return milliVoltsToDegreesC(vdiv_input(adc_voltage<uint16_t, 1024>(value, 1.1f), vdiv_factor(5.1f, 7.5f)) * 1000.0f);
}

float current_outV(float const current, float const vref) {
	return (current * ((vref - 0.6f) / (12.5f * 2.0f)));
}

enum class Instruction : uint8_t {
	Normal = 0,
	ReceiveData,
	FireRelay,
	LEDOn,
	LEDOff,
};

enum class DataIndexes {
	TemperatureADC = 0,
	CurrentADC,
	Cell0ADC,
	Cell1ADC,
	Cell2ADC,
	Cell3ADC,
	Cell4ADC,
	Cell5ADC,
	_size,
};

constexpr ADC_MUXPOS_enum ADCMap[static_cast<unsigned int>(DataIndexes::_size)] = {
	ADC_MUXPOS_AIN9_gc,	//Temperature
	ADC_MUXPOS_AIN7_gc,	//Current
	ADC_MUXPOS_AIN1_gc,	//Cell0
	ADC_MUXPOS_AIN2_gc,	//Cell1
	ADC_MUXPOS_AIN3_gc,	//Cell2
	ADC_MUXPOS_AIN4_gc, //Cell3
	ADC_MUXPOS_AIN6_gc, //Cell4
	ADC_MUXPOS_AIN8_gc	//Cell5
};

//Using a double buffer (*2 since values are 16 bit)
volatile uint8_t dataBuffer[2][static_cast<unsigned int>(DataIndexes::_size) * 2];
volatile uint8_t dataBufferPos = 0;
volatile uint8_t dataBufferChoice = 0;

uint16_t retreiveFromBuffer(volatile uint8_t const buffer[], DataIndexes const pos) {
	return (buffer[static_cast<unsigned int>(pos) * 2] |
	(buffer[static_cast<unsigned int>(pos) * 2 + 1] << 8));
}

void fireRelay() {
	DAC0.DATA = 0;
}

void setLEDState(bool const state) {
	if(state) PORTB.OUTSET = 1 << 0;
	else PORTB.OUTCLR = 1 << 0;
}

//Interrupt where things are sampled (regular, every 10ms)	
ISR(TCB0_INT_vect) {
	//Clear the interrupt flag
	TCB0.INTFLAGS = TCB_CAPT_bm;
	//Sample each ADC
	uint8_t const bufferChoice = (dataBufferChoice == 0 ? 1 : 0);
	for(uint8_t i = 0; i < static_cast<unsigned int>(DataIndexes::_size); i++) {
		//Clear and set reference voltages
		VREF.CTRLA &= ~VREF_ADC0REFSEL_gm;
		if(static_cast<DataIndexes>(i) == DataIndexes::TemperatureADC)
			VREF.CTRLA |= VREF_ADC0REFSEL_1V1_gc;
		else if(static_cast<DataIndexes>(i) == DataIndexes::CurrentADC)
			VREF.CTRLA |= VREF_ADC0REFSEL_1V5_gc;
		else
			VREF.CTRLA |= VREF_ADC0REFSEL_2V5_gc;
		ADC0.MUXPOS = ADCMap[i];
		ADC0.COMMAND = ADC_STCONV_bm;
		while(ADC0.COMMAND & ADC_STCONV_bm);
		//Store the result as lower byte -> higher byte
		uint16_t const result = ADC0.RES / 4;	//Div by 4 because of the 4 accumulator
		dataBuffer[bufferChoice][i * 2] = result & 0x00ff;
		dataBuffer[bufferChoice][i * 2 + 1] = (result & 0xff00) >> 8;
	}
	//Calculate VCC to that the DAC threshold can be calculated (since output of ACS711 is dependent on VCC)
	float vcc = vdiv_input(adc_voltage<uint16_t, 1024>(retreiveFromBuffer(dataBuffer[bufferChoice], DataIndexes::Cell0ADC), 2.5f), vdiv_factor(10, 5.6));
	DAC0.DATA = adc_value<uint8_t, 0xff>(current_outV(CutoffCurrent, vcc), 1.5f);
	volatile uint16_t currentval = retreiveFromBuffer(dataBuffer[bufferChoice], DataIndexes::CurrentADC);
	//Make sure that VCC is still high enough
	if(vcc <= CutoffVoltage)
		fireRelay();
	//Check the temperature (comparator should take care of current)
	float temperature = calculateTemperature(retreiveFromBuffer(dataBuffer[bufferChoice], DataIndexes::TemperatureADC));
	if(temperature >= CutoffTemperature)
		fireRelay();
	dataBufferChoice = bufferChoice;
}

//SPI interrupt
ISR(SPI0_INT_vect) {
	PORTB.OUTSET = (1 << 1);
	static bool ledOn = true;
	if(ledOn) {
		//Disable TCA0 and turn off LED
		TCA0.SINGLE.CTRLA &= ~TCA_SINGLE_ENABLE_bm;
		TCA0.SINGLE.CTRLB &= ~TCA_SINGLE_CMP0_bm;
		setLEDState(false);
		ledOn = false;
	}
	if(SPI0.INTFLAGS & SPI_RXCIF_bm) {
		uint8_t const data = SPI0.DATA;
		switch(static_cast<Instruction>(data)) {
		case Instruction::ReceiveData:
			dataBufferPos = 0;
			//Fallthrough intentional
		case Instruction::Normal:
			if(dataBufferPos >= static_cast<unsigned int>(DataIndexes::_size) * 2)
				dataBufferPos = 0;
			SPI0.DATA = dataBuffer[dataBufferChoice][dataBufferPos++];
			PORTB.OUTCLR = (1 << 1);
			break;
		case Instruction::FireRelay:
			fireRelay();
			break;
		case Instruction::LEDOn:
			setLEDState(true);
			break;
		case Instruction::LEDOff:
			setLEDState(false);
			break;
		default:
			break;
		}
	}
}

void setup() {
	//Diable CCP protection of clock registers (works for 4 instructions)
	CCP = CCP_IOREG_gc;
	//Enable main clock as 20Mhz osc
	CLKCTRL.MCLKCTRLA |= CLKCTRL_CLKSEL_OSC20M_gc;
	CCP = CCP_IOREG_gc;
	//Disable prescaler
	CLKCTRL.MCLKCTRLB = 0;

	//Set sleep mode to idle and enable sleep instruction
	SLPCTRL.CTRLA = SLPCTRL_SMODE_IDLE_gc | SLPCTRL_SEN_bm;

	//Enable alternative SPI0 pins
	PORTMUX.CTRLB |= PORTMUX_SPI0_ALTERNATE_gc;

	//Set ports pin config
	PORTA.DIR = 0b00100000;
	PORTB.DIR = 0b00000111;
	PORTC.DIR = 0b00000010;
	//Isolator enable
	PORTB.OUTSET = 1 << 2;
	//Button
	PORTB.PIN3CTRL = PORT_PULLUPEN_bm;

	//Set DAC reference voltage
	VREF.CTRLA = VREF_DAC0REFSEL_1V5_gc;

	//Set DAC output voltage (will be calculated based on VCC later)
	DAC0.DATA = 0xff;
	//Setup DAC
	DAC0.CTRLA = DAC_RUNSTDBY_bm | DAC_ENABLE_bm;

	//Setup AC muxes (P0 for positive, DAC for negative)
	AC0.MUXCTRLA = AC_MUXPOS_PIN0_gc | AC_MUXNEG_DAC_gc;
	//Setup AC (run in standby, enable output, low power mode, enable)
	AC0.CTRLA = AC_RUNSTDBY_bm | AC_OUTEN_bm | AC_LPMODE_EN_gc | AC_ENABLE_bm;

	//Take 4 accumulated samples
	ADC0.CTRLB = ADC_SAMPNUM_ACC4_gc;
	//Increase capacitance and prescale 32
	ADC0.CTRLC = ADC_SAMPCAP_bm | ADC_PRESC_DIV32_gc;
	//Setup ADC
	ADC0.CTRLA = ADC_RESSEL_10BIT_gc | ADC_ENABLE_bm;

	//Setup SPI
	SPI0.CTRLB = SPI_BUFEN_bm | SPI_BUFWR_bm | SPI_MODE_0_gc;
	SPI0.INTCTRL = SPI_RXCIE_bm; //| SPI_TXCIE_bm;
	SPI0.CTRLA = SPI_ENABLE_bm;
	//Set the SPI interrupt to be the highest priority so that it always responds to the master
	CPUINT.LVL1VEC = SPI0_INT_vect_num;

	//Set the CCMP (top) value for TCB0
	constexpr uint32_t TCB0Top = static_cast<uint32_t>((20000000 / 2) / 200);
	static_assert(TCB0Top <= 0xffff, "TCB0 top too large");
	TCB0.CCMP = TCB0Top;
	//Run a check/data update every 10ms.
	TCB0.CTRLA = TCB_RUNSTDBY_bm | TCB_CLKSEL_CLKDIV2_gc | TCB_ENABLE_bm;
	TCB0.INTCTRL = TCB_CAPT_bm;

	//Setup TCA to blink LED for 100ms every two seconds
	constexpr uint32_t TCA0Top = static_cast<uint32_t>((20000000 / 1024) * 2);
	static_assert(TCA0Top <= 0xffff, "TCA0 top too large");
	constexpr uint32_t TCA0Compare = static_cast<uint32_t>((20000000 / 1024) / 10);
	static_assert(TCA0Compare <= 0xffff, "TCA0Compare too large");
	TCA0.SINGLE.CTRLB = TCA_SINGLE_CMP0_bm | TCA_SINGLE_WGMODE_SINGLESLOPE_gc;
	TCA0.SINGLE.PER = TCA0Top;
	TCA0.SINGLE.CMP0 = TCA0Compare;
	TCA0.SINGLE.CTRLA = TCA_SINGLE_CLKSEL_DIV1024_gc | TCA_SINGLE_ENABLE_bm;

	//Enable global interrupts
	sei();
}

int main(void)
{
	setup();
	while(true) {
		sleep_cpu();
	}
}
