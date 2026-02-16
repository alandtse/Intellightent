#pragma once

enum FormulaParams
{
	kFormulaParam_LightIndex,
	kFormulaParam_LightIntensity,
	kFormulaParam_LightDistance,
	kFormulaParam_LightRadius,
	kFormulaParam_LightX,
	kFormulaParam_LightY,
	kFormulaParam_LightZ,
	kFormulaParam_LightR,
	kFormulaParam_LightG,
	kFormulaParam_LightB,
	kFormulaParam_LightAmbientR,
	kFormulaParam_LightAmbientG,
	kFormulaParam_LightAmbientB,
	kFormulaParam_LightChosenLastFrame,

	kFormulaParam_CameraX,
	kFormulaParam_CameraY,
	kFormulaParam_CameraZ,
	kFormulaParam_IsInterior,
	kFormulaParam_TimeOfDay,

	kFormulaParam_Max
};

struct FormulaHelper
{
	FormulaHelper();
	~FormulaHelper();

	bool   Parse(const std::string& input);
	double Calculate();
	void   SetParam(int32_t index, double value);

private:
	double* _params;
	void*   _ptr;
};
