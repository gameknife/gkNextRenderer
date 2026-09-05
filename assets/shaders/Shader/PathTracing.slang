import Shader.Bindless;
// Visibility ids carry a LOD level; the reconstruction in GeneralFunc.slang decodes through it.
import Shader.VisibilityBuffer;

module PathTracing;

#include "common/BasicTypes.slang"
__include "common/BSDF.slang";
__include "common/Interfaces.slang";
__include "common/SceneSampling.slang";
__include "common/Sky.slang";
__include "common/RayTracers.slang";
__include "common/PrimaryRayCasters.slang";
__include "common/RadianceCache.slang";
__include "common/Sharc.slang";
__include "common/Restir.slang";
__include "common/RestirSpatialShade.slang";
// LightGrid before Lighting: Slang resolves unqualified names within a module in include order, and
// the NEE evaluators in Lighting.slang call the grid's pdf authority.
__include "common/LightGrid.slang";
__include "common/Lighting.slang";
__include "common/PathTracingRenderer.slang";
#include "common/ConstFunc.slang"
#include "common/Tonemap.slang"
__include "common/GeneralFunc.slang";
__include "common/AmbientCube.slang";
__include "common/AmbientCubeBaker.slang";
#include "common/GPUScene.slang"
