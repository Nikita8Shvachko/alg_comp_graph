cbuffer ModelBuffer : register(b0)
{
    float4x4 model;
    float4 material;
};

cbuffer SceneBuffer : register(b1)
{
    float4x4 vp;
    float4 cameraPos;
    float4 ambientColor;
    int4 lightCount;
};

struct VSInput
{
    float3 pos : POSITION;
    float3 tangent : TANGENT;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD;
};

struct VSOutput
{
    float4 pos : SV_Position;
    float3 worldPos : POSITION1;
    float3 worldTangent : TANGENT;
    float3 worldNormal : NORMAL;
    float2 uv : TEXCOORD;
};

VSOutput main(VSInput vertex)
{
    VSOutput result;
    float4 worldPos = mul(float4(vertex.pos, 1.0f), model);
    result.pos = mul(worldPos, vp);
    result.worldPos = worldPos.xyz;

    // For rigid transforms (rotation + translation), normal matrix equals model's 3x3 part.
    float3x3 normalMatrix = (float3x3)model;
    result.worldTangent = mul(vertex.tangent, normalMatrix);
    result.worldNormal = mul(vertex.normal, normalMatrix);
    result.uv = vertex.uv;
    return result;
}
