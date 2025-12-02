#include "pch.h"
#include "ClothSimulationSystem.h"

// NvCloth includes
#include <NvCloth/Factory.h>
#include <NvCloth/Solver.h>
#include <NvCloth/Cloth.h>
#include <NvCloth/Callbacks.h>

// PhysX Callbacks (NvCloth 초기화에 필요)
class FNvClothAllocatorCallback : public physx::PxAllocatorCallback
{
public:
    void* allocate(size_t size, const char* typeName, const char* filename, int line) override
    {
        return _aligned_malloc(size, 16);
    }

    void deallocate(void* ptr) override
    {
        _aligned_free(ptr);
    }
};

class FNvClothErrorCallback : public physx::PxErrorCallback
{
public:
    void reportError(physx::PxErrorCode::Enum code, const char* message, const char* file, int line) override
    {
        // TODO: 엔진 로그 시스템 연동
        OutputDebugStringA("[NvCloth Error] ");
        OutputDebugStringA(message);
        OutputDebugStringA("\n");
    }
};

class FNvClothAssertHandler : public nv::cloth::PxAssertHandler
{
public:
    void operator()(const char* exp, const char* file, int line, bool& ignore) override
    {
        OutputDebugStringA("[NvCloth Assert] ");
        OutputDebugStringA(exp);
        OutputDebugStringA("\n");
    }
};

// 전역 콜백 인스턴스
static FNvClothAllocatorCallback GNvClothAllocator;
static FNvClothErrorCallback GNvClothErrorCallback;
static FNvClothAssertHandler GNvClothAssertHandler;
static bool GNvClothInitialized = false;

FClothSimulationSystem::FClothSimulationSystem()
{
}

FClothSimulationSystem::~FClothSimulationSystem()
{
    Shutdown();
}

bool FClothSimulationSystem::Initialize()
{
    if (bInitialized)
    {
        return true;
    }

    // NvCloth 라이브러리 초기화 (한 번만)
    if (!GNvClothInitialized)
    {
        nv::cloth::InitializeNvCloth(&GNvClothAllocator, &GNvClothErrorCallback, &GNvClothAssertHandler, nullptr);
        GNvClothInitialized = true;
    }

    // CPU Factory 생성
    Factory = NvClothCreateFactoryCPU();
    if (Factory == nullptr)
    {
        return false;
    }

    // Solver 생성
    Solver = Factory->createSolver();
    if (Solver == nullptr)
    {
        NvClothDestroyFactory(Factory);
        Factory = nullptr;
        return false;
    }

    bInitialized = true;
    return true;
}

void FClothSimulationSystem::Shutdown()
{
    if (!bInitialized)
    {
        return;
    }

    // 등록된 모든 Cloth 해제
    for (nv::cloth::Cloth* Cloth : RegisteredCloths)
    {
        if (Solver != nullptr && Cloth != nullptr)
        {
            Solver->removeCloth(Cloth);
        }
    }
    RegisteredCloths.Empty();

    // Solver 해제
    if (Solver != nullptr)
    {
        delete Solver;
        Solver = nullptr;
    }

    // Factory 해제
    if (Factory != nullptr)
    {
        NvClothDestroyFactory(Factory);
        Factory = nullptr;
    }

    bInitialized = false;
}

void FClothSimulationSystem::Simulate(float DeltaTime)
{
    if (!bInitialized || Solver == nullptr)
    {
        return;
    }

    if (RegisteredCloths.Num() == 0)
    {
        return;
    }

    // 시뮬레이션 시작
    if (Solver->beginSimulation(DeltaTime))
    {
        // 청크 처리 (멀티스레드 확장 가능)
        int32 ChunkCount = Solver->getSimulationChunkCount();
        for (int32 i = 0; i < ChunkCount; i++)
        {
            Solver->simulateChunk(i);
        }

        // 시뮬레이션 종료
        Solver->endSimulation();
    }
}

void FClothSimulationSystem::RegisterCloth(nv::cloth::Cloth* Cloth)
{
    if (!bInitialized || Solver == nullptr || Cloth == nullptr)
    {
        return;
    }

    if (RegisteredCloths.Find(Cloth) == INDEX_NONE)
    {
        Solver->addCloth(Cloth);
        RegisteredCloths.Add(Cloth);
    }
}

void FClothSimulationSystem::UnregisterCloth(nv::cloth::Cloth* Cloth)
{
    if (Solver == nullptr || Cloth == nullptr)
    {
        return;
    }

    int32 Index = RegisteredCloths.Find(Cloth);
    if (Index != INDEX_NONE)
    {
        Solver->removeCloth(Cloth);
        RegisteredCloths.RemoveAt(Index);
    }
}
