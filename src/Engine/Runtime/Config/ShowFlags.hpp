#pragma once

namespace Runtime::Config
{

struct ShowFlags final
{
    bool DebugPhysicsOverlay = false;
    bool DebugGraphicsPanel = false;
    bool DebugCVarPanel = false;
    bool DebugProfileOverlay = false;
    bool DebugDraw_Lighting = false;
    bool DebugDraw_BoundingBox = false;
    bool DebugDraw_PhysicsBodies = false;
    bool ShowVisualDebug = false;
    bool ShowEdge = false;
    bool ShowDebugSkeleton = false;
    bool ShowGrid = true;
    bool ShowWireframe = false;
};

}
