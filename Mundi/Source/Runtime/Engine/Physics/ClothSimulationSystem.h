#pragma once

#include "UEContainer.h"

// Forward declarations
namespace nv { namespace cloth {
    class Factory;
    class Solver;
    class Cloth;
}}

class UClothComponent;

/**
 * @brief Cloth 시뮬레이션 시스템
 *
 * NvCloth Factory와 Solver를 관리하고, 등록된 모든 Cloth를 시뮬레이션합니다.
 * World가 소유하며 PhysScene과 유사한 수명 주기를 가집니다.
 */
class FClothSimulationSystem
{
public:
    FClothSimulationSystem();
    ~FClothSimulationSystem();

    // 복사/이동 금지
    FClothSimulationSystem(const FClothSimulationSystem&) = delete;
    FClothSimulationSystem& operator=(const FClothSimulationSystem&) = delete;

    /**
     * @brief 시스템 초기화
     * @return 성공 여부
     */
    bool Initialize();

    /**
     * @brief 시스템 종료
     */
    void Shutdown();

    /**
     * @brief 매 프레임 시뮬레이션 실행
     * @param DeltaTime 프레임 시간
     */
    void Simulate(float DeltaTime);

    /**
     * @brief Factory 가져오기
     */
    nv::cloth::Factory* GetFactory() const { return Factory; }

    /**
     * @brief Solver 가져오기
     */
    nv::cloth::Solver* GetSolver() const { return Solver; }

    /**
     * @brief Cloth를 Solver에 등록
     */
    void RegisterCloth(nv::cloth::Cloth* Cloth);

    /**
     * @brief Cloth를 Solver에서 해제
     */
    void UnregisterCloth(nv::cloth::Cloth* Cloth);

    /**
     * @brief 시스템이 초기화되었는지 확인
     */
    bool IsInitialized() const { return bInitialized; }

private:
    nv::cloth::Factory* Factory = nullptr;
    nv::cloth::Solver* Solver = nullptr;

    TArray<nv::cloth::Cloth*> RegisteredCloths;

    bool bInitialized = false;
};
