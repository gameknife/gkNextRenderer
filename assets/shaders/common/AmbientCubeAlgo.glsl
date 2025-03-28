
#ifdef __cplusplus
vec4 TraceOcclusion(uvec4& RandomSeed, vec3 origin, vec3 basis, uint& activeProbe, vec4& bounceColor, vec4& skyColor, Assets::UniformBufferObject& Camera)
{
#else
vec4 TraceOcclusion(inout uvec4 RandomSeed, vec3 origin, vec3 basis, inout uint activeProbe, inout vec4 bounceColor, inout vec4 skyColor, in UniformBufferObject Camera)
{
#endif
    vec4 rayColor = vec4(0.0);
    bounceColor = vec4(0.0);
    skyColor = vec4(0.0);

    // Generate a random angle for z-axis rotation
    float randAngle = RandomFloat(RandomSeed) * 6.283185f;
    float cosTheta = cos(randAngle);
    float sinTheta = sin(randAngle);
    float skyMultiplier = Camera.HasSky ? Camera.SkyIntensity : 0.0f;

    for( uint i = 0; i < FACE_TRACING; i++ )
    {
        vec3 hemiVec = hemisphereVectors[i];

        // Apply rotation around z-axis
        vec3 rotatedVec = vec3(
        hemiVec.x * cosTheta - hemiVec.y * sinTheta,
        hemiVec.x * sinTheta + hemiVec.y * cosTheta,
        hemiVec.z
        );

        // Align with the surface normal
        vec3 rayDir = AlignWithNormal(rotatedVec, basis);
        vec3 OutNormal;
        uint OutMaterialId;
        uint OutInstanceId;
        float OutRayDist;

        if( TracingFunction(origin, rayDir, OutNormal, OutMaterialId, OutRayDist, OutInstanceId) )
        {
            if( dot(OutNormal, rayDir) < 0.0 )
            {
                vec3 hitPos = origin + rayDir * OutRayDist;
                bounceColor += FetchDirectLight(hitPos, OutNormal, OutMaterialId, OutInstanceId);
            }
            else
            {
                // 这里，命中了背面，说明这个probe位于几何体内部，如果hitDist小于probe的间隔，可以考虑将probe推到表面，再次发出
                activeProbe = 0;
                break;
            }
        }
        else
        {
            rayColor += SampleIBL(Camera.SkyIdx, rayDir, Camera.SkyRotation, 1.0) * skyMultiplier;
        }
    }
    rayColor = rayColor / float(FACE_TRACING);
    skyColor = rayColor;
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
