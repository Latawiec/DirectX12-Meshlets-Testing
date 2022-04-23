

#define ROOT_SIG "CBV(b0),\
                  SRV(t0),\
                  SRV(t1)"

struct Constants
{
    float4x4 World;
    float4x4 WorldView;
    float4x4 WorldViewProj;
    uint     DrawMeshlets;
    uint     IndicesCount;
    uint     VerticesCount;
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

ConstantBuffer<Constants>   Globals     : register(b0);

StructuredBuffer<Vertex>    Vertices   : register(t0);
StructuredBuffer<uint>      Indices    : register(t1);

#define GROUP_SIZE_X 32

[RootSignature(ROOT_SIG)]
[NumThreads(GROUP_SIZE_X, 1, 1)]
[OutputTopology("triangle")]
void main(
    uint gtid : SV_GroupThreadID,
    uint gid : SV_GroupID,
    out indices uint3 tris[GROUP_SIZE_X],
    out vertices VertexOut verts[3 * GROUP_SIZE_X]
)
{
    const uint executionOffset = gid * GROUP_SIZE_X + gtid;

    SetMeshOutputCounts(3 * GROUP_SIZE_X, GROUP_SIZE_X);

    if ( executionOffset < Globals.IndicesCount ) {
        // just write directly to output... I guess...
        tris[gtid] = 
            uint3(3 * gtid + 0, 3 * gtid + 1, 3 * gtid + 2);

        for (uint i = 0; i < 3; i++ ) {
            Vertex v = Vertices[Indices[3 * executionOffset + i]];

            VertexOut vout;
            vout.PositionVS = mul(float4(v.Position, 1), Globals.WorldView).xyz;
            vout.PositionHS = mul(float4(v.Position, 1), Globals.WorldViewProj);
            vout.Normal = mul(float4(v.Normal, 0), Globals.World).xyz;
            vout.GroupIndex = gid;

            verts[3 * gtid + i] = vout;
        }
    }

}