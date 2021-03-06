/*
 * encoder.h
 *
 * Created: 19/01/2018 11:28:14 PM
 *  Author: teddy
 */ 

#pragma once

#include <array>
#include <numeric>
#include <iterator>
#include <functional>
#include <tcc.h>
#include <tcc_callback.h>

#include "main_config.h"

namespace tccEncoder {
	extern tcc_module instance;
	//Returns the speed of said thing in gears per second
	float motor_speedConvert(float const interval);
	float motor_driveTrainConvert(float const interval);
	void init();
}

class Encoder {
public:
	virtual float getSpeed() const = 0;
	virtual float getAverageInterval() const = 0;
};

template <typename T, size_t s = config::encoder::bufferSize>
class Ticker {
public:
	void tick(T const t);
	T getAverageInterval() const;
	Ticker();
private:
	using buf_t = std::array<T, s>;
	buf_t buffer;
	//For seme reason iterator wouldn't work in the std:: functions. TODO: Try again with std functions, might have just messed up some syntax or something
	size_t itr = 0;
	bool filled = false;
};

template <typename T, size_t s /*= config::encoder::bufferSize*/>
Ticker<T, s>::Ticker()
{
	std::fill(buffer.begin(), buffer.end(), 0);
}

template <typename T, size_t s /*= config::encoder::bufferSize*/>
T Ticker<T, s>::getAverageInterval() const
{
	T result = 0;
	if(!filled) {
		//Prevent divide by zero error
		if(itr == 0) return 0;
		result = std::accumulate(buffer.begin(), buffer.begin() + itr, 0) / std::distance(buffer.begin(), buffer.begin() + itr);
	}
	else if(itr != 0) {
		result = std::accumulate(buffer.begin(), buffer.begin() + itr,
			std::accumulate(buffer.begin() + itr, buffer.end(), 0)) / buffer.size();
	}
	else
		result = std::accumulate(buffer.begin(), buffer.end(), 0) / buffer.size();
	return result;
}

template <typename T, size_t s /*= config::encoder::*/>
void Ticker<T, s>::tick(T const t)
{
	buffer[itr++] = t;
	if(itr == buffer.size()) {
		filled = true;
		itr = 0;
	}
}

template <size_t n, typename counter_t = unsigned int>
class TimerEncoder : public Encoder {
public:
	using convert_t = std::function<float (float const)>;
	static void overflow(counter_t const amount = 0xffff);
	static void compare(counter_t const value);
	float getSpeed() const override;
	float getAverageInterval() const override;
	static counter_t getCounterValue();
	TimerEncoder(convert_t const convertfn);
protected:
	static TimerEncoder *current;
	convert_t convertfn;
	Ticker<counter_t> ticker;
	counter_t total = 0;
	counter_t start = 0;
};

//Should probably put this in a specialised subclass, but they all share the same timer so whatever
template <size_t n, typename counter_t /*= unsigned int*/>
counter_t TimerEncoder<n, counter_t>::getCounterValue()
{
	return tcc_get_count_value(&tccEncoder::instance);
}

template <size_t n, typename counter_t /*= unsigned int*/>
float TimerEncoder<n, counter_t>::getAverageInterval() const 
{
	//Get whichever is higher: The average time or the time since the last tick
	auto rtrnTime = std::max(ticker.getAverageInterval(), getCounterValue() + total - start);
	if(rtrnTime >= config::encoder::zerotimeout)
		return 0;
	return rtrnTime;
}

template <size_t n, typename counter_t /*= size_t*/>
TimerEncoder<n, counter_t> * TimerEncoder<n, counter_t>::current = nullptr;

template <size_t n, typename counter_t /*= size_t*/>
void TimerEncoder<n, counter_t>::compare(counter_t const value)
{
	if(current != nullptr) {
		current->ticker.tick(current->total - current->start + value);
		current->start = value;
		current->total = 0;
	}
}

template <size_t n, typename counter_t /*= size_t*/>
void TimerEncoder<n, counter_t>::overflow(counter_t const amount /*= 0x00ffffff*/)
{
	if(current != nullptr)
		current->total += amount;
}

template <size_t n, typename counter_t>
float TimerEncoder<n, counter_t>::getSpeed() const
{
	return(convertfn(getAverageInterval()));
}

template <size_t n, typename counter_t /*= size_t*/>
TimerEncoder<n, counter_t>::TimerEncoder(convert_t const convertfn) : convertfn(convertfn)
{
	current = this;
}

typedef TimerEncoder<0> Encoder0;
typedef TimerEncoder<1> Encoder1;
typedef TimerEncoder<2> Encoder2;
