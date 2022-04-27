

#define ROOT_SIG "CBV(b0),\
                  SRV(t0),\
                  SRV(t1),\
                  SRV(t2),\
                  SRV(t3)"

struct Constants
{
    float4x4 World;
    float4x4 WorldView;
    float4x4 WorldViewProj;
    uint     DrawMeshlets;
    uint     IndicesCount;
    uint     VerticesCount;
};

struct Meshlet
{
    uint VertCount;
    uint VertOffset;
    uint PrimCount;
    uint PrimOffset;
};

struct Vertex
{
    float3 Position;
    float3 Normal;
    float2 TexCoord;
};

struct VertexOut
{
    float4 PositionHS   : SV_Position;
    float3 PositionVS   : POSITION0;
    float3 Normal       : NORMAL0;
    uint   GroupIndex   : COLOR0;
};

uint3 DecodePrimitiveIndices(uint primitive)
{
    return uint3(primitive & 0x3FF, (primitive >> 10) & 0x3FF, (primitive >> 20) & 0x3FF);
}

ConstantBuffer<Constants>   Globals     : register(b0);

StructuredBuffer<Vertex>    Vertices            : register(t0);
StructuredBuffer<Meshlet>   Meshlets            : register(t1);
ByteAddressBuffer           UniqueVertexIndices : register(t2);
StructuredBuffer<uint>      PrimitiveIndices    : register(t3);

#define GROUP_SIZE_X 128

[RootSignature(ROOT_SIG)]
[NumThreads(GROUP_SIZE_X, 1, 1)]
[OutputTopology("triangle")]
void main(
    uint gtid : SV_GroupThreadID,
    uint gid : SV_GroupID,
    out indices uint3 tris[GROUP_SIZE_X],
    out vertices VertexOut verts[GROUP_SIZE_X]
)
{
    const Meshlet meshlet = Meshlets[gid];
    SetMeshOutputCounts(meshlet.VertCount, meshlet.PrimCount);

    if (gtid < meshlet.PrimCount)
    {
        tris[gtid] = DecodePrimitiveIndices(PrimitiveIndices[meshlet.PrimOffset + gtid]);
    }

    if (gtid < meshlet.VertCount) 
    {
        uint localIndex = meshlet.VertOffset + gtid;
        uint vertexIndex = UniqueVertexIndices.Load(localIndex * 4); // 4 because we assume uint_32 indices

        Vertex v = Vertices[vertexIndex];

        VertexOut vout;
        vout.PositionVS = mul(float4(v.Position, 1), Globals.WorldView).xyz;
        vout.PositionHS = mul(float4(v.Position, 1), Globals.WorldViewProj);
        vout.Normal = mul(float4(v.Normal, 0), Globals.World).xyz;
        vout.GroupIndex = gid;

        verts[gtid] = vout;
    }
}