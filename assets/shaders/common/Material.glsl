
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
	int MRATextureId;
	int NormalTextureId;
	float Fuzziness;
	float RefractionIndex;
	uint MaterialModel;
	float Metalness;

	float RefractionIndex2;
	float Reserverd1;
	float Reserverd2;
};


struct LightObject
{
	vec4 p0;
	vec4 p1;
	vec4 p3;
	vec4 normal_area;
};

struct NodeProxy
{
	mat4 World;
};

struct RayCastContext
{
	vec4 Origin;
	vec4 Direction;
	float TMin;
	float TMax;
};

struct RayCastResult
{
	vec4 HitPoint;
	vec4 Normal;
	float T;
	uint InstanceId;
	uint MaterialId;
	uint Hitted;
};
