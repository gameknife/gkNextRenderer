
#ifdef __cplusplus
bool IsInside( vec3 origin, vec3& offset, uint& materialId)
#else
bool IsInside( vec3 origin, inout vec3 offset, inout uint materialId)
#endif
{
    for( uint i = 0; i < 6; i++ )
    {
        vec3 rayDir = cubeVectors[i];
        
        // Align with the surface normal
        vec3 OutNormal;
        uint OutMaterialId;
        uint OutInstanceId;
        float OutRayDist;

        if( TracingFunction(origin, rayDir, OutNormal, OutMaterialId, OutRayDist, OutInstanceId) )
        {
             if( dot(OutNormal, rayDir) > 0.0 )
            {
                materialId = FetchMaterialId( OutMaterialId, OutInstanceId );
                offset = rayDir * (OutRayDist + 0.05f);
                if(OutRayDist < CUBE_UNIT)
                {
                    return true;
                }
            }
        }
    }
    return false;
}

#ifdef __cplusplus
vec4 TraceOcclusion(uint iterate, vec3 origin, vec3 basis, uint& activeProbe, uint& materialId, vec4& bounceColor, Assets::UniformBufferObject& Camera)
{
#else
vec4 TraceOcclusion(uint iterate, vec3 origin, vec3 basis, inout uint activeProbe, inout uint materialId, inout vec4 bounceColor, in UniformBufferObject Camera)
{
#endif
    vec4 rayColor = vec4(0.0);
    bounceColor = vec4(0.0);

    const vec2 offset = grid5x5[iterate % 25] * 0.25f;
    float skyMultiplier = Camera.HasSky ? Camera.SkyIntensity : 0.0f;
    
    bool offsetProbe = false;
    vec3 probeOffset = vec3(0.0);

    for( uint i = 0; i < FACE_TRACING; i++ )
    {
        vec3 hemiVec = normalize(vec3(grid4x4[i] + offset, 1.0));

        // Align with the surface normal
        vec3 rayDir = AlignWithNormal(hemiVec, basis);
        
        vec3 OutNormal;
        uint OutMaterialId;
        uint OutInstanceId;
        float OutRayDist;

        if( TracingFunction(origin, rayDir, OutNormal, OutMaterialId, OutRayDist, OutInstanceId) )
        {
            vec3 hitPos = origin + rayDir * OutRayDist;
            
            //if( dot(OutNormal, rayDir) < 0.0 )
            {
                bounceColor += FetchDirectLight(hitPos, OutNormal, OutMaterialId, OutInstanceId);
            }
//            else
//            {
//                // 这里，命中了背面，说明这个probe位于几何体内部，如果hitDist小于probe的间隔，可以考虑将probe推到表面，再次发出
//                materialId = FetchMaterialId( OutMaterialId, OutInstanceId );
//                //activeProbe = 0;
//                break;
//            }
        }
        else
        {
            rayColor += SampleIBL(Camera.SkyIdx, rayDir, Camera.SkyRotation, 1.0) * skyMultiplier;
        }
    }
    rayColor = rayColor / float(FACE_TRACING);
    bounceColor = bounceColor / float(FACE_TRACING);

    if(Camera.LightCount > 0)
    {
        vec4 lightPower = vec4(0.0);
        LightObject light = FetchLight(0, lightPower);
        vec3 lightPos = mix(vec3(light.p1), vec3(light.p3), 0.5f);
        float lightAtten = float(TracingOccludeFunction(origin, lightPos));
        vec3 lightDir = normalize(lightPos - origin);
        float ndotl = clamp(dot(basis, lightDir), 0.0f, 1.0f);
        float distance = length(lightPos - origin);
        float attenuation = ndotl * light.normal_area.w / (distance * distance * 3.14159f);
        rayColor += lightPower * attenuation * lightAtten;
    }
    
    vec3 sunDir = vec3(Camera.SunDirection);
    float sunAtten = float(TracingOccludeFunction(origin, origin + sunDir * 1000.0f));
    float ndotl = clamp(dot(basis, sunDir), 0.0f, 1.0f);
    rayColor += Camera.SunColor * sunAtten * ndotl * (Camera.HasSun ? 1.0f : 0.0f) * 0.25f;
    return rayColor;
}
