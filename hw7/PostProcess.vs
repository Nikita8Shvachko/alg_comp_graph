struct VSOutput
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD;
};

VSOutput main(uint vertexId : SV_VertexID)
{
    VSOutput output;
    if (vertexId == 0)
    {
        output.pos = float4(-1.0f, -1.0f, 0.0f, 1.0f);
        output.uv = float2(0.0f, 1.0f);
    }
    else if (vertexId == 1)
    {
        output.pos = float4(3.0f, -1.0f, 0.0f, 1.0f);
        output.uv = float2(2.0f, 1.0f);
    }
    else
    {
        output.pos = float4(-1.0f, 3.0f, 0.0f, 1.0f);
        output.uv = float2(0.0f, -1.0f);
    }
    return output;
}
