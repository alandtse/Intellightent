#include "formula.h"
#include "exprtk.hpp"

struct wrapper
{
	exprtk::expression<double> expression;
	exprtk::parser<double>     parser;
};

double                       g_params[kFormulaParam_Max];
exprtk::symbol_table<double> g_symbols;
bool                         g_inited = false;

void initFormulaStatic()
{
	if (g_inited)
		return;

	g_inited = true;

	memset(g_params, 0, sizeof(double) * kFormulaParam_Max);

	double* _params = g_params;
	g_symbols.add_variable("lightindex", _params[kFormulaParam_LightIndex]);
	g_symbols.add_variable("lightdistance", _params[kFormulaParam_LightDistance]);
	g_symbols.add_variable("lightintensity", _params[kFormulaParam_LightIntensity]);
	g_symbols.add_variable("lightradius", _params[kFormulaParam_LightRadius]);
	g_symbols.add_variable("lightx", _params[kFormulaParam_LightX]);
	g_symbols.add_variable("lighty", _params[kFormulaParam_LightY]);
	g_symbols.add_variable("lightz", _params[kFormulaParam_LightZ]);
	g_symbols.add_variable("lightr", _params[kFormulaParam_LightR]);
	g_symbols.add_variable("lightg", _params[kFormulaParam_LightG]);
	g_symbols.add_variable("lightb", _params[kFormulaParam_LightB]);
	g_symbols.add_variable("lightambientr", _params[kFormulaParam_LightAmbientR]);
	g_symbols.add_variable("lightambientg", _params[kFormulaParam_LightAmbientG]);
	g_symbols.add_variable("lightambientb", _params[kFormulaParam_LightAmbientB]);
	g_symbols.add_variable("lightchosenlastframe", _params[kFormulaParam_LightChosenLastFrame]);
	g_symbols.add_variable("lightneverfades", _params[kFormulaParam_LightNeverFades]);
	g_symbols.add_variable("lightportalstrict", _params[kFormulaParam_LightPortalStrict]);
	g_symbols.add_variable("camerax", _params[kFormulaParam_CameraX]);
	g_symbols.add_variable("cameray", _params[kFormulaParam_CameraY]);
	g_symbols.add_variable("isinterior", _params[kFormulaParam_IsInterior]);
	g_symbols.add_variable("timeofday", _params[kFormulaParam_TimeOfDay]);
}

FormulaHelper::FormulaHelper()
{
	_ptr = nullptr;

	initFormulaStatic();
}

FormulaHelper::~FormulaHelper()
{
	wrapper* w = (wrapper*)_ptr;

	if (w)
		delete w;
}

bool FormulaHelper::Parse(const std::string& input)
{
	if (_ptr)
		return false;

	auto w = new wrapper();
	_ptr = w;

	w->expression.register_symbol_table(g_symbols);
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
	g_params[index] = value;
}
