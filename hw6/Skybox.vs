cbuffer SceneBuffer : register(b0)
{
    float4x4 vp;
    float4 cameraPos;
};

cbuffer GeomBuffer : register(b1)
{
    float4x4 model;
    float4 size;
};

struct VSInput
{
    float3 pos : POSITION;
};

struct VSOutput
{
    float4 pos : SV_Position;
    float3 localPos : POSITION1;
};

VSOutput main(VSInput vertex)
{
    VSOutput result;
    float3 worldPos = cameraPos.xyz + vertex.pos * size.x;
    float4 clipPos = mul(mul(float4(worldPos, 1.0f), model), vp);
    result.pos = float4(clipPos.xy, 0.0f, clipPos.w);
    result.localPos = vertex.pos;
    return result;
}
