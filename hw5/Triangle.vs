cbuffer ModelBuffer : register(b0)
{
    float4x4 model;
};

cbuffer SceneBuffer : register(b1)
{
    float4x4 vp;
};

struct VSInput
{
    float3 pos : POSITION;
    float2 uv : TEXCOORD;
};

struct VSOutput
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD;
};

VSOutput main(VSInput vertex)
{
    VSOutput result;
    result.pos = mul(mul(float4(vertex.pos, 1.0f), model), vp);
    result.uv = vertex.uv;
    return result;
}
