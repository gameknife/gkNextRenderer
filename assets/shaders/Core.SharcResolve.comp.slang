import Common;
import Bindless;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    SharcResolveOfficialEntry(DTid.x);
}

