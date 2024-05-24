
const uint MaterialLambertian = 0;
const uint MaterialMetallic = 1;
const uint MaterialDielectric = 2;
const uint MaterialIsotropic = 3;
const uint MaterialDiffuseLight = 4;
const uint MaterialMixture = 5;

struct Material
{
	vec4 Diffuse;
	int DiffuseTextureId;
	float Fuzziness;
	float RefractionIndex;
	uint MaterialModel;
	float Metalness;

	float Reserverd0;
	float Reserverd1;
	float Reserverd2;
};


struct LightObject
{
	vec4 WorldPosMin;
	vec4 WorldPosMax;
	vec4 WorldDirection;
	float area;
	float Reserverd0;
	float Reserverd1;
	float Reserverd2;
};