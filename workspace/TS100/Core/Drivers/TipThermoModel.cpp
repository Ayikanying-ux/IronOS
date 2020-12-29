/*
 * TipThermoModel.cpp
 *
 *  Created on: 7 Oct 2019
 *      Author: ralim
 */

#include "TipThermoModel.h"
#include "Settings.h"
#include "BSP.h"
#include "power.hpp"
#include "../../configuration.h"
#include "main.hpp"
/*
 * The hardware is laid out  as a non-inverting op-amp
 * There is a pullup of 39k(TS100) from the +ve input to 3.9V (1M pulup on TS100)
 *
 * The simplest case to model this, is to ignore the pullup resistors influence, and assume that its influence is mostly constant
 * -> Tip resistance *does* change with temp, but this should be much less than the rest of the system.
 *
 * When a thermocouple is equal temperature at both sides (hot and cold junction), then the output should be 0uV
 * Therefore, by measuring the uV when both are equal, the measured reading is the offset value.
 * This is a mix of the pull-up resistor, combined with tip manufacturing differences.
 *
 * All of the thermocouple readings are based on this expired patent
 * - > https://patents.google.com/patent/US6087631A/en
 *
 * This was bought to my attention by <Kuba Sztandera>
 */

uint32_t TipThermoModel::convertTipRawADCTouV(uint16_t rawADC) {
	// This takes the raw ADC samples, converts these to uV
	// Then divides this down by the gain to convert to the uV on the input to the op-amp (A+B terminals)
	// Then remove the calibration value that is stored as a tip offset
	uint32_t vddRailmVX10 = 33000;	//The vreg is +-2%, but we have no higher accuracy available
	// 4096 * 8 readings for full scale
	// Convert the input ADC reading back into mV times 10 format.
	uint32_t rawInputmVX10 = (rawADC * vddRailmVX10) / (4096 * 8);

	uint32_t valueuV = rawInputmVX10 * 100;	// shift into uV
	//Now to divide this down by the gain
	valueuV /= OP_AMP_GAIN_STAGE;

	if (systemSettings.CalibrationOffset) {
		//Remove uV tipOffset
		if (valueuV >= systemSettings.CalibrationOffset)
			valueuV -= systemSettings.CalibrationOffset;
		else
			valueuV = 0;
	}
	// Bias removal (Compensating for a temperature related non-linearity
	// This uses the target temperature for the tip to calculate a compensation value for temperature related bias
	// This is not entirely ideal as this means we will be wrong on heat up, but will settle to the correct value
	// This will cause us to underread on the heatup until we reach the target temp
	// Compensation (uV)==  ((((80+150*(target_temp_c_x10-1000)/3000)*vddRailmVX10)/4096)*100)/GAIN
	// Reordered with Wolframalpha
	if (currentTempTargetDegC > 0) {
		uint32_t compensation = (20625 * ((currentTempTargetDegC * 10) + 600)) / 512;
		compensation /= OP_AMP_GAIN_STAGE;
		if (valueuV > compensation) {
			valueuV -= compensation;
		}
	}
	return valueuV;
}

uint32_t TipThermoModel::convertTipRawADCToDegC(uint16_t rawADC) {
	return convertuVToDegC(convertTipRawADCTouV(rawADC));
}
#ifdef ENABLED_FAHRENHEIT_SUPPORT
uint32_t TipThermoModel::convertTipRawADCToDegF(uint16_t rawADC) {
	return convertuVToDegF(convertTipRawADCTouV(rawADC));
}
#endif

//Table that is designed to be walked to find the best sample for the lookup

//Extrapolate between two points
// [x1, y1] = point 1
// [x2, y2] = point 2
//  x = input value
// output is x's extrapolated y value
int32_t LinearInterpolate(int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t x) {
	return y1 + (((((x - x1) * 1000) / (x2 - x1)) * (y2 - y1))) / 1000;
}

const uint16_t uVtoDegC[] = { //
		//
				0, 0,	//
				175, 10,	//
				381, 20,	//
				587, 30,	//
				804, 40,	//
				1005, 50,	//
				1007, 60,	//
				1107, 70,	//
				1310, 80,	//
				1522, 90,	//
				1731, 100,	//
				1939, 110,	//
				2079, 120,	//
				2265, 130,	//
				2470, 140,	//
				2676, 150,	//
				2899, 160,	//
				3081, 170,	//
				3186, 180,	//
				3422, 190,	//
				3622, 200,	//
				3830, 210,	//
				4044, 220,	//
				4400, 230,	//
				4691, 240,	//
				4989, 250,	//
				5289, 260,	//
				5583, 270,	//
				5879, 280,	//
				6075, 290,	//
				6332, 300,	//
				6521, 310,	//
				6724, 320,	//
				6929, 330,	//
				7132, 340,	//
				7356, 350,	//
				7561, 360,	//
				7774, 370,	//
				7992, 380,	//
				8200, 390,	//
				8410, 400,	//
				8626, 410,	//
				8849, 420,	//
				9060, 430,	//
				9271, 440,	//
				9531, 450,	//
				9748, 460,	//
				10210, 470,	//
				10219, 480,	//
				10429, 490,	//
				10649, 500,	//

		};
uint32_t TipThermoModel::convertuVToDegC(uint32_t tipuVDelta) {
	if (tipuVDelta) {
		int noItems = sizeof(uVtoDegC) / (2 * sizeof(uint16_t));
		for (int i = 1; i < (noItems - 1); i++) {
			//If current tip temp is less than current lookup, then this current loopup is the higher point to interpolate
			if (tipuVDelta < uVtoDegC[i * 2]) {
				return LinearInterpolate(uVtoDegC[(i - 1) * 2], uVtoDegC[((i - 1) * 2) + 1], uVtoDegC[i * 2], uVtoDegC[(i * 2) + 1], tipuVDelta);
			}
		}
		return LinearInterpolate(uVtoDegC[(noItems - 2) * 2], uVtoDegC[((noItems - 2) * 2) + 1], uVtoDegC[(noItems - 1) * 2], uVtoDegC[((noItems - 1) * 2) + 1], tipuVDelta);
	}
	return 0;
}

#ifdef ENABLED_FAHRENHEIT_SUPPORT
uint32_t TipThermoModel::convertuVToDegF(uint32_t tipuVDelta) {
	return convertCtoF(convertuVToDegC(tipuVDelta));
}

uint32_t TipThermoModel::convertCtoF(uint32_t degC) {
	//(Y °C × 9/5) + 32 =Y°F
	return 32 + ((degC * 9) / 5);
}

uint32_t TipThermoModel::convertFtoC(uint32_t degF) {
	//(Y°F − 32) × 5/9 = Y°C
	if (degF < 32)
	return 0;
	return ((degF - 32) * 5) / 9;
}
#endif

uint32_t TipThermoModel::getTipInC(bool sampleNow) {
	int32_t currentTipTempInC = TipThermoModel::convertTipRawADCToDegC(getTipRawTemp(sampleNow));
	currentTipTempInC += getHandleTemperature() / 10; //Add handle offset
	// Power usage indicates that our tip temp is lower than our thermocouple temp.
	// I found a number that doesn't unbalance the existing PID, causing overshoot.
	// This could be tuned in concert with PID parameters...
	currentTipTempInC -= x10WattHistory.average() / 25;
	if (currentTipTempInC < 0)
		return 0;
	return currentTipTempInC;
}
#ifdef ENABLED_FAHRENHEIT_SUPPORT
uint32_t TipThermoModel::getTipInF(bool sampleNow) {
	uint32_t currentTipTempInF = TipThermoModel::convertTipRawADCToDegF(
			getTipRawTemp(sampleNow));
	currentTipTempInF += convertCtoF(getHandleTemperature() / 10); //Add handle offset
	currentTipTempInF += x10WattHistory.average() / 45; // 25 * 9 / 5, see getTipInC
	return currentTipTempInF;
}
#endif

uint32_t TipThermoModel::getTipMaxInC() {
	uint32_t maximumTipTemp = TipThermoModel::convertTipRawADCToDegC(0x7FFF - (80 * 5)); //back off approx 5 deg c from ADC max
	maximumTipTemp += getHandleTemperature() / 10; //Add handle offset
	return maximumTipTemp - 1;
}
