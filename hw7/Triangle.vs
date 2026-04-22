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

struct GeomBufferData
{
    float4x4 model;
    float4x4 normalMatrix;
    float4 params;
};

cbuffer GeomBufferInst : register(b2)
{
    GeomBufferData geomBuffer[100];
};

cbuffer VisibleIndices : register(b3)
{
    uint4 visibleIds[100];
};

struct VSInput
{
    float3 pos : POSITION;
    float3 tangent : TANGENT;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD;
    uint instanceId : SV_InstanceID;
};

struct VSOutput
{
    float4 pos : SV_Position;
    float3 worldPos : POSITION1;
    float3 worldTangent : TANGENT;
    float3 worldNormal : NORMAL;
    float2 uv : TEXCOORD;
    nointerpolation uint instanceId : INST_ID;
    nointerpolation float shininess : SHININESS;
    nointerpolation float useNormalMap : USE_NM;
};

VSOutput main(VSInput vertex)
{
    VSOutput result;
    uint realInstanceId = visibleIds[min(vertex.instanceId, 99)].x;
    GeomBufferData inst = geomBuffer[min(realInstanceId, 99)];
    float4 worldPos = mul(float4(vertex.pos, 1.0f), inst.model);
    result.pos = mul(worldPos, vp);
    result.worldPos = worldPos.xyz;
    float3x3 normalMatrix = (float3x3)inst.normalMatrix;
    result.worldTangent = mul(vertex.tangent, normalMatrix);
    result.worldNormal = mul(vertex.normal, normalMatrix);
    result.uv = vertex.uv;
    result.instanceId = realInstanceId;
    result.shininess = inst.params.x;
    result.useNormalMap = inst.params.w;
    return result;
}
