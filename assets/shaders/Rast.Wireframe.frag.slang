struct FragmentOutput
{
    [[vk::location(0)]] float4 OutColor : SV_Target0;
};

[shader("fragment")]
FragmentOutput main()
{
    FragmentOutput output;
    
    output.OutColor = float4(0, 0, 0, 0);
    output.OutColor.a = 0.15;
    
    return output;
}