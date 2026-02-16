#include "formula.h"
#include "exprtk.hpp"

struct wrapper
{
	exprtk::symbol_table<double> symbols;
	exprtk::expression<double>   expression;
	exprtk::parser<double>       parser;
};

FormulaHelper::FormulaHelper()
{
	_ptr = nullptr;
	_params = (double*)malloc(sizeof(double) * kFormulaParam_Max);
	memset(_params, 0, sizeof(double) * kFormulaParam_Max);
}

FormulaHelper::~FormulaHelper()
{
	wrapper* w = (wrapper*)_ptr;

	if (w)
		delete w;

	if (_params)
		free(_params);
}

bool FormulaHelper::Parse(const std::string& input)
{
	if (_ptr)
		return false;

	auto w = new wrapper();
	_ptr = w;

	w->symbols.add_variable("lightindex", _params[kFormulaParam_LightIndex]);
	w->symbols.add_variable("lightdistance", _params[kFormulaParam_LightDistance]);
	w->symbols.add_variable("lightintensity", _params[kFormulaParam_LightIntensity]);
	w->symbols.add_variable("lightradius", _params[kFormulaParam_LightRadius]);
	w->symbols.add_variable("lightx", _params[kFormulaParam_LightX]);
	w->symbols.add_variable("lighty", _params[kFormulaParam_LightY]);
	w->symbols.add_variable("lightz", _params[kFormulaParam_LightZ]);
	w->symbols.add_variable("lightr", _params[kFormulaParam_LightR]);
	w->symbols.add_variable("lightg", _params[kFormulaParam_LightG]);
	w->symbols.add_variable("lightb", _params[kFormulaParam_LightB]);
	w->symbols.add_variable("lightambientr", _params[kFormulaParam_LightAmbientR]);
	w->symbols.add_variable("lightambientg", _params[kFormulaParam_LightAmbientG]);
	w->symbols.add_variable("lightambientb", _params[kFormulaParam_LightAmbientB]);
	w->symbols.add_variable("lightchosenlastframe", _params[kFormulaParam_LightChosenLastFrame]);
	w->symbols.add_variable("camerax", _params[kFormulaParam_CameraX]);
	w->symbols.add_variable("cameray", _params[kFormulaParam_CameraY]);
	w->symbols.add_variable("isinterior", _params[kFormulaParam_IsInterior]);
	w->symbols.add_variable("timeofday", _params[kFormulaParam_TimeOfDay]);

	w->expression.register_symbol_table(w->symbols);
	return w->parser.compile(input, w->expression);
}

double FormulaHelper::Calculate()
{
	auto w = (wrapper*)_ptr;
	if (!w)
		return 0.0;

	return w->expression.value();
}

void FormulaHelper::SetParam(int32_t index, double value)
{
	_params[index] = value;
}
