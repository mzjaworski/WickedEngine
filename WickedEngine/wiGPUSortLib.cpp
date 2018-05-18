#include "wiGPUSortLib.h"
#include "wiRenderer.h"
#include "wiResourceManager.h"
#include "ShaderInterop_GPUSortLib.h"

using namespace wiGraphicsTypes;

GPUBuffer* wiGPUSortLib::indirectBuffer = nullptr;
GPUBuffer* wiGPUSortLib::sortCB = nullptr;
ComputeShader* wiGPUSortLib::kickoffSortCS = nullptr;
ComputeShader* wiGPUSortLib::sortCS = nullptr;
ComputeShader* wiGPUSortLib::sortInnerCS = nullptr;
ComputeShader* wiGPUSortLib::sortStepCS = nullptr;
ComputePSO wiGPUSortLib::CPSO_kickoffSort;
ComputePSO wiGPUSortLib::CPSO_sort;
ComputePSO wiGPUSortLib::CPSO_sortInner;
ComputePSO wiGPUSortLib::CPSO_sortStep;

void wiGPUSortLib::Initialize()
{
	GPUBufferDesc bd;

	bd.Usage = USAGE_DYNAMIC;
	bd.CPUAccessFlags = CPU_ACCESS_WRITE;
	bd.BindFlags = BIND_CONSTANT_BUFFER;
	bd.MiscFlags = 0;
	bd.ByteWidth = sizeof(SortConstants);
	sortCB = new GPUBuffer;
	wiRenderer::GetDevice()->CreateBuffer(&bd, nullptr, sortCB);


	bd.Usage = USAGE_DEFAULT;
	bd.CPUAccessFlags = 0;
	bd.BindFlags = BIND_UNORDERED_ACCESS;
	bd.MiscFlags = RESOURCE_MISC_DRAWINDIRECT_ARGS | RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
	bd.ByteWidth = sizeof(IndirectDispatchArgs);
	indirectBuffer = new GPUBuffer;
	wiRenderer::GetDevice()->CreateBuffer(&bd, nullptr, indirectBuffer);

}

void wiGPUSortLib::LoadShaders()
{
	GraphicsDevice* device = wiRenderer::GetDevice();

	kickoffSortCS = static_cast<ComputeShader*>(wiResourceManager::GetShaderManager()->add(wiRenderer::SHADERPATH + "gpusortlib_kickoffSortCS.cso", wiResourceManager::COMPUTESHADER));
	sortCS = static_cast<ComputeShader*>(wiResourceManager::GetShaderManager()->add(wiRenderer::SHADERPATH + "gpusortlib_sortCS.cso", wiResourceManager::COMPUTESHADER));
	sortInnerCS = static_cast<ComputeShader*>(wiResourceManager::GetShaderManager()->add(wiRenderer::SHADERPATH + "gpusortlib_sortInnerCS.cso", wiResourceManager::COMPUTESHADER));
	sortStepCS = static_cast<ComputeShader*>(wiResourceManager::GetShaderManager()->add(wiRenderer::SHADERPATH + "gpusortlib_sortStepCS.cso", wiResourceManager::COMPUTESHADER));



	ComputePSODesc desc;

	desc.cs = kickoffSortCS;
	device->CreateComputePSO(&desc, &CPSO_kickoffSort);

	desc.cs = sortCS;
	device->CreateComputePSO(&desc, &CPSO_sort);

	desc.cs = sortInnerCS;
	device->CreateComputePSO(&desc, &CPSO_sortInner);

	desc.cs = sortStepCS;
	device->CreateComputePSO(&desc, &CPSO_sortStep);
}

void wiGPUSortLib::CleanUpStatic()
{
}


void wiGPUSortLib::Sort(UINT maxCount, GPUBuffer* comparisonBuffer_read, GPUBuffer* counterBuffer_read, UINT counterReadOffset, GPUBuffer* indexBuffer_write, GRAPHICSTHREAD threadID)
{
	static bool init = false;
	if (!init)
	{
		Initialize();
		init = true;
	}


	GraphicsDevice* device = wiRenderer::GetDevice();

	device->EventBegin("GPUSortLib", threadID);


	SortConstants sc;
	sc.counterReadOffset = counterReadOffset;
	device->UpdateBuffer(sortCB, &sc, threadID);
	device->BindConstantBuffer(CS, sortCB, CB_GETBINDSLOT(SortConstants), threadID);


	// initialize sorting arguments:
	{
		device->BindComputePSO(&CPSO_kickoffSort, threadID);

		GPUResource* res[] = {
			counterBuffer_read,
		};
		device->BindResources(CS, res, 0, ARRAYSIZE(res), threadID);

		GPUResource* uavs[] = {
			indirectBuffer,
		};
		device->BindUnorderedAccessResourcesCS(uavs, 0, ARRAYSIZE(uavs), threadID);

		device->Dispatch(1, 1, 1, threadID);
		device->UAVBarrier(uavs, ARRAYSIZE(uavs), threadID);

		device->UnBindUnorderedAccessResources(0, ARRAYSIZE(uavs), threadID);
	}


	GPUResource* uavs[] = {
		indexBuffer_write,
	};
	device->BindUnorderedAccessResourcesCS(uavs, 0, ARRAYSIZE(uavs), threadID);

	GPUResource* resources[] = {
		counterBuffer_read,
		comparisonBuffer_read,
	};
	device->BindResources(CS, resources, 0, ARRAYSIZE(resources), threadID);

	// initial sorting:
	bool bDone = true;
	{
		// calculate how many threads we'll require:
		//   we'll sort 512 elements per CU (threadgroupsize 256)
		//     maybe need to optimize this or make it changeable during init
		//     TGS=256 is a good intermediate value

		unsigned int numThreadGroups = ((maxCount - 1) >> 9) + 1;

		assert(numThreadGroups <= 1024);

		if (numThreadGroups > 1)
		{
			bDone = false;
		}

		// sort all buffers of size 512 (and presort bigger ones)
		device->BindComputePSO(&CPSO_sort, threadID);
		device->DispatchIndirect(indirectBuffer, 0, threadID);
		device->UAVBarrier(uavs, ARRAYSIZE(uavs), threadID);
	}

	int presorted = 512;
	while (!bDone)
	{
		// Incremental sorting:

		bDone = true;
		device->BindComputePSO(&CPSO_sortStep, threadID);

		// prepare thread group description data
		uint32_t numThreadGroups = 0;

		if (maxCount > (uint32_t)presorted)
		{
			if (maxCount > (uint32_t)presorted * 2)
				bDone = false;

			uint32_t pow2 = presorted;
			while (pow2 < maxCount)
				pow2 *= 2;
			numThreadGroups = pow2 >> 9;
		}

		uint32_t nMergeSize = presorted * 2;
		for (uint32_t nMergeSubSize = nMergeSize >> 1; nMergeSubSize > 256; nMergeSubSize = nMergeSubSize >> 1)
		{
			SortConstants sc;
			sc.job_params.x = nMergeSubSize;
			if (nMergeSubSize == nMergeSize >> 1)
			{
				sc.job_params.y = (2 * nMergeSubSize - 1);
				sc.job_params.z = -1;
			}
			else
			{
				sc.job_params.y = nMergeSubSize;
				sc.job_params.z = 1;
			}
			sc.counterReadOffset = counterReadOffset;

			device->UpdateBuffer(sortCB, &sc, threadID);
			device->BindConstantBuffer(CS, sortCB, CB_GETBINDSLOT(SortConstants), threadID);

			device->Dispatch(numThreadGroups, 1, 1, threadID);
			device->UAVBarrier(uavs, ARRAYSIZE(uavs), threadID);
		}

		device->BindComputePSO(&CPSO_sortInner, threadID);
		device->Dispatch(numThreadGroups, 1, 1, threadID);
		device->UAVBarrier(uavs, ARRAYSIZE(uavs), threadID);

		presorted *= 2;
	}

	device->UnBindUnorderedAccessResources(0, ARRAYSIZE(uavs), threadID);
	device->UnBindResources(0, ARRAYSIZE(resources), threadID);


	device->EventEnd(threadID);
}
