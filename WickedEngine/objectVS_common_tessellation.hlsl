#include "objectHF.hlsli"


struct HullInputType
{
	float4 pos								: POSITION;
	float4 color							: COLOR;
	float4 uvsets							: UVSETS;
	float4 atlas							: ATLAS;
	float4 nor								: NORMAL;
	float4 tan								: TANGENT;
	float4 posPrev							: POSITIONPREV;
	uint emissiveColor						: EMISSIVECOLOR;
};


HullInputType main(Input_Object_ALL input)
{
	HullInputType Out;
	
	float4x4 WORLD = MakeWorldMatrixFromInstance(input.inst);
	float4x4 WORLDPREV = MakeWorldMatrixFromInstance(input.instPrev);

	VertexSurface surface;
	surface.create(g_xMaterial, input);

	surface.position = mul(WORLD, surface.position);
	surface.positionPrev = mul(WORLDPREV, surface.positionPrev);
	surface.normal = normalize(mul((float3x3)WORLD, surface.normal));
	surface.tangent.xyz = normalize(mul((float3x3)WORLD, surface.tangent.xyz));

	Out.pos = surface.position;
	Out.color = surface.color;
	Out.uvsets = surface.uvsets;
	Out.atlas = surface.atlas.xyxy;
	Out.nor = float4(surface.normal, 1);
	Out.tan = surface.tangent;
	Out.posPrev = surface.positionPrev;
	Out.emissiveColor = surface.emissiveColor;

	return Out;
}
